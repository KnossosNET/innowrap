/*
 * innowrap.cpp - minimal C ABI around innoextract's PARSER.
 *
 * The native side only PARSES the installer (loader offsets + setup headers +
 * file/data entries). It performs no file I/O (the managed side owns the stream
 * and feeds bytes through a read callback) and links no compression library
 * (header decompression is delegated to the managed side through an inflate
 * callback; payload extraction is done entirely in C#/SharpCompress using the
 * per-file metadata returned here).
 */

#include <cstdint>
#include <cstring>
#include <istream>
#include <streambuf>
#include <string>
#include <vector>

#include "innowrap_cb.hpp"

#include "loader/offsets.hpp"
#include "setup/info.hpp"
#include "setup/header.hpp"
#include "setup/file.hpp"
#include "setup/data.hpp"
#include "stream/chunk.hpp"
#include "stream/file.hpp"
#include "crypto/checksum.hpp"
#include "cli/goggalaxy.hpp"

namespace innowrap {
innowrap_inflate_fn g_inflate = NULL;
void *              g_inflate_user = NULL;
}

extern "C" {

typedef int64_t (*innowrap_read_fn)(void * user, uint64_t offset, uint8_t * buf, int64_t len);

#if defined(_WIN32)
#define INNOWRAP_API __declspec(dllexport)
#else
#define INNOWRAP_API __attribute__((visibility("default")))
#endif

// One logical installer file (for GOG Galaxy installers this is the reconstructed
// file, e.g. root_fs2.vp, made of many parts). Pointer-free, identical layout on
// 32/64-bit. Names/paths via innowrap_file_name()/innowrap_file_path().
struct innowrap_file {
	uint64_t     size;          // total final (uncompressed) size
	int32_t      part_count;    // number of parts to concatenate (>= 1)
	int32_t      checksum_type; // whole-file checksum: 0 none,1 adler32,2 crc32,3 md5,4 sha1,5 sha256
	uint8_t      checksum[32];
};

// One part of a file: a chunk in the .exe (embedded) or a .bin slice (external).
struct innowrap_part {
	uint64_t     chunk_offset;  // offset of the chunk (its magic) within its first slice
	uint64_t     chunk_size;    // compressed chunk size (excludes 4-byte magic + salt)
	uint64_t     file_offset;   // offset of this part within the DECOMPRESSED chunk
	uint64_t     file_size;     // pre-filter size within the decompressed chunk
	uint32_t     first_slice;
	uint32_t     last_slice;
	int32_t      compression;   // chunk: 0 stored,1 zlib,2 bzip2,3 lzma1,4 lzma2,5 unknown
	int32_t      encryption;    // 0 plaintext (others unsupported)
	int32_t      filter;        // per-part: 0 none,1 i4108,2 i5200,3 i5309,4 zlib
	int32_t      reserved;      // padding / future use (0)
};

} // extern "C"

namespace {

// std::streambuf backed by a managed read callback. Seekable, read-only.
class cb_streambuf : public std::streambuf {
public:
	cb_streambuf(innowrap_read_fn read_cb, void * user, uint64_t size)
		: read_cb_(read_cb), user_(user), size_(size), pos_(0) { }

protected:
	// Single-char fallback (rarely used; we mainly serve via xsgetn/seek).
	int_type underflow() {
		if(pos_ >= size_) return traits_type::eof();
		uint8_t b;
		int64_t n = read_cb_(user_, pos_, &b, 1);
		if(n != 1) return traits_type::eof();
		cur_ = char(b);
		setg(&cur_, &cur_, &cur_ + 1);
		return traits_type::to_int_type(cur_);
	}

	std::streamsize xsgetn(char * s, std::streamsize n) {
		std::streamsize total = 0;
		while(n > 0 && pos_ < size_) {
			int64_t want = int64_t(std::min<uint64_t>(uint64_t(n), size_ - pos_));
			int64_t got = read_cb_(user_, pos_, reinterpret_cast<uint8_t *>(s), want);
			if(got <= 0) break;
			pos_ += uint64_t(got);
			s += got; n -= got; total += got;
			setg(0, 0, 0); // invalidate the single-char buffer
		}
		return total;
	}

	pos_type seekoff(off_type off, std::ios_base::seekdir dir,
	                 std::ios_base::openmode which) {
		(void)which;
		uint64_t base = 0;
		if(dir == std::ios_base::beg) base = 0;
		else if(dir == std::ios_base::cur) base = pos_;
		else if(dir == std::ios_base::end) base = size_;
		int64_t np = int64_t(base) + int64_t(off);
		if(np < 0 || uint64_t(np) > size_) return pos_type(off_type(-1));
		pos_ = uint64_t(np);
		setg(0, 0, 0);
		return pos_type(off_type(pos_));
	}

	pos_type seekpos(pos_type sp, std::ios_base::openmode which) {
		return seekoff(off_type(sp), std::ios_base::beg, which);
	}

private:
	innowrap_read_fn read_cb_;
	void *           user_;
	uint64_t         size_;
	uint64_t         pos_;
	char             cur_;
};

struct logical_file {
	std::string name;   // lowercased basename
	std::string path;   // full destination
	uint64_t    size;
	int         checksum_type;
	uint8_t     checksum[32];
	std::vector<uint32_t> locations; // data_entry indices, in concatenation order
};

struct context {
	loader::offsets offsets;
	setup::info     info;
	std::vector<logical_file> lfiles;
};

std::string basename_lower(const std::string & p) {
	size_t s = p.find_last_of("\\/");
	std::string b = (s == std::string::npos) ? p : p.substr(s + 1);
	for(size_t i = 0; i < b.size(); i++) {
		char c = b[i];
		if(c >= 'A' && c <= 'Z') b[i] = char(c - 'A' + 'a');
	}
	return b;
}

int map_filter(stream::compression_filter f) {
	switch(f) {
		case stream::NoFilter:              return 0;
		case stream::InstructionFilter4108: return 1;
		case stream::InstructionFilter5200: return 2;
		case stream::InstructionFilter5309: return 3;
		case stream::ZlibFilter:            return 4;
	}
	return 0;
}

int map_checksum(crypto::checksum_type t) {
	switch(t) {
		case crypto::None:    return 0;
		case crypto::Adler32: return 1;
		case crypto::CRC32:   return 2;
		case crypto::MD5:     return 3;
		case crypto::SHA1:    return 4;
		case crypto::SHA256:  return 5;
		default:              return 0;
	}
}

} // namespace

extern "C" {

INNOWRAP_API const char * innowrap_version(void) { return "innowrap-1.0"; }

// err codes: 0 ok, -1 read/parse error
INNOWRAP_API context * innowrap_open(innowrap_read_fn read_cb,
                                     innowrap_inflate_fn inflate_cb,
                                     void * user, uint64_t total_size, int * err) {
	if(err) *err = 0;
	context * ctx = new (std::nothrow) context();
	if(!ctx) { if(err) *err = -1; return NULL; }

	innowrap::g_inflate = inflate_cb;
	innowrap::g_inflate_user = user;

	try {
		cb_streambuf sb(read_cb, user, total_size);
		std::istream is(&sb);
		is.exceptions(std::ios_base::badbit);

		ctx->offsets.load(is);

		is.clear();
		is.seekg(std::streamoff(ctx->offsets.header_offset));

		setup::info::entry_types entries = setup::info::Files | setup::info::DataEntries;
		ctx->info.load(is, entries);
		gog::parse_galaxy_files(ctx->info, true);

		// Build the logical file list. GOG Galaxy reconstruction sets a real
		// destination on the first part and clears it on the others (absorbed into
		// additional_locations). Non-GOG files are simply single-part.
		const size_t nde = ctx->info.data_entries.size();
		for(size_t i = 0; i < ctx->info.files.size(); i++) {
			const setup::file_entry & fe = ctx->info.files[i];
			if(fe.destination.empty()) continue;        // absorbed part / non-output entry
			if(fe.location >= nde) continue;            // no data (directory, etc.)

			logical_file lf;
			lf.path = fe.destination;
			lf.name = basename_lower(fe.destination);
			lf.size = fe.size;

			// Whole-file checksum if present (GOG sets it); else the single part's.
			const setup::data_entry & de0 = ctx->info.data_entries[fe.location];
			if(fe.checksum.type != crypto::None) {
				lf.checksum_type = map_checksum(fe.checksum.type);
				std::memcpy(lf.checksum, fe.checksum.sha256, 32);
			} else {
				lf.checksum_type = map_checksum(de0.file.checksum.type);
				std::memcpy(lf.checksum, de0.file.checksum.sha256, 32);
			}

			lf.locations.push_back(fe.location);
			for(size_t k = 0; k < fe.additional_locations.size(); k++) {
				if(fe.additional_locations[k] < nde)
					lf.locations.push_back(fe.additional_locations[k]);
			}
			if(lf.size == 0) {
				lf.size = 0;
				for(size_t k = 0; k < lf.locations.size(); k++)
					lf.size += ctx->info.data_entries[lf.locations[k]].uncompressed_size;
			}
			ctx->lfiles.push_back(lf);
		}
	} catch(...) {
		innowrap::g_inflate = NULL; innowrap::g_inflate_user = NULL;
		delete ctx;
		if(err) *err = -1;
		return NULL;
	}

	innowrap::g_inflate = NULL; innowrap::g_inflate_user = NULL;
	return ctx;
}

INNOWRAP_API int innowrap_file_count(context * ctx) {
	return ctx ? int(ctx->lfiles.size()) : 0;
}

INNOWRAP_API uint64_t innowrap_data_offset(context * ctx) {
	return ctx ? uint64_t(ctx->offsets.data_offset) : 0;
}

// Number of slices grouped per disk (for external .bin naming). 1 in the common case.
INNOWRAP_API uint32_t innowrap_slices_per_disk(context * ctx) {
	return ctx ? uint32_t(ctx->info.header.slices_per_disk) : 1;
}

// Fallback base filename stored in the header (basename2), or NULL/empty if unset.
INNOWRAP_API const char * innowrap_base_filename(context * ctx) {
	return ctx ? ctx->info.header.base_filename.c_str() : NULL;
}

INNOWRAP_API int innowrap_get_file(context * ctx, int index, innowrap_file * out) {
	if(!ctx || !out || index < 0 || size_t(index) >= ctx->lfiles.size()) return -1;
	const logical_file & lf = ctx->lfiles[size_t(index)];
	std::memset(out, 0, sizeof(*out));
	out->size = lf.size;
	out->part_count = int(lf.locations.size());
	out->checksum_type = lf.checksum_type;
	std::memcpy(out->checksum, lf.checksum, 32);
	return 0;
}

INNOWRAP_API int innowrap_get_part(context * ctx, int file_index, int part_index, innowrap_part * out) {
	if(!ctx || !out || file_index < 0 || size_t(file_index) >= ctx->lfiles.size()) return -1;
	const logical_file & lf = ctx->lfiles[size_t(file_index)];
	if(part_index < 0 || size_t(part_index) >= lf.locations.size()) return -1;
	uint32_t loc = lf.locations[size_t(part_index)];
	if(loc >= ctx->info.data_entries.size()) return -1;
	const setup::data_entry & de = ctx->info.data_entries[loc];

	std::memset(out, 0, sizeof(*out));
	out->first_slice  = de.chunk.first_slice;
	out->last_slice   = de.chunk.last_slice;
	out->chunk_offset = de.chunk.offset;
	out->chunk_size   = de.chunk.size;
	out->compression  = int(de.chunk.compression);
	out->encryption   = int(de.chunk.encryption);
	out->file_offset  = de.file.offset;
	out->file_size    = de.file.size;
	out->filter       = map_filter(de.file.filter);
	return 0;
}

INNOWRAP_API void innowrap_close(context * ctx) { delete ctx; }

// Lowercased basename (UTF-8), or NULL if the index is out of range.
INNOWRAP_API const char * innowrap_file_name(context * ctx, int index) {
	if(!ctx || index < 0 || size_t(index) >= ctx->lfiles.size()) return NULL;
	return ctx->lfiles[size_t(index)].name.c_str();
}

// Full destination path as stored (UTF-8), or NULL if the index is out of range.
INNOWRAP_API const char * innowrap_file_path(context * ctx, int index) {
	if(!ctx || index < 0 || size_t(index) >= ctx->lfiles.size()) return NULL;
	return ctx->lfiles[size_t(index)].path.c_str();
}

} // extern "C"
