#ifndef CORE_THREAD_POOL_H
#define CORE_THREAD_POOL_H

#include "platform.h"

typedef void (*task_fn)(void *arg);

typedef struct Task { task_fn fn; void *arg; struct Task *next; } Task;

typedef struct {
    thread_t *threads;
    int       count;
    Task     *head;
    Task     *tail;
    int       queue_size;
    int       active;
    int       shutdown;
    mutex_t   mutex;
    cond_t    work_ready;
    cond_t    work_done;
} ThreadPool;

ThreadPool *pool_create(int num_threads);
void        pool_submit(ThreadPool *pool, task_fn fn, void *arg);
void        pool_destroy(ThreadPool *pool);

#endif
