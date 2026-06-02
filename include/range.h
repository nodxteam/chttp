#ifndef RANGE_H
#define RANGE_H

#include "io.h"

typedef struct {
    long start;
    long end;   /* inclusive; -1 means EOF */
    int  valid;
} RangeSpec;

/* Parse "bytes=START-END" from Range header */
RangeSpec range_parse(const char *header_val, long file_size);

/* Send 206 Partial Content */
void range_send(IoCtx *io, const char *path, const char *mime,
                const char *conn, const RangeSpec *r, long file_size);

#endif /* RANGE_H */
