#ifndef HTTP_CONTEXT_H
#define HTTP_CONTEXT_H

#include "platform.h"
#include "net/io.h"
#include "http/parser.h"

/*
 * Forward-declare Server to avoid circular includes.
 * Handlers that need to access server internals include "core/server.h".
 */
typedef struct Server Server;

/*
 * RequestCtx — the single object passed through the entire pipeline:
 *   accept → middleware chain → router → handler
 *
 * Handlers read from here and write responses through io.
 */
typedef struct {
    /* ── Network ────────────────────────────────────────────────────────── */
    IoCtx  io;
    int    is_tls;
    char   client_ip[64];

    /* ── Server reference ───────────────────────────────────────────────── */
    Server *server;

    /* ── HTTP request (filled by http/parser) ───────────────────────────── */
    HttpRequest req;

    /* ── Resolved per-request values ────────────────────────────────────── */
    char        decoded_uri[MAX_URI];
    char        doc_root[MAX_PATH_LEN];
    const char *conn;           /* "keep-alive" or "close" */
    int         keep_alive;
    int         accept_gzip;

    /* ── Pipeline control ───────────────────────────────────────────────── */
    int response_sent;  /* set to 1 by handler; server loop checks this */
} RequestCtx;

#endif /* HTTP_CONTEXT_H */
