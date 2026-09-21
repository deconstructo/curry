#pragma once
/*
 * workpool.h — persistent thread pool with Chase-Lev work-stealing deques.
 *
 * One pool is created at startup (pool_init) and lives for the process
 * lifetime.  Worker threads call gc_register_thread() + vm_init() once
 * and then loop, popping or stealing WorkItems.
 *
 * Used by map / reduce / for-each/par.  The public API in builtins_curry.c
 * calls pool_submit() then waits on the shared done_cond.
 */

#include "value.h"
#include "eval.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ── work item ─────────────────────────────────────────────────────────── */

typedef enum { WORK_MAP, WORK_FOREACH, WORK_REDUCE } WorkKind;

typedef struct {
    WorkKind kind;
    val_t    fn;
    val_t   *elems;
    val_t   *results;   /* MAP: output array; FOREACH/REDUCE: NULL */
    val_t    result;    /* REDUCE: partial result written by worker */
    int      start;
    int      end;
    bool     error;
    val_t    exn;
    atomic_int      *n_done;
    pthread_mutex_t *done_mutex;
    pthread_cond_t  *done_cond;
} WorkItem;

/* ── Chase-Lev work-stealing deque ─────────────────────────────────────── */

#define DEQUE_CAP 512   /* must be a power of 2; enough for 128 workers × 4× */

typedef struct {
    _Atomic int64_t  bottom;
    _Atomic int64_t  top;
    WorkItem       **buf;       /* circular buffer, length DEQUE_CAP */
    /* Textbook Chase-Lev assumes a single fixed owner thread does every
     * push AND pop, with only *steal* coming from other threads -- that's
     * why bottom/buf are touched with relaxed stores guarded by fences
     * rather than a lock. This pool violates that assumption: pool_submit()
     * runs on whichever thread is DISPATCHING work (the main thread, or
     * any actor thread calling map/reduce/for-each/par), and pushes
     * directly into a *worker* thread's deque -- a different thread from
     * the one whose worker_loop calls deque_pop() on that same deque.
     * That makes push and pop genuinely concurrent writers of bottom/buf
     * from two different threads, which the relaxed-atomics/fence scheme
     * cannot make safe (TSan-confirmed data race, issue #243: a torn/lost
     * push means the chunk's completion signal never fires and the
     * dispatcher hangs forever in POOL_WAIT, with every worker idle).
     * owner_mutex serializes push, pop, AND steal against each other --
     * steal() also had to be locked (not just push/pop): once bottom is
     * written by two different threads instead of Chase-Lev's assumed
     * single owner, a bare fence + acquire-load of bottom is no longer a
     * reliable synchronization edge for steal's buf[] read either
     * (also TSan-confirmed racing against a concurrent push). This gives
     * up steal's usual lock-freedom, but steal only runs on an otherwise-
     * idle worker scanning for work, never on a per-item hot path. */
    pthread_mutex_t  owner_mutex;
} WSDeque;

/* ── pool ───────────────────────────────────────────────────────────────── */

typedef struct {
    int          n_workers;
    pthread_t   *threads;
    WSDeque     *deques;
    atomic_bool  shutdown;
    pthread_mutex_t park_mutex;
    pthread_cond_t  park_cond;
    atomic_int      n_parked;
} WorkPool;

/* ── public API ─────────────────────────────────────────────────────────── */

/* Called once from builtins_curry_register().  Idempotent. */
void pool_init(void);

/* True on worker threads; false on the main thread and any non-pool thread.
 * Used by prim_map / prim_reduce to avoid nested parallel dispatch, which
 * would deadlock: all workers blocked waiting on nested work items that
 * can never run because every worker is already occupied. */
extern CURRY_THREAD_LOCAL bool pool_is_worker;

/* Number of logical CPUs (same value the pool uses for its thread count). */
int pool_hw_concurrency(void);

/*
 * Submit a parallel job split into chunks derived from n elements.
 * Writes nchunks_out with the number of chunks dispatched.
 * Returns the WorkItem array so the caller can inspect errors / partial
 * reduce results after waiting for completion.
 *
 * Caller is responsible for:
 *   1. Waiting: pthread_cond_wait until *n_done == *nchunks_out
 *   2. Checking: items[c]->error for each chunk
 *   3. Combining: items[c]->result for WORK_REDUCE
 */
WorkItem **pool_submit(WorkKind kind, val_t fn,
                       val_t *elems, val_t *results, int n,
                       int *nchunks_out,
                       atomic_int *n_done,
                       pthread_mutex_t *done_mutex,
                       pthread_cond_t  *done_cond);
