/*
 * gc_generational.c — Two-generation GC for Curry Scheme.
 *
 * Milestone 1: card table, write barrier globals, tenured space mmap.
 * Milestone 2: GC age bits (object.h), tenured bump-pointer allocator,
 *   nursery refill hook (gc_nursery_refill_fn).
 *
 * Pending milestones:
 *   3 — polling safepoints (gc_stop_world / gc_gen_safepoint)
 *   4 — minor collection: Cheney nursery→tenured + remembered set
 *   5 — write barrier instrumentation at all mutation sites
 *   6 — major collection trigger (tenured > GC_TENURED_FILL_PCT)
 *
 * See gc_generational.h for the design overview.
 */

#define GC_THREADS
#include "gc_generational.h"
#include "gc.h"
#include "object.h"
#include <gc/gc.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Write barrier globals (extern in gc.h) ──────────────────────────────── */

volatile int  gc_gen_active       = 0;
uint8_t      *gc_gen_tenured_base  = NULL;
uint8_t      *gc_gen_tenured_limit = NULL;
uint8_t      *gc_gen_card_table    = NULL;

/* ── Tenured space ───────────────────────────────────────────────────────── */

static uint8_t        *tenured_top  = NULL;
static size_t          tenured_cap  = 0;
static size_t          card_count   = 0;
static pthread_mutex_t tenured_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * gen_tenured_alloc — thread-safe bump-pointer allocation in Gen1.
 *
 * Called from minor collection (milestone 4) when promoting a live nursery
 * object to Gen1.  Returns NULL when tenured space is full; the caller must
 * then trigger a major collection before retrying.
 *
 * Objects allocated here are marked GC_AGE_GEN1 in their Hdr.flags by the
 * minor collector after copying.
 */
void *gen_tenured_alloc(size_t n) {
    n = (n + 7u) & ~7u;  /* 8-byte align */
    pthread_mutex_lock(&tenured_lock);
    uint8_t *p = tenured_top;
    if (p + n > gc_gen_tenured_limit) {
        pthread_mutex_unlock(&tenured_lock);
        return NULL;  /* tenured full — caller triggers major GC */
    }
    tenured_top += n;
    pthread_mutex_unlock(&tenured_lock);
    return p;
}

/* Current tenured usage in bytes (safe to call without the lock for stats). */
size_t gen_tenured_used(void) {
    return tenured_top ? (size_t)(tenured_top - gc_gen_tenured_base) : 0;
}

/* ── Nursery refill hook ─────────────────────────────────────────────────── */

/*
 * gc_nursery_refill_fn — pluggable slow path for nursery exhaustion.
 *
 * Set by the generational backend in gc_gen_init().  Called by
 * gc_nursery_refill() in gc.c when the per-thread nursery bump pointer
 * reaches its limit.
 *
 * NULL (the default) means "fall through to gc_ops->alloc()" — the Boehm
 * and semispace behaviours.  Milestone 4 installs a function here that
 * triggers a minor collection, resets the nursery, and returns the
 * newly-allocated object from the fresh nursery.
 */
void *(*gc_nursery_refill_fn)(size_t n, bool has_ptrs) = NULL;

/* ── Safepoint ────────────────────────────────────────────────────────────── */

volatile int gc_stop_world = 0;

void gc_gen_safepoint(void) {
    /* Milestone 3: threads park here during STW pause.
     * For now this is a no-op — all collections run only on the allocating
     * thread, which is safe in the single-threaded phase of development. */
}

/* ── External root scanners ──────────────────────────────────────────────── */

#define MAX_EXT_SCANNERS 16
static void (*ext_scanners[MAX_EXT_SCANNERS])(void);
static int   ext_scanner_count = 0;

void gc_gen_register_ext_scanner(void (*cb)(void)) {
    if (ext_scanner_count < MAX_EXT_SCANNERS)
        ext_scanners[ext_scanner_count++] = cb;
}

/* ── Statistics ──────────────────────────────────────────────────────────── */

static uint64_t stat_minor        = 0;
static uint64_t stat_major        = 0;
static size_t   cfg_nursery_bytes = GC_NURSERY_DEFAULT_BYTES;
static size_t   stat_pinned       = 0;

static void (*gen_hook)(void) = NULL;

void gc_gen_set_hook(void (*hook)(void)) { gen_hook = hook; }

void gc_gen_bump_minor(void) { stat_minor++; if (gen_hook) gen_hook(); }
void gc_gen_bump_major(void) { stat_major++; if (gen_hook) gen_hook(); }

/* ── Evacuation (implemented in milestone 4) ─────────────────────────────── */

uintptr_t gc_gen_evac(uintptr_t v) { return v; }

/* ── gc_ops_t backend (Boehm fall-through until milestone 4) ─────────────── */
/*
 * Until minor/major collection are wired in, all allocation goes through
 * Boehm.  The nursery fast path remains disabled (gc_nursery.top == NULL).
 * This changes in milestone 4 when gc_nursery_refill_fn is installed and
 * gen_register_thread() allocates a per-thread nursery slab.
 */

static void *gen_alloc(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}
static void *gen_alloc_pinned(size_t n, bool has_ptrs) {
    void *p = has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
    if (p) {
        stat_pinned++;
        /* Mark pinned so the minor collector skips it. */
        if (has_ptrs)
            hdr_set_age((Hdr *)p, GC_AGE_PINNED >> GC_AGE_SHIFT);
    }
    return p;
}
static void *gen_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}
static void gen_collect(void) { GC_gcollect(); }
static void gen_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
}
static void gen_pin(void *obj)              { (void)obj; }
static void gen_unpin(void *obj)            { (void)obj; }
static void gen_register_root(void *slot)   { (void)slot; }
static void gen_unregister_root(void *slot) { (void)slot; }
static void gen_write_barrier(void *obj)    { (void)obj; }  /* fast path in macro */
static size_t gen_heap_size(void) {
    return (size_t)GC_get_heap_size() + gen_tenured_used();
}
static size_t gen_free_bytes(void) {
    return (size_t)GC_get_free_bytes() + (tenured_cap - gen_tenured_used());
}

gc_ops_t gc_gen_ops = {
    .alloc             = gen_alloc,
    .alloc_pinned      = gen_alloc_pinned,
    .alloc_raw_pinned  = gen_alloc_raw_pinned,
    .collect           = gen_collect,
    .register_thread   = gen_register_thread,
    .pin               = gen_pin,
    .unpin             = gen_unpin,
    .register_root     = gen_register_root,
    .unregister_root   = gen_unregister_root,
    .heap_size         = gen_heap_size,
    .free_bytes        = gen_free_bytes,
    .write_barrier     = gen_write_barrier,
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_gen_init(size_t nursery_bytes, size_t tenured_bytes) {
    if (nursery_bytes == 0) nursery_bytes = GC_NURSERY_DEFAULT_BYTES;
    if (tenured_bytes == 0) tenured_bytes = GC_TENURED_DEFAULT_BYTES;

    cfg_nursery_bytes = nursery_bytes;
    tenured_cap       = tenured_bytes;

    void *t = mmap(NULL, tenured_bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (t == MAP_FAILED) {
        perror("[gc_gen] mmap tenured");
        abort();
    }
    gc_gen_tenured_base  = (uint8_t *)t;
    gc_gen_tenured_limit = (uint8_t *)t + tenured_bytes;
    tenured_top          = (uint8_t *)t;

    card_count        = (tenured_bytes + GC_CARD_BYTES - 1) >> GC_CARD_SHIFT;
    gc_gen_card_table = (uint8_t *)calloc(card_count, 1);
    if (!gc_gen_card_table) {
        perror("[gc_gen] card table alloc");
        abort();
    }

    gc_ops        = &gc_gen_ops;
    gc_gen_active = 1;
}

/* ── Stats ────────────────────────────────────────────────────────────────── */

GcGenStats gc_gen_stats(void) {
    size_t nu = gc_nursery.top ? (size_t)(gc_nursery.top - gc_nursery.base) : 0;
    return (GcGenStats){
        .minor_collections = stat_minor,
        .major_collections = stat_major,
        .nursery_bytes     = cfg_nursery_bytes,
        .nursery_used      = nu,
        .tenured_used      = gen_tenured_used(),
        .tenured_capacity  = tenured_cap,
        .pinned_count      = stat_pinned,
    };
}
