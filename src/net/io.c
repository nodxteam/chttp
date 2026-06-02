#include "net/io.h"

void io_init(IoCtx *io, sock_t fd) {
    io->fd = fd;
#ifdef HAVE_OPENSSL
    io->ssl = NULL;
#endif
}

int io_read(IoCtx *io, char *buf, int len) {
#ifdef HAVE_OPENSSL
    if (io->ssl) return SSL_read(io->ssl, buf, len);
#endif
    return recv(io->fd, buf, len, 0);
}

int io_write(IoCtx *io, const char *buf, int len) {
#ifdef HAVE_OPENSSL
    if (io->ssl) return SSL_write(io->ssl, buf, len);
#endif
    return send(io->fd, buf, len, 0);
}

void io_write_all(IoCtx *io, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = io_write(io, buf + sent, (int)(len - sent));
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

void io_close(IoCtx *io) {
#ifdef HAVE_OPENSSL
    if (io->ssl) { SSL_shutdown(io->ssl); SSL_free(io->ssl); io->ssl = NULL; }
#endif
    sock_close(io->fd);
}
