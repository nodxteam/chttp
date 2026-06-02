#include "middleware/rate_limiter.h"
#include <stdlib.h>
#include <string.h>

static unsigned int hash_ip(const char *ip) {
    unsigned int h=5381;
    for(const char *p=ip;*p;p++) h=((h<<5)+h)^(unsigned char)*p;
    return h%RL_BUCKETS;
}

RateLimiter *rl_create(int max_req, int window_sec) {
    RateLimiter *rl=(RateLimiter *)calloc(1,sizeof(RateLimiter));
    if(!rl) return NULL;
    rl->max_req=max_req; rl->window_sec=window_sec;
    mutex_init(&rl->mutex); return rl;
}

int rl_check(RateLimiter *rl, const char *ip) {
    if(!rl||rl->max_req<=0) return 1;
    unsigned int h=hash_ip(ip); time_t now=time(NULL);
    mutex_lock(&rl->mutex);
    RLEntry *e=rl->buckets[h];
    while(e&&strcmp(e->ip,ip)!=0) e=e->next;
    if(!e) {
        e=(RLEntry *)calloc(1,sizeof(RLEntry));
        strncpy(e->ip,ip,sizeof(e->ip)-1); e->window_start=now;
        e->next=rl->buckets[h]; rl->buckets[h]=e;
    }
    if(now-e->window_start>=rl->window_sec) { e->count=0; e->window_start=now; }
    e->count++;
    int ok=(e->count<=rl->max_req);
    mutex_unlock(&rl->mutex); return ok;
}

void rl_destroy(RateLimiter *rl) {
    if(!rl) return;
    for(int i=0;i<RL_BUCKETS;i++){RLEntry *e=rl->buckets[i];while(e){RLEntry *nx=e->next;free(e);e=nx;}}
    mutex_destroy(&rl->mutex); free(rl);
}
