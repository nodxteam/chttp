#include "router/router.h"
#include <string.h>
#include <stdio.h>

#define MAX_ROUTES 64

static Route  routes[MAX_ROUTES];
static int    route_count = 0;

void router_add(const char *method, const char *pattern,
                HandlerFn fn, int is_public) {
    if (route_count >= MAX_ROUTES) {
        fprintf(stderr, "router: route table full\n");
        return;
    }
    strncpy(routes[route_count].method,  method,  sizeof(routes[0].method)  - 1);
    strncpy(routes[route_count].pattern, pattern, sizeof(routes[0].pattern) - 1);
    routes[route_count].fn        = fn;
    routes[route_count].is_public = is_public;
    route_count++;
}

Route *router_match(const char *method, const char *uri) {
    for (int i = 0; i < route_count; i++) {
        Route *r = &routes[i];

        /* Method check ("*" matches any) */
        if (strcmp(r->method, "*") != 0 &&
            strcasecmp(r->method, method) != 0)
            continue;

        /* Pattern check */
        size_t plen = strlen(r->pattern);
        int matched;

        if (plen > 0 && r->pattern[plen - 1] == '*') {
            /* Prefix match: "/api*" matches "/api", "/api/users", etc. */
            matched = (strncmp(uri, r->pattern, plen - 1) == 0);
        } else if (strcmp(r->pattern, "*") == 0) {
            /* Wildcard: matches anything */
            matched = 1;
        } else {
            /* Exact match */
            matched = (strcmp(uri, r->pattern) == 0);
        }

        if (matched) return r;
    }
    return NULL;
}

void router_clear(void) {
    route_count = 0;
}
