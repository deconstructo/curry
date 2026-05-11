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
val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;
val_t SX_FRACDIFF, SX_FRACINT;

void symbolic_init(void) {
    SX_ADD       = sym_intern_cstr("+");
    SX_SUB       = sym_intern_cstr("-");
    SX_MUL       = sym_intern_cstr("*");
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
}

/* ---- Constructors ---- */

val_t sx_make_var(val_t name) {
    SymVar *v = CURRY_NEW(SymVar);
    v->hdr.type  = T_SYMVAR;
    v->hdr.flags = 0;
    v->name      = name;
    return vptr(v);
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
    return false;
}

/* ---- Simplification ---- */

/* forward declaration */
val_t sx_simplify(val_t expr);

static bool is_zero(val_t v) { return vis_number(v) && num_is_zero(v); }
static bool is_one(val_t v)  { return vis_number(v) && !vis_symbolic(v) && num_is_one(v); }


val_t sx_simplify(val_t expr) {
    if (!vis_symexpr(expr)) return expr;

    SymExpr *se = as_symexpr(expr);
    val_t op = se->op;
    int n = (int)se->nargs;

    /* Recursively simplify all arguments */
    val_t *sa = (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    for (int i = 0; i < n; i++) sa[i] = sx_simplify(se->args[i]);

    /* Flatten nested ADD or MUL into one level */
    if (op == SX_ADD || op == SX_MUL) {
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
            return sx_make_expr(SX_MUL, nsym, nsyms);
        }
        /* coeff == -1: return neg */
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

    /* ---- Transcendentals on constants ---- */
    if (n == 1 && num_count == 1) {
        if (op == SX_SIN) return num_sin(sa[0]);
        if (op == SX_COS) return num_cos(sa[0]);
        if (op == SX_TAN) return num_tan(sa[0]);
        if (op == SX_EXP) return num_exp(sa[0]);
        if (op == SX_LOG) return num_log(sa[0]);
        if (op == SX_ABS) return num_abs(sa[0]);
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
val_t sx_mul(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_MUL, a, b)); }
val_t sx_div(val_t a, val_t b) { return sx_simplify(sx_expr2(SX_DIV, a, b)); }
val_t sx_expt(val_t base, val_t exp) { return sx_simplify(sx_expr2(SX_EXPT, base, exp)); }
val_t sx_sqrt(val_t a) { return sx_simplify(sx_expr1(SX_SQRT, a)); }
val_t sx_sin(val_t a)  { return sx_simplify(sx_expr1(SX_SIN,  a)); }
val_t sx_cos(val_t a)  { return sx_simplify(sx_expr1(SX_COS,  a)); }
val_t sx_tan(val_t a)  { return sx_simplify(sx_expr1(SX_TAN,  a)); }
val_t sx_exp(val_t a)  { return sx_simplify(sx_expr1(SX_EXP,  a)); }
val_t sx_log(val_t a)  { return sx_simplify(sx_expr1(SX_LOG,  a)); }
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
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        for (uint32_t i = 0; i < se->nargs; i++)
            if (sx_depends_on(se->args[i], var)) return true;
    }
    return false;
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
        /* All factors depend on var — no simple rule, fall through to unevaluated */
    }

    /* Power rule: ∫f^n dx where n is numeric */
    if (op == SX_EXPT && n == 2) {
        val_t base = args[0], exp_v = args[1];
        if (!sx_depends_on(exp_v, var) && vis_number(exp_v)) {
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

    /* ∫1/f dx = ln|f| / f'  (linear f) */
    if (op == SX_DIV && n == 2) {
        val_t num_v = args[0], den = args[1];
        if (!sx_depends_on(num_v, var)) {
            val_t df = sx_diff(den, var);
            if (vis_number(df) && !num_is_zero(df))
                return sx_mul(sx_div(num_v, df), sx_log(sx_abs(den)));
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

/* ===================================================================
 * Infix and LaTeX pretty-printing
 * =================================================================== */

/* Operator precedence levels for parenthesisation decisions */
#define SP_LOW   0   /* outermost / always safe */
#define SP_ADD   1   /* +  - */
#define SP_MUL   2   /* *  / */
#define SP_NEG   3   /* unary - */
#define SP_POW   4   /* ^ */
#define SP_ATOM  5   /* numbers, variables, function calls */

static int sp_prec(val_t v) {
    if (!vis_symexpr(v)) return SP_ATOM;
    val_t op = as_symexpr(v)->op;
    if (op == SX_ADD || op == SX_SUB) return SP_ADD;
    if (op == SX_MUL || op == SX_DIV) return SP_MUL;
    if (op == SX_NEG)                  return SP_NEG;
    if (op == SX_EXPT)                 return SP_POW;
    return SP_ATOM;
}

static bool sp_is_neg_node(val_t v) {
    return vis_symexpr(v) && as_symexpr(v)->op == SX_NEG;
}
static val_t sp_neg_inner(val_t v) { return as_symexpr(v)->args[0]; }

/* Write a NUL-terminated string to port (avoids manual byte counts) */
static void pws(val_t port, const char *s) {
    port_write_string(port, s, (uint32_t)strlen(s));
}

/* True iff v is a non-symbolic number whose value is negative */
static bool sp_num_negative(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) < 0;
    if (vis_flonum(v))   return vfloat(v) < 0.0;
    if (vis_rational(v)) return mpz_sgn(mpq_numref(as_rat(v)->q)) < 0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) < 0;
    return false;
}

/* True iff v is a MUL whose leading factor is a negative numeric constant,
   e.g. (* -3 x) — distinct from (neg (* 3 x)) which the simplifier uses
   only when the coefficient is exactly -1. */
static bool sp_is_neg_mul(val_t v) {
    if (!vis_symexpr(v)) return false;
    SymExpr *se = as_symexpr(v);
    return se->op == SX_MUL && se->nargs >= 1 &&
           vis_number(se->args[0]) && !vis_symbolic(se->args[0]) &&
           sp_num_negative(se->args[0]);
}

/* True iff v should show a leading minus sign in a sum (either NEG node or neg-MUL) */
static bool sp_term_negative(val_t v) {
    return sp_is_neg_node(v) || sp_is_neg_mul(v);
}

/* Forward declarations so the pos-mul helpers can call sp_infix / sl_latex */
static void sp_infix(val_t expr, int ctx, val_t port);
static void sl_latex(val_t expr, int ctx, val_t port);

/* Render a neg-MUL with its leading coefficient made positive.
   Caller has already emitted the " - " separator. */
static void sp_pos_mul_infix(val_t v, val_t port) {
    SymExpr *mul = as_symexpr(v);
    int      mn  = (int)mul->nargs;
    val_t    pc  = num_neg(mul->args[0]);   /* flip sign → positive */
    bool     sk  = num_is_one(pc) && mn >= 2;
    for (int j = 0; j < mn; j++) {
        if (j == 0 && sk) continue;
        if (j > (sk ? 1 : 0)) pws(port, " * ");
        sp_infix((j == 0) ? pc : mul->args[j], SP_MUL, port);
    }
}
static void sp_pos_mul_latex(val_t v, val_t port) {
    SymExpr *mul = as_symexpr(v);
    int      mn  = (int)mul->nargs;
    val_t    pc  = num_neg(mul->args[0]);
    bool     sk  = num_is_one(pc) && mn >= 2;
    for (int j = 0; j < mn; j++) {
        if (j == 0 && sk) continue;
        if (j > (sk ? 1 : 0)) port_write_char(port, ' ');
        sl_latex((j == 0) ? pc : mul->args[j], SP_MUL, port);
    }
}

/* ---- INFIX ---- */

/*
 * sp_infix(expr, ctx_prec, port)
 *
 * Writes expr in infix notation. If sp_prec(expr) < ctx_prec the whole
 * expression is wrapped in parentheses so the caller's context is respected.
 */
static void sp_infix(val_t expr, int ctx, val_t port) {
    extern void scm_display(val_t, val_t);

    if (!vis_symbolic(expr)) { scm_display(expr, port); return; }
    if (vis_symvar(expr)) {
        Symbol *s = as_sym(as_symvar(expr)->name);
        port_write_string(port, s->data, s->len);
        return;
    }

    SymExpr *se     = as_symexpr(expr);
    val_t    op     = se->op;
    int      n      = (int)se->nargs;
    val_t   *a      = se->args;
    bool     paren  = (sp_prec(expr) < ctx);
    if (paren) port_write_char(port, '(');

    if (op == SX_ADD) {
        /* First term */
        if (sp_is_neg_node(a[0])) {
            port_write_char(port, '-');
            sp_infix(sp_neg_inner(a[0]), SP_NEG, port);
        } else if (sp_is_neg_mul(a[0])) {
            port_write_char(port, '-');
            sp_pos_mul_infix(a[0], port);
        } else {
            sp_infix(a[0], SP_ADD, port);
        }
        /* Remaining terms */
        for (int i = 1; i < n; i++) {
            if (sp_is_neg_node(a[i])) {
                pws(port, " - "); sp_infix(sp_neg_inner(a[i]), SP_MUL, port);
            } else if (sp_is_neg_mul(a[i])) {
                pws(port, " - "); sp_pos_mul_infix(a[i], port);
            } else {
                pws(port, " + "); sp_infix(a[i], SP_ADD, port);
            }
        }
    } else if (op == SX_SUB && n == 2) {
        sp_infix(a[0], SP_ADD, port);
        pws(port, " - ");
        sp_infix(a[1], SP_MUL, port);
    } else if (op == SX_NEG && n == 1) {
        port_write_char(port, '-');
        sp_infix(a[0], SP_NEG, port);
    } else if (op == SX_MUL) {
        for (int i = 0; i < n; i++) {
            if (i > 0) pws(port, " * ");
            sp_infix(a[i], SP_MUL, port);
        }
    } else if (op == SX_DIV && n == 2) {
        sp_infix(a[0], SP_MUL, port);
        port_write_char(port, '/');
        /* SP_NEG ensures ADD(1) and MUL(2) both get parens on the right side */
        sp_infix(a[1], SP_NEG, port);
    } else if (op == SX_EXPT && n == 2) {
        sp_infix(a[0], SP_POW, port);
        port_write_char(port, '^');
        /* Wrap exponent in parens if it is not a bare fixnum or variable */
        bool ep = !(vis_fixnum(a[1]) || vis_symvar(a[1]));
        if (ep) port_write_char(port, '(');
        sp_infix(a[1], SP_LOW, port);
        if (ep) port_write_char(port, ')');
    } else if (op == SX_SQRT && n == 1) {
        pws(port, "sqrt("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ABS && n == 1) {
        port_write_char(port, '|'); sp_infix(a[0], SP_LOW, port); port_write_char(port, '|');
    } else if (op == SX_CONJ && n == 1) {
        pws(port, "conj("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_REAL && n == 1) {
        pws(port, "Re("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_IMAG && n == 1) {
        pws(port, "Im("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_FRACDIFF && n == 3) {
        pws(port, "D^");
        bool ep = (sp_prec(a[1]) < SP_ATOM);
        if (ep) port_write_char(port, '(');
        sp_infix(a[1], SP_LOW, port);
        if (ep) port_write_char(port, ')');
        port_write_char(port, '[');
        sp_infix(a[0], SP_LOW, port); pws(port, ", "); sp_infix(a[2], SP_LOW, port);
        port_write_char(port, ']');
    } else if (op == SX_FRACINT && n == 3) {
        pws(port, "I^");
        bool ep = (sp_prec(a[1]) < SP_ATOM);
        if (ep) port_write_char(port, '(');
        sp_infix(a[1], SP_LOW, port);
        if (ep) port_write_char(port, ')');
        port_write_char(port, '[');
        sp_infix(a[0], SP_LOW, port); pws(port, ", "); sp_infix(a[2], SP_LOW, port);
        port_write_char(port, ']');
    } else if (op == SX_INTEGRATE && n >= 2) {
        pws(port, "\xe2\x88\xab ");   /* ∫ */
        sp_infix(a[0], SP_ADD, port);
        pws(port, " d");
        sp_infix(a[1], SP_LOW, port);
    } else {
        Symbol *ops = as_sym(op);
        /* Unevaluated ∂ node: U+2202 = 0xE2 0x88 0x82 */
        if (ops->len == 3 &&
            (unsigned char)ops->data[0] == 0xe2 &&
            (unsigned char)ops->data[1] == 0x88 &&
            (unsigned char)ops->data[2] == 0x82 && n == 2) {
            pws(port, "d("); sp_infix(a[0], SP_LOW, port);
            pws(port, ")/d("); sp_infix(a[1], SP_LOW, port);
            port_write_char(port, ')');
        } else {
            /* Generic function call */
            port_write_string(port, ops->data, ops->len);
            port_write_char(port, '(');
            for (int i = 0; i < n; i++) {
                if (i > 0) pws(port, ", ");
                sp_infix(a[i], SP_LOW, port);
            }
            port_write_char(port, ')');
        }
    }

    if (paren) port_write_char(port, ')');
}

void sx_write_infix(val_t expr, val_t port) { sp_infix(expr, SP_LOW, port); }

/* ---- LATEX ---- */

/* Map recognised Greek-letter variable names to LaTeX commands */
static const char *sl_greek(const char *name, uint32_t len) {
    static const struct { const char *n; const char *cmd; } T[] = {
        {"alpha","\\alpha"},{"beta","\\beta"},{"gamma","\\gamma"},
        {"delta","\\delta"},{"epsilon","\\epsilon"},{"varepsilon","\\varepsilon"},
        {"zeta","\\zeta"},{"eta","\\eta"},{"theta","\\theta"},
        {"iota","\\iota"},{"kappa","\\kappa"},{"lambda","\\lambda"},
        {"mu","\\mu"},{"nu","\\nu"},{"xi","\\xi"},
        {"pi","\\pi"},{"rho","\\rho"},{"sigma","\\sigma"},
        {"tau","\\tau"},{"upsilon","\\upsilon"},{"phi","\\phi"},
        {"varphi","\\varphi"},{"chi","\\chi"},{"psi","\\psi"},{"omega","\\omega"},
        {"Gamma","\\Gamma"},{"Delta","\\Delta"},{"Theta","\\Theta"},
        {"Lambda","\\Lambda"},{"Xi","\\Xi"},{"Pi","\\Pi"},
        {"Sigma","\\Sigma"},{"Upsilon","\\Upsilon"},{"Phi","\\Phi"},
        {"Psi","\\Psi"},{"Omega","\\Omega"},
        {NULL,NULL}
    };
    for (int i = 0; T[i].n; i++) {
        uint32_t nl = (uint32_t)strlen(T[i].n);
        if (nl == len && memcmp(T[i].n, name, len) == 0) return T[i].cmd;
    }
    return NULL;
}

static void sl_varname(val_t v, val_t port) {
    Symbol *s  = as_sym(as_symvar(v)->name);
    const char *gr = sl_greek(s->data, s->len);
    if (gr) pws(port, gr);
    else    port_write_string(port, s->data, s->len);
}

/* Write a number in LaTeX: exact rationals become \frac{p}{q} */
static void sl_num(val_t v, val_t port) {
    extern void scm_display(val_t, val_t);
    if (vis_rational(v)) {
        Rational *r  = as_rat(v);
        mpz_ptr   nm = mpq_numref(r->q);
        mpz_ptr   dn = mpq_denref(r->q);
        if (mpz_cmp_ui(dn, 1) == 0) {
            char *s = mpz_get_str(NULL, 10, nm);
            port_write_string(port, s, (uint32_t)strlen(s));
            free(s);
            return;
        }
        int ng = mpz_sgn(nm) < 0;
        if (ng) {
            pws(port, "-\\frac{");
            mpz_t absnm; mpz_init(absnm); mpz_abs(absnm, nm);
            char *ns = mpz_get_str(NULL, 10, absnm);
            port_write_string(port, ns, (uint32_t)strlen(ns));
            free(ns); mpz_clear(absnm);
        } else {
            pws(port, "\\frac{");
            char *ns = mpz_get_str(NULL, 10, nm);
            port_write_string(port, ns, (uint32_t)strlen(ns));
            free(ns);
        }
        pws(port, "}{");
        char *ds = mpz_get_str(NULL, 10, dn);
        port_write_string(port, ds, (uint32_t)strlen(ds));
        free(ds);
        port_write_char(port, '}');
        return;
    }
    scm_display(v, port);
}

/*
 * sl_latex(expr, ctx_prec, port)
 *
 * Writes expr in LaTeX notation. If sp_prec(expr) < ctx_prec the expression
 * is wrapped in \left(\right) for proper grouping.
 */
static void sl_latex(val_t expr, int ctx, val_t port) {
    if (!vis_symbolic(expr)) { sl_num(expr, port); return; }
    if (vis_symvar(expr)) { sl_varname(expr, port); return; }

    SymExpr *se    = as_symexpr(expr);
    val_t    op    = se->op;
    int      n     = (int)se->nargs;
    val_t   *a     = se->args;
    bool     paren = (sp_prec(expr) < ctx);
    if (paren) pws(port, "\\left(");

    if (op == SX_ADD) {
        if (sp_is_neg_node(a[0])) {
            port_write_char(port, '-');
            sl_latex(sp_neg_inner(a[0]), SP_NEG, port);
        } else if (sp_is_neg_mul(a[0])) {
            port_write_char(port, '-');
            sp_pos_mul_latex(a[0], port);
        } else {
            sl_latex(a[0], SP_ADD, port);
        }
        for (int i = 1; i < n; i++) {
            if (sp_is_neg_node(a[i])) {
                pws(port, " - "); sl_latex(sp_neg_inner(a[i]), SP_MUL, port);
            } else if (sp_is_neg_mul(a[i])) {
                pws(port, " - "); sp_pos_mul_latex(a[i], port);
            } else {
                pws(port, " + "); sl_latex(a[i], SP_ADD, port);
            }
        }
    } else if (op == SX_SUB && n == 2) {
        sl_latex(a[0], SP_ADD, port);
        pws(port, " - ");
        sl_latex(a[1], SP_MUL, port);
    } else if (op == SX_NEG && n == 1) {
        port_write_char(port, '-');
        sl_latex(a[0], SP_NEG, port);
    } else if (op == SX_MUL) {
        for (int i = 0; i < n; i++) {
            if (i > 0) port_write_char(port, ' ');
            sl_latex(a[i], SP_MUL, port);
        }
    } else if (op == SX_DIV && n == 2) {
        pws(port, "\\frac{");
        sl_latex(a[0], SP_LOW, port);
        pws(port, "}{");
        sl_latex(a[1], SP_LOW, port);
        port_write_char(port, '}');
    } else if (op == SX_EXPT && n == 2) {
        sl_latex(a[0], SP_POW, port);
        pws(port, "^{");
        sl_latex(a[1], SP_LOW, port);
        port_write_char(port, '}');
    } else if (op == SX_SQRT && n == 1) {
        pws(port, "\\sqrt{"); sl_latex(a[0], SP_LOW, port); port_write_char(port, '}');
    } else if (op == SX_SIN && n == 1) {
        pws(port, "\\sin\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_COS && n == 1) {
        pws(port, "\\cos\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_TAN && n == 1) {
        pws(port, "\\tan\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_EXP && n == 1) {
        pws(port, "e^{"); sl_latex(a[0], SP_LOW, port); port_write_char(port, '}');
    } else if (op == SX_LOG && n == 1) {
        pws(port, "\\ln\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ABS && n == 1) {
        pws(port, "\\left|"); sl_latex(a[0], SP_LOW, port); pws(port, "\\right|");
    } else if (op == SX_CONJ && n == 1) {
        pws(port, "\\overline{"); sl_latex(a[0], SP_LOW, port); port_write_char(port, '}');
    } else if (op == SX_REAL && n == 1) {
        pws(port, "\\operatorname{Re}\\!\\left(");
        sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_IMAG && n == 1) {
        pws(port, "\\operatorname{Im}\\!\\left(");
        sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_FRACDIFF && n == 3) {
        pws(port, "D^{"); sl_latex(a[1], SP_LOW, port);
        pws(port, "}_{"); sl_latex(a[2], SP_LOW, port);
        pws(port, "}\\!\\left["); sl_latex(a[0], SP_LOW, port); pws(port, "\\right]");
    } else if (op == SX_FRACINT && n == 3) {
        pws(port, "I^{"); sl_latex(a[1], SP_LOW, port);
        pws(port, "}_{"); sl_latex(a[2], SP_LOW, port);
        pws(port, "}\\!\\left["); sl_latex(a[0], SP_LOW, port); pws(port, "\\right]");
    } else if (op == SX_INTEGRATE && n >= 2) {
        pws(port, "\\int ");
        sl_latex(a[0], SP_ADD, port);
        pws(port, " \\, \\mathrm{d}");
        sl_latex(a[1], SP_LOW, port);
    } else {
        Symbol *ops = as_sym(op);
        /* Unevaluated ∂ node */
        if (ops->len == 3 &&
            (unsigned char)ops->data[0] == 0xe2 &&
            (unsigned char)ops->data[1] == 0x88 &&
            (unsigned char)ops->data[2] == 0x82 && n == 2) {
            pws(port, "\\frac{\\partial ");
            sl_latex(a[0], SP_LOW, port);
            pws(port, "}{\\partial ");
            sl_latex(a[1], SP_LOW, port);
            port_write_char(port, '}');
        } else {
            pws(port, "\\operatorname{");
            port_write_string(port, ops->data, ops->len);
            pws(port, "}\\!\\left(");
            for (int i = 0; i < n; i++) {
                if (i > 0) pws(port, ",\\,");
                sl_latex(a[i], SP_LOW, port);
            }
            pws(port, "\\right)");
        }
    }

    if (paren) pws(port, "\\right)");
}

void sx_write_latex(val_t expr, val_t port) { sl_latex(expr, SP_LOW, port); }

/* ---- Display ---- */

void sx_write(val_t expr, val_t port) {
    if (vis_symvar(expr)) {
        Symbol *s = as_sym(as_symvar(expr)->name);
        port_write_string(port, s->data, s->len);
        return;
    }
    if (vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);

        /* Special display for neg: write -(x) → (- x) */
        if (se->op == SX_NEG && se->nargs == 1) {
            port_write_string(port, "(- ", 3);
            sx_write(se->args[0], port);
            port_write_char(port, ')');
            return;
        }

        /* (frac-diff f α x) → (D^α α f x) */
        if (se->op == SX_FRACDIFF && se->nargs == 3) {
            port_write_string(port, "(D^\xce\xb1 ", 6); /* D^α */
            sx_write(se->args[1], port);
            port_write_char(port, ' ');
            sx_write(se->args[0], port);
            port_write_char(port, ' ');
            sx_write(se->args[2], port);
            port_write_char(port, ')');
            return;
        }

        /* (frac-int f α x) → (I^α α f x) */
        if (se->op == SX_FRACINT && se->nargs == 3) {
            port_write_string(port, "(I^\xce\xb1 ", 6); /* I^α */
            sx_write(se->args[1], port);
            port_write_char(port, ' ');
            sx_write(se->args[0], port);
            port_write_char(port, ' ');
            sx_write(se->args[2], port);
            port_write_char(port, ')');
            return;
        }

        port_write_char(port, '(');
        /* Write the operator symbol */
        Symbol *ops = as_sym(se->op);
        port_write_string(port, ops->data, ops->len);
        for (uint32_t i = 0; i < se->nargs; i++) {
            port_write_char(port, ' ');
            sx_write(se->args[i], port);
        }
        port_write_char(port, ')');
        return;
    }
    /* Numeric fallback */
    extern void scm_write(val_t v, val_t port);
    scm_write(expr, port);
}
