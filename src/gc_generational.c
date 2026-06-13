/*
 * gc_generational.c — Two-generation GC for Curry Scheme.
 *
 * Milestone 1: card table, write barrier globals, tenured space mmap.
 *   Collection is not yet implemented; allocation falls through to Boehm.
 *
 * Milestone 2 (next): tenured bump-pointer allocator, nursery refill minor GC.
 * Milestone 3 (next): polling safepoints for STW pause.
 * Milestone 4 (next): full minor collection with remembered set.
 *
 * See gc_generational.h for the design overview.
 */

#define GC_THREADS
#include "gc_generational.h"
#include "gc.h"
#include "object.h"
#include "value.h"
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

static uint8_t *tenured_top  = NULL;
static size_t   tenured_cap  = 0;
static size_t   card_count   = 0;
/* tenured_lock protects the bump pointer against concurrent promotion;
 * wired in at milestone 3 (minor collection). */
static pthread_mutex_t tenured_lock __attribute__((unused)) = PTHREAD_MUTEX_INITIALIZER;

/* ── Safepoint ────────────────────────────────────────────────────────────── */

volatile int gc_stop_world = 0;

/* Implemented in milestone 3. */
void gc_gen_safepoint(void) { /* TODO: parking lot for STW pause */ }

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

/* ── Evacuation stub (implemented in milestone 4) ────────────────────────── */

uintptr_t gc_gen_evac(uintptr_t v) { return v; }

/* ── Boehm fall-through backend (milestones 1–3) ────────────────────────── */
/*
 * Until nursery and tenured allocators are wired in (milestones 2–4),
 * allocation goes straight through Boehm.  The nursery fast path remains
 * disabled (top == limit == NULL), so every alloc hits gc_nursery_refill()
 * which calls gc_ops->alloc — i.e., gen_alloc() below.
 */

static void *gen_alloc(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}
static void *gen_alloc_pinned(size_t n, bool has_ptrs) {
    stat_pinned++;
    return gen_alloc(n, has_ptrs);
}
static void *gen_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return gen_alloc(n, has_ptrs);
}
static void gen_collect(void) {
    GC_gcollect();
}
static void gen_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
}
static void gen_pin(void *obj)              { (void)obj; }
static void gen_unpin(void *obj)            { (void)obj; }
static void gen_register_root(void *slot)   { (void)slot; }
static void gen_unregister_root(void *slot) { (void)slot; }
static void gen_write_barrier(void *obj)    { (void)obj; }   /* fast path in macro */
static size_t gen_heap_size(void) {
    size_t tu = (size_t)(tenured_top ? tenured_top - gc_gen_tenured_base : 0);
    return (size_t)GC_get_heap_size() + tu;
}
static size_t gen_free_bytes(void) {
    size_t tu = (size_t)(tenured_top ? tenured_top - gc_gen_tenured_base : 0);
    return (size_t)GC_get_free_bytes() + (tenured_cap - tu);
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

    gc_ops       = &gc_gen_ops;
    gc_gen_active = 1;
}

/* ── Stats ────────────────────────────────────────────────────────────────── */

GcGenStats gc_gen_stats(void) {
    size_t nu = gc_nursery.top ? (size_t)(gc_nursery.top - gc_nursery.base) : 0;
    size_t tu = tenured_top   ? (size_t)(tenured_top - gc_gen_tenured_base) : 0;
    return (GcGenStats){
        .minor_collections = stat_minor,
        .major_collections = stat_major,
        .nursery_bytes     = cfg_nursery_bytes,
        .nursery_used      = nu,
        .tenured_used      = tu,
        .tenured_capacity  = tenured_cap,
        .pinned_count      = stat_pinned,
    };
}
