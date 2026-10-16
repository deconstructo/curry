/*
 * gc.c — garbage collector: Boehm setup, root registration, statistics.
 *
 * Boehm GC is the only collector (see gc.h's header comment for why the
 * generational backend was removed). Allocation is inline in gc.h.
 */

#define GC_THREADS
#include "gc.h"
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

/* ── Inert hooks of the removed moving collector ──────────────────────────── */

CURRY_THREAD_LOCAL GcFrame *gc_shadow_stack  = NULL;
CURRY_THREAD_LOCAL int      gc_inhibit_count = 0;
CURRY_THREAD_LOCAL bool     gc_minor_pending = false;

void gc_register_ext_scanner(void (*cb)(void)) { (void)cb; }
uintptr_t gc_ss_evac(uintptr_t v) { return v; }
void     *gc_ss_fwd(void *p)      { return p; }

/* longjmp-safe shadow-stack helpers (called from SCM_PROTECT) */
void *gc_shadow_save(void)        { return gc_shadow_stack; }
void  gc_shadow_restore(void *p)  { gc_shadow_stack = (GcFrame *)p; }

/* inhibit-counter wrappers for C++ and JIT code */
void gc_inhibit_minor_fn(void) { gc_inhibit_count++; }
void gc_resume_minor_fn(void)  { if (gc_inhibit_count > 0) gc_inhibit_count--; }
int  gc_inhibit_save(void)     { return gc_inhibit_count; }
void gc_inhibit_restore(int v) { gc_inhibit_count = v; }

/* ── Safe points (no-ops; see gc.h) ───────────────────────────────────────── */

void gc_gen_thread_park(void)   {}
void gc_gen_thread_unpark(void) {}

/* ── GC statistics ────────────────────────────────────────────────────────── */

_Atomic uint64_t gc_stat_major_count = 0;

/* Boehm pause timing. Boehm calls gc_boehm_event_cb with its allocation
 * lock held, from the thread running the collection, so the two start
 * timestamps below are only ever touched by one thread at a time. The
 * published counters are atomic because (gc-stats) reads them from any
 * thread.
 *
 * A stop-the-world interval runs from GC_EVENT_PRE_STOP_WORLD to
 * GC_EVENT_POST_START_WORLD: every registered thread is suspended for that
 * long (the collecting thread is busy for it too). In Boehm's incremental
 * mode one collection is split into several such intervals, so these are
 * the pauses mutator threads actually see. A collection (GC_EVENT_START to
 * GC_EVENT_END) is the whole cycle, which in incremental mode can span many
 * intervals with mutator work in between. */
_Atomic uint64_t gc_stat_stw_count       = 0;
_Atomic uint64_t gc_stat_stw_total_ns    = 0;
_Atomic uint64_t gc_stat_stw_max_ns      = 0;
_Atomic uint64_t gc_stat_major_total_ns  = 0;
_Atomic uint64_t gc_stat_major_max_ns    = 0;

static _Atomic uint64_t gc_stw_ring[GC_STW_RING_N];
static _Atomic size_t   gc_stw_ring_head = 0;

static uint64_t gc_stw_start_ns;    /* guarded by Boehm's allocation lock */
static uint64_t gc_major_start_ns;  /* ditto */

static uint64_t gc_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void gc_stat_max(_Atomic uint64_t *slot, uint64_t v) {
    uint64_t old = atomic_load_explicit(slot, memory_order_relaxed);
    while (v > old &&
           !atomic_compare_exchange_weak_explicit(slot, &old, v,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed))
        ;
}

static void gc_boehm_event_cb(GC_EventType ev) {
    switch (ev) {
    case GC_EVENT_START:
        gc_major_start_ns = gc_now_ns();
        break;
    case GC_EVENT_END: {
        uint64_t d = gc_now_ns() - gc_major_start_ns;
        atomic_fetch_add_explicit(&gc_stat_major_count, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&gc_stat_major_total_ns, d, memory_order_relaxed);
        gc_stat_max(&gc_stat_major_max_ns, d);
        break;
    }
    case GC_EVENT_PRE_STOP_WORLD:
        gc_stw_start_ns = gc_now_ns();
        break;
    case GC_EVENT_POST_START_WORLD: {
        uint64_t d = gc_now_ns() - gc_stw_start_ns;
        atomic_fetch_add_explicit(&gc_stat_stw_count, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&gc_stat_stw_total_ns, d, memory_order_relaxed);
        gc_stat_max(&gc_stat_stw_max_ns, d);
        size_t i = atomic_fetch_add_explicit(&gc_stw_ring_head, 1, memory_order_relaxed);
        atomic_store_explicit(&gc_stw_ring[i % GC_STW_RING_N], d, memory_order_relaxed);
        break;
    }
    default:
        break;
    }
}

size_t gc_get_stw_ring(uint64_t *out) {
    size_t head = atomic_load_explicit(&gc_stw_ring_head, memory_order_relaxed);
    size_t n = head < GC_STW_RING_N ? head : GC_STW_RING_N;
    size_t first = head - n;
    for (size_t i = 0; i < n; i++)
        out[i] = atomic_load_explicit(&gc_stw_ring[(first + i) % GC_STW_RING_N],
                                      memory_order_relaxed);
    return n;
}

void gc_reset_boehm_stats(void) {
    atomic_store_explicit(&gc_stat_stw_count,      0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_stw_total_ns,   0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_stw_max_ns,     0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_major_total_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&gc_stat_major_max_ns,   0, memory_order_relaxed);
    atomic_store_explicit(&gc_stw_ring_head,       0, memory_order_relaxed);
}

/* ── Registered roots ─────────────────────────────────────────────────────── */

#define GC_ROOTS_INIT_CAP 32

static val_t **g_roots;         /* registered val_t* slots                    */
static size_t  g_roots_count;
static size_t  g_roots_cap;
static val_t  *g_roots_shadow;  /* GC_MALLOC_UNCOLLECTABLE copies of *g_roots[i]
                                 * taken at registration, so the registered
                                 * value stays alive even when the slot itself
                                 * is in memory Boehm does not scan */

static pthread_mutex_t g_roots_lock_mtx = PTHREAD_MUTEX_INITIALIZER;

static void ensure_roots(void) {
    if (!g_roots) {
        g_roots        = GC_MALLOC_UNCOLLECTABLE(GC_ROOTS_INIT_CAP * sizeof(val_t *));
        g_roots_shadow = GC_MALLOC_UNCOLLECTABLE(GC_ROOTS_INIT_CAP * sizeof(val_t));
        g_roots_cap = GC_ROOTS_INIT_CAP;
    }
}

static void register_root_locked(void *slot) {
    ensure_roots();
    if (g_roots_count == g_roots_cap) {
        size_t nc = g_roots_cap * 2;
        val_t **nr = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(val_t *));
        val_t  *ns = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(val_t));
        memcpy(nr, g_roots,        g_roots_count * sizeof(val_t *));
        memcpy(ns, g_roots_shadow, g_roots_count * sizeof(val_t));
        GC_FREE(g_roots); GC_FREE(g_roots_shadow);
        g_roots = nr; g_roots_shadow = ns; g_roots_cap = nc;
    }
    size_t idx = g_roots_count++;
    g_roots[idx]        = (val_t *)slot;
    g_roots_shadow[idx] = *(val_t *)slot;
    /* No GC_add_roots call needed: the slot is GC_MALLOC_UNCOLLECTABLE, so
     * Boehm conservatively scans it and keeps the pointed-to object alive.
     * GC_add_roots has a fixed-size table (~1024 entries) that overflows when
     * many JIT literals are registered. */
}

void gc_register_root(void *slot) {
    pthread_mutex_lock(&g_roots_lock_mtx);
    register_root_locked(slot);
    pthread_mutex_unlock(&g_roots_lock_mtx);
}

/* Stores v into *slot and registers it as a root in one critical section. */
void gc_register_root_val(void *slot, val_t v) {
    pthread_mutex_lock(&g_roots_lock_mtx);
    *(val_t *)slot = v;
    register_root_locked(slot);
    pthread_mutex_unlock(&g_roots_lock_mtx);
}

void gc_unregister_root(void *slot) {
    pthread_mutex_lock(&g_roots_lock_mtx);
    for (size_t i = 0; i < g_roots_count; i++) {
        if (g_roots[i] == (val_t *)slot) {
            size_t last = --g_roots_count;
            g_roots[i]        = g_roots[last];
            g_roots_shadow[i] = g_roots_shadow[last];
            pthread_mutex_unlock(&g_roots_lock_mtx);
            return;
        }
    }
    pthread_mutex_unlock(&g_roots_lock_mtx);
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_init(void) {
    GC_INIT();
    GC_allow_register_threads();
    GC_set_on_collection_event(gc_boehm_event_cb);
}

void gc_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
}

/* Threads started with pthread_create in a file that includes gc.h go
 * through Boehm's GC_pthread_create redirect, which unregisters them on
 * exit by itself, so there is nothing to do here. Kept so every thread's
 * exit path has a matching call for a future collector. */
void gc_unregister_thread(void) {}

void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd) {
    GC_register_finalizer(obj, (GC_finalization_proc)fn, cd, NULL, NULL);
}

void   gc_set_max_heap(size_t bytes)    { GC_set_max_heap_size((GC_word)bytes); }
void   gc_set_free_space_divisor(int n) { GC_set_free_space_divisor((GC_word)n); }
void   gc_enable_incremental(void)      { GC_enable_incremental(); }
size_t gc_heap_size(void)               { return (size_t)GC_get_heap_size(); }
size_t gc_free_bytes(void)              { return (size_t)GC_get_free_bytes(); }
size_t gc_total_bytes(void)             { return (size_t)GC_get_total_bytes(); }
