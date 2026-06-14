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
} gc_ops_t;

/* Active GC backend.  Set during gc_init(); never NULL after that. */
extern gc_ops_t *gc_ops;

/* ── Write barrier (generational GC) ─────────────────────────────────────── */

/*
 * Fast-path write barrier globals.  Set by gc_gen_init(); zero/NULL under
 * Boehm and semispace.  Declared here so GC_WRITE_BARRIER can be inlined at
 * every mutation site without including gc_generational.h.
 */
extern volatile int  gc_gen_active;       /* 1 when generational GC is live  */
extern uint8_t      *gc_gen_tenured_base; /* start of tenured region         */
extern uint8_t      *gc_gen_tenured_limit;/* one past end of tenured region  */
extern uint8_t      *gc_gen_card_table;   /* one dirty byte per 512-byte card */

#define GC_CARD_SHIFT 9u /* 1 card = 2^9 = 512 bytes */

/*
 * GC_WRITE_BARRIER(obj, field_ptr, new_val)
 *
 * Must wrap every store of a val_t pointer into a heap-allocated object.
 * Under Boehm and semispace (gc_gen_active == 0) this compiles to a single
 * store — the branch is predicted-not-taken and eliminated by the optimiser.
 *
 * obj:       raw C pointer to the object being mutated (not a val_t)
 * field_ptr: pointer to the val_t field (val_t *)
 * new_val:   the value being stored
 *
 * When the generational backend is active and obj is in tenured space, the
 * 512-byte card containing obj is marked dirty so the next minor collection
 * scans it for old→young pointers (the remembered set).
 */
#define GC_WRITE_BARRIER(obj, field_ptr, new_val)                              \
    do {                                                                        \
        if (__builtin_expect(gc_gen_active, 0)) {                              \
            uint8_t *_gcwb_o = (uint8_t *)(void *)(obj);                      \
            if (_gcwb_o >= gc_gen_tenured_base &&                              \
                    _gcwb_o < gc_gen_tenured_limit)                            \
                gc_gen_card_table[(_gcwb_o - gc_gen_tenured_base)              \
                                  >> GC_CARD_SHIFT] = 1;                       \
        }                                                                       \
        *(field_ptr) = (new_val);                                               \
    } while (0)

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
static inline void *gc_nursery_alloc(size_t n, bool has_ptrs) {
    /* Align to 8 bytes (all Curry heap objects require 8-byte alignment). */
    n = (n + 7u) & ~7u;
    uint8_t *p = gc_nursery.top;
    uint8_t *next = p + n;
    if (__builtin_expect(next > gc_nursery.limit, 0))
        return gc_nursery_refill(n, has_ptrs);
    gc_nursery.top = next;
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

static inline void gc_pin(void *obj)               { gc_ops->pin(obj);   }
static inline void gc_unpin(void *obj)             { gc_ops->unpin(obj); }
static inline void gc_register_root(void *slot)    { gc_ops->register_root(slot);   }
static inline void gc_unregister_root(void *slot)  { gc_ops->unregister_root(slot); }

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

#endif /* CURRY_GC_H */
