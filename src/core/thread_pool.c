#include "core/thread_pool.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
static DWORD WINAPI worker_fn(LPVOID arg)
#else
static void *worker_fn(void *arg)
#endif
{
    ThreadPool *pool = (ThreadPool *)arg;
    for (;;) {
        mutex_lock(&pool->mutex);
        while (pool->queue_size == 0 && !pool->shutdown)
            cond_wait(&pool->work_ready, &pool->mutex);
        if (pool->shutdown && pool->queue_size == 0) { mutex_unlock(&pool->mutex); break; }
        Task *task = pool->head;
        pool->head = task->next; if (!pool->head) pool->tail = NULL;
        pool->queue_size--; pool->active++;
        mutex_unlock(&pool->mutex);
        task->fn(task->arg); free(task);
        mutex_lock(&pool->mutex);
        pool->active--;
        if (pool->active == 0 && pool->queue_size == 0) cond_broadcast(&pool->work_done);
        mutex_unlock(&pool->mutex);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

ThreadPool *pool_create(int num_threads) {
    ThreadPool *pool = (ThreadPool *)calloc(1, sizeof(ThreadPool));
    pool->threads = (thread_t *)malloc((size_t)num_threads * sizeof(thread_t));
    pool->count = num_threads;
    mutex_init(&pool->mutex); cond_init(&pool->work_ready); cond_init(&pool->work_done);
    for (int i = 0; i < num_threads; i++) {
#ifdef _WIN32
        pool->threads[i] = CreateThread(NULL, 0, worker_fn, pool, 0, NULL);
#else
        pthread_create(&pool->threads[i], NULL, worker_fn, pool);
#endif
    }
    return pool;
}

void pool_submit(ThreadPool *pool, task_fn fn, void *arg) {
    Task *task = (Task *)malloc(sizeof(Task));
    task->fn = fn; task->arg = arg; task->next = NULL;
    mutex_lock(&pool->mutex);
    if (pool->tail) pool->tail->next = task; else pool->head = task;
    pool->tail = task; pool->queue_size++;
    cond_signal(&pool->work_ready);
    mutex_unlock(&pool->mutex);
}

void pool_destroy(ThreadPool *pool) {
    mutex_lock(&pool->mutex); pool->shutdown = 1; cond_broadcast(&pool->work_ready);
    mutex_unlock(&pool->mutex);
    for (int i = 0; i < pool->count; i++) {
#ifdef _WIN32
        WaitForSingleObject(pool->threads[i], INFINITE); CloseHandle(pool->threads[i]);
#else
        pthread_join(pool->threads[i], NULL);
#endif
    }
    cond_destroy(&pool->work_ready); cond_destroy(&pool->work_done);
    mutex_destroy(&pool->mutex); free(pool->threads); free(pool);
}
