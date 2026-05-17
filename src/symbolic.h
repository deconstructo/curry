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
 *   Chain rule: ∂/∂x sin(f)    = cos(f)·f'
 *               ∂/∂x cos(f)    = −sin(f)·f'
 *               ∂/∂x tan(f)    = f' / cos²(f)
 *               ∂/∂x exp(f)    = exp(f)·f'
 *               ∂/∂x log(f)    = f'/f
 *               ∂/∂x √f        = f' / (2·√f)
 *               ∂/∂x |f|       = f·f' / |f|  (undefined at f=0)
 *               ∂/∂x sinh(f)   = cosh(f)·f'
 *               ∂/∂x cosh(f)   = sinh(f)·f'
 *               ∂/∂x tanh(f)   = f' / cosh²(f)
 *               ∂/∂x asin(f)   = f' / √(1−f²)
 *               ∂/∂x acos(f)   = −f' / √(1−f²)
 *               ∂/∂x atan(f)   = f' / (1+f²)
 *               ∂/∂x asinh(f)  = f' / √(f²+1)
 *               ∂/∂x acosh(f)  = f' / √(f²−1)
 *               ∂/∂x atanh(f)  = f' / (1−f²)
 *               ∂/∂x cot(f)    = −f' / sin²(f)
 *               ∂/∂x sec(f)    = sec(f)·tan(f)·f'
 *               ∂/∂x csc(f)    = −csc(f)·cot(f)·f'
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
 *   sinh/cosh:  ∫sinh(f) dx    = cosh(f) / f'  (linear f)
 *               ∫cosh(f) dx    = sinh(f) / f'
 *   tanh:       ∫tanh(f) dx    = ln(cosh(f)) / f'
 *   cot:        ∫cot(f) dx     = ln|sin(f)| / f'
 *   sec:        ∫sec(f) dx     = ln|sec(f)+tan(f)| / f'
 *   csc:        ∫csc(f) dx     = −ln|csc(f)+cot(f)| / f'
 *   sec²/csc²:  ∫sec²(f) dx    = tan(f) / f'
 *               ∫csc²(f) dx    = −cot(f) / f'
 *   inv trig:   ∫asin(f) dx    = (f·asin(f) + √(1−f²)) / f'  (IBP, linear f)
 *               ∫acos(f) dx    = (f·acos(f) − √(1−f²)) / f'
 *               ∫atan(f) dx    = (f·atan(f) − ln(1+f²)/2) / f'
 *               ∫asinh(f) dx   = (f·asinh(f) − √(f²+1)) / f'
 *               ∫acosh(f) dx   = (f·acosh(f) − √(f²−1)) / f'
 *               ∫atanh(f) dx   = (f·atanh(f) + ln(1−f²)/2) / f'
 *   sin²/cos²:  ∫sin²(f) dx    = x/2 − sin(2f)/(4f')  (half-angle, linear f)
 *               ∫cos²(f) dx    = x/2 + sin(2f)/(4f')
 *   IBP products:
 *               ∫x^n·sin(f) dx  = x^n·(−cos(f)/f') − ∫(n·x^(n−1)·(−cos(f)/f')) dx
 *               ∫x^n·cos(f) dx  = x^n·(sin(f)/f')  − ∫(n·x^(n−1)·(sin(f)/f'))  dx
 *               ∫x^n·exp(f) dx  = x^n·(exp(f)/f')  − ∫(n·x^(n−1)·(exp(f)/f'))  dx
 *               ∫x^n·ln(f) dx   = x^(n+1)·ln(f)/(n+1) − ∫x^(n+1)/(n+1)·f'/f dx
 *   Quad. denom:∫1/(x²+k) dx   = atan(x/√k)/√k       (k>0, b=0)
 *               ∫1/(ax²+k) dx  = atan(x√(a/k))/√(ak) (k>0, b=0)
 *   Unknown ops / products with var: left as unevaluated (∫ expr var) notation.
 *   Definite:   (∫ f x a b)    = F(b) − F(a)  where F = ∫f dx
 *
 * --- Limits ---
 *
 *   (limit f x point)           ; two-sided limit x→point
 *   (limit f x point 'left)     ; one-sided: x→point⁻
 *   (limit f x point 'right)    ; one-sided: x→point⁺
 *
 *   Algorithm:
 *     1. Direct substitution — simplify f(point); return if numeric/finite.
 *     2. L'Hôpital — if f = p/q and both p(point)=0 and q(point)=0 (or both ∞),
 *        retry limit(p'/q', x, point).  Repeats up to 5 times.
 *     3. Infinity — point = ±∞: substitute and simplify; finite/∞=0, ∞/∞ → L'Hôpital.
 *     4. Fallback: unevaluated (limit f x point) node.
 *
 * --- Taylor series ---
 *
 *   (series f x point n)        ; expand f around point to order n
 *
 *   Returns the truncated Taylor series as an ADD expression:
 *     Σ_{k=0}^{n} f^(k)(point)/k! · (x − point)^k
 *
 *   Algorithm: compute successive derivatives via sx_diff, evaluate each at
 *   point via sx_substitute, divide by k!, and accumulate non-zero terms.
 *   Zero-coefficient terms are dropped.  If point = 0, (x − 0) simplifies
 *   to x automatically.  Returns an exact rational ADD expression when f is
 *   a standard transcendental and point is exact (e.g. 0).
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

/* Assumption flags stored in SymVar.hdr.flags */
#define SYM_ASSUME_REAL     (1u << 0)  /* variable is real-valued */
#define SYM_ASSUME_POSITIVE (1u << 1)  /* variable is strictly positive (implies real) */
#define SYM_ASSUME_NEGATIVE (1u << 2)  /* variable is strictly negative (implies real) */
#define SYM_ASSUME_INTEGER  (1u << 3)  /* variable is an integer (implies real) */
#define SYM_ASSUME_NONZERO  (1u << 4)  /* variable is nonzero */
#define SYM_ASSUME_QUATERNION (1u << 5) /* variable is quaternion-valued (non-commutative) */

/* Helpers — only meaningful when v is a sym-var */
#define sym_var_flags(v)    (as_symvar(v)->hdr.flags)
#define sym_is_positive(v)  (vis_symvar(v) && (sym_var_flags(v) & SYM_ASSUME_POSITIVE) != 0)
#define sym_is_negative(v)  (vis_symvar(v) && (sym_var_flags(v) & SYM_ASSUME_NEGATIVE) != 0)
#define sym_is_real(v)      (vis_symvar(v) && (sym_var_flags(v) & \
    (SYM_ASSUME_REAL|SYM_ASSUME_POSITIVE|SYM_ASSUME_NEGATIVE|SYM_ASSUME_INTEGER)) != 0)

/* ---- Constructors ---- */
val_t sx_make_var(val_t name);                          /* T_SYMVAR from symbol */
val_t sx_make_var_flags(val_t name, uint32_t flags);    /* T_SYMVAR with assumption flags */
val_t sx_make_expr(val_t op, int nargs, val_t *args);   /* T_SYMEXPR */
val_t sx_make_fn(val_t name, val_t params);              /* T_SYMFN: symbolic function object */
val_t sx_make_apply(val_t fn, int nargs, val_t *args);   /* SX_APPLY: fn applied to args */
val_t sx_fn_name(val_t fn);                              /* name symbol from T_SYMFN */
val_t sx_fn_params(val_t fn);                            /* params list from T_SYMFN */

/* ---- Accessors ---- */
val_t sx_var_name(val_t v);                /* for T_SYMVAR */
val_t sx_expr_op(val_t e);                 /* for T_SYMEXPR */
int   sx_expr_nargs(val_t e);
val_t sx_expr_arg(val_t e, int i);

/* ---- Arithmetic (returns symbolic or numeric) ---- */
val_t sx_add(val_t a, val_t b);
val_t sx_sub(val_t a, val_t b);
val_t sx_mul(val_t a, val_t b);
val_t sx_ncmul(val_t a, val_t b); /* ordered product — use when either arg is quaternion-valued */
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
val_t sx_sinh(val_t a);
val_t sx_cosh(val_t a);
val_t sx_tanh(val_t a);
val_t sx_asin(val_t a);
val_t sx_acos(val_t a);
val_t sx_atan(val_t a);
val_t sx_asinh(val_t a);
val_t sx_acosh(val_t a);
val_t sx_atanh(val_t a);
val_t sx_cot(val_t a);
val_t sx_sec(val_t a);
val_t sx_csc(val_t a);
val_t sx_sign(val_t a);

/* ---- Arithmetic (continued) ---- */
val_t sx_conj(val_t a);
val_t sx_real(val_t a);
val_t sx_imag(val_t a);

/* ---- CAS operations ---- */
val_t sx_trigsimp(val_t expr);                               /* trig identities: sin²+cos²=1, etc. */
val_t sx_diff(val_t expr, val_t var);                        /* ∂/∂var (real variable) */
val_t sx_wirtinger(val_t expr, val_t var, bool is_dbar);     /* ∂/∂z or ∂/∂z̄ */
val_t sx_integrate(val_t expr, val_t var);                   /* antiderivative ∫ ... dx */
val_t sx_simplify(val_t expr);                               /* algebraic simplification */
val_t sx_substitute(val_t expr, val_t var, val_t val);       /* substitute var=val */
bool  sx_equal(val_t a, val_t b);                            /* structural equality */
bool  sx_depends_on(val_t expr, val_t var);                  /* true if expr contains var */

/* limit: dir = 0 (both), -1 (left), +1 (right) */
val_t sx_limit(val_t expr, val_t var, val_t point, int dir);

/* Taylor series: Σ f^(k)(point)/k! · (x−point)^k  for k=0..n */
val_t sx_series(val_t expr, val_t var, val_t point, int n);

/* Integral transforms */
val_t sx_laplace(val_t expr, val_t t_var, val_t s_var);  /* Laplace transform L{expr}(s) */
val_t sx_ilaplace(val_t expr, val_t s_var, val_t t_var); /* inverse Laplace */
val_t sx_fourier(val_t expr, val_t t_var, val_t w_var);  /* Fourier transform F{expr}(ω) */
val_t sx_ifourier(val_t expr, val_t w_var, val_t t_var); /* inverse Fourier */

/* ---- Polynomial / structural operations ---- */
val_t sx_expand(val_t expr);                                 /* distribute * over +, expand integer powers */
val_t sx_degree(val_t expr, val_t var);                      /* polynomial degree in var (fixnum) */
val_t sx_collect(val_t expr, val_t var);                     /* collect like-degree terms in var */
val_t sx_leading_coeff(val_t expr, val_t var);               /* coefficient of highest-degree term */

/* ---- Display ---- */
void  sx_write(val_t expr, val_t port);
void  sx_write_infix(val_t expr, val_t port);   /* infix:  x^2 + 2*x + 1  */
void  sx_write_latex(val_t expr, val_t port);   /* LaTeX:  x^{2} + 2 x + 1 */

/* Interned operator symbols (available after symbolic_init) */
extern val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
extern val_t SX_NCMUL; /* ordered (non-commutative) product */
extern val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
extern val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;
extern val_t SX_FRACDIFF, SX_FRACINT;
extern val_t SX_SINH, SX_COSH, SX_TANH;
extern val_t SX_ASIN, SX_ACOS, SX_ATAN;
extern val_t SX_ASINH, SX_ACOSH, SX_ATANH;
extern val_t SX_COT, SX_SEC, SX_CSC;
extern val_t SX_LIMIT;
extern val_t SX_SIGN;
extern val_t SX_APPLY;   /* symbolic function application: (apply fn arg0 arg1 ...) */
extern val_t SX_LAPLACE; /* unevaluated Laplace transform node */
extern val_t SX_FOURIER; /* unevaluated Fourier transform node */

val_t sx_fracdiff(val_t expr, val_t alpha, val_t var); /* D^α fractional derivative */
val_t sx_fracint (val_t expr, val_t alpha, val_t var); /* I^α fractional integral   */

#endif /* CURRY_SYMBOLIC_H */
