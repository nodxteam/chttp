#ifndef PROXY_H
#define PROXY_H

#include "io.h"
#include "http.h"

typedef struct ProxyRule {
    char              prefix[256];   /* URI prefix, e.g. "/api" */
    char              backend[512];  /* "http://host:port" */
    struct ProxyRule *next;
} ProxyRule;

/* Return first rule whose prefix matches the start of uri, or NULL */
ProxyRule *proxy_match(ProxyRule *rules, const char *uri);

/* Forward request to backend and stream response to client.
   Returns 0 on success, -1 on connection error. */
int proxy_forward(IoCtx *client_io, const ProxyRule *rule,
                  const HttpRequest *req, const char *conn);

ProxyRule *proxy_rule_create(const char *prefix, const char *backend);
void       proxy_rule_free_all(ProxyRule *list);

#endif /* PROXY_H */
