#include "upload.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* Extract boundary= value from Content-Type header */
static int extract_boundary(const char *ct, char *out, size_t sz) {
    const char *p = strstr(ct, "boundary=");
    if (!p) return 0;
    p += 9;
    /* skip optional quote */
    if (*p == '"') p++;
    size_t i = 0;
    while (p[i] && p[i] != '"' && p[i] != ';' && p[i] != ' ' && i + 1 < sz)
        out[i] = p[i++];
    out[i] = '\0';
    return i > 0;
}

/* Find header value within a part's header block */
static const char *find_part_header(const char *hdrs, size_t hlen,
                                     const char *name,
                                     char *out, size_t out_sz) {
    char buf[2048];
    size_t n = hlen < sizeof(buf)-1 ? hlen : sizeof(buf)-1;
    memcpy(buf, hdrs, n); buf[n] = '\0';

    char search[128];
    snprintf(search, sizeof(search), "%s:", name);
    char *p = strcasestr(buf, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ') p++;
    char *end = strstr(p, "\r\n");
    if (!end) end = p + strlen(p);
    size_t vlen = (size_t)(end - p);
    if (vlen >= out_sz) vlen = out_sz - 1;
    memcpy(out, p, vlen);
    out[vlen] = '\0';
    return out;
}

/* Extract filename="..." from Content-Disposition */
static void extract_filename(const char *cd, char *out, size_t sz) {
    out[0] = '\0';
    const char *p = strstr(cd, "filename=\"");
    if (!p) { p = strstr(cd, "filename*=UTF-8''"); if (!p) return; p += 17; }
    else p += 10;
    size_t i = 0;
    while (p[i] && p[i] != '"' && i + 1 < sz) out[i] = p[i++];
    out[i] = '\0';
}

/* Remove path separators and dangerous chars */
static void sanitize_name(const char *in, char *out, size_t sz) {
    size_t di = 0;
    for (size_t i = 0; in[i] && di + 1 < sz; i++) {
        char c = in[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        out[di++] = c;
    }
    out[di] = '\0';
    if (di == 0) snprintf(out, sz, "file_%ld", (long)time(NULL));
}

int upload_handle(IoCtx *io,
                  const char *body, size_t body_len,
                  const char *content_type,
                  const char *upload_dir,
                  long max_bytes,
                  const char *conn) {
    char boundary[256];
    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        const char *r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        io_write_all(io, r, strlen(r));
        return -1;
    }

    /* Ensure upload directory exists */
#ifdef _WIN32
    _mkdir(upload_dir);
#else
    mkdir(upload_dir, 0755);
#endif

    char delim[300];
    int dlen = snprintf(delim, sizeof(delim), "--%s", boundary);

    int saved = 0;
    const char *p   = body;
    const char *end = body + body_len;

    /* Seek to first boundary */
    const char *cur = (const char *)memmem(p, (size_t)(end - p),
                                            delim, (size_t)dlen);
    if (!cur) goto respond;
    cur += dlen;
    if (cur[0] == '\r') cur += 2;

    while (cur < end) {
        /* Find next boundary */
        const char *next = (const char *)memmem(cur, (size_t)(end - cur),
                                                  delim, (size_t)dlen);
        if (!next) break;

        /* Split headers / body at \r\n\r\n */
        const char *hdrs_end = (const char *)memmem(cur, (size_t)(next - cur),
                                                      "\r\n\r\n", 4);
        if (!hdrs_end) { cur = next + dlen + 2; continue; }

        size_t hdrs_len = (size_t)(hdrs_end - cur);
        const char *part_body     = hdrs_end + 4;
        const char *part_body_end = next - 2;  /* strip trailing \r\n */
        if (part_body_end <= part_body) { cur = next + dlen + 2; continue; }
        size_t part_body_len = (size_t)(part_body_end - part_body);

        /* Get Content-Disposition */
        char cd[512];
        if (!find_part_header(cur, hdrs_len, "Content-Disposition",
                               cd, sizeof(cd))) {
            cur = next + dlen + 2; continue;
        }

        char filename[256];
        extract_filename(cd, filename, sizeof(filename));
        if (!filename[0]) { cur = next + dlen + 2; continue; }

        /* Size check */
        if (max_bytes > 0 && (long)part_body_len > max_bytes) {
            const char *r = "HTTP/1.1 413 Payload Too Large\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
            io_write_all(io, r, strlen(r));
            return -1;
        }

        /* Save file */
        char safe[256];
        sanitize_name(filename, safe, sizeof(safe));
        char out_path[MAX_PATH_LEN];
        snprintf(out_path, sizeof(out_path), "%s/%s", upload_dir, safe);

        FILE *fp = fopen(out_path, "wb");
        if (fp) {
            fwrite(part_body, 1, part_body_len, fp);
            fclose(fp);
            saved++;
        }

        cur = next + dlen;
        if (cur[0] == '-' && cur[1] == '-') break; /* final -- */
        if (cur[0] == '\r') cur += 2;
    }

respond:;
    char resp_body[64];
    int blen = snprintf(resp_body, sizeof(resp_body),
                        "%d file(s) uploaded successfully.", saved);
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "Server: chttp/1.0\r\n\r\n",
        blen, conn);
    io_write_all(io, header, (size_t)hlen);
    io_write_all(io, resp_body, (size_t)blen);
    return saved;
}
