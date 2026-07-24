#ifndef CURRY_RUNTIME_INTERNAL_H
#define CURRY_RUNTIME_INTERNAL_H

/*
 * Private helpers shared between runtime.c (the surviving exception/apply/
 * dynamic-wind machinery) and eval.c (the shrinking tree-walker dispatch).
 * Not part of the public API — do not include outside these two files.
 */

#include "value.h"

/* Cons a pair without going through the Scheme-level allocator wrappers. */
val_t make_pair(val_t car, val_t cdr);

/* Length of a proper list. */
int list_length(val_t lst);

/* Collect a list into a C array; returns count. */
int list_to_arr(val_t lst, val_t *arr, int max);

/* True if form is one of the definition special forms (define,
 * define-syntax, define-values, define-record-type, define-rule,
 * define-ruleset, define-algebra) — used to enforce R7RS's "internal
 * definitions must precede all expressions in a body" rule. */
bool is_definition(val_t form);

/* Unwind dynamic-wind frames from current_wind down to target, calling each
 * `after` thunk.  Used before longjmp-ing to an escape continuation, both
 * from eval()'s own continuation-invocation case and from apply()/apply_arr(). */
void wind_unwind_to(struct WindFrame *target);

#endif /* CURRY_RUNTIME_INTERNAL_H */
