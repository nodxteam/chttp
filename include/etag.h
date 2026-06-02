#ifndef ETAG_H
#define ETAG_H

#include "platform.h"

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

/* Build ETag string from file's mtime + size: "hex-hex" */
void etag_generate(const struct stat *st, char *out, size_t out_sz);

/* Returns 1 if the client's If-None-Match header matches our ETag */
int  etag_matches(const char *client_etag, const char *server_etag);

#endif /* ETAG_H */
