#pragma once
#include "value.h"
#include <stdbool.h>

/* Algebraic structure declaration for a symbolic operator.
   Covers user-defined operators; built-ins (+, *, etc.) are handled by
   the hard-coded simplifier and need not be declared here.

   Properties applied by sx_simplify after user rules fire:
     - identity elimination:   (op x  id) → x
     - absorbing elimination:  (op x abs) → abs
     - associative flattening: (op (op a b) c) → (op a b c)
     - relations_fn:           called on the expression for custom rewriting  */

typedef struct {
    val_t op;           /* operator symbol (V_VOID = empty slot) */
    bool  commutative;
    bool  associative;
    val_t identity;     /* V_VOID = no identity declared */
    val_t absorbing;    /* V_VOID = no absorbing element */
    val_t relations_fn; /* V_FALSE = none; callable (expr → expr | V_VOID) */
} AlgebraInfo;

void         sx_algebra_init(void);

/* Register (or overwrite) an algebra declaration. */
void         sx_algebra_define(val_t op, bool commutative, bool associative,
                               val_t identity, val_t absorbing, val_t relations_fn);

/* Look up algebra info for op, copying it into *out. Returns false (and
   leaves *out untouched) if op has no declaration. A snapshot copy, not a
   pointer into the live table, so the caller's reads (including invoking
   relations_fn) can't observe a concurrent sx_algebra_define's write
   mid-flight -- see sx_algebra.c's atab_lock comment. */
bool         sx_algebra_lookup(val_t op, AlgebraInfo *out);

/* Semispace GC scanner. */
void         sx_algebra_gc_scan(void);

/* Map an assumption keyword symbol to its SymVar flag bit.
   Returns 0 for unrecognised keywords. */
uint32_t sx_assumption_flag(val_t sym);
