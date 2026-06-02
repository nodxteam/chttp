#ifndef HTTP_RANGE_H
#define HTTP_RANGE_H

#include "net/io.h"

typedef struct { long start; long end; int valid; } RangeSpec;

RangeSpec range_parse(const char *header_val, long file_size);
void      range_send(IoCtx *io, const char *path, const char *mime,
                     const char *conn, const RangeSpec *r, long file_size);

#endif
