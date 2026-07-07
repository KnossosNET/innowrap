// extras.cpp - minimal symbol definitions so we don't have to compile
// chunk.cpp / file.cpp / time.cpp (which pull in zlib/bzip2/filesystem).

#include <ctime>

#include "stream/chunk.hpp"
#include "stream/file.hpp"
#include "util/enum.hpp"
#include "util/time.hpp"

namespace stream {

bool chunk::operator<(const chunk & o) const {
	if(first_slice != o.first_slice) return (first_slice < o.first_slice);
	else if(sort_offset != o.sort_offset) return (sort_offset < o.sort_offset);
	else if(size != o.size) return (size < o.size);
	else if(compression != o.compression) return (compression < o.compression);
	else if(encryption != o.encryption) return (encryption < o.encryption);
	return false;
}
bool chunk::operator==(const chunk & o) const {
	return (first_slice == o.first_slice && sort_offset == o.sort_offset
	        && size == o.size && compression == o.compression && encryption == o.encryption);
}

bool file::operator<(const stream::file & o) const {
	if(offset != o.offset) return (offset < o.offset);
	else if(size != o.size) return (size < o.size);
	else if(filter != o.filter) return (filter < o.filter);
	return false;
}
bool file::operator==(const file & o) const {
	return (offset == o.offset && size == o.size && filter == o.filter);
}

} // namespace stream

NAMES(stream::compression_method, "Compression Method",
	"stored", "zlib", "bzip2", "lzma1", "lzma2", "unknown",
)
NAMES(stream::encryption_method, "Encryption Method",
	"plaintext", "rc4 + md5", "rc4 + sha1", "xchacha20",
)

namespace util {
time parse_time(std::tm tm) {
	tm.tm_isdst = 0;
#if defined(_WIN32)
	return _mkgmtime(&tm);
#else
	return timegm(&tm);
#endif
}
} // namespace util

#if defined(_WIN32)
/*
 * On Windows these live in util/windows.cpp, which we don't compile (it pulls in
 * <windows.h> and boost::filesystem). The parser never shows the progress UI, so
 * stub them out: reporting "not a console" disables the progress animation.
 */
namespace util {
int isatty(int /* fd */) { return 0; }
int console_width() { return 0; }
} // namespace util
#endif
