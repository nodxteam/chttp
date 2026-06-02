#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
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
        size_t plen = strlen(r->prefix);
        if (strncmp(uri, r->prefix, plen) == 0 &&
            (uri[plen] == '\0' || uri[plen] == '/' || uri[plen] == '?'))
            return r;
    }
    return NULL;
}

/* Parse "http://host:port" or "http://host" into host+port */
static int parse_backend(const char *backend, char *host, int *port) {
    const char *p = backend;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;

    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= 256) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
        if (hlen >= 256) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = 80;
    }
    return 0;
}

int proxy_forward(IoCtx *client_io, const ProxyRule *rule,
                  const HttpRequest *req, const char *conn) {
    char bhost[256];
    int  bport;
    if (parse_backend(rule->backend, bhost, &bport) < 0) return -1;

    /* Connect to backend */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", bport);
    if (getaddrinfo(bhost, port_str, &hints, &res) != 0) return -1;

    sock_t bfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (bfd == INVALID_SOCK) { freeaddrinfo(res); return -1; }
    if (connect(bfd, res->ai_addr, (int)res->ai_addrlen) < 0) {
        sock_close(bfd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);

    /* Strip prefix from URI when forwarding */
    const char *fwd_uri = req->uri + strlen(rule->prefix);
    if (!fwd_uri[0]) fwd_uri = "/";

    /* Build forwarded request */
    char fwd_req[8192];
    int  rlen = snprintf(fwd_req, sizeof(fwd_req),
        "%s %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "X-Forwarded-For: %s\r\n",
        req->method, fwd_uri, bhost, bport,
        /* client IP comes from ConnCtx; pass as header that server.c sets */
        "unknown");

    /* Forward original headers (skip Connection, Host) */
    for (int i = 0; i < req->header_count && rlen < (int)sizeof(fwd_req) - 4; i++) {
        if (strcasecmp(req->header_names[i], "Connection") == 0) continue;
        if (strcasecmp(req->header_names[i], "Host")       == 0) continue;
        rlen += snprintf(fwd_req + rlen, sizeof(fwd_req) - (size_t)rlen,
                         "%s: %s\r\n",
                         req->header_names[i], req->header_values[i]);
    }
    rlen += snprintf(fwd_req + rlen, sizeof(fwd_req) - (size_t)rlen,
                     "Connection: close\r\n\r\n");

    send(bfd, fwd_req, rlen, 0);

    /* If POST, forward body */
    if (req->body && req->body_len > 0)
        send(bfd, req->body, (int)req->body_len, 0);

    /* Stream backend response back to client */
    char buf[65536];
    int  n;
    while ((n = recv(bfd, buf, sizeof(buf), 0)) > 0)
        io_write_all(client_io, buf, (size_t)n);

    sock_close(bfd);
    (void)conn;
    return 0;
}
