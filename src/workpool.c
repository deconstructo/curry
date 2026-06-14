#include "workpool.h"
#include "gc.h"
#include "gc_generational.h"
#include "vm.h"
#include "eval.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#ifdef __APPLE__
#  include <sys/sysctl.h>
#else
#  include <unistd.h>
#endif

/* ── hardware concurrency ──────────────────────────────────────────────── */

int pool_hw_concurrency(void) {
#ifdef __APPLE__
    int n = 4; size_t sz = sizeof(n);
    sysctlbyname("hw.logicalcpu", &n, &sz, NULL, 0);
    return n;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 4;
#endif
}

/* ── Chase-Lev deque ────────────────────────────────────────────────────── */

static void deque_init(WSDeque *d) {
    d->buf = gc_alloc_raw_pinned(DEQUE_CAP * sizeof(WorkItem *));
    atomic_init(&d->bottom, 0);
    atomic_init(&d->top,    0);
}

/* Owner pushes onto bottom.  No locking needed — only the owner pushes. */
static bool deque_push(WSDeque *d, WorkItem *item) {
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&d->top,    memory_order_acquire);
    if ((b - t) >= DEQUE_CAP - 1) return false;   /* full — caller falls back */
    d->buf[b & (DEQUE_CAP - 1)] = item;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
    return true;
}

/* Owner pops from bottom (LIFO — favours cache-warm work). */
static WorkItem *deque_pop(WSDeque *d) {
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&d->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&d->top, memory_order_relaxed);

    if (b < t) {
        atomic_store_explicit(&d->bottom, t, memory_order_relaxed);
        return NULL;
    }
    WorkItem *item = d->buf[b & (DEQUE_CAP - 1)];
    if (b > t) return item;   /* more than one item — no race with thieves */

    /* Exactly one item left: race with a potential thief. */
    int64_t expected = t;
    bool won = atomic_compare_exchange_strong_explicit(
                   &d->top, &expected, t + 1,
                   memory_order_seq_cst, memory_order_relaxed);
    atomic_store_explicit(&d->bottom, t + 1, memory_order_relaxed);
    return won ? item : NULL;
}

/* Thief steals from top (FIFO — steals the oldest, least-cache-warm work). */
static WorkItem *deque_steal(WSDeque *d) {
    int64_t t = atomic_load_explicit(&d->top,    memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_acquire);
    if (t >= b) return NULL;

    WorkItem *item = d->buf[t & (DEQUE_CAP - 1)];
    int64_t expected = t;
    if (!atomic_compare_exchange_strong_explicit(
            &d->top, &expected, t + 1,
            memory_order_seq_cst, memory_order_relaxed))
        return NULL;   /* another thief won the race */
    return item;
}

/* ── pool ────────────────────────────────────────────────────────────────── */

static WorkPool *global_pool = NULL;

_Thread_local bool pool_is_worker = false;

static void execute_item(WorkItem *item) {
    ExnHandler h;
    h.prev = current_handler; current_handler = &h;

    if (setjmp(h.jmp) == 0) {
        switch (item->kind) {
        case WORK_MAP:
            for (int i = item->start; i < item->end; i++) {
                if (gc_gen_active) gc_gen_safepoint();
                item->results[i] = apply(item->fn,
                                         scm_cons(item->elems[i], V_NIL));
            }
            break;
        case WORK_FOREACH:
            for (int i = item->start; i < item->end; i++) {
                if (gc_gen_active) gc_gen_safepoint();
                apply(item->fn, scm_cons(item->elems[i], V_NIL));
            }
            break;
        case WORK_REDUCE: {
            val_t acc = item->elems[item->start];
            for (int i = item->start + 1; i < item->end; i++) {
                if (gc_gen_active) gc_gen_safepoint();
                acc = apply(item->fn,
                            scm_cons(acc, scm_cons(item->elems[i], V_NIL)));
            }
            item->result = acc;
            break;
        }
        }
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        item->error = true;
        item->exn   = h.exn;
    }

    /* Signal one completion to the dispatcher. */
    pthread_mutex_lock(item->done_mutex);
    atomic_fetch_add(item->n_done, 1);
    pthread_cond_signal(item->done_cond);
    pthread_mutex_unlock(item->done_mutex);
}

#define STEAL_ATTEMPTS 3

static WorkItem *try_steal(int my_id) {
    int n = global_pool->n_workers;
    for (int a = 0; a < STEAL_ATTEMPTS; a++) {
        /* Rotate starting victim each attempt to spread load. */
        int start = (my_id + 1 + a * 7) % n;
        for (int i = 0; i < n - 1; i++) {
            int victim = (start + i) % n;
            if (victim == my_id) continue;
            WorkItem *item = deque_steal(&global_pool->deques[victim]);
            if (item) return item;
        }
    }
    return NULL;
}

static void *worker_loop(void *arg) {
    int id = (int)(intptr_t)arg;
    pool_is_worker = true;
    gc_register_thread();
    vm_init();

    while (!atomic_load_explicit(&global_pool->shutdown, memory_order_relaxed)) {
        WorkItem *item = deque_pop(&global_pool->deques[id]);
        if (!item) item = try_steal(id);

        if (item) {
            execute_item(item);
            continue;
        }

        /* Nothing to do — service any pending GC pause, then park.
         * Re-check under the lock to close the window between "found nothing"
         * and "parked" in which the dispatcher might broadcast and be missed. */
        if (gc_gen_active) gc_gen_safepoint();
        pthread_mutex_lock(&global_pool->park_mutex);
        item = deque_pop(&global_pool->deques[id]);
        if (!item) item = try_steal(id);
        if (item) {
            pthread_mutex_unlock(&global_pool->park_mutex);
            execute_item(item);
        } else {
            atomic_fetch_add(&global_pool->n_parked, 1);
            /* Tell the generational GC not to wait for us while we sleep. */
            if (gc_gen_active) gc_gen_thread_park();
            pthread_cond_wait(&global_pool->park_cond, &global_pool->park_mutex);
            /* Re-register and service any STW pause before doing real work. */
            if (gc_gen_active) gc_gen_thread_unpark();
            atomic_fetch_sub(&global_pool->n_parked, 1);
            pthread_mutex_unlock(&global_pool->park_mutex);
        }
    }
    vm_free();
    return NULL;
}

void pool_init(void) {
    if (global_pool) return;

    int n = pool_hw_concurrency();
    /* WorkPool lives for the process lifetime and is pointed to by the raw C
     * global `global_pool`.  It must be pinned so the generational GC never
     * moves it and leaves `global_pool` dangling after a nursery reset. */
    global_pool = gc_alloc_raw_pinned(sizeof(WorkPool));
    global_pool->n_workers = n;
    global_pool->threads   = gc_alloc_raw_pinned(n * sizeof(pthread_t));
    global_pool->deques    = gc_alloc_raw_pinned(n * sizeof(WSDeque));
    atomic_init(&global_pool->shutdown, false);
    atomic_init(&global_pool->n_parked, 0);
    pthread_mutex_init(&global_pool->park_mutex, NULL);
    pthread_cond_init(&global_pool->park_cond,   NULL);

    for (int i = 0; i < n; i++) {
        deque_init(&global_pool->deques[i]);
        pthread_create(&global_pool->threads[i], NULL,
                       worker_loop, (void *)(intptr_t)i);
    }
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

#define OVERSUBSCRIPTION 4

WorkItem **pool_submit(WorkKind kind, val_t fn,
                       val_t *elems, val_t *results, int n,
                       int *nchunks_out,
                       atomic_int *n_done,
                       pthread_mutex_t *done_mutex,
                       pthread_cond_t  *done_cond) {
    int nw      = global_pool->n_workers;
    int nchunks = n < nw ? n : (n < nw * OVERSUBSCRIPTION
                                    ? n : nw * OVERSUBSCRIPTION);
    *nchunks_out = nchunks;

    WorkItem **items = gc_alloc_raw_pinned(nchunks * sizeof(WorkItem *));

    int pos = 0;
    for (int c = 0; c < nchunks; c++) {
        int sz = (n - pos) / (nchunks - c);
        /* WorkItem is pointed to by a raw C pointer in the deque's buf[].
         * Pin it so the deque pointer stays valid across nursery resets. */
        WorkItem *item = gc_alloc_raw_pinned(sizeof(WorkItem));
        *item = (WorkItem){
            .kind       = kind,
            .fn         = fn,
            .elems      = elems,
            .results    = results,
            .result     = V_VOID,
            .start      = pos,
            .end        = pos + sz,
            .error      = false,
            .exn        = V_FALSE,
            .n_done     = n_done,
            .done_mutex = done_mutex,
            .done_cond  = done_cond,
        };
        items[c] = item;

        /* Distribute across worker deques round-robin.
         * If a deque is unexpectedly full, execute inline (safety valve). */
        if (!deque_push(&global_pool->deques[c % nw], item)) {
            execute_item(item);
        }
        pos += sz;
    }

    /* Wake any parked workers.  The lock must be held while checking n_parked
     * to close the race where a worker increments n_parked and then calls
     * cond_wait between our relaxed load above and the broadcast. */
    pthread_mutex_lock(&global_pool->park_mutex);
    if (atomic_load_explicit(&global_pool->n_parked, memory_order_relaxed) > 0)
        pthread_cond_broadcast(&global_pool->park_cond);
    pthread_mutex_unlock(&global_pool->park_mutex);

    return items;
}
