#ifndef CURRY_GC_GENERATIONAL_H
#define CURRY_GC_GENERATIONAL_H

/*
 * gc_generational.h — Two-generation GC for Curry Scheme.
 *
 * Architecture
 * ─────────────
 * Gen0 (nursery): per-thread bump-pointer, GC_NURSERY_DEFAULT_BYTES each.
 *   The GcNursery struct in gc.h is the allocation fast path.  On exhaustion,
 *   gc_nursery_refill() triggers a minor collection and resets the nursery.
 *
 * Gen1 (tenured): single mmap'd region, GC_TENURED_DEFAULT_BYTES.  Objects
 *   are promoted here after surviving GC_AGE_PROMOTE minor collections.  A
 *   major collection uses Cheney-copy over the tenured region (same algorithm
 *   as the semispace backend) when tenured space exceeds GC_TENURED_FILL_PCT.
 *
 * Write barrier — card table:
 *   Tenured space is divided into GC_CARD_BYTES-sized (512 B) cards.  One
 *   dirty byte per card.  GC_WRITE_BARRIER() in gc.h marks the card of any
 *   tenured object that receives a new pointer value.  Minor collection scans
 *   dirty cards to build the remembered set (old→young references).
 *
 * Safepoints — polling:
 *   gc_stop_world is a global flag checked at nursery-refill time and at
 *   actor receive/send!.  Threads arriving at a safepoint park on
 *   gc_gen_safepoint_mutex until the collector releases them.
 */

#include "gc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Configuration constants ──────────────────────────────────────────────── */

#define GC_CARD_BYTES            (1u << GC_CARD_SHIFT)      /* 512 bytes/card  */
#define GC_NURSERY_DEFAULT_BYTES (2u   * 1024u * 1024u)     /* 2 MB per thread */
#define GC_TENURED_DEFAULT_BYTES (128u * 1024u * 1024u)     /* 128 MB          */
#define GC_TENURED_FILL_PCT      85                          /* major trigger % */
#define GC_AGE_PROMOTE           2u                          /* minor GCs until promotion */

/* ── Safepoint ────────────────────────────────────────────────────────────── */

/*
 * Threads must call gc_gen_safepoint() at every allocation failure (nursery
 * refill) and at actor receive/send! yield points.  If gc_stop_world is set
 * the thread parks until the collector releases it.
 *
 * The collector sets gc_stop_world, waits for all threads to park, runs the
 * collection, then clears gc_stop_world and broadcasts the condvar.
 */
extern volatile int gc_stop_world;

/* Check for a pending STW pause; park if gc_stop_world is set. */
void gc_gen_safepoint(void);

/* Acquire the STW pause (set flag, wait for all threads to park).
 * The caller holds the internal STW mutex on return. */
void gc_gen_stop_the_world(void);

/* Release the STW pause; wake all parked threads. */
void gc_gen_start_the_world(void);

/* Call at thread exit to decrement the registered-thread count.
 * Installed as a pthread_cleanup_push handler by actor_thread(). */
void gc_gen_unregister_thread(void);

/* Call before blocking on a non-GC condvar (e.g. a work-queue park).
 * Decrements the thread count so STW does not wait for this thread.
 * Call gc_gen_thread_unpark() after the condvar returns. */
void gc_gen_thread_park(void);

/* Call after waking from a non-GC condvar.
 * Services any pending STW pause, then increments the thread count. */
void gc_gen_thread_unpark(void);

/* ── VM registry ──────────────────────────────────────────────────────────── */

/*
 * Register/unregister a VM instance with the GC.  The minor collector
 * traces all registered VMs for root scanning.
 * vm_init() calls gc_gen_register_vm(); vm_free() calls gc_gen_unregister_vm().
 * These are no-ops when gc_gen_active == 0.
 */
struct VM;  /* forward declaration */
void gc_gen_register_vm(struct VM *v);
void gc_gen_unregister_vm(struct VM *v);

/* ── Tenured allocator ────────────────────────────────────────────────────── */

/*
 * Thread-safe bump-pointer allocation in Gen1 (tenured space).
 * Called by the minor collector when promoting a live nursery object.
 * Returns NULL if tenured space is full (caller must trigger major GC).
 * Callers must set GC_AGE_GEN1 in the object's Hdr.flags after copying.
 */
void  *gen_tenured_alloc(size_t n);
size_t gen_tenured_used(void);

/* Increment collection counters and invoke the on-collection hook. */
void gc_gen_bump_minor(void);
void gc_gen_bump_major(void);

/* ── GC inhibit ───────────────────────────────────────────────────────────── */

/*
 * Inhibit / uninhibit minor collection.  While the inhibit count is > 0,
 * nursery exhaustion in gen_alloc falls back to direct tenured allocation
 * instead of triggering a minor collection.
 *
 * Used by C extension callbacks (Qt6 event handlers, etc.) to prevent minor GC
 * from firing while val_t values are live only in C locals on the native call
 * stack, which the generational collector does not scan.
 *
 * Calls must be balanced.  The count is a depth counter so inhibit/uninhibit
 * may be nested.
 */
void gc_gen_inhibit(void);
void gc_gen_uninhibit(void);

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/*
 * Initialise the generational backend.  Call before gc_init().
 * nursery_bytes: per-thread nursery size (0 → GC_NURSERY_DEFAULT_BYTES)
 * tenured_bytes: tenured region capacity (0 → GC_TENURED_DEFAULT_BYTES)
 * Sets gc_ops = &gc_gen_ops and gc_gen_active = 1.
 */
void gc_gen_init(size_t nursery_bytes, size_t tenured_bytes);

/* The generational gc_ops_t; set gc_ops = &gc_gen_ops after gc_gen_init(). */
extern gc_ops_t gc_gen_ops;

/* ── Stats ────────────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t minor_collections;
    uint64_t major_collections;
    size_t   nursery_bytes;     /* configured per-thread nursery size */
    size_t   nursery_used;      /* bytes used in this thread's nursery */
    size_t   tenured_used;      /* bytes live in tenured space */
    size_t   tenured_capacity;  /* total tenured capacity */
    size_t   pinned_count;      /* pinned (non-moving) objects */
} GcGenStats;

GcGenStats gc_gen_stats(void);

/* on-collection hook (for (gc-on-collection) builtin) */
void gc_gen_set_hook(void (*hook)(void));

/*
 * Register an "external root scanner" callback — same interface as the
 * semispace backend.  Called during minor and major collection after primary
 * roots are evacuated.  Callbacks must use gc_gen_evac() to update val_t
 * fields.
 */
void gc_gen_register_ext_scanner(void (*cb)(void));

/*
 * Evacuate a val_t (as uintptr_t) during collection — for use inside ext
 * scanner callbacks.  Returns the updated val_t pointing to the survivor copy.
 */
uintptr_t gc_gen_evac(uintptr_t v);

#endif /* CURRY_GC_GENERATIONAL_H */
