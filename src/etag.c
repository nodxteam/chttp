#include "etag.h"
#include <stdio.h>
#include <string.h>

void etag_generate(const struct stat *st, char *out, size_t out_sz) {
    snprintf(out, out_sz, "\"%lx-%lx\"",
             (unsigned long)st->st_mtime,
             (unsigned long)st->st_size);
}

int etag_matches(const char *client_etag, const char *server_etag) {
    if (!client_etag || !server_etag) return 0;

    /* Handle weak etag prefix W/ */
    if (strncmp(client_etag, "W/", 2) == 0) client_etag += 2;
    if (strncmp(server_etag, "W/", 2) == 0) server_etag += 2;

    /* Also handle * wildcard */
    if (strcmp(client_etag, "*") == 0) return 1;

    /* Strip surrounding quotes for comparison */
    char ce[128], se[128];
    strncpy(ce, client_etag, sizeof(ce) - 1);
    strncpy(se, server_etag, sizeof(se) - 1);
    char *p;
    if (ce[0] == '"') { memmove(ce, ce+1, strlen(ce)); }
    if ((p = strchr(ce, '"'))) *p = '\0';
    if (se[0] == '"') { memmove(se, se+1, strlen(se)); }
    if ((p = strchr(se, '"'))) *p = '\0';

    return strcmp(ce, se) == 0;
}
