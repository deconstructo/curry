/*
 * gc_gen.h — Generational GC backend for Curry Scheme.
 *
 * Architecture (see docs/gc-moving-design.md for the full spec):
 *
 *   Gen 0 (nursery): per-thread bump-pointer slab (default 512 KB).
 *     GC:MOVE typed objects only.  Fast path: inline bump in gc.h.
 *     Slow path: gc_nursery_refill() — Phase 4: minor GC + nursery reset.
 *
 *   Gen 1 (tenured): Boehm GC.  Promotion = GC_MALLOC copy.
 *     Boehm handles major GC conservatively; no separate mark-compact.
 *
 *   Card table: 512-byte cards covering the Boehm heap range.
 *     Dirty when a tenured object stores a nursery (Gen 0) reference.
 *     Scanned during minor GC to find old→young pointers.
 */

#ifndef CURRY_GC_GEN_H
#define CURRY_GC_GEN_H

#include "gc.h"
#include "value.h"

/* The generational GC vtable — set gc_ops = &gc_gen_ops to activate. */
extern gc_ops_t gc_gen_ops;

/*
 * Initialise the generational GC.  Must be called before gc_init() —
 * it calls GC_INIT() internally.
 * nursery_bytes: per-thread slab size; 0 = default (512 KB).
 */
void gc_gen_init(size_t nursery_bytes);

/* Resize the per-thread nursery (safe-point only). */
void gc_gen_set_nursery_size(size_t bytes);

/* Trigger a minor GC of the current thread's nursery.  Called from
 * gc_nursery_refill() when the nursery slab is full. */
void gc_gen_minor_collect(void);

/* ── Root/stack/ext-scanner registries (defined in gc.c, read by gc_gen.c) ── */
typedef struct { val_t *base; val_t **sp_ptr; } GcStackRange;

extern val_t       **g_roots;
extern size_t        g_roots_count;
extern GcStackRange *g_stacks;
extern size_t        g_stacks_count;
extern void        (**g_ext_scanners)(void);
extern size_t        g_ext_count;

/* Evacuation function pointers set during collection (defined in gc.c). */
extern uintptr_t (*gc_evac_fn)(uintptr_t);
extern void     *(*gc_fwd_fn)(void *);

#endif /* CURRY_GC_GEN_H */
