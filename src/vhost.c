#include "vhost.h"
#include <stdlib.h>
#include <string.h>

VHost *vhost_create(const char *hostname, const char *root) {
    VHost *v = (VHost *)calloc(1, sizeof(VHost));
    if (!v) return NULL;
    strncpy(v->hostname, hostname, sizeof(v->hostname) - 1);
    strncpy(v->root,     root,     sizeof(v->root)     - 1);
    return v;
}

VHost *vhost_lookup(VHost *list, const char *host_header) {
    if (!host_header) return NULL;

    /* Strip port from host (e.g. "example.com:8080" → "example.com") */
    char host[256];
    strncpy(host, host_header, sizeof(host) - 1);
    char *colon = strrchr(host, ':');
    if (colon) *colon = '\0';

    for (VHost *v = list; v; v = v->next)
        if (strcasecmp(v->hostname, host) == 0) return v;

    return NULL;
}

void vhost_free_all(VHost *list) {
    while (list) { VHost *nx = list->next; free(list); list = nx; }
}
