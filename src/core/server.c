#include "core/server.h"
#include "router/router.h"
#include "handlers/login.h"
#include "handlers/files.h"
#include "handlers/upload.h"
#include "handlers/proxy.h"
#include "middleware/auth.h"
#include "middleware/vhost.h"
#include "http/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Request reading ─────────────────────────────────────────────────────── */

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

    const char *he = strstr(buf, "\r\n\r\n");
    if (he) {
        const char *cl_hdr = strcasestr(buf, "Content-Length:");
        if (cl_hdr) {
            long cl = atol(cl_hdr + 15);
            size_t hdrs_len = (size_t)(he + 4 - buf);
            long remaining  = cl - (long)(total - hdrs_len);
            while (remaining > 0 && total < buf_sz - 1) {
                int chunk = (int)((buf_sz-1-total) < (size_t)remaining
                                  ? buf_sz-1-total : (size_t)remaining);
                int n = io_read(io, buf + total, chunk);
                if (n <= 0) break;
                total += (size_t)n; remaining -= n;
            }
            buf[total] = '\0';
        }
    }
    *out_len = total;
    return 0;
}

/* ── URL decode ──────────────────────────────────────────────────────────── */

static void url_decode(char *dst, const char *src, size_t dst_sz) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di+1 < dst_sz; i++) {
        if (src[i]=='%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16); i += 2;
        } else if (src[i]=='+') { dst[di++] = ' ';
        } else { dst[di++] = src[i]; }
    }
    dst[di] = '\0';
}

/* ── Worker (runs in thread pool) ────────────────────────────────────────── */

static void worker(void *arg) {
    RequestCtx *ctx = (RequestCtx *)arg;
    Server     *srv = ctx->server;

#ifdef HAVE_OPENSSL
    if (ctx->is_tls && srv->tls) {
        if (tls_accept(srv->tls, &ctx->io) < 0) {
            io_close(&ctx->io); free(ctx); return;
        }
    }
#endif

    /* Receive timeout */
    if (srv->config->timeout_sec > 0) {
#ifdef _WIN32
        DWORD tv = (DWORD)(srv->config->timeout_sec * 1000);
        setsockopt(ctx->io.fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
        struct timeval tv = { srv->config->timeout_sec, 0 };
        setsockopt(ctx->io.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    /* Rate limit */
    if (!rl_check(srv->rl, ctx->client_ip)) {
        const char *r429 =
            "HTTP/1.1 429 Too Many Requests\r\n"
            "Content-Length: 0\r\nRetry-After: 60\r\n"
            "Connection: close\r\nServer: chttp/1.0\r\n\r\n";
        io_write_all(&ctx->io, r429, strlen(r429));
        logger_log(srv->logger, ctx->client_ip, "-", "-", 429, 0);
        io_close(&ctx->io); free(ctx); return;
    }

    char buf[READ_BUF * 2];

    for (;;) {
        /* ── Read ──────────────────────────────────────────────────────── */
        size_t total = 0;
        if (read_request(&ctx->io, buf, sizeof(buf), &total) < 0) break;

        /* ── Parse ─────────────────────────────────────────────────────── */
        if (http_parse_request(buf, total, &ctx->req) < 0) {
            const char *r400 = "HTTP/1.1 400 Bad Request\r\n"
                               "Content-Length: 0\r\nConnection: close\r\n\r\n";
            io_write_all(&ctx->io, r400, strlen(r400));
            logger_log(srv->logger, ctx->client_ip, "?", "?", 400, 0);
            break;
        }

        /* ── Keep-alive ────────────────────────────────────────────────── */
        ctx->keep_alive = (strcmp(ctx->req.version, "HTTP/1.1") == 0);
        const char *ch = http_get_header(&ctx->req, "Connection");
        if (ch) {
            if (strcasecmp(ch, "close")      == 0) ctx->keep_alive = 0;
            if (strcasecmp(ch, "keep-alive") == 0) ctx->keep_alive = 1;
        }
        ctx->conn = ctx->keep_alive ? "keep-alive" : "close";

        /* ── Decode URI ─────────────────────────────────────────────────── */
        char uri_copy[MAX_URI];
        strncpy(uri_copy, ctx->req.uri, MAX_URI-1);
        char *qs = strchr(uri_copy, '?'); if (qs) *qs = '\0';
        url_decode(ctx->decoded_uri, uri_copy, sizeof(ctx->decoded_uri));

        /* ── Virtual host → doc root ────────────────────────────────────── */
        const char *host_hdr = http_get_header(&ctx->req, "Host");
        VHost *vh = vhost_lookup(srv->config->vhosts, host_hdr);
        strncpy(ctx->doc_root,
                vh ? vh->root : srv->config->root,
                sizeof(ctx->doc_root) - 1);

        /* ── Accept-Encoding ────────────────────────────────────────────── */
        const char *ae = http_get_header(&ctx->req, "Accept-Encoding");
        ctx->accept_gzip = ae && strstr(ae, "gzip") != NULL;

        /* ── Proxy check (before auth) ──────────────────────────────────── */
        ProxyRule *pr = proxy_match(srv->config->proxy_rules, ctx->decoded_uri);
        if (pr) {
            handler_proxy(ctx, pr);
            logger_log(srv->logger, ctx->client_ip,
                       ctx->req.method, ctx->req.uri, 200, 0);
            goto next;
        }

        /* ── Route + auth middleware ────────────────────────────────────── */
        Route *route = router_match(ctx->req.method, ctx->decoded_uri);

        if (route && route->is_public) {
            /* Public route — skip auth */
            ctx->response_sent = 0;
            route->fn(ctx);
        } else {
            /* Check session cookie */
            if (!auth_check_cookie(ctx)) {
                /* Redirect to login; resp_redirect sets response_sent */
                const char *hdr =
                    "HTTP/1.1 302 Found\r\nLocation: /login\r\n"
                    "Content-Length: 0\r\nServer: chttp/1.0\r\n\r\n";
                io_write_all(&ctx->io, hdr, strlen(hdr));
                ctx->response_sent = 1;
                logger_log(srv->logger, ctx->client_ip,
                           ctx->req.method, ctx->req.uri, 302, 0);
                goto next;
            }
            if (route) {
                ctx->response_sent = 0;
                route->fn(ctx);
            } else {
                /* No matching route */
                const char *hdr =
                    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                    "Server: chttp/1.0\r\n\r\n";
                io_write_all(&ctx->io, hdr, strlen(hdr));
                ctx->response_sent = 1;
            }
        }

        logger_log(srv->logger, ctx->client_ip,
                   ctx->req.method, ctx->req.uri, 200, 0);

    next:
        if (!ctx->keep_alive) break;
        memset(buf, 0, sizeof(buf));
        continue;
    }

    io_close(&ctx->io);
    free(ctx);
}

/* ── Listener ────────────────────────────────────────────────────────────── */

static sock_t make_listener(int port) {
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return INVALID_SOCK;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port   = htons((unsigned short)port);
    if (bind(fd,(struct sockaddr *)&addr,sizeof(addr))<0 || listen(fd,SOMAXCONN)<0) {
        sock_close(fd); return INVALID_SOCK;
    }
    return fd;
}

/* ── Public entry: register routes, init subsystems, accept loop ─────────── */

/* Forward declaration — auth_check_cookie is implemented in handlers/login.c */
int auth_check_cookie(RequestCtx *ctx);

void server_start(Config *config) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    /* ── Register routes ──────────────────────────────────────────────── */
    router_clear();
    /*        method   pattern    handler               public */
    router_add("GET",  "/login",  handler_login_get,    1);
    router_add("POST", "/login",  handler_login_post,   1);
    router_add("*",    "/logout", handler_logout,       1);
    router_add("POST", "/upload", handler_upload,       0);
    router_add("*",    "*",       handler_files,        0); /* catch-all */

    /* ── Init subsystems ──────────────────────────────────────────────── */
    AuthManager *auth = auth_create();
    if (!auth_load_users(auth, config->users_file)) {
        fprintf(stderr, "Warning: '%s' not found — default admin/admin\n",
                config->users_file);
        auth_add_user(auth, "admin", "admin");
    }

    Logger     *logger = logger_open(config->log_file);
    ThreadPool *pool   = pool_create(config->max_threads);
    RateLimiter *rl    = rl_create(config->rate_limit, 60);

    Server srv; memset(&srv, 0, sizeof(srv));
    srv.config = config; srv.pool = pool;
    srv.logger = logger; srv.auth = auth; srv.rl = rl;
    srv.fd = srv.tls_fd = INVALID_SOCK;

#ifdef HAVE_OPENSSL
    if (config->tls_port > 0 && config->tls_cert[0]) {
        srv.tls    = tls_create(config->tls_cert, config->tls_key);
        srv.tls_fd = make_listener(config->tls_port);
        if (srv.tls_fd != INVALID_SOCK)
            printf("HTTPS → https://127.0.0.1:%d\n", config->tls_port);
    }
#endif

    srv.fd = make_listener(config->port);
    if (srv.fd == INVALID_SOCK) {
        fprintf(stderr, "bind() failed on port %d\n", config->port);
        goto cleanup;
    }

    printf("HTTP  → http://127.0.0.1:%d\n", config->port);
    printf("root=%s  threads=%d  rate=%d/min  timeout=%ds\n\n",
           config->root, config->max_threads,
           config->rate_limit, config->timeout_sec);

    for (;;) {
        struct sockaddr_in caddr;
#ifdef _WIN32
        int alen = sizeof(caddr);
#else
        socklen_t alen = sizeof(caddr);
#endif
        sock_t client = INVALID_SOCK; int is_tls = 0;

#ifdef HAVE_OPENSSL
        if (srv.tls_fd != INVALID_SOCK) {
            fd_set rd; FD_ZERO(&rd);
            FD_SET(srv.fd, &rd); FD_SET(srv.tls_fd, &rd);
            int mx = (int)(srv.fd > srv.tls_fd ? srv.fd : srv.tls_fd) + 1;
            struct timeval tv = {0, 100000};
            if (select(mx, &rd, NULL, NULL, &tv) <= 0) continue;
            if (FD_ISSET(srv.tls_fd, &rd)) {
                client = accept(srv.tls_fd,(struct sockaddr *)&caddr,&alen); is_tls=1;
            } else { client = accept(srv.fd,(struct sockaddr *)&caddr,&alen); }
        } else
#endif
        { client = accept(srv.fd,(struct sockaddr *)&caddr,&alen); }

        if (client == INVALID_SOCK) continue;

        RequestCtx *ctx = (RequestCtx *)calloc(1, sizeof(RequestCtx));
        if (!ctx) { sock_close(client); continue; }
        io_init(&ctx->io, client);
        ctx->server = &srv;
        ctx->is_tls = is_tls;
        inet_ntop(AF_INET, &caddr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));

        pool_submit(pool, worker, ctx);
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
