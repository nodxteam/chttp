#ifndef NET_IO_H
#define NET_IO_H

#include "platform.h"

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

typedef struct {
    sock_t fd;
#ifdef HAVE_OPENSSL
    SSL *ssl;
#endif
} IoCtx;

void io_init(IoCtx *io, sock_t fd);
int  io_read(IoCtx *io, char *buf, int len);
int  io_write(IoCtx *io, const char *buf, int len);
void io_write_all(IoCtx *io, const char *buf, size_t len);
void io_close(IoCtx *io);

#endif /* NET_IO_H */
