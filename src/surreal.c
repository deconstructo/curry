#include "surreal.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "port.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));
extern void scm_write(val_t v, val_t port);

val_t SUR_ZERO, SUR_ONE, SUR_NEG_ONE, SUR_OMEGA, SUR_EPSILON;

#define MAX_TERMS 64

/* ---- Allocation ---- */

static val_t alloc_sur(int n) {
    Surreal *s = CURRY_NEW_FLEX(Surreal, 2 * n);
    s->hdr.type  = T_SURREAL;
    s->hdr.flags = 0;
    s->nterms    = n;
    return vptr(s);
}

/* Build from arrays, skipping zero coefficients. Arrays need not be pre-filtered. */
static val_t from_arrays(int n, val_t *exps, val_t *coeffs) {
    int nnz = 0;
    for (int i = 0; i < n; i++)
        if (!num_is_zero(coeffs[i])) nnz++;
    val_t r = alloc_sur(nnz);
    Surreal *s = as_surreal(r);
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (!num_is_zero(coeffs[i])) {
            s->data[2*j]   = exps[i];
            s->data[2*j+1] = coeffs[i];
            j++;
        }
    }
    return r;
}

/* ---- Public constructors ---- */

val_t sur_make_term(val_t exp, val_t coeff) {
    if (num_is_zero(coeff)) return SUR_ZERO;
    val_t r = alloc_sur(1);
    as_surreal(r)->data[0] = exp;
    as_surreal(r)->data[1] = coeff;
    return r;
}

val_t sur_make(int n, val_t *exps, val_t *coeffs) {
    return from_arrays(n, exps, coeffs);
}

val_t sur_from_val(val_t v) {
    if (vis_surreal(v)) return v;
    if (num_is_zero(v)) return SUR_ZERO;
    val_t r = alloc_sur(1);
    as_surreal(r)->data[0] = vfix(0);
    as_surreal(r)->data[1] = v;
    return r;
}

void surreal_init(void) {
    /* Bootstrap: SUR_ZERO must exist before sur_make_term is called */
    Surreal *z = CURRY_NEW_FLEX(Surreal, 0);
    z->hdr.type = T_SURREAL; z->hdr.flags = 0; z->nterms = 0;
    SUR_ZERO    = vptr(z);
    SUR_ONE     = sur_make_term(vfix(0),  vfix(1));
    SUR_NEG_ONE = sur_make_term(vfix(0),  vfix(-1));
    SUR_OMEGA   = sur_make_term(vfix(1),  vfix(1));
    SUR_EPSILON = sur_make_term(vfix(-1), vfix(1));
}

/* ---- Arithmetic helpers ---- */

/* Sort parallel (exp, coeff) arrays descending by exp — insertion sort */
static void sort_terms(int n, val_t *exps, val_t *coeffs) {
    for (int i = 1; i < n; i++) {
        val_t ke = exps[i], kc = coeffs[i];
        int j = i - 1;
        while (j >= 0 && num_cmp(exps[j], ke) < 0) {
            exps[j+1]   = exps[j];
            coeffs[j+1] = coeffs[j];
            j--;
        }
        exps[j+1]   = ke;
        coeffs[j+1] = kc;
    }
}

/* Merge adjacent equal-exponent terms in a sorted array (in-place).
 * Returns the new length. */
static int merge_equal(int n, val_t *exps, val_t *coeffs) {
    if (n == 0) return 0;
    int out = 0;
    for (int i = 0; i < n; ) {
        val_t acc = coeffs[i];
        int j = i + 1;
        while (j < n && num_cmp(exps[j], exps[i]) == 0) {
            acc = num_add(acc, coeffs[j]);
            j++;
        }
        if (!num_is_zero(acc)) {
            exps[out]   = exps[i];
            coeffs[out] = acc;
            out++;
        }
        i = j;
    }
    return out;
}

/* ---- Arithmetic ---- */

val_t sur_neg(val_t a) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    Surreal *sa = as_surreal(a);
    int n = sa->nterms;
    val_t r = alloc_sur(n);
    Surreal *sr = as_surreal(r);
    for (int i = 0; i < n; i++) {
        sr->data[2*i]   = sa->data[2*i];
        sr->data[2*i+1] = num_neg(sa->data[2*i+1]);
    }
    return r;
}

val_t sur_add(val_t a, val_t b) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    if (!vis_surreal(b)) b = sur_from_val(b);
    Surreal *sa = as_surreal(a), *sb = as_surreal(b);
    int na = sa->nterms, nb = sb->nterms;
    if (na == 0) return b;
    if (nb == 0) return a;

    int cap = na + nb;
    val_t *exps   = (val_t *)gc_alloc((size_t)cap * sizeof(val_t));
    val_t *coeffs = (val_t *)gc_alloc((size_t)cap * sizeof(val_t));
    int n = 0, i = 0, j = 0;

    while (i < na && j < nb) {
        int cmp = num_cmp(sa->data[2*i], sb->data[2*j]);
        if (cmp > 0) {
            exps[n]   = sa->data[2*i];
            coeffs[n] = sa->data[2*i+1];
            n++; i++;
        } else if (cmp < 0) {
            exps[n]   = sb->data[2*j];
            coeffs[n] = sb->data[2*j+1];
            n++; j++;
        } else {
            val_t c = num_add(sa->data[2*i+1], sb->data[2*j+1]);
            if (!num_is_zero(c)) {
                exps[n]   = sa->data[2*i];
                coeffs[n] = c;
                n++;
            }
            i++; j++;
        }
    }
    while (i < na) { exps[n] = sa->data[2*i]; coeffs[n] = sa->data[2*i+1]; n++; i++; }
    while (j < nb) { exps[n] = sb->data[2*j]; coeffs[n] = sb->data[2*j+1]; n++; j++; }

    return from_arrays(n, exps, coeffs);
}

val_t sur_sub(val_t a, val_t b) {
    return sur_add(a, sur_neg(b));
}

val_t sur_mul(val_t a, val_t b) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    if (!vis_surreal(b)) b = sur_from_val(b);
    Surreal *sa = as_surreal(a), *sb = as_surreal(b);
    int na = sa->nterms, nb = sb->nterms;
    if (na == 0 || nb == 0) return SUR_ZERO;

    int nc = na * nb;
    if (nc > MAX_TERMS * MAX_TERMS) nc = MAX_TERMS * MAX_TERMS;
    val_t *exps   = (val_t *)gc_alloc((size_t)nc * sizeof(val_t));
    val_t *coeffs = (val_t *)gc_alloc((size_t)nc * sizeof(val_t));

    int n = 0;
    for (int i = 0; i < na && n < nc; i++) {
        for (int j = 0; j < nb && n < nc; j++) {
            exps[n]   = num_add(sa->data[2*i], sb->data[2*j]);
            coeffs[n] = num_mul(sa->data[2*i+1], sb->data[2*j+1]);
            n++;
        }
    }
    sort_terms(n, exps, coeffs);
    n = merge_equal(n, exps, coeffs);
    return from_arrays(n, exps, coeffs);
}

val_t sur_div(val_t a, val_t b) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    if (!vis_surreal(b)) b = sur_from_val(b);
    Surreal *sb = as_surreal(b);
    if (sb->nterms == 0)
        scm_raise(V_FALSE, "𒀭 ḫiṭītu — ina ṣifri pašāṭum lā leqû: surreal division by zero");

    val_t *qexps   = (val_t *)gc_alloc(MAX_TERMS * sizeof(val_t));
    val_t *qcoeffs = (val_t *)gc_alloc(MAX_TERMS * sizeof(val_t));
    int qn = 0;

    val_t r = a;
    for (int iter = 0; iter < MAX_TERMS; iter++) {
        Surreal *sr = as_surreal(r);
        if (sr->nterms == 0) break;

        val_t re = sr->data[0];
        val_t rc = sr->data[1];
        val_t be = as_surreal(b)->data[0];
        val_t bc = as_surreal(b)->data[1];

        val_t qe = num_sub(re, be);
        val_t qc = num_div(rc, bc);

        qexps[qn]   = qe;
        qcoeffs[qn] = qc;
        qn++;

        val_t qterm = sur_make_term(qe, qc);
        r = sur_sub(r, sur_mul(qterm, b));
    }
    return from_arrays(qn, qexps, qcoeffs);
}

val_t sur_abs(val_t a) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    return sur_is_negative(a) ? sur_neg(a) : a;
}

val_t sur_expt(val_t base, val_t exp) {
    if (!vis_surreal(base)) base = sur_from_val(base);
    Surreal *sb = as_surreal(base);

    /* Single-term base: exact result for any numeric exponent */
    if (sb->nterms == 1) {
        val_t be = sb->data[0];
        val_t bc = sb->data[1];
        val_t ne = num_mul(be, exp);       /* new ω-exponent */
        val_t nc = num_expt(bc, exp);      /* new coefficient */
        return sur_make_term(ne, nc);
    }

    /* Multi-term: integer exponents only — repeated multiplication */
    if (vis_fixnum(exp)) {
        intptr_t n = vunfix(exp);
        if (n == 0) return SUR_ONE;
        if (n < 0) return sur_div(SUR_ONE, sur_expt(base, vfix(-n)));
        val_t r = SUR_ONE;
        for (intptr_t i = 0; i < n && i < MAX_TERMS; i++) r = sur_mul(r, base);
        return r;
    }

    /* Fallback: approximate via double */
    return sur_from_val(num_make_float(pow(sur_to_double(base), num_to_double(exp))));
}

/* ---- Comparison ---- */

int sur_compare(val_t a, val_t b) {
    if (!vis_surreal(a)) a = sur_from_val(a);
    if (!vis_surreal(b)) b = sur_from_val(b);
    val_t d = sur_sub(a, b);
    Surreal *sd = as_surreal(d);
    if (sd->nterms == 0) return 0;
    return num_is_positive(sd->data[1]) ? 1 : -1;
}

bool sur_is_zero(val_t a)     { return as_surreal(a)->nterms == 0; }
bool sur_is_positive(val_t a) {
    Surreal *s = as_surreal(a);
    return s->nterms > 0 && num_is_positive(s->data[1]);
}
bool sur_is_negative(val_t a) {
    Surreal *s = as_surreal(a);
    return s->nterms > 0 && num_is_negative(s->data[1]);
}

/* ---- Classification ---- */

bool sur_finite_p(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return true;
    return num_cmp(s->data[0], vfix(0)) <= 0;
}

bool sur_infinite_p(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return false;
    return num_cmp(s->data[0], vfix(0)) > 0;
}

bool sur_infinitesimal_p(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return false;  /* 0 is not a positive infinitesimal */
    return num_cmp(s->data[0], vfix(0)) < 0;
}

/* ---- Accessors ---- */

int   sur_nterms(val_t a)           { return as_surreal(a)->nterms; }
val_t sur_term_exp(val_t a, int i)  { return as_surreal(a)->data[2*i]; }
val_t sur_term_coeff(val_t a, int i){ return as_surreal(a)->data[2*i+1]; }

static val_t coeff_at_exp(val_t a, val_t target_exp) {
    Surreal *s = as_surreal(a);
    for (int i = 0; i < s->nterms; i++) {
        int c = num_cmp(s->data[2*i], target_exp);
        if (c == 0) return s->data[2*i+1];
        if (c < 0) break;
    }
    return vfix(0);
}

val_t sur_real_part(val_t a)    { return coeff_at_exp(a, vfix(0));  }
val_t sur_omega_part(val_t a)   { return coeff_at_exp(a, vfix(1));  }
val_t sur_epsilon_part(val_t a) { return coeff_at_exp(a, vfix(-1)); }

/* ---- Conversion ---- */

double sur_to_double(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return 0.0;
    /* If leading exponent > 0, surreal is infinite */
    if (num_cmp(s->data[0], vfix(0)) > 0)
        return num_is_positive(s->data[1]) ? HUGE_VAL : -HUGE_VAL;
    /* Find ω⁰ term — contributions from ω^n, n<0 are infinitesimal → 0 */
    for (int i = 0; i < s->nterms; i++) {
        int c = num_cmp(s->data[2*i], vfix(0));
        if (c == 0) return num_to_double(s->data[2*i+1]);
        if (c < 0) break;
    }
    return 0.0;
}

val_t sur_to_val(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return vfix(0);
    if (s->nterms == 1 && num_cmp(s->data[0], vfix(0)) == 0)
        return s->data[1];  /* pure real → extract the coefficient */
    return a;
}

/* ---- Birthday ---- */

val_t sur_birthday(val_t a) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) return vfix(0);
    if (sur_infinite_p(a) || sur_infinitesimal_p(a)) return SUR_OMEGA;
    val_t re = sur_real_part(a);
    if (num_is_integer(re)) return num_abs(re);
    return SUR_OMEGA;  /* rationals have transfinite birthday in general */
}

/* ---- Display ---- */

/* UTF-8 for ω and ε */
static const char STR_OMEGA[]   = "\xCF\x89";   /* ω U+03C9 */
static const char STR_EPSILON[] = "\xCE\xB5";   /* ε U+03B5 */

void sur_write(val_t a, val_t port) {
    Surreal *s = as_surreal(a);
    if (s->nterms == 0) { port_write_string(port, "0", 1); return; }

    for (int i = 0; i < s->nterms; i++) {
        val_t e = s->data[2*i];
        val_t c = s->data[2*i+1];

        int first = (i == 0);
        int neg   = num_is_negative(c);

        if (!first) {
            if (neg) { port_write_string(port, " - ", 3); c = num_neg(c); }
            else      port_write_string(port, " + ", 3);
        } else if (neg) {
            port_write_string(port, "-", 1); c = num_neg(c);
        }

        int ecmp = num_cmp(e, vfix(0));

        if (ecmp == 0) {
            scm_write(c, port);
        } else {
            /* Has ω^e factor */
            int unit_coeff = num_cmp(c, vfix(1)) == 0;
            if (!unit_coeff) scm_write(c, port);

            if (num_cmp(e, vfix(1)) == 0) {
                port_write_string(port, STR_OMEGA, 2);
            } else if (num_cmp(e, vfix(-1)) == 0) {
                port_write_string(port, STR_EPSILON, 2);
            } else {
                port_write_string(port, STR_OMEGA, 2);
                port_write_char(port, '^');
                char buf[32];
                int n;
                if (vis_fixnum(e)) {
                    n = snprintf(buf, sizeof(buf), "%ld", (long)vunfix(e));
                } else if (vis_rational(e)) {
                    /* Print p/q */
                    mpq_t q; mpq_init(q); mpq_set(q, as_rat(e)->q);
                    char *s2 = mpq_get_str(NULL, 10, q);
                    n = snprintf(buf, sizeof(buf), "%s", s2);
                    free(s2); mpq_clear(q);
                } else {
                    n = snprintf(buf, sizeof(buf), "?");
                }
                port_write_string(port, buf, (uint32_t)n);
            }
        }
    }
}
