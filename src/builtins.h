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
 * compiler.c's car/cdr/cons/pair?/null? emission in ir_emit's IR_CALL
 * case) and the VM's OP_CAR/OP_CDR/OP_CONS/OP_PAIRP/OP_NULLP handlers
 * (vm.c) can both compare a call site's currently-bound global against
 * the ACTUAL primitive by function-pointer identity, rather than
 * trusting a symbol name alone -- these are ordinary, user-redefinable
 * global bindings, and a redefined one must still be called as a real
 * procedure, not open-coded as a raw operation. See OP_CAR/OP_CDR's own
 * comment (opcode.h) for the full design. */
val_t prim_car(int argc, val_t *argv, void *ud);
val_t prim_cdr(int argc, val_t *argv, void *ud);
val_t prim_cons(int argc, val_t *argv, void *ud);
val_t prim_pair_p(int argc, val_t *argv, void *ud);
val_t prim_null_p(int argc, val_t *argv, void *ud);
/* Same reasoning, extended to +, -, *, =, <, <=, >, >= (Tier 2.5 step 2). Each
 * open-coded opcode calls the REAL primitive directly on a match
 * (exactly like the five above), rather than reusing OP_ADD/OP_SUB/
 * OP_MUL/OP_LT/etc.'s own separate, pre-existing inline fixnum-fast-path
 * bodies -- found while landing this: those bodies are NOT a safe
 * drop-in replacement for calling the primitive. prim_num_lt/le/gt/ge's
 * own 2-arg case dispatches to sx_lt/sx_le/sx_gt/sx_ge for a symbolic
 * (CAS) operand (docs/reference/symbolic.md), which the raw opcodes'
 * existing fixnum-or-else-num_lt logic does not replicate at all --
 * reusing that logic for an open-coded `(< x 5)` where `x` is a sym-var
 * would have silently done the wrong thing (or raised) instead of
 * returning the correct symbolic comparison node. Calling the actual
 * primitive sidesteps this class of risk entirely, at the cost of
 * leaving the true "inline the fixnum fast math too" optimization
 * (Chez sec 3.5's dominating-test-proven unchecked variants) for a later,
 * more careful pass -- consistent with how car/cdr/cons/pair?/null? were
 * landed. */
val_t prim_add(int argc, val_t *argv, void *ud);
val_t prim_sub(int argc, val_t *argv, void *ud);
val_t prim_mul(int argc, val_t *argv, void *ud);
val_t prim_num_eq(int argc, val_t *argv, void *ud);
val_t prim_num_lt(int argc, val_t *argv, void *ud);
val_t prim_num_le(int argc, val_t *argv, void *ud);
val_t prim_num_gt(int argc, val_t *argv, void *ud);
val_t prim_num_ge(int argc, val_t *argv, void *ud);

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
val_t macroexpand_1(val_t expr);
val_t type_of_val(val_t v);
val_t scm_string_copy(val_t s);
val_t scm_string_append(val_t a, val_t b);
val_t scm_number_to_string(val_t n, int radix);
val_t scm_string_to_symbol(val_t s);
val_t scm_symbol_to_string(val_t sym);

#endif /* CURRY_BUILTINS_H */
