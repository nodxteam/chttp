#ifdef HAVE_OPENSSL
#include "net/tls.h"
#include <stdio.h>
#include <stdlib.h>

TlsCtx *tls_create(const char *cert_file, const char *key_file) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { fprintf(stderr, "TLS: SSL_CTX_new failed\n"); return NULL; }

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, key_file,   SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "TLS: cert/key load failed\n");
        SSL_CTX_free(ctx); return NULL;
    }

    TlsCtx *tls = (TlsCtx *)malloc(sizeof(TlsCtx));
    tls->ctx = ctx;
    return tls;
}

int tls_accept(TlsCtx *tls, IoCtx *io) {
    SSL *ssl = SSL_new(tls->ctx);
    if (!ssl) return -1;
    SSL_set_fd(ssl, (int)io->fd);
    if (SSL_accept(ssl) <= 0) { SSL_free(ssl); return -1; }
    io->ssl = ssl;
    return 0;
}

void tls_destroy(TlsCtx *tls) {
    if (tls) { SSL_CTX_free(tls->ctx); free(tls); }
}
#endif
