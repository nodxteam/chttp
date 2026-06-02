#ifndef MIDDLEWARE_RATE_LIMITER_H
#define MIDDLEWARE_RATE_LIMITER_H

#include "platform.h"

#define RL_BUCKETS 256

typedef struct RLEntry {
    char           ip[64];
    int            count;
    time_t         window_start;
    struct RLEntry *next;
} RLEntry;

typedef struct {
    RLEntry *buckets[RL_BUCKETS];
    int      max_req;
    int      window_sec;
    mutex_t  mutex;
} RateLimiter;

RateLimiter *rl_create(int max_req, int window_sec);
int          rl_check(RateLimiter *rl, const char *ip);
void         rl_destroy(RateLimiter *rl);

#endif
