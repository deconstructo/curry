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
void gc_gen_safepoint(void);

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
