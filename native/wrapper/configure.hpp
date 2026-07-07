#ifndef INNOEXTRACT_CONFIGURE_HPP
#define INNOEXTRACT_CONFIGURE_HPP

/*
 * Fixed configuration for the parser-only "innowrap" build.
 *
 * Compression (LZMA), decryption, iconv and the filesystem/console extras are
 * all disabled. The few settings that differ per compiler are selected here so
 * the same file works for GCC/Clang (Linux/Android/MinGW) and MSVC (Windows).
 */

/* --- byte swap -----------------------------------------------------------
 * util/endian.hpp checks the __builtin_bswap macros FIRST, so they must be 0
 * on MSVC (which has no __builtin_bswap*) to fall through to _byteswap_*. */
#if defined(__GNUC__) || defined(__clang__)
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP16 1
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP32 1
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP64 1
#else /* MSVC: use _byteswap_ushort/_ulong/_uint64 from endian.hpp */
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP16 0
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP32 0
  #define INNOEXTRACT_HAVE_BUILTIN_BSWAP64 0
#endif
#define INNOEXTRACT_HAVE_BSWAP_16 0
#define INNOEXTRACT_HAVE_BSWAP_32 0
#define INNOEXTRACT_HAVE_BSWAP_64 0

/* --- time ---------------------------------------------------------------
 * time.cpp is not compiled; parse_time() is provided in extras.cpp using
 * timegm() on POSIX and _mkgmtime() on Windows, so these can stay 0. */
#if defined(_WIN32)
  #define INNOEXTRACT_HAVE_TIMEGM 0
  #define INNOEXTRACT_HAVE_GMTIME_R 0
#else
  #define INNOEXTRACT_HAVE_TIMEGM 1
  #define INNOEXTRACT_HAVE_GMTIME_R 1
#endif

/* --- everything else off / minimal -------------------------------------- */
#define INNOEXTRACT_HAVE_ISATTY 0
#define INNOEXTRACT_HAVE_IOCTL 0
#define INNOEXTRACT_HAVE_UTIMENSAT 0
#define INNOEXTRACT_HAVE_DYNAMIC_UTIMENSAT 0
#define INNOEXTRACT_HAVE_AT_FDCWD 0
#define INNOEXTRACT_HAVE_UTIMES 0
#define INNOEXTRACT_HAVE_DLSYM 0
#define INNOEXTRACT_HAVE_ALIGNOF 1
#define INNOEXTRACT_HAVE_STD_CODECVT_UTF8_UTF16 1
#define INNOEXTRACT_HAVE_STD_UNIQUE_PTR 1
#define INNOEXTRACT_HAVE_DECRYPTION 0
#define INNOEXTRACT_HAVE_LZMA 0
#define INNOEXTRACT_HAVE_ICONV 0
#define INNOEXTRACT_HAVE_WIN32_CONV 0
#define INNOEXTRACT_HAVE_BUILTIN_CONV 1
#define INNOEXTRACT_HAVE_POSIX_SPAWNP 0
#define INNOEXTRACT_HAVE_UNISTD_ENVIRON 0
#define INNOEXTRACT_HAVE_FORK 0
#define INNOEXTRACT_HAVE_EXECVP 0
#define INNOEXTRACT_HAVE_WAITPID 0

#endif
