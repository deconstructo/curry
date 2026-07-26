#include "builtins.h"
#include "sx_rules.h"
#include "sx_algebra.h"
#include "sx_poly.h"
#include "sx_special.h"
#include "object.h"
#include "stm.h"
#include "channel.h"
#ifdef BUILD_LLVM
#  include "llvm/curry_llvm.h"
#endif
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include "env.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "eval.h"
#include "vm.h"
#include "gc.h"
#include "workpool.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#define DEF(name, fn, min, max) defprim(env, name, fn, min, max)

/* ---- Symbolic / CAS primitives ---- */

static val_t prim_sx_diff(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_diff(av[0], av[1]); }
static val_t prim_sx_simplify(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_simplify(av[0]); }
static val_t prim_list_rules(int ac, val_t *av, void *ud)
    { (void)ud; return sx_rules_list(ac > 0 ? av[0] : V_FALSE); }
static val_t prim_clear_rules(int ac, val_t *av, void *ud)
    { (void)ud; sx_rules_clear(ac > 0 ? av[0] : V_FALSE); return V_VOID; }

/* ---- Phase 4f: special functions ---- */
static val_t prim_legendre(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_legendre(av[0], av[1]); }
static val_t prim_assoc_legendre(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_assoc_legendre(av[0], av[1], av[2]); }
static val_t prim_hermite(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_hermite(av[0], av[1]); }
static val_t prim_hermite_prob(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_hermite_prob(av[0], av[1]); }
static val_t prim_chebyshev_t(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_chebyshev_t(av[0], av[1]); }
static val_t prim_chebyshev_u(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_chebyshev_u(av[0], av[1]); }
static val_t prim_laguerre(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_laguerre(av[0], av[1]); }
static val_t prim_assoc_laguerre(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_assoc_laguerre(av[0], av[1], av[2]); }
static val_t prim_spherical_harmonic(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_spherical_harmonic(av[0], av[1], av[2], av[3]); }
static val_t prim_gamma(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_gamma(av[0]); }
static val_t prim_log_gamma(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_log_gamma(av[0]); }
static val_t prim_digamma(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_digamma(av[0]); }
static val_t prim_beta_fn(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_beta(av[0], av[1]); }
static val_t prim_erf(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_erf(av[0]); }
static val_t prim_erfc(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_erfc(av[0]); }
static val_t prim_bessel_j(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_bessel_j(av[0], av[1]); }
static val_t prim_bessel_y(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_bessel_y(av[0], av[1]); }
static val_t prim_bessel_i(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_bessel_i(av[0], av[1]); }
static val_t prim_bessel_k(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_bessel_k(av[0], av[1]); }
static val_t prim_elliptic_k(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_elliptic_k(av[0]); }
static val_t prim_elliptic_e(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_elliptic_e(av[0]); }
static val_t prim_elliptic_f(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_elliptic_f(av[0], av[1]); }
static val_t prim_elliptic_pi(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_elliptic_pi(av[0], av[1]); }
static val_t prim_laurent(int ac, val_t *av, void *ud) {
    (void)ud;
    int n = (ac >= 4 && vis_fixnum(av[3])) ? (int)vunfix(av[3]) : 6;
    return sx_laurent(av[0], av[1], av[2], n);
}
static val_t prim_puiseux(int ac, val_t *av, void *ud) {
    (void)ud;
    int n     = (ac >= 4 && vis_fixnum(av[3])) ? (int)vunfix(av[3]) : 6;
    int denom = (ac >= 5 && vis_fixnum(av[4])) ? (int)vunfix(av[4]) : 2;
    return sx_puiseux(av[0], av[1], av[2], n, denom);
}

/* ---- Phase 4c/4d: polynomial machinery and equation solving ---- */
static val_t prim_poly_gcd(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_poly_gcd(av[0], av[1], av[2]); }
static val_t prim_poly_resultant(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_poly_resultant(av[0], av[1], av[2]); }
static val_t prim_poly_squarefree(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_poly_squarefree(av[0], av[1]); }
static val_t prim_poly_factor(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_poly_factor(av[0], av[1]); }
static val_t prim_partial_fractions(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_partial_fractions(av[0], av[1], av[2]); }
static val_t prim_solve(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_solve(av[0], av[1]); }
static val_t prim_solve_system(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_solve_system(av[0], av[1]); }
static val_t prim_groebner(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_groebner(av[0], av[1]); }

static val_t prim_sym_expr(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "sym-expr: first argument must be an operator symbol");
    return sx_simplify(sx_make_expr(av[0], ac - 1, av + 1));
}
static val_t prim_sym_expr_nargs(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symexpr(av[0])) scm_raise(V_FALSE, "sym-expr-nargs: not a sym-expr");
    return vfix((intptr_t)as_symexpr(av[0])->nargs);
}
static val_t prim_sym_expr_arg(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symexpr(av[0])) scm_raise(V_FALSE, "sym-expr-arg: not a sym-expr");
    SymExpr *se = as_symexpr(av[0]);
    intptr_t i = vunfix(av[1]);
    if (i < 0 || i >= (intptr_t)se->nargs)
        scm_raise(V_FALSE, "sym-expr-arg: index out of range");
    return se->args[i];
}
static val_t prim_sym_expr_op(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symexpr(av[0])) scm_raise(V_FALSE, "sym-expr-op: not a sym-expr");
    return as_symexpr(av[0])->op;
}
static val_t prim_assume(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return V_VOID;
    uint32_t flag = sx_assumption_flag(av[1]);
    if (flag) as_symvar(av[0])->hdr.flags |= flag;
    return V_VOID;
}
static val_t prim_can_assume(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return V_FALSE;
    uint32_t flag = sx_assumption_flag(av[1]);
    return (flag && (as_symvar(av[0])->hdr.flags & flag)) ? V_TRUE : V_FALSE;
}
static val_t prim_drop_assumption(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return V_VOID;
    uint32_t flag = sx_assumption_flag(av[1]);
    if (flag) as_symvar(av[0])->hdr.flags &= ~flag;
    return V_VOID;
}
/* ---- with-assumptions support (native compiler codegen, compiler.c) ----
 * The three primitives below back compile_with_assumptions's dynamic-wind
 * desugaring: capture current flags, OR in the new ones, overwrite back to
 * the captured value on the way out. All three silently no-op on a
 * non-SymVar argument, matching the tree-walker's S_WITH_ASSUMPTIONS case
 * (eval.c) which skips non-SymVar clause targets rather than erroring. */
static val_t prim_assumption_flags(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return vfix(0);
    return vfix((intptr_t)as_symvar(av[0])->hdr.flags);
}
static val_t prim_assumption_set(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return V_VOID;
    as_symvar(av[0])->hdr.flags |= (uint32_t)vunfix(av[1]);
    return V_VOID;
}
static val_t prim_assumption_restore(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    if (!vis_symvar(av[0])) return V_VOID;
    as_symvar(av[0])->hdr.flags = (uint32_t)vunfix(av[1]);
    return V_VOID;
}

/* ---- define-rule / define-ruleset support (native compiler codegen,
 * compiler.c: build_define_rule_call) ----
 * (pattern pvars guard-fn action-fn ruleset) — argument order matches
 * sx_rule_add's parameter order exactly. pattern/pvars are quoted source
 * data (never evaluated, matching the tree-walker's S_DEFINE_RULE /
 * S_DEFINE_RULESET); guard-fn/action-fn are real closures compiled and
 * evaluated the ordinary way, so — unlike the tree-eval path this
 * replaces — they correctly close over the enclosing lexical scope
 * instead of always GLOBAL_ENV. */
static val_t prim_define_rule_bang(int ac, val_t *av, void *ud) {
    (void)ud; (void)ac;
    sx_rule_add(av[0], av[1], av[2], av[3], av[4]);
    return V_VOID;
}

/* ---- define-algebra support (native compiler codegen, compiler.c:
 * compile_define_algebra) ----
 * (op kw1 val1 kw2 val2 ...) — op is the already-evaluated operator
 * symbol; keyword tokens are self-evaluating (#:foo), values are ordinary
 * evaluated expressions, matching eval.c's S_DEFINE_ALGEBRA parsing
 * exactly (a keyword with no trailing value is silently ignored, same as
 * the tree-walker's `while (... && vis_pair(vcdr(kws)))` loop). Only
 * registers the algebra info — the auto-bound operator procedure is built
 * and bound by the caller's generated (define ...) so it gets a real
 * lexical binding when possible; see compile_define_algebra. */
static val_t prim_define_algebra_bang(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "%define-algebra!: operator must be a symbol");
    bool commutative = false, associative = false;
    val_t identity = V_VOID, absorbing = V_VOID, relations_fn = V_FALSE;
    for (int i = 1; i + 1 < ac; i += 2) {
        val_t kw = av[i], val = av[i + 1];
        if (kw == S_KW_COMMUTATIVE)      commutative  = !vis_false(val);
        else if (kw == S_KW_ASSOCIATIVE) associative  = !vis_false(val);
        else if (kw == S_KW_IDENTITY)    identity     = val;
        else if (kw == S_KW_ABSORBING)   absorbing    = val;
        else if (kw == S_KW_RELATIONS)   relations_fn = val;
    }
    sx_algebra_define(av[0], commutative, associative, identity, absorbing, relations_fn);
    return V_VOID;
}
static val_t prim_sx_trigsimp(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return sx_trigsimp(av[0]); }
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
static val_t prim_unspecified_p(int ac, val_t *av, void *ud)
    { (void)ac;(void)ud; return vbool(vis_void(av[0])); }
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

/* dot-product(A, B) — symbolic dot product of two same-length tuples or lists. */
static val_t prim_dot_product(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t A = av[0], B = av[1];
    val_t acc = vfix(0);
    if (vis_tuple(A) && vis_tuple(B)) {
        Tuple *ta = as_tuple(A), *tb = as_tuple(B);
        if (ta->len != tb->len)
            scm_raise(V_FALSE, "dot-product: tuple length mismatch (%u vs %u)", ta->len, tb->len);
        for (uint32_t i = 0; i < ta->len; i++)
            acc = sx_add(acc, sx_mul(ta->data[i], tb->data[i]));
    } else {
        while (vis_pair(A) && vis_pair(B)) {
            acc = sx_add(acc, sx_mul(vcar(A), vcar(B)));
            A = vcdr(A); B = vcdr(B);
        }
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
    (void)ac;(void)ud;
    val_t lst = av[0];
    int n = 0;
    val_t p = lst;
    while (vis_pair(p)) { n++; p = vcdr(p); }
    if (n == 0) return SUR_ZERO;

    val_t *exps   = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
    val_t *coeffs = (val_t *)gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
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
    (void)ac;(void)ud;
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

    QInterval *stk = (QInterval *)gc_alloc_raw_pinned(QUAD_STACK_MAX * sizeof(QInterval));
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
    (void)ac;(void)ud;
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

/* ---- partial operator (partial derivative by argument index) ----
 *
 * (partial i) → an operator P such that (P f) is a function g where
 * (g a0 a1 ... an-1) = ∂f/∂aᵢ evaluated at (a0, ..., an-1).
 *
 * Works by minting a fresh sym-var for argument i, applying f with that
 * substituted, differentiating symbolically, then substituting the actual
 * argument back.  Concrete numeric results are returned for concrete args.
 *
 * Typical SICM usage:  ((partial 2) L) — returns ∂L/∂qdot as a function.
 */

typedef struct { val_t f; intptr_t idx; } PartialFn;

static val_t partial_fn_call(int argc, val_t *argv, void *ud) {
    PartialFn *cap = (PartialFn *)ud;
    intptr_t i = cap->idx;

    /* SICM-style: single tuple argument — differentiate w.r.t. component i.
     *
     * Each slot may itself be a tuple (multi-DOF case).  For tuple-valued
     * slots we create a nested tuple of fresh sym-vars so the Lagrangian can
     * call dot-product / ref on them normally.  For slot i we differentiate
     * w.r.t. every inner sym-var and return a tuple of those partials;
     * for scalar slot i we differentiate w.r.t. the single sym-var.
     */
    if (argc == 1 && vis_tuple(argv[0])) {
        Tuple *tup = as_tuple(argv[0]);
        uint32_t n = tup->len;
        if (i < 0 || (uint32_t)i >= n)
            scm_raise(V_FALSE,
                "partial: index %ld out of range for tuple of dimension %u", (long)i, n);
        static int partial_tup_ctr = 0;
        int ctr = partial_tup_ctr++;

        /* Build a replacement tuple where each slot becomes sym-vars.
         * slot_syms[k] is either a single sym-var or a sym-tuple. */
        val_t *slot_syms = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
        /* diff_vars[j] are the sym-vars we differentiate w.r.t. (slot i's vars) */
        uint32_t m_i = vis_tuple(tup->data[(uint32_t)i])
                       ? as_tuple(tup->data[(uint32_t)i])->len : 1;
        val_t *diff_vars = (val_t *)gc_alloc_raw_pinned(m_i * sizeof(val_t));

        for (uint32_t k = 0; k < n; k++) {
            if (vis_tuple(tup->data[k])) {
                Tuple *inner = as_tuple(tup->data[k]);
                val_t *isyms = (val_t *)gc_alloc_raw_pinned(inner->len * sizeof(val_t));
                for (uint32_t j = 0; j < inner->len; j++) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "_∂p%u_%u_%d", k, j, ctr);
                    isyms[j] = sx_make_var(sym_intern_cstr(buf));
                }
                slot_syms[k] = num_make_tuple((int)inner->hdr.type, inner->len, isyms);
                if ((intptr_t)k == i)
                    for (uint32_t j = 0; j < inner->len; j++) diff_vars[j] = isyms[j];
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "_∂p%u_%d", k, ctr);
                val_t sv = sx_make_var(sym_intern_cstr(buf));
                slot_syms[k] = sv;
                if ((intptr_t)k == i) diff_vars[0] = sv;
            }
        }

        val_t sym_tup = num_make_tuple((int)tup->hdr.type, n, slot_syms);
        val_t expr    = apply_arr(cap->f, 1, &sym_tup);

        /* Substitute all slot sym-vars back with their actual values */
#define SUBST_ALL_SLOTS(dval) \
    do { \
        for (uint32_t k = 0; k < n; k++) { \
            if (vis_tuple(slot_syms[k])) { \
                Tuple *ss = as_tuple(slot_syms[k]); \
                Tuple *ts = as_tuple(tup->data[k]); \
                for (uint32_t j2 = 0; j2 < ss->len; j2++) \
                    (dval) = sx_substitute((dval), ss->data[j2], ts->data[j2]); \
            } else { \
                (dval) = sx_substitute((dval), slot_syms[k], tup->data[k]); \
            } \
        } \
    } while (0)

        if (vis_tuple(tup->data[(uint32_t)i])) {
            /* Multi-DOF: return a tuple of partial derivatives */
            val_t *diffs = (val_t *)gc_alloc_raw_pinned(m_i * sizeof(val_t));
            for (uint32_t j = 0; j < m_i; j++) {
                val_t dj = sx_diff(expr, diff_vars[j]);
                SUBST_ALL_SLOTS(dj);
                diffs[j] = sx_simplify(dj);
            }
            /* Gradient of scalar w.r.t. up-tuple is a down-tuple */
            int ret_type = vis_up(tup->data[(uint32_t)i]) ? T_DOWN : T_UP;
            return num_make_tuple(ret_type, m_i, diffs);
        } else {
            /* Scalar slot: original behaviour */
            val_t deriv = sx_diff(expr, diff_vars[0]);
            SUBST_ALL_SLOTS(deriv);
            return sx_simplify(deriv);
        }
#undef SUBST_ALL_SLOTS
    }

    if (i < 0 || (intptr_t)argc <= i)
        scm_raise(V_FALSE,
            "partial: index %ld out of range for %d-argument call", (long)i, argc);

    static int partial_counter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "_∂%d", partial_counter++);
    val_t var = sx_make_var(sym_intern_cstr(buf));

    /* Build arg list with argv[i] replaced by fresh sym-var */
    val_t *args2 = (val_t *)gc_alloc_raw_pinned((size_t)argc * sizeof(val_t));
    for (int j = 0; j < argc; j++) args2[j] = argv[j];
    val_t orig  = args2[(uint32_t)i];
    args2[(uint32_t)i] = var;

    /* Apply f, differentiate, substitute original back */
    val_t expr   = apply_arr(cap->f, argc, args2);
    val_t dexpr  = sx_diff(expr, var);
    return sx_substitute(dexpr, var, orig);
}

static val_t make_partial_fn(val_t f, intptr_t idx) {
    PartialFn *cap   = (PartialFn *)gc_alloc_raw_pinned(sizeof(PartialFn));
    cap->f           = f;
    cap->idx         = idx;
    Primitive *p     = CURRY_NEW_PINNED(Primitive);
    p->hdr.type      = T_PRIMITIVE; p->hdr.flags = 0;
    p->name          = "partial-derivative";
    p->min_args      = 0; p->max_args = -1;
    p->fn            = partial_fn_call;
    p->ud            = cap;
    return vptr(p);
}

typedef struct { intptr_t idx; } PartialOp;

static val_t partial_op_call(int argc, val_t *argv, void *ud) {
    (void)argc;
    PartialOp *op = (PartialOp *)ud;
    return make_partial_fn(argv[0], op->idx);
}

static val_t prim_partial(int argc, val_t *argv, void *ud) {
    (void)ud;
    if (!vis_fixnum(argv[0]))
        scm_raise(V_FALSE, "partial: argument must be an exact integer index");
    intptr_t i = vunfix(argv[0]);

    /* 1-arg form: (partial i) → operator that takes f */
    if (argc == 1) {
        PartialOp *op    = (PartialOp *)gc_alloc_raw_pinned_atomic(sizeof(PartialOp));
        op->idx          = i;
        Primitive *p     = CURRY_NEW_PINNED(Primitive);
        p->hdr.type      = T_PRIMITIVE; p->hdr.flags = 0;
        p->name          = "partial-op";
        p->min_args      = 1; p->max_args = 1;
        p->fn            = partial_op_call;
        p->ud            = op;
        return vptr(p);
    }

    /* 2-arg form: (partial i f) → the operator applied to f immediately */
    return make_partial_fn(argv[1], i);
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
    DCapture *cap = (DCapture *)gc_alloc_raw_pinned(sizeof(DCapture));
    cap->expr = expr;
    cap->var  = var;
    Primitive *p  = CURRY_NEW_PINNED(Primitive);
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

/* ---- tree-eval: force evaluation through the tree-walking interpreter ---- */

static val_t prim_tree_eval(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return eval(av[0], GLOBAL_ENV);
}

/* ---- Parallel map, reduce, for-each/par — backed by work-stealing pool ---- */

static int map_par_threshold = 8;

/* Shared wait pattern: submit, wait, check errors. */
#define POOL_WAIT(n_done_var, nchunks_var, done_mutex_var, done_cond_var) \
    do { \
        pthread_mutex_lock(&(done_mutex_var)); \
        while (atomic_load(&(n_done_var)) < (nchunks_var)) \
            pthread_cond_wait(&(done_cond_var), &(done_mutex_var)); \
        pthread_mutex_unlock(&(done_mutex_var)); \
        pthread_mutex_destroy(&(done_mutex_var)); \
        pthread_cond_destroy(&(done_cond_var)); \
    } while (0)

/* Sequential map — also exported as map/seq */
static val_t prim_map_seq(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    int nlists = ac - 1;
    val_t *lists = av + 1;
    val_t result = V_NIL;
    for (;;) {
        for (int i = 0; i < nlists; i++)
            if (!vis_pair(lists[i])) return scm_reverse(result);
        val_t args = V_NIL;
        for (int i = nlists - 1; i >= 0; i--)
            args = scm_cons(vcar(lists[i]), args);
        result = scm_cons(apply(proc, args), result);
        for (int i = 0; i < nlists; i++)
            lists[i] = vcdr(lists[i]);
    }
}

static val_t prim_map(int ac, val_t *av, void *ud) {
    if (ac != 2) return prim_map_seq(ac, av, ud);
    val_t proc = av[0], lst = av[1];
    int n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    if (n < map_par_threshold || pool_is_worker) return prim_map_seq(ac, av, ud);

    val_t *elems   = gc_alloc_raw_pinned(n * sizeof(val_t));
    val_t *results = gc_alloc_raw_pinned(n * sizeof(val_t));
    { val_t p = lst; for (int i = 0; i < n; i++, p = vcdr(p)) elems[i] = vcar(p); }

    atomic_int n_done; atomic_init(&n_done, 0);
    pthread_mutex_t mu; pthread_mutex_init(&mu, NULL);
    pthread_cond_t  cv; pthread_cond_init(&cv,  NULL);
    int nchunks;
    WorkItem **items = pool_submit(WORK_MAP, proc, elems, results, n,
                                   &nchunks, &n_done, &mu, &cv);
    POOL_WAIT(n_done, nchunks, mu, cv);

    for (int c = 0; c < nchunks; c++)
        if (items[c]->error) scm_raise(V_FALSE, "map: error in parallel worker");

    val_t result = V_NIL;
    for (int i = n - 1; i >= 0; i--)
        result = scm_cons(results[i], result);
    return result;
}

/* (reduce f ridentity lst)
 * f must be associative — the programmer's contract, not enforced.
 * ridentity is returned for the empty list only; not used as a per-chunk
 * seed (that would apply it nchunks times, not once). */

static val_t prim_reduce_seq(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t fn = av[0], ridentity = av[1], lst = av[2];
    if (!vis_pair(lst)) return ridentity;
    val_t acc = vcar(lst);
    for (lst = vcdr(lst); vis_pair(lst); lst = vcdr(lst))
        acc = apply(fn, scm_cons(acc, scm_cons(vcar(lst), V_NIL)));
    return acc;
}

static val_t prim_reduce(int ac, val_t *av, void *ud) {
    val_t fn = av[0], ridentity = av[1], lst = av[2];
    if (!vis_pair(lst)) return ridentity;
    int n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    if (n < map_par_threshold || pool_is_worker) return prim_reduce_seq(ac, av, ud);

    val_t *elems = gc_alloc_raw_pinned(n * sizeof(val_t));
    { val_t p = lst; for (int i = 0; i < n; i++, p = vcdr(p)) elems[i] = vcar(p); }

    atomic_int n_done; atomic_init(&n_done, 0);
    pthread_mutex_t mu; pthread_mutex_init(&mu, NULL);
    pthread_cond_t  cv; pthread_cond_init(&cv,  NULL);
    int nchunks;
    WorkItem **items = pool_submit(WORK_REDUCE, fn, elems, NULL, n,
                                   &nchunks, &n_done, &mu, &cv);
    POOL_WAIT(n_done, nchunks, mu, cv);

    for (int c = 0; c < nchunks; c++)
        if (items[c]->error) scm_raise(V_FALSE, "reduce: error in parallel worker");

    val_t acc = items[0]->result;
    for (int c = 1; c < nchunks; c++)
        acc = apply(fn, scm_cons(acc, scm_cons(items[c]->result, V_NIL)));
    return acc;
}

static val_t prim_get_map_threshold(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud; return vfix(map_par_threshold);
}

static val_t prim_set_map_threshold(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) < 1)
        scm_raise(V_FALSE, "set-map-parallel-threshold!: expected positive integer");
    map_par_threshold = (int)vunfix(av[0]);
    return V_VOID;
}

static val_t prim_for_each_par(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t proc = av[0];
    if (ac != 2) {
        int nlists = ac - 1; val_t *lists = av + 1;
        for (;;) {
            for (int i = 0; i < nlists; i++)
                if (!vis_pair(lists[i])) return V_VOID;
            val_t args = V_NIL;
            for (int i = nlists - 1; i >= 0; i--)
                args = scm_cons(vcar(lists[i]), args);
            apply(proc, args);
            for (int i = 0; i < nlists; i++) lists[i] = vcdr(lists[i]);
        }
    }
    val_t lst = av[1];
    int n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    if (n < map_par_threshold || pool_is_worker) {
        for (val_t p = lst; vis_pair(p); p = vcdr(p))
            apply(proc, scm_cons(vcar(p), V_NIL));
        return V_VOID;
    }
    val_t *elems = gc_alloc_raw_pinned(n * sizeof(val_t));
    { val_t p = lst; for (int i = 0; i < n; i++, p = vcdr(p)) elems[i] = vcar(p); }

    atomic_int n_done; atomic_init(&n_done, 0);
    pthread_mutex_t mu; pthread_mutex_init(&mu, NULL);
    pthread_cond_t  cv; pthread_cond_init(&cv,  NULL);
    int nchunks;
    WorkItem **items = pool_submit(WORK_FOREACH, proc, elems, NULL, n,
                                   &nchunks, &n_done, &mu, &cv);
    POOL_WAIT(n_done, nchunks, mu, cv);

    for (int c = 0; c < nchunks; c++)
        if (items[c]->error) scm_raise(V_FALSE, "for-each/par: error in parallel worker");
    return V_VOID;
}

/* ---- SRFI-27 random number primitives ---- */

/* xoshiro256+ PRNG state — global, seeded lazily */
static uint64_t rng_s[4] = { 0x123456789abcdef0ULL, 0xdeadbeefcafeULL,
                              0x0123456789ULL,        0xfedcba9876543210ULL };
static bool rng_seeded = false;

static uint64_t rng_rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t rng_next(void) {
    const uint64_t result = rng_s[0] + rng_s[3];
    const uint64_t t = rng_s[1] << 17;
    rng_s[2] ^= rng_s[0]; rng_s[3] ^= rng_s[1];
    rng_s[1] ^= rng_s[2]; rng_s[0] ^= rng_s[3];
    rng_s[2] ^= t;
    rng_s[3] = rng_rotl(rng_s[3], 45);
    return result;
}

static void rng_seed_from_os(void) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        (void)fread(rng_s, sizeof(rng_s), 1, f);
        fclose(f);
    } else {
        uint64_t t = (uint64_t)time(NULL);
        rng_s[0] ^= t; rng_s[1] ^= t * 6364136223846793005ULL;
        rng_s[2] ^= ~t; rng_s[3] ^= t ^ (t >> 33);
    }
    rng_seeded = true;
}

static val_t prim_random_real(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    if (!rng_seeded) rng_seed_from_os();
    uint64_t bits = (rng_next() >> 11) | 0x3FF0000000000000ULL;
    double   d;
    memcpy(&d, &bits, sizeof(d));
    return num_make_float(d - 1.0);
}

static val_t prim_random_seed(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    rng_seed_from_os();
    return V_VOID;
}

static val_t prim_make_random_source(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return sym_intern_cstr("default-random-source");
}

static val_t prim_random_source_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return vis_symbol(av[0]) ? V_TRUE : V_FALSE;
}

static val_t prim_random_source_to_real(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return env_lookup(GLOBAL_ENV, sym_intern_cstr("random-real"));
}

static val_t prim_random_integer(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!rng_seeded) rng_seed_from_os();
    intptr_t n = vunfix(av[0]);
    if (n <= 0) return vfix(0);
    return vfix((intptr_t)(rng_next() % (uint64_t)n));
}

/* ---- STM primitives ---- */

static val_t prim_make_tvar(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return stm_make_tvar(av[0]); }
static val_t prim_tvar_read(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return stm_tvar_read(av[0]); }
static val_t prim_tvar_write(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; stm_tvar_write(av[0], av[1]); return V_VOID; }
static val_t prim_tvar_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_tvar(av[0])); }
static val_t prim_atomically(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return stm_atomically(av[0]); }
static val_t prim_stm_retry(int ac, val_t *av, void *ud)
    { (void)ac; (void)av; (void)ud; stm_retry(); }
static val_t prim_or_else(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return stm_or_else(av[0], av[1]); }

/* ---- Channel primitives ---- */

static val_t prim_make_channel(int ac, val_t *av, void *ud) {
    (void)ud;
    uint32_t cap = 0;
    if (ac == 1) {
        if (!vis_fixnum(av[0]))
            scm_raise(V_FALSE, "make-channel: capacity must be an exact integer");
        intptr_t n = vunfix(av[0]);
        if (n < 0) scm_raise(V_FALSE, "make-channel: capacity must be >= 0");
        cap = (uint32_t)n;
    }
    return channel_make(cap);
}
static val_t prim_channel_send(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; channel_send(av[0], av[1]); return V_VOID; }
static val_t prim_channel_recv(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return channel_recv(av[0]); }
static val_t prim_channel_close(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; channel_close(av[0]); return V_VOID; }
static val_t prim_channel_closed_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(channel_closed(av[0])); }
static val_t prim_channel_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_channel(av[0])); }
static val_t prim_channel_try_send(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return channel_try_send(av[0], av[1]); }
static val_t prim_channel_try_recv(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return channel_try_recv(av[0]); }
static val_t prim_channel_blocked_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(av[0] == V_UNDEF); }

/* ---- LLVM JIT builtins (compiled in only when BUILD_LLVM=ON) ---- */

#ifdef BUILD_LLVM
static val_t prim_llvm_available(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return curry_llvm_available() ? V_TRUE : V_FALSE;
}
static val_t prim_llvm_dump_last(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    curry_llvm_dump_last();
    return V_VOID;
}
static val_t prim_jit_call(int ac, val_t *av, void *ud) {
    /* (curry-jit-call proc) — force-JIT-compile proc then call it.
     * For BcClosure: extract src_lambda, compile, hot-swap jit_val, then call.
     * Falls back to apply_arr if JIT unavailable or src_lambda not stored. */
    (void)ac; (void)ud;
    val_t proc = av[0];
    if (vis_bcclosure(proc)) {
        BcClosure *cl = as_bcclosure(proc);
        if (cl->chunk->src_lambda != V_VOID) {
            val_t compiled = curry_llvm_jit_compile(cl->chunk->src_lambda);
            if (vis_jitclosure(compiled))
                __atomic_store_n(&cl->jit_val, compiled, __ATOMIC_RELEASE);
        }
        return apply_arr(proc, 0, NULL);
    }
    return curry_jit_call(proc);
}
static val_t prim_jit_eval(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return curry_jit_eval_expr(av[0]);
}
static val_t prim_jit_call_depth(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return vfix(g_jit_call_depth);
}
static val_t prim_jit_compile(int ac, val_t *av, void *ud) {
    /* (jit-compile proc) — force immediate JIT compilation of proc.
     * Returns the same proc (with jit_val set) or proc unchanged on failure. */
    (void)ac; (void)ud;
    val_t proc = av[0];
    if (!vis_bcclosure(proc)) return proc;
    BcClosure *cl = as_bcclosure(proc);
    if (cl->chunk->src_lambda == V_VOID) return proc;
    if (cl->upval_count > 0 && !cl->chunk->upval_names) return proc;
    extern val_t jit_wrap_upvals(BcClosure *);
    val_t src = (cl->upval_count > 0)
        ? jit_wrap_upvals(cl)
        : cl->chunk->src_lambda;
    val_t compiled = curry_llvm_jit_compile(src);
    if (vis_jitclosure(compiled))
        __atomic_store_n(&cl->jit_val, compiled, __ATOMIC_RELEASE);
    return proc;
}
#endif /* BUILD_LLVM */

/* (jit-never! proc) — permanently exempt proc from tiered-JIT promotion.
 * Unconditional (not guarded by BUILD_LLVM): the jit_val field and the
 * V_FALSE "never compile this instance" sentinel both exist regardless of
 * whether LLVM is built in, so this is meaningful (if inert) either way.
 * Uses the exact same store maybe_jit_bcc (src/runtime.c) already uses to
 * permanently pin a self-referencing named-let closure — this is that
 * mechanism made callable directly, not a new one. jit_val is set with a
 * release store BEFORE any call can observe it, so no call ever races
 * ahead and promotes proc between this store and its first invocation.
 * Per-BcClosure-instance, not per-Chunk: pinning one closure has no effect
 * on any other closure compiled from the same lambda source (each fresh
 * closure gets its own independent jit_val/call_count — see vm_make_closure
 * in src/vm.c). Returns proc unchanged (including when it isn't a
 * BcClosure at all, e.g. an already-native or C-primitive procedure,
 * for which "never JIT" is trivially and harmlessly already true). */
static val_t prim_jit_never(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t proc = av[0];
    if (vis_bcclosure(proc)) {
        BcClosure *cl = as_bcclosure(proc);
        __atomic_store_n(&cl->jit_val, V_FALSE, __ATOMIC_RELEASE);
    }
    return proc;
}

/* (jit-compiled? proc) — has proc actually been promoted to native code
 * right now? #f for an unpromoted BcClosure (whether merely not-yet-hot or
 * permanently jit-never!'d — both leave jit_val as something other than a
 * real T_JITCLOSURE) and for any non-BcClosure procedure. Introspection
 * only; exists so callers (tests, this feature's own verification) can
 * directly observe the pinning `jit-never!` performs rather than inferring
 * it indirectly from timing. */
static val_t prim_jit_compiled_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t proc = av[0];
    if (!vis_bcclosure(proc)) return V_FALSE;
    BcClosure *cl = as_bcclosure(proc);
    val_t jv = __atomic_load_n(&cl->jit_val, __ATOMIC_ACQUIRE);
    return vbool(vis_jitclosure(jv));
}

/* ── GC builtins ─────────────────────────────────────────────────────────── */

static val_t prim_gc_collect(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    gc_collect();
    return V_VOID;
}

/* gc-stats is defined in builtins.c with full instrumentation */

static val_t prim_gc_on_coll(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    /* gc-on-collection is a no-op until Phase 5 wires a collection hook. */
    return V_VOID;
}

/* ---- Registration ---- */

static val_t prim_hardware_concurrency(int ac, val_t *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    return vfix(pool_hw_concurrency());
}

void builtins_curry_register(val_t env) {
    pool_init();   /* start the work-stealing thread pool */

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
    DEF("trigsimp",       prim_sx_trigsimp,     1, 1);
    DEF("substitute",     prim_sx_substitute,   3, 3);
    DEF("conjugate",      prim_conjugate,       1, 1);
    DEF("conj",           prim_conjugate,       1, 1);
    DEF("sym->string",    prim_sym_to_string,   1, 1);
    DEF("sym->infix",     prim_sym_to_string,   1, 1);
    DEF("sym->latex",     prim_sym_to_latex,    1, 1);
    DEF("sym-var",        prim_sym_var,         1, 2);
    DEF("unspecified?",   prim_unspecified_p,   1, 1);
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
    DEF("partial",        prim_partial,          1,  2);
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
    DEF("tree-eval",            prim_tree_eval,             1, 1);

    /* Parallel map / reduce */
    DEF("map",                        prim_map,               2,-1);
    DEF("map/seq",                    prim_map_seq,           2,-1);
    DEF("reduce",                     prim_reduce,            3, 3);
    DEF("reduce/seq",                 prim_reduce_seq,        3, 3);
    DEF("map-parallel-threshold",     prim_get_map_threshold,    0, 0);
    DEF("set-map-parallel-threshold!",prim_set_map_threshold,    1, 1);
    DEF("for-each/par",               prim_for_each_par,          2,-1);
    DEF("hardware-concurrency",       prim_hardware_concurrency,  0, 0);

    /* SRFI-27 random numbers */
    DEF("random-real",                    prim_random_real,           0,0);
    DEF("random-integer",                 prim_random_integer,        1,1);
    DEF("make-random-source",             prim_make_random_source,    0,0);
    DEF("random-source?",                 prim_random_source_p,       1,1);
    DEF("random-source-randomize!",       prim_random_seed,           1,1);
    DEF("random-source-pseudo-randomize!",prim_random_seed,           3,3);
    DEF("random-source->random-real",     prim_random_source_to_real, 1,1);
    DEF("random-source->random-integer",  prim_random_source_to_real, 1,1);

    /* Special functions (Phase 4f) */
    DEF("legendre",           prim_legendre,          2, 2);
    DEF("assoc-legendre",     prim_assoc_legendre,    3, 3);
    DEF("hermite",            prim_hermite,           2, 2);
    DEF("hermite-prob",       prim_hermite_prob,      2, 2);
    DEF("chebyshev-t",        prim_chebyshev_t,       2, 2);
    DEF("chebyshev-u",        prim_chebyshev_u,       2, 2);
    DEF("laguerre",           prim_laguerre,          2, 2);
    DEF("assoc-laguerre",     prim_assoc_laguerre,    3, 3);
    DEF("spherical-harmonic", prim_spherical_harmonic,4, 4);
    DEF("gamma",              prim_gamma,             1, 1);
    DEF("log-gamma",          prim_log_gamma,         1, 1);
    DEF("digamma",            prim_digamma,           1, 1);
    DEF("beta",               prim_beta_fn,           2, 2);
    DEF("erf",                prim_erf,               1, 1);
    DEF("erfc",               prim_erfc,              1, 1);
    DEF("bessel-j",           prim_bessel_j,          2, 2);
    DEF("bessel-y",           prim_bessel_y,          2, 2);
    DEF("bessel-i",           prim_bessel_i,          2, 2);
    DEF("bessel-k",           prim_bessel_k,          2, 2);
    DEF("elliptic-k",         prim_elliptic_k,        1, 1);
    DEF("elliptic-e",         prim_elliptic_e,        1, 1);
    DEF("elliptic-f",         prim_elliptic_f,        2, 2);
    DEF("elliptic-pi",        prim_elliptic_pi,       2, 2);
    /* Series (Phase 4g) */
    DEF("laurent",            prim_laurent,           3, 4);
    DEF("puiseux",            prim_puiseux,           3, 5);

    /* Polynomial machinery (Phase 4c) and equation solving (Phase 4d) */
    DEF("poly-gcd",          prim_poly_gcd,          3, 3);
    DEF("poly-resultant",    prim_poly_resultant,     3, 3);
    DEF("poly-squarefree",   prim_poly_squarefree,    2, 2);
    DEF("poly-factor",       prim_poly_factor,        2, 2);
    DEF("partial-fractions", prim_partial_fractions,  3, 3);
    DEF("solve",             prim_solve,              2, 2);
    DEF("solve-system",      prim_solve_system,       2, 2);
    DEF("groebner",          prim_groebner,           2, 2);

    /* User-defined rewrite rules */
    DEF("list-rules",   prim_list_rules,  0, 1);
    DEF("clear-rules!", prim_clear_rules, 0, 1);

    /* Assumptions on sym-vars */
    DEF("assume!",          prim_assume,          2, 2);
    DEF("can-assume?",      prim_can_assume,       2, 2);
    DEF("drop-assumption!", prim_drop_assumption,  2, 2);

    /* with-assumptions support — see compile_with_assumptions (compiler.c) */
    DEF("%assumption-flags",   prim_assumption_flags,   1, 1);
    DEF("%assumption-set!",    prim_assumption_set,     2, 2);
    DEF("%assumption-restore!",prim_assumption_restore, 2, 2);

    /* define-rule / define-ruleset / define-algebra support — see
     * build_define_rule_call / compile_define_algebra (compiler.c) */
    DEF("%define-rule!",      prim_define_rule_bang,     5,  5);
    DEF("%define-algebra!",   prim_define_algebra_bang,  1, -1);

    /* Low-level SymExpr constructor and accessors */
    DEF("sym-expr",       prim_sym_expr,       1, -1);
    DEF("sym-expr-nargs", prim_sym_expr_nargs, 1,  1);
    DEF("sym-expr-arg",   prim_sym_expr_arg,   2,  2);
    DEF("sym-expr-op",    prim_sym_expr_op,    1,  1);
    env_define(env, sym_intern_cstr("default-random-source"),
               sym_intern_cstr("default-random-source"));

    /* STM — transactional variables */
    DEF("make-tvar",          prim_make_tvar,          1, 1);
    DEF("tvar-read",          prim_tvar_read,          1, 1);
    DEF("tvar-write!",        prim_tvar_write,         2, 2);
    DEF("tvar?",              prim_tvar_p,             1, 1);
    DEF("atomically",         prim_atomically,         1, 1);
    DEF("retry",              prim_stm_retry,          0, 0);
    DEF("%or-else",           prim_or_else,            2, 2);

    /* Channels — CSP buffered communication */
    DEF("make-channel",       prim_make_channel,       0, 1);
    DEF("channel-send!",      prim_channel_send,       2, 2);
    DEF("channel-recv!",      prim_channel_recv,       1, 1);
    DEF("channel-close!",     prim_channel_close,      1, 1);
    DEF("channel-closed?",    prim_channel_closed_p,   1, 1);
    DEF("channel?",           prim_channel_p,          1, 1);
    DEF("%channel-try-send",  prim_channel_try_send,   2, 2);
    DEF("%channel-try-recv",  prim_channel_try_recv,   1, 1);
    DEF("%channel-blocked?",  prim_channel_blocked_p,  1, 1);

    /* LLVM JIT control procedures */
#ifdef BUILD_LLVM
    DEF("curry-llvm-available?", prim_llvm_available,    0, 0);
    DEF("curry-llvm-dump-last",  prim_llvm_dump_last,    0, 0);
    DEF("curry-jit-call",        prim_jit_call,          1, 1);
    DEF("curry-jit-eval",        prim_jit_eval,          1, 1);
    DEF("jit-call-depth",        prim_jit_call_depth,    0, 0);
    DEF("jit-compile!",          prim_jit_compile,       1, 1);
#endif
    DEF("jit-never!",            prim_jit_never,         1, 1);
    DEF("jit-compiled?",         prim_jit_compiled_p,    1, 1);

    /* ── GC builtins ───────────────────────────────────────────────────────── */
    DEF("gc-collect!",      prim_gc_collect,    0, 0);
    /* gc-stats registered in builtins.c */
    DEF("gc-on-collection", prim_gc_on_coll,    1, 1);

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
