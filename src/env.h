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

void env_init(void);

#endif /* CURRY_ENV_H */
