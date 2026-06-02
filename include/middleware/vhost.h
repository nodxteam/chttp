#ifndef MIDDLEWARE_VHOST_H
#define MIDDLEWARE_VHOST_H

#include "platform.h"

typedef struct VHost {
    char          hostname[256];
    char          root[MAX_PATH_LEN];
    struct VHost *next;
} VHost;

VHost *vhost_create(const char *hostname, const char *root);
VHost *vhost_lookup(VHost *list, const char *host_header);
void   vhost_free_all(VHost *list);

#endif
