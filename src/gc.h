#ifndef CURRY_GC_H
#define CURRY_GC_H

/*
 * Garbage collector interface for Curry Scheme.
 *
 * The collector is Boehm GC (conservative, non-moving, thread-safe). Objects
 * never move, so a pointer to a heap object stays valid for as long as the
 * object is reachable, and C code may hold Scheme values in local variables,
 * in heap structs allocated with gc_alloc*, or in globals without any extra
 * registration.
 *
 * The experimental generational backend (--gc generational, src/gc_gen.c)
 * was removed: it moved objects that other threads could still see (issue
 * #226), it depended on every C store going through the write barrier
 * (issues #285, #286), and it measured no faster than Boehm. The plan for a
 * non-moving concurrent collector that keeps the hooks below is in
 * docs/reference/gc.md.
 *
 * Hooks kept for that future collector (all cheap no-ops under Boehm):
 *   - gc_wb_slot / gc_wb_slot_atomic_relaxed / GC_WB: the write barrier.
 *     Keep routing pointer stores into existing objects through these.
 *   - gc_thread_park / gc_thread_unpark: brackets around blocking
 *     calls, for per-thread handshakes. (The per-instruction safe-point poll
 *     was removed; vm.c's L_DISPATCH comment says where it would go.)
 *
 * Also removed with it: the shadow stack (GC_AUTOFRAME), the minor-GC
 * inhibit counter (gc_inhibit_minor/gc_resume_minor) and the ext-scanner
 * registry. They told a moving collector where pointers were; a non-moving
 * collector does not need them.
 *
 * Thread safety
 * ─────────────
 * Call gc_register_thread() at the start of every new pthread that touches
 * Scheme values, and gc_unregister_thread() from its exit path.
 */

#define GC_THREADS
#include <gc/gc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#ifndef __cplusplus
#include <stdatomic.h> /* C++ TUs (qt6.cpp) that include this header never
                         * call gc_wb_slot_atomic_relaxed below -- stdatomic.h
                         * is C11-only and its atomic_* macros collide with
                         * <atomic>/libc++ when pulled into a C++ TU, so it's
                         * guarded out entirely rather than risk that clash. */
#endif
#include "value.h" /* val_t, CURRY_THREAD_LOCAL */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_init(void);              /* call once at startup, before any allocation */
void gc_register_thread(void);   /* call once per new pthread                   */
void gc_unregister_thread(void); /* call once from that same pthread's exit path */
void gc_finalizer(void *obj, void (*fn)(void *, void *), void *cd);

/* GC tuning — safe to call after gc_init() */
void   gc_set_max_heap(size_t bytes);
void   gc_set_free_space_divisor(int n);
void   gc_enable_incremental(void);
size_t gc_heap_size(void);
size_t gc_free_bytes(void);
size_t gc_total_bytes(void);

/* ── Allocation ───────────────────────────────────────────────────────────── */

/*
 * gc_alloc: an object that may contain GC pointers (scanned).
 * gc_alloc_atomic: pointer-free data (strings, flonums, byte buffers); never
 *   scanned, so it cannot keep anything alive by accident.
 * The _pinned / _raw_pinned variants are the same allocations under a
 * non-moving collector; they are kept so call sites keep stating which
 * objects must never move (mutexes, GMP data, FILE*, jmp_buf, raw helper
 * arrays), for any future collector that cares.
 * All of them return zeroed memory.
 */
static inline void *gc_alloc(size_t n)                   { return GC_MALLOC(n); }
static inline void *gc_alloc_atomic(size_t n)            { return GC_MALLOC_ATOMIC(n); }
static inline void *gc_alloc_pinned(size_t n)            { return GC_MALLOC(n); }
static inline void *gc_alloc_pinned_atomic(size_t n)     { return GC_MALLOC_ATOMIC(n); }
static inline void *gc_alloc_raw_pinned(size_t n)        { return GC_MALLOC(n); }
static inline void *gc_alloc_raw_pinned_atomic(size_t n) { return GC_MALLOC_ATOMIC(n); }
static inline void *gc_alloc_obj(size_t n)               { return GC_MALLOC(n); }
static inline void  gc_collect(void)                     { GC_gcollect(); }

#define CURRY_NEW(T)              ((T *)gc_alloc_obj(sizeof(T)))
#define CURRY_NEW_FLEX(T, n)      ((T *)gc_alloc_obj(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))
#define CURRY_NEW_ATOM(T)         ((T *)gc_alloc_atomic(sizeof(T)))
#define CURRY_NEW_FLEX_ATOM(T, n) ((T *)gc_alloc_atomic(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))

#define CURRY_NEW_PINNED(T)              ((T *)gc_alloc_pinned(sizeof(T)))
#define CURRY_NEW_FLEX_PINNED(T, n)      ((T *)gc_alloc_pinned(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))
#define CURRY_NEW_PINNED_ATOM(T)         ((T *)gc_alloc_pinned_atomic(sizeof(T)))
#define CURRY_NEW_FLEX_PINNED_ATOM(T, n) ((T *)gc_alloc_pinned_atomic(sizeof(T) + (n)*sizeof(((T*)0)->data[0])))

/* ── Roots ────────────────────────────────────────────────────────────────── */

/* Pin/unpin: no-ops for a non-moving collector. Kept for C extensions that
 * already call them. */
static inline void gc_pin(void *obj)     { (void)obj; }
static inline void gc_unpin(void *obj)   { (void)obj; }
static inline void gc_val_pin(val_t v)   { (void)v; }
static inline void gc_val_unpin(val_t v) { (void)v; }

/*
 * Register a val_t slot as a GC root. Needed only when the slot lives in
 * memory Boehm does not scan: malloc'd memory, or thread-local storage (TLS
 * is not scanned by Boehm). gc.c keeps a copy of the value in an
 * uncollectable array, so the value registered last stays alive until
 * gc_unregister_root. gc_register_root_val stores v and registers in one
 * step.
 */
void gc_register_root(void *slot);
void gc_register_root_val(void *slot, val_t v);
void gc_unregister_root(void *slot);

/* ── Safe points (hooks for a future concurrent collector) ────────────────── */

/*
 * gc_thread_park()/gc_thread_unpark() bracket a long or unbounded
 * blocking call (mailbox receive, accept/recv, a work-queue wait). Both are
 * no-ops today; a collector that scans each thread's roots at its own safe
 * point would scan a parked thread on its behalf.
 */
#ifdef __cplusplus
extern "C" {
#endif
void gc_thread_park(void);
void gc_thread_unpark(void);
#ifdef __cplusplus
}
#endif

/* ── GC statistics ────────────────────────────────────────────────────────── */

/*
 * Boehm collection and stop-the-world timing (gc.c's event callback).
 * Times are in nanoseconds. A stop-the-world interval is the pause every
 * registered thread sees; Boehm's incremental mode splits a collection into
 * several of them and does not report collection start/end, so the
 * major-* counters stay 0 in that mode. gc_get_stw_ring copies up to
 * GC_STW_RING_N of the most recent stop-the-world durations out, oldest
 * first.
 */
#define GC_STW_RING_N 1024
#ifndef __cplusplus
extern _Atomic uint64_t gc_stat_major_count;
extern _Atomic uint64_t gc_stat_major_total_ns;
extern _Atomic uint64_t gc_stat_major_max_ns;
extern _Atomic uint64_t gc_stat_stw_count;
extern _Atomic uint64_t gc_stat_stw_total_ns;
extern _Atomic uint64_t gc_stat_stw_max_ns;
#endif
size_t gc_get_stw_ring(uint64_t *out);
void   gc_reset_boehm_stats(void);

/* ── Write barrier ────────────────────────────────────────────────────────── */

/*
 * GC_WB(obj, field, val): store val into obj->field. gc_wb_slot(&slot, val)
 * for array elements. Today these are plain stores. Keep using them for every
 * store of a pointer into an object that already exists: a concurrent
 * collector needs a barrier on exactly these stores.
 */
static inline void gc_wb_slot(val_t *slot, val_t newval) {
    *slot = newval;
}

#define GC_WB(obj, field, newval) \
    do { val_t _gc_v = (newval); gc_wb_slot((val_t *)&(obj)->field, _gc_v); } while(0)

/*
 * gc_wb_slot_atomic_relaxed: the same barrier for a slot that another thread
 * reads without a lock -- GLOBAL_ENV's vals[] (env.c, vm.c OP_STORE_GLOBAL)
 * and TVar.value (stm.c). The store is atomic-relaxed so it is not a data
 * race; the ordering readers need comes from each caller's own version or
 * seqlock counter (issues #153, #210).
 *
 * C-only (see the stdatomic.h include guard above).
 */
#ifndef __cplusplus
static inline void gc_wb_slot_atomic_relaxed(val_t *slot, val_t newval) {
    atomic_store_explicit((_Atomic val_t *)slot, newval, memory_order_relaxed);
}
#endif

#endif /* CURRY_GC_H */
