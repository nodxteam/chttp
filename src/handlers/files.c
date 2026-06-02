#include "handlers/files.h"
#include "handlers/cgi.h"
#include "http/response.h"
#include "http/mime.h"
#include "http/parser.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <io.h>
#else
  #include <sys/stat.h>
#endif

/* Directory traversal prevention */
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

void handler_files(RequestCtx *ctx) {
    int is_head = (strcmp(ctx->req.method, "HEAD") == 0);

    if (strcmp(ctx->req.method, "GET") != 0 && !is_head) {
        resp_error(ctx, 501);
        return;
    }

    /* Resolve to filesystem */
    char fspath[MAX_PATH_LEN];
    if (resolve_path(ctx->doc_root, ctx->decoded_uri, fspath, sizeof(fspath)) < 0) {
        resp_error(ctx, 403);
        return;
    }

    struct stat st;
    if (stat(fspath, &st) < 0) {
        resp_error(ctx, 404);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        /* Redirect to trailing slash */
        if (ctx->decoded_uri[strlen(ctx->decoded_uri)-1] != '/') {
            char loc[MAX_URI+2];
            snprintf(loc, sizeof(loc), "%s/", ctx->decoded_uri);
            resp_redirect(ctx, 301, loc, NULL);
            return;
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
            resp_file(ctx, idx, "text/html; charset=utf-8", is_head);
        } else {
            resp_dir_listing(ctx, fspath);
        }
        return;
    }

    if (S_ISREG(st.st_mode)) {
        /* CGI script? */
        if (cgi_is_script(fspath)) {
            handler_cgi(ctx, fspath);
        } else {
            resp_file(ctx, fspath, mime_get(fspath), is_head);
        }
        return;
    }

    resp_error(ctx, 403);
}
