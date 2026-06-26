#ifndef CURRY_GC_H
#define CURRY_GC_H

/*
 * Garbage collector abstraction for Curry Scheme — v2 design.
 *
 * Architecture
 * ─────────────
 * Three layers:
 *
 *   1. Per-thread nursery (GcNursery) — bump-pointer allocation, no locks.
 *      Fast path: pointer increment in thread-local storage.
 *      When the nursery is exhausted, gc_nursery_refill() is called; it either
 *      minor-collects and resets the nursery, or falls back to gc_ops->alloc.
 *
 *   2. GC vtable (gc_ops_t) — all GC operations go through here.
 *      The initial backend is Boehm; it can be replaced with a precise
 *      generational collector without touching allocation call sites.
 *
 *   3. Object header forwarding (see object.h) — every heap object carries
 *      a `fwd` field that the GC writes when evacuating an object.  Zero
 *      during normal execution.
 *
 * C/C++ module interop
 * ─────────────────────
 * Boehm conservatively scans C stack frames, so existing C modules need no
 * changes.  When a precise GC replaces Boehm, C modules that store Scheme
 * pointers in heap-allocated structs or globals must use gc_pin/gc_unpin or
 * gc_register_root/gc_unregister_root to keep those references live.
 *
 * Thread safety
 * ─────────────
 * Each thread owns its own GcNursery — allocation is lock-free on the fast
 * path.  The shared tenured space is protected by the GC implementation.
 * Call gc_register_thread() at the start of every new pthread.
 */

#define GC_THREADS
#include <gc/gc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── GC vtable ────────────────────────────────────────────────────────────── */

typedef struct gc_ops {
    /*
     * Allocate `bytes` bytes.  `has_ptrs` true means the object may contain
     * GC-managed pointers (use GC_MALLOC); false means it is atomic (use
     * GC_MALLOC_ATOMIC or equivalent — no interior pointer scan needed).
     */
    void *(*alloc)(size_t bytes, bool has_ptrs);

    /*
     * Allocate a TYPED heap object that must never be moved by a copying GC.
     * Use for objects with embedded mutexes, GMP data, FILE*, or jmp_buf.
     * When has_ptrs=true the object is added to the semispace pinned-list so
     * its val_t fields are scanned during collection; when false it is not.
     * Under Boehm this is identical to alloc().
     */
    void *(*alloc_pinned)(size_t bytes, bool has_ptrs);

    /*
     * Allocate an UNTYPED raw array (no Hdr, no ObjType) in non-moving space.
     * Use for val_t[], uint32_t[], uint8_t[] helper arrays owned by a heap
     * object.  Never added to the pinned-list.
     * Under Boehm identical to alloc().
     */
    void *(*alloc_raw_pinned)(size_t bytes, bool has_ptrs);

    /* Trigger a collection cycle. */
    void  (*collect)(void);

    /* Called once per new pthread before any allocation on that thread. */
    void  (*register_thread)(void);

    /*
     * Pin/unpin: prevent the collector from moving `obj`.
     * Required when a C extension stores a raw Scheme pointer in a struct or
     * global that lives across a GC point and is not on the C stack.
     * Pin/unpin calls must be balanced.  Pinning is a no-op under Boehm.
     */
    void  (*pin)(void *obj);
    void  (*unpin)(void *obj);

    /*
     * Register a val_t slot as a GC root.  The slot must remain valid until
     * gc_unregister_root is called.  No-op under Boehm (conservative scan
     * finds it anyway), but required for precise GC.
     */
    void  (*register_root)(void *slot);
    void  (*unregister_root)(void *slot);

    /* Optional: GC heap statistics. */
    size_t (*heap_size)(void);
    size_t (*free_bytes)(void);

    /*
     * Promote an object from the nursery into the tenured generation.
     * Called during minor GC to copy a live nursery object into Boehm.
     * Returns the new tenured address; the caller stamps GC_FORWARDED in the
     * old nursery copy.  No-op under Boehm (objects are never in nursery).
     */
    void *(*promote)(void *obj, size_t bytes, bool has_ptrs);
} gc_ops_t;

/* Active GC backend.  Set during gc_init(); never NULL after that. */
extern gc_ops_t *gc_ops;

/* ── Per-thread nursery ───────────────────────────────────────────────────── */

typedef struct {
    uint8_t *base;   /* start of nursery buffer                  */
    uint8_t *limit;  /* one past the end                         */
    uint8_t *top;    /* bump pointer; next allocation starts here */
} GcNursery;

#ifndef __cplusplus
extern _Thread_local GcNursery gc_nursery;
#endif

/*
 * Slow path: called when top + n > limit.  Returns a pointer to n bytes.
 *
 * If gc_nursery_refill_fn is non-NULL (set by the generational backend),
 * it is called to trigger a minor collection, reset the nursery, and
 * return the newly allocated object.  Otherwise falls through to
 * gc_ops->alloc (Boehm / semispace behaviour).
 */
void *gc_nursery_refill(size_t n, bool has_ptrs);


/* C-linkage allocator entry points used by C++ callers (avoids C++ TLS
 * wrapper generation for gc_nursery which is incompatible with the C TLS ABI
 * on macOS/arm64). */
#ifdef __cplusplus
extern "C" {
    void *gc_alloc_impl(size_t n, int has_ptrs);
    void *gc_alloc_pinned_impl(size_t n, int has_ptrs);
}
#endif

/*
 * Fast-path nursery allocation (C only — C++ uses gc_alloc_impl).
 * Inline bump-pointer; falls through to gc_nursery_refill on exhaustion.
 * `has_ptrs` must be a compile-time constant for the branch to be folded.
 */
#ifndef __cplusplus
/* Ring buffer of last 16 nursery allocations for GC crash forensics */
#define GC_ALLOC_TRACE_N 16
extern void *gc_alloc_trace[GC_ALLOC_TRACE_N];
extern size_t gc_alloc_trace_idx;
extern size_t gc_alloc_trace_sz[GC_ALLOC_TRACE_N];

static inline void *gc_nursery_alloc(size_t n, bool has_ptrs) {
    /* Align to 8 bytes (all Curry heap objects require 8-byte alignment). */
    n = (n + 7u) & ~7u;
    uint8_t *p = gc_nursery.top;
    uint8_t *next = p + n;
    if (__builtin_expect(next > gc_nursery.limit, 0))
        return gc_nursery_refill(n, has_ptrs);
    gc_nursery.top = next;
#ifdef CURRY_GC_DEBUG_TRACE
    gc_alloc_trace[gc_alloc_trace_idx % GC_ALLOC_TRACE_N] = p;
    gc_alloc_trace_sz[gc_alloc_trace_idx % GC_ALLOC_TRACE_N] = n;
    gc_alloc_trace_idx++;
#endif
    return p;
}
#endif

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_init(void);              /* call once at startup, before any allocation */
void gc_register_thread(void);   /* call once per new pthread                   */
void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd);

/* GC tuning — safe to call after gc_init() */
void   gc_set_max_heap(size_t bytes);
void   gc_set_free_space_divisor(int n);
void   gc_enable_incremental(void);
size_t gc_heap_size(void);
size_t gc_free_bytes(void);
size_t gc_total_bytes(void);

/* ── Convenience wrappers (keep same names so call sites don't change) ────── */

#ifdef __cplusplus
static inline void *gc_alloc(size_t n)               { return gc_alloc_impl(n, 1); }
static inline void *gc_alloc_atomic(size_t n)        { return gc_alloc_impl(n, 0); }
static inline void *gc_alloc_pinned(size_t n)        { return gc_alloc_pinned_impl(n, 1); }
static inline void *gc_alloc_pinned_atomic(size_t n) { return gc_alloc_pinned_impl(n, 0); }
static inline void *gc_alloc_raw_pinned(size_t n)        { return gc_ops->alloc_raw_pinned(n, true);  }
static inline void *gc_alloc_raw_pinned_atomic(size_t n) { return gc_ops->alloc_raw_pinned(n, false); }
#else
static inline void *gc_alloc(size_t n)               { return gc_nursery_alloc(n, true);  }
static inline void *gc_alloc_atomic(size_t n)        { return gc_nursery_alloc(n, false); }
static inline void *gc_alloc_pinned(size_t n)        { return gc_ops->alloc_pinned(n, true);  }
static inline void *gc_alloc_pinned_atomic(size_t n) { return gc_ops->alloc_pinned(n, false); }
static inline void *gc_alloc_raw_pinned(size_t n)        { return gc_ops->alloc_raw_pinned(n, true);  }
static inline void *gc_alloc_raw_pinned_atomic(size_t n) { return gc_ops->alloc_raw_pinned(n, false); }
#endif
static inline void  gc_collect(void)                 { gc_ops->collect(); }

/* ── Allocation macros ────────────────────────────────────────────────────── */

/* Standard (semispace-eligible): */
#define CURRY_NEW(T)              ((T *)gc_alloc(sizeof(T)))
#define CURRY_NEW_FLEX(T, n)      ((T *)gc_alloc(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))
#define CURRY_NEW_ATOM(T)         ((T *)gc_alloc_atomic(sizeof(T)))
#define CURRY_NEW_FLEX_ATOM(T, n) ((T *)gc_alloc_atomic(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))

/* Pinned typed objects (never moved; under Boehm same as above): */
#define CURRY_NEW_PINNED(T)              ((T *)gc_alloc_pinned(sizeof(T)))
#define CURRY_NEW_FLEX_PINNED(T, n)      ((T *)gc_alloc_pinned(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))
#define CURRY_NEW_PINNED_ATOM(T)         ((T *)gc_alloc_pinned_atomic(sizeof(T)))
#define CURRY_NEW_FLEX_PINNED_ATOM(T, n) ((T *)gc_alloc_pinned_atomic(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))

/* ── Root registration helpers ────────────────────────────────────────────── */

static inline void gc_pin(void *obj)   { gc_ops->pin(obj);   }
static inline void gc_unpin(void *obj) { gc_ops->unpin(obj); }
/* gc_register_root/unregister_root are plain functions defined in gc.c
 * (they maintain a global list read by gc_gen_minor_collect). */
void gc_register_root(void *slot);
void gc_unregister_root(void *slot);

/* val_t-taking pin/unpin for FFI use: C extensions call these to protect
 * Scheme values they hold in heap-allocated structs across GC points.
 * Under Boehm these are no-ops (conservative scan finds them anyway).
 * Under a moving GC they prevent the referenced object from being relocated. */
#include "value.h"
static inline void gc_val_pin(val_t v)   { if (vis_ptr(v)) gc_ops->pin((void *)(uintptr_t)v);   }
static inline void gc_val_unpin(val_t v) { if (vis_ptr(v)) gc_ops->unpin((void *)(uintptr_t)v); }

/*
 * Register a raw C pointer (not a val_t) that may point to a semispace object.
 * The GC updates *rawptr after collection if the target was moved.
 * No-op under Boehm.  rawptr must be stable (not itself in the semispace).
 */
void gc_register_rawptr(void **rawptr);
void gc_unregister_rawptr(void **rawptr);

/*
 * Register a VM value stack as a bulk root range.
 * base: fixed bottom of the val_t[] stack.
 * sp_ptr: pointer to the stack pointer (volatile — advances with each push).
 * The GC scans [base, *sp_ptr) on every collection.
 * No-op under Boehm.
 */
void gc_register_stack(void *base, void **sp_ptr);
void gc_unregister_stack(void *base);

/*
 * External root scanner registry.
 *
 * Modules that hold val_t references outside the standard root set (e.g.
 * global rule tables, the module registry) register a scanner callback here.
 * The callback is invoked during minor GC; it must call gc_evac_val / gc_fwd_ptr
 * on every such reference so the GC can update them after nursery evacuation.
 *
 * No-op under Boehm (conservative scan finds roots anyway).
 * Phase 4 will wire these into the minor GC scan loop.
 */
void gc_register_ext_scanner(void (*cb)(void));

/*
 * Called from inside an ext_scanner callback to evacuate a single val_t or
 * raw pointer from the nursery.  Returns the updated value/pointer.
 * Under Boehm / Phase 3 these are identity functions.
 *
 * Historical names (gc_ss_evac, gc_ss_fwd) kept for compatibility with
 * existing scanner callbacks in modules.c and sx_rules.c.
 */
uintptr_t gc_ss_evac(uintptr_t v);
void     *gc_ss_fwd(void *p);

/* Alias for gc_register_ext_scanner — matches name used in existing code. */
static inline void gc_ss_register_ext_scanner(void (*cb)(void)) {
    gc_register_ext_scanner(cb);
}

/* ── Shadow stack ─────────────────────────────────────────────────────────── */

/*
 * Always active — lightweight enough that the overhead under Boehm is
 * negligible (~4 pointer stores per eval() call).
 *
 * Usage — one per function that can trigger collection:
 *   val_t x = V_NIL, y = V_NIL;
 *   GC_AUTOFRAME(2, &x, &y);   // push frame; auto-pops on scope exit
 *   x = eval(...);             // might move nursery objects
 *   y = eval(...);             // x is still valid — GC updated it via frame
 */

/*
 * gc_minor_pending — deferred minor GC flag.
 *
 * Set by gc_nursery_refill() when the nursery overflowed but minor GC
 * could not fire immediately.  Two cases:
 *
 *   1. gc_shadow_stack != NULL (tree-walking evaluator): C-local val_t
 *      temporaries exist in eval() frames that are not tracked by the GC.
 *
 *   2. gc_inhibit_count > 0 (inside a primitive call): apply_arr() calls
 *      gc_inhibit_minor() before every primitive dispatch so that C call
 *      stacks in builtins are not interrupted mid-execution.  This is the
 *      common case during VM execution (shadow stack is NULL, but inhibit > 0).
 *
 * The VM's L_DISPATCH safepoint fires between every pair of bytecode
 * instructions, after the current op has committed all results to
 * vm->stack[0..sp) — the registered GC root range.  The tree-walker's
 * tail: label is a comparable safepoint in the eval() loop.
 *
 * Only set when gc_nursery.base is non-NULL (gen backend active).
 * Thread-local so each thread manages its own nursery independently.
 */
#ifndef __cplusplus
extern _Thread_local bool gc_minor_pending;
#endif

typedef struct GcFrame {
    val_t         **slots;
    int             count;
    struct GcFrame *prev;
} GcFrame;

#ifndef __cplusplus
extern _Thread_local GcFrame *gc_shadow_stack;
#endif

#ifndef __cplusplus
static inline void gc_pop_frame(GcFrame **fp) {
    gc_shadow_stack = (*fp)->prev;
}
#endif

/*
 * gc_inhibit_minor() / gc_resume_minor() — safe-point inhibit counter.
 *
 * Any C code that holds val_t locals across nursery allocation points but has
 * NOT been shadow-stacked (compiler.c, reader.c, builtins etc.) must call
 * gc_inhibit_minor() on entry and gc_resume_minor() on exit.  This increments
 * a counter checked by gc_nursery_refill(); while the counter is positive,
 * nursery overflow falls back to Boehm rather than triggering minor GC.
 *
 * eval() already handles this implicitly: GC_AUTOFRAME pushes a shadow stack
 * frame; gc_nursery_refill checks gc_shadow_stack != NULL as the primary gate,
 * and gc_inhibit_count > 0 as a secondary gate for non-eval code.
 *
 * Calls are nestable and balanced.
 */
#ifndef __cplusplus
extern _Thread_local int gc_inhibit_count;
#endif

static inline void gc_inhibit_minor(void) {
#ifndef __cplusplus
    gc_inhibit_count++;
#endif
}
static inline void gc_resume_minor(void) {
#ifndef __cplusplus
    if (gc_inhibit_count > 0) gc_inhibit_count--;
#endif
}

#define GC_AUTOFRAME(n, ...) \
    val_t *_gc_frame_roots[] = {__VA_ARGS__}; \
    GcFrame _gc_frame = {_gc_frame_roots, (n), gc_shadow_stack}; \
    gc_shadow_stack = &_gc_frame; \
    __attribute__((cleanup(gc_pop_frame))) GcFrame *_gc_frame_sentinel = &_gc_frame

/* ── GC statistics (available under both Boehm and generational backends) ── */

/*
 * Atomic counters updated on every minor/major collection.
 * Read from Scheme via (gc-stats); reset via (gc-stats-reset!).
 * All are 0 under Boehm (minor GC never fires; major count tracked via
 * GC_set_on_collection_event hook installed in gc_init).
 */
#include <stdatomic.h>
extern _Atomic uint64_t gc_stat_minor_count;    /* minor GC invocations         */
extern _Atomic uint64_t gc_stat_major_count;    /* Boehm major GC completions   */
extern _Atomic uint64_t gc_stat_minor_total_us; /* cumulative minor GC wall time */
extern _Atomic uint64_t gc_stat_minor_max_us;   /* max single minor GC pause    */

/* Number of valid entries in the pause ring (up to GC_PAUSE_RING_N).
 * gc_get_pause_ring() copies them out in chronological order. */
#define GC_PAUSE_RING_N 256
extern size_t gc_get_pause_ring(uint64_t *out); /* defined in gc_gen.c; 0 under Boehm */
extern void   gc_reset_pause_ring(void);        /* resets ring head to 0        */

/* ── Card table (retained for backwards compat; no longer written by barrier) */

#define GC_CARD_BYTES 512u
extern uint8_t  *gc_card_table;
extern uintptr_t gc_tenured_base;
extern size_t    gc_card_table_ncards;

/* ── Write-buffer remembered set ─────────────────────────────────────────── */

/*
 * When gc_dirty_slots is non-NULL (generational GC active), gc_wb_slot records
 * the address of each slot that receives a main-thread-nursery→tenured write.
 * Minor GC iterates these slot addresses to update forwarding pointers instead
 * of scanning the entire pinned list every collection.
 *
 * The buffer is a plain global because only the main thread can produce nursery
 * pointers (actor and worker threads run the tree-walker or hold gc_inhibit, so
 * their allocations go to Boehm).  Using the MAIN thread's nursery bounds
 * (gc_main_nursery_base/limit, set once in gc_gen_init) as the range check
 * ensures no other thread ever matches, making the global non-TLS safe.
 */
#define GC_DIRTY_CAP 4096
extern val_t  **gc_dirty_slots;      /* GC_MALLOC_UNCOLLECTABLE array of slot addresses */
extern size_t   gc_dirty_count;      /* number of valid entries                          */
extern bool     gc_dirty_overflow;   /* set when buffer is full (fall back to full scan) */
extern uint8_t *gc_main_nursery_base;  /* set once by gc_gen_init; NULL under Boehm    */
extern uint8_t *gc_main_nursery_limit; /* ditto                                          */

/* ── Write barrier ────────────────────────────────────────────────────────── */

/*
 * GC_WB(obj, field, val): write val into obj->field and record the slot if it
 * is a tenured→nursery write.  Under Boehm gc_dirty_slots is NULL so the
 * barrier reduces to the bare assignment.
 *
 * val must be a val_t.  Example: GC_WB(as_pair(p), car, new_car_val)
 * For array-element writes use gc_wb_slot(&arr[i], new_val) directly.
 */
static inline void gc_wb_slot(val_t *slot, val_t newval) {
    *slot = newval;
    /* Only record when the written VALUE is in the MAIN thread's nursery.
     * Using gc_main_nursery_base/limit (globals set once at init, never
     * modified) rather than TLS gc_nursery ensures that actor threads and
     * workpool workers — whose allocations go to Boehm and can never land in
     * the main-thread slab — never touch gc_dirty_count, eliminating the
     * need for any lock on the write path. */
    if (gc_dirty_slots && vis_ptr(newval)) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)newval;
        if (p >= gc_main_nursery_base && p < gc_main_nursery_limit) {
            if (gc_dirty_count < GC_DIRTY_CAP)
                gc_dirty_slots[gc_dirty_count++] = slot;
            else
                gc_dirty_overflow = true;
        }
    }
}

#define GC_WB(obj, field, newval) \
    do { val_t _gc_v = (newval); gc_wb_slot((val_t *)&(obj)->field, _gc_v); } while(0)

#endif /* CURRY_GC_H */
