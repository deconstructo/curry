/*
 * sx_poly.c — Polynomial machinery for Phase 4c/4d
 *
 * Univariate: GCD, resultant, squarefree, factorisation, partial fractions.
 * Equation solving: linear, polynomial, linear systems.
 * Multivariate: Groebner basis (Buchberger, lex ordering).
 *
 * Internal representation: C arrays val_t c[0..n] where c[i] is the rational
 * coefficient of var^i.  All computation is over ℚ (exact).
 */

#include "sx_poly.h"
#include "symbolic.h"
#include "object.h"
#include "numeric.h"
#include "gc.h"
#include "builtins.h"   /* scm_cons */
#include <string.h>
#include <stdlib.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* ========================================================================== */
/* Internal helpers                                                            */
/* ========================================================================== */

static val_t Q(long n) { return vfix(n); }
static bool  qzero(val_t v) { return num_is_zero(v); }
static bool  qone(val_t v)  { return num_is_one(v); }

/* ========================================================================== */
/* PART 1: Dense coefficient arrays                                            */
/* ========================================================================== */

/* Allocate a zeroed coefficient array of length n+1 (degree n). */
static val_t *poly_alloc(int n) {
    val_t *c = (val_t *)gc_alloc_raw_pinned((size_t)(n + 1) * sizeof(val_t));
    for (int i = 0; i <= n; i++) c[i] = Q(0);
    return c;
}

/* Return degree of coefficient array (highest non-zero index), -1 for zero. */
static int poly_deg(val_t *c, int len) {
    for (int i = len - 1; i >= 0; i--)
        if (!qzero(c[i])) return i;
    return -1;
}

/* content(p) = gcd of all coefficients (as an integer) */
static val_t poly_content(val_t *c, int n) {
    val_t g = Q(0);
    for (int i = 0; i <= n; i++) {
        if (!qzero(c[i])) g = num_gcd(g, num_abs(c[i]));
    }
    return qzero(g) ? Q(1) : g;
}

/* Make primitive: divide all coefficients by their gcd */
static void poly_primitive(val_t *c, int n) {
    val_t g = poly_content(c, n);
    if (qone(g)) return;
    for (int i = 0; i <= n; i++) c[i] = num_div(c[i], g);
}

/* Make monic: divide all coefficients by leading coefficient */
static void poly_monic(val_t *c, int n) {
    int d = poly_deg(c, n + 1);
    if (d < 0) return;
    val_t lc = c[d];
    if (qone(lc)) return;
    for (int i = 0; i <= d; i++) c[i] = num_div(c[i], lc);
}

/* Convert symbolic expression to dense coefficient array in var.
   Returns NULL if expr is not a polynomial in var.
   *out_deg is set to the degree. */
static val_t *expr_to_poly(val_t expr, val_t var, int *out_deg) {
    /* Simplify first to normalise SUB/NEG into ADD form */
    val_t ex = sx_simplify(sx_expand(expr));

    /* Collect additive terms */
    int nterms;
    val_t *terms;
    val_t single[1];
    if (vis_symexpr(ex) && as_symexpr(ex)->op == SX_ADD) {
        nterms = (int)as_symexpr(ex)->nargs;
        terms  = as_symexpr(ex)->args;
    } else {
        single[0] = ex; nterms = 1; terms = single;
    }

    /* First pass: find degree */
    long maxdeg = 0;
    for (int i = 0; i < nterms; i++) {
        val_t t = terms[i];
        /* quick degree estimate */
        if (vis_number(t)) continue;
        if (vis_symvar(t)) {
            if (sx_equal(t, var) && maxdeg < 1) maxdeg = 1;
            continue;
        }
        if (!vis_symexpr(t)) return NULL;
        SymExpr *se = as_symexpr(t);
        if (se->op == SX_EXPT && (int)se->nargs == 2 &&
            sx_equal(se->args[0], var) && vis_fixnum(se->args[1])) {
            long e = vunfix(se->args[1]);
            if (e > maxdeg) maxdeg = e;
        } else if (se->op == SX_MUL || se->op == SX_NEG) {
            long d = sx_degree_long(t, var);
            if (d > maxdeg) maxdeg = d;
        }
        /* If term contains var in a non-polynomial way, fail */
        if (sx_depends_on(t, var) && sx_degree_long(t, var) == 0)
            return NULL; /* e.g. sin(x) */
    }

    val_t *c = poly_alloc((int)maxdeg);

    /* Second pass: fill coefficients */
    for (int i = 0; i < nterms; i++) {
        val_t t = terms[i];
        long deg = 0; val_t coeff = Q(0);

        if (vis_number(t)) { coeff = t; deg = 0; }
        else if (vis_symvar(t)) {
            if (sx_equal(t, var)) { coeff = Q(1); deg = 1; }
            else if (!sx_depends_on(t, var)) { coeff = t; deg = 0; }
            else return NULL;
        } else if (vis_symexpr(t)) {
            SymExpr *se = as_symexpr(t);
            if (se->op == SX_NEG && (int)se->nargs == 1) {
                /* recurse: negate the inner term */
                val_t inner = se->args[0];
                long ideg; val_t ic = Q(0);
                if (vis_number(inner)) { ic = inner; ideg = 0; }
                else if (vis_symvar(inner) && sx_equal(inner, var)) { ic = Q(1); ideg = 1; }
                else if (vis_symexpr(inner)) {
                    SymExpr *ise = as_symexpr(inner);
                    if (ise->op == SX_EXPT && (int)ise->nargs == 2 &&
                        sx_equal(ise->args[0], var) && vis_fixnum(ise->args[1])) {
                        ic = Q(1); ideg = vunfix(ise->args[1]);
                    } else goto mul_path;
                } else goto mul_path;
                coeff = num_neg(ic); deg = ideg;
                goto add_coeff;
            }
            mul_path:
            if (se->op == SX_EXPT && (int)se->nargs == 2 &&
                sx_equal(se->args[0], var) && vis_fixnum(se->args[1])) {
                coeff = Q(1); deg = vunfix(se->args[1]);
            } else if (se->op == SX_MUL) {
                /* split: numeric factors × power-of-var factor */
                val_t num_part = Q(1);
                long pow_deg = 0;
                bool ok = true;
                for (int j = 0; j < (int)se->nargs && ok; j++) {
                    val_t f = se->args[j];
                    if (vis_number(f)) {
                        num_part = num_mul(num_part, f);
                    } else if (vis_symvar(f) && sx_equal(f, var)) {
                        pow_deg += 1;
                    } else if (vis_symexpr(f) &&
                               as_symexpr(f)->op == SX_EXPT &&
                               (int)as_symexpr(f)->nargs == 2 &&
                               sx_equal(as_symexpr(f)->args[0], var) &&
                               vis_fixnum(as_symexpr(f)->args[1])) {
                        pow_deg += vunfix(as_symexpr(f)->args[1]);
                    } else if (!sx_depends_on(f, var)) {
                        /* symbolic coefficient part — treat as non-poly if not pure number */
                        return NULL;
                    } else { ok = false; }
                }
                if (!ok) return NULL;
                coeff = num_part; deg = pow_deg;
            } else if (!sx_depends_on(t, var)) {
                coeff = t; deg = 0;
            } else {
                return NULL; /* non-polynomial term */
            }
        } else return NULL;

        add_coeff:
        if (deg > maxdeg) return NULL;
        c[deg] = num_add(c[deg], coeff);
    }

    *out_deg = poly_deg(c, (int)maxdeg + 1);
    return c;
}

/* Convert coefficient array back to SymExpr */
static val_t poly_to_expr(val_t *c, int n, val_t var) {
    int d = poly_deg(c, n + 1);
    if (d < 0) return Q(0);

    val_t terms[1024]; /* sufficient for reasonable degrees */
    int ri = 0;
    for (int i = d; i >= 0 && ri < 1024; i--) {
        if (qzero(c[i])) continue;
        val_t t;
        if (i == 0)      t = c[i];
        else if (i == 1) t = qone(c[i]) ? var : sx_mul(c[i], var);
        else {
            val_t vp = sx_expt(var, Q(i));
            t = qone(c[i]) ? vp : sx_mul(c[i], vp);
        }
        terms[ri++] = t;
    }
    if (ri == 0) return Q(0);
    if (ri == 1) return sx_simplify(terms[0]);
    return sx_simplify(sx_make_expr(SX_ADD, ri, terms));
}

/* ========================================================================== */
/* PART 2: Polynomial pseudo-division                                          */
/* ========================================================================== */

/*
 * Pseudo-division: compute pseudo-quotient Q and pseudo-remainder R such that
 *   lc(b)^(deg(a)-deg(b)+1) * a = Q*b + R
 * where lc(b) is the leading coefficient of b.
 * Used to avoid rational arithmetic blow-up in GCD computation.
 */
static void poly_pseudo_div(val_t *a, int da, val_t *b, int db,
                            val_t **qout, int *dq_out,
                            val_t **rout, int *dr_out) {
    int dq = da - db;
    val_t *q = poly_alloc(dq < 0 ? 0 : dq);
    val_t *r = poly_alloc(da);
    for (int i = 0; i <= da; i++) r[i] = a[i];

    val_t lcb = b[db];
    int dr = da;

    while (dr >= db) {
        val_t lcr = r[dr];
        int e = dr - db;
        /* q[e] += lcr / lcb  (pseudo: multiply through by lcb) */
        /* Actual pseudo: q[e] = lcr; scale everything by lcb */
        q[e] = lcr;
        for (int i = 0; i <= dr; i++) r[i] = num_mul(r[i], lcb);
        for (int j = 0; j <= db; j++) r[e + j] = num_sub(r[e + j], num_mul(lcr, b[j]));
        /* find new dr */
        int new_dr = dr - 1;
        while (new_dr >= 0 && qzero(r[new_dr])) new_dr--;
        dr = new_dr;
    }
    *qout = q; *dq_out = dq < 0 ? -1 : poly_deg(q, dq + 1);
    *rout = r; *dr_out = dr;
}

/* ========================================================================== */
/* PART 3: Polynomial GCD — subresultant PRS                                  */
/* ========================================================================== */

/* Negate all coefficients */
static void poly_negate(val_t *c, int n) {
    for (int i = 0; i <= n; i++) c[i] = num_neg(c[i]);
}

/* Copy coefficient array */
static val_t *poly_copy(val_t *c, int n) {
    val_t *out = poly_alloc(n);
    for (int i = 0; i <= n; i++) out[i] = c[i];
    return out;
}

val_t sx_poly_gcd(val_t p, val_t q, val_t var) {
    int dp, dq;
    val_t *cp = expr_to_poly(p, var, &dp);
    val_t *cq = expr_to_poly(q, var, &dq);
    if (!cp || !cq) scm_raise(V_FALSE, "poly-gcd: arguments must be polynomials");

    /* Trivial cases */
    if (dp < 0) return q;  /* gcd(0, q) = q */
    if (dq < 0) return p;  /* gcd(p, 0) = p */

    /* Ensure deg(a) >= deg(b) */
    val_t *a = dp >= dq ? poly_copy(cp, dp) : poly_copy(cq, dq);
    int    da = dp >= dq ? dp : dq;
    val_t *b = dp >= dq ? poly_copy(cq, dq) : poly_copy(cp, dp);
    int    db = dp >= dq ? dq : dp;

    /* Subresultant PRS */
    val_t beta = Q(db % 2 == 0 ? 1 : -1);
    val_t psi  = Q(-1);

    while (db >= 0) {
        val_t *rem; int dr;
        val_t *quot; int dqt;
        poly_pseudo_div(a, da, b, db, &quot, &dqt, &rem, &dr);
        (void)quot; (void)dqt;

        if (dr < 0) break; /* b divides a — b is gcd */

        /* Scale rem by beta */
        for (int i = 0; i <= dr; i++) rem[i] = num_div(rem[i], beta);

        int delta = da - db;
        /* Update psi and beta for next iteration */
        val_t lc_a = a[da];
        val_t new_psi;
        if (delta == 1) {
            new_psi = lc_a;
        } else {
            val_t psi_pow = num_expt(num_neg(psi), Q(delta - 1));
            new_psi = num_div(num_expt(lc_a, Q(delta)), psi_pow);
        }
        val_t new_beta = num_neg(num_mul(lc_a, num_expt(new_psi, Q(db))));

        a = b; da = db;
        b = rem; db = dr;
        psi = new_psi;
        beta = qzero(new_beta) ? Q(1) : new_beta;
    }

    /* b is the GCD (up to scalar) */
    if (db < 0) {
        /* GCD is a constant */
        return Q(1);
    }

    /* Make primitive and monic */
    poly_primitive(b, db);
    if (!qzero(b[db]) && vis_fixnum(b[db]) && vunfix(b[db]) < 0)
        poly_negate(b, db);
    poly_monic(b, db);

    return poly_to_expr(b, db, var);
}

/* ========================================================================== */
/* PART 4: Resultant                                                           */
/* ========================================================================== */

val_t sx_poly_resultant(val_t p, val_t q, val_t var) {
    int dp, dq;
    val_t *cp = expr_to_poly(p, var, &dp);
    val_t *cq = expr_to_poly(q, var, &dq);
    if (!cp || !cq) scm_raise(V_FALSE, "poly-resultant: arguments must be polynomials");
    if (dp < 0 || dq < 0) return Q(0);

    /* res(p,q) = (-1)^(dp*dq) * lc(q)^(dp-dq) * res(q, p mod q) for dp > dq */
    /* Use subresultant PRS to compute resultant */
    val_t *a = poly_copy(cp, dp); int da = dp;
    val_t *b = poly_copy(cq, dq); int db = dq;

    val_t res = Q(1);
    int sign = 1;

    while (db >= 0) {
        if (da < db) {
            /* swap */
            val_t *tmp = a; a = b; b = tmp;
            int td = da; da = db; db = td;
            if (da % 2 == 1 && db % 2 == 1) sign = -sign;
        }

        val_t *rem; int dr;
        val_t *quot; int dqt;
        poly_pseudo_div(a, da, b, db, &quot, &dqt, &rem, &dr);
        (void)quot; (void)dqt;

        res = num_mul(res, num_expt(b[db], Q(da - db)));
        if ((da % 2 == 1) && (db % 2 == 1)) sign = -sign;

        a = b; da = db;
        b = rem; db = dr;
    }
    if (da == 0) res = num_mul(res, num_expt(a[0], Q(db + 1)));
    /* (db is -1 here, so exponent is 0, so result is just res) */
    val_t out = sign < 0 ? num_neg(res) : res;
    return out;
}

/* ========================================================================== */
/* PART 5: Squarefree factorisation (Yun's algorithm)                         */
/* ========================================================================== */

/* Exact polynomial quotient: returns p/q (no remainder expected).
   If there IS a remainder the result is approximate — callers guarantee
   exact divisibility. */
static val_t poly_exact_div(val_t p, val_t q, val_t var) {
    int dp, dq;
    val_t *cp = expr_to_poly(p, var, &dp);
    val_t *cq = expr_to_poly(q, var, &dq);
    if (!cp || !cq || dq < 0) return sx_div(p, q); /* fall back */
    if (dp < dq) return Q(0);
    val_t *rem; int dr;
    val_t *quot; int dqt;
    poly_pseudo_div(cp, dp, cq, dq, &quot, &dqt, &rem, &dr);
    /* Divide out any scalar introduced by pseudo-division */
    val_t content = poly_content(quot, dqt < 0 ? 0 : dqt);
    if (!qone(content))
        for (int i = 0; i <= dqt; i++) quot[i] = num_div(quot[i], content);
    return poly_to_expr(quot, dqt < 0 ? 0 : dqt, var);
}

/*
 * Yun's algorithm: given p(x), compute squarefree decomposition
 *   p = a_1 * a_2^2 * a_3^3 * ...
 * where each a_i is squarefree.
 * Returns Scheme list of (factor . multiplicity) pairs.
 */
val_t sx_poly_squarefree(val_t p, val_t var) {
    /* Expand to ensure we have a clean polynomial */
    val_t p_ex = sx_simplify(sx_expand(p));
    val_t dp   = sx_simplify(sx_expand(sx_diff(p_ex, var)));

    val_t result = V_NIL;

    /* g = gcd(p, p'),  a = p/g = squarefree radical */
    val_t g = sx_poly_gcd(p_ex, dp, var);
    val_t a = poly_exact_div(p_ex, g, var);

    int k = 1;
    while (k < 64) {
        /* Check if a is trivial */
        val_t a_s = sx_simplify(a);
        if (num_is_one(a_s) || (vis_fixnum(a_s) && (vunfix(a_s) == 1 || vunfix(a_s) == -1)))
            break;

        val_t b = sx_poly_gcd(g, a, var);           /* b = gcd(g, a) */
        val_t f = poly_exact_div(a, b, var);         /* f = factors at multiplicity k */
        val_t f_s = sx_simplify(f);
        if (!num_is_one(f_s) && !(vis_fixnum(f_s) && (vunfix(f_s) == 1 || vunfix(f_s) == -1)))
            result = scm_cons(scm_cons(f_s, Q(k)), result);

        a = b;
        g = poly_exact_div(g, b, var);
        k++;
    }

    /* Reverse to get ascending multiplicity order */
    val_t rev = V_NIL;
    while (vis_pair(result)) { rev = scm_cons(vcar(result), rev); result = vcdr(result); }
    return rev;
}

/* ========================================================================== */
/* PART 6: Factorisation over ℚ (Kronecker's algorithm)                       */
/* ========================================================================== */

/*
 * Kronecker's algorithm for univariate factorisation over ℚ.
 * Given primitive polynomial p(x) of degree n, tries all possible factors
 * of degree 1..floor(n/2) by interpolation at n/2+1 evaluation points.
 *
 * This is correct but O(exp) in worst case; sufficient for small degrees.
 */

/* Evaluate p(x) at x = v */
static val_t poly_eval_at(val_t *c, int n, val_t v) {
    /* Horner's method */
    val_t acc = Q(0);
    for (int i = n; i >= 0; i--)
        acc = num_add(num_mul(acc, v), c[i]);
    return acc;
}

/* Given a list of (x_i, y_i) pairs, find a polynomial of degree d passing
   through all of them using Lagrange interpolation.
   Returns coefficient array (pinned) or NULL if denominators are zero. */
static val_t *lagrange_interp(val_t *xs, val_t *ys, int npts, int *out_deg) {
    /* result as rational coefficients */
    val_t *coeff = poly_alloc(npts - 1);

    for (int i = 0; i < npts; i++) {
        /* L_i(x) = product_{j!=i} (x - x_j) / (x_i - x_j) */
        /* Compute L_i as array of coefficients */
        val_t *li = poly_alloc(npts - 1);
        li[0] = Q(1);
        int li_deg = 0;
        val_t denom = Q(1);
        for (int j = 0; j < npts; j++) {
            if (j == i) continue;
            /* multiply li by (x - x_j) */
            for (int k = li_deg; k >= 0; k--) {
                li[k + 1] = li[k];
                li[k] = num_neg(num_mul(li[k], xs[j]));
                if (k + 1 > li_deg + 1) li_deg = k + 1;
            }
            if (li_deg < 1) { li_deg = 1; li[1] = li[0]; li[0] = num_neg(xs[j]); }
            else li_deg++;
            denom = num_mul(denom, num_sub(xs[i], xs[j]));
        }
        if (qzero(denom)) { return NULL; }
        val_t yi_over_denom = num_div(ys[i], denom);
        for (int k = 0; k <= li_deg; k++)
            coeff[k] = num_add(coeff[k], num_mul(li[k], yi_over_denom));
    }
    *out_deg = poly_deg(coeff, npts);
    return coeff;
}

/* Integer divisors of n (positive) as a val_t array */
static val_t *int_divisors(long n, int *count) {
    n = n < 0 ? -n : n;
    if (n == 0) { *count = 0; return NULL; }
    /* Count first */
    int cnt = 0;
    for (long i = 1; i * i <= n; i++)
        if (n % i == 0) cnt += (i * i == n) ? 1 : 2;
    val_t *divs = (val_t *)gc_alloc_raw_pinned((size_t)(2 * cnt) * sizeof(val_t));
    int k = 0;
    for (long i = 1; i * i <= n; i++) {
        if (n % i == 0) {
            divs[k++] = Q(i); divs[k++] = Q(-i);
            if (i * i != n) { divs[k++] = Q(n/i); divs[k++] = Q(-(n/i)); }
        }
    }
    *count = k;
    return divs;
}

/*
 * Try to find a factor of p (coefficient array, degree dp) of degree fd
 * by Kronecker interpolation.  Returns factor expr or Q(1) if not found.
 */
static val_t kronecker_find_factor(val_t *p, int dp, val_t var, int fd) {
    int npts = fd + 1;
    val_t *xs = (val_t *)gc_alloc_raw_pinned((size_t)npts * sizeof(val_t));
    val_t *ys = (val_t *)gc_alloc_raw_pinned((size_t)npts * sizeof(val_t));

    /* Choose evaluation points 0, 1, -1, 2, -2, ... */
    for (int i = 0; i < npts; i++) {
        xs[i] = (i == 0) ? Q(0) : (i % 2 == 1) ? Q((i+1)/2) : Q(-(i/2));
        ys[i] = poly_eval_at(p, dp, xs[i]);
    }

    /* Build sets of candidate y-values: divisors of each y_i */
    /* For each combination of divisors (one per point), try Lagrange interpolation */
    int *div_counts = (int *)gc_alloc_raw_pinned_atomic((size_t)npts * sizeof(int));
    val_t **divisors = (val_t **)gc_alloc_raw_pinned((size_t)npts * sizeof(val_t *));
    for (int i = 0; i < npts; i++) {
        if (!vis_fixnum(ys[i]) && !(vis_number(ys[i]))) {
            return Q(1); /* non-integer value — can't do Kronecker */
        }
        long yv = vis_fixnum(ys[i]) ? vunfix(ys[i]) : 0;
        if (yv == 0) {
            /* xs[i] is a root of p — (x - xs[i]) is a factor */
            /* Check if it divides p */
            val_t linear[2]; linear[0] = num_neg(xs[i]); linear[1] = Q(1);
            val_t *rem; int dr;
            val_t *quot; int dqt;
            val_t *p2 = poly_copy(p, dp);
            poly_pseudo_div(p2, dp, linear, 1, &quot, &dqt, &rem, &dr);
            if (dr < 0 || qzero(rem[0])) {
                /* (x - xs[i]) is a factor */
                val_t factor_expr = sx_sub(var, xs[i]);
                return sx_simplify(factor_expr);
            }
        }
        divisors[i] = int_divisors(yv == 0 ? 1 : yv, &div_counts[i]);
        if (div_counts[i] == 0) return Q(1);
    }

    /* Try all combinations (limited to avoid combinatorial explosion) */
    int *idx = (int *)gc_alloc_raw_pinned_atomic((size_t)npts * sizeof(int));
    memset(idx, 0, (size_t)npts * sizeof(int));

    int max_tries = 1;
    for (int i = 0; i < npts; i++) max_tries *= div_counts[i];
    if (max_tries > 2048) max_tries = 2048; /* safety limit */

    for (int attempt = 0; attempt < max_tries; attempt++) {
        val_t *cand_ys = (val_t *)gc_alloc_raw_pinned((size_t)npts * sizeof(val_t));
        for (int i = 0; i < npts; i++) cand_ys[i] = divisors[i][idx[i]];

        int cd;
        val_t *cand = lagrange_interp(xs, cand_ys, npts, &cd);
        if (cand && cd == fd) {
            /* Check if coefficients are integers and cand divides p */
            bool integer_coeffs = true;
            for (int i = 0; i <= cd; i++) {
                if (!vis_fixnum(cand[i])) { integer_coeffs = false; break; }
            }
            if (integer_coeffs && !qzero(cand[cd])) {
                /* Trial division */
                val_t *rem; int dr;
                val_t *quot; int dqt;
                val_t *p2 = poly_copy(p, dp);
                poly_pseudo_div(p2, dp, cand, cd, &quot, &dqt, &rem, &dr);
                if (dr < 0) {
                    return poly_to_expr(cand, cd, var);
                }
            }
        }

        /* Increment multi-index */
        for (int i = npts - 1; i >= 0; i--) {
            idx[i]++;
            if (idx[i] < div_counts[i]) break;
            idx[i] = 0;
        }
    }
    return Q(1); /* no factor found */
}

val_t sx_poly_factor(val_t p, val_t var) {
    int dp;
    val_t *cp = expr_to_poly(p, var, &dp);
    if (!cp) scm_raise(V_FALSE, "poly-factor: argument must be a polynomial");
    if (dp < 0) return scm_cons(scm_cons(Q(0), Q(1)), V_NIL);

    /* Extract content (rational part) */
    val_t cont = poly_content(cp, dp);
    poly_primitive(cp, dp);

    val_t result = V_NIL;
    if (!qone(cont)) result = scm_cons(scm_cons(cont, Q(1)), result);

    /* Squarefree decomposition first */
    val_t sqfree = sx_poly_squarefree(poly_to_expr(cp, dp, var), var);

    while (vis_pair(sqfree)) {
        val_t kv   = vcar(sqfree); sqfree = vcdr(sqfree);
        val_t factor = vcar(kv);
        val_t mult   = vcdr(kv);

        /* Factor the squarefree component */
        int dfi; val_t *cf = expr_to_poly(factor, var, &dfi);
        if (!cf || dfi <= 1) {
            /* Irreducible (degree 0 or 1) */
            result = scm_cons(scm_cons(factor, mult), result);
            continue;
        }

        /* Try Kronecker for degrees 1 .. floor(dfi/2) */
        val_t remaining = factor;
        int drem = dfi;
        val_t *crem = cf;
        bool found_any = false;

        for (int trial_deg = 1; trial_deg <= drem / 2; trial_deg++) {
            val_t f = kronecker_find_factor(crem, drem, var, trial_deg);
            if (!vis_fixnum(f) || vunfix(f) != 1) {
                /* Found a factor */
                int df; val_t *cf2 = expr_to_poly(f, var, &df);
                (void)df;
                /* Divide out completely */
                while (true) {
                    val_t *rem; int dr; val_t *quot; int dqt;
                    val_t *crem2 = poly_copy(crem, drem);
                    if (!cf2) break;
                    poly_pseudo_div(crem2, drem, cf2, df, &quot, &dqt, &rem, &dr);
                    if (dr >= 0) break;
                    result = scm_cons(scm_cons(sx_simplify(f), mult), result);
                    /* quot is new remainder */
                    crem = quot; drem = dqt;
                    poly_monic(crem, drem);
                    found_any = true;
                    trial_deg = 0; /* restart search with reduced polynomial */
                    break;
                }
            }
        }

        /* Whatever remains is irreducible */
        val_t rem_expr = poly_to_expr(crem, drem, var);
        val_t rs = sx_simplify(rem_expr);
        if (!num_is_one(rs))
            result = scm_cons(scm_cons(rs, mult), result);
        (void)found_any;
    }

    /* Reverse */
    val_t rev = V_NIL;
    while (vis_pair(result)) { rev = scm_cons(vcar(result), rev); result = vcdr(result); }
    return rev;
}

/* ========================================================================== */
/* PART 7: Partial fractions                                                   */
/* ========================================================================== */

/*
 * Extended Euclidean algorithm for polynomials.
 * Finds s, t such that s*a + t*b = gcd(a,b).
 * Returns gcd; sets *s_out, *t_out.
 */
static val_t poly_extended_gcd(val_t *a, int da, val_t *b, int db,
                               val_t var,
                               val_t **s_out, int *ds_out,
                               val_t **t_out, int *dt_out) {
    if (da < 0) {
        /* gcd(0, b) = b; s=0, t=1 */
        *s_out = poly_alloc(0); (*s_out)[0] = Q(0); *ds_out = -1;
        *t_out = poly_alloc(0); (*t_out)[0] = Q(1); *dt_out = 0;
        return poly_to_expr(b, db, var);
    }
    if (db < 0) {
        *s_out = poly_alloc(0); (*s_out)[0] = Q(1); *ds_out = 0;
        *t_out = poly_alloc(0); (*t_out)[0] = Q(0); *dt_out = -1;
        return poly_to_expr(a, da, var);
    }

    /* Extended Euclidean via matrix method */
    val_t *old_r = poly_copy(a, da); int dor = da;
    val_t *r     = poly_copy(b, db); int dr  = db;
    val_t *old_s = poly_alloc(0); old_s[0] = Q(1); int dос = 0;
    val_t *s     = poly_alloc(0); s[0]     = Q(0); int ds  = -1;
    val_t *old_t = poly_alloc(0); old_t[0] = Q(0); int dot = -1;
    val_t *t     = poly_alloc(0); t[0]     = Q(1); int dt  = 0;

    while (dr >= 0) {
        val_t *quot; int dq;
        val_t *rem;  int dr2;
        poly_pseudo_div(old_r, dor, r, dr, &quot, &dq, &rem, &dr2);

        /* new_s = old_s - q*s; new_t = old_t - q*t */
        /* (simplified: use expr arithmetic for the Bezout coefficients) */
        val_t q_expr = poly_to_expr(quot, dq, var);
        val_t s_expr = poly_to_expr(s, ds < 0 ? 0 : ds, var);
        val_t t_expr = poly_to_expr(t, dt < 0 ? 0 : dt, var);
        val_t os_expr= poly_to_expr(old_s, dос < 0 ? 0 : dос, var);
        val_t ot_expr= poly_to_expr(old_t, dot < 0 ? 0 : dot, var);

        val_t new_s_expr = sx_simplify(sx_sub(os_expr, sx_mul(q_expr, s_expr)));
        val_t new_t_expr = sx_simplify(sx_sub(ot_expr, sx_mul(q_expr, t_expr)));

        val_t *new_s = expr_to_poly(new_s_expr, var, &dос);
        val_t *new_t = expr_to_poly(new_t_expr, var, &dot);
        if (!new_s || !new_t) break; /* shouldn't happen for poly inputs */

        old_r = r;  dor = dr;
        r     = rem; dr  = dr2;
        old_s = s;  /* (reuse dос) */
        s     = new_s; /* dос already set */
        old_t = t;
        t     = new_t; /* dot already set */
        (void)ds;
        ds = dос; dot = dot; /* silence warnings */
    }

    *s_out = old_s; *ds_out = dос;
    *t_out = old_t; *dt_out = dot;
    return poly_to_expr(old_r, dor, var);
}

val_t sx_partial_fractions(val_t num_expr, val_t den_expr, val_t var) {
    /* Polynomial part: if deg(N) >= deg(D), divide out first */
    int dn, dd;
    val_t *cn = expr_to_poly(num_expr, var, &dn);
    val_t *cd = expr_to_poly(den_expr, var, &dd);
    if (!cn || !cd) return sx_div(num_expr, den_expr);

    val_t poly_part = Q(0);
    val_t *rem_n = cn; int drn = dn;
    if (dn >= dd) {
        val_t *quot; int dq; val_t *rem2; int dr2;
        poly_pseudo_div(cn, dn, cd, dd, &quot, &dq, &rem2, &dr2);
        poly_part = poly_to_expr(quot, dq, var);
        rem_n = rem2; drn = dr2;
    }
    if (drn < 0) return sx_simplify(poly_part);
    val_t N = poly_to_expr(rem_n, drn, var);

    /* Factorise denominator */
    val_t factors = sx_poly_factor(den_expr, var);
    if (!vis_pair(factors)) return sx_add(poly_part, sx_div(N, den_expr));

    /*
     * For each irreducible factor fᵢ of multiplicity mᵢ:
     *   N/D = Σᵢ Σₖ₌₁ᵐⁱ Aᵢₖ / fᵢᵏ
     *
     * For simple factors (deg=1, mult=1), use the residue formula:
     *   A = N(root) / D'(root)
     * where D' = d(den)/d(var) and root satisfies f(root)=0.
     *
     * For linear factors f = a*var + b, root = -b/a.
     * For quadratic/higher or repeated factors, fall back to Bezout.
     */
    val_t D_prime = sx_simplify(sx_expand(sx_diff(den_expr, var)));
    val_t result  = poly_part;

    val_t flist = factors;
    while (vis_pair(flist)) {
        val_t kv   = vcar(flist); flist = vcdr(flist);
        val_t f    = vcar(kv);
        val_t mult = vcdr(kv);
        if (!vis_fixnum(mult)) continue;
        long m = vunfix(mult);

        /* Get factor degree */
        int df; val_t *cf = expr_to_poly(f, var, &df);
        if (!cf) { result = sx_add(result, sx_div(N, sx_expt(f, Q(m)))); continue; }

        if (df == 1 && m == 1) {
            /* Simple linear factor: residue formula.
               Root r = -c[0]/c[1] */
            val_t r = sx_simplify(num_neg(num_div(cf[0], cf[1])));
            val_t N_at_r  = sx_simplify(sx_substitute(N,       var, r));
            val_t Dp_at_r = sx_simplify(sx_substitute(D_prime, var, r));
            if (num_is_zero(Dp_at_r)) continue;
            val_t A = sx_simplify(num_div(N_at_r, Dp_at_r));
            if (!num_is_zero(A))
                result = sx_add(result, sx_simplify(sx_div(A, f)));
        } else if (df == 1 && m > 1) {
            /* Repeated linear factor: use repeated residues */
            val_t r = sx_simplify(num_neg(num_div(cf[0], cf[1])));
            val_t g = N; /* will divide out roots iteratively */
            for (long k = 1; k <= m; k++) {
                /* A_k = 1/(m-k)! * d^(m-k)/dvar^(m-k) [N*(f/lc(f))^m / D] at r */
                /* Simple version: A_k from g at r, where g = remainder */
                val_t g_at_r = sx_simplify(sx_substitute(g, var, r));
                /* Compute denominator: D / f^k evaluated at r, then A_k = g(r)/that */
                val_t fk = sx_expt(f, Q(k));
                val_t comp = poly_exact_div(den_expr, fk, var);
                val_t comp_at_r = sx_simplify(sx_substitute(comp, var, r));
                if (!num_is_zero(comp_at_r)) {
                    val_t Ak = sx_simplify(num_div(g_at_r, comp_at_r));
                    if (!num_is_zero(Ak)) {
                        result = sx_add(result, sx_div(Ak, sx_simplify(fk)));
                        g = sx_simplify(sx_expand(sx_sub(g, sx_mul(Ak, comp))));
                    }
                }
            }
        } else {
            /* Irreducible higher-degree factor (e.g. x²+1).
               Find comp = den / f^m (complementary denominator).
               The coefficient A satisfies: A·comp + B·f^m = N for some B.
               If comp = 1 (f^m = den), then A = N directly (reduce mod f^m). */
            val_t fk = sx_expt(f, Q(m));
            val_t comp = poly_exact_div(den_expr, sx_simplify(fk), var);
            int dcomp; val_t *ccomp = expr_to_poly(comp, var, &dcomp);

            val_t A;
            if (!ccomp || dcomp == 0) {
                /* comp is a constant — A = N / comp (mod f^m) */
                val_t comp_val = (!ccomp || dcomp < 0) ? Q(1) : ccomp[0];
                A = poly_to_expr(cn, dn, var); /* A = N */
                if (!qone(comp_val)) {
                    int da; val_t *ca = expr_to_poly(
                        sx_simplify(num_div(A, comp_val)), var, &da);
                    A = ca ? poly_to_expr(ca, da, var) : A;
                }
            } else {
                /* General case: use extended GCD */
                val_t *s_b; int ds; val_t *t_b; int dt;
                poly_extended_gcd(cf, df, ccomp, dcomp, var,
                                  &s_b, &ds, &t_b, &dt);
                A = sx_simplify(sx_mul(
                    poly_to_expr(t_b, dt < 0 ? 0 : dt, var), N));
            }
            /* Reduce A modulo f */
            A = sx_simplify(A);
            int da; val_t *ca = expr_to_poly(A, var, &da);
            if (ca && da >= df) {
                val_t *qr; int dqr; val_t *rr; int drr;
                poly_pseudo_div(ca, da, cf, df, &qr, &dqr, &rr, &drr);
                A = poly_to_expr(rr, drr < 0 ? 0 : drr, var);
                A = sx_simplify(A);
            }
            if (!num_is_zero(A))
                result = sx_add(result, sx_div(A, sx_simplify(fk)));
        }
    }

    return sx_simplify(result);
}

/* ========================================================================== */
/* PART 8: Equation solving                                                    */
/* ========================================================================== */

val_t sx_solve(val_t expr, val_t var) {
    /* Normalise: expand and collect */
    val_t p = sx_simplify(sx_expand(expr));
    int dp;
    val_t *cp = expr_to_poly(p, var, &dp);

    if (!cp) {
        /* Not a polynomial — return unevaluated (future: trig/exp solvers) */
        return V_FALSE;
    }

    if (dp < 0) return V_NIL; /* 0 = 0 — infinite solutions; return empty */

    if (dp == 0) {
        /* Non-zero constant = 0 — no solution */
        return V_NIL;
    }

    if (dp == 1) {
        /* Linear: c[1]*x + c[0] = 0 → x = -c[0]/c[1] */
        val_t sol = sx_simplify(num_neg(num_div(cp[0], cp[1])));
        return scm_cons(sol, V_NIL);
    }

    if (dp == 2) {
        /* Quadratic: c[2]*x² + c[1]*x + c[0] = 0 */
        val_t a2 = cp[2], b2 = cp[1], c2 = cp[0];
        val_t disc = sx_simplify(sx_sub(sx_mul(b2, b2),
                                        sx_mul(Q(4), sx_mul(a2, c2))));
        if (vis_number(disc)) {
            /* Exact discriminant */
            if (num_is_zero(disc)) {
                val_t sol = sx_simplify(num_neg(num_div(b2, num_mul(Q(2), a2))));
                return scm_cons(sol, V_NIL);
            }
            val_t sqrt_disc = sx_simplify(sx_sqrt(disc));
            val_t denom = num_mul(Q(2), a2);
            val_t s1 = sx_simplify(num_div(sx_add(num_neg(b2), sqrt_disc), denom));
            val_t s2 = sx_simplify(num_div(sx_sub(num_neg(b2), sqrt_disc), denom));
            return scm_cons(s1, scm_cons(s2, V_NIL));
        }
        /* Symbolic discriminant */
        val_t sqrt_disc = sx_sqrt(disc);
        val_t denom = sx_mul(Q(2), a2);
        val_t s1 = sx_simplify(sx_div(sx_add(sx_neg(b2), sqrt_disc), denom));
        val_t s2 = sx_simplify(sx_div(sx_sub(sx_neg(b2), sqrt_disc), denom));
        return scm_cons(s1, scm_cons(s2, V_NIL));
    }

    /* Degree >= 3: factorise and solve each factor */
    val_t factors = sx_poly_factor(p, var);
    val_t solutions = V_NIL;

    while (vis_pair(factors)) {
        val_t kv = vcar(factors); factors = vcdr(factors);
        val_t f  = vcar(kv);
        /* Recursively solve f = 0 */
        val_t fsols = sx_solve(f, var);
        if (vis_pair(fsols) || vis_nil(fsols)) {
            val_t scan = fsols;
            while (vis_pair(scan)) {
                solutions = scm_cons(vcar(scan), solutions);
                scan = vcdr(scan);
            }
        }
    }

    return solutions;
}

/* ========================================================================== */
/* PART 9: Linear system solver (Gaussian elimination)                         */
/* ========================================================================== */

val_t sx_solve_system(val_t eqs, val_t vars) {
    /* Count equations and variables */
    int m = 0, n = 0;
    val_t tmp = eqs;   while (vis_pair(tmp)) { m++; tmp = vcdr(tmp); }
    tmp = vars; while (vis_pair(tmp)) { n++; tmp = vcdr(tmp); }
    if (m == 0 || n == 0) return V_FALSE;

    /* Extract variable list */
    val_t var_arr[64];
    if (n > 64) return V_FALSE;
    tmp = vars;
    for (int i = 0; i < n; i++) { var_arr[i] = vcar(tmp); tmp = vcdr(tmp); }

    /* Build augmented matrix [A | b] from linear equations (expr = 0) */
    /* Matrix rows: m equations, n+1 columns (n coefficients + RHS) */
    val_t **mat = (val_t **)gc_alloc_raw_pinned((size_t)m * sizeof(val_t *));
    for (int i = 0; i < m; i++) {
        mat[i] = (val_t *)gc_alloc_raw_pinned((size_t)(n + 1) * sizeof(val_t));
        for (int j = 0; j <= n; j++) mat[i][j] = Q(0);
    }

    tmp = eqs;
    for (int i = 0; i < m; i++) {
        val_t eq = vcar(tmp); tmp = vcdr(tmp);
        val_t ex = sx_simplify(sx_expand(eq));
        /* Extract coefficients via partial differentiation (exact for linear) */
        for (int j = 0; j < n; j++) {
            val_t c_expr = sx_simplify(sx_diff(ex, var_arr[j]));
            if (!vis_number(c_expr)) return V_FALSE; /* non-linear */
            mat[i][j] = c_expr;
        }
        /* RHS = -(constant term) = -eq evaluated at all vars = 0 */
        val_t zero_sub = ex;
        for (int j = 0; j < n; j++)
            zero_sub = sx_substitute(zero_sub, var_arr[j], Q(0));
        zero_sub = sx_simplify(zero_sub);
        if (!vis_number(zero_sub)) return V_FALSE;
        mat[i][n] = num_neg(zero_sub);
    }

    /* Gaussian elimination with partial pivoting */
    int pivot_row = 0;
    for (int col = 0; col < n && pivot_row < m; col++) {
        /* Find pivot */
        int best = -1;
        for (int row = pivot_row; row < m; row++) {
            if (!qzero(mat[row][col])) { best = row; break; }
        }
        if (best < 0) continue; /* free variable */

        /* Swap rows */
        if (best != pivot_row) {
            val_t *rtmp = mat[pivot_row]; mat[pivot_row] = mat[best]; mat[best] = rtmp;
        }

        /* Eliminate column */
        val_t piv = mat[pivot_row][col];
        for (int row = 0; row < m; row++) {
            if (row == pivot_row) continue;
            if (qzero(mat[row][col])) continue;
            val_t factor = num_div(mat[row][col], piv);
            for (int k = col; k <= n; k++)
                mat[row][k] = num_sub(mat[row][k], num_mul(factor, mat[pivot_row][k]));
        }
        pivot_row++;
    }

    /* Back-substitute: build solution alist */
    /* Check for inconsistency: row with all-zero coefficients but non-zero RHS */
    for (int i = pivot_row; i < m; i++) {
        if (!qzero(mat[i][n])) return V_FALSE; /* inconsistent */
    }

    val_t result = V_NIL;
    int solved = 0;
    for (int col = 0; col < n; col++) {
        /* Find the pivot row for this column */
        int pr = -1;
        for (int row = 0; row < pivot_row; row++) {
            if (!qzero(mat[row][col])) { pr = row; break; }
        }
        if (pr < 0) continue; /* free variable — skip */
        val_t sol = num_div(mat[pr][n], mat[pr][col]);
        sol = sx_simplify(sol);
        result = scm_cons(scm_cons(var_arr[col], sol), result);
        solved++;
    }

    if (solved != n) return V_FALSE; /* underdetermined */

    /* Reverse for natural order */
    val_t rev = V_NIL;
    while (vis_pair(result)) { rev = scm_cons(vcar(result), rev); result = vcdr(result); }
    return rev;
}

/* ========================================================================== */
/* PART 10: Groebner basis (Buchberger's algorithm, lex ordering)             */
/* ========================================================================== */

/* Monomial ordering: lex w.r.t. vars (first var = highest priority). */

/* Compare two expressions as monomials by lex order. */
static int mono_lex_cmp(val_t ma, val_t mb, val_t *vars, int nvars) {
    for (int i = 0; i < nvars; i++) {
        long da = sx_degree_long(ma, vars[i]);
        long db = sx_degree_long(mb, vars[i]);
        if (da != db) return (da > db) ? 1 : -1;
    }
    return 0;
}

/* Build monomial expr x₁^d₁ * x₂^d₂ * ... from degree vector. */
static val_t mono_from_degs(long *degs, val_t *vars, int nvars) {
    val_t m = Q(1);
    for (int i = 0; i < nvars; i++)
        if (degs[i] > 0) m = sx_simplify(sx_mul(m, sx_expt(vars[i], Q(degs[i]))));
    return m;
}

/* Extract (numerical coefficient, degree vector) from a single term. */
static val_t term_split(val_t t, val_t *vars, int nvars, long *degs) {
    for (int i = 0; i < nvars; i++) degs[i] = sx_degree_long(t, vars[i]);
    if (vis_number(t)) return t;
    if (vis_symvar(t)) return Q(1);
    if (!vis_symexpr(t)) return Q(1);
    SymExpr *se = as_symexpr(t);
    if (se->op == SX_NEG && (int)se->nargs == 1) {
        val_t c = term_split(se->args[0], vars, nvars, degs);
        return num_neg(c);
    }
    if (se->op == SX_MUL) {
        val_t coeff = Q(1);
        for (int i = 0; i < (int)se->nargs; i++) {
            if (vis_number(se->args[i])) coeff = num_mul(coeff, se->args[i]);
        }
        return coeff;
    }
    return Q(1);
}

/* Leading term of a multivariate polynomial (highest in lex order).
   Returns (coeff, degs[0..nvars-1]) via output params; returns coeff. */
static val_t poly_lt_degs(val_t p, val_t *vars, int nvars, long *best_degs) {
    val_t ex = sx_simplify(sx_expand(p));
    int nterms; val_t *terms; val_t single[1];
    if (vis_symexpr(ex) && as_symexpr(ex)->op == SX_ADD) {
        nterms = (int)as_symexpr(ex)->nargs; terms = as_symexpr(ex)->args;
    } else { single[0] = ex; nterms = 1; terms = single; }

    for (int i = 0; i < nvars; i++) best_degs[i] = -1;
    val_t best_coeff = Q(0);
    long cur_degs[16];

    for (int i = 0; i < nterms; i++) {
        if (qzero(terms[i])) continue;
        val_t c = term_split(terms[i], vars, nvars, cur_degs);
        if (qzero(c)) continue;
        if (best_degs[0] < 0 || mono_lex_cmp(terms[i],
                mono_from_degs(best_degs, vars, nvars), vars, nvars) > 0) {
            for (int j = 0; j < nvars; j++) best_degs[j] = cur_degs[j];
            best_coeff = c;
        }
    }
    return best_coeff;
}

/* Check if polynomial is zero by evaluating at test point. */
static bool is_zero_poly(val_t p, val_t *vars, int nvars) {
    if (num_is_zero(p)) return true;
    /* Evaluate at x₁=2, x₂=3, x₃=5, ... */
    val_t test_pts[] = {Q(2), Q(3), Q(5), Q(7), Q(11), Q(13), Q(17), Q(19)};
    val_t q = sx_simplify(sx_expand(p));
    for (int i = 0; i < nvars && i < 8; i++)
        q = sx_simplify(sx_substitute(q, vars[i], test_pts[i]));
    return num_is_zero(q);
}

/* S-polynomial using proper monomial arithmetic. */
static val_t s_poly_mv(val_t f, val_t g, val_t *vars, int nvars) {
    long df[16], dg[16], dlcm[16];
    val_t cf = poly_lt_degs(f, vars, nvars, df);
    val_t cg = poly_lt_degs(g, vars, nvars, dg);
    if (qzero(cf) || qzero(cg)) return Q(0);

    for (int i = 0; i < nvars; i++)
        dlcm[i] = df[i] > dg[i] ? df[i] : dg[i];

    /* tf = lcm/lt(f) monomial,  tg = lcm/lt(g) monomial */
    long dtf[16], dtg[16];
    for (int i = 0; i < nvars; i++) { dtf[i] = dlcm[i]-df[i]; dtg[i] = dlcm[i]-dg[i]; }
    val_t tf_mono = mono_from_degs(dtf, vars, nvars);
    val_t tg_mono = mono_from_degs(dtg, vars, nvars);

    /* S = (1/cf)*tf_mono*f - (1/cg)*tg_mono*g */
    val_t sp = sx_simplify(sx_expand(sx_sub(
        sx_mul(num_div(Q(1), cf), sx_mul(tf_mono, f)),
        sx_mul(num_div(Q(1), cg), sx_mul(tg_mono, g)))));
    return sp;
}

/* Reduce h modulo basis G using proper monomial coefficient arithmetic. */
static val_t poly_reduce_mv(val_t h, val_t *G, int ng, val_t *vars, int nvars) {
    val_t r = sx_simplify(sx_expand(h));
    int safety = 0;
    while (!is_zero_poly(r, vars, nvars) && safety++ < 256) {
        long dr[16], dgi[16];
        val_t cr = poly_lt_degs(r, vars, nvars, dr);
        if (qzero(cr)) break;

        bool reduced = false;
        for (int i = 0; i < ng && !reduced; i++) {
            val_t cgi = poly_lt_degs(G[i], vars, nvars, dgi);
            if (qzero(cgi)) continue;
            /* Check if LT(G[i]) divides LT(r) */
            bool divides = true;
            for (int j = 0; j < nvars; j++) {
                if (dgi[j] > dr[j]) { divides = false; break; }
            }
            if (divides) {
                long dt[16];
                for (int j = 0; j < nvars; j++) dt[j] = dr[j] - dgi[j];
                val_t t_mono = mono_from_degs(dt, vars, nvars);
                val_t t = sx_simplify(sx_mul(num_div(cr, cgi), t_mono));
                r = sx_simplify(sx_expand(sx_sub(r, sx_mul(t, G[i]))));
                reduced = true;
            }
        }
        if (!reduced) break;
    }
    if (is_zero_poly(r, vars, nvars)) return Q(0);
    return sx_simplify(r);
}

val_t sx_groebner(val_t polys, val_t vars_list) {
    /* Extract vars array */
    int nvars = 0; val_t vtmp = vars_list;
    while (vis_pair(vtmp)) { nvars++; vtmp = vcdr(vtmp); }
    if (nvars == 0 || nvars > 16) return polys;

    val_t *vars = (val_t *)gc_alloc_raw_pinned((size_t)nvars * sizeof(val_t));
    vtmp = vars_list;
    for (int i = 0; i < nvars; i++) { vars[i] = vcar(vtmp); vtmp = vcdr(vtmp); }

    /* Build initial basis */
    int max_basis = 256;
    val_t *G = (val_t *)gc_alloc_raw_pinned((size_t)max_basis * sizeof(val_t));
    int ng = 0;
    val_t ptmp = polys;
    while (vis_pair(ptmp) && ng < max_basis) {
        val_t p = sx_simplify(vcar(ptmp));
        if (!qzero(p)) G[ng++] = p;
        ptmp = vcdr(ptmp);
    }

    /* Buchberger's algorithm */
    int max_iter = 1000; /* safety limit */
    for (int iter = 0; iter < max_iter; iter++) {
        bool added = false;
        for (int i = 0; i < ng && !added; i++) {
            for (int j = i + 1; j < ng && !added; j++) {
                /* Buchberger's criterion: skip coprime leading terms */
                bool coprime = true;
                long di[16], dj[16];
                poly_lt_degs(G[i], vars, nvars, di);
                poly_lt_degs(G[j], vars, nvars, dj);
                for (int k = 0; k < nvars; k++) {
                    if (di[k] > 0 && dj[k] > 0) { coprime = false; break; }
                }
                if (coprime) continue;

                val_t sp = s_poly_mv(G[i], G[j], vars, nvars);
                val_t r  = poly_reduce_mv(sp, G, ng, vars, nvars);
                if (!is_zero_poly(r, vars, nvars) && ng < max_basis) {
                    G[ng++] = sx_simplify(r);
                    added = true;
                }
            }
        }
        if (!added) break;
    }

    /* Reduce basis: remove redundant elements */
    val_t result = V_NIL;
    for (int i = ng - 1; i >= 0; i--) {
        val_t *Gred = (val_t *)gc_alloc_raw_pinned((size_t)(ng - 1) * sizeof(val_t));
        int k = 0;
        for (int j = 0; j < ng; j++) if (j != i) Gred[k++] = G[j];
        val_t r = poly_reduce_mv(G[i], Gred, k, vars, nvars);
        if (!is_zero_poly(r, vars, nvars)) result = scm_cons(sx_simplify(r), result);
    }

    return result;
}

void sx_poly_init(void) { /* no-op for now */ }

/* ========================================================================== */
/* PART 11: Risch integration — rational functions (Phase 4e)                 */
/* ========================================================================== */

/*
 * Integrate a single partial-fraction term of the form A / f^k, where f is a
 * polynomial (linear or quadratic) and A is a constant or linear numerator.
 *
 * Linear factor  f = ax + b,  k ≥ 1:
 *   k = 1:  A/(ax+b)      → (A/a) * log|ax+b|
 *   k > 1:  A/(ax+b)^k    → A / (a*(1-k)*(ax+b)^(k-1))
 *
 * Quadratic factor  f = ax² + bx + c  (irreducible, k = 1):
 *   Numerator A (constant):
 *     → 2A/√disc * atan((2ax+b)/√disc)   (disc = 4ac-b²)
 *   Numerator Mx + N (linear):
 *     Split into (M/2a)*f'/f + (N - Mb/2a)/f
 *     → (M/2a)*log|f| + ∫(N - Mb/2a)/f dx   (atan case)
 *
 * Returns V_VOID if the term can't be handled here.
 */
static val_t integrate_pf_term(val_t A, val_t f, long k, val_t var) {
    int df; val_t *cf = expr_to_poly(f, var, &df);
    if (!cf) return V_VOID;

    if (df == 1) {
        /* Linear factor: f = c[1]*var + c[0] */
        val_t a = cf[1], b = cf[0];
        if (qzero(a)) return V_VOID;
        if (k == 1) {
            /* (A/a) * log|f| */
            val_t coeff = num_div(A, a);
            return sx_simplify(sx_mul(coeff, sx_log(sx_abs(f))));
        } else {
            /* A / (a*(1-k)*f^(k-1)) */
            val_t exp_v = Q(k - 1);
            val_t denom = num_mul(a, Q(1 - k));
            return sx_simplify(sx_div(A, sx_mul(denom, sx_expt(f, exp_v))));
        }
    }

    if (df == 2 && k == 1) {
        /* Irreducible quadratic f = c[2]*x² + c[1]*x + c[0] */
        val_t a2 = cf[2], b2 = cf[1], c2 = cf[0];
        val_t disc = num_sub(num_mul(num_mul(Q(4), a2), c2),
                             num_mul(b2, b2));
        if (!vis_number(disc) || num_to_double(disc) <= 0.0) return V_VOID;

        /* Check numerator shape */
        int dA; val_t *cA = expr_to_poly(A, var, &dA);
        if (!cA) return V_VOID;

        if (dA <= 0) {
            /* Constant numerator: 2*A/sqrt(disc) * atan((2a*x+b)/sqrt(disc)) */
            val_t sq   = sx_sqrt(disc);
            val_t inner = sx_div(
                sx_add(sx_mul(num_mul(Q(2), a2), var), b2), sq);
            return sx_simplify(
                sx_mul(num_div(num_mul(Q(2), A), sq), sx_atan(inner)));
        }

        if (dA == 1) {
            /* Linear numerator A = M*x + N */
            val_t M = cA[1], N = cA[0];
            /* Split: (M/2a)*d(f)/dx*1/f  +  (N - M*b/2a)/f */
            val_t M_over_2a   = num_div(M, num_mul(Q(2), a2));
            val_t N_minus     = num_sub(N, num_mul(M, num_div(b2, num_mul(Q(2), a2))));
            /* ∫ (M/2a)*f'/f dx = (M/2a)*log|f| */
            val_t log_part = sx_mul(M_over_2a, sx_log(sx_abs(f)));
            /* ∫ N_minus/f dx — constant/quadratic → atan */
            val_t atan_part = integrate_pf_term(N_minus, f, 1, var);
            if (atan_part == V_VOID) return V_VOID;
            return sx_simplify(sx_add(log_part, atan_part));
        }
    }

    return V_VOID;
}

/*
 * Integrate a polynomial expression (all integer powers of var) term-by-term.
 * Returns V_VOID if expr is not a polynomial in var.
 */
static val_t integrate_poly(val_t expr, val_t var) {
    int dp; val_t *cp = expr_to_poly(expr, var, &dp);
    if (!cp) return V_VOID;
    if (dp < 0) return Q(0);
    /* ∫ Σ c[i]*x^i dx = Σ c[i]*x^(i+1)/(i+1) */
    val_t result = Q(0);
    for (int i = 0; i <= dp; i++) {
        if (qzero(cp[i])) continue;
        if (i == 0) {
            result = sx_add(result, sx_mul(cp[i], var));
        } else {
            val_t ip1 = Q(i + 1);
            result = sx_add(result,
                sx_simplify(sx_div(sx_mul(cp[i], sx_expt(var, ip1)), ip1)));
        }
    }
    return sx_simplify(result);
}

val_t sx_integrate_rational(val_t expr, val_t var) {
    val_t ex = sx_simplify(sx_expand(expr));

    /* Is it a DIV expression? */
    if (!vis_symexpr(ex) || as_symexpr(ex)->op != SX_DIV ||
        (int)as_symexpr(ex)->nargs != 2)
        return V_VOID;

    val_t num_ex = as_symexpr(ex)->args[0];
    val_t den_ex = as_symexpr(ex)->args[1];

    /* Both must be polynomials in var */
    int dn, dd;
    val_t *cn = expr_to_poly(num_ex, var, &dn);
    val_t *cd = expr_to_poly(den_ex, var, &dd);
    if (!cn || !cd || dd <= 0) return V_VOID;

    /* Polynomial part */
    val_t result = Q(0);
    if (dn >= dd) {
        val_t *quot; int dq; val_t *rem2; int dr2;
        poly_pseudo_div(cn, dn, cd, dd, &quot, &dq, &rem2, &dr2);
        val_t poly_part = integrate_poly(poly_to_expr(quot, dq < 0 ? 0 : dq, var), var);
        if (poly_part == V_VOID) return V_VOID;
        result = poly_part;
        cn = rem2; dn = dr2;
        if (dn < 0) return sx_simplify(result);
    }

    val_t N = poly_to_expr(cn, dn, var);
    val_t D = poly_to_expr(cd, dd, var);

    /* Partial fraction decomposition */
    val_t pf = sx_partial_fractions(N, D, var);
    if (num_is_zero(pf)) return V_VOID; /* decomposition failed */

    /* Integrate each partial fraction term.
       pf is a sum of A/f^k and possibly a polynomial part. */
    val_t pf_ex = sx_simplify(sx_expand(pf));

    /* Collect additive terms */
    int nterms; val_t *terms; val_t single[1];
    if (vis_symexpr(pf_ex) && as_symexpr(pf_ex)->op == SX_ADD) {
        nterms = (int)as_symexpr(pf_ex)->nargs;
        terms  = as_symexpr(pf_ex)->args;
    } else { single[0] = pf_ex; nterms = 1; terms = single; }

    for (int i = 0; i < nterms; i++) {
        val_t t = terms[i];
        val_t int_t = V_VOID;

        /* Pure number or polynomial-in-var term */
        if (vis_number(t)) {
            int_t = sx_mul(t, var);
        } else if (!vis_symexpr(t)) {
            int_t = V_VOID;
        } else {
            SymExpr *se = as_symexpr(t);

            /* A / f^k */
            if (se->op == SX_DIV && (int)se->nargs == 2) {
                val_t A = se->args[0];
                val_t denom = se->args[1];
                /* Extract f and k from denom = f^k */
                val_t f; long k = 1;
                if (vis_symexpr(denom) && as_symexpr(denom)->op == SX_EXPT &&
                    (int)as_symexpr(denom)->nargs == 2 &&
                    vis_fixnum(as_symexpr(denom)->args[1])) {
                    f = as_symexpr(denom)->args[0];
                    k = vunfix(as_symexpr(denom)->args[1]);
                } else {
                    f = denom;
                }
                int_t = integrate_pf_term(A, f, k, var);
            }
            /* Polynomial term: x^k or (c * x^k) etc. */
            else {
                int_t = integrate_poly(t, var);
            }
        }

        if (int_t == V_VOID) {
            /* Can't integrate this term — give up */
            return V_VOID;
        }
        result = sx_add(result, int_t);
    }

    return sx_simplify(result);
}

val_t sx_integrate_log_poly(val_t f_arg, val_t var) {
    /* ∫ log(f(x)) dx = x*log(f) - ∫ x*f'(x)/f(x) dx
       The tail ∫ x*f'/f dx is a rational function (if f is a polynomial). */
    int df; val_t *cf = expr_to_poly(f_arg, var, &df);
    if (!cf || df <= 0) return V_VOID;

    val_t fp     = sx_simplify(sx_expand(sx_diff(f_arg, var)));
    val_t log_f  = sx_log(f_arg);
    val_t head   = sx_mul(var, log_f);       /* x * log(f) */
    val_t tail_integrand = sx_simplify(      /* x * f'/f */
        sx_div(sx_mul(var, fp), f_arg));

    /* tail_integrand is rational — try rational integration */
    val_t tail = sx_integrate_rational(tail_integrand, var);
    if (tail == V_VOID) return V_VOID;

    return sx_simplify(sx_sub(head, tail));
}
