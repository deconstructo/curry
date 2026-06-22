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
#include <stdio.h>

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
/* Boehm objects never live in the nursery — promote is a no-op. */
static void *boehm_promote(void *obj, size_t bytes, bool has_ptrs) {
    (void)bytes; (void)has_ptrs; return obj;
}

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
    .promote           = boehm_promote,
};

gc_ops_t *gc_ops = &gc_boehm_ops;

/* ── Per-thread nursery ─────────────────────────────────────────────────── */

_Thread_local GcNursery gc_nursery = { NULL, NULL, NULL };

void   *gc_alloc_trace[GC_ALLOC_TRACE_N];
size_t  gc_alloc_trace_idx;
size_t  gc_alloc_trace_sz[GC_ALLOC_TRACE_N];

/* Shadow stack TLS root — head of the per-thread GcFrame linked list.
 * NULL when no eval() frame is active (e.g. at top level between expressions).
 * Under Boehm this is never read by the GC; it exists so that GC_AUTOFRAME
 * in eval.c compiles cleanly and a debug assertion can verify balance. */
_Thread_local GcFrame *gc_shadow_stack = NULL;

/* Counter for gc_inhibit_minor() / gc_resume_minor().
 * > 0 means we're inside compiler/reader/other unsafe C code;
 * gc_nursery_refill falls back to Boehm instead of triggering minor GC. */
_Thread_local int gc_inhibit_count = 0;

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
     * Slow path: nursery exhausted (or disabled under Boehm where top==NULL).
     *
     * Under Boehm: top==limit==NULL, so every allocation reaches here.
     * Call GC_MALLOC directly so each object is its own Boehm allocation
     * (required for correct finaliser registration and atomic distinction).
     *
     * Under the generational GC (Phase 4): trigger a minor collection, reset
     * the nursery, then retry the bump pointer.  If the object is larger than
     * the nursery slab, promote it directly to Boehm.
     *
     * Do NOT call gc_ops->alloc() — that recurses back through gc_nursery_alloc
     * and causes infinite recursion.
     */
    if (gc_nursery.base) {
        /*
         * Generational backend.
         *
         * Safe-point check: only fire minor GC when the tree-walking evaluator
         * is NOT active.  The tree-walker (eval.c) pushes a GcFrame onto
         * gc_shadow_stack at entry; when the shadow stack is non-empty we are
         * inside an eval() call that holds untracked C-local val_t pointers.
         * Triggering a minor GC at that point would leave those locals stale.
         *
         * The bytecode VM dispatch loop runs with gc_shadow_stack == NULL
         * (no eval() frame active), so it IS a safe point for collection.
         * When the VM calls call_foreign() → apply_arr() → eval(), the shadow
         * stack becomes non-NULL and minor GC is inhibited until eval() returns.
         *
         * Allocations during tree-walker execution that overflow the nursery
         * fall back to Boehm (same as Phase 3).  They are collected by Boehm's
         * own major GC.
         */
        if (gc_shadow_stack == NULL && gc_inhibit_count == 0) {
            extern void gc_gen_minor_collect(void);
            gc_gen_minor_collect();
            /* Retry bump pointer after collection */
            n = (n + 7u) & ~7u;
            if (gc_nursery.top + n <= gc_nursery.limit) {
                void *p = gc_nursery.top;
                gc_nursery.top += n;
                return p;
            }
        }
        /* Inside tree-walker (shadow stack non-empty), or object too large
         * for the slab after collection — fall back to Boehm directly. */
    }
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
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

/* ── Global root/stack/ext-scanner registries ────────────────────────────── */
/*
 * These registries are always maintained regardless of GC backend.  Under Boehm
 * they are never read (conservative scan handles everything).  Under the
 * generational GC, gc_gen_minor_collect() reads them to find all roots.
 *
 * Capacities start small; arrays are realloc'd as needed.
 */

#define GC_ROOTS_INIT_CAP   32
#define GC_STACKS_INIT_CAP   4
#define GC_EXT_INIT_CAP     16

typedef struct { val_t *base; val_t **sp_ptr; } GcStackRange;

/* Non-static so gc_gen.c can read them via extern declarations in gc_gen.h. */
val_t       **g_roots;         /* registered val_t* roots                     */
size_t        g_roots_count;
size_t        g_roots_cap;

/*
 * Shadow copy of root VALUES (not pointers) in a GC_MALLOC_UNCOLLECTABLE block.
 * Boehm's conservative scan sees this block and marks the val_t heap targets live.
 * Without this, val_t* roots in .data/.bss are below Boehm's plausible heap range
 * and are invisible to Boehm's conservative scan, so referenced objects can be freed.
 */
val_t         *g_roots_shadow;  /* GC_MALLOC_UNCOLLECTABLE, holds copies of *g_roots[i] */

GcStackRange *g_stacks;        /* registered VM value-stack ranges            */
size_t        g_stacks_count;
size_t        g_stacks_cap;

void        (**g_ext_scanners)(void);  /* external scanner callbacks          */
size_t        g_ext_count;
size_t        g_ext_cap;

/* Function pointers set during minor GC so gc_ss_evac/gc_ss_fwd delegate to
 * gc_gen.c's evacuator without a circular dependency.  NULL outside collection. */
uintptr_t (*gc_evac_fn)(uintptr_t) = NULL;
void     *(*gc_fwd_fn)(void *)      = NULL;

static void ensure_roots(void) {
    if (!g_roots) {
        g_roots        = GC_MALLOC_UNCOLLECTABLE(GC_ROOTS_INIT_CAP * sizeof(val_t *));
        g_roots_shadow = GC_MALLOC_UNCOLLECTABLE(GC_ROOTS_INIT_CAP * sizeof(val_t));
        g_roots_cap = GC_ROOTS_INIT_CAP;
    }
}
static void ensure_stacks(void) {
    if (!g_stacks) {
        g_stacks = GC_MALLOC_UNCOLLECTABLE(GC_STACKS_INIT_CAP * sizeof(GcStackRange));
        g_stacks_cap = GC_STACKS_INIT_CAP;
    }
}
static void ensure_ext(void) {
    if (!g_ext_scanners) {
        g_ext_scanners = GC_MALLOC_UNCOLLECTABLE(GC_EXT_INIT_CAP * sizeof(void (*)(void)));
        g_ext_cap = GC_EXT_INIT_CAP;
    }
}

void gc_register_rawptr(void **rawptr)   { (void)rawptr; }
void gc_unregister_rawptr(void **rawptr) { (void)rawptr; }

void gc_register_root(void *slot) {
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
    val_t cur = *(val_t *)slot;
    g_roots_shadow[idx] = cur;
    /* Register the val_t slot itself as a root range (covers .data/.bss addresses). */
    GC_add_roots(slot, (char *)slot + sizeof(val_t));
    /*
     * Also register the OBJECT pointed to by the root value as a root range.
     * Boehm's GC_add_roots_inner checks if the base is a heap pointer and
     * explicitly marks the containing block as live — this is the reliable
     * mechanism for .data-resident val_t roots on macOS arm64.
     */
    if (cur & ~3u) {  /* quick heap-ptr check: non-zero and tag==0 */
        uintptr_t p = cur & ~(uintptr_t)3u;
        GC_add_roots((char *)p, (char *)p + sizeof(val_t));
    }
}

void gc_unregister_root(void *slot) {
    for (size_t i = 0; i < g_roots_count; i++) {
        if (g_roots[i] == (val_t *)slot) {
            size_t last = --g_roots_count;
            g_roots[i]        = g_roots[last];
            g_roots_shadow[i] = g_roots_shadow[last];
            GC_remove_roots(slot, (char *)slot + sizeof(val_t));
            return;
        }
    }
}

void gc_register_stack(void *base, void **sp_ptr) {
    ensure_stacks();
    if (g_stacks_count == g_stacks_cap) {
        size_t nc = g_stacks_cap * 2;
        GcStackRange *ns = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(GcStackRange));
        memcpy(ns, g_stacks, g_stacks_count * sizeof(GcStackRange));
        GC_FREE(g_stacks);
        g_stacks = ns; g_stacks_cap = nc;
    }
    g_stacks[g_stacks_count].base   = (val_t *)base;
    g_stacks[g_stacks_count].sp_ptr = (val_t **)sp_ptr;
    g_stacks_count++;
}

void gc_unregister_stack(void *base) {
    for (size_t i = 0; i < g_stacks_count; i++) {
        if (g_stacks[i].base == (val_t *)base) {
            g_stacks[i] = g_stacks[--g_stacks_count];
            return;
        }
    }
}

void gc_register_ext_scanner(void (*cb)(void)) {
    ensure_ext();
    if (g_ext_count == g_ext_cap) {
        size_t nc = g_ext_cap * 2;
        void (**ns)(void) = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(void (*)(void)));
        memcpy(ns, g_ext_scanners, g_ext_count * sizeof(void (*)(void)));
        GC_FREE(g_ext_scanners);
        g_ext_scanners = ns; g_ext_cap = nc;
    }
    g_ext_scanners[g_ext_count++] = cb;
}

/* gc_gen.c accesses the registries via extern declarations in gc_gen.h */

uintptr_t gc_ss_evac(uintptr_t v) { return gc_evac_fn ? gc_evac_fn(v) : v; }
void     *gc_ss_fwd(void *p)      { return gc_fwd_fn  ? gc_fwd_fn(p)  : p; }

/* ── longjmp-safe shadow-stack helpers (called from SCM_PROTECT) ──────── */

void *gc_shadow_save(void)        { return gc_shadow_stack; }
void  gc_shadow_restore(void *p)  { gc_shadow_stack = (GcFrame *)p; }

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
