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

/* Register all built-in procedures into env */
void builtins_register(val_t env);

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
