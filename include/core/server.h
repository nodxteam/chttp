#ifndef CORE_SERVER_H
#define CORE_SERVER_H

#include "platform.h"
#include "core/config.h"
#include "core/thread_pool.h"
#include "core/logger.h"
#include "middleware/auth.h"
#include "middleware/rate_limiter.h"
#include "net/tls.h"
#include "http/context.h"

/*
 * Server — created once at startup, shared across all worker threads.
 * Workers only read from this struct (no writes after init).
 */
struct Server {
    Config      *config;
    ThreadPool  *pool;
    Logger      *logger;
    AuthManager *auth;
    RateLimiter *rl;
    TlsCtx      *tls;      /* NULL if TLS disabled */
    sock_t       fd;        /* plain HTTP listener */
    sock_t       tls_fd;    /* HTTPS listener */
};

void server_start(Config *config);

#endif /* CORE_SERVER_H */
