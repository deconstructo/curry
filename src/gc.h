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

/* ── Per-thread nursery ───────────────────────────────────────────────────── */

typedef struct {
    uint8_t *base;   /* start of nursery buffer                  */
    uint8_t *limit;  /* one past the end                         */
    uint8_t *top;    /* bump pointer; next allocation starts here */
} GcNursery;

#ifndef __cplusplus
extern _Thread_local GcNursery gc_nursery;
#endif

/* Slow path: called when top + n > limit.  Returns a pointer to n bytes.
 * May trigger a minor collection, refill the nursery, or fall back to
 * gc_ops->alloc for large objects. */
void *gc_nursery_refill(size_t n, bool has_ptrs);

/* C-linkage allocator entry points used by C++ callers (avoids C++ TLS
 * wrapper generation for gc_nursery which is incompatible with the C TLS ABI
 * on macOS/arm64). */
#ifdef __cplusplus
extern "C" {
    void *gc_alloc_impl(size_t n, int has_ptrs);
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
static inline void *gc_alloc(size_t n)        { return gc_alloc_impl(n, 1); }
static inline void *gc_alloc_atomic(size_t n) { return gc_alloc_impl(n, 0); }
#else
static inline void *gc_alloc(size_t n)        { return gc_nursery_alloc(n, true);  }
static inline void *gc_alloc_atomic(size_t n) { return gc_nursery_alloc(n, false); }
#endif
static inline void  gc_collect(void)          { gc_ops->collect();                  }

/* ── Allocation macros (unchanged API) ────────────────────────────────────── */

#define CURRY_NEW(T)                  ((T *)gc_alloc(sizeof(T)))
#define CURRY_NEW_FLEX(T, n)          ((T *)gc_alloc(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))
#define CURRY_NEW_ATOM(T)             ((T *)gc_alloc_atomic(sizeof(T)))
#define CURRY_NEW_FLEX_ATOM(T, n)     ((T *)gc_alloc_atomic(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))

/* ── Root registration helpers ────────────────────────────────────────────── */

static inline void gc_pin(void *obj)               { gc_ops->pin(obj);   }
static inline void gc_unpin(void *obj)             { gc_ops->unpin(obj); }
static inline void gc_register_root(void *slot)    { gc_ops->register_root(slot);   }
static inline void gc_unregister_root(void *slot)  { gc_ops->unregister_root(slot); }

#endif /* CURRY_GC_H */
