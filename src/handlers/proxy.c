#include "handlers/proxy.h"
#include "http/response.h"
#include "http/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <unistd.h>
#endif

ProxyRule *proxy_rule_create(const char *prefix, const char *backend) {
    ProxyRule *r = (ProxyRule *)calloc(1, sizeof(ProxyRule));
    if (!r) return NULL;
    strncpy(r->prefix,  prefix,  sizeof(r->prefix)  - 1);
    strncpy(r->backend, backend, sizeof(r->backend) - 1);
    return r;
}

void proxy_rule_free_all(ProxyRule *list) {
    while (list) { ProxyRule *nx = list->next; free(list); list = nx; }
}

ProxyRule *proxy_match(ProxyRule *rules, const char *uri) {
    for (ProxyRule *r = rules; r; r = r->next) {
        size_t pl = strlen(r->prefix);
        if (strncmp(uri, r->prefix, pl) == 0 &&
            (uri[pl]=='\0' || uri[pl]=='/' || uri[pl]=='?'))
            return r;
    }
    return NULL;
}

static int parse_backend(const char *backend, char *host, int *port) {
    const char *p = backend;
    if (strncmp(p,"http://",7)==0) p+=7; else if (strncmp(p,"https://",8)==0) p+=8;
    const char *colon = strchr(p,':'), *slash = strchr(p,'/');
    if (colon && (!slash || colon < slash)) {
        size_t hl=(size_t)(colon-p); if (hl>=256) return -1;
        memcpy(host,p,hl); host[hl]='\0'; *port=atoi(colon+1);
    } else {
        size_t hl = slash?(size_t)(slash-p):strlen(p); if (hl>=256) return -1;
        memcpy(host,p,hl); host[hl]='\0'; *port=80;
    }
    return 0;
}

void handler_proxy(RequestCtx *ctx, const ProxyRule *rule) {
    char bhost[256]; int bport;
    if (parse_backend(rule->backend, bhost, &bport) < 0) { resp_error(ctx, 502); return; }

    struct addrinfo hints, *res;
    memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    char ps[8]; snprintf(ps,sizeof(ps),"%d",bport);
    if (getaddrinfo(bhost, ps, &hints, &res) != 0) { resp_error(ctx, 502); return; }

    sock_t bfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (bfd == INVALID_SOCK || connect(bfd, res->ai_addr, (int)res->ai_addrlen) < 0) {
        if (bfd!=INVALID_SOCK) sock_close(bfd);
        freeaddrinfo(res); resp_error(ctx, 502); return;
    }
    freeaddrinfo(res);

    /* Strip prefix from URI */
    const char *fwd_uri = ctx->req.uri + strlen(rule->prefix);
    if (!fwd_uri[0]) fwd_uri = "/";

    char fwd[8192];
    int rlen = snprintf(fwd, sizeof(fwd),
        "%s %s HTTP/1.0\r\nHost: %s:%d\r\nX-Forwarded-For: %s\r\n",
        ctx->req.method, fwd_uri, bhost, bport, ctx->client_ip);

    for (int i=0; i<ctx->req.header_count && rlen<(int)sizeof(fwd)-4; i++) {
        if (strcasecmp(ctx->req.header_names[i],"Connection")==0) continue;
        if (strcasecmp(ctx->req.header_names[i],"Host")==0)       continue;
        rlen += snprintf(fwd+rlen, sizeof(fwd)-(size_t)rlen, "%s: %s\r\n",
                         ctx->req.header_names[i], ctx->req.header_values[i]);
    }
    rlen += snprintf(fwd+rlen, sizeof(fwd)-(size_t)rlen, "Connection: close\r\n\r\n");

    send(bfd, fwd, rlen, 0);
    if (ctx->req.body && ctx->req.body_len)
        send(bfd, ctx->req.body, (int)ctx->req.body_len, 0);

    char buf[65536]; int n;
    while ((n = recv(bfd, buf, sizeof(buf), 0)) > 0)
        io_write_all(&ctx->io, buf, (size_t)n);

    sock_close(bfd);
    ctx->response_sent = 1;
}
