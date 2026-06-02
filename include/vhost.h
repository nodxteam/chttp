#ifndef VHOST_H
#define VHOST_H

#include "platform.h"

typedef struct VHost {
    char          hostname[256];
    char          root[MAX_PATH_LEN];
    struct VHost *next;
} VHost;

/* Find a vhost entry matching the given Host header value.
   Returns NULL if not found (caller should use default root). */
VHost *vhost_lookup(VHost *list, const char *host_header);

VHost *vhost_create(const char *hostname, const char *root);
void   vhost_free_all(VHost *list);

#endif /* VHOST_H */
