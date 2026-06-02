#include "server.h"
#include "http.h"
#include "mime.h"
#include "auth.h"
#include "etag.h"
#include "range.h"
#include "gzip.h"
#include "upload.h"
#include "cgi.h"
#include "vhost.h"
#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <io.h>
#else
  #include <dirent.h>
  #include <sys/stat.h>
#endif

#define SEND_BUF 65536

/* ── Per-connection context ──────────────────────────────────────────────── */

typedef struct {
    IoCtx   io;
    Server *server;
    char    client_ip[64];
    int     is_tls;
} ConnCtx;

/* ── Error + redirect responses ─────────────────────────────────────────── */

static void send_error(IoCtx *io, int code, const char *conn,
                        const char *root) {
    /* Try custom error page first */
    if (root) {
        char err_path[MAX_PATH_LEN];
        snprintf(err_path, sizeof(err_path), "%s/%d.html", root, code);
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
                    code, http_status_text(code), (long)st.st_size, conn);
                io_write_all(io, header, (size_t)hlen);
                char buf[SEND_BUF]; size_t n;
                while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
                    io_write_all(io, buf, n);
                fclose(fp);
                return;
            }
        }
    }

    /* Fallback to generated error page */
    const char *text = http_status_text(code);
    char body[256];
    int blen = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><hr><em>chttp/1.0</em></body></html>",
        code, text, code, text);
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\nServer: chttp/1.0\r\n\r\n",
        code, text, blen, conn);
    io_write_all(io, header, (size_t)hlen);
    io_write_all(io, body,   (size_t)blen);
}

static void send_redirect(IoCtx *io, int code, const char *location,
                           const char *extra_hdrs, const char *conn) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: %s\r\n"
        "%s"
        "Server: chttp/1.0\r\n\r\n",
        code, http_status_text(code), location, conn,
        extra_hdrs ? extra_hdrs : "");
    io_write_all(io, header, (size_t)hlen);
}

/* ── URL percent-decode ──────────────────────────────────────────────────── */

static void url_decode(char *dst, const char *src, size_t dst_sz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_sz; i++) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

/* ── Cookie parser ───────────────────────────────────────────────────────── */

static int parse_cookie(const char *hdr, const char *name,
                          char *out, size_t out_sz) {
    if (!hdr) return 0;
    size_t nlen = strlen(name);
    const char *p = hdr;
    while (*p) {
        while (*p == ' ') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i + 1 < out_sz) out[i] = p[i++];
            out[i] = '\0';
            return 1;
        }
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
    return 0;
}

/* ── Form body parser ────────────────────────────────────────────────────── */

static void parse_form_body(const char *body, size_t body_len,
                              char *out_user, size_t user_sz,
                              char *out_pass, size_t pass_sz) {
    out_user[0] = out_pass[0] = '\0';
    if (!body || !body_len) return;
    char buf[2048];
    size_t n = body_len < sizeof(buf)-1 ? body_len : sizeof(buf)-1;
    memcpy(buf, body, n); buf[n] = '\0';

    const char *p = buf;
    while (*p) {
        const char *eq  = strchr(p, '=');  if (!eq) break;
        const char *amp = strchr(eq+1, '&');
        if (!amp) amp = eq + 1 + strlen(eq + 1);
        char key[128]={0}, val[512]={0};
        size_t kl = (size_t)(eq-p), vl = (size_t)(amp-eq-1);
        if (kl>127) kl=127; if (vl>511) vl=511;
        memcpy(key, p,    kl);
        memcpy(val, eq+1, vl);
        if      (strcmp(key, "username") == 0) url_decode(out_user, val, user_sz);
        else if (strcmp(key, "password") == 0) url_decode(out_pass, val, pass_sz);
        p = *amp ? amp+1 : amp;
    }
}

/* ── Login page HTML ─────────────────────────────────────────────────────── */

static void send_login_page(IoCtx *io, const char *conn, int show_error) {
    const char *err = show_error
        ? "<p class=\"err\">Kullan&#x131;c&#x131; ad&#x131; veya &#x15f;ifre yanl&#x131;&#x15f;.</p>"
        : "";
    char body[4096];
    int blen = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html lang=\"tr\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Giri&#x15f;</title>"
        "<style>*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f0f0f;"
        "display:flex;align-items:center;justify-content:center;min-height:100vh}"
        ".card{background:#1a1a1a;border:1px solid #333;border-radius:12px;"
        "padding:40px 48px;width:100%%;max-width:380px}"
        "h2{color:#e0e0e0;margin-bottom:24px;font-size:1.4rem}"
        "label{display:block;color:#888;font-size:.82rem;margin-bottom:6px}"
        "input{width:100%%;background:#111;border:1px solid #444;border-radius:6px;"
        "color:#e0e0e0;padding:10px 12px;font-size:.95rem;outline:none;transition:border .15s}"
        "input:focus{border-color:#58a6ff}.field{margin-bottom:16px}"
        "button{width:100%%;background:#238636;border:none;border-radius:6px;"
        "color:#fff;padding:11px;font-size:.95rem;cursor:pointer;margin-top:8px;transition:background .15s}"
        "button:hover{background:#2ea043}.err{color:#f85149;font-size:.85rem;margin-bottom:16px}"
        "</style></head><body><div class=\"card\">"
        "<h2>chttp &mdash; Giri&#x15f;</h2>%s"
        "<form method=\"POST\" action=\"/login\">"
        "<div class=\"field\"><label>Kullan&#x131;c&#x131; Ad&#x131;</label>"
        "<input name=\"username\" type=\"text\" autofocus autocomplete=\"username\"></div>"
        "<div class=\"field\"><label>&#x15e;ifre</label>"
        "<input name=\"password\" type=\"password\" autocomplete=\"current-password\"></div>"
        "<button type=\"submit\">Giri&#x15f; Yap</button>"
        "</form></div></body></html>",
        err);
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
        blen, conn);
    io_write_all(io, header, (size_t)hlen);
    io_write_all(io, body,   (size_t)blen);
}

/* ── Path traversal check ────────────────────────────────────────────────── */

static int resolve_path(const char *root, const char *uri,
                          char *out, size_t out_sz) {
    char tmp[MAX_PATH_LEN * 2];
    snprintf(tmp, sizeof(tmp), "%s%s", root, uri);
#ifdef _WIN32
    for (char *p = tmp; *p; p++) if (*p == '/') *p = '\\';
    char resolved[MAX_PATH_LEN], root_abs[MAX_PATH_LEN];
    if (!_fullpath(resolved, tmp,  MAX_PATH_LEN)) return -1;
    if (!_fullpath(root_abs, root, MAX_PATH_LEN)) return -1;
    size_t rl = strlen(root_abs);
    if (strncmp(resolved, root_abs, rl) != 0) return -1;
    if (resolved[rl] != '\0' && resolved[rl] != '\\') return -1;
#else
    char resolved[MAX_PATH_LEN], root_abs[MAX_PATH_LEN];
    if (!realpath(tmp,  resolved)) strncpy(resolved, tmp, MAX_PATH_LEN-1);
    if (!realpath(root, root_abs)) return -1;
    size_t rl = strlen(root_abs);
    if (strncmp(resolved, root_abs, rl) != 0) return -1;
    if (resolved[rl] != '\0' && resolved[rl] != '/') return -1;
#endif
    strncpy(out, resolved, out_sz-1);
    out[out_sz-1] = '\0';
    return 0;
}

/* ── File send with ETag, Range, Gzip ───────────────────────────────────── */

static long send_file(IoCtx *io, const char *path, const char *mime,
                       const char *conn, int head_only,
                       const char *if_none_match,
                       const char *range_hdr,
                       int accept_gzip) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;

    /* ETag */
    char etag[64];
    etag_generate(&st, etag, sizeof(etag));

    /* 304 Not Modified? */
    if (if_none_match && etag_matches(if_none_match, etag)) {
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 304 Not Modified\r\n"
            "ETag: %s\r\nConnection: %s\r\nServer: chttp/1.0\r\n\r\n",
            etag, conn);
        io_write_all(io, header, (size_t)hlen);
        return 0;
    }

    /* Range request? */
    if (range_hdr) {
        RangeSpec r = range_parse(range_hdr, (long)st.st_size);
        if (r.valid) {
            range_send(io, path, mime, conn, &r, (long)st.st_size);
            return r.end - r.start + 1;
        }
    }

#ifdef HAVE_ZLIB
    /* Gzip compression? */
    if (accept_gzip && !head_only && gzip_should_compress(mime)
        && st.st_size < 4 * 1024 * 1024) {
        FILE *fp = fopen(path, "rb");
        if (fp) {
            char *raw = (char *)malloc((size_t)st.st_size);
            if (raw && fread(raw, 1, (size_t)st.st_size, fp) == (size_t)st.st_size) {
                char *compressed = NULL;
                size_t clen = 0;
                if (gzip_compress(raw, (size_t)st.st_size, &compressed, &clen) > 0) {
                    char header[512];
                    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Content-Encoding: gzip\r\n"
                        "ETag: %s\r\n"
                        "Accept-Ranges: bytes\r\n"
                        "Connection: %s\r\nServer: chttp/1.0\r\n\r\n",
                        mime, clen, etag, conn);
                    io_write_all(io, header, (size_t)hlen);
                    io_write_all(io, compressed, clen);
                    free(compressed);
                    free(raw);
                    fclose(fp);
                    return (long)st.st_size;
                }
                free(compressed);
            }
            free(raw);
            fclose(fp);
        }
    }
#else
    (void)accept_gzip;
#endif

    /* Plain send */
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "ETag: %s\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: %s\r\nServer: chttp/1.0\r\n\r\n",
        mime, (long)st.st_size, etag, conn);
    io_write_all(io, header, (size_t)hlen);

    if (!head_only) {
        char buf[SEND_BUF]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            io_write_all(io, buf, n);
    }
    fclose(fp);
    return (long)st.st_size;
}

/* ── Directory listing ───────────────────────────────────────────────────── */

static void send_dir_listing(IoCtx *io, const char *fspath,
                               const char *uri, const char *conn) {
    size_t cap  = 64 * 1024;
    size_t used = 0;
    char  *html = (char *)malloc(cap);
    if (!html) { send_error(io, 500, conn, NULL); return; }

#define AP(...) do { int _n=snprintf(html+used,cap-used,__VA_ARGS__); \
                     if(_n>0) used+=(size_t)_n; } while(0)

    AP("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
       "<title>Index of %s</title>"
       "<style>body{font-family:monospace;padding:24px;max-width:860px;margin:auto}"
       "h2{border-bottom:1px solid #ccc;padding-bottom:8px}"
       "table{width:100%%;border-collapse:collapse}"
       "th,td{text-align:left;padding:4px 10px;border-bottom:1px solid #eee}"
       "a{text-decoration:none;color:#0366d6}a:hover{text-decoration:underline}"
       ".logout{float:right;background:#c00;color:#fff;padding:4px 10px;"
                "border-radius:4px;text-decoration:none;font-size:.85rem}"
       "</style></head><body>"
       "<a class=\"logout\" href=\"/logout\">&#199;&#305;k&#305;&#351;</a>"
       "<h2>Index of %s</h2><table>"
       "<tr><th>Name</th><th>Size</th><th>Type</th></tr>", uri, uri);

    if (strcmp(uri, "/") != 0)
        AP("<tr><td><a href=\"..\">&larr; Parent</a></td><td>-</td><td>dir</td></tr>");

#ifdef _WIN32
    WIN32_FIND_DATAA fd2;
    char pat[MAX_PATH_LEN];
    snprintf(pat, sizeof(pat), "%s\\*", fspath);
    HANDLE hf = FindFirstFileA(pat, &fd2);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fd2.cFileName[0] == '.') continue;
            int is_dir = (fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            char sz[32];
            if (is_dir) snprintf(sz, sizeof(sz), "-");
            else { LARGE_INTEGER li; li.LowPart=fd2.nFileSizeLow; li.HighPart=(LONG)fd2.nFileSizeHigh;
                   snprintf(sz, sizeof(sz), "%lld", li.QuadPart); }
            const char *sep = uri[strlen(uri)-1]=='/'?"":"/";
            AP("<tr><td><a href=\"%s%s%s%s\">%s%s</a></td><td>%s</td><td>%s</td></tr>",
               uri, sep, fd2.cFileName, is_dir?"/":"",
               fd2.cFileName, is_dir?"/":"",
               sz, is_dir?"directory":mime_get(fd2.cFileName));
        } while (FindNextFileA(hf, &fd2));
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
            int is_dir = S_ISDIR(st.st_mode);
            char sz[32];
            if (is_dir) snprintf(sz, sizeof(sz), "-");
            else        snprintf(sz, sizeof(sz), "%ld", (long)st.st_size);
            const char *sep = uri[strlen(uri)-1]=='/'?"":"/";
            AP("<tr><td><a href=\"%s%s%s%s\">%s%s</a></td><td>%s</td><td>%s</td></tr>",
               uri, sep, ent->d_name, is_dir?"/":"",
               ent->d_name, is_dir?"/":"",
               sz, is_dir?"directory":mime_get(ent->d_name));
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
        used, conn);
    io_write_all(io, header, (size_t)hlen);
    io_write_all(io, html, used);
    free(html);
}

/* ── Read full HTTP request (headers + body) ─────────────────────────────── */

static int read_request(IoCtx *io, char *buf, size_t buf_sz, size_t *out_len) {
    size_t total = 0;
    while (total < buf_sz - 1) {
        int n = io_read(io, buf + total, (int)(buf_sz - 1 - total));
        if (n <= 0) return -1;
        total += (size_t)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (!total) return -1;

    /* Read body if Content-Length present */
    const char *hdrs_end = strstr(buf, "\r\n\r\n");
    if (hdrs_end) {
        const char *cl_hdr = strcasestr(buf, "Content-Length:");
        if (cl_hdr) {
            long cl = atol(cl_hdr + 15);
            size_t hdrs_len  = (size_t)(hdrs_end + 4 - buf);
            long   remaining = cl - (long)(total - hdrs_len);
            while (remaining > 0 && total < buf_sz - 1) {
                int chunk = (int)((buf_sz - 1 - total) < (size_t)remaining
                                  ? (buf_sz - 1 - total) : (size_t)remaining);
                int n = io_read(io, buf + total, chunk);
                if (n <= 0) break;
                total    += (size_t)n;
                remaining -= n;
            }
            buf[total] = '\0';
        }
    }

    *out_len = total;
    return 0;
}

/* ── Per-connection worker ───────────────────────────────────────────────── */

static void handle_connection(void *arg) {
    ConnCtx *ctx = (ConnCtx *)arg;
    IoCtx   *io  = &ctx->io;
    Server  *srv = ctx->server;
    char client_ip[64];
    strncpy(client_ip, ctx->client_ip, sizeof(client_ip) - 1);

#ifdef HAVE_OPENSSL
    /* TLS handshake */
    if (ctx->is_tls && srv->tls) {
        if (tls_accept(srv->tls, io) < 0) {
            io_close(io); free(ctx); return;
        }
    }
#endif

    /* Apply receive timeout */
    if (srv->config->timeout_sec > 0) {
#ifdef _WIN32
        DWORD tv = (DWORD)(srv->config->timeout_sec * 1000);
        setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
        struct timeval tv = { srv->config->timeout_sec, 0 };
        setsockopt(io->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    /* Rate limit check */
    if (!rl_check(srv->rl, client_ip)) {
        const char *resp =
            "HTTP/1.1 429 Too Many Requests\r\n"
            "Content-Length: 0\r\nRetry-After: 60\r\n"
            "Connection: close\r\nServer: chttp/1.0\r\n\r\n";
        io_write_all(io, resp, strlen(resp));
        logger_log(srv->logger, client_ip, "-", "-", 429, 0);
        io_close(io); free(ctx); return;
    }

    char buf[READ_BUF * 2];

    for (;;) {
        size_t total = 0;
        if (read_request(io, buf, sizeof(buf), &total) < 0) goto done;

        HttpRequest req;
        if (http_parse_request(buf, total, &req) < 0) {
            send_error(io, 400, "close", NULL);
            logger_log(srv->logger, client_ip, "?", "?", 400, 0);
            break;
        }

        /* Keep-alive */
        int keep_alive = (strcmp(req.version, "HTTP/1.1") == 0);
        const char *ch = http_get_header(&req, "Connection");
        if (ch) {
            if (strcasecmp(ch, "close")      == 0) keep_alive = 0;
            if (strcasecmp(ch, "keep-alive") == 0) keep_alive = 1;
        }
        const char *conn_resp = keep_alive ? "keep-alive" : "close";

        /* Decode URI and strip query string */
        char uri[MAX_URI];
        strncpy(uri, req.uri, MAX_URI - 1);
        char *qs = strchr(uri, '?');
        int has_error_qs = qs && strstr(qs, "error=1");
        if (qs) *qs = '\0';
        char decoded_uri[MAX_URI];
        url_decode(decoded_uri, uri, sizeof(decoded_uri));

        /* Determine document root (vhost override) */
        const char *host_hdr = http_get_header(&req, "Host");
        VHost *vh = vhost_lookup(srv->config->vhosts, host_hdr);
        const char *doc_root = vh ? vh->root : srv->config->root;

        /* ── Auth-free routes ──────────────────────────────────────────── */

        if (strcmp(decoded_uri, "/login") == 0 &&
            strcmp(req.method, "GET") == 0) {
            send_login_page(io, conn_resp, has_error_qs);
            logger_log(srv->logger, client_ip, "GET", req.uri, 200, 0);
            goto next;
        }

        if (strcmp(decoded_uri, "/login") == 0 &&
            strcmp(req.method, "POST") == 0) {
            char username[MAX_USERNAME]={0}, password[MAX_PASSWORD]={0};
            parse_form_body(req.body, req.body_len,
                            username, sizeof(username),
                            password, sizeof(password));
            if (auth_validate(srv->auth, username, password)) {
                const char *tok = auth_create_session(srv->auth, username);
                char sc[256];
                snprintf(sc, sizeof(sc),
                         "Set-Cookie: session=%s; HttpOnly; Path=/\r\n", tok);
                send_redirect(io, 302, "/", sc, conn_resp);
                logger_log(srv->logger, client_ip, "POST", "/login", 302, 0);
            } else {
                send_redirect(io, 302, "/login?error=1", NULL, conn_resp);
                logger_log(srv->logger, client_ip, "POST", "/login", 302, 0);
            }
            goto next;
        }

        if (strcmp(decoded_uri, "/logout") == 0) {
            char tok[SESSION_TOKEN_LEN+1]={0};
            const char *ck = http_get_header(&req, "Cookie");
            if (parse_cookie(ck, "session", tok, sizeof(tok)))
                auth_delete_session(srv->auth, tok);
            send_redirect(io, 302, "/login",
                          "Set-Cookie: session=; HttpOnly; Path=/; Max-Age=0\r\n",
                          conn_resp);
            logger_log(srv->logger, client_ip, req.method, "/logout", 302, 0);
            goto next;
        }

        /* ── Auth check ────────────────────────────────────────────────── */
        {
            char tok[SESSION_TOKEN_LEN+1]={0};
            const char *ck = http_get_header(&req, "Cookie");
            int authed = parse_cookie(ck, "session", tok, sizeof(tok)) &&
                         (auth_get_session(srv->auth, tok) != NULL);
            if (!authed) {
                send_redirect(io, 302, "/login", NULL, conn_resp);
                logger_log(srv->logger, client_ip, req.method, req.uri, 302, 0);
                goto next;
            }
        }

        /* ── Proxy rules ───────────────────────────────────────────────── */
        {
            ProxyRule *pr = proxy_match(srv->config->proxy_rules, decoded_uri);
            if (pr) {
                if (proxy_forward(io, pr, &req, conn_resp) < 0)
                    send_error(io, 502, conn_resp, doc_root);
                logger_log(srv->logger, client_ip, req.method, req.uri, 200, 0);
                goto next;
            }
        }

        /* ── File upload POST /upload ───────────────────────────────────── */
        if (strcmp(decoded_uri, "/upload") == 0 &&
            strcmp(req.method, "POST") == 0) {
            const char *ct = http_get_header(&req, "Content-Type");
            long max_bytes = (long)srv->config->upload_max_mb * 1024 * 1024;
            upload_handle(io, req.body, req.body_len,
                          ct ? ct : "", srv->config->upload_dir,
                          max_bytes, conn_resp);
            logger_log(srv->logger, client_ip, "POST", "/upload", 200, 0);
            goto next;
        }

        /* ── GET / HEAD only beyond here ───────────────────────────────── */
        int is_head = (strcmp(req.method, "HEAD") == 0);
        if (strcmp(req.method, "GET") != 0 && !is_head) {
            send_error(io, 501, conn_resp, doc_root);
            logger_log(srv->logger, client_ip, req.method, req.uri, 501, 0);
            goto next;
        }

        /* Accept-Encoding check */
        const char *ae = http_get_header(&req, "Accept-Encoding");
        int accept_gzip = ae && strstr(ae, "gzip") != NULL;

        /* ── Resolve filesystem path ────────────────────────────────────── */
        char fspath[MAX_PATH_LEN];
        if (resolve_path(doc_root, decoded_uri, fspath, sizeof(fspath)) < 0) {
            send_error(io, 403, conn_resp, doc_root);
            logger_log(srv->logger, client_ip, req.method, req.uri, 403, 0);
            goto next;
        }

        struct stat st;
        if (stat(fspath, &st) < 0) {
            send_error(io, 404, conn_resp, doc_root);
            logger_log(srv->logger, client_ip, req.method, req.uri, 404, 0);
            goto next;
        }

        /* ── Serve ──────────────────────────────────────────────────────── */
        if (S_ISDIR(st.st_mode)) {
            /* Redirect bare dir to trailing slash */
            if (decoded_uri[strlen(decoded_uri)-1] != '/') {
                char loc[MAX_URI+2];
                snprintf(loc, sizeof(loc), "%s/", decoded_uri);
                send_redirect(io, 301, loc, NULL, conn_resp);
                logger_log(srv->logger, client_ip, req.method, req.uri, 301, 0);
                goto next;
            }
            /* Try index.html */
            char idx[MAX_PATH_LEN];
#ifdef _WIN32
            snprintf(idx, sizeof(idx), "%s\\index.html", fspath);
#else
            snprintf(idx, sizeof(idx), "%s/index.html", fspath);
#endif
            struct stat ist;
            if (stat(idx, &ist) == 0 && S_ISREG(ist.st_mode)) {
                const char *inm = http_get_header(&req, "If-None-Match");
                const char *rng = http_get_header(&req, "Range");
                long sz = send_file(io, idx, "text/html; charset=utf-8",
                                    conn_resp, is_head, inm, rng, accept_gzip);
                logger_log(srv->logger, client_ip, req.method, req.uri,
                           sz >= 0 ? 200 : 500, sz > 0 ? sz : 0);
            } else {
                send_dir_listing(io, fspath, decoded_uri, conn_resp);
                logger_log(srv->logger, client_ip, req.method, req.uri, 200, 0);
            }
        } else if (S_ISREG(st.st_mode)) {
            /* CGI? */
            if (cgi_is_script(fspath)) {
                if (cgi_execute(io, fspath, &req, client_ip, conn_resp) < 0)
                    send_error(io, 500, conn_resp, doc_root);
                logger_log(srv->logger, client_ip, req.method, req.uri, 200, 0);
            } else {
                const char *inm  = http_get_header(&req, "If-None-Match");
                const char *rng  = http_get_header(&req, "Range");
                long sz = send_file(io, fspath, mime_get(fspath),
                                    conn_resp, is_head, inm, rng, accept_gzip);
                logger_log(srv->logger, client_ip, req.method, req.uri,
                           sz >= 0 ? 200 : 500, sz > 0 ? sz : 0);
            }
        } else {
            send_error(io, 403, conn_resp, doc_root);
            logger_log(srv->logger, client_ip, req.method, req.uri, 403, 0);
        }

    next:
        if (!keep_alive) break;
        memset(buf, 0, sizeof(buf));
        continue;
    done:
        break;
    }

    io_close(io);
    free(ctx);
}

/* ── Listener helpers ────────────────────────────────────────────────────── */

static sock_t create_listener(int port) {
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, SOMAXCONN) < 0) {
        sock_close(fd);
        return INVALID_SOCK;
    }
    return fd;
}

/* ── Public entry point ───────────────────────────────────────────────────── */

void server_start(Config *config) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n"); return;
    }
#endif

    AuthManager *auth   = auth_create();
    if (!auth_load_users(auth, config->users_file)) {
        fprintf(stderr, "Warning: '%s' not found — using default admin/admin\n",
                config->users_file);
        auth_add_user(auth, "admin", "admin");
    }

    Logger     *logger = logger_open(config->log_file);
    ThreadPool *pool   = pool_create(config->max_threads);
    RateLimiter *rl    = rl_create(config->rate_limit, 60);

    Server srv;
    memset(&srv, 0, sizeof(srv));
    srv.config = config;
    srv.pool   = pool;
    srv.logger = logger;
    srv.auth   = auth;
    srv.rl     = rl;
    srv.fd     = INVALID_SOCK;
    srv.tls_fd = INVALID_SOCK;

#ifdef HAVE_OPENSSL
    if (config->tls_port > 0 && config->tls_cert[0] && config->tls_key[0]) {
        srv.tls    = tls_create(config->tls_cert, config->tls_key);
        srv.tls_fd = create_listener(config->tls_port);
        if (srv.tls_fd != INVALID_SOCK)
            printf("HTTPS listening on https://127.0.0.1:%d\n", config->tls_port);
    }
#endif

    srv.fd = create_listener(config->port);
    if (srv.fd == INVALID_SOCK) {
        fprintf(stderr, "bind() failed on port %d\n", config->port);
        goto cleanup;
    }

    printf("HTTP  listening on http://127.0.0.1:%d\n", config->port);
    printf("Serving: %s | threads: %d | rate: %d/min | timeout: %ds\n\n",
           config->root, config->max_threads,
           config->rate_limit, config->timeout_sec);

    for (;;) {
        struct sockaddr_in caddr;
#ifdef _WIN32
        int alen = sizeof(caddr);
#else
        socklen_t alen = sizeof(caddr);
#endif
        /* Accept from whichever listener has a connection */
        sock_t client = INVALID_SOCK;
        int    is_tls = 0;

#ifdef HAVE_OPENSSL
        if (srv.tls_fd != INVALID_SOCK) {
            /* Non-blocking check on TLS fd */
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(srv.fd, &rd);
            FD_SET(srv.tls_fd, &rd);
            int maxfd = (int)(srv.fd > srv.tls_fd ? srv.fd : srv.tls_fd) + 1;
            struct timeval tv = {0, 100000}; /* 100ms */
            int sel = select(maxfd, &rd, NULL, NULL, &tv);
            if (sel <= 0) continue;
            if (FD_ISSET(srv.tls_fd, &rd)) {
                client = accept(srv.tls_fd, (struct sockaddr *)&caddr, &alen);
                is_tls = 1;
            } else {
                client = accept(srv.fd, (struct sockaddr *)&caddr, &alen);
            }
        } else
#endif
        {
            client = accept(srv.fd, (struct sockaddr *)&caddr, &alen);
        }

        if (client == INVALID_SOCK) continue;

        ConnCtx *ctx = (ConnCtx *)malloc(sizeof(ConnCtx));
        if (!ctx) { sock_close(client); continue; }
        io_init(&ctx->io, client);
        ctx->server = &srv;
        ctx->is_tls = is_tls;
        inet_ntop(AF_INET, &caddr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));

        pool_submit(pool, handle_connection, ctx);
    }

    sock_close(srv.fd);
    if (srv.tls_fd != INVALID_SOCK) sock_close(srv.tls_fd);

cleanup:
    pool_destroy(pool);
    logger_close(logger);
    auth_destroy(auth);
    rl_destroy(rl);
#ifdef HAVE_OPENSSL
    if (srv.tls) tls_destroy(srv.tls);
#endif
#ifdef _WIN32
    WSACleanup();
#endif
}
