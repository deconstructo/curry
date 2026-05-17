#include "builtins.h"
#include "object.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include "env.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "eval.h"
#include "gc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define DEF(name, fn, min, max) defprim(env, name, fn, min, max)

/* ---- Symbolic / CAS primitives ---- */

static val_t prim_sx_diff(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_diff(av[0], av[1]); }
static val_t prim_sx_simplify(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_simplify(av[0]); }
static val_t prim_sx_substitute(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_substitute(av[0], av[1], av[2]); }
static val_t prim_sx_integrate(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t antideriv = sx_integrate(av[0], av[1]);
    if (ac == 4) {
        val_t Fa = sx_substitute(antideriv, av[1], av[2]);
        val_t Fb = sx_substitute(antideriv, av[1], av[3]);
        return sx_sub(Fb, Fa);
    }
    return antideriv;
}
static val_t prim_frac_diff(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    return sx_fracdiff(av[0], av[1], av[2]);
}
static val_t prim_frac_int(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t result = sx_fracint(av[0], av[1], av[2]);
    if (ac == 5) {
        val_t Fa = sx_substitute(result, av[2], av[3]);
        val_t Fb = sx_substitute(result, av[2], av[4]);
        return sx_sub(Fb, Fa);
    }
    return result;
}
static val_t prim_conjugate(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return num_conjugate(av[0]); }
static val_t prim_sx_real(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return num_real_part(av[0]); }
static val_t prim_sx_imag(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return num_imag_part(av[0]); }
static val_t prim_wirtinger_d(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_wirtinger(av[0], av[1], false); }
static val_t prim_wirtinger_dbar(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_wirtinger(av[0], av[1], true); }
static val_t prim_sym_to_string(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t p = port_open_output_string();
    sx_write_infix(av[0], p);
    return port_get_output_string(p);
}
static val_t prim_sym_to_latex(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    val_t p = port_open_output_string();
    sx_write_latex(av[0], p);
    return port_get_output_string(p);
}
static val_t prim_sym_var(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0])) scm_raise(V_FALSE, "sym-var: first argument must be a symbol");
    if (ac == 1) return sx_make_var(av[0]);
    /* Optional second arg: assumption symbol */
    if (!vis_symbol(av[1])) scm_raise(V_FALSE, "sym-var: second argument must be an assumption symbol");
    const char *s = sym_cstr(av[1]);
    uint32_t flags = 0;
    if      (strcmp(s, "real")     == 0 || strcmp(s, "ṣīrum")     == 0) flags = SYM_ASSUME_REAL;
    else if (strcmp(s, "positive") == 0 || strcmp(s, "damqum")    == 0) flags = SYM_ASSUME_POSITIVE;
    else if (strcmp(s, "negative") == 0 || strcmp(s, "lemnûm")    == 0) flags = SYM_ASSUME_NEGATIVE;
    else if (strcmp(s, "integer")  == 0 || strcmp(s, "nikkassum") == 0) flags = SYM_ASSUME_INTEGER;
    else if (strcmp(s, "nonzero")    == 0 || strcmp(s, "la-ṣifrum")  == 0) flags = SYM_ASSUME_NONZERO;
    else if (strcmp(s, "quaternion") == 0 || strcmp(s, "rebûm")      == 0) flags = SYM_ASSUME_QUATERNION;
    else scm_raise(V_FALSE, "sym-var: unknown assumption (expected real/ṣīrum, positive/damqum, negative/lemnûm, integer/nikkassum, nonzero/la-ṣifrum, quaternion/rebûm)");
    return sx_make_var_flags(av[0], flags);
}
static val_t prim_sym_assumption_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symvar(av[0])) return V_FALSE;
    if (!vis_symbol(av[1])) scm_raise(V_FALSE, "sym-assumption?: second argument must be a symbol");
    const char *s = sym_cstr(av[1]);
    uint32_t f = sym_var_flags(av[0]);
    bool result = false;
    if      (strcmp(s, "real")       == 0 || strcmp(s, "ṣīrum")     == 0) result = (f & (SYM_ASSUME_REAL|SYM_ASSUME_POSITIVE|SYM_ASSUME_NEGATIVE|SYM_ASSUME_INTEGER)) != 0;
    else if (strcmp(s, "positive")   == 0 || strcmp(s, "damqum")    == 0) result = (f & SYM_ASSUME_POSITIVE) != 0;
    else if (strcmp(s, "negative")   == 0 || strcmp(s, "lemnûm")    == 0) result = (f & SYM_ASSUME_NEGATIVE) != 0;
    else if (strcmp(s, "integer")    == 0 || strcmp(s, "nikkassum") == 0) result = (f & SYM_ASSUME_INTEGER)  != 0;
    else if (strcmp(s, "nonzero")    == 0 || strcmp(s, "la-ṣifrum") == 0) result = (f & (SYM_ASSUME_NONZERO|SYM_ASSUME_POSITIVE|SYM_ASSUME_NEGATIVE)) != 0;
    else if (strcmp(s, "quaternion") == 0 || strcmp(s, "rebûm")     == 0) result = (f & SYM_ASSUME_QUATERNION) != 0;
    return vbool(result);
}
static val_t prim_sx_sign(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return sx_sign(av[0]); }
static val_t prim_sym_var_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symvar(av[0])); }
static val_t prim_sym_expr_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symexpr(av[0])); }
static val_t prim_symbolic_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_symbolic(av[0])); }
static val_t prim_sym_var_name(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_symvar(av[0])) scm_raise(V_FALSE, "sym-var-name: not a symbolic variable");
    return sx_var_name(av[0]);
}
static val_t prim_expand(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_expand(av[0]); }
static val_t prim_sx_limit(int ac, val_t *av, void *ud) {
    (void)ud;
    int dir = 0;
    if (ac == 4) {
        /* 4th arg: 'left → -1, 'right → +1, else 0 */
        val_t d = av[3];
        if (vis_symbol(d)) {
            const char *s = sym_cstr(d);
            if (strcmp(s, "left")  == 0) dir = -1;
            else if (strcmp(s, "right") == 0) dir =  1;
        }
    }
    return sx_limit(av[0], av[1], av[2], dir);
}
static val_t prim_sx_series(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[3]) || vunfix(av[3]) < 0)
        scm_raise(V_FALSE, "series: fourth argument must be a non-negative integer");
    return sx_series(av[0], av[1], av[2], (int)vunfix(av[3]));
}

/* ---- Vector calculus (symbolic, Cartesian) ---- */

/* grad(f, vars) — gradient of scalar f; vars is a list of sym-vars.
 * Returns a list (∂f/∂x₁  ∂f/∂x₂  ...) in the same order as vars. */
static val_t prim_grad(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = av[0], vars = av[1];
    val_t result = V_NIL, *tail_ptr = &result;
    while (vis_pair(vars)) {
        val_t v = vcar(vars);
        if (!vis_symvar(v)) scm_raise(V_FALSE, "grad: vars must be a list of symbolic variables");
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = sx_simplify(sx_diff(f, v));
        cell->cdr = V_NIL;
        *tail_ptr = vptr(cell); tail_ptr = &cell->cdr;
        vars = vcdr(vars);
    }
    return result;
}

/* divergence(F, vars) — ∑ ∂Fᵢ/∂xᵢ; F and vars are same-length lists. */
static val_t prim_divergence(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t F = av[0], vars = av[1];
    val_t acc = vfix(0);
    while (vis_pair(F) && vis_pair(vars)) {
        val_t fi = vcar(F), vi = vcar(vars);
        if (!vis_symvar(vi)) scm_raise(V_FALSE, "divergence: vars must be a list of symbolic variables");
        acc = sx_add(acc, sx_diff(fi, vi));
        F = vcdr(F); vars = vcdr(vars);
    }
    return sx_simplify(acc);
}

/* curl(F, vars) — curl of a 3-D vector field; both lists must have exactly 3 elements.
 * Returns (∂Fz/∂y − ∂Fy/∂z,  ∂Fx/∂z − ∂Fz/∂x,  ∂Fy/∂x − ∂Fx/∂y). */
static val_t prim_curl(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t F = av[0], vars = av[1];
    if (!vis_pair(F) || !vis_pair(vcdr(F)) || !vis_pair(vcddr(F)))
        scm_raise(V_FALSE, "curl: vector field must have exactly 3 components");
    if (!vis_pair(vars) || !vis_pair(vcdr(vars)) || !vis_pair(vcddr(vars)))
        scm_raise(V_FALSE, "curl: vars must be a list of exactly 3 variables");
    val_t Fx = vcar(F),       Fy = vcadr(F),       Fz = vcaddr(F);
    val_t x  = vcar(vars),    y  = vcadr(vars),     z  = vcaddr(vars);
    if (!vis_symvar(x) || !vis_symvar(y) || !vis_symvar(z))
        scm_raise(V_FALSE, "curl: vars must be symbolic variables");
    val_t cx = sx_simplify(sx_sub(sx_diff(Fz, y), sx_diff(Fy, z)));
    val_t cy = sx_simplify(sx_sub(sx_diff(Fx, z), sx_diff(Fz, x)));
    val_t cz = sx_simplify(sx_sub(sx_diff(Fy, x), sx_diff(Fx, y)));
    return scm_cons(cx, scm_cons(cy, scm_cons(cz, V_NIL)));
}

/* laplacian(f, vars) — ∑ ∂²f/∂xᵢ² (scalar). */
static val_t prim_laplacian(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = av[0], vars = av[1];
    val_t acc = vfix(0);
    while (vis_pair(vars)) {
        val_t v = vcar(vars);
        if (!vis_symvar(v)) scm_raise(V_FALSE, "laplacian: vars must be a list of symbolic variables");
        acc = sx_add(acc, sx_diff(sx_diff(f, v), v));
        vars = vcdr(vars);
    }
    return sx_simplify(acc);
}

/* vec-laplacian(F, vars) — component-wise Laplacian of a vector field. */
static val_t prim_vec_laplacian(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t F = av[0], vars = av[1];
    val_t result = V_NIL, *tail_ptr = &result;
    while (vis_pair(F)) {
        val_t fi = vcar(F);
        val_t fivars[2] = {fi, vars};
        val_t lfi = prim_laplacian(2, fivars, NULL);
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = lfi; cell->cdr = V_NIL;
        *tail_ptr = vptr(cell); tail_ptr = &cell->cdr;
        F = vcdr(F);
    }
    (void)ac;
    return result;
}

/* dot-product(A, B) — symbolic dot product of two same-length lists. */
static val_t prim_dot_product(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t A = av[0], B = av[1];
    val_t acc = vfix(0);
    while (vis_pair(A) && vis_pair(B)) {
        acc = sx_add(acc, sx_mul(vcar(A), vcar(B)));
        A = vcdr(A); B = vcdr(B);
    }
    return sx_simplify(acc);
}

/* cross-product(A, B) — 3-D symbolic cross product. */
static val_t prim_cross_product(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t A = av[0], B = av[1];
    if (!vis_pair(A) || !vis_pair(vcdr(A)) || !vis_pair(vcddr(A)) ||
        !vis_pair(B) || !vis_pair(vcdr(B)) || !vis_pair(vcddr(B)))
        scm_raise(V_FALSE, "cross-product: both arguments must be lists of exactly 3 elements");
    val_t ax = vcar(A),  ay = vcadr(A),  az = vcaddr(A);
    val_t bx = vcar(B),  by = vcadr(B),  bz = vcaddr(B);
    val_t cx = sx_simplify(sx_sub(sx_mul(ay, bz), sx_mul(az, by)));
    val_t cy = sx_simplify(sx_sub(sx_mul(az, bx), sx_mul(ax, bz)));
    val_t cz = sx_simplify(sx_sub(sx_mul(ax, by), sx_mul(ay, bx)));
    return scm_cons(cx, scm_cons(cy, scm_cons(cz, V_NIL)));
}

static val_t prim_degree(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_degree(av[0], av[1]); }
static val_t prim_collect(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_collect(av[0], av[1]); }
static val_t prim_leading_coeff(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_leading_coeff(av[0], av[1]); }

/* ---- Quantum primitives ---- */
static val_t prim_superpose(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return quantum_from_pairs(av[0]); }
static val_t prim_quantum_uniform(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return quantum_uniform(av[0]); }
static val_t prim_observe(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quantum(av[0])) scm_raise(V_FALSE, "observe: not a quantum value");
    return quantum_observe(av[0]);
}
static val_t prim_quantum_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_quantum(av[0])); }
static val_t prim_quantum_states(int ac, val_t *av, void *ud) {
    (void)ac;(void)ud;
    if (!vis_quantum(av[0])) scm_raise(V_FALSE, "quantum-states: not a quantum value");
    return quantum_to_list(av[0]);
}
static val_t prim_quantum_n(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vfix(quantum_n(av[0])); }

/* ---- Surreal primitives ---- */

static val_t prim_surreal_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vis_surreal(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_infinite_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_FALSE;
      return sur_infinite_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_finite_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_TRUE; /* all normal numbers are finite */
      return sur_finite_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_infinitesimal_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return V_FALSE;
      return sur_infinitesimal_p(av[0]) ? V_TRUE : V_FALSE; }

static val_t prim_surreal_real_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return av[0];
      return sur_real_part(av[0]); }

static val_t prim_surreal_omega_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(0);
      return sur_omega_part(av[0]); }

static val_t prim_surreal_epsilon_part(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(0);
      return sur_epsilon_part(av[0]); }

static val_t prim_surreal_birthday(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return num_abs(av[0]);
      return sur_birthday(av[0]); }

static val_t prim_surreal_to_val(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return av[0];
      return sur_to_val(av[0]); }

static val_t prim_surreal_nterms(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud;
      if (!vis_surreal(av[0])) return vfix(1);
      return vfix(sur_nterms(av[0])); }

/* Build a surreal from a list of (exponent . coefficient) pairs */
static val_t prim_make_surreal(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t lst = av[0];
    int n = 0;
    val_t p = lst;
    while (vis_pair(p)) { n++; p = vcdr(p); }
    if (n == 0) return SUR_ZERO;

    val_t *exps   = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    val_t *coeffs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    p = lst;
    for (int i = 0; i < n; i++) {
        val_t pair = vcar(p);
        if (!vis_pair(pair))
            scm_raise(V_FALSE, "make-surreal: each element must be (exponent . coefficient)");
        exps[i]   = vcar(pair);
        coeffs[i] = vcdr(pair);
        if (!vis_number(exps[i]) || !vis_number(coeffs[i]))
            scm_raise(V_FALSE, "make-surreal: exponents and coefficients must be numbers");
        p = vcdr(p);
    }
    return sur_make(n, exps, coeffs);
}

/* Return list of (exponent . coefficient) pairs */
static val_t prim_surreal_terms(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_surreal(av[0])) {
        /* wrap a plain number as a single-term list */
        val_t pair = scm_cons(vfix(0), av[0]);
        return scm_cons(pair, V_NIL);
    }
    Surreal *s = as_surreal(av[0]);
    val_t result = V_NIL;
    for (int i = s->nterms - 1; i >= 0; i--) {
        val_t pair = scm_cons(s->data[2*i], s->data[2*i+1]);
        result = scm_cons(pair, result);
    }
    return result;
}

/* ---- Numerical quadrature (adaptive Gauss-Kronrod G7K15) ---- */

/*
 * G7K15: 15-point Gauss-Kronrod rule on [-1,1].
 * The 7 Gauss-Legendre nodes are a subset (even indices 0,2,4,6,8,10,12).
 * Error estimate = |K15 - G7|.
 */
static const double GK_NODES[15] = {
    -0.9914553711208126, -0.9491079123427585, -0.8648644233597691,
    -0.7415311855993945, -0.5860872354676911, -0.4058451513773972,
    -0.2077849550078985,  0.0000000000000000,  0.2077849550078985,
     0.4058451513773972,  0.5860872354676911,  0.7415311855993945,
     0.8648644233597691,  0.9491079123427585,  0.9914553711208126
};
static const double GK_WK[15] = {   /* Kronrod weights */
    0.02293532201052922, 0.06309209262997856, 0.10479001032225018,
    0.14065325971552592, 0.16900472663926790, 0.19035057806478541,
    0.20443294007529889, 0.20948214108472783, 0.20443294007529889,
    0.19035057806478541, 0.16900472663926790, 0.14065325971552592,
    0.10479001032225018, 0.06309209262997856, 0.02293532201052922
};
static const double GK_WG[7] = {    /* Gauss weights (nodes 1,3,5,7,9,11,13) */
    0.12948496616886423, 0.27970539148927664, 0.38183005050511894,
    0.41795918367346939, 0.38183005050511894, 0.27970539148927664,
    0.12948496616886423
};

/* Call a Scheme unary function with a C double; return double (NaN on error). */
static double quad_call(val_t f, double x) {
    val_t xv  = num_make_float(x);
    val_t res = apply(f, scm_cons(xv, V_NIL));
    return vis_number(res) ? num_to_double(res) : NAN;
}

/* Apply G7K15 on [a,b]; store K15 result and error estimate. */
static double gk15(val_t f, double a, double b, double *err_out) {
    double mid = (a + b) / 2.0, hw = (b - a) / 2.0;
    double fv[15];
    for (int i = 0; i < 15; i++) fv[i] = quad_call(f, mid + hw * GK_NODES[i]);
    double K = 0.0, G = 0.0;
    for (int i = 0; i < 15; i++) K += GK_WK[i] * fv[i];
    for (int i = 0; i < 7;  i++) G += GK_WG[i] * fv[1 + 2*i];
    K *= hw; G *= hw;
    *err_out = fabs(K - G);
    return K;
}

/* Stack entry for adaptive subdivision */
typedef struct { double a, b, val, err; } QInterval;

#define QUAD_STACK_MAX 2048

static val_t prim_quad(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t f   = av[0];
    double a  = num_to_double(av[1]);
    double b  = num_to_double(av[2]);
    double tol = (ac > 3) ? num_to_double(av[3]) : 1e-8;

    QInterval *stk = (QInterval *)gc_alloc(QUAD_STACK_MAX * sizeof(QInterval));
    int top = 0;
    double err0;
    double v0 = gk15(f, a, b, &err0);
    stk[top++] = (QInterval){a, b, v0, err0};

    double total = 0.0;
    int evals = 15;

    while (top > 0) {
        QInterval cur = stk[--top];
        if (cur.err <= tol * fabs(cur.b - cur.a) / fabs(b - a)
                || top >= QUAD_STACK_MAX - 2
                || evals >= 150000) {
            total += cur.val;
            continue;
        }
        double m = (cur.a + cur.b) / 2.0;
        double e1, e2;
        double v1 = gk15(f, cur.a, m, &e1);
        double v2 = gk15(f, m, cur.b, &e2);
        evals += 30;
        stk[top++] = (QInterval){cur.a, m, v1, e1};
        stk[top++] = (QInterval){m, cur.b, v2, e2};
    }
    return num_make_float(total);
}

/*
 * quad-frac-diff: Grünwald-Letnikov numerical D^α f(x).
 * D^α f(x) ≈ h^{-α} Σ_{k=0}^{N} w_k · f(x - k·h)
 * where h = x/N and w_k = w_{k-1}·(1 - (α+1)/k), w_0 = 1.
 * Accurate for smooth f on [0,x]; N defaults to 200.
 */
static val_t prim_quad_frac_diff(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t f      = av[0];
    double alpha = num_to_double(av[1]);
    double x     = num_to_double(av[2]);
    int    N     = (ac > 3) ? (int)num_to_long(av[3]) : 500;
    if (N < 2) N = 2;
    if (N > 10000) N = 10000;

    double h   = x / N;
    double sum = 0.0;
    double w   = 1.0;
    for (int k = 0; k <= N; k++) {
        sum += w * quad_call(f, x - k * h);
        w *= (1.0 - (alpha + 1.0) / (k + 1));
    }
    return num_make_float(pow(h, -alpha) * sum);
}

/*
 * quad-frac-int: Riemann-Liouville fractional integral I^α f(x).
 * I^α f(x) = (1/Γ(α)) ∫₀ˣ (x-t)^{α-1} f(t) dt
 *
 * Substitution to remove the endpoint singularity: let t = x·(1 - u^{1/α}),
 * which transforms the integral to a smooth form:
 *   I^α f(x) = x^α/Γ(α+1) · ∫₀¹ f(x·(1 - u^{1/α})) du
 * The integrand is smooth for any α > 0 and smooth f, allowing standard G7K15.
 * (Derivation: kernel (x-t)^{α-1} = x^{α-1}·u^{(α-1)/α}, Jacobian x/α·u^{1/α-1},
 *  product = x^α/α · u⁰ = x^α/α; then x^α/(α·Γ(α)) = x^α/Γ(α+1).)
 */
static val_t prim_quad_frac_int(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t f      = av[0];
    double alpha = num_to_double(av[1]);
    double x     = num_to_double(av[2]);
    int    nsub  = (ac > 3) ? (int)num_to_long(av[3]) : 32;

    if (x <= 0.0)  return num_make_float(0.0);
    if (alpha <= 0.0)
        scm_raise(V_FALSE, "quad-frac-int: alpha must be positive");

    double inv_alpha = 1.0 / alpha;
    double result = 0.0;
    for (int i = 0; i < nsub; i++) {
        double u0 = (double)i / nsub, u1 = (double)(i+1) / nsub;
        double hw = (u1 - u0) / 2.0, mid = (u0 + u1) / 2.0;
        double sub = 0.0;
        for (int j = 0; j < 15; j++) {
            double u  = mid + hw * GK_NODES[j];
            double t  = x * (1.0 - pow(u, inv_alpha));
            sub += GK_WK[j] * quad_call(f, t);
        }
        result += hw * sub;
    }
    return num_make_float(pow(x, alpha) * result / tgamma(alpha + 1.0));
}

/* Auto-differentiation: f'(x) via f(x + ε) */
static val_t prim_auto_diff(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t f = av[0];
    val_t x = av[1];
    val_t x_eps = sur_add(sur_from_val(x), SUR_EPSILON);
    val_t fval = apply(f, scm_cons(x_eps, V_NIL));
    if (vis_surreal(fval)) return sur_epsilon_part(fval);
    return vfix(0);
}

/* ---- Symbolic function objects ---- */

/* (sym-fn 'name) or (sym-fn 'name x y t ...) */
static val_t prim_sym_fn(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "sym-fn: first argument must be a symbol");
    val_t name = av[0];
    /* Remaining args are sym-vars forming the params list */
    for (int i = 1; i < ac; i++) {
        if (!vis_symvar(av[i]))
            scm_raise(V_FALSE, "sym-fn: parameters must be symbolic variables");
    }
    /* Build params list in order */
    val_t params = V_NIL;
    val_t *tail  = &params;
    for (int i = 1; i < ac; i++) {
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = av[i]; cell->cdr = V_NIL;
        *tail = vptr(cell); tail = &cell->cdr;
    }
    return sx_make_fn(name, params);
}

static val_t prim_sym_fn_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_symfn(av[0])); }

static val_t prim_sym_fn_name(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symfn(av[0])) scm_raise(V_FALSE, "sym-fn-name: not a symbolic function");
    return sx_fn_name(av[0]);
}

/* (fn-apply f arg0 arg1 ...) — explicit application */
static val_t prim_fn_apply(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symfn(av[0])) scm_raise(V_FALSE, "fn-apply: first argument must be a sym-fn");
    return sx_make_apply(av[0], ac - 1, av + 1);
}

/* ---- Up / Down tuples (contravariant / covariant) ---- */

static val_t prim_up(int argc, val_t *argv, void *ud) {
    (void)ud;
    return num_make_tuple(T_UP, (uint32_t)argc, argv);
}
static val_t prim_down(int argc, val_t *argv, void *ud) {
    (void)ud;
    return num_make_tuple(T_DOWN, (uint32_t)argc, argv);
}
static val_t prim_up_p(int argc, val_t *argv, void *ud)
    { (void)argc; (void)ud; return vis_up(argv[0]) ? V_TRUE : V_FALSE; }
static val_t prim_down_p(int argc, val_t *argv, void *ud)
    { (void)argc; (void)ud; return vis_down(argv[0]) ? V_TRUE : V_FALSE; }
static val_t prim_tuple_p(int argc, val_t *argv, void *ud)
    { (void)argc; (void)ud; return vis_tuple(argv[0]) ? V_TRUE : V_FALSE; }

static val_t prim_ref(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    if (!vis_tuple(argv[0])) scm_raise(V_FALSE, "ref: not a tuple");
    if (!vis_fixnum(argv[1])) scm_raise(V_FALSE, "ref: index must be an exact integer");
    Tuple *t = as_tuple(argv[0]);
    intptr_t i = vunfix(argv[1]);
    if (i < 0 || (uint32_t)i >= t->len)
        scm_raise(V_FALSE, "ref: index %ld out of range (tuple length %u)", (long)i, t->len);
    return t->data[(uint32_t)i];
}
static val_t prim_dimension(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    if (!vis_tuple(argv[0])) scm_raise(V_FALSE, "dimension: not a tuple");
    return vfix((intptr_t)as_tuple(argv[0])->len);
}
static val_t prim_tuple_to_list(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    if (!vis_tuple(argv[0])) scm_raise(V_FALSE, "tuple->list: not a tuple");
    Tuple *t = as_tuple(argv[0]);
    val_t lst = V_NIL;
    for (uint32_t i = t->len; i-- > 0;)
        lst = scm_cons(t->data[i], lst);
    return lst;
}
static val_t prim_list_to_up(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    val_t lst = argv[0]; uint32_t n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    val_t buf[256]; if (n > 256) n = 256;
    uint32_t k = 0;
    for (val_t p = lst; vis_pair(p) && k < n; p = vcdr(p)) buf[k++] = vcar(p);
    return num_make_tuple(T_UP, k, buf);
}
static val_t prim_list_to_down(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    val_t lst = argv[0]; uint32_t n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    val_t buf[256]; if (n > 256) n = 256;
    uint32_t k = 0;
    for (val_t p = lst; vis_pair(p) && k < n; p = vcdr(p)) buf[k++] = vcar(p);
    return num_make_tuple(T_DOWN, k, buf);
}

/* ---- D operator (functional derivative) ----
 *
 * (D f) → a function g such that (g x) = f'(x).
 *
 * Works by applying f to a fresh symbolic variable, differentiating
 * the resulting expression, and returning a closure that substitutes
 * a concrete argument for that variable.  All of Curry's numeric tower
 * participates: (D sin) → cos, (D (lambda (x) (* x x))) → 2x, etc.
 * Higher-order: (D (D f)) differentiates again through the closure.
 */

typedef struct { val_t expr; val_t var; } DCapture;

static val_t d_call(int argc, val_t *argv, void *ud) {
    (void)argc;
    DCapture *cap = (DCapture *)ud;
    return sx_substitute(cap->expr, cap->var, argv[0]);
}

static val_t make_d_closure(val_t expr, val_t var) {
    DCapture *cap = CURRY_NEW(DCapture);
    cap->expr = expr;
    cap->var  = var;
    Primitive *p  = CURRY_NEW(Primitive);
    p->hdr.type   = T_PRIMITIVE; p->hdr.flags = 0;
    p->name       = "D-result";
    p->min_args   = 1; p->max_args = 1;
    p->fn         = d_call;
    p->ud         = cap;
    return vptr(p);
}

static val_t prim_D(int argc, val_t *argv, void *ud) {
    (void)argc; (void)ud;
    val_t f = argv[0];

    /* Mint a fresh sym-var _D0, _D1, ... as the formal argument */
    static int d_counter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "_D%d", d_counter++);
    val_t var = sx_make_var(sym_intern_cstr(buf));

    /* Apply f to the sym-var — the numeric tower lifts to symbolic */
    val_t expr = apply_arr(f, 1, &var);

    /* Differentiate and wrap in a substituting closure */
    return make_d_closure(sx_diff(expr, var), var);
}

/* ---- Integral transforms ---- */
static val_t prim_laplace(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return sx_laplace(av[0], av[1], av[2]); }
static val_t prim_ilaplace(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return sx_ilaplace(av[0], av[1], av[2]); }
static val_t prim_fourier(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return sx_fourier(av[0], av[1], av[2]); }
static val_t prim_ifourier(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return sx_ifourier(av[0], av[1], av[2]); }

/* ---- Registration ---- */

void builtins_curry_register(val_t env) {
    /* Surreal Akkadian/cuneiform constants */
    env_define(env, sym_intern_cstr("dāriš"),         SUR_OMEGA);   /* ω: the eternal */
    env_define(env, sym_intern_cstr("𒀭𒀭"),           SUR_OMEGA);   /* AN.AN = sky-sky = the infinite */
    env_define(env, sym_intern_cstr("ṣiḫrum-ṣīrum"),  SUR_EPSILON); /* ε: supremely tiny */
    env_define(env, sym_intern_cstr("𒉡𒉡𒉡"),         SUR_EPSILON); /* NU.NU.NU = triple-not = infinitesimal */

    DEF("sign",          prim_sx_sign,        1, 1);

    /* ---- Symbolic / CAS and Quantum ---- */
    DEF("∂",              prim_sx_diff,         2, 2);
    DEF("sym-diff",       prim_sx_diff,         2, 2);
    DEF("∫",              prim_sx_integrate,    2, 4);
    DEF("integrate",      prim_sx_integrate,    2, 4);
    DEF("frac-diff",      prim_frac_diff,       3, 3);
    DEF("frac-int",       prim_frac_int,        3, 5);
    DEF("wirtinger-d",    prim_wirtinger_d,     2, 2);
    DEF("wirtinger-dbar", prim_wirtinger_dbar,  2, 2);
    DEF("simplify",       prim_sx_simplify,     1, 1);
    DEF("substitute",     prim_sx_substitute,   3, 3);
    DEF("conjugate",      prim_conjugate,       1, 1);
    DEF("conj",           prim_conjugate,       1, 1);
    DEF("sym->string",    prim_sym_to_string,   1, 1);
    DEF("sym->infix",     prim_sym_to_string,   1, 1);
    DEF("sym->latex",     prim_sym_to_latex,    1, 1);
    DEF("sym-var",        prim_sym_var,         1, 2);
    DEF("sym-var?",       prim_sym_var_p,       1, 1);
    DEF("sym-assumption?",prim_sym_assumption_p,2, 2);
    DEF("sym-expr?",      prim_sym_expr_p,      1, 1);
    DEF("symbolic?",      prim_symbolic_p,      1, 1);
    DEF("sym-var-name",   prim_sym_var_name,    1, 1);
    DEF("expand",         prim_expand,          1, 1);
    DEF("degree",         prim_degree,          2, 2);
    DEF("collect",        prim_collect,         2, 2);
    DEF("leading-coeff",  prim_leading_coeff,   2, 2);
    DEF("limit",          prim_sx_limit,        3, 4);
    DEF("series",         prim_sx_series,       4, 4);
    DEF("sym-fn",         prim_sym_fn,          1, -1);
    DEF("sym-fn?",        prim_sym_fn_p,        1,  1);
    DEF("sym-fn-name",    prim_sym_fn_name,     1,  1);
    DEF("fn-apply",       prim_fn_apply,        1, -1);
    /* Tuples */
    DEF("up",             prim_up,              0, -1);
    DEF("down",           prim_down,            0, -1);
    DEF("up?",            prim_up_p,            1, 1);
    DEF("down?",          prim_down_p,          1, 1);
    DEF("tuple?",         prim_tuple_p,         1, 1);
    DEF("ref",            prim_ref,             2, 2);
    DEF("dimension",      prim_dimension,       1, 1);
    DEF("tuple->list",    prim_tuple_to_list,   1, 1);
    DEF("list->up",       prim_list_to_up,      1, 1);
    DEF("list->down",     prim_list_to_down,    1, 1);
    DEF("D",              prim_D,               1,  1);
    DEF("laplace",        prim_laplace,         3,  3);
    DEF("ilaplace",       prim_ilaplace,        3,  3);
    DEF("fourier",        prim_fourier,         3,  3);
    DEF("ifourier",       prim_ifourier,        3,  3);
    DEF("grad",           prim_grad,            2, 2);
    DEF("gradient",       prim_grad,            2, 2);
    DEF("divergence",     prim_divergence,      2, 2);
    DEF("curl",           prim_curl,            2, 2);
    DEF("laplacian",      prim_laplacian,       2, 2);
    DEF("vec-laplacian",  prim_vec_laplacian,   2, 2);
    DEF("dot-product",    prim_dot_product,     2, 2);
    DEF("cross-product",  prim_cross_product,   2, 2);
    DEF("superpose",      prim_superpose,       1, 1);
    DEF("quantum-uniform",prim_quantum_uniform, 1, 1);
    DEF("observe",        prim_observe,         1, 1);
    DEF("quantum?",       prim_quantum_p,       1, 1);
    DEF("quantum-states", prim_quantum_states,  1, 1);
    DEF("quantum-n",      prim_quantum_n,       1, 1);

    /* Surreal numbers */
    env_define(env, sym_intern_cstr("omega"),   SUR_OMEGA);
    env_define(env, sym_intern_cstr("epsilon"),  SUR_EPSILON);
    DEF("surreal?",             prim_surreal_p,             1, 1);
    DEF("surreal-infinite?",    prim_surreal_infinite_p,    1, 1);
    DEF("surreal-finite?",      prim_surreal_finite_p,      1, 1);
    DEF("surreal-infinitesimal?",prim_surreal_infinitesimal_p,1,1);
    DEF("surreal-real-part",    prim_surreal_real_part,     1, 1);
    DEF("surreal-omega-part",   prim_surreal_omega_part,    1, 1);
    DEF("surreal-epsilon-part", prim_surreal_epsilon_part,  1, 1);
    DEF("surreal-birthday",     prim_surreal_birthday,      1, 1);
    DEF("surreal-nterms",       prim_surreal_nterms,        1, 1);
    DEF("surreal->number",      prim_surreal_to_val,        1, 1);
    DEF("make-surreal",         prim_make_surreal,          1, 1);
    DEF("surreal-terms",        prim_surreal_terms,         1, 1);
    DEF("auto-diff",            prim_auto_diff,             2, 2);
    DEF("quad",                 prim_quad,                  3, 4);
    DEF("quad-frac-diff",       prim_quad_frac_diff,        3, 4);
    DEF("quad-frac-int",        prim_quad_frac_int,         3, 4);

    /* Second AKK_PR pass — registers Akkadian aliases for CAS, surreal, quantum,
     * multivector, and quaternion procedures that are defined after the first pass. */
    {
#define AKK(e, t, c)
#define AKK_SF(e, t, c)
#define AKK_PR(e, t, c) \
        { \
            val_t _v = env_lookup_or_false(env, sym_intern_cstr(e)); \
            if (!vis_false(_v)) { \
                env_define(env, sym_intern_cstr(t), _v); \
                env_define(env, sym_intern_cstr(c), _v); \
            } \
        }
#include "akkadian_names.h"
    }
}
