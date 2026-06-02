#ifndef HANDLERS_PROXY_H
#define HANDLERS_PROXY_H

#include "http/context.h"

typedef struct ProxyRule {
    char              prefix[256];
    char              backend[512];
    struct ProxyRule *next;
} ProxyRule;

ProxyRule *proxy_rule_create(const char *prefix, const char *backend);
void       proxy_rule_free_all(ProxyRule *list);
ProxyRule *proxy_match(ProxyRule *rules, const char *uri);
void       handler_proxy(RequestCtx *ctx, const ProxyRule *rule);

#endif
