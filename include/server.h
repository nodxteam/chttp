#ifndef SERVER_H
#define SERVER_H

#include "platform.h"
#include "config.h"
#include "thread_pool.h"
#include "logger.h"
#include "auth.h"
#include "rate_limiter.h"
#include "tls.h"
#include "io.h"

typedef struct {
    Config      *config;
    ThreadPool  *pool;
    Logger      *logger;
    AuthManager *auth;
    RateLimiter *rl;
#ifdef HAVE_OPENSSL
    TlsCtx      *tls;
#endif
    sock_t       fd;      /* plain HTTP listener */
    sock_t       tls_fd;  /* HTTPS listener (INVALID_SOCK if disabled) */
} Server;

void server_start(Config *config);

#endif /* SERVER_H */
