#include "range.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RangeSpec range_parse(const char *header_val, long file_size) {
    RangeSpec r = {0, -1, 0};
    if (!header_val) return r;
    if (strncmp(header_val, "bytes=", 6) != 0) return r;

    const char *p    = header_val + 6;
    const char *dash = strchr(p, '-');
    if (!dash) return r;

    if (p == dash) {
        /* -N  →  last N bytes */
        long n = atol(dash + 1);
        if (n <= 0) return r;
        r.start = file_size - n;
        r.end   = file_size - 1;
    } else {
        r.start = atol(p);
        r.end   = (dash[1] != '\0') ? atol(dash + 1) : file_size - 1;
    }

    if (r.start < 0)           r.start = 0;
    if (r.end   >= file_size)  r.end   = file_size - 1;
    if (r.start > r.end) return r;

    r.valid = 1;
    return r;
}

void range_send(IoCtx *io, const char *path, const char *mime,
                const char *conn, const RangeSpec *r, long file_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return;

    long length = r->end - r->start + 1;

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Content-Range: bytes %ld-%ld/%ld\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: %s\r\n"
        "Server: chttp/1.0\r\n\r\n",
        mime, length, r->start, r->end, file_size, conn);

    io_write_all(io, header, (size_t)hlen);

    if (fseek(fp, r->start, SEEK_SET) != 0) { fclose(fp); return; }

    char buf[65536];
    long remaining = length;
    while (remaining > 0) {
        size_t want = (size_t)(remaining < (long)sizeof(buf)
                               ? remaining : (long)sizeof(buf));
        size_t got = fread(buf, 1, want, fp);
        if (got == 0) break;
        io_write_all(io, buf, got);
        remaining -= (long)got;
    }
    fclose(fp);
}
