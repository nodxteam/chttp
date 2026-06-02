#ifndef GZIP_H
#define GZIP_H

#include <stddef.h>

#ifdef HAVE_ZLIB

/* Compress src with gzip. Allocates *dst_out (caller must free).
   Returns compressed size, or -1 on error. */
long gzip_compress(const char *src, size_t src_len,
                   char **dst_out, size_t *dst_len_out);

/* Returns 1 if this MIME type is worth gzip-compressing */
int gzip_should_compress(const char *mime);

#endif /* HAVE_ZLIB */
#endif /* GZIP_H */
