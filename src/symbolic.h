#ifndef CURRY_SYMBOLIC_H
#define CURRY_SYMBOLIC_H

/*
 * Symbolic expressions for Curry Scheme.
 *
 * Extends the numeric tower upward: when any arithmetic operation receives
 * a symbolic argument (T_SYMVAR or T_SYMEXPR), instead of erroring it
 * returns a symbolic expression representing the unevaluated computation.
 *
 *   (symbolic x y)      ; bind x, y as symbolic unknowns
 *   (+ x 2)             ; returns symbolic expr (+ x 2)
 *   (* x x)             ; returns (expt x 2) after simplification
 *   (∂ (* x x) x)       ; returns (* 2 x)
 *   (∂ (* 1/2 m (expt v 2)) v)  ; returns (* m v)
 *
 * Symbolic expressions are printed in standard Scheme prefix notation and
 * are valid Scheme code when all variables are defined.
 */

#include "value.h"
#include <stdbool.h>

/* One-time setup — called from eval_init (after sym_init) */
void symbolic_init(void);

/* ---- Constructors ---- */
val_t sx_make_var(val_t name);                          /* T_SYMVAR from symbol */
val_t sx_make_expr(val_t op, int nargs, val_t *args);   /* T_SYMEXPR */

/* ---- Accessors ---- */
val_t sx_var_name(val_t v);                /* for T_SYMVAR */
val_t sx_expr_op(val_t e);                 /* for T_SYMEXPR */
int   sx_expr_nargs(val_t e);
val_t sx_expr_arg(val_t e, int i);

/* ---- Arithmetic (returns symbolic or numeric) ---- */
val_t sx_add(val_t a, val_t b);
val_t sx_sub(val_t a, val_t b);
val_t sx_mul(val_t a, val_t b);
val_t sx_div(val_t a, val_t b);
val_t sx_neg(val_t a);
val_t sx_abs(val_t a);
val_t sx_expt(val_t base, val_t exp);
val_t sx_sqrt(val_t a);
val_t sx_sin(val_t a);
val_t sx_cos(val_t a);
val_t sx_tan(val_t a);
val_t sx_exp(val_t a);
val_t sx_log(val_t a);

/* ---- CAS operations ---- */
val_t sx_diff(val_t expr, val_t var);      /* symbolic differentiation ∂/∂var */
val_t sx_simplify(val_t expr);             /* algebraic simplification */
val_t sx_substitute(val_t expr, val_t var, val_t val); /* substitute var=val */
bool  sx_equal(val_t a, val_t b);          /* structural equality */

/* ---- Display ---- */
void  sx_write(val_t expr, val_t port);

/* Interned operator symbols (available after symbolic_init) */
extern val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
extern val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;

#endif /* CURRY_SYMBOLIC_H */
