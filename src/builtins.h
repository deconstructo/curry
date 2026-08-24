#ifndef CURRY_BUILTINS_H
#define CURRY_BUILTINS_H

/*
 * R7RS built-in procedures for Curry Scheme.
 *
 * All procedures are registered into an environment by builtins_register().
 * Internal primitives for record types, actors, sets, etc. are also
 * registered here under their % names.
 */

#include "value.h"
#include "object.h"

/* Register all built-in procedures into env */
void builtins_register(val_t env);

/* defprim: register a primitive function (used by builtins_curry.c) */
void defprim(val_t env, const char *name, PrimFn fn, int min, int max);

/* Exposed (not static) so the compiler's open-coding (Tier 2.5,
 * compiler.c's car/cdr emission in ir_emit's IR_CALL case) and the VM's
 * OP_CAR/OP_CDR handlers (vm.c) can both compare a call site's currently-
 * bound `car`/`cdr` global against the ACTUAL primitive by function-
 * pointer identity, rather than trusting a symbol name alone -- `car`/
 * `cdr` are ordinary, user-redefinable global bindings, and a redefined
 * one must still be called as a real procedure, not open-coded as a raw
 * pair access. See OP_CAR/OP_CDR's own comment (opcode.h) for the full
 * design. */
val_t prim_car(int argc, val_t *argv, void *ud);
val_t prim_cdr(int argc, val_t *argv, void *ud);

/* Register Curry-specific (CAS, quantum, surreal) procedures into env */
void builtins_curry_register(val_t env);

/* Helpers for list operations (used across the codebase) */
val_t scm_cons(val_t car, val_t cdr);
val_t scm_list(int n, ...);         /* (list a b c ...) */
int   scm_list_length(val_t lst);   /* -1 if improper */
val_t scm_list_ref(val_t lst, int n);
val_t scm_list_tail(val_t lst, int n);
val_t scm_append(val_t a, val_t b);
val_t scm_reverse(val_t lst);

/* String helpers */
val_t scm_make_string(uint32_t len, int fill_char);
val_t scm_string_copy(val_t s);
val_t scm_string_append(val_t a, val_t b);
val_t scm_number_to_string(val_t n, int radix);
val_t scm_string_to_symbol(val_t s);
val_t scm_symbol_to_string(val_t sym);

#endif /* CURRY_BUILTINS_H */
