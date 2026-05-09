#include "symbolic.h"
#include "object.h"
#include "gc.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* Interned operator symbols */
val_t SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG;
val_t SX_EXPT, SX_SQRT, SX_SIN, SX_COS, SX_TAN, SX_EXP, SX_LOG, SX_ABS;
val_t SX_INTEGRATE, SX_CONJ, SX_REAL, SX_IMAG;

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
