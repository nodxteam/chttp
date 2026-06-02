#ifndef TLS_H
#define TLS_H

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include "io.h"

typedef struct {
    SSL_CTX *ctx;
} TlsCtx;

TlsCtx *tls_create(const char *cert_file, const char *key_file);
int     tls_accept(TlsCtx *tls, IoCtx *io);  /* upgrades plain IoCtx to TLS */
void    tls_destroy(TlsCtx *tls);

#endif /* HAVE_OPENSSL */
#endif /* TLS_H */
