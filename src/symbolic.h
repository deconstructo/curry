#ifndef CURRY_SYMBOLIC_H
#define CURRY_SYMBOLIC_H

/*
 * Symbolic expressions for Curry Scheme.
 *
 * Extends the numeric tower upward: when any arithmetic operation receives
 * a symbolic argument (T_SYMVAR or T_SYMEXPR), instead of erroring it
 * returns a symbolic expression representing the unevaluated computation.
 *
 * --- Quick reference ---
 *
 *   (symbolic x y)               ; bind x, y as symbolic unknowns in scope
 *   (sym-var 'x)                 ; create symbolic variable from a quoted symbol
 *   (+ x 2)                      ; returns symbolic expr (+ x 2)
 *   (* x x)                      ; stays as (* x x); no automatic expt folding
 *   (∂ (* x x) x)                ; returns (* 2 x)
 *   (∂ (* 1/2 m (expt v 2)) v)   ; returns (* m v)
 *   (simplify expr)              ; algebraic simplification pass
 *   (substitute expr x 3)        ; replace variable x with value 3
 *
 * --- Simplification rules applied by sx_simplify ---
 *
 *   ADD/MUL: nested same-op trees are flattened before folding, e.g.
 *     (+ (+ a b) c)  →  (+ a b c)
 *   ADD: numeric terms are accumulated; zero terms dropped.
 *   MUL: numeric factors are accumulated; zero factor collapses to 0;
 *        coefficient 1 is dropped; coefficient -1 becomes (neg ...).
 *        Note: the -1 → neg optimisation is intentionally skipped for
 *        complex coefficients to preserve type information.
 *   SUB/NEG: constant folding; (- 0 x) → (neg x); (neg (neg x)) → x.
 *   DIV: 0/x → 0; x/1 → x; constant folding.
 *   EXPT: x^0 → 1; x^1 → x; 0^n → 0; 1^n → 1; constant folding.
 *   SQRT/transcendentals: evaluated directly when the argument is numeric.
 *
 *   Zero and one detection uses num_is_zero / num_is_one, which cover all
 *   tower levels (fixnum, bignum, rational, flonum, complex).
 *
 * --- Differentiation rules applied by sx_diff ---
 *
 *   Linearity:  ∂/∂x (f+g+...)  = ∂f/∂x + ∂g/∂x + ...
 *   Product:    ∂/∂x (f·g·...)  = Σᵢ product with fᵢ replaced by ∂fᵢ/∂x
 *   Quotient:   ∂/∂x (f/g)      = (f'g − fg') / g²
 *   Power:      ∂/∂x (f^n)      = n·f^(n−1)·f'      (n numeric)
 *               ∂/∂x (f^g)      = f^g·(g'·ln f + g·f'/f)  (general)
 *   Chain rule: ∂/∂x sin(f)  = cos(f)·f'
 *               ∂/∂x cos(f)  = −sin(f)·f'
 *               ∂/∂x tan(f)  = f' / cos²(f)
 *               ∂/∂x exp(f)  = exp(f)·f'
 *               ∂/∂x log(f)  = f'/f
 *               ∂/∂x √f      = f' / (2·√f)
 *               ∂/∂x |f|     = f·f' / |f|  (undefined at f=0)
 *   Unknown ops: left as unevaluated (∂ expr var) notation.
 *
 * --- Integration rules applied by sx_integrate ---
 *
 *   Linearity:  ∫(f+g+...) dx  = ∫f dx + ∫g dx + ...
 *   Constant:   ∫c dx          = c·x   (c doesn't depend on x)
 *   Const mul:  ∫c·f dx        = c · ∫f dx
 *   Power:      ∫x^n dx        = x^(n+1)/(n+1)   (n ≠ −1)
 *               ∫x^(−1) dx     = ln|x|
 *   Linear sub: ∫f(ax+b)^n dx  = f(ax+b)^(n+1) / (a·(n+1))  (n ≠ −1)
 *               ∫1/(ax+b) dx   = ln|ax+b| / a
 *   sin/cos:    ∫sin(ax+b) dx  = −cos(ax+b)/a
 *               ∫cos(ax+b) dx  = sin(ax+b)/a
 *   tan:        ∫tan(ax+b) dx  = −ln|cos(ax+b)| / a
 *   exp:        ∫exp(ax+b) dx  = exp(ax+b)/a
 *   log:        ∫ln(ax+b) dx   = ((ax+b)·ln(ax+b) − (ax+b)) / a
 *   sqrt:       ∫√(ax+b) dx    = 2(ax+b)^(3/2) / (3a)
 *   Unknown ops / products with var: left as unevaluated (∫ expr var) notation.
 *   Definite:   (∫ f x a b)    = F(b) − F(a)  where F = ∫f dx
 *
 * --- Complex / conjugate operators ---
 *
 *   (conj expr)                  ; symbolic complex conjugate
 *   (real-part expr)             ; symbolic real part (returns symbolic when arg is symbolic)
 *   (imag-part expr)             ; symbolic imaginary part
 *
 *   Simplification identities:
 *     conj(conj(f))  = f
 *     conj(real(f))  = real(f)   (real-part is real-valued)
 *     conj(imag(f))  = imag(f)   (imag-part is real-valued)
 *     imag(real(f))  = 0         (real-part of a real = 0)
 *     imag(imag(f))  = 0
 *     real(conj(f))  = real(f)
 *     imag(conj(f))  = -(imag(f))
 *
 *   Differentiation (x real):
 *     ∂conj(f)/∂x = conj(∂f/∂x)
 *     ∂real(f)/∂x = real(∂f/∂x)
 *     ∂imag(f)/∂x = imag(∂f/∂x)
 *
 *   Integration (x real):
 *     ∫conj(f) dx = conj(∫f dx)
 *     ∫real(f) dx = real(∫f dx)
 *     ∫imag(f) dx = imag(∫f dx)
 *
 * --- Wirtinger calculus ---
 *
 *   Treats z and z̄ = conj(z) as independent variables.
 *
 *   (wirtinger-d    expr z)    ; ∂/∂z:  ∂z/∂z = 1,  ∂conj(z)/∂z = 0
 *   (wirtinger-dbar expr z)    ; ∂/∂z̄: ∂z/∂z̄ = 0, ∂conj(z)/∂z̄ = 1
 *
 *   Key rules:
 *     ∂conj(f)/∂z  = conj(∂f/∂z̄)
 *     ∂conj(f)/∂z̄ = conj(∂f/∂z)
 *     ∂real(f)/∂z  = ½(∂f/∂z + conj(∂f/∂z̄))
 *     ∂imag(f)/∂z  = (∂f/∂z − conj(∂f/∂z̄)) / (2i)
 *   Arithmetic/transcendentals follow the same chain rule as ∂.
 *   A function is holomorphic iff wirtinger-dbar returns 0.
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

/* ---- Arithmetic (continued) ---- */
val_t sx_conj(val_t a);
val_t sx_real(val_t a);
val_t sx_imag(val_t a);

/* ---- CAS operations ---- */
val_t sx_diff(val_t expr, val_t var);                        /* ∂/∂var (real variable) */
val_t sx_wirtinger(val_t expr, val_t var, bool is_dbar);     /* ∂/∂z or ∂/∂z̄ */
val_t sx_integrate(val_t expr, val_t var);                   /* antiderivative ∫ ... dx */
val_t sx_simplify(val_t expr);                               /* algebraic simplification */
val_t sx_substitute(val_t expr, val_t var, val_t val);       /* substitute var=val */
bool  sx_equal(val_t a, val_t b);                            /* structural equality */
bool  sx_depends_on(val_t expr, val_t var);                  /* true if expr contains var */

/* ---- Display ---- */
void  sx_write(val_t expr, val_t port);

/* Interned operator symbols (available after symbolic_init) */
extern val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
extern val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
extern val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;
extern val_t SX_FRACDIFF, SX_FRACINT;

val_t sx_fracdiff(val_t expr, val_t alpha, val_t var); /* D^α fractional derivative */
val_t sx_fracint (val_t expr, val_t alpha, val_t var); /* I^α fractional integral   */

#endif /* CURRY_SYMBOLIC_H */
