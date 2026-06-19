/*
 * gc_gen.h — Generational GC backend for Curry Scheme.
 *
 * Architecture (see docs/gc-moving-design.md for the full spec):
 *
 *   Gen 0 (nursery): per-thread bump-pointer slab (default 512 KB).
 *     GC:MOVE typed objects only.  Fast path: inline bump in gc.h.
 *     Slow path: gc_nursery_refill() — Phase 3: Boehm fallback.
 *                                     Phase 4: minor GC + nursery reset.
 *
 *   Gen 1 (tenured): Boehm GC.  Promotion = GC_MALLOC copy.
 *     Boehm handles major GC conservatively; no separate mark-compact.
 *
 *   Card table: 512-byte cards covering the Boehm heap range.
 *     Dirty when a tenured object stores a nursery (Gen 0) reference.
 *     Scanned during minor GC to find old→young pointers.
 *     gc_card_table and gc_tenured_base are declared in gc.h.
 */

#ifndef CURRY_GC_GEN_H
#define CURRY_GC_GEN_H

#include "gc.h"

/* The generational GC vtable — set gc_ops = &gc_gen_ops to activate. */
extern gc_ops_t gc_gen_ops;

/*
 * Initialise the generational GC.  Must be called before gc_init() (or
 * instead of it) — it calls GC_INIT() internally.
 *
 * nursery_bytes: per-thread nursery slab size; 0 = use default (512 KB).
 * Allocates the main thread's nursery slab and the card table.
 */
void gc_gen_init(size_t nursery_bytes);

/* Resize the per-thread nursery.  Must be called when the nursery is empty
 * (i.e. at a safe point between expressions, not during evaluation). */
void gc_gen_set_nursery_size(size_t bytes);

#endif /* CURRY_GC_GEN_H */
