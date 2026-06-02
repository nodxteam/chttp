#ifndef ROUTER_H
#define ROUTER_H

#include "http/context.h"

typedef void (*HandlerFn)(RequestCtx *ctx);

typedef struct {
    char      method[16];   /* "GET" / "POST" / "*" = any */
    char      pattern[256]; /* "/login" / "/api*" (trailing * = prefix) */
    HandlerFn fn;
    int       is_public;    /* 1 = skip auth check */
} Route;

void   router_add(const char *method, const char *pattern,
                  HandlerFn fn, int is_public);
Route *router_match(const char *method, const char *uri);
void   router_clear(void);

#endif /* ROUTER_H */
