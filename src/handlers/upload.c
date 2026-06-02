#include "handlers/upload.h"
#include "http/response.h"
#include "http/parser.h"
#include "core/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static int extract_boundary(const char *ct, char *out, size_t sz) {
    const char *p = strstr(ct, "boundary=");
    if (!p) return 0;
    p += 9;
    if (*p == '"') p++;
    size_t i = 0;
    while (p[i] && p[i] != '"' && p[i] != ';' && p[i] != ' ' && i+1 < sz)
        out[i] = p[i++];
    out[i] = '\0';
    return i > 0;
}

static const char *find_part_hdr(const char *hdrs, size_t hlen,
                                   const char *name, char *out, size_t sz) {
    char buf[2048];
    size_t n = hlen < sizeof(buf)-1 ? hlen : sizeof(buf)-1;
    memcpy(buf, hdrs, n); buf[n] = '\0';
    char search[128]; snprintf(search, sizeof(search), "%s:", name);
    char *p = strcasestr(buf, search);
    if (!p) return NULL;
    p += strlen(search); while (*p == ' ') p++;
    char *end = strstr(p, "\r\n"); if (!end) end = p + strlen(p);
    size_t vl = (size_t)(end-p); if (vl >= sz) vl = sz-1;
    memcpy(out, p, vl); out[vl] = '\0';
    return out;
}

static void extract_filename(const char *cd, char *out, size_t sz) {
    out[0] = '\0';
    const char *p = strstr(cd, "filename=\""); if (!p) return; p += 10;
    size_t i = 0;
    while (p[i] && p[i] != '"' && i+1 < sz) out[i] = p[i++];
    out[i] = '\0';
}

static void sanitize_name(const char *in, char *out, size_t sz) {
    size_t di = 0;
    for (size_t i = 0; in[i] && di+1 < sz; i++) {
        char c = in[i];
        if (c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') continue;
        out[di++] = c;
    }
    out[di] = '\0';
    if (!di) snprintf(out, sz, "file_%ld", (long)time(NULL));
}

void handler_upload(RequestCtx *ctx) {
    const char *ct = http_get_header(&ctx->req, "Content-Type");
    if (!ct) { resp_error(ctx, 400); return; }

    char boundary[256];
    if (!extract_boundary(ct, boundary, sizeof(boundary))) {
        resp_error(ctx, 400); return;
    }

    const char *upload_dir = ctx->server->config->upload_dir;
    long max_bytes = (long)ctx->server->config->upload_max_mb * 1024 * 1024;

#ifdef _WIN32
    _mkdir(upload_dir);
#else
    mkdir(upload_dir, 0755);
#endif

    char delim[300];
    int dlen = snprintf(delim, sizeof(delim), "--%s", boundary);

    const char *body = ctx->req.body;
    size_t body_len  = ctx->req.body_len;
    int saved = 0;

    const char *cur = (const char *)memmem(body, body_len, delim, (size_t)dlen);
    if (!cur) goto respond;
    cur += dlen; if (cur[0] == '\r') cur += 2;

    const char *end = body + body_len;
    while (cur < end) {
        const char *next = (const char *)memmem(cur, (size_t)(end-cur), delim, (size_t)dlen);
        if (!next) break;
        const char *hdrs_end = (const char *)memmem(cur, (size_t)(next-cur), "\r\n\r\n", 4);
        if (!hdrs_end) { cur = next+dlen+2; continue; }

        char cd[512];
        if (!find_part_hdr(cur, (size_t)(hdrs_end-cur), "Content-Disposition", cd, sizeof(cd)))
            { cur = next+dlen+2; continue; }

        char filename[256]; extract_filename(cd, filename, sizeof(filename));
        if (!filename[0]) { cur = next+dlen+2; continue; }

        const char *fdata = hdrs_end + 4;
        const char *fend  = next - 2;
        if (fend <= fdata) { cur = next+dlen+2; continue; }
        size_t fsz = (size_t)(fend - fdata);

        if (max_bytes > 0 && (long)fsz > max_bytes) {
            resp_error(ctx, 413); return;
        }

        char safe[256]; sanitize_name(filename, safe, sizeof(safe));
        char out_path[MAX_PATH_LEN];
        snprintf(out_path, sizeof(out_path), "%s/%s", upload_dir, safe);
        FILE *fp = fopen(out_path, "wb");
        if (fp) { fwrite(fdata, 1, fsz, fp); fclose(fp); saved++; }

        cur = next + dlen;
        if (cur[0] == '-' && cur[1] == '-') break;
        if (cur[0] == '\r') cur += 2;
    }

respond:;
    char body_str[64];
    int blen = snprintf(body_str, sizeof(body_str),
                        "%d file(s) uploaded.", saved);
    resp_string(ctx, 200, "text/plain", body_str, blen);
}
