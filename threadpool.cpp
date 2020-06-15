/*
 * Copyright (c) 2016, Mathias Brossard <mathias@brossard.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /**
  * @file threadpool.c
  * @brief Threadpool implementation file
  */

#include "threadpool.h"

typedef enum {
    immediate_shutdown = 1,
    graceful_shutdown = 2
} threadpool_shutdown_t;

/**
 *  @struct threadpool_task
 *  @brief the work struct
 *
 *  @var function Pointer to the function that will perform the task.
 *  @var argument Argument to be passed to the function.
 */

typedef struct {
    void (*function)(void*);
    void* argument;
} threadpool_task_t;

/**
 *  @struct threadpool
 *  @brief The threadpool struct
 *
 *  @var notify       Condition variable to notify worker threads.
 *  @var threads      Array containing worker threads ID.
 *  @var thread_count Number of threads
 *  @var queue        Array containing the task queue.
 *  @var queue_size   Size of the task queue.
 *  @var head         Index of the first element.
 *  @var tail         Index of the next element.
 *  @var count        Number of pending tasks
 *  @var shutdown     Flag indicating if the pool is shutting down
 *  @var started      Number of started threads
 */
struct threadpool_t {
    CRITICAL_SECTION cs;
    HANDLE sem;
    HANDLE* threads;
    threadpool_task_t* queue;
    int thread_count;
    int queue_size;
    int head;
    int tail;
    int count;
    int shutdown;
    int started;
};

/**
 * @function void *threadpool_thread(void *threadpool)
 * @brief the worker thread
 * @param threadpool the pool which own the thread
 */

static void threadpool_thread(void* threadpool);
static int threadpool_free(threadpool_t* pool);

threadpool_t* threadpool_create(int thread_count, int queue_size, int flags)
{
    threadpool_t* pool;
    int i;
    (void)flags;

    if (thread_count <= 0 || thread_count > MAX_THREADS || queue_size <= 0 || queue_size > MAX_QUEUE) {
        return NULL;
    }

    if ((pool = (threadpool_t*)malloc(sizeof(threadpool_t))) == NULL) {
        goto err;
    }

    /* Initialize */
    pool->thread_count = 0;
    pool->queue_size = queue_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = pool->started = 0;

    /* Allocate thread and task queue */
    pool->threads = (HANDLE*)malloc(sizeof(HANDLE) * thread_count);
    pool->queue = (threadpool_task_t*)malloc
    (sizeof(threadpool_task_t) * queue_size);

    InitializeCriticalSection(&(pool->cs));
    pool->sem = CreateSemaphore(NULL, 0, MAX_QUEUE, "E9043031-DFFF-4182-8FD9-B7B7C28F6B14");


    /* Start worker threads */
    for (i = 0; i < thread_count; i++) {
        if ((pool->threads[i] = (HANDLE)_beginthread(threadpool_thread, 0, (void*)pool)) < 0) {
            threadpool_destroy(pool, 0);
            return NULL;
        }
        pool->thread_count++;
        pool->started++;
    }
    return pool;

err:
    if (pool) {
        threadpool_free(pool);
    }
    return NULL;
}

int threadpool_add(threadpool_t* pool, void (*function)(void*),
    void* argument, int flags)
{
    int err = 0;
    int next;
    (void)flags;

    if (pool == NULL || function == NULL) {
        return threadpool_invalid;
    }

    EnterCriticalSection(&(pool->cs));

    next = (pool->tail + 1) % pool->queue_size;
    do {
        /* Are we full ? */
        if (pool->count == pool->queue_size) {
            err = threadpool_queue_full;
            break;
        }

        /* Are we shutting down ? */
        if (pool->shutdown) {
            err = threadpool_shutdown;
            break;
        }

        /* Add task to queue */
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument = argument;
        pool->tail = next;
        pool->count += 1;

        /* pthread_cond_broadcast */
        ReleaseSemaphore(pool->sem, 1, NULL);

    } while (0);

    LeaveCriticalSection(&(pool->cs));

    return err;
}


int threadpool_destroy(threadpool_t* pool, int flags)
{
    int i, err = 0;

    if (pool == NULL) {
        return threadpool_invalid;
    }

    do {
        EnterCriticalSection(&(pool->cs));

        if (pool->shutdown) {
            err = threadpool_shutdown;
            break;
        }
        pool->shutdown = (flags & threadpool_graceful) ?
            graceful_shutdown : immediate_shutdown;

        LeaveCriticalSection(&(pool->cs));

        /* Join all worker thread */
        if (pool->shutdown == immediate_shutdown) {
            for (i = 0; i < pool->thread_count; i++) {
                TerminateThread(pool->threads[i], 0);
                CloseHandle(pool->threads[i]);
            }
        }
        else{
            //shutdown gracefully
        }

    } while (0);

    /* Only if everything went well do we deallocate the pool */
    if (!err) {
        threadpool_free(pool);
    }
    return err;
}

int threadpool_free(threadpool_t* pool)
{
    if (pool == NULL || pool->started > 0) {
        return -1;
    }

    /* Did we manage to allocate ? */
    if (pool->threads) {
        free(pool->threads);
        free(pool->queue);
    }
    free(pool);
    return 0;
}


void threadpool_thread(void* threadpool)
{
    threadpool_t* pool = (threadpool_t*)threadpool;
    threadpool_task_t task;

    for (;;) {
        printf("Before WaitForSingleObject\n");

        DWORD dw = WaitForSingleObject(pool->sem, INFINITE);
        if (dw == WAIT_FAILED)
        {
            pool->started--;
            return;
        }

        printf("After WaitForSingleObject\n");

        EnterCriticalSection(&(pool->cs));

        if ((pool->shutdown == immediate_shutdown) ||
            ((pool->shutdown == graceful_shutdown) &&
               pool->count == 0)) {
            LeaveCriticalSection(&(pool->cs));
            break;
        }

        /* Grab our task */
        task.function = pool->queue[pool->head].function;
        task.argument = pool->queue[pool->head].argument;
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count -= 1;

        /* Unlock */
        LeaveCriticalSection(&(pool->cs));

        /* Get to work */
        (*(task.function))(task.argument);
    }

    pool->started--;
    return;
}
