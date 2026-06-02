#ifndef HTTP_ETAG_H
#define HTTP_ETAG_H

#include "platform.h"
#include <sys/stat.h>

void etag_generate(const struct stat *st, char *out, size_t out_sz);
int  etag_matches(const char *client_etag, const char *server_etag);

#endif
