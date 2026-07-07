/* innowrap.h - C ABI for the parser-only innoextract wrapper.
 *
 * The library only PARSES an Inno Setup installer and returns per-file metadata.
 * The caller owns the installer stream (read callback) and performs ALL
 * decompression itself. Header-block decompression is delegated back to the
 * caller through the inflate callback (so the library links no compression lib).
 *
 * All lengths/offsets use fixed-width types so the ABI is identical on every
 * platform (Windows long is 32-bit; we never use C long across the boundary).
 */
#ifndef INNOWRAP_H
#define INNOWRAP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct innowrap_ctx innowrap_ctx;

/* Read `len` bytes at absolute `offset` into `buf`. Return bytes read, or <0. */
typedef int64_t (*innowrap_read_fn)(void *user, uint64_t offset, uint8_t *buf, int64_t len);

/* Streaming header-block inflate:
 *   op 0 OPEN  (method 1=zlib, 2=inno-LZMA1; in/in_len = full block) -> handle>=0 or <0
 *   op 1 READ  (handle; fill up to out_cap into out)                 -> bytes (0=EOF) or <0
 *   op 2 CLOSE (handle)                                              -> 0
 */
typedef int64_t (*innowrap_inflate_fn)(void *user, int op, int64_t handle, int method,
                                       const uint8_t *in, int64_t in_len,
                                       uint8_t *out, int64_t out_cap);

/* One logical installer file. For GOG Galaxy installers this is the reconstructed
 * file (e.g. root_fs2.vp), assembled from `part_count` parts. */
typedef struct {
    uint64_t size;          /* total final (uncompressed) size                    */
    int32_t  part_count;    /* number of parts to extract and concatenate (>= 1)  */
    int32_t  checksum_type; /* whole-file: 0 none 1 adler32 2 crc32 3 md5 4 sha1 5 sha256 */
    uint8_t  checksum[32];
} innowrap_file;

/* One part of a file: a chunk in the .exe (embedded) or a .bin slice (external). */
typedef struct {
    uint64_t chunk_offset;  /* chunk offset (its magic) within its first slice    */
    uint64_t chunk_size;    /* compressed chunk size (excl. 4-byte magic + salt)  */
    uint64_t file_offset;   /* offset of this part within the decompressed chunk  */
    uint64_t file_size;     /* pre-filter size within the decompressed chunk      */
    uint32_t first_slice;
    uint32_t last_slice;
    int32_t  compression;   /* chunk: 0 stored 1 zlib 2 bzip2 3 lzma1 4 lzma2 5 ? */
    int32_t  encryption;    /* 0 plaintext (others unsupported by this build)     */
    int32_t  filter;        /* part: 0 none 1 i4108 2 i5200 3 i5309 4 zlib        */
    int32_t  reserved;      /* 0                                                  */
} innowrap_part;

const char  *innowrap_version(void);
innowrap_ctx *innowrap_open(innowrap_read_fn read_cb, innowrap_inflate_fn inflate_cb,
                            void *user, uint64_t total_size, int *err);
int          innowrap_file_count(innowrap_ctx *ctx);
uint64_t     innowrap_data_offset(innowrap_ctx *ctx);    /* 0 => data is in external .bin slices */
uint32_t     innowrap_slices_per_disk(innowrap_ctx *ctx);/* for .bin naming; usually 1            */
const char  *innowrap_base_filename(innowrap_ctx *ctx);  /* fallback base name for .bin slices    */
int          innowrap_get_file(innowrap_ctx *ctx, int index, innowrap_file *out);
int          innowrap_get_part(innowrap_ctx *ctx, int file_index, int part_index, innowrap_part *out);
const char  *innowrap_file_name(innowrap_ctx *ctx, int index); /* lowercased basename, UTF-8 */
const char  *innowrap_file_path(innowrap_ctx *ctx, int index); /* full destination, UTF-8    */
void         innowrap_close(innowrap_ctx *ctx);

#ifdef __cplusplus
}
#endif
#endif
