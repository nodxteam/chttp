#ifndef NET_TLS_H
#define NET_TLS_H

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include "net/io.h"

typedef struct { SSL_CTX *ctx; } TlsCtx;

TlsCtx *tls_create(const char *cert_file, const char *key_file);
int     tls_accept(TlsCtx *tls, IoCtx *io);
void    tls_destroy(TlsCtx *tls);

#else
typedef struct { int _unused; } TlsCtx;
#endif

#endif /* NET_TLS_H */
