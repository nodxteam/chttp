#include "http/response.h"
#include "http/parser.h"
#include "http/mime.h"
#include "http/etag.h"
#include "http/range.h"
#include "http/gzip.h"
#include "core/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <io.h>
#else
  #include <sys/stat.h>
  #include <dirent.h>
#endif

#define SEND_BUF 65536

/* ── Internals ───────────────────────────────────────────────────────────── */

static void write_header(RequestCtx *ctx, int status, const char *mime,
                          long content_len, const char *extra) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: %s\r\n"
        "%s"
        "Server: chttp/1.0\r\n\r\n",
        status, http_status_text(status),
        mime, content_len, ctx->conn,
        extra ? extra : "");
    io_write_all(&ctx->io, header, (size_t)hlen);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void resp_string(RequestCtx *ctx, int status, const char *mime,
                 const char *body, int body_len) {
    write_header(ctx, status, mime, body_len, NULL);
    if (body_len > 0) io_write_all(&ctx->io, body, (size_t)body_len);
    ctx->response_sent = 1;
}

void resp_error(RequestCtx *ctx, int code) {
    /* Try custom error page: www/404.html etc. */
    char err_path[MAX_PATH_LEN];
    snprintf(err_path, sizeof(err_path), "%s/%d.html", ctx->doc_root, code);
    struct stat st;
    if (stat(err_path, &st) == 0 && S_ISREG(st.st_mode)) {
        FILE *fp = fopen(err_path, "rb");
        if (fp) {
            char header[512];
            int hlen = snprintf(header, sizeof(header),
                "HTTP/1.1 %d %s\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: %ld\r\n"
                "Connection: %s\r\nServer: chttp/1.0\r\n\r\n",
                code, http_status_text(code), (long)st.st_size, ctx->conn);
            io_write_all(&ctx->io, header, (size_t)hlen);
            char buf[SEND_BUF]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
                io_write_all(&ctx->io, buf, n);
            fclose(fp);
            ctx->response_sent = 1;
            return;
        }
    }

    const char *text = http_status_text(code);
    char body[256];
    int blen = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><hr><em>chttp/1.0</em></body></html>",
        code, text, code, text);
    resp_string(ctx, code, "text/html; charset=utf-8", body, blen);
}

void resp_redirect(RequestCtx *ctx, int code, const char *location,
                   const char *extra_headers) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: %s\r\n"
        "%s"
        "Server: chttp/1.0\r\n\r\n",
        code, http_status_text(code), location, ctx->conn,
        extra_headers ? extra_headers : "");
    io_write_all(&ctx->io, header, (size_t)hlen);
    ctx->response_sent = 1;
}

long resp_file(RequestCtx *ctx, const char *path, const char *mime, int head_only) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;

    /* ETag */
    char etag[64];
    etag_generate(&st, etag, sizeof(etag));

    /* 304 Not Modified */
    const char *inm = http_get_header(&ctx->req, "If-None-Match");
    if (inm && etag_matches(inm, etag)) {
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 304 Not Modified\r\n"
            "ETag: %s\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
            etag, ctx->conn);
        io_write_all(&ctx->io, header, (size_t)hlen);
        ctx->response_sent = 1;
        return 0;
    }

    /* Range request */
    const char *rng = http_get_header(&ctx->req, "Range");
    if (rng) {
        RangeSpec r = range_parse(rng, (long)st.st_size);
        if (r.valid) {
            range_send(&ctx->io, path, mime, ctx->conn, &r, (long)st.st_size);
            ctx->response_sent = 1;
            return r.end - r.start + 1;
        }
    }

#ifdef HAVE_ZLIB
    /* Gzip */
    if (ctx->accept_gzip && !head_only && gzip_should_compress(mime)
        && st.st_size < 4 * 1024 * 1024) {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            char *raw = (char *)malloc((size_t)st.st_size);
            if (raw && fread(raw, 1, (size_t)st.st_size, fp) == (size_t)st.st_size) {
                char *comp = NULL; size_t clen = 0;
                if (gzip_compress(raw, (size_t)st.st_size, &comp, &clen) > 0) {
                    char header[512];
                    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                        "Content-Length: %zu\r\nContent-Encoding: gzip\r\n"
                        "ETag: %s\r\nAccept-Ranges: bytes\r\n"
                        "Connection: %s\r\nServer: chttp/1.0\r\n\r\n",
                        mime, clen, etag, ctx->conn);
                    io_write_all(&ctx->io, header, (size_t)hlen);
                    io_write_all(&ctx->io, comp, clen);
                    free(comp); free(raw); fclose(fp);
                    ctx->response_sent = 1;
                    return (long)st.st_size;
                }
                free(comp);
            }
            free(raw); fclose(fp);
        }
    }
#endif

    /* Plain */
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
        "Content-Length: %ld\r\nETag: %s\r\n"
        "Accept-Ranges: bytes\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
        mime, (long)st.st_size, etag, ctx->conn);
    io_write_all(&ctx->io, header, (size_t)hlen);
    if (!head_only) {
        char buf[SEND_BUF]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            io_write_all(&ctx->io, buf, n);
    }
    fclose(fp);
    ctx->response_sent = 1;
    return (long)st.st_size;
}

void resp_dir_listing(RequestCtx *ctx, const char *fspath) {
    size_t cap = 64 * 1024, used = 0;
    char *html = (char *)malloc(cap);
    if (!html) { resp_error(ctx, 500); return; }

#define AP(...) do { int _n=snprintf(html+used,cap-used,__VA_ARGS__); \
                     if(_n>0) used+=(size_t)_n; } while(0)

    AP("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
       "<title>Index of %s</title>"
       "<style>body{font-family:monospace;padding:24px;max-width:860px;margin:auto}"
       "h2{border-bottom:1px solid #ccc;padding-bottom:8px}"
       "table{width:100%%;border-collapse:collapse}"
       "th,td{padding:4px 10px;text-align:left;border-bottom:1px solid #eee}"
       "a{text-decoration:none;color:#0366d6}a:hover{text-decoration:underline}"
       ".out{float:right;background:#c00;color:#fff;padding:3px 10px;"
       "border-radius:4px;text-decoration:none;font-size:.85rem}"
       "</style></head><body>"
       "<a class=\"out\" href=\"/logout\">&#199;&#305;k&#305;&#351;</a>"
       "<h2>Index of %s</h2><table>"
       "<tr><th>Name</th><th>Size</th><th>Type</th></tr>",
       ctx->decoded_uri, ctx->decoded_uri);

    if (strcmp(ctx->decoded_uri, "/") != 0)
        AP("<tr><td><a href=\"..\">&larr; Parent</a></td><td>-</td><td>dir</td></tr>");

#ifdef _WIN32
    WIN32_FIND_DATAA fdata;
    char pat[MAX_PATH_LEN];
    snprintf(pat, sizeof(pat), "%s\\*", fspath);
    HANDLE hf = FindFirstFileA(pat, &fdata);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fdata.cFileName[0] == '.') continue;
            int isd = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            char sz[32];
            if (isd) snprintf(sz, sizeof(sz), "-");
            else { LARGE_INTEGER li; li.LowPart=fdata.nFileSizeLow; li.HighPart=(LONG)fdata.nFileSizeHigh;
                   snprintf(sz, sizeof(sz), "%lld", li.QuadPart); }
            const char *sep = ctx->decoded_uri[strlen(ctx->decoded_uri)-1]=='/'?"":"/";
            AP("<tr><td><a href=\"%s%s%s%s\">%s%s</a></td><td>%s</td><td>%s</td></tr>",
               ctx->decoded_uri, sep, fdata.cFileName, isd?"/":"",
               fdata.cFileName, isd?"/":"", sz, isd?"dir":mime_get(fdata.cFileName));
        } while (FindNextFileA(hf, &fdata));
        FindClose(hf);
    }
#else
    DIR *dir = opendir(fspath);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            if (ent->d_name[0] == '.') continue;
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", fspath, ent->d_name);
            struct stat st;
            if (stat(full, &st) < 0) continue;
            int isd = S_ISDIR(st.st_mode);
            char sz[32];
            if (isd) snprintf(sz, sizeof(sz), "-");
            else     snprintf(sz, sizeof(sz), "%ld", (long)st.st_size);
            const char *sep = ctx->decoded_uri[strlen(ctx->decoded_uri)-1]=='/'?"":"/";
            AP("<tr><td><a href=\"%s%s%s%s\">%s%s</a></td><td>%s</td><td>%s</td></tr>",
               ctx->decoded_uri, sep, ent->d_name, isd?"/":"",
               ent->d_name, isd?"/":"", sz, isd?"dir":mime_get(ent->d_name));
        }
        closedir(dir);
    }
#endif

    AP("</table><hr><em>chttp/1.0</em></body></html>");
#undef AP

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
        used, ctx->conn);
    io_write_all(&ctx->io, header, (size_t)hlen);
    io_write_all(&ctx->io, html, used);
    free(html);
    ctx->response_sent = 1;
}
