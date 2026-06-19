/*
 * gc.c — GC lifecycle, Boehm backend, and per-thread nursery.
 *
 * The active backend is selected at gc_init() time via the gc_ops pointer.
 * Currently only the Boehm backend exists; a precise generational backend
 * will be added later and selected by setting gc_ops = &gc_boehm_ops (or a
 * new gc_gen_ops) before calling gc_init().
 *
 * Nursery (per-thread bump-pointer arena)
 * ────────────────────────────────────────
 * Each thread gets a GcNursery.  The fast path is a single pointer bump in
 * thread-local storage — no locks, no function calls.  When the nursery is
 * exhausted, gc_nursery_refill() is called; under the Boehm backend it
 * allocates a fresh slab from Boehm and resets the nursery, so there is no
 * minor collection yet.  The interface is correct for a future minor GC pass.
 *
 * Nursery slab size: 256 KB per thread.  Large enough to absorb typical
 * allocation bursts; small enough that Boehm's stop-the-world pause doesn't
 * grow unboundedly per thread.  Will be tunable once the generational GC lands.
 */

#define GC_THREADS
#include "gc.h"
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>

/* ── Boehm backend ──────────────────────────────────────────────────────── */

static void *boehm_alloc(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}
/* Under Boehm, pinned allocation is identical to regular allocation.
 * The semispace backend overrides these to bypass the nursery. */
static void *boehm_alloc_pinned(size_t n, bool has_ptrs)     { return boehm_alloc(n, has_ptrs); }
static void *boehm_alloc_raw_pinned(size_t n, bool has_ptrs) { return boehm_alloc(n, has_ptrs); }

static void  boehm_collect(void)           { GC_gcollect(); }
static void  boehm_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
}
/* Boehm is conservative — pin/unpin and root registration are no-ops. */
static void  boehm_pin(void *obj)              { (void)obj; }
static void  boehm_unpin(void *obj)            { (void)obj; }
static void  boehm_register_root(void *slot)   { (void)slot; }
static void  boehm_unregister_root(void *slot) { (void)slot; }
static size_t boehm_heap_size(void)  { return (size_t)GC_get_heap_size();  }
static size_t boehm_free_bytes(void) { return (size_t)GC_get_free_bytes(); }

static gc_ops_t gc_boehm_ops = {
    .alloc             = boehm_alloc,
    .alloc_pinned      = boehm_alloc_pinned,
    .alloc_raw_pinned  = boehm_alloc_raw_pinned,
    .collect           = boehm_collect,
    .register_thread   = boehm_register_thread,
    .pin               = boehm_pin,
    .unpin             = boehm_unpin,
    .register_root     = boehm_register_root,
    .unregister_root   = boehm_unregister_root,
    .heap_size         = boehm_heap_size,
    .free_bytes        = boehm_free_bytes,
};

gc_ops_t *gc_ops = &gc_boehm_ops;

/* ── Per-thread nursery ─────────────────────────────────────────────────── */

_Thread_local GcNursery gc_nursery = { NULL, NULL, NULL };

/* Shadow stack TLS root — head of the per-thread GcFrame linked list.
 * NULL when no eval() frame is active (e.g. at top level between expressions).
 * Under Boehm this is never read by the GC; it exists so that GC_AUTOFRAME
 * in eval.c compiles cleanly and a debug assertion can verify balance. */
_Thread_local GcFrame *gc_shadow_stack = NULL;

/* Globals required by the write barrier when CURRY_GC_PRECISE is defined.
 * Under Boehm they are never read; they are NULL/0 by default. */
uint8_t  *gc_card_table   = NULL;
uintptr_t gc_tenured_base = 0;

#define NURSERY_SLAB_BYTES  (256u * 1024u)  /* 256 KB per thread */

/*
 * Refill the nursery with a fresh slab and allocate n bytes from it.
 * Called only on the slow path (nursery exhausted or not yet initialised).
 *
 * Under Boehm: each slab is a GC_MALLOC allocation, so Boehm owns and scans
 * it.  The nursery top/limit are just a view into that allocation.
 *
 * Under a future generational GC: this is where a minor collection fires,
 * live objects are promoted, and the nursery is reset to its original extent.
 */
void *gc_nursery_refill(size_t n, bool has_ptrs) {
    /*
     * Boehm backend: the nursery slab is disabled.
     *
     * Interior pointers into a single GC_MALLOC slab are not individually
     * tracked by Boehm — finalisers can only be registered on the start of
     * an allocation, and the atomic/non-atomic distinction applies per-object.
     * Packing multiple Scheme objects into one slab is therefore unsafe under
     * Boehm; each object must be its own GC_MALLOC call.
     *
     * The nursery stays permanently exhausted (top == limit == NULL) so every
     * allocation hits this refill path and goes straight to gc_ops->alloc.
     * The slab mechanism becomes active when a precise generational GC backend
     * is installed — at that point objects within the slab are individually
     * understood by the collector, and the fast bump-pointer path becomes real.
     */
    return gc_ops->alloc(n, has_ptrs);
}

/* C-linkage allocator called from C++ translation units (jit.cpp).
 * C++ code cannot safely reference gc_nursery via 'extern thread_local' because
 * the C++ TLS wrapper symbol (_ZTW…) is ABI-incompatible with the C TLS symbol
 * ($tlv$init) on macOS/arm64. */
void *gc_alloc_impl(size_t n, int has_ptrs) {
    return gc_nursery_alloc(n, (bool)has_ptrs);
}
void *gc_alloc_pinned_impl(size_t n, int has_ptrs) {
    return gc_ops->alloc_pinned(n, (bool)has_ptrs);
}

/* ── Raw-pointer and stack root registration (no-ops under Boehm) ─────────── */

void gc_register_rawptr(void **rawptr)   { (void)rawptr; }
void gc_unregister_rawptr(void **rawptr) { (void)rawptr; }
void gc_register_stack(void *base, void **sp_ptr) { (void)base; (void)sp_ptr; }
void gc_unregister_stack(void *base)     { (void)base; }

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void gc_init(void) {
    GC_INIT();
    GC_allow_register_threads();
}

void gc_register_thread(void) {
    gc_ops->register_thread();
}

void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd) {
    GC_register_finalizer(obj, (GC_finalization_proc)fn, cd, NULL, NULL);
}

void   gc_set_max_heap(size_t bytes)    { GC_set_max_heap_size((GC_word)bytes); }
void   gc_set_free_space_divisor(int n) { GC_set_free_space_divisor((GC_word)n); }
void   gc_enable_incremental(void)      { GC_enable_incremental(); }
size_t gc_heap_size(void)               { return gc_ops->heap_size();   }
size_t gc_free_bytes(void)              { return gc_ops->free_bytes();  }
size_t gc_total_bytes(void)             { return (size_t)GC_get_total_bytes(); }
