/*
 * gc_gen.c — Generational GC backend.
 *
 * Phase 3 (current): nursery bump-pointer allocation + Boehm fallback on
 * overflow.  No minor GC yet — that is Phase 4.
 *
 * Phase 4 will replace gc_nursery_refill() with a stop-the-world minor GC:
 *   1. Evacuate live nursery objects to Boehm (promotion via gc_ops->promote).
 *   2. Update roots: shadow stack, VM value stacks, registered val_t roots,
 *      pinned cross-heap object scan (scan_pinned_object), ext scanners.
 *   3. Scan dirty cards for old→young references.
 *   4. Reset nursery (base..limit), clear card table.
 */

#define GC_THREADS
#include "gc_gen.h"
#include "gc.h"
#include "object.h"   /* Hdr, GC_FORWARDED */
#include <gc/gc.h>
#include <gc/gc_mark.h>  /* GC_least/greatest_plausible_heap_addr (global vars) */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Nursery size ─────────────────────────────────────────────────────────── */

#define GEN_NURSERY_DEFAULT_BYTES (512u * 1024u)

static size_t gen_nursery_size = GEN_NURSERY_DEFAULT_BYTES;

void gc_gen_set_nursery_size(size_t bytes) {
    gen_nursery_size = bytes ? bytes : GEN_NURSERY_DEFAULT_BYTES;
}

/* ── Per-thread nursery slab ──────────────────────────────────────────────── */

static void alloc_thread_nursery(void) {
    size_t sz = gen_nursery_size;
    uint8_t *slab = (uint8_t *)GC_MALLOC_UNCOLLECTABLE(sz);
    if (!slab) {
        fprintf(stderr, "[gc_gen] FATAL: cannot allocate nursery slab (%zu KB)\n",
                sz / 1024);
        abort();
    }
    memset(slab, 0, sz);
    gc_nursery.base  = slab;
    gc_nursery.top   = slab;
    gc_nursery.limit = slab + sz;
}

/* ── Card table ───────────────────────────────────────────────────────────── */

static void init_card_table(void) {
    uintptr_t base = (uintptr_t)GC_least_plausible_heap_addr;
    uintptr_t top  = (uintptr_t)GC_greatest_plausible_heap_addr;

    /* Round outward to card boundaries. */
    base &= ~(uintptr_t)(GC_CARD_BYTES - 1u);
    top   = (top + GC_CARD_BYTES - 1u) & ~(uintptr_t)(GC_CARD_BYTES - 1u);

    if (top <= base) {
        /* Boehm hasn't committed any heap yet — use a generous default range. */
        base = 0;
        top  = (uintptr_t)4 * 1024 * 1024 * 1024;  /* 4 GB */
    }

    size_t ncards = (top - base) / GC_CARD_BYTES;
    gc_tenured_base = base;
    gc_card_table   = (uint8_t *)GC_MALLOC_ATOMIC(ncards);
    if (!gc_card_table) {
        fprintf(stderr, "[gc_gen] FATAL: cannot allocate card table (%zu KB)\n",
                ncards / 1024);
        abort();
    }
    memset(gc_card_table, 0, ncards);
}

/* ── vtable implementation ────────────────────────────────────────────────── */

/*
 * gen_alloc: bump-pointer from the per-thread nursery slab.
 * On overflow, gc_nursery_alloc() calls gc_nursery_refill() which — in
 * Phase 3 — falls back to GC_MALLOC directly (see gc.c).  Phase 4 will
 * replace that fallback with a minor GC + nursery reset.
 */
static void *gen_alloc(size_t n, bool has_ptrs) {
    return gc_nursery_alloc(n, has_ptrs);
}

/* Pinned objects always go to Boehm — they are never in the nursery. */
static void *gen_alloc_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}

static void *gen_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}

/* Phase 3: collect triggers Boehm major GC only.
 * Phase 4 will call gc_gen_minor_collect() first. */
static void gen_collect(void) {
    GC_gcollect();
}

static void gen_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    alloc_thread_nursery();
}

/* Promotion (Phase 4): copy a live nursery object into Boehm and return the
 * new address.  Phase 3 doesn't call this — it is wired up by the minor GC
 * in gc_gen.c Phase 4.  Provided here so the vtable slot is always valid. */
static void *gen_promote(void *obj, size_t bytes, bool has_ptrs) {
    void *dst = has_ptrs ? GC_MALLOC(bytes) : GC_MALLOC_ATOMIC(bytes);
    if (!dst) { fprintf(stderr, "[gc_gen] OOM in promote\n"); abort(); }
    memcpy(dst, obj, bytes);
    /* Caller stamps GC_FORWARDED in the old nursery copy. */
    return dst;
}

static void  gen_pin(void *obj)              { (void)obj; }
static void  gen_unpin(void *obj)            { (void)obj; }
static void  gen_register_root(void *slot)   { (void)slot; }
static void  gen_unregister_root(void *slot) { (void)slot; }
static size_t gen_heap_size(void)  { return (size_t)GC_get_heap_size();  }
static size_t gen_free_bytes(void) { return (size_t)GC_get_free_bytes(); }

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
    .promote           = gen_promote,
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_gen_init(size_t nursery_bytes) {
    if (nursery_bytes) gen_nursery_size = nursery_bytes;

    /* Boehm must be initialised first so GC_least/greatest_plausible_heap_addr
     * return meaningful values and GC_MALLOC_UNCOLLECTABLE is available. */
    GC_INIT();
    GC_allow_register_threads();

    init_card_table();
    alloc_thread_nursery();   /* main thread nursery */
}
