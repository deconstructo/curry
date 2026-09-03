#ifndef CURRY_ENV_H
#define CURRY_ENV_H

/*
 * Lexical environments for Curry Scheme.
 *
 * An environment is a chain of frames.  Each frame is a flat array of
 * (symbol, value) pairs.  Lookup walks the chain from innermost to
 * outermost, checking each frame sequentially.
 *
 * Frame sizes are typically small (< 16 bindings) so linear scan is fast.
 * For very large top-level environments a hash-table frame is used instead.
 */

#include "value.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Frame operations ---- */
struct EnvFrame;
struct EnvFrame *frame_new(uint32_t capacity, struct EnvFrame *parent);
bool             frame_define(struct EnvFrame *f, val_t sym, val_t val);
bool             frame_set(struct EnvFrame *f, val_t sym, val_t val);  /* local only */
val_t           *frame_lookup(struct EnvFrame *f, val_t sym);          /* NULL if not found */

/* Like frame_lookup, but for the (possibly shared) GLOBAL_ENV frame also
 * hands back the frame->version the lookup was validated against, in the
 * same lock-free read as the slot fetch itself — see vm.c's OP_LOAD_GLOBAL/
 * OP_STORE_GLOBAL/OP_DEF_GLOBAL, which stamp their monomorphic inline cache
 * with *out_ver rather than re-reading frame->version afterward (a
 * time-of-check-to-time-of-use gap: a frame_grow landing in between would
 * silently stamp a slot from the OLD vals array with the NEW version,
 * telling every future cache hit to trust a stale pointer forever). NULL
 * out_ver is fine when the caller doesn't need it. */
val_t           *frame_lookup_versioned(struct EnvFrame *f, val_t sym, uint32_t *out_ver);

/* Copies out every (sym, val) pair currently in this ONE frame (not its
 * parent chain) into freshly allocated arrays, returning the count. For a
 * root frame (frame_is_global — reachable from more than one actor, see
 * env.c's big seqlock comment) this is validated against a concurrent
 * frame_define the same lock-free way frame_lookup_versioned validates a
 * single lookup, retrying if a structural write was in progress or landed
 * mid-copy; a plain memcpy for every other (single-thread-owned) frame.
 * Needed by anything that must walk a WHOLE frame's bindings rather than
 * look up one symbol at a time — e.g. modules.c's no-export-list import
 * path, which used to index f->syms[i]/f->vals[i] directly with no
 * synchronization at all. */
uint32_t         frame_snapshot_bindings(struct EnvFrame *f, val_t **out_syms, val_t **out_vals);

/* ---- Environment operations ---- */

/* Create a new root (global) environment */
val_t env_new_root(void);

/* Extend an environment with a new frame */
val_t env_extend(val_t parent_env);

/* Define a binding in the innermost frame */
void env_define(val_t env, val_t sym, val_t val);

/* Set an existing binding (R7RS set!) - walks chain */
bool env_set(val_t env, val_t sym, val_t val);

/* Look up a symbol - raises error if not found */
val_t env_lookup(val_t env, val_t sym);

/* Look up a symbol - returns V_FALSE if not found (no error) */
val_t env_lookup_or_false(val_t env, val_t sym);

/* Returns a pointer into the frame's vals array, NULL if unbound */
val_t *env_lookup_slot(val_t env, val_t sym);

/* Bind multiple parameters to arguments (for function call) */
val_t env_bind_args(val_t parent_env, val_t params, val_t args);

/* Bind parameters from a C array — avoids building an intermediate cons list */
val_t env_bind_arr(val_t parent_env, val_t params, int argc, val_t *argv);

/* The global (top-level) environment */
extern val_t GLOBAL_ENV;

/* Implemented in runtime.c; a real body only when BUILD_LLVM is on
 * (a cheap no-op otherwise), kept declared unconditionally so env.c's
 * call sites need no #ifdef of their own. Called by env_define/env_set
 * whenever GLOBAL_ENV specifically (not some other environment) is the
 * one actually being mutated. Detects a global redefinition of one of
 * the arithmetic operator names (+, -, *, /, <, ...) the LLVM JIT
 * assumes are stable when open-coding them directly rather than doing a
 * real global-variable lookup + call (see src/llvm/codegen.cpp's ARITH2/
 * ARITH1 tables) -- see issue #118. Once any such redefinition is
 * detected, permanently taints ALL future arithmetic fast-pathing
 * (checked by codegen.cpp before ever taking the fast path, and by
 * apply_arr's/vm.c's already-JIT-compiled-closure dispatch, which
 * additionally deoptimizes any closure that was compiled and fast-
 * pathed before the taint occurred: the taint check simply stops that
 * dispatch from ever invoking the closure's now-permanently-wrong
 * native code again -- jit_val itself is deliberately left as-is, not
 * reset, since the taint is permanent and there's nothing to gain from
 * ever recompiling that closure -- so it falls back to the bytecode
 * interpreter every time instead, which always reads the
 * CURRENT GLOBAL_ENV binding correctly). Global and one-way rather than
 * per-symbol/per-closure: this kind of redefinition is expected to be
 * extremely rare, so a coarse, simple, definitely-correct invalidation
 * is preferred over a more precise but far more complex one. */
void jit_maybe_taint_global_arith(val_t sym, val_t new_val);

void env_init(void);

#endif /* CURRY_ENV_H */
