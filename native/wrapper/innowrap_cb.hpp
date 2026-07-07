#ifndef INNOWRAP_CB_HPP
#define INNOWRAP_CB_HPP

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <ios>

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/read.hpp>

/*
 * Header (file-list) decompression is delegated to the managed (C#) side so the
 * native library does not need to link any compression library.
 *
 * The callback is a small streaming protocol so that raw LZMA1 streams (which
 * carry no end-of-stream marker) work correctly: the parser pulls only as many
 * decompressed bytes as it needs and then closes, exactly like innoextract.
 *
 *   op 0 = OPEN : method (1 = zlib, 2 = inno LZMA1), `in`/`in_len` give the full
 *                 compressed block. Returns a handle >= 0, or < 0 on error.
 *   op 1 = READ : `handle` selects the decoder; fill up to `out_cap` bytes into
 *                 `out`. Returns the number of bytes produced (0 = end), or < 0.
 *   op 2 = CLOSE: `handle` selects the decoder to dispose. Returns 0.
 */
typedef int64_t (*innowrap_inflate_fn)(void * user, int op, int64_t handle, int method,
                                       const uint8_t * in, int64_t in_len,
                                       uint8_t * out, int64_t out_cap);

namespace innowrap {

// Set for the duration of a parse call (single-threaded per context).
extern innowrap_inflate_fn g_inflate;
extern void *              g_inflate_user;

} // namespace innowrap

namespace stream {

/*
 * A boost::iostreams input filter that buffers the (small, deframed) compressed
 * input, opens a managed decoder over it, and then streams decompressed bytes
 * on demand. Used only for the setup header blocks.
 */
class cs_inflate_filter : public boost::iostreams::multichar_input_filter {

	typedef boost::iostreams::multichar_input_filter base_type;

public:

	typedef base_type::char_type char_type;
	typedef base_type::category  category;

	explicit cs_inflate_filter(int method)
		: method_(method), opened_(false), eof_(false), handle_(-1) { }

	template <typename Source>
	std::streamsize read(Source & src, char * dest, std::streamsize n) {

		if(!innowrap::g_inflate) {
			throw std::ios_base::failure("no inflate callback set");
		}

		if(!opened_) {
			// Drain the entire (deframed) compressed stream from the source.
			std::vector<uint8_t> in;
			char buf[8192];
			std::streamsize got;
			while((got = boost::iostreams::read(src, buf, std::streamsize(sizeof(buf)))) != EOF) {
				in.insert(in.end(), buf, buf + got);
			}
			in_.swap(in);
			int64_t h = innowrap::g_inflate(innowrap::g_inflate_user, 0, -1, method_,
			                                in_.empty() ? NULL : &in_[0], int64_t(in_.size()),
			                                NULL, 0);
			if(h < 0) {
				throw std::ios_base::failure("managed inflate open failed");
			}
			handle_ = h;
			opened_ = true;
		}

		if(eof_) {
			return EOF;
		}

		int64_t produced = innowrap::g_inflate(innowrap::g_inflate_user, 1, handle_, method_,
		                                        NULL, 0,
		                                        reinterpret_cast<uint8_t *>(dest), int64_t(n));
		if(produced < 0) {
			throw std::ios_base::failure("managed inflate read failed");
		}
		if(produced == 0) {
			eof_ = true;
			return EOF;
		}
		return std::streamsize(produced);
	}

	template <typename Source>
	void close(const Source &) {
		if(opened_ && handle_ >= 0 && innowrap::g_inflate) {
			innowrap::g_inflate(innowrap::g_inflate_user, 2, handle_, method_, NULL, 0, NULL, 0);
		}
		in_.clear();
		in_.shrink_to_fit();
		opened_ = false;
		eof_ = false;
		handle_ = -1;
	}

private:
	int method_;
	bool opened_;
	bool eof_;
	int64_t handle_;
	std::vector<uint8_t> in_;
};

} // namespace stream

#endif
