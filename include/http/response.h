#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "http/context.h"

/*
 * All response helpers take RequestCtx so they can read doc_root, conn, etc.
 * They set ctx->response_sent = 1 on completion.
 */

void resp_error(RequestCtx *ctx, int code);
void resp_redirect(RequestCtx *ctx, int code, const char *location,
                   const char *extra_headers);

/* Send a file with ETag, Range, Gzip support.
   Returns bytes sent, 0 for 304, -1 on error. */
long resp_file(RequestCtx *ctx, const char *path, const char *mime, int head_only);

void resp_dir_listing(RequestCtx *ctx, const char *fspath);

/* Convenience: send a plain text/html body string */
void resp_string(RequestCtx *ctx, int status, const char *mime,
                 const char *body, int body_len);

#endif /* HTTP_RESPONSE_H */
