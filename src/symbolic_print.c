#include "symbolic.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

extern void scm_display(val_t v, val_t port);
extern void scm_write(val_t v, val_t port);

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
    if (op == SX_MUL || op == SX_DIV || op == SX_NCMUL) return SP_MUL;
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
    if (!vis_symbolic(expr) && !vis_symfn(expr)) { scm_display(expr, port); return; }
    if (vis_symvar(expr)) {
        Symbol *s = as_sym(as_symvar(expr)->name);
        port_write_string(port, s->data, s->len);
        return;
    }
    if (vis_symfn(expr)) {
        Symbol *s = as_sym(as_symfn(expr)->name);
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
    } else if (op == SX_MUL || op == SX_NCMUL) {
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
    } else if (op == SX_SINH && n == 1) {
        pws(port, "sinh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_COSH && n == 1) {
        pws(port, "cosh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_TANH && n == 1) {
        pws(port, "tanh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ASIN && n == 1) {
        pws(port, "asin("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ACOS && n == 1) {
        pws(port, "acos("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ATAN && n == 1) {
        pws(port, "atan("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ASINH && n == 1) {
        pws(port, "asinh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ACOSH && n == 1) {
        pws(port, "acosh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_ATANH && n == 1) {
        pws(port, "atanh("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_COT && n == 1) {
        pws(port, "cot("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_SEC && n == 1) {
        pws(port, "sec("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_CSC && n == 1) {
        pws(port, "csc("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
    } else if (op == SX_SIGN && n == 1) {
        pws(port, "sign("); sp_infix(a[0], SP_LOW, port); port_write_char(port, ')');
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
    } else if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
        Symbol *fsym = as_sym(as_symfn(a[0])->name);
        port_write_string(port, fsym->data, fsym->len);
        port_write_char(port, '(');
        for (int i = 1; i < n; i++) {
            if (i > 1) pws(port, ", ");
            sp_infix(a[i], SP_LOW, port);
        }
        port_write_char(port, ')');
    } else if (op == SX_LAPLACE && n >= 2) {
        pws(port, "L{");
        sp_infix(a[0], SP_LOW, port);
        pws(port, ", d");
        sp_infix(a[1], SP_LOW, port);
        port_write_char(port, '}');
    } else if (op == SX_FOURIER && n >= 2) {
        pws(port, "F{");
        sp_infix(a[0], SP_LOW, port);
        pws(port, ", d");
        sp_infix(a[1], SP_LOW, port);
        port_write_char(port, '}');
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
    if (!vis_symbolic(expr) && !vis_symfn(expr)) { sl_num(expr, port); return; }
    if (vis_symvar(expr)) { sl_varname(expr, port); return; }
    if (vis_symfn(expr)) {
        Symbol *s = as_sym(as_symfn(expr)->name);
        port_write_string(port, s->data, s->len);
        return;
    }

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
    } else if (op == SX_MUL || op == SX_NCMUL) {
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
    } else if (op == SX_SINH && n == 1) {
        pws(port, "\\sinh\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_COSH && n == 1) {
        pws(port, "\\cosh\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_TANH && n == 1) {
        pws(port, "\\tanh\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ASIN && n == 1) {
        pws(port, "\\arcsin\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ACOS && n == 1) {
        pws(port, "\\arccos\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ATAN && n == 1) {
        pws(port, "\\arctan\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ASINH && n == 1) {
        pws(port, "\\operatorname{arcsinh}\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ACOSH && n == 1) {
        pws(port, "\\operatorname{arccosh}\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_ATANH && n == 1) {
        pws(port, "\\operatorname{arctanh}\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_COT && n == 1) {
        pws(port, "\\cot\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_SEC && n == 1) {
        pws(port, "\\sec\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_CSC && n == 1) {
        pws(port, "\\csc\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
    } else if (op == SX_SIGN && n == 1) {
        pws(port, "\\operatorname{sign}\\!\\left("); sl_latex(a[0], SP_LOW, port); pws(port, "\\right)");
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
    } else if (op == SX_APPLY && n >= 1 && vis_symfn(a[0])) {
        Symbol *fsym = as_sym(as_symfn(a[0])->name);
        port_write_string(port, fsym->data, fsym->len);
        pws(port, "\\!\\left(");
        for (int i = 1; i < n; i++) {
            if (i > 1) pws(port, ",\\,");
            sl_latex(a[i], SP_LOW, port);
        }
        pws(port, "\\right)");
    } else if (op == SX_LAPLACE && n >= 2) {
        pws(port, "\\mathcal{L}\\!\\left\\{");
        sl_latex(a[0], SP_LOW, port);
        pws(port, "\\right\\}");
    } else if (op == SX_FOURIER && n >= 2) {
        pws(port, "\\mathcal{F}\\!\\left\\{");
        sl_latex(a[0], SP_LOW, port);
        pws(port, "\\right\\}");
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

/* ---- Display (prefix notation) ---- */

void sx_write(val_t expr, val_t port) {
    if (vis_symvar(expr)) {
        Symbol *s = as_sym(as_symvar(expr)->name);
        port_write_string(port, s->data, s->len);
        return;
    }
    if (vis_symfn(expr)) {
        Symbol *s = as_sym(as_symfn(expr)->name);
        pws(port, "#<sym-fn:");
        port_write_string(port, s->data, s->len);
        port_write_char(port, '>');
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
    scm_write(expr, port);
}
