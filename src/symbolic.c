#include "symbolic.h"
#include "object.h"
#include "gc.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* Interned operator symbols */
val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
val_t SX_NCMUL;
val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;
val_t SX_FRACDIFF, SX_FRACINT;
val_t SX_SINH, SX_COSH, SX_TANH;
val_t SX_ASIN, SX_ACOS, SX_ATAN;
val_t SX_ASINH, SX_ACOSH, SX_ATANH;
val_t SX_COT, SX_SEC, SX_CSC;
val_t SX_LIMIT;
val_t SX_SIGN;
val_t SX_APPLY;
val_t SX_LAPLACE;
val_t SX_FOURIER;

void symbolic_init(void) {
    SX_ADD       = sym_intern_cstr("+");
    SX_SUB       = sym_intern_cstr("-");
    SX_MUL       = sym_intern_cstr("*");
    SX_NCMUL     = sym_intern_cstr("nc*");
    SX_DIV       = sym_intern_cstr("/");
    SX_NEG       = sym_intern_cstr("neg");
    SX_EXPT      = sym_intern_cstr("expt");
    SX_SQRT      = sym_intern_cstr("sqrt");
    SX_SIN       = sym_intern_cstr("sin");
    SX_COS       = sym_intern_cstr("cos");
    SX_TAN       = sym_intern_cstr("tan");
    SX_EXP       = sym_intern_cstr("exp");
    SX_LOG       = sym_intern_cstr("log");
    SX_ABS       = sym_intern_cstr("abs");
    SX_INTEGRATE = sym_intern_cstr("∫");
    SX_CONJ      = sym_intern_cstr("conj");
    SX_REAL      = sym_intern_cstr("real-part");
    SX_IMAG      = sym_intern_cstr("imag-part");
    SX_FRACDIFF  = sym_intern_cstr("frac-diff");
    SX_FRACINT   = sym_intern_cstr("frac-int");
    SX_SINH      = sym_intern_cstr("sinh");
    SX_COSH      = sym_intern_cstr("cosh");
    SX_TANH      = sym_intern_cstr("tanh");
    SX_ASIN      = sym_intern_cstr("asin");
    SX_ACOS      = sym_intern_cstr("acos");
    SX_ATAN      = sym_intern_cstr("atan");
    SX_ASINH     = sym_intern_cstr("asinh");
    SX_ACOSH     = sym_intern_cstr("acosh");
    SX_ATANH     = sym_intern_cstr("atanh");
    SX_COT       = sym_intern_cstr("cot");
    SX_SEC       = sym_intern_cstr("sec");
    SX_CSC       = sym_intern_cstr("csc");
    SX_LIMIT     = sym_intern_cstr("limit");
    SX_SIGN      = sym_intern_cstr("sign");
    SX_APPLY     = sym_intern_cstr("apply");
    SX_LAPLACE   = sym_intern_cstr("laplace");
    SX_FOURIER   = sym_intern_cstr("fourier");
}

/* ---- Constructors ---- */

val_t sx_make_var(val_t name) {
    SymVar *v = CURRY_NEW(SymVar);
    v->hdr.type  = T_SYMVAR;
    v->hdr.flags = 0;
    v->name      = name;
    return vptr(v);
}

val_t sx_make_var_flags(val_t name, uint32_t flags) {
    SymVar *v = CURRY_NEW(SymVar);
    v->hdr.type  = T_SYMVAR;
    v->hdr.flags = flags;
    v->name      = name;
    return vptr(v);
}

val_t sx_make_fn(val_t name, val_t params) {
    SymFn *f = CURRY_NEW(SymFn);
    f->hdr.type  = T_SYMFN;
    f->hdr.flags = 0;
    f->name      = name;
    f->params    = params;
    f->parent    = V_FALSE;
    f->d_param   = V_FALSE;
    return vptr(f);
}

val_t sx_fn_name(val_t fn)   { return as_symfn(fn)->name; }
val_t sx_fn_params(val_t fn) { return as_symfn(fn)->params; }

val_t sx_make_apply(val_t fn, int nargs, val_t *args) {
    /* SX_APPLY: args[0]=T_SYMFN, args[1..nargs]=applied arguments */
    int total = nargs + 1;
    val_t *all = (val_t *)gc_alloc((size_t)total * sizeof(val_t));
    all[0] = fn;
    for (int i = 0; i < nargs; i++) all[i + 1] = args[i];
    return sx_make_expr(SX_APPLY, total, all);
}

val_t sx_make_expr(val_t op, int nargs, val_t *args) {
    SymExpr *e = (SymExpr *)gc_alloc(sizeof(SymExpr) + (size_t)nargs * sizeof(val_t));
    e->hdr.type  = T_SYMEXPR;
    e->hdr.flags = 0;
    e->op        = op;
    e->nargs     = (uint32_t)nargs;
    for (int i = 0; i < nargs; i++) e->args[i] = args[i];
    return vptr(e);
}

static val_t sx_expr1(val_t op, val_t a) {
    return sx_make_expr(op, 1, &a);
}
static val_t sx_expr2(val_t op, val_t a, val_t b) {
    val_t args[2] = {a, b}; return sx_make_expr(op, 2, args);
}

/* ---- Accessors ---- */

val_t sx_var_name(val_t v)        { return as_symvar(v)->name; }
val_t sx_expr_op(val_t e)         { return as_symexpr(e)->op; }
int   sx_expr_nargs(val_t e)      { return (int)as_symexpr(e)->nargs; }
val_t sx_expr_arg(val_t e, int i) { return as_symexpr(e)->args[i]; }

/* ---- Structural equality ---- */

bool sx_equal(val_t a, val_t b) {
    if (a == b) return true;
    if (vis_symvar(a) && vis_symvar(b))
        return as_symvar(a)->name == as_symvar(b)->name;
    if (vis_symexpr(a) && vis_symexpr(b)) {
        SymExpr *ea = as_symexpr(a), *eb = as_symexpr(b);
        if (ea->op != eb->op || ea->nargs != eb->nargs) return false;
        for (uint32_t i = 0; i < ea->nargs; i++)
            if (!sx_equal(ea->args[i], eb->args[i])) return false;
        return true;
    }
    if (vis_number(a) && vis_number(b)) return num_eq(a, b);
    if (vis_symfn(a) && vis_symfn(b))
        return as_symfn(a)->name == as_symfn(b)->name;
    return false;
}

/* ---- Simplification ---- */

/* forward declaration */
val_t sx_simplify(val_t expr);

static bool is_zero(val_t v) { return vis_number(v) && num_is_zero(v); }
static bool is_one(val_t v)  { return vis_number(v) && !vis_symbolic(v) && num_is_one(v); }
static bool is_two(val_t v)  { return vis_fixnum(v) && vunfix(v) == 2; }


val_t sx_simplify(val_t expr) {
    if (!vis_symexpr(expr)) return expr;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    /* Recursively simplify all arguments */
    val_t *sa = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) sa[i] = sx_simplify(se->args[i]);

    /* Flatten nested ADD or MUL into one level */
    if (op == SX_ADD || op == SX_MUL || op == SX_NCMUL) {
        /* Count total terms after flattening */
        int total = 0;
        for (int i = 0; i < n; i++) {
            if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op)
                total += (int)as_symexpr(sa[i])->nargs;
            else total += 1;
        }
        if (total > n) {
            val_t *flat = (val_t *)gc_alloc((size_t)total * sizeof(val_t));
            int k = 0;
            for (int i = 0; i < n; i++) {
                if (vis_symexpr(sa[i]) && as_symexpr(sa[i])->op == op) {
                    SymExpr *sub = as_symexpr(sa[i]);
                    for (uint32_t j = 0; j < sub->nargs; j++) flat[k++] = sub->args[j];
                } else flat[k++] = sa[i];
            }
            sa = flat; n = total;
        }
    }

    /* Count purely numeric args */
    int num_count = 0;
    for (int i = 0; i < n; i++) if (vis_number(sa[i])) num_count++;

    /* ---- ADD: fold numerics and strip zeros ---- */
    if (op == SX_ADD) {
        if (num_count == n) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++) acc = num_add(acc, sa[i]);
            return acc;
        }
        /* Remove zero terms, fold remaining numerics */
        val_t num_acc = vfix(0);
        int nsym = 0;
        for (int i = 0; i < n; i++) if (!vis_number(sa[i])) nsym++;
        val_t *syms = (val_t *)gc_alloc((size_t)nsym * sizeof(val_t));
        int j = 0;
        for (int i = 0; i < n; i++) {
            if (vis_number(sa[i])) num_acc = num_add(num_acc, sa[i]);
            else syms[j++] = sa[i];
        }
        if (nsym == 0) return num_acc;

        /* Like-term collection: combine terms with equal bases.
           Each symbolic term is decomposed as coeff * base where base is the
           "structural" part (a sym-var, an nc* product, or other expr).
           Equal bases accumulate their coefficients; coeff=0 terms are dropped. */
        {
            /* Extract (coeff, base) pairs.  Convention:
               - plain sym-var or sym-expr (not neg, not coeff*base): coeff=1, base=self
               - neg(e):                                               coeff=-1, base=e
               - (* n e) or (nc* n e) where n is numeric, e is non-numeric: coeff=n, base=e */
            val_t *bases  = (val_t *)gc_alloc((size_t)nsym * sizeof(val_t));
            val_t *coeffs = (val_t *)gc_alloc((size_t)nsym * sizeof(val_t));
            for (int i = 0; i < nsym; i++) {
                val_t s = syms[i];
                bases[i] = s; coeffs[i] = vfix(1);
                if (vis_symexpr(s)) {
                    SymExpr *se = as_symexpr(s);
                    if (se->op == SX_NEG && se->nargs == 1) {
                        bases[i] = se->args[0]; coeffs[i] = vfix(-1);
                    } else if ((se->op == SX_MUL || se->op == SX_NCMUL) && se->nargs >= 2
                               && vis_number(se->args[0]) && !vis_symbolic(se->args[0])) {
                        coeffs[i] = se->args[0];
                        bases[i] = se->nargs == 2 ? se->args[1]
                                 : sx_make_expr(se->op, se->nargs - 1, se->args + 1);
                    }
                }
            }
            bool any_combined = false;
            for (int i = 0; i < nsym; i++) {
                if (is_zero(coeffs[i])) continue;
                for (int k2 = i + 1; k2 < nsym; k2++) {
                    if (is_zero(coeffs[k2])) continue;
                    if (sx_equal(bases[i], bases[k2])) {
                        coeffs[i] = num_add(coeffs[i], coeffs[k2]);
                        coeffs[k2] = vfix(0);
                        any_combined = true;
                    }
                }
            }
            if (any_combined) {
                int new_nsym = 0;
                for (int i = 0; i < nsym; i++) {
                    if (is_zero(coeffs[i])) continue;
                    if (is_one(coeffs[i])) syms[new_nsym++] = bases[i];
                    else syms[new_nsym++] = sx_mul(coeffs[i], bases[i]);
                }
                nsym = new_nsym;
                if (nsym == 0) return num_acc;
            }
        }

        if (is_zero(num_acc)) {
            if (nsym == 1) return syms[0];
            return sx_make_expr(SX_ADD, nsym, syms);
        }
        /* prepend numeric sum */
        val_t *all = (val_t *)gc_alloc((size_t)(nsym + 1) * sizeof(val_t));
        all[0] = num_acc;
        for (int i = 0; i < nsym; i++) all[i+1] = syms[i];
        if (nsym + 1 == 1) return all[0];
        return sx_make_expr(SX_ADD, nsym + 1, all);
    }

    /* ---- SUB ---- */
    if (op == SX_SUB && n == 2) {
        val_t a = sa[0], b = sa[1];
        if (num_count == 2) return num_sub(a, b);
        if (is_zero(b)) return a;
        if (is_zero(a)) return sx_neg(b);
        if (sx_equal(a, b)) return vfix(0);
    }

    /* ---- NEG ---- */
    if (op == SX_NEG && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_neg(a);
        if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG)
            return as_symexpr(a)->args[0];
    }

    /* ---- MUL: fold numerics, strip ones, detect zero ---- */
    if (op == SX_MUL) {
        if (num_count == n) {
            val_t acc = vfix(1);
            for (int i = 0; i < n; i++) acc = num_mul(acc, sa[i]);
            return acc;
        }
        /* Any zero factor? */
        for (int i = 0; i < n; i++) if (is_zero(sa[i])) return vfix(0);
        /* Fold numeric factors and strip ones */
        val_t coeff = vfix(1);
        int nsym = 0;
        for (int i = 0; i < n; i++) if (!vis_number(sa[i])) nsym++;
        val_t *nsyms = (val_t *)gc_alloc((size_t)nsym * sizeof(val_t));
        int j = 0;
        for (int i = 0; i < n; i++) {
            if (vis_number(sa[i])) coeff = num_mul(coeff, sa[i]);
            else nsyms[j++] = sa[i];
        }
        if (is_zero(coeff)) return vfix(0);
        if (is_one(coeff)) {
            if (nsym == 1) return nsyms[0];
            /* (a/b) * c → (a*c)/b */
            if (nsym == 2) {
                for (int i = 0; i < 2; i++) {
                    int jj = 1 - i;
                    if (!vis_symexpr(nsyms[i]) || as_symexpr(nsyms[i])->op != SX_DIV ||
                        as_symexpr(nsyms[i])->nargs != 2) continue;
                    val_t dn = as_symexpr(nsyms[i])->args[0];
                    val_t dd = as_symexpr(nsyms[i])->args[1];
                    val_t new_num = sx_mul(dn, nsyms[jj]);
                    return sx_simplify(sx_div(new_num, dd));
                }
            }
            return sx_make_expr(SX_MUL, nsym, nsyms);
        }
        /* coeff == -1: return neg (also detect quaternion -1+0i+0j+0k) */
        if (vis_number(coeff) && !vis_complex(coeff) && num_eq(coeff, vfix(-1))) {
            val_t inner = nsym == 1 ? nsyms[0] : sx_make_expr(SX_MUL, nsym, nsyms);
            return sx_neg(inner);
        }
        val_t *all = (val_t *)gc_alloc((size_t)(nsym + 1) * sizeof(val_t));
        all[0] = coeff;
        for (int i = 0; i < nsym; i++) all[i+1] = nsyms[i];
        if (nsym + 1 == 1) return all[0];
        return sx_make_expr(SX_MUL, nsym + 1, all);
    }

    /* ---- DIV ---- */
    if (op == SX_DIV && n == 2) {
        val_t a = sa[0], b = sa[1];
        if (num_count == 2) return num_div(a, b);
        if (is_zero(a)) return vfix(0);
        if (is_one(b)) return a;
        /* (/ (* c . syms) d) → fold numeric factors: (* (c/d) . syms) */
        if (vis_number(b) && vis_symexpr(a) && as_symexpr(a)->op == SX_MUL) {
            SymExpr *mul = as_symexpr(a);
            int mn = (int)mul->nargs, mnum = 0;
            for (int i = 0; i < mn; i++) if (vis_number(mul->args[i])) mnum++;
            if (mnum > 0) {
                val_t coeff = vfix(1);
                int msym = mn - mnum;
                val_t *msyms = (val_t *)gc_alloc((size_t)msym * sizeof(val_t));
                int j = 0;
                for (int i = 0; i < mn; i++) {
                    if (vis_number(mul->args[i])) coeff = num_mul(coeff, mul->args[i]);
                    else msyms[j++] = mul->args[i];
                }
                val_t nc = num_div(coeff, b);
                if (msym == 0) return nc;
                if (is_one(nc)) return msym == 1 ? msyms[0] : sx_make_expr(SX_MUL, msym, msyms);
                val_t *all = (val_t *)gc_alloc((size_t)(msym + 1) * sizeof(val_t));
                all[0] = nc;
                for (int i = 0; i < msym; i++) all[i+1] = msyms[i];
                return sx_make_expr(SX_MUL, msym + 1, all);
            }
        }
    }

    /* ---- NCMUL: ordered (non-commutative) product ---- */
    if (op == SX_NCMUL) {
        if (n == 1) return sa[0];
        /* All concrete: fold left-to-right */
        if (num_count == n) {
            val_t acc = sa[0];
            for (int i = 1; i < n; i++) acc = num_mul(acc, sa[i]);
            return acc;
        }
        /* Separate real scalars (commute freely with quaternions) from ordered
           non-commutative factors.  Adjacent NC concretes are folded in place. */
        val_t scalar = vfix(1); bool has_scalar = false;
        val_t *nc = (val_t *)gc_alloc((size_t)n * sizeof(val_t)); int nc_n = 0;
        for (int i = 0; i < n; i++) {
            val_t v = sa[i];
            if (is_zero(v)) return vfix(0);
            if (vis_fixnum(v) || vis_flonum(v) || vis_bignum(v) || vis_rational(v)) {
                scalar = has_scalar ? num_mul(scalar, v) : v; has_scalar = true;
            } else if (vis_number(v)) {
                /* Quaternion/complex/octonion with zero imaginary parts is a real
                   scalar and commutes freely; otherwise maintain order. */
                bool is_real_embed = false;
                if (vis_quat(v)) {
                    Quaternion *q = as_quat(v);
                    is_real_embed = (q->b == 0.0 && q->c == 0.0 && q->d == 0.0);
                }
                if (is_real_embed) {
                    val_t rv = num_make_float(as_quat(v)->a);
                    scalar = has_scalar ? num_mul(scalar, rv) : rv; has_scalar = true;
                } else {
                    /* NC concrete: fold with adjacent NC concrete */
                    if (nc_n > 0 && vis_number(nc[nc_n-1])) nc[nc_n-1] = num_mul(nc[nc_n-1], v);
                    else nc[nc_n++] = v;
                }
            } else {
                nc[nc_n++] = v;
            }
        }
        /* scalar == -1 with NC factors: emit (neg (nc* ...)) */
        if (has_scalar && num_eq(scalar, vfix(-1)) && nc_n > 0) {
            val_t inner = nc_n == 1 ? nc[0] : sx_make_expr(SX_NCMUL, nc_n, nc);
            return sx_neg(inner);
        }
        /* Rebuild: leading real scalar then ordered NC factors */
        val_t *res = (val_t *)gc_alloc((size_t)(nc_n + 1) * sizeof(val_t)); int rn = 0;
        if (has_scalar && !is_one(scalar)) res[rn++] = scalar;
        for (int i = 0; i < nc_n; i++) if (!is_one(nc[i])) res[rn++] = nc[i];
        if (rn == 0) return vfix(1);
        if (rn == 1) return res[0];
        return sx_make_expr(SX_NCMUL, rn, res);
    }

    /* ---- EXPT ---- */
    if (op == SX_EXPT && n == 2) {
        val_t base = sa[0], exp = sa[1];
        if (num_count == 2) return num_expt(base, exp);
        if (is_zero(exp)) return vfix(1);
        if (is_one(exp)) return base;
        if (vis_number(base) && num_is_zero(base)) return vfix(0);
        if (vis_number(base) && is_one(base)) return vfix(1);
    }

    /* ---- SQRT ---- */
    if (op == SX_SQRT && n == 1 && vis_number(sa[0]))
        return num_sqrt(sa[0]);
    /* sqrt(x^2) — simplify with assumption knowledge */
    if (op == SX_SQRT && n == 1 && vis_symexpr(sa[0])) {
        SymExpr *inner = as_symexpr(sa[0]);
        if (inner->op == SX_EXPT && inner->nargs == 2 && is_two(inner->args[1])) {
            val_t base = inner->args[0];
            if (sym_is_positive(base)) return base;         /* sqrt(x^2) = x  (x > 0) */
            return sx_simplify(sx_expr1(SX_ABS, base));     /* sqrt(x^2) = |x| */
        }
    }

    /* ---- Transcendentals on constants ---- */
    if (n == 1 && num_count == 1) {
        if (op == SX_SIN)  return num_sin(sa[0]);
        if (op == SX_COS)  return num_cos(sa[0]);
        if (op == SX_TAN)  return num_tan(sa[0]);
        if (op == SX_EXP)  return num_exp(sa[0]);
        if (op == SX_LOG)  return num_log(sa[0]);
        if (op == SX_ABS)  return num_abs(sa[0]);
        if (op == SX_SINH)  return num_sinh(sa[0]);
        if (op == SX_COSH)  return num_cosh(sa[0]);
        if (op == SX_TANH)  return num_tanh(sa[0]);
        if (op == SX_ASIN)  return num_asin(sa[0]);
        if (op == SX_ACOS)  return num_acos(sa[0]);
        if (op == SX_ATAN)  return num_atan(sa[0]);
        if (op == SX_ASINH) return num_asinh(sa[0]);
        if (op == SX_ACOSH) return num_acosh(sa[0]);
        if (op == SX_ATANH) return num_atanh(sa[0]);
        if (op == SX_COT)   return num_cot(sa[0]);
        if (op == SX_SEC)   return num_sec(sa[0]);
        if (op == SX_CSC)   return num_csc(sa[0]);
        if (op == SX_SIGN) {
            if (num_is_zero(sa[0]))     return vfix(0);
            if (num_is_negative(sa[0])) return vfix(-1);
            return vfix(1);
        }
    }

    /* ---- ABS — assumption-based simplification ---- */
    if (op == SX_ABS && n == 1) {
        val_t a = sa[0];
        if (sym_is_positive(a)) return a;              /* |x| = x  (x > 0) */
        if (sym_is_negative(a)) return sx_neg(a);      /* |x| = -x  (x < 0) */
        /* |(-x)| = |x| */
        if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG && as_symexpr(a)->nargs == 1)
            return sx_simplify(sx_expr1(SX_ABS, as_symexpr(a)->args[0]));
    }

    /* ---- LOG — pull exponent when base is positive ---- */
    if (op == SX_LOG && n == 1 && vis_symexpr(sa[0])) {
        SymExpr *inner = as_symexpr(sa[0]);
        if (inner->op == SX_EXPT && inner->nargs == 2 && sym_is_positive(inner->args[0]))
            return sx_simplify(sx_mul(inner->args[1], sx_log(inner->args[0]))); /* log(x^n) = n*log(x) */
    }

    /* ---- SIGN — assumption-based simplification ---- */
    if (op == SX_SIGN && n == 1) {
        val_t a = sa[0];
        if (sym_is_positive(a)) return vfix(1);
        if (sym_is_negative(a)) return vfix(-1);
    }

    /* ---- CONJ ---- */
    if (op == SX_CONJ && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_conjugate(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* conj(conj(f)) = f */
            if (iop == SX_CONJ) return as_symexpr(a)->args[0];
            /* conj(real(f)) = real(f)  — real-part is always real */
            if (iop == SX_REAL) return a;
            /* conj(imag(f)) = imag(f)  — imag-part is always real */
            if (iop == SX_IMAG) return a;
        }
    }

    /* ---- REAL ---- */
    if (op == SX_REAL && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_real_part(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* real(conj(f)) = real(f) */
            if (iop == SX_CONJ) return sx_real(as_symexpr(a)->args[0]);
            /* real(real(f)) = real(f) */
            if (iop == SX_REAL) return a;
            /* real(imag(f)) = imag(f)  — imag-part is real */
            if (iop == SX_IMAG) return a;
        }
    }

    /* ---- IMAG ---- */
    if (op == SX_IMAG && n == 1) {
        val_t a = sa[0];
        if (vis_number(a)) return num_imag_part(a);
        if (vis_symexpr(a)) {
            val_t iop = as_symexpr(a)->op;
            /* imag(conj(f)) = -imag(f) */
            if (iop == SX_CONJ) return sx_neg(sx_imag(as_symexpr(a)->args[0]));
            /* imag(real(f)) = 0 */
            if (iop == SX_REAL) return vfix(0);
            /* imag(imag(f)) = 0 */
            if (iop == SX_IMAG) return vfix(0);
        }
    }

    return sx_make_expr(op, n, sa);
}

/* ---- Symbolic arithmetic (these return simplified expressions) ---- */

/* Returns true if v is or contains a quaternion/octonion value or a
   quaternion-typed sym-var — signalling that products involving v are
   non-commutative and must use SX_NCMUL. */
static bool sx_is_nc(val_t v) {
    if (vis_quat(v) || vis_oct(v)) return true;
    if (vis_symvar(v)) return (sym_var_flags(v) & SYM_ASSUME_QUATERNION) != 0;
    if (!vis_symexpr(v)) return false;
    SymExpr *e = as_symexpr(v);
    if (e->op == SX_NCMUL) return true;
    for (uint32_t i = 0; i < e->nargs; i++)
        if (sx_is_nc(e->args[i])) return true;
    return false;
}

val_t sx_neg(val_t a) {
    if (vis_number(a)) return num_neg(a);
    return sx_simplify(sx_expr1(SX_NEG, a));
}
val_t sx_abs(val_t a) {
    if (vis_number(a)) return num_abs(a);
    return sx_simplify(sx_expr1(SX_ABS, a));
}
val_t sx_add(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_ADD, a, b)); }
val_t sx_sub(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_SUB, a, b)); }
val_t sx_mul(val_t a, val_t b) {
    if (sx_is_nc(a) || sx_is_nc(b)) return sx_ncmul(a, b);
    return sx_simplify(sx_expr2(SX_MUL, a, b));
}
val_t sx_ncmul(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_NCMUL, a, b)); }
val_t sx_div(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_DIV, a, b)); }
val_t sx_expt(val_t base, val_t exp) { return sx_simplify(sx_expr2(SX_EXPT, base, exp)); }
val_t sx_sqrt(val_t a) { return sx_simplify(sx_expr1(SX_SQRT, a)); }
val_t sx_sin(val_t a)  { return sx_simplify(sx_expr1(SX_SIN,  a)); }
val_t sx_cos(val_t a)  { return sx_simplify(sx_expr1(SX_COS,  a)); }
val_t sx_tan(val_t a)  { return sx_simplify(sx_expr1(SX_TAN,  a)); }
val_t sx_exp(val_t a)  { return sx_simplify(sx_expr1(SX_EXP,  a)); }
val_t sx_log(val_t a)  { return sx_simplify(sx_expr1(SX_LOG,  a)); }
val_t sx_sinh(val_t a)  { if (vis_number(a)) return num_sinh(a);  return sx_simplify(sx_expr1(SX_SINH,  a)); }
val_t sx_cosh(val_t a)  { if (vis_number(a)) return num_cosh(a);  return sx_simplify(sx_expr1(SX_COSH,  a)); }
val_t sx_tanh(val_t a)  { if (vis_number(a)) return num_tanh(a);  return sx_simplify(sx_expr1(SX_TANH,  a)); }
val_t sx_asin(val_t a)  { if (vis_number(a)) return num_asin(a);  return sx_simplify(sx_expr1(SX_ASIN,  a)); }
val_t sx_acos(val_t a)  { if (vis_number(a)) return num_acos(a);  return sx_simplify(sx_expr1(SX_ACOS,  a)); }
val_t sx_atan(val_t a)  { if (vis_number(a)) return num_atan(a);  return sx_simplify(sx_expr1(SX_ATAN,  a)); }
val_t sx_asinh(val_t a) { if (vis_number(a)) return num_asinh(a); return sx_simplify(sx_expr1(SX_ASINH, a)); }
val_t sx_acosh(val_t a) { if (vis_number(a)) return num_acosh(a); return sx_simplify(sx_expr1(SX_ACOSH, a)); }
val_t sx_atanh(val_t a) { if (vis_number(a)) return num_atanh(a); return sx_simplify(sx_expr1(SX_ATANH, a)); }
val_t sx_cot(val_t a)   { if (vis_number(a)) return num_cot(a);   return sx_simplify(sx_expr1(SX_COT,   a)); }
val_t sx_sec(val_t a)   { if (vis_number(a)) return num_sec(a);   return sx_simplify(sx_expr1(SX_SEC,   a)); }
val_t sx_csc(val_t a)   { if (vis_number(a)) return num_csc(a);   return sx_simplify(sx_expr1(SX_CSC,   a)); }

val_t sx_sign(val_t a) {
    if (vis_number(a)) {
        if (num_is_zero(a))     return vfix(0);
        if (num_is_negative(a)) return vfix(-1);
        return vfix(1);
    }
    return sx_simplify(sx_expr1(SX_SIGN, a));
}

val_t sx_conj(val_t a) {
    if (vis_number(a)) return num_conjugate(a);
    return sx_simplify(sx_expr1(SX_CONJ, a));
}
val_t sx_real(val_t a) {
    if (vis_number(a)) return num_real_part(a);
    return sx_simplify(sx_expr1(SX_REAL, a));
}
val_t sx_imag(val_t a) {
    if (vis_number(a)) return num_imag_part(a);
    return sx_simplify(sx_expr1(SX_IMAG, a));
}

/* ---- Symbolic function (T_SYMFN) derivative helpers ---- */

/* Create the partial derivative of fn w.r.t. param_var (a sym-var from fn's params).
 * Name: "<fn_name>_<param_name>" (e.g. u → u_x).  Inherits fn's params and records lineage. */
static val_t sx_diff_symfn(val_t fn, val_t param_var) {
    SymFn   *sf      = as_symfn(fn);
    Symbol  *fn_sym  = as_sym(sf->name);
    Symbol  *par_sym = as_sym(as_symvar(param_var)->name);
    /* Build "<fn_name>_<param_name>" */
    size_t  total    = fn_sym->len + 1 + par_sym->len;
    char   *buf      = (char *)gc_alloc(total + 1);
    memcpy(buf, fn_sym->data, fn_sym->len);
    buf[fn_sym->len] = '_';
    memcpy(buf + fn_sym->len + 1, par_sym->data, par_sym->len);
    buf[total] = '\0';
    val_t   dname = sym_intern_cstr(buf);
    SymFn  *d     = CURRY_NEW(SymFn);
    d->hdr.type   = T_SYMFN;
    d->hdr.flags  = 0;
    d->name       = dname;
    d->params     = sf->params;
    d->parent     = fn;
    d->d_param    = param_var;
    return vptr(d);
}

/* Like sx_diff_symfn but using an integer index when params are unavailable.
 * Name: "<fn_name>_<index>" (e.g. f_0, f_1). */
static val_t sx_diff_symfn_idx(val_t fn, int idx) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[128];
    snprintf(buf, sizeof(buf), "%.*s_%d", (int)fn_sym->len, fn_sym->data, idx);
    SymFn  *d    = CURRY_NEW(SymFn);
    d->hdr.type  = T_SYMFN;
    d->hdr.flags = 0;
    d->name      = sym_intern_cstr(buf);
    d->params    = sf->params;
    d->parent    = fn;
    d->d_param   = V_FALSE;
    return vptr(d);
}

/* ---- Symbolic differentiation ---- */

val_t sx_diff(val_t expr, val_t var) {
    /* var must be a T_SYMVAR */
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "∂: second argument must be a symbolic variable");

    /* Numbers differentiate to 0 */
    if (vis_number(expr)) return vfix(0);

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        return (as_symvar(expr)->name == as_symvar(var)->name) ? vfix(1) : vfix(0);
    }

    if (!vis_symexpr(expr)) return vfix(0);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* ---- d/dx (a + b + ...) = da/dx + db/dx + ... ---- */
    if (op == SX_ADD) {
        val_t *dargs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) dargs[i] = sx_diff(args[i], var);
        return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
    }

    /* ---- d/dx (a - b) = da/dx - db/dx ---- */
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_diff(args[0], var), sx_diff(args[1], var));

    /* ---- d/dx (-a) = -(da/dx) ---- */
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_diff(args[0], var));

    /* ---- Product rule: d/dx (f*g*h*...) = Σᵢ (product with fᵢ replaced by dfᵢ/dx) ---- */
    if (op == SX_MUL) {
        val_t *sum_terms = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) {
            val_t *factors = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? sx_diff(args[j], var) : args[j];
            sum_terms[i] = sx_simplify(sx_make_expr(SX_MUL, n, factors));
        }
        return sx_simplify(sx_make_expr(SX_ADD, n, sum_terms));
    }

    /* ---- NC product rule: order of remaining factors must be preserved ---- */
    if (op == SX_NCMUL) {
        val_t *sum_terms = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        int nterms = 0;
        for (int i = 0; i < n; i++) {
            val_t di = sx_diff(args[i], var);
            if (is_zero(di)) continue;
            val_t *factors = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? di : args[j];
            sum_terms[nterms++] = sx_simplify(sx_make_expr(SX_NCMUL, n, factors));
        }
        if (nterms == 0) return vfix(0);
        val_t result = sum_terms[0];
        for (int i = 1; i < nterms; i++) result = sx_add(result, sum_terms[i]);
        return result;
    }

    /* ---- Quotient rule: d/dx (f/g) = (f'g - fg') / g² ---- */
    if (op == SX_DIV && n == 2) {
        val_t f = args[0], g = args[1];
        val_t df = sx_diff(f, var), dg = sx_diff(g, var);
        if (is_zero(dg))
            return sx_div(df, g);   /* constant denominator: simpler form */
        return sx_div(sx_sub(sx_mul(df, g), sx_mul(f, dg)),
                      sx_mul(g, g));
    }

    /* ---- Power rule: d/dx (f^n) = n * f^(n-1) * f' ---- */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp = args[1];
        if (vis_number(exp)) {
            val_t n1 = num_sub(exp, vfix(1));
            return sx_mul(sx_mul(exp, sx_expt(base, n1)), sx_diff(base, var));
        }
        /* General: d/dx[f^g] = f^g * (g' ln f + g f'/f) */
        val_t df = sx_diff(base, var), dg = sx_diff(exp, var);
        val_t term1 = sx_mul(dg, sx_log(base));
        val_t term2 = sx_mul(exp, sx_div(df, base));
        return sx_mul(expr, sx_add(term1, term2));
    }

    /* ---- Transcendentals (chain rule) ---- */
    if (op == SX_SIN && n == 1)
        return sx_mul(sx_cos(args[0]), sx_diff(args[0], var));

    if (op == SX_COS && n == 1)
        return sx_neg(sx_mul(sx_sin(args[0]), sx_diff(args[0], var)));

    if (op == SX_TAN && n == 1) {
        /* d/dx tan(f) = f' / cos²(f) */
        val_t cos2 = sx_expt(sx_cos(args[0]), vfix(2));
        return sx_div(sx_diff(args[0], var), cos2);
    }

    if (op == SX_EXP && n == 1)
        return sx_mul(expr, sx_diff(args[0], var));

    if (op == SX_LOG && n == 1)
        return sx_div(sx_diff(args[0], var), args[0]);

    if (op == SX_SQRT && n == 1) {
        /* d/dx √f = f' / (2√f) */
        return sx_div(sx_diff(args[0], var),
                      sx_mul(vfix(2), sx_sqrt(args[0])));
    }

    if (op == SX_ABS && n == 1) {
        /* d/dx |f| = f * f' / |f|  (undefined at 0) */
        return sx_div(sx_mul(args[0], sx_diff(args[0], var)), expr);
    }

    /* ---- Hyperbolic (chain rule) ---- */
    if (op == SX_SINH && n == 1)
        return sx_mul(sx_cosh(args[0]), sx_diff(args[0], var));

    if (op == SX_COSH && n == 1)
        return sx_mul(sx_sinh(args[0]), sx_diff(args[0], var));

    if (op == SX_TANH && n == 1)
        return sx_div(sx_diff(args[0], var), sx_expt(sx_cosh(args[0]), vfix(2)));

    /* ---- Inverse trig (chain rule) ---- */
    if (op == SX_ASIN && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2)))));

    if (op == SX_ACOS && n == 1)
        return sx_neg(sx_div(sx_diff(args[0], var),
                             sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2))))));

    if (op == SX_ATAN && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_add(vfix(1), sx_expt(args[0], vfix(2))));

    if (op == SX_ASINH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_add(sx_expt(args[0], vfix(2)), vfix(1))));

    if (op == SX_ACOSH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sqrt(sx_sub(sx_expt(args[0], vfix(2)), vfix(1))));

    if (op == SX_ATANH && n == 1)
        return sx_div(sx_diff(args[0], var),
                      sx_sub(vfix(1), sx_expt(args[0], vfix(2))));

    /* ---- Reciprocal trig (chain rule) ---- */
    if (op == SX_COT && n == 1)
        return sx_neg(sx_div(sx_diff(args[0], var),
                             sx_expt(sx_sin(args[0]), vfix(2))));

    if (op == SX_SEC && n == 1)
        return sx_mul(sx_mul(sx_sec(args[0]), sx_tan(args[0])),
                      sx_diff(args[0], var));

    if (op == SX_CSC && n == 1)
        return sx_neg(sx_mul(sx_mul(sx_csc(args[0]), sx_cot(args[0])),
                             sx_diff(args[0], var)));

    /* ---- Complex operators (x is a real variable) ---- */
    /* ∂conj(f)/∂x = conj(∂f/∂x) */
    if (op == SX_CONJ && n == 1)
        return sx_conj(sx_diff(args[0], var));

    /* ∂real(f)/∂x = real(∂f/∂x) */
    if (op == SX_REAL && n == 1)
        return sx_real(sx_diff(args[0], var));

    /* ∂imag(f)/∂x = imag(∂f/∂x) */
    if (op == SX_IMAG && n == 1)
        return sx_imag(sx_diff(args[0], var));

    /* ---- SX_APPLY: chain rule over function arguments ---- */
    if (op == SX_APPLY && n >= 1) {
        val_t fn      = args[0];  /* T_SYMFN */
        int   nf      = n - 1;   /* number of applied arguments */
        val_t *fargs  = args + 1;
        val_t params  = as_symfn(fn)->params;
        val_t sum     = vfix(0);
        for (int i = 0; i < nf; i++) {
            val_t darg = sx_diff(fargs[i], var);
            if (is_zero(darg)) continue;
            /* Find i-th param sym-var for derivative naming */
            val_t param_i = V_FALSE;
            val_t pl = params;
            for (int j = 0; vis_pair(pl); j++, pl = vcdr(pl)) {
                if (j == i) { param_i = vcar(pl); break; }
            }
            val_t d_fn = vis_symvar(param_i)
                       ? sx_diff_symfn(fn, param_i)
                       : sx_diff_symfn_idx(fn, i);
            val_t d_apply = sx_make_apply(d_fn, nf, fargs);
            val_t term    = sx_simplify(sx_mul(d_apply, darg));
            sum           = sx_simplify(sx_add(sum, term));
        }
        return sum;
    }

    /* Unknown op — return unevaluated ∂ notation */
    val_t diff_sym = sym_intern_cstr("∂");
    val_t d_args[2] = {expr, var};
    return sx_make_expr(diff_sym, 2, d_args);
}

/* ---- Wirtinger derivatives  ∂/∂z  and  ∂/∂z̄ ---- */

val_t sx_wirtinger(val_t expr, val_t var, bool is_dbar) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "wirtinger: second argument must be a symbolic variable");

    if (vis_number(expr)) return vfix(0);

    /* Variable: ∂z/∂z=1, ∂z/∂z̄=0 */
    if (vis_symvar(expr)) {
        return (as_symvar(expr)->name == as_symvar(var)->name)
               ? (is_dbar ? vfix(0) : vfix(1))
               : vfix(0);
    }

    if (!vis_symexpr(expr)) return vfix(0);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* KEY WIRTINGER RULES for conj ---------------------------------------- */
    if (op == SX_CONJ && n == 1) {
        /* conj(var): ∂conj(z)/∂z = 0,  ∂conj(z)/∂z̄ = 1 */
        if (vis_symvar(args[0]) &&
            as_symvar(args[0])->name == as_symvar(var)->name)
            return is_dbar ? vfix(1) : vfix(0);
        /* conj(f): ∂/∂z = conj(∂f/∂z̄),  ∂/∂z̄ = conj(∂f/∂z) */
        return sx_conj(sx_wirtinger(args[0], var, !is_dbar));
    }

    /* real(f) = ½(f + conj(f)):
       ∂real(f)/∂z  = ½(∂f/∂z  + conj(∂f/∂z̄))
       ∂real(f)/∂z̄ = ½(∂f/∂z̄ + conj(∂f/∂z))   */
    if (op == SX_REAL && n == 1) {
        val_t df   = sx_wirtinger(args[0], var, is_dbar);
        val_t dfb  = sx_wirtinger(args[0], var, !is_dbar);
        return sx_div(sx_add(df, sx_conj(dfb)), vfix(2));
    }

    /* imag(f) = (f - conj(f))/(2i):
       ∂imag(f)/∂z  = (∂f/∂z  - conj(∂f/∂z̄)) / (2i)
       ∂imag(f)/∂z̄ = (∂f/∂z̄ - conj(∂f/∂z))  / (2i)  */
    if (op == SX_IMAG && n == 1) {
        val_t df   = sx_wirtinger(args[0], var, is_dbar);
        val_t dfb  = sx_wirtinger(args[0], var, !is_dbar);
        val_t two_i = num_make_complex(vfix(0), vfix(2));
        return sx_div(sx_sub(df, sx_conj(dfb)), two_i);
    }

    /* Linearity -----------------------------------------------------------  */
    if (op == SX_ADD) {
        val_t *dargs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) dargs[i] = sx_wirtinger(args[i], var, is_dbar);
        return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
    }
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_wirtinger(args[0], var, is_dbar),
                      sx_wirtinger(args[1], var, is_dbar));
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_wirtinger(args[0], var, is_dbar));

    /* Product rule --------------------------------------------------------- */
    if (op == SX_MUL) {
        val_t *terms = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) {
            val_t *factors = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? sx_wirtinger(args[j], var, is_dbar) : args[j];
            terms[i] = sx_simplify(sx_make_expr(SX_MUL, n, factors));
        }
        return sx_simplify(sx_make_expr(SX_ADD, n, terms));
    }
    if (op == SX_NCMUL) {
        val_t *terms = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        int nterms = 0;
        for (int i = 0; i < n; i++) {
            val_t di = sx_wirtinger(args[i], var, is_dbar);
            if (is_zero(di)) continue;
            val_t *factors = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int j = 0; j < n; j++)
                factors[j] = (j == i) ? di : args[j];
            terms[nterms++] = sx_simplify(sx_make_expr(SX_NCMUL, n, factors));
        }
        if (nterms == 0) return vfix(0);
        val_t result = terms[0];
        for (int i = 1; i < nterms; i++) result = sx_add(result, terms[i]);
        return result;
    }

    /* Quotient rule -------------------------------------------------------- */
    if (op == SX_DIV && n == 2) {
        val_t f = args[0], g = args[1];
        val_t df = sx_wirtinger(f, var, is_dbar);
        val_t dg = sx_wirtinger(g, var, is_dbar);
        if (is_zero(dg)) return sx_div(df, g);
        return sx_div(sx_sub(sx_mul(df, g), sx_mul(f, dg)), sx_mul(g, g));
    }

    /* Power rule ----------------------------------------------------------- */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (vis_number(exp_v)) {
            val_t n1 = num_sub(exp_v, vfix(1));
            return sx_mul(sx_mul(exp_v, sx_expt(base, n1)),
                          sx_wirtinger(base, var, is_dbar));
        }
        val_t df = sx_wirtinger(base, var, is_dbar);
        val_t dg = sx_wirtinger(exp_v, var, is_dbar);
        return sx_mul(expr, sx_add(sx_mul(dg, sx_log(base)),
                                   sx_mul(exp_v, sx_div(df, base))));
    }

    /* Holomorphic transcendentals — chain rule (same for ∂/∂z and ∂/∂z̄) -- */
    if (op == SX_SIN && n == 1)
        return sx_mul(sx_cos(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_COS && n == 1)
        return sx_neg(sx_mul(sx_sin(args[0]), sx_wirtinger(args[0], var, is_dbar)));
    if (op == SX_TAN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_expt(sx_cos(args[0]), vfix(2)));
    if (op == SX_EXP && n == 1)
        return sx_mul(expr, sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_LOG && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar), args[0]);
    if (op == SX_SQRT && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_mul(vfix(2), sx_sqrt(args[0])));

    /* Holomorphic transcendentals — Phase 1 extensions */
    if (op == SX_SINH && n == 1)
        return sx_mul(sx_cosh(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_COSH && n == 1)
        return sx_mul(sx_sinh(args[0]), sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_TANH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_expt(sx_cosh(args[0]), vfix(2)));
    if (op == SX_ASIN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2)))));
    if (op == SX_ACOS && n == 1)
        return sx_neg(sx_div(sx_wirtinger(args[0], var, is_dbar),
                             sx_sqrt(sx_sub(vfix(1), sx_expt(args[0], vfix(2))))));
    if (op == SX_ATAN && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_add(vfix(1), sx_expt(args[0], vfix(2))));
    if (op == SX_ASINH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_add(sx_expt(args[0], vfix(2)), vfix(1))));
    if (op == SX_ACOSH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sqrt(sx_sub(sx_expt(args[0], vfix(2)), vfix(1))));
    if (op == SX_ATANH && n == 1)
        return sx_div(sx_wirtinger(args[0], var, is_dbar),
                      sx_sub(vfix(1), sx_expt(args[0], vfix(2))));
    if (op == SX_COT && n == 1)
        return sx_neg(sx_div(sx_wirtinger(args[0], var, is_dbar),
                             sx_expt(sx_sin(args[0]), vfix(2))));
    if (op == SX_SEC && n == 1)
        return sx_mul(sx_mul(sx_sec(args[0]), sx_tan(args[0])),
                      sx_wirtinger(args[0], var, is_dbar));
    if (op == SX_CSC && n == 1)
        return sx_neg(sx_mul(sx_mul(sx_csc(args[0]), sx_cot(args[0])),
                             sx_wirtinger(args[0], var, is_dbar)));

    /* Fallback: unevaluated */
    val_t wsym = sym_intern_cstr(is_dbar ? "∂z̄" : "∂z");
    val_t w_args[2] = {expr, var};
    return sx_make_expr(wsym, 2, w_args);
}

/* ---- Substitution ---- */

val_t sx_substitute(val_t expr, val_t var, val_t val) {
    if (vis_number(expr)) return expr;
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name) return val;
        return expr;
    }
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t *sargs = (val_t *)gc_alloc((size_t)se->nargs * sizeof(val_t));
        for (uint32_t i = 0; i < se->nargs; i++)
            sargs[i] = sx_substitute(se->args[i], var, val);
        return sx_simplify(sx_make_expr(se->op, (int)se->nargs, sargs));
    }
    return expr;
}

/* ---- Dependency test ---- */

bool sx_depends_on(val_t expr, val_t var) {
    if (vis_number(expr)) return false;
    if (vis_symvar(expr))
        return as_symvar(expr)->name == as_symvar(var)->name;
    if (vis_symfn(expr)) return false;  /* sym-fn is a function object, not a variable */
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        for (uint32_t i = 0; i < se->nargs; i++)
            if (sx_depends_on(se->args[i], var)) return true;
    }
    return false;
}

/* ---- Polynomial / structural operations ---- */

/* Distribute a*b when either operand is a sum. */
static val_t expand_mul2(val_t a, val_t b) {
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_ADD) {
        SymExpr *aa = as_symexpr(a);
        int m = (int)aa->nargs;
        val_t *terms = (val_t *)gc_alloc((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_mul2(aa->args[i], b);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_ADD) {
        SymExpr *ab = as_symexpr(b);
        int m = (int)ab->nargs;
        val_t *terms = (val_t *)gc_alloc((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_mul2(a, ab->args[i]);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    return sx_mul(a, b);
}

/* Non-commutative analogue of expand_mul2: distribute a⊗b maintaining order. */
static val_t expand_ncmul2(val_t a, val_t b) {
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_ADD) {
        SymExpr *aa = as_symexpr(a);
        int m = (int)aa->nargs;
        val_t *terms = (val_t *)gc_alloc((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_ncmul2(aa->args[i], b);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_ADD) {
        SymExpr *ab = as_symexpr(b);
        int m = (int)ab->nargs;
        val_t *terms = (val_t *)gc_alloc((size_t)m * sizeof(val_t));
        for (int i = 0; i < m; i++) terms[i] = expand_ncmul2(a, ab->args[i]);
        return sx_simplify(sx_make_expr(SX_ADD, m, terms));
    }
    return sx_ncmul(a, b);
}

val_t sx_expand(val_t expr) {
    if (!vis_symbolic(expr)) return expr;
    if (vis_symvar(expr)) return expr;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    val_t *ea = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) ea[i] = sx_expand(se->args[i]);

    if (op == SX_ADD)
        return sx_simplify(sx_make_expr(SX_ADD, n, ea));

    if (op == SX_SUB && n == 2)
        return sx_sub(ea[0], ea[1]);

    /* Push NEG into ADD */
    if (op == SX_NEG && n == 1) {
        val_t inner = ea[0];
        if (vis_symexpr(inner) && as_symexpr(inner)->op == SX_ADD) {
            SymExpr *ia = as_symexpr(inner);
            int m = (int)ia->nargs;
            val_t *neg_terms = (val_t *)gc_alloc((size_t)m * sizeof(val_t));
            for (int i = 0; i < m; i++) neg_terms[i] = sx_neg(ia->args[i]);
            return sx_simplify(sx_make_expr(SX_ADD, m, neg_terms));
        }
        return sx_neg(inner);
    }

    /* Distribute MUL over ADD, folding left */
    if (op == SX_MUL) {
        val_t acc = ea[0];
        for (int i = 1; i < n; i++) acc = expand_mul2(acc, ea[i]);
        return acc;
    }

    /* Distribute NCMUL over ADD preserving order */
    if (op == SX_NCMUL) {
        val_t acc = ea[0];
        for (int i = 1; i < n; i++) acc = expand_ncmul2(acc, ea[i]);
        return acc;
    }

    /* Expand integer exponents 2..16 by repeated multiplication */
    if (op == SX_EXPT && n == 2) {
        val_t base = ea[0], exp_v = ea[1];
        if (vis_fixnum(exp_v)) {
            long e = vunfix(exp_v);
            if (e == 0) return vfix(1);
            if (e == 1) return base;
            if (e >= 2 && e <= 16) {
                val_t acc = base;
                for (long i = 1; i < e; i++)
                    acc = sx_is_nc(acc) ? expand_ncmul2(acc, base) : expand_mul2(acc, base);
                return acc;
            }
        }
    }

    return sx_simplify(sx_make_expr(op, n, ea));
}

/* Internal: degree as a C long (−1 for transcendentals of var, 0 for constants). */
static long sx_degree_long(val_t expr, val_t var) {
    if (vis_number(expr)) return 0;
    if (vis_symvar(expr))
        return (as_symvar(expr)->name == as_symvar(var)->name) ? 1 : 0;
    if (!vis_symexpr(expr)) return 0;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    if (op == SX_ADD) {
        long mx = 0;
        for (int i = 0; i < n; i++) {
            long d = sx_degree_long(args[i], var);
            if (d > mx) mx = d;
        }
        return mx;
    }
    if (op == SX_SUB && n == 2) {
        long d0 = sx_degree_long(args[0], var);
        long d1 = sx_degree_long(args[1], var);
        return d0 > d1 ? d0 : d1;
    }
    if (op == SX_NEG && n == 1)
        return sx_degree_long(args[0], var);
    if (op == SX_MUL || op == SX_NCMUL) {
        long s = 0;
        for (int i = 0; i < n; i++) s += sx_degree_long(args[i], var);
        return s;
    }
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (vis_fixnum(exp_v) && vunfix(exp_v) >= 0)
            return sx_degree_long(base, var) * vunfix(exp_v);
    }
    return 0;
}

val_t sx_degree(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "degree: second argument must be a symbolic variable");
    return vfix(sx_degree_long(expr, var));
}

/*
 * Decompose a single monomial term into (coefficient × var^degree).
 * Returns true and fills *coeff / *deg on success.
 * Returns false if the term is not a recognizable monomial in var.
 */
static bool decomp_monomial(val_t term, val_t var, val_t *coeff, long *deg) {
    if (vis_number(term)) { *coeff = term; *deg = 0; return true; }
    if (vis_symvar(term)) {
        if (as_symvar(term)->name == as_symvar(var)->name)
            { *coeff = vfix(1); *deg = 1; }
        else
            { *coeff = term; *deg = 0; }
        return true;
    }
    if (!vis_symexpr(term)) { *coeff = term; *deg = 0; return true; }

    SymExpr *se = as_symexpr(term);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    if (op == SX_NEG && n == 1) {
        if (decomp_monomial(args[0], var, coeff, deg)) {
            *coeff = sx_neg(*coeff);
            return true;
        }
        return false;
    }

    /* (expt var k) */
    if (op == SX_EXPT && n == 2 &&
        sx_equal(args[0], var) && vis_fixnum(args[1])) {
        *coeff = vfix(1); *deg = vunfix(args[1]); return true;
    }

    if (op == SX_MUL) {
        long degree = 0;
        val_t coeff_acc = vfix(1);
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            val_t f = args[i];
            if (!sx_depends_on(f, var)) {
                coeff_acc = sx_mul(coeff_acc, f);
            } else if (vis_symvar(f) &&
                       as_symvar(f)->name == as_symvar(var)->name) {
                degree += 1;
            } else if (vis_symexpr(f) && as_symexpr(f)->op == SX_EXPT &&
                       (int)as_symexpr(f)->nargs == 2 &&
                       sx_equal(as_symexpr(f)->args[0], var) &&
                       vis_fixnum(as_symexpr(f)->args[1])) {
                degree += vunfix(as_symexpr(f)->args[1]);
            } else {
                ok = false;
            }
        }
        if (ok) { *coeff = coeff_acc; *deg = degree; return true; }
    }

    if (!sx_depends_on(term, var)) { *coeff = term; *deg = 0; return true; }
    return false;
}

val_t sx_collect(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "collect: second argument must be a symbolic variable");

    val_t expanded = sx_expand(expr);

    /* Flatten additive terms */
    int nterms;
    val_t *terms;
    val_t single[1];
    if (vis_symexpr(expanded) && as_symexpr(expanded)->op == SX_ADD) {
        SymExpr *se = as_symexpr(expanded);
        nterms = (int)se->nargs;
        terms  = se->args;
    } else {
        single[0] = expanded; nterms = 1; terms = single;
    }

    /* degree → accumulated coefficient table */
    long  *degs    = (long  *)gc_alloc((size_t)nterms * sizeof(long));
    val_t *coeffs  = (val_t *)gc_alloc((size_t)nterms * sizeof(val_t));
    val_t *unc     = (val_t *)gc_alloc((size_t)nterms * sizeof(val_t));
    int ndeg = 0, nunc = 0;

    for (int i = 0; i < nterms; i++) {
        val_t c; long d;
        if (decomp_monomial(terms[i], var, &c, &d)) {
            int found = -1;
            for (int j = 0; j < ndeg; j++) if (degs[j] == d) { found = j; break; }
            if (found >= 0) coeffs[found] = sx_add(coeffs[found], c);
            else { degs[ndeg] = d; coeffs[ndeg] = c; ndeg++; }
        } else {
            unc[nunc++] = terms[i];
        }
    }

    /* Sort by descending degree */
    for (int i = 0; i < ndeg - 1; i++) {
        for (int j = 0; j < ndeg - i - 1; j++) {
            if (degs[j] < degs[j+1]) {
                long td = degs[j]; degs[j] = degs[j+1]; degs[j+1] = td;
                val_t tc = coeffs[j]; coeffs[j] = coeffs[j+1]; coeffs[j+1] = tc;
            }
        }
    }

    /* Build result terms */
    val_t *result = (val_t *)gc_alloc((size_t)(ndeg + nunc) * sizeof(val_t));
    int ri = 0;
    for (int i = 0; i < ndeg; i++) {
        val_t c = coeffs[i];
        long d  = degs[i];
        if (vis_number(c) && num_is_zero(c)) continue;
        if (d == 0)      result[ri++] = c;
        else if (d == 1) result[ri++] = is_one(c) ? var : sx_mul(c, var);
        else {
            val_t vp = sx_expt(var, vfix(d));
            result[ri++] = is_one(c) ? vp : sx_mul(c, vp);
        }
    }
    for (int i = 0; i < nunc; i++) result[ri++] = unc[i];

    if (ri == 0) return vfix(0);
    if (ri == 1) return result[0];
    return sx_simplify(sx_make_expr(SX_ADD, ri, result));
}

val_t sx_leading_coeff(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "leading-coeff: second argument must be a symbolic variable");

    long target = sx_degree_long(expr, var);
    val_t expanded = sx_expand(expr);

    int nterms;
    val_t *terms;
    val_t single[1];
    if (vis_symexpr(expanded) && as_symexpr(expanded)->op == SX_ADD) {
        SymExpr *se = as_symexpr(expanded);
        nterms = (int)se->nargs;
        terms  = se->args;
    } else {
        single[0] = expanded; nterms = 1; terms = single;
    }

    val_t acc = vfix(0);
    for (int i = 0; i < nterms; i++) {
        val_t c; long d;
        if (decomp_monomial(terms[i], var, &c, &d) && d == target)
            acc = sx_add(acc, c);
    }
    return acc;
}

/* ---- Symbolic integration ---- */

val_t sx_integrate(val_t expr, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "∫: second argument must be a symbolic variable");

    /* ∫c dx = c*x */
    if (vis_number(expr))
        return sx_mul(expr, var);

    /* ∫x dx = x²/2,   ∫y dx = y*x  (y is constant wrt x) */
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name)
            return sx_div(sx_expt(var, vfix(2)), vfix(2));
        return sx_mul(expr, var);
    }

    if (!vis_symexpr(expr)) return sx_mul(expr, var);

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;
    val_t *args = se->args;

    /* Linearity: ∫(f+g+...) = ∫f + ∫g + ... */
    if (op == SX_ADD) {
        val_t *iargs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
        for (int i = 0; i < n; i++) iargs[i] = sx_integrate(args[i], var);
        return sx_simplify(sx_make_expr(SX_ADD, n, iargs));
    }

    /* ∫(a - b) = ∫a - ∫b */
    if (op == SX_SUB && n == 2)
        return sx_sub(sx_integrate(args[0], var), sx_integrate(args[1], var));

    /* ∫(-f) = -(∫f) */
    if (op == SX_NEG && n == 1)
        return sx_neg(sx_integrate(args[0], var));

    /* Constant multiple: pull out factors that don't depend on var */
    if (op == SX_MUL) {
        int ndep = 0, nconst = 0;
        for (int i = 0; i < n; i++) {
            if (sx_depends_on(args[i], var)) ndep++;
            else nconst++;
        }
        if (nconst == n)
            return sx_mul(expr, var);
        if (nconst > 0) {
            val_t *consts = (val_t *)gc_alloc((size_t)nconst * sizeof(val_t));
            val_t *deps   = (val_t *)gc_alloc((size_t)ndep   * sizeof(val_t));
            int ci = 0, di = 0;
            for (int i = 0; i < n; i++) {
                if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                else consts[ci++] = args[i];
            }
            val_t cfactor = (nconst == 1) ? consts[0] : sx_make_expr(SX_MUL, nconst, consts);
            val_t dpart   = (ndep   == 1) ? deps[0]   : sx_make_expr(SX_MUL, ndep, deps);
            return sx_mul(cfactor, sx_integrate(dpart, var));
        }
        /* All factors depend on var: try IBP.
         * Priority: log factors (LIATE rule — differentiate log, integrate rest);
         * then polynomial (x^n) factors (differentiate poly, integrate rest). */
        {
            int log_idx = -1;
            int poly_idx = -1;
            for (int i = 0; i < n; i++) {
                val_t a = args[i];
                /* LOG factor: use LIATE — differentiate log, integrate algebraic */
                if (log_idx < 0 && vis_symexpr(a) &&
                    as_symexpr(a)->op == SX_LOG && as_symexpr(a)->nargs == 1)
                    log_idx = i;
                /* Polynomial factor: var itself or var^k for positive integer k */
                if (poly_idx < 0) {
                    if (sx_equal(a, var)) {
                        poly_idx = i;
                    } else if (vis_symexpr(a) &&
                               as_symexpr(a)->op == SX_EXPT &&
                               as_symexpr(a)->nargs == 2 &&
                               sx_equal(as_symexpr(a)->args[0], var) &&
                               vis_fixnum(as_symexpr(a)->args[1]) &&
                               vunfix(as_symexpr(a)->args[1]) > 0) {
                        poly_idx = i;
                    }

                }
            }
            /* Choose which factor to differentiate (u) and which to integrate (f) */
            int u_idx = -1;
            if (log_idx >= 0)  u_idx = log_idx;
            else if (poly_idx >= 0) u_idx = poly_idx;

            /* Direct formula for log × var^k: avoids unsimplified intermediate fractions.
             * ∫ var^k · ln(f) dx = var^(k+1)·ln(f)/(k+1) - ∫ var^(k+1)/(k+1) · f'/f dx */
            if (log_idx >= 0 && poly_idx >= 0 && n == 2) {
                val_t log_expr = args[log_idx];
                val_t log_arg  = as_symexpr(log_expr)->args[0];
                val_t poly_f   = args[poly_idx];
                /* Determine k: var → 1, expt(var, k) → k */
                long k;
                if (sx_equal(poly_f, var)) {
                    k = 1;
                } else {
                    k = vunfix(as_symexpr(poly_f)->args[1]);
                }
                val_t kp1    = vfix(k + 1);
                val_t kp1_sq = num_mul(kp1, kp1);
                val_t vpow   = sx_expt(var, kp1);
                /* ∫ var^(k+1) * f'/(f*(k+1)) dx — try integration */
                val_t df_log = sx_simplify(sx_diff(log_arg, var));
                if (vis_number(df_log) && !num_is_zero(df_log)) {
                    /* inner integrand = var^(k+1) * df_log / (log_arg * (k+1)) */
                    val_t tail_expr = sx_div(sx_mul(vpow, df_log),
                                            sx_mul(log_arg, kp1));
                    val_t tail = sx_integrate(tail_expr, var);
                    if (!(vis_symexpr(tail) && as_symexpr(tail)->op == SX_INTEGRATE)) {
                        val_t head = sx_div(sx_mul(vpow, log_expr), kp1);
                        return sx_simplify(sx_sub(head, tail));
                    }
                    /* Fallback for log(x): direct closed form */
                    if (sx_equal(log_arg, var)) {
                        /* ∫ x^k · ln(x) dx = x^(k+1)·ln(x)/(k+1) - x^(k+1)/(k+1)^2 */
                        val_t head2 = sx_div(sx_mul(vpow, log_expr), kp1);
                        val_t tail2 = sx_div(vpow, kp1_sq);
                        return sx_simplify(sx_sub(head2, tail2));
                    }
                }
            }

            if (u_idx >= 0) {
                val_t u = args[u_idx];
                /* f_part = product of all factors except u */
                val_t f_part;
                if (n == 2) {
                    f_part = args[1 - u_idx];
                } else {
                    val_t *rargs = (val_t *)gc_alloc((size_t)(n - 1) * sizeof(val_t));
                    int ri = 0;
                    for (int i = 0; i < n; i++)
                        if (i != u_idx) rargs[ri++] = args[i];
                    f_part = sx_make_expr(SX_MUL, n - 1, rargs);
                }
                /* v = ∫f_part dx */
                val_t v = sx_integrate(f_part, var);
                /* Guard: if v is still unevaluated, give up */
                if (!(vis_symexpr(v) && as_symexpr(v)->op == SX_INTEGRATE)) {
                    /* IBP: ∫u·f dx = u·v - ∫v·du dx
                     * Multiply fractions explicitly to avoid unsimplified (a/b)*(c/d) */
                    val_t du = sx_simplify(sx_diff(u, var));
                    if (vis_number(du) && num_is_zero(du))
                        return sx_mul(u, v);
                    val_t inner;
                    if (vis_symexpr(v) && as_symexpr(v)->op == SX_DIV &&
                        as_symexpr(v)->nargs == 2 &&
                        vis_symexpr(du) && as_symexpr(du)->op == SX_DIV &&
                        as_symexpr(du)->nargs == 2) {
                        /* (a/b) · (c/d) → (a*c)/(b*d) */
                        val_t vn = as_symexpr(v)->args[0], vd = as_symexpr(v)->args[1];
                        val_t dn = as_symexpr(du)->args[0], dd = as_symexpr(du)->args[1];
                        inner = sx_simplify(sx_div(sx_simplify(sx_mul(vn, dn)),
                                                   sx_simplify(sx_mul(vd, dd))));
                    } else {
                        inner = sx_simplify(sx_mul(v, du));
                    }
                    val_t tail  = sx_integrate(inner, var);
                    return sx_simplify(sx_sub(sx_mul(u, v), tail));
                }
            }
        }
    }

    /* NCMUL: factor out a leading real-scalar constant, leave quaternion-prefixed
       products unevaluated (quaternion constants don't commute past the integrand) */
    if (op == SX_NCMUL) {
        /* Partition factors into a leading constant block, a variable block, and
           a trailing constant block.  If the variable block integrates cleanly,
           return  leading ⊗ ∫(var_block) ⊗ trailing. */
        int first_var = n, last_var = -1;
        for (int i = 0; i < n; i++) {
            if (sx_depends_on(args[i], var)) {
                if (i < first_var) first_var = i;
                last_var = i;
            }
        }
        if (first_var == n) {
            /* No factors depend on var: whole product is constant */
            val_t whole = n == 1 ? args[0] : sx_make_expr(SX_NCMUL, n, args);
            return sx_ncmul(whole, var);
        }
        if (first_var > 0 || last_var < n - 1) {
            /* Build the var-dependent sub-product and integrate it */
            int vn = last_var - first_var + 1;
            val_t var_part = vn == 1 ? args[first_var]
                           : sx_make_expr(SX_NCMUL, vn, args + first_var);
            val_t ivar = sx_integrate(var_part, var);
            if (!vis_symexpr(ivar) || as_symexpr(ivar)->op != SX_INTEGRATE) {
                /* leading ⊗ ivar ⊗ trailing */
                val_t result = ivar;
                if (last_var < n - 1) {
                    val_t *trail = args + last_var + 1;
                    int tn = n - last_var - 1;
                    val_t tail = tn == 1 ? trail[0] : sx_make_expr(SX_NCMUL, tn, trail);
                    result = sx_ncmul(result, tail);
                }
                if (first_var > 0) {
                    val_t head = first_var == 1 ? args[0]
                               : sx_make_expr(SX_NCMUL, first_var, args);
                    result = sx_ncmul(head, result);
                }
                return result;
            }
        }
    }

    /* Power rule: ∫f^n dx where n is numeric */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (!sx_depends_on(exp_v, var) && vis_number(exp_v)) {
            /* ∫sec²(f) dx = tan(f)/f',  ∫csc²(f) dx = -cot(f)/f'
             * ∫sin²(f) dx = x/2 - sin(2f)/(4f'),  ∫cos²(f) dx = x/2 + sin(2f)/(4f') */
            if (vis_symexpr(base) && num_eq(exp_v, vfix(2))) {
                val_t bop = as_symexpr(base)->op;
                if (as_symexpr(base)->nargs == 1) {
                    val_t f  = as_symexpr(base)->args[0];
                    val_t df = sx_diff(f, var);
                    if (vis_number(df) && !num_is_zero(df)) {
                        if (bop == SX_SEC)
                            return sx_div(sx_tan(f), df);
                        if (bop == SX_CSC)
                            return sx_div(sx_neg(sx_cot(f)), df);
                        if (bop == SX_SIN) {
                            val_t sin2f = sx_sin(sx_mul(vfix(2), f));
                            return sx_sub(sx_div(var, vfix(2)),
                                          sx_div(sin2f, sx_mul(vfix(4), df)));
                        }
                        if (bop == SX_COS) {
                            val_t sin2f = sx_sin(sx_mul(vfix(2), f));
                            return sx_add(sx_div(var, vfix(2)),
                                          sx_div(sin2f, sx_mul(vfix(4), df)));
                        }
                    }
                }
            }
            if (sx_equal(base, var)) {
                /* ∫x^n dx */
                if (num_eq(exp_v, vfix(-1)))
                    return sx_log(sx_abs(var));
                val_t np1 = num_add(exp_v, vfix(1));
                return sx_div(sx_expt(var, np1), np1);
            }
            /* ∫(ax+b)^n dx via linear substitution */
            val_t df = sx_diff(base, var);
            if (vis_number(df) && !num_is_zero(df)) {
                if (num_eq(exp_v, vfix(-1)))
                    return sx_div(sx_log(sx_abs(base)), df);
                val_t np1 = num_add(exp_v, vfix(1));
                return sx_div(sx_expt(base, np1), sx_mul(df, np1));
            }
        }
    }

    /* ∫ num/den dx — linear and quadratic denominator cases */
    if (op == SX_DIV && n == 2) {
        val_t num_v = args[0], den = args[1];
        if (!sx_depends_on(num_v, var)) {
            /* Linear: f' is constant → ln|f|/f' */
            val_t df = sx_diff(den, var);
            if (vis_number(df) && !num_is_zero(df))
                return sx_mul(sx_div(num_v, df), sx_log(sx_abs(den)));

            /* Quadratic: extract a, b, c from ax²+bx+c via point evaluation */
            if (sx_depends_on(den, var)) {
                val_t deg = sx_degree(den, var);
                if (vis_fixnum(deg) && vunfix(deg) == 2) {
                    val_t at0  = sx_simplify(sx_substitute(den, var, vfix(0)));
                    val_t at1  = sx_simplify(sx_substitute(den, var, vfix(1)));
                    val_t atn1 = sx_simplify(sx_substitute(den, var, vfix(-1)));
                    if (vis_number(at0) && vis_number(at1) && vis_number(atn1)) {
                        val_t c_v = at0;
                        val_t b_v = num_div(num_sub(at1, atn1), vfix(2));
                        val_t a_v = num_sub(num_sub(at1, b_v), c_v);
                        /* disc = 4ac - b² */
                        val_t disc = num_sub(num_mul(num_mul(vfix(4), a_v), c_v),
                                             num_mul(b_v, b_v));
                        if (vis_number(disc)) {
                            double disc_d = num_to_double(disc);
                            if (disc_d > 0.0) {
                                /* ∫ num/(ax²+bx+c) dx = 2·num/√disc · atan((2ax+b)/√disc) */
                                val_t sq = sx_sqrt(disc);
                                val_t inner = sx_div(sx_add(sx_mul(num_mul(vfix(2), a_v), var), b_v), sq);
                                return sx_mul(sx_div(num_mul(vfix(2), num_v), sq),
                                              sx_atan(inner));
                            }
                        }
                    }
                }
            }
        }
    }

    /* ∫sin(f) dx = -cos(f) / f'  (linear f) */
    if (op == SX_SIN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_cos(args[0])), df);
    }

    /* ∫cos(f) dx = sin(f) / f'  (linear f) */
    if (op == SX_COS && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_sin(args[0]), df);
    }

    /* ∫tan(f) dx = -ln|cos(f)| / f'  (linear f) */
    if (op == SX_TAN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_log(sx_abs(sx_cos(args[0])))), df);
    }

    /* ∫exp(f) dx = exp(f) / f'  (linear f) */
    if (op == SX_EXP && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(expr, df);
    }

    /* ∫ln(f) dx = (f*ln(f) - f) / f'  (linear f, from integration by parts) */
    if (op == SX_LOG && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f = args[0];
            return sx_div(sx_sub(sx_mul(f, sx_log(f)), f), df);
        }
    }

    /* ∫sqrt(f) dx = 2*f^(3/2) / (3*f')  (linear f) */
    if (op == SX_SQRT && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t three_halves = num_div(vfix(3), vfix(2));
            return sx_div(sx_mul(vfix(2), sx_expt(args[0], three_halves)),
                          sx_mul(vfix(3), df));
        }
    }

    /* ---- Complex operators (x is a real variable) ---- */
    /* ∫conj(f) dx = conj(∫f dx) */
    if (op == SX_CONJ && n == 1)
        return sx_conj(sx_integrate(args[0], var));

    /* ∫real(f) dx = real(∫f dx) */
    if (op == SX_REAL && n == 1)
        return sx_real(sx_integrate(args[0], var));

    /* ∫imag(f) dx = imag(∫f dx) */
    if (op == SX_IMAG && n == 1)
        return sx_imag(sx_integrate(args[0], var));

    /* ---- Hyperbolic integrals (linear argument) ---- */
    if (op == SX_SINH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_cosh(args[0]), df);
    }
    if (op == SX_COSH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_sinh(args[0]), df);
    }
    if (op == SX_TANH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_cosh(args[0])), df);
    }

    /* ---- Reciprocal trig integrals (linear argument) ---- */
    if (op == SX_COT && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_abs(sx_sin(args[0]))), df);
    }
    if (op == SX_SEC && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_log(sx_abs(sx_add(sx_sec(args[0]), sx_tan(args[0])))), df);
    }
    if (op == SX_CSC && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df))
            return sx_div(sx_neg(sx_log(sx_abs(sx_add(sx_csc(args[0]), sx_cot(args[0]))))), df);
    }

    /* ---- Inverse trig integrals — IBP results (linear argument) ---- */
    if (op == SX_ASIN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_add(sx_mul(f, sx_asin(f)),
                               sx_sqrt(sx_sub(vfix(1), sx_expt(f, vfix(2)))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ACOS && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_acos(f)),
                               sx_sqrt(sx_sub(vfix(1), sx_expt(f, vfix(2)))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ATAN && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_atan(f)),
                               sx_div(sx_log(sx_add(vfix(1), sx_expt(f, vfix(2)))), vfix(2)));
            return sx_div(res, df);
        }
    }
    if (op == SX_ASINH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_asinh(f)),
                               sx_sqrt(sx_add(sx_expt(f, vfix(2)), vfix(1))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ACOSH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_sub(sx_mul(f, sx_acosh(f)),
                               sx_sqrt(sx_sub(sx_expt(f, vfix(2)), vfix(1))));
            return sx_div(res, df);
        }
    }
    if (op == SX_ATANH && n == 1) {
        val_t df = sx_diff(args[0], var);
        if (vis_number(df) && !num_is_zero(df)) {
            val_t f   = args[0];
            val_t res = sx_add(sx_mul(f, sx_atanh(f)),
                               sx_div(sx_log(sx_sub(vfix(1), sx_expt(f, vfix(2)))), vfix(2)));
            return sx_div(res, df);
        }
    }

    /* Fallback: unevaluated (∫ expr var) */
    val_t i_args[2] = {expr, var};
    return sx_make_expr(SX_INTEGRATE, 2, i_args);
}

/* ---- Fractional calculus ---- */

/*
 * sx_fracdiff: Caputo fractional derivative D^α[expr] w.r.t. var.
 *
 * Rules (α numeric):
 *   D^0 f       = f                            (identity)
 *   D^1 f       = df/dx                        (ordinary derivative)
 *   D^n f       = d^n f / dx^n  (positive integer n, iterated)
 *   D^α c       = 0                             (constants vanish, Caputo)
 *   D^α x^n     = Γ(n+1)/Γ(n−α+1) · x^(n−α)  (power rule, n ≥ 0)
 *   D^α e^(λx)  = λ^α · e^(λx)               (eigenfunction, linear argument)
 *   D^α (f+g)   = D^α f + D^α g              (linearity)
 *   D^α (c·f)   = c · D^α f                  (constant factor)
 *   D^α (D^β f) = D^(α+β) f                  (composition)
 */
val_t sx_fracdiff(val_t expr, val_t alpha, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "frac-diff: third argument must be a symbolic variable");

    /* α=0: identity */
    if (vis_number(alpha) && num_is_zero(alpha)) return expr;

    /* α=1: ordinary derivative */
    if (vis_number(alpha) && num_is_one(alpha)) return sx_diff(expr, var);

    /* positive integer α: iterate */
    if (vis_fixnum(alpha)) {
        long n = vunfix(alpha);
        if (n > 1 && n <= 20) {
            val_t r = expr;
            for (long i = 0; i < n; i++) r = sx_diff(r, var);
            return r;
        }
    }

    /* Numbers → 0 (Caputo: constants have zero fractional derivative) */
    if (vis_number(expr)) return vfix(0);

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        /* D^α[x] = power rule with n=1 */
        if (as_symvar(expr)->name == as_symvar(var)->name && vis_number(alpha)) {
            double a = num_to_double(alpha);
            double coeff = tgamma(2.0) / tgamma(2.0 - a);
            val_t new_exp = num_sub(vfix(1), alpha);
            return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
        }
        /* D^α[y] = 0 for y independent of var */
        if (as_symvar(expr)->name != as_symvar(var)->name) return vfix(0);
    }

    if (!vis_symexpr(expr)) goto unevaluated;

    {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* Linearity: D^α(f+g+...) */
        if (op == SX_ADD) {
            val_t *dargs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++) dargs[i] = sx_fracdiff(args[i], alpha, var);
            return sx_simplify(sx_make_expr(SX_ADD, n, dargs));
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fracdiff(args[0], alpha, var),
                          sx_fracdiff(args[1], alpha, var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fracdiff(args[0], alpha, var));

        /* Constant multiple: pull out factors independent of var */
        if (op == SX_MUL) {
            int nconst = 0;
            for (int i = 0; i < n; i++)
                if (!sx_depends_on(args[i], var)) nconst++;
            if (nconst == n) return vfix(0);  /* whole product is constant */
            if (nconst > 0) {
                val_t *consts = (val_t *)gc_alloc((size_t)nconst * sizeof(val_t));
                val_t *deps   = (val_t *)gc_alloc((size_t)(n - nconst) * sizeof(val_t));
                int ci = 0, di = 0;
                for (int i = 0; i < n; i++) {
                    if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                    else consts[ci++] = args[i];
                }
                val_t cfactor = (nconst == 1) ? consts[0]
                                              : sx_make_expr(SX_MUL, nconst, consts);
                val_t dpart   = (di == 1) ? deps[0] : sx_make_expr(SX_MUL, di, deps);
                return sx_mul(cfactor, sx_fracdiff(dpart, alpha, var));
            }
        }

        /* Power rule: D^α[x^n] = Γ(n+1)/Γ(n−α+1) · x^(n−α) */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], exp_v = args[1];
            if (sx_equal(base, var) && vis_number(exp_v) && vis_number(alpha)) {
                double pn = num_to_double(exp_v);
                double a  = num_to_double(alpha);
                if (pn >= 0.0) {
                    double coeff = tgamma(pn + 1.0) / tgamma(pn - a + 1.0);
                    val_t new_exp = num_sub(exp_v, alpha);
                    return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
                }
            }
        }

        /* Exponential eigenfunction: D^α[e^(λx)] = λ^α · e^(λx)  (linear arg) */
        if (op == SX_EXP && n == 1 && vis_number(alpha)) {
            val_t df = sx_diff(args[0], var);  /* extract λ */
            if (vis_number(df) && !num_is_zero(df)) {
                double lambda = num_to_double(df);
                double a = num_to_double(alpha);
                return sx_mul(num_make_float(pow(lambda, a)), expr);
            }
        }

        /* Composition: D^α[D^β[f, β, x], α, x] = D^(α+β)[f, x] */
        if (op == SX_FRACDIFF && n == 3 && sx_equal(args[2], var)) {
            val_t new_alpha = sx_simplify(sx_add(alpha, args[1]));
            return sx_fracdiff(args[0], new_alpha, var);
        }
    }

unevaluated:;
    val_t fargs[3] = {expr, alpha, var};
    return sx_make_expr(SX_FRACDIFF, 3, fargs);
}

/*
 * sx_fracint: Riemann-Liouville fractional integral I^α[expr] w.r.t. var.
 *
 * Rules (α numeric):
 *   I^0 f      = f                             (identity)
 *   I^1 f      = ∫f dx                         (ordinary integral)
 *   I^α c      = c · x^α / Γ(α+1)             (constant)
 *   I^α x^n    = Γ(n+1)/Γ(n+α+1) · x^(n+α)  (power rule, n ≥ 0)
 *   I^α e^(λx) = λ^(−α) · e^(λx)             (eigenfunction, linear arg)
 *   I^α (f+g)  = I^α f + I^α g               (linearity)
 *   I^α (c·f)  = c · I^α f                   (constant factor)
 */
val_t sx_fracint(val_t expr, val_t alpha, val_t var) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "frac-int: third argument must be a symbolic variable");

    /* α=0: identity */
    if (vis_number(alpha) && num_is_zero(alpha)) return expr;

    /* α=1: ordinary integral */
    if (vis_number(alpha) && num_is_one(alpha)) return sx_integrate(expr, var);

    /* positive integer α: iterate */
    if (vis_fixnum(alpha)) {
        long n = vunfix(alpha);
        if (n > 1 && n <= 20) {
            val_t r = expr;
            for (long i = 0; i < n; i++) r = sx_integrate(r, var);
            return r;
        }
    }

    /* Constant: I^α[c] = c · x^α / Γ(α+1) */
    if (vis_number(expr) && vis_number(alpha)) {
        double a = num_to_double(alpha);
        double denom = tgamma(a + 1.0);
        val_t coeff = (num_is_one(expr))
            ? num_make_float(1.0 / denom)
            : sx_div(expr, num_make_float(denom));
        return sx_mul(coeff, sx_expt(var, alpha));
    }

    /* Symbolic variable */
    if (vis_symvar(expr)) {
        if (as_symvar(expr)->name == as_symvar(var)->name && vis_number(alpha)) {
            /* I^α[x] = power rule with n=1 */
            double a  = num_to_double(alpha);
            double coeff = tgamma(2.0) / tgamma(2.0 + a);
            val_t new_exp = num_add(vfix(1), alpha);
            return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
        }
        if (as_symvar(expr)->name != as_symvar(var)->name && vis_number(alpha)) {
            /* I^α[c] where c is a different variable (treated as constant) */
            double a = num_to_double(alpha);
            double denom = tgamma(a + 1.0);
            return sx_mul(sx_div(expr, num_make_float(denom)), sx_expt(var, alpha));
        }
    }

    if (!vis_symexpr(expr)) goto unevaluated_i;

    {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* Linearity: I^α(f+g+...) */
        if (op == SX_ADD) {
            val_t *iargs = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++) iargs[i] = sx_fracint(args[i], alpha, var);
            return sx_simplify(sx_make_expr(SX_ADD, n, iargs));
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fracint(args[0], alpha, var),
                          sx_fracint(args[1], alpha, var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fracint(args[0], alpha, var));

        /* Constant multiple */
        if (op == SX_MUL) {
            int nconst = 0;
            for (int i = 0; i < n; i++)
                if (!sx_depends_on(args[i], var)) nconst++;
            if (nconst > 0 && nconst < n) {
                val_t *consts = (val_t *)gc_alloc((size_t)nconst * sizeof(val_t));
                val_t *deps   = (val_t *)gc_alloc((size_t)(n - nconst) * sizeof(val_t));
                int ci = 0, di = 0;
                for (int i = 0; i < n; i++) {
                    if (sx_depends_on(args[i], var)) deps[di++] = args[i];
                    else consts[ci++] = args[i];
                }
                val_t cfactor = (nconst == 1) ? consts[0]
                                              : sx_make_expr(SX_MUL, nconst, consts);
                val_t dpart   = (di == 1) ? deps[0] : sx_make_expr(SX_MUL, di, deps);
                return sx_mul(cfactor, sx_fracint(dpart, alpha, var));
            }
            if (nconst == n && vis_number(alpha)) {
                /* whole product is constant */
                double a = num_to_double(alpha);
                return sx_mul(sx_div(expr, num_make_float(tgamma(a + 1.0))),
                              sx_expt(var, alpha));
            }
        }

        /* Power rule: I^α[x^n] = Γ(n+1)/Γ(n+α+1) · x^(n+α) */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], exp_v = args[1];
            if (sx_equal(base, var) && vis_number(exp_v) && vis_number(alpha)) {
                double pn = num_to_double(exp_v);
                double a  = num_to_double(alpha);
                if (pn >= 0.0) {
                    double coeff = tgamma(pn + 1.0) / tgamma(pn + a + 1.0);
                    val_t new_exp = num_add(exp_v, alpha);
                    return sx_mul(num_make_float(coeff), sx_expt(var, new_exp));
                }
            }
        }

        /* Exponential eigenfunction: I^α[e^(λx)] = λ^(−α) · e^(λx) */
        if (op == SX_EXP && n == 1 && vis_number(alpha)) {
            val_t df = sx_diff(args[0], var);
            if (vis_number(df) && !num_is_zero(df)) {
                double lambda = num_to_double(df);
                double a = num_to_double(alpha);
                return sx_mul(num_make_float(pow(lambda, -a)), expr);
            }
        }

        /* Composition: I^α[I^β[f, β, x], α, x] = I^(α+β)[f, x] */
        if (op == SX_FRACINT && n == 3 && sx_equal(args[2], var)) {
            val_t new_alpha = sx_simplify(sx_add(alpha, args[1]));
            return sx_fracint(args[0], new_alpha, var);
        }
    }

unevaluated_i:;
    val_t iargs[3] = {expr, alpha, var};
    return sx_make_expr(SX_FRACINT, 3, iargs);
}

/* ---- Limits ---- */

/*
 * sx_ratio_simplify / sx_mul_for_ratio — aggressive ratio algebra used exclusively
 * by L'Hôpital to cancel the derivative quotient dp/dq.  NOT called from sx_simplify
 * to avoid rewrite loops (inverting a denominator would undo the 0·∞ rewrite).
 */
static val_t sx_ratio_simplify(val_t num, val_t den);

static val_t sx_mul_for_ratio(val_t a, val_t b) {
    /* (p/q)*b → ratio_simplify(p*b, q) */
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_DIV && as_symexpr(a)->nargs == 2)
        return sx_ratio_simplify(sx_mul_for_ratio(as_symexpr(a)->args[0], b),
                                 as_symexpr(a)->args[1]);
    if (vis_symexpr(a) && as_symexpr(a)->op == SX_NEG && as_symexpr(a)->nargs == 1)
        return sx_neg(sx_mul_for_ratio(as_symexpr(a)->args[0], b));
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_DIV && as_symexpr(b)->nargs == 2)
        return sx_ratio_simplify(sx_mul_for_ratio(a, as_symexpr(b)->args[0]),
                                 as_symexpr(b)->args[1]);
    if (vis_symexpr(b) && as_symexpr(b)->op == SX_NEG && as_symexpr(b)->nargs == 1)
        return sx_neg(sx_mul_for_ratio(a, as_symexpr(b)->args[0]));
    return sx_simplify(sx_expr2(SX_MUL, a, b));
}

static val_t sx_ratio_simplify(val_t num, val_t den) {
    /* Pull negation from denominator */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_NEG && as_symexpr(den)->nargs == 1)
        return sx_neg(sx_ratio_simplify(num, as_symexpr(den)->args[0]));
    if (vis_number(den) && num_is_negative(den))
        return sx_neg(sx_ratio_simplify(num, num_neg(den)));
    /* Pull negation from numerator */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_NEG && as_symexpr(num)->nargs == 1)
        return sx_neg(sx_ratio_simplify(as_symexpr(num)->args[0], den));
    if (vis_number(num) && num_is_negative(num))
        return sx_neg(sx_ratio_simplify(num_neg(num), den));
    /* num/(p/q) → (num*q)/p */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_DIV && as_symexpr(den)->nargs == 2) {
        val_t p = as_symexpr(den)->args[0], q = as_symexpr(den)->args[1];
        return sx_ratio_simplify(sx_mul_for_ratio(num, q), p);
    }
    /* (p/q)/den → p/(q*den) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_DIV && as_symexpr(num)->nargs == 2) {
        val_t p = as_symexpr(num)->args[0], q = as_symexpr(num)->args[1];
        return sx_ratio_simplify(p, sx_mul_for_ratio(q, den));
    }
    /* Cancel equal numerator and denominator */
    if (sx_equal(num, den)) return vfix(1);
    /* Factor cancellation: den appears as a factor inside a MUL numerator */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_MUL) {
        SymExpr *mul = as_symexpr(num);
        for (uint32_t i = 0; i < mul->nargs; i++) {
            if (sx_equal(mul->args[i], den)) {
                int nn = (int)mul->nargs - 1;
                if (nn == 0) return vfix(1);
                if (nn == 1) return sx_simplify((i == 0) ? mul->args[1] : mul->args[0]);
                val_t *nf = (val_t *)gc_alloc((size_t)nn * sizeof(val_t));
                int k = 0;
                for (uint32_t j = 0; j < mul->nargs; j++)
                    if (j != i) nf[k++] = mul->args[j];
                return sx_simplify(sx_make_expr(SX_MUL, nn, nf));
            }
        }
    }
    /* Power cancellations */
    /* x^n / x → x^(n-1) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_EXPT && as_symexpr(num)->nargs == 2 &&
        sx_equal(as_symexpr(num)->args[0], den))
        return sx_simplify(sx_expt(den, num_sub(as_symexpr(num)->args[1], vfix(1))));
    /* x / x^n → x^(1-n) */
    if (vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT && as_symexpr(den)->nargs == 2 &&
        sx_equal(num, as_symexpr(den)->args[0]))
        return sx_simplify(sx_expt(num, num_sub(vfix(1), as_symexpr(den)->args[1])));
    /* x^n / x^m → x^(n-m) */
    if (vis_symexpr(num) && as_symexpr(num)->op == SX_EXPT && as_symexpr(num)->nargs == 2 &&
        vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT && as_symexpr(den)->nargs == 2 &&
        sx_equal(as_symexpr(num)->args[0], as_symexpr(den)->args[0]))
        return sx_simplify(sx_expt(as_symexpr(num)->args[0],
                                   num_sub(as_symexpr(num)->args[1], as_symexpr(den)->args[1])));
    return sx_simplify(sx_expr2(SX_DIV, num, den));
}

/*
 * sx_limit_inner: recursive worker.  depth guards against infinite L'Hôpital loops.
 *
 * Strategy:
 *  1. Direct substitution — if result is a finite number, return it.
 *  2. For expr = p/q (SX_DIV):
 *       evaluate p and q at point separately (avoiding the 0/0→0 shortcut in simplify).
 *       - p=0, q≠0           → 0
 *       - p≠0, q=0            → ±∞  (unevaluated if sign is ambiguous)
 *       - finite p, infinite q → 0
 *       - 0/0 or ∞/∞          → L'Hôpital: retry limit(p'/q', x, point)
 *  3. Fallback: unevaluated (limit expr var point).
 */

#define LHOPITAL_MAX 5

static val_t sx_limit_inner(val_t expr, val_t var, val_t point, int dir, int depth);

static val_t sx_limit_unevaluated(val_t expr, val_t var, val_t point) {
    val_t args[3] = {expr, var, point};
    return sx_make_expr(SX_LIMIT, 3, args);
}

static val_t sx_limit_inner(val_t expr, val_t var, val_t point, int dir, int depth) {
    if (depth > LHOPITAL_MAX)
        return sx_limit_unevaluated(expr, var, point);

    /* Constant w.r.t. var */
    if (!sx_depends_on(expr, var))
        return sx_simplify(expr);

    /* Dispatch on form */
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *args = se->args;

        /* p/q — handle 0/0 and ∞/∞ via L'Hôpital */
        if (op == SX_DIV && n == 2) {
            val_t p = args[0], q = args[1];
            val_t pv = sx_simplify(sx_substitute(p, var, point));
            val_t qv = sx_simplify(sx_substitute(q, var, point));

            bool p_zero  = vis_number(pv) && num_is_zero(pv);
            bool q_zero  = vis_number(qv) && num_is_zero(qv);
            bool p_inf   = vis_flonum(pv) && isinf(num_to_double(pv));
            bool q_inf   = vis_flonum(qv) && isinf(num_to_double(qv));
            bool p_fin   = vis_number(pv) && !p_inf;
            bool q_fin   = vis_number(qv) && !q_inf;

            if (p_zero && q_zero) {
                /* 0/0: L'Hôpital */
                val_t dp = sx_simplify(sx_diff(p, var));
                val_t dq = sx_simplify(sx_diff(q, var));
                return sx_limit_inner(sx_ratio_simplify(dp, dq), var, point, dir, depth + 1);
            }
            if (p_inf && q_inf) {
                /* ∞/∞: L'Hôpital */
                val_t dp = sx_simplify(sx_diff(p, var));
                val_t dq = sx_simplify(sx_diff(q, var));
                return sx_limit_inner(sx_ratio_simplify(dp, dq), var, point, dir, depth + 1);
            }
            if (p_fin && q_inf)
                return vfix(0);  /* finite/∞ = 0 */
            if (p_zero && q_fin && !q_zero)
                return vfix(0);
            if (p_fin && q_fin && !q_zero)
                return sx_simplify(sx_div(pv, qv));
        }

        /* EXPT: handle indeterminate power forms 1^∞, 0^0, ∞^0 */
        if (op == SX_EXPT && n == 2) {
            val_t base = args[0], expo = args[1];
            val_t bv = sx_simplify(sx_substitute(base, var, point));
            val_t ev = sx_simplify(sx_substitute(expo, var, point));
            bool b_one  = is_one(bv);
            bool b_zero = vis_number(bv) && num_is_zero(bv);
            bool b_inf  = vis_flonum(bv) && isinf(num_to_double(bv));
            bool e_zero = vis_number(ev) && num_is_zero(ev);
            bool e_inf  = vis_flonum(ev) && isinf(num_to_double(ev));
            /* Indeterminate power: rewrite f^g = exp(g * log(f)), take limit of exponent */
            if ((b_one && e_inf) || (b_zero && e_zero) || (b_inf && e_zero)) {
                val_t exponent = sx_mul(expo, sx_log(base));
                val_t lim_exp = sx_limit_inner(exponent, var, point, dir, depth + 1);
                if (vis_number(lim_exp) && !(vis_flonum(lim_exp) && isnan(num_to_double(lim_exp)))) {
                    val_t res = sx_simplify(sx_exp(lim_exp));
                    /* Coerce flonum integer results (e.g. 1.0 → 1) for exact equality */
                    if (vis_flonum(res)) {
                        double d = num_to_double(res), fl = floor(d);
                        if (d == fl && d >= -9.0e18 && d <= 9.0e18)
                            res = vfix((long long)d);
                    }
                    return res;
                }
            }
            /* Non-indeterminate: both limits are determinate numbers */
            if (vis_number(bv) && vis_number(ev) &&
                !(vis_flonum(bv) && isnan(num_to_double(bv))) &&
                !(vis_flonum(ev) && isnan(num_to_double(ev))))
                return sx_simplify(num_expt(bv, ev));
        }

        /* ADD/SUB/MUL/NEG: substitute into each subterm and reassemble */
        if (op == SX_ADD) {
            val_t *la = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++)
                la[i] = sx_limit_inner(args[i], var, point, dir, depth);
            return sx_simplify(sx_make_expr(SX_ADD, n, la));
        }
        if (op == SX_SUB && n == 2)
            return sx_simplify(sx_sub(sx_limit_inner(args[0], var, point, dir, depth),
                                      sx_limit_inner(args[1], var, point, dir, depth)));
        if (op == SX_NEG && n == 1)
            return sx_simplify(sx_neg(sx_limit_inner(args[0], var, point, dir, depth)));
        if (op == SX_MUL) {
            val_t *la = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
            for (int i = 0; i < n; i++)
                la[i] = sx_limit_inner(args[i], var, point, dir, depth);
            /* 0·∞: detect after taking individual limits and rewrite as ratio.
             * Heuristic: put the simpler (sym-var) factor in the denominator.
             * If the ∞ factor is a sym-var → use f/(1/g) = 0/0 form (faster convergence).
             * Otherwise → use g/(1/f) = ∞/∞ form. */
            for (int i = 0; i < n && n == 2; i++) {
                int j = 1 - i;
                bool li_zero = vis_number(la[i]) && num_is_zero(la[i]);
                bool lj_inf  = vis_flonum(la[j]) && isinf(num_to_double(la[j]));
                if (li_zero && lj_inf) {
                    val_t rewritten;
                    if (vis_symvar(args[j]))
                        /* ∞ factor is a sym-var: f/(1/g) = 0/0 form */
                        rewritten = sx_div(args[i], sx_div(vfix(1), args[j]));
                    else
                        /* default: g/(1/f) = ∞/∞ form */
                        rewritten = sx_div(args[j], sx_div(vfix(1), args[i]));
                    return sx_limit_inner(rewritten, var, point, dir, depth + 1);
                }
            }
            return sx_simplify(sx_make_expr(SX_MUL, n, la));
        }
    }

    /* Direct substitution fallback */
    val_t sub = sx_simplify(sx_substitute(expr, var, point));
    if (vis_number(sub)) {
        if (vis_flonum(sub) && isnan(num_to_double(sub)))
            return sx_limit_unevaluated(expr, var, point);
        return sub;
    }
    /* Still symbolic after substitution — unevaluated */
    if (!sx_depends_on(sub, var))
        return sub;
    return sx_limit_unevaluated(expr, var, point);
}

val_t sx_limit(val_t expr, val_t var, val_t point, int dir) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "limit: second argument must be a symbolic variable");
    return sx_limit_inner(expr, var, point, dir, 0);
}

/* ---- Taylor series ---- */

/* Coerce a flonum with an integer value to a fixnum so that subsequent
 * num_div produces exact rationals (e.g. exp(0)=1.0 → 1 → 1/6 not 0.1667). */
static val_t series_coerce_exact(val_t v) {
    if (!vis_flonum(v)) return v;
    double d = num_to_double(v);
    double fl = floor(d);
    if (d == fl && d >= (double)INTPTR_MIN && d <= (double)INTPTR_MAX)
        return vfix((intptr_t)fl);
    return v;
}

val_t sx_series(val_t expr, val_t var, val_t point, int n) {
    if (!vis_symvar(var))
        scm_raise(V_FALSE, "series: second argument must be a symbolic variable");
    if (n < 0) n = 0;

    val_t *terms = (val_t *)gc_alloc((size_t)(n + 1) * sizeof(val_t));
    int nterms = 0;
    val_t fk = expr;
    val_t fact = vfix(1);  /* k! */

    for (int k = 0; k <= n; k++) {
        if (k > 0) {
            fk = sx_simplify(sx_diff(fk, var));
            fact = num_mul(fact, vfix(k));
        }

        val_t ck = sx_simplify(sx_substitute(fk, var, point));
        if (vis_number(ck) && num_is_zero(ck)) continue;

        /* Promote integer-valued flonums to fixnum for exact rational output */
        ck = series_coerce_exact(ck);

        val_t coeff = vis_number(ck) ? num_div(ck, fact) : sx_div(ck, fact);
        if (vis_number(coeff) && num_is_zero(coeff)) continue;

        val_t term;
        if (k == 0) {
            term = coeff;
        } else {
            /* sx_sub(var, 0) simplifies to var automatically */
            val_t xma = sx_sub(var, point);
            val_t pw = (k == 1) ? xma : sx_expt(xma, vfix(k));
            term = sx_mul(coeff, pw);
        }
        terms[nterms++] = term;
    }

    if (nterms == 0) return vfix(0);
    if (nterms == 1) return terms[0];
    return sx_simplify(sx_make_expr(SX_ADD, nterms, terms));
}

/* ---- Integral transforms ---- */

/* Helper: test if two sym-vars have the same name */
static bool sx_same_var(val_t a, val_t b) {
    return vis_symvar(a) && vis_symvar(b) &&
           as_symvar(a)->name == as_symvar(b)->name;
}

/* Create the Laplace-transform function of fn: name "L_<fn_name>", same params
 * but with t replaced by s in the params list. */
static val_t sx_laplace_fn(val_t fn, val_t t_var, val_t s_var) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[256];
    snprintf(buf, sizeof(buf), "L_%.*s", (int)fn_sym->len, fn_sym->data);
    /* Build new params: replace t with s */
    val_t old_params = sf->params, new_params = V_NIL;
    val_t *tail = &new_params;
    while (vis_pair(old_params)) {
        val_t p = vcar(old_params);
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = sx_same_var(p, t_var) ? s_var : p;
        cell->cdr = V_NIL;
        *tail = vptr(cell); tail = &cell->cdr;
        old_params = vcdr(old_params);
    }
    return sx_make_fn(sym_intern_cstr(buf), new_params);
}

/* Replace t_var with s_var in the arg list of a SX_APPLY node */
static void sx_replace_var_in_args(val_t *dst, val_t *src, int n,
                                   val_t t_var, val_t s_var) {
    for (int i = 0; i < n; i++)
        dst[i] = sx_same_var(src[i], t_var) ? s_var : src[i];
}

/* Extract the linear coefficient of var in expr, i.e. return a such that
 * expr == a*var + b, or return V_FALSE if not of that form. */
static val_t sx_linear_coeff(val_t expr, val_t var) {
    if (sx_same_var(expr, var)) return vfix(1);
    if (!vis_symexpr(expr)) return V_FALSE;
    SymExpr *se = as_symexpr(expr);
    if (se->op == SX_MUL && se->nargs == 2) {
        if (sx_same_var(se->args[0], var) && !sx_depends_on(se->args[1], var))
            return se->args[1];
        if (sx_same_var(se->args[1], var) && !sx_depends_on(se->args[0], var))
            return se->args[0];
    }
    return V_FALSE;
}

val_t sx_laplace(val_t expr, val_t t_var, val_t s_var) {
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "laplace: t argument must be a symbolic variable");
    if (!vis_symvar(s_var))
        scm_raise(V_FALSE, "laplace: s argument must be a symbolic variable");

    /* Constant (doesn't depend on t): L{c} = c/s */
    if (!sx_depends_on(expr, t_var))
        return sx_div(expr, s_var);

    /* The t variable itself: L{t} = 1/s^2 */
    if (sx_same_var(expr, t_var))
        return sx_div(vfix(1), sx_expt(s_var, vfix(2)));

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity: L{f+g} = L{f}+L{g} */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_laplace(a[i], t_var, s_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_laplace(a[0], t_var, s_var),
                          sx_laplace(a[1], t_var, s_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_laplace(a[0], t_var, s_var));

        /* Constant multiple: L{c*f} = c*L{f} */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], t_var))
                return sx_mul(a[0], sx_laplace(a[1], t_var, s_var));
            if (!sx_depends_on(a[1], t_var))
                return sx_mul(a[1], sx_laplace(a[0], t_var, s_var));
        }

        /* t^n: L{t^n} = n!/s^{n+1} */
        if (op == SX_EXPT && n == 2 && sx_same_var(a[0], t_var) &&
            vis_fixnum(a[1]) && vunfix(a[1]) >= 0) {
            long nv = vunfix(a[1]);
            /* compute n! as a fixnum (safe for small n) */
            val_t fact = vfix(1);
            for (long k = 2; k <= nv; k++) fact = num_mul(fact, vfix(k));
            return sx_div(fact, sx_expt(s_var, vfix(nv + 1)));
        }

        /* e^{a*t}: L{exp(a*t)} = 1/(s-a) */
        if (op == SX_EXP && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff))
                return sx_div(vfix(1), sx_sub(s_var, coeff));
        }

        /* sin(omega*t): L{sin} = omega/(s^2+omega^2) */
        if (op == SX_SIN && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(coeff, sx_add(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* cos(omega*t): L{cos} = s/(s^2+omega^2) */
        if (op == SX_COS && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(s_var, sx_add(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* sinh(omega*t): L{sinh} = omega/(s^2-omega^2) */
        if (op == SX_SINH && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(coeff, sx_sub(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* cosh(omega*t): L{cosh} = s/(s^2-omega^2) */
        if (op == SX_COSH && n == 1) {
            val_t coeff = sx_linear_coeff(a[0], t_var);
            if (!vis_false(coeff)) {
                val_t w2 = sx_expt(coeff, vfix(2));
                return sx_div(s_var, sx_sub(sx_expt(s_var, vfix(2)), w2));
            }
        }

        /* SX_APPLY: symbolic function application */
        if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
            val_t fn  = a[0];
            int   nf  = n - 1;
            val_t *fa = a + 1;
            SymFn *sf = as_symfn(fn);

            /* Derivative property: L{u_t(x,t)} = s*L{u(x,t)} - u(x,0) */
            if (!vis_false(sf->parent) && vis_symvar(sf->d_param) &&
                as_symvar(sf->d_param)->name == as_symvar(t_var)->name) {
                /* Replace t with s in args for L{parent} call */
                val_t *la = (val_t *)gc_alloc((size_t)nf * sizeof(val_t));
                val_t *za = (val_t *)gc_alloc((size_t)nf * sizeof(val_t));
                sx_replace_var_in_args(la, fa, nf, t_var, s_var);
                sx_replace_var_in_args(za, fa, nf, t_var, vfix(0));
                val_t L_fn       = sx_laplace_fn(sf->parent, t_var, s_var);
                val_t L_of_par   = sx_make_apply(L_fn, nf, la);
                val_t par_at_0   = sx_make_apply(sf->parent, nf, za);
                /* s * L{parent}(x,s) - parent(x,0) */
                return sx_sub(sx_mul(s_var, L_of_par), par_at_0);
            }

            /* Simple case: u(x,t) -> L_u(x,s) */
            val_t *na = (val_t *)gc_alloc((size_t)nf * sizeof(val_t));
            sx_replace_var_in_args(na, fa, nf, t_var, s_var);
            val_t L_fn = sx_laplace_fn(fn, t_var, s_var);
            return sx_make_apply(L_fn, nf, na);
        }
    }

    /* Fallback: unevaluated node (laplace expr t s) */
    val_t largs[3] = {expr, t_var, s_var};
    return sx_make_expr(SX_LAPLACE, 3, largs);
}

/* ---- Inverse Laplace (table-based) ---- */

val_t sx_ilaplace(val_t expr, val_t s_var, val_t t_var) {
    if (!vis_symvar(s_var))
        scm_raise(V_FALSE, "ilaplace: s argument must be a symbolic variable");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "ilaplace: t argument must be a symbolic variable");

    /* Constant (doesn't depend on s): L^{-1}{c} -> unevaluated */
    if (!sx_depends_on(expr, s_var)) {
        val_t largs[3] = {expr, s_var, t_var};
        return sx_make_expr(SX_LAPLACE, 3, largs);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_ilaplace(a[i], s_var, t_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_ilaplace(a[0], s_var, t_var),
                          sx_ilaplace(a[1], s_var, t_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_ilaplace(a[0], s_var, t_var));

        /* Constant multiple */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], s_var))
                return sx_mul(a[0], sx_ilaplace(a[1], s_var, t_var));
            if (!sx_depends_on(a[1], s_var))
                return sx_mul(a[1], sx_ilaplace(a[0], s_var, t_var));
        }

        /* c/s -> c (step function * constant) */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && sx_same_var(a[1], s_var))
            return a[0];

        /* c/s^n -> c * t^{n-1}/(n-1)! */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && !sx_depends_on(a[0], s_var)) {
            val_t den = a[1];
            if (vis_symexpr(den) && as_symexpr(den)->op == SX_EXPT &&
                sx_same_var(as_symexpr(den)->args[0], s_var) &&
                vis_fixnum(as_symexpr(den)->args[1])) {
                long nv = vunfix(as_symexpr(den)->args[1]);
                if (nv >= 1) {
                    /* c * t^{n-1}/(n-1)! — try to reduce c/(n-1)! exactly */
                    val_t fact = vfix(1);
                    for (long k = 2; k <= nv - 1; k++) fact = num_mul(fact, vfix(k));
                    val_t coeff = vis_number(a[0]) ? num_div(a[0], fact) : sx_div(a[0], fact);
                    if (num_is_one(coeff))
                        return sx_expt(t_var, vfix(nv - 1));
                    return sx_mul(coeff, sx_expt(t_var, vfix(nv - 1)));
                }
            }
        }

        /* c/(s-a) -> c*e^{at} */
        if (op == SX_DIV && n == 2 && vis_number(a[0]) && !sx_depends_on(a[0], s_var)) {
            val_t den = a[1];
            if (vis_symexpr(den) && as_symexpr(den)->op == SX_SUB &&
                (int)as_symexpr(den)->nargs == 2 &&
                sx_same_var(as_symexpr(den)->args[0], s_var) &&
                !sx_depends_on(as_symexpr(den)->args[1], s_var)) {
                val_t pole = as_symexpr(den)->args[1];
                val_t base_e = sx_exp(sx_mul(pole, t_var));
                return num_is_one(a[0]) ? base_e : sx_mul(a[0], base_e);
            }
        }

        /* omega/(s^2+omega^2) -> sin(omega*t)
         * omega/(s^2-omega^2) -> sinh(omega*t)
         * s/(s^2+omega^2)     -> cos(omega*t)
         * s/(s^2-omega^2)     -> cosh(omega*t)
         * Also handles simplifier-reordered forms: (+ c s^2) or (+ s^2 c)
         * num_v may be s itself (for cos/cosh) or a constant (for sin/sinh) */
        if (op == SX_DIV && n == 2 &&
            (!sx_depends_on(a[0], s_var) || sx_same_var(a[0], s_var))) {
            val_t num_v = a[0], den = a[1];
            if (vis_symexpr(den)) {
                val_t dop = as_symexpr(den)->op;
                int   dn  = (int)as_symexpr(den)->nargs;
                if ((dop == SX_ADD || dop == SX_SUB) && dn == 2) {
                    val_t d0 = as_symexpr(den)->args[0];
                    val_t d1 = as_symexpr(den)->args[1];
                    /* Find which of d0, d1 is s^2 and which is the constant part.
                     * The simplifier places numeric constants first, so s^2 may be d1. */
                    val_t s2_term = V_FALSE, c_term = V_FALSE;
                    if (vis_symexpr(d0) && as_symexpr(d0)->op == SX_EXPT &&
                        (int)as_symexpr(d0)->nargs == 2 &&
                        sx_same_var(as_symexpr(d0)->args[0], s_var) &&
                        vis_fixnum(as_symexpr(d0)->args[1]) &&
                        vunfix(as_symexpr(d0)->args[1]) == 2 &&
                        !sx_depends_on(d1, s_var)) {
                        s2_term = d0; c_term = d1;
                    } else if (dop == SX_ADD &&
                               vis_symexpr(d1) && as_symexpr(d1)->op == SX_EXPT &&
                               (int)as_symexpr(d1)->nargs == 2 &&
                               sx_same_var(as_symexpr(d1)->args[0], s_var) &&
                               vis_fixnum(as_symexpr(d1)->args[1]) &&
                               vunfix(as_symexpr(d1)->args[1]) == 2 &&
                               !sx_depends_on(d0, s_var)) {
                        s2_term = d1; c_term = d0;
                    }
                    if (!vis_false(s2_term)) {
                        /* c_term is omega^2 or a constant; extract omega */
                        val_t w = V_FALSE;
                        if (vis_symexpr(c_term) && as_symexpr(c_term)->op == SX_EXPT &&
                            (int)as_symexpr(c_term)->nargs == 2 &&
                            vis_fixnum(as_symexpr(c_term)->args[1]) &&
                            vunfix(as_symexpr(c_term)->args[1]) == 2)
                            w = as_symexpr(c_term)->args[0];  /* omega from omega^2 */
                        if (vis_false(w)) w = sx_simplify(sx_sqrt(c_term)); /* omega = sqrt(c_term) */
                        if (dop == SX_ADD) {
                            if (sx_equal(num_v, w))
                                return sx_sin(sx_mul(w, t_var));
                            if (sx_same_var(num_v, s_var))
                                return sx_cos(sx_mul(w, t_var));
                            /* c/(s^2+w^2) where c != w: return c/w * sin(w*t) */
                            if (vis_number(num_v) && !vis_false(w) && !vis_symbolic(w))
                                return sx_mul(sx_div(num_v, w), sx_sin(sx_mul(w, t_var)));
                        } else { /* SX_SUB: hyperbolic */
                            if (sx_equal(num_v, w))
                                return sx_sinh(sx_mul(w, t_var));
                            if (sx_same_var(num_v, s_var))
                                return sx_cosh(sx_mul(w, t_var));
                            /* c/(s^2-w^2) where c != w: return c/w * sinh(w*t) */
                            if (vis_number(num_v) && !vis_false(w) && !vis_symbolic(w))
                                return sx_mul(sx_div(num_v, w), sx_sinh(sx_mul(w, t_var)));
                        }
                    }
                }
            }
        }
    }

    /* Fallback: unevaluated */
    val_t largs[3] = {expr, s_var, t_var};
    return sx_make_expr(SX_LAPLACE, 3, largs);
}

/* ---- Fourier transform ---- */

/* Create the Fourier-transform function of fn: name "F_<fn_name>" */
static val_t sx_fourier_fn(val_t fn, val_t t_var, val_t w_var) {
    SymFn  *sf     = as_symfn(fn);
    Symbol *fn_sym = as_sym(sf->name);
    char    buf[256];
    snprintf(buf, sizeof(buf), "F_%.*s", (int)fn_sym->len, fn_sym->data);
    /* Replace t with omega in params */
    val_t old_params = sf->params, new_params = V_NIL;
    val_t *tail = &new_params;
    while (vis_pair(old_params)) {
        val_t p = vcar(old_params);
        Pair *cell = CURRY_NEW(Pair);
        cell->hdr.type = T_PAIR; cell->hdr.flags = 0;
        cell->car = sx_same_var(p, t_var) ? w_var : p;
        cell->cdr = V_NIL;
        *tail = vptr(cell); tail = &cell->cdr;
        old_params = vcdr(old_params);
    }
    return sx_make_fn(sym_intern_cstr(buf), new_params);
}

val_t sx_fourier(val_t expr, val_t t_var, val_t w_var) {
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "fourier: t argument must be a symbolic variable");
    if (!vis_symvar(w_var))
        scm_raise(V_FALSE, "fourier: omega argument must be a symbolic variable");

    /* Constant: F{c} -> unevaluated (needs Dirac delta) */
    if (!sx_depends_on(expr, t_var)) {
        val_t fargs[3] = {expr, t_var, w_var};
        return sx_make_expr(SX_FOURIER, 3, fargs);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t    op = se->op;
        int      n  = (int)se->nargs;
        val_t   *a  = se->args;

        /* Linearity */
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++)
                acc = sx_add(acc, sx_fourier(a[i], t_var, w_var));
            return acc;
        }
        if (op == SX_SUB && n == 2)
            return sx_sub(sx_fourier(a[0], t_var, w_var),
                          sx_fourier(a[1], t_var, w_var));
        if (op == SX_NEG && n == 1)
            return sx_neg(sx_fourier(a[0], t_var, w_var));

        /* Constant multiple */
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], t_var))
                return sx_mul(a[0], sx_fourier(a[1], t_var, w_var));
            if (!sx_depends_on(a[1], t_var))
                return sx_mul(a[1], sx_fourier(a[0], t_var, w_var));
        }

        /* SX_APPLY: symbolic function application */
        if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
            val_t fn  = a[0];
            int   nf  = n - 1;
            val_t *fa = a + 1;
            SymFn *sf = as_symfn(fn);

            /* Derivative property: F{u_t(x,t)} = i*omega * F{u(x,t)} */
            if (!vis_false(sf->parent) && vis_symvar(sf->d_param) &&
                as_symvar(sf->d_param)->name == as_symvar(t_var)->name) {
                val_t *wa = (val_t *)gc_alloc((size_t)nf * sizeof(val_t));
                sx_replace_var_in_args(wa, fa, nf, t_var, w_var);
                val_t F_fn     = sx_fourier_fn(sf->parent, t_var, w_var);
                val_t F_of_par = sx_make_apply(F_fn, nf, wa);
                /* i*omega * F{parent} — use complex i */
                val_t i_val = num_make_complex(vfix(0), vfix(1));  /* 0+1i */
                return sx_mul(sx_mul(i_val, w_var), F_of_par);
            }

            /* Simple case: u(x,t) -> F_u(x,omega) */
            val_t *wa = (val_t *)gc_alloc((size_t)nf * sizeof(val_t));
            sx_replace_var_in_args(wa, fa, nf, t_var, w_var);
            val_t F_fn = sx_fourier_fn(fn, t_var, w_var);
            return sx_make_apply(F_fn, nf, wa);
        }
    }

    /* Fallback: unevaluated */
    val_t fargs[3] = {expr, t_var, w_var};
    return sx_make_expr(SX_FOURIER, 3, fargs);
}

val_t sx_ifourier(val_t expr, val_t w_var, val_t t_var) {
    /* Minimal inverse: linearity only, fallback unevaluated */
    if (!vis_symvar(w_var))
        scm_raise(V_FALSE, "ifourier: omega argument must be a symbolic variable");
    if (!vis_symvar(t_var))
        scm_raise(V_FALSE, "ifourier: t argument must be a symbolic variable");

    if (!sx_depends_on(expr, w_var)) {
        val_t fa[3] = {expr, w_var, t_var};
        return sx_make_expr(SX_FOURIER, 3, fa);
    }

    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t op = se->op;
        int n = (int)se->nargs;
        val_t *a = se->args;
        if (op == SX_ADD) {
            val_t acc = vfix(0);
            for (int i = 0; i < n; i++) acc = sx_add(acc, sx_ifourier(a[i], w_var, t_var));
            return acc;
        }
        if (op == SX_NEG && n == 1) return sx_neg(sx_ifourier(a[0], w_var, t_var));
        if ((op == SX_MUL || op == SX_NCMUL) && n == 2) {
            if (!sx_depends_on(a[0], w_var)) return sx_mul(a[0], sx_ifourier(a[1], w_var, t_var));
            if (!sx_depends_on(a[1], w_var)) return sx_mul(a[1], sx_ifourier(a[0], w_var, t_var));
        }
    }
    val_t fa[3] = {expr, w_var, t_var};
    return sx_make_expr(SX_FOURIER, 3, fa);
}
