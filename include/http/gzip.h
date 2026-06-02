#ifndef HTTP_GZIP_H
#define HTTP_GZIP_H

#include <stddef.h>

#ifdef HAVE_ZLIB
long gzip_compress(const char *src, size_t src_len,
                   char **dst_out, size_t *dst_len_out);
int  gzip_should_compress(const char *mime);
#endif

#endif
