#ifndef CURRY_GC_SEMISPACE_H
#define CURRY_GC_SEMISPACE_H

/*
 * Semispace (Cheney) GC backend for Curry Scheme.
 *
 * Architecture
 * ─────────────
 * Two equal-size semispaces (from-space / to-space).  All typed Scheme objects
 * (those with an Hdr and a valid ObjType tag) are allocated in from-space via a
 * bump pointer.  On exhaustion a stop-the-world Cheney copy evacuates all live
 * objects to to-space, then swaps the spaces.
 *
 * Objects that must never move (embedded pthreads primitives) are allocated via
 * alloc_pinned() which calls GC_MALLOC and registers typed objects in the pinned
 * list so their val_t fields can be updated after each collection.
 *
 * Root set
 * ────────
 *  1. val_t stack ranges registered via gc_register_stack()
 *  2. Individual val_t slots registered via gc_register_root()
 *  3. val_t fields of pinned objects (scanned from the pinned list)
 *  4. Raw C pointers to semispace objects registered via gc_register_rawptr()
 *
 * Activation
 * ──────────
 * Call gc_semispace_init() before gc_init() and set gc_ops = &gc_ss_ops.
 * The --gc semispace flag in main.c does this.
 */

#include "gc.h"
#include <stddef.h>
#include <stdint.h>

/* Total bytes in EACH semispace (default 32 MB). */
#ifndef GC_SS_SPACE_BYTES
#define GC_SS_SPACE_BYTES (32u * 1024u * 1024u)
#endif

/* Initialise the semispace backend.  Must be called before gc_init(). */
void gc_semispace_init(size_t space_bytes);

/* The semispace gc_ops_t; set gc_ops = &gc_ss_ops after gc_semispace_init(). */
extern gc_ops_t gc_ss_ops;

/* Register a symbol-table fixup callback; called once per collection so the
 * symbol interning table can update any forwarded Symbol pointers. */
void gc_ss_register_sym_fixup(void (*cb)(void));

/*
 * Register an "external root scanner" callback.  Called after evacuating
 * primary roots and before the Cheney scan loop ends.  Callbacks must use
 * gc_ss_evac / gc_ss_fwd to update their val_t / raw pointer fields.
 * No-op under Boehm.
 */
void gc_ss_register_ext_scanner(void (*cb)(void));

/*
 * Evacuate a val_t (as uintptr_t) — for use inside ext scanner callbacks.
 * Returns the new val_t (updated to point to to-space).
 */
uintptr_t gc_ss_evac(uintptr_t v);

/*
 * Forward a raw C pointer to a semispace object.  Returns new address if moved.
 */
void *gc_ss_fwd(void *p);

/* GC stats snapshot. */
typedef struct {
    uint64_t collections;
    size_t   bytes_allocated;
    size_t   bytes_survived;
    size_t   from_used;
    size_t   space_size;
    size_t   pinned_count;
} GcSsStats;

GcSsStats gc_ss_stats(void);

/* on-collection hook (installed by gc-on-collection builtin) */
void gc_ss_set_hook(void (*hook)(void));

#endif /* CURRY_GC_SEMISPACE_H */
