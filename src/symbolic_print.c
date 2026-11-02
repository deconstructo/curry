#include "symbolic.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "lang_registry.h"
#include "eval.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

extern void scm_display(val_t v, val_t port);
extern void scm_write(val_t v, val_t port);

/* ===================================================================
 * Infix and LaTeX pretty-printing
 * =================================================================== */

/* Operator precedence levels for parenthesisation decisions */
#define SP_LOW   0   /* outermost / always safe -- also comparisons (<,<=,>,>=): the
                        loosest binding, same as being unparenthesized at top level */
#define SP_ADD   1   /* +  - */
#define SP_MUL   2   /* *  / */
#define SP_NEG   3   /* unary - */
#define SP_POW   4   /* ^ */
#define SP_ATOM  5   /* numbers, variables, function calls */

static bool sp_is_cmp_op(val_t op) {
    return op == SX_LT || op == SX_LE || op == SX_GT || op == SX_GE;
}

static int sp_prec(val_t v) {
    if (!vis_symexpr(v)) return SP_ATOM;
    val_t op = as_symexpr(v)->op;
    if (op == SX_ADD || op == SX_SUB) return SP_ADD;
    if (op == SX_MUL || op == SX_DIV || op == SX_NCMUL) return SP_MUL;
    if (op == SX_NEG)                  return SP_NEG;
    if (op == SX_EXPT)                 return SP_POW;
    if (sp_is_cmp_op(op))              return SP_LOW;
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
    /* Issue #134: this expression-tree traversal self-recurses once per
     * subexpression with no bound, distinct from #133's printer guard
     * (which only covers scm_write's own plain-list/vector recursion,
     * reached only for a numeric LEAF here, not this function's own
     * tree-walk above that). Shares check_c_stack_depth with #133 and
     * symbolic.c's own sx_simplify/sx_diff/etc (same fix, issue #134). */
    check_c_stack_depth("symbolic");
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
    } else if (sp_is_cmp_op(op) && n == 2) {
        sp_infix(a[0], SP_ADD, port);
        if (op == SX_LT) pws(port, " < ");
        else if (op == SX_LE) pws(port, " <= ");
        else if (op == SX_GT) pws(port, " > ");
        else pws(port, " >= ");
        sp_infix(a[1], SP_ADD, port);
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

/* Names and free text reach LaTeX from user data (a symbol or string can
 * contain anything), and this output is embedded in $$ ... $$ by the Jupyter
 * kernel and other callers, so characters that are special to LaTeX or HTML
 * are written as math commands rather than raw. Everything else, including
 * multi-byte UTF-8, passes through; an underscore in a variable name is left
 * alone (x_1 is a subscript) unless `plain` says the text is not a name. */
static void sl_write_escaped(val_t port, const char *s, uint32_t len, bool plain) {
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '\\': pws(port, "\\backslash "); break;
        case '$': case '&': case '%': case '#': case '{': case '}':
            port_write_char(port, '\\'); port_write_char(port, (int)c); break;
        case '_':
            if (plain) pws(port, "\\_"); else port_write_char(port, '_');
            break;
        case '^': pws(port, "\\hat{}"); break;
        case '~': pws(port, "\\sim "); break;
        case '<': pws(port, "\\lt "); break;
        case '>': pws(port, "\\gt "); break;
        default:
            if (c < 0x20 || c == 0x7f || c == ' ') pws(port, "\\ ");
            else port_write_string(port, s + i, 1);   /* raw byte: keeps UTF-8 intact */
        }
    }
}

static void sl_varname(val_t v, val_t port) {
    Symbol *s  = as_sym(as_symvar(v)->name);
    const char *gr = sl_greek(s->data, s->len);
    if (gr) pws(port, gr);
    else    sl_write_escaped(port, s->data, s->len, false);
}

static void sl_num(val_t v, val_t port);

/* ---- Other numeric-tower values (issue #334) ------------------------------
 * Complex, quaternion, octonion, multivector, surreal and quantum values are
 * written as a sum of terms, each a real coefficient and a unit:
 *   1 + 2i      1 + 2\mathbf{i} + 3\mathbf{j} + 4\mathbf{k}      e_{12}
 *   3\omega - \varepsilon      \frac{1}{2}|0\rangle + \frac{1}{2}|1\rangle */

/* A double as a number value: integral doubles become exact integers, so a
 * quaternion 1+2i+3j+4k is not written 1.0 + 2.0\mathbf{i} ... */
static val_t sl_dval(double d) {
    if (d == d && fabs(d) < 9007199254740992.0 && d == trunc(d)) return vfix((intptr_t)d);
    return num_make_float(d);
}

/* A finite flonum: the shortest text that reads back, with an exponent
 * written as m \times 10^{e}. */
static void sl_flonum(double d, val_t port) {
    char buf[64];
    num_flonum_to_shortest_cstr(d, buf, sizeof(buf));
    char *e = strchr(buf, 'e');
    if (!e) { pws(port, buf); return; }
    *e = '\0';
    const char *ex = e + 1;
    bool eneg = (*ex == '-');
    if (*ex == '+' || *ex == '-') ex++;
    while (*ex == '0' && ex[1]) ex++;
    pws(port, buf);
    pws(port, "\\times 10^{");
    if (eneg) port_write_char(port, '-');
    pws(port, ex);
    port_write_char(port, '}');
}

static bool sl_is_nan(val_t c) { return vis_flonum(c) && vfloat(c) != vfloat(c); }

/* The sign and magnitude of one term of a sum. `first` is true for the leading
 * term (a bare "-" or nothing); later terms get " + " or " - ". With
 * `has_unit`, a coefficient of 1 is left out: the unit alone is written. */
static void sl_term_coef(val_t port, val_t c, bool first, bool has_unit) {
    bool neg = !sl_is_nan(c) && num_is_negative(c);
    if (neg) c = num_neg(c);
    if (first) { if (neg) port_write_char(port, '-'); }
    else pws(port, neg ? " - " : " + ");
    if (has_unit && !sl_is_nan(c) && num_cmp(c, vfix(1)) == 0) return;
    sl_num(c, port);
}

static bool sl_is_zero(val_t c) { return !sl_is_nan(c) && num_cmp(c, vfix(0)) == 0; }

typedef void (*sl_unit_fn)(val_t port, const void *ctx, int idx);

/* Write sum of coefs[i] * unit(i). Unit index 0 (when `scalar0`) is the plain
 * number with no unit. Zero terms are skipped; an all-zero sum writes 0. */
static void sl_sum(val_t port, int n, val_t (*coef)(const void *, int), const void *ctx,
                   sl_unit_fn unit, bool scalar0) {
    bool first = true;
    for (int i = 0; i < n; i++) {
        val_t c = coef(ctx, i);
        if (sl_is_zero(c)) continue;
        bool has_unit = !(scalar0 && i == 0);
        sl_term_coef(port, c, first, has_unit);
        if (has_unit) unit(port, ctx, i);
        first = false;
    }
    if (first) port_write_char(port, '0');
}

static val_t sl_cpx_coef(const void *ctx, int i) {
    const Complex *c = (const Complex *)ctx;
    return i == 0 ? c->real : c->imag;
}
static void sl_cpx_unit(val_t port, const void *ctx, int i) { (void)ctx; (void)i; port_write_char(port, 'i'); }

static val_t sl_quat_coef(const void *ctx, int i) {
    const Quaternion *q = (const Quaternion *)ctx;
    return sl_dval(i == 0 ? q->a : i == 1 ? q->b : i == 2 ? q->c : q->d);
}
static void sl_quat_unit(val_t port, const void *ctx, int i) {
    (void)ctx;
    static const char *const u[] = { "", "\\mathbf{i}", "\\mathbf{j}", "\\mathbf{k}" };
    pws(port, u[i]);
}

static val_t sl_oct_coef(const void *ctx, int i) { return sl_dval(((const Octonion *)ctx)->e[i]); }
static void sl_oct_unit(val_t port, const void *ctx, int i) {
    (void)ctx;
    char buf[16];
    snprintf(buf, sizeof(buf), "e_{%d}", i);
    pws(port, buf);
}

static val_t sl_mv_coef(const void *ctx, int i) { return sl_dval(((const Multivector *)ctx)->c[i]); }
/* The blade with bitmap i: e_{12} for e1 e2 (1-based subscripts). */
static void sl_mv_unit(val_t port, const void *ctx, int i) {
    const Multivector *mv = (const Multivector *)ctx;
    pws(port, "e_{");
    for (int k = 0; k < mv->n; k++) {
        if (i & (1 << k)) port_write_char(port, (char)('1' + k));
    }
    port_write_char(port, '}');
}

static val_t sl_sur_coef(const void *ctx, int i) { return ((const Surreal *)ctx)->data[2*i+1]; }
static void sl_sur_unit(val_t port, const void *ctx, int i) {
    val_t e = ((const Surreal *)ctx)->data[2*i];
    if (num_cmp(e, vfix(1)) == 0)       pws(port, "\\omega");
    else if (num_cmp(e, vfix(-1)) == 0) pws(port, "\\varepsilon");
    else { pws(port, "\\omega^{"); sl_num(e, port); port_write_char(port, '}'); }
}

/* Surreal exponent 0 is a plain real number: no unit. */
static void sl_surreal(val_t v, val_t port) {
    Surreal *s = as_surreal(v);
    bool first = true;
    for (int i = 0; i < s->nterms; i++) {
        val_t c = s->data[2*i+1];
        if (sl_is_zero(c)) continue;
        bool has_unit = num_cmp(s->data[2*i], vfix(0)) != 0;
        sl_term_coef(port, c, first, has_unit);
        if (has_unit) sl_sur_unit(port, s, i);
        first = false;
    }
    if (first) port_write_char(port, '0');
}

/* \alpha|x\rangle + ... ; a complex amplitude is parenthesized. */
static void sl_quantum(val_t v, val_t port) {
    Quantum *q = as_quantum(v);
    if (q->n == 0) { port_write_char(port, '0'); return; }
    for (int i = 0; i < q->n; i++) {
        val_t amp = q->data[2*i];
        if (vis_complex(amp)) {
            if (i) pws(port, " + ");
            pws(port, "\\left(");
            sl_num(amp, port);
            pws(port, "\\right)");
        } else {
            sl_term_coef(port, amp, i == 0, true);
        }
        pws(port, "|");
        sl_num(q->data[2*i+1], port);
        pws(port, "\\rangle");
    }
}

/* True for a value written as a sum of several terms, which needs
 * parentheses inside a product or a power. */
static bool sl_is_sum_like(val_t v) {
    return vis_complex(v) || vis_quat(v) || vis_oct(v) || vis_mv(v) ||
           vis_surreal(v) || vis_quantum(v);
}

/* Returns true if it handled `v`. */
static bool sl_num_tower(val_t v, val_t port) {
    if (vis_complex(v)) { sl_sum(port, 2, sl_cpx_coef, as_cpx(v), sl_cpx_unit, true); return true; }
    if (vis_quat(v))    { sl_sum(port, 4, sl_quat_coef, as_quat(v), sl_quat_unit, true); return true; }
    if (vis_oct(v))     { sl_sum(port, 8, sl_oct_coef, as_oct(v), sl_oct_unit, true); return true; }
    if (vis_mv(v)) {
        sl_sum(port, (int)as_mv(v)->dim, sl_mv_coef, as_mv(v), sl_mv_unit, true);
        return true;
    }
    if (vis_surreal(v)) { sl_surreal(v, port); return true; }
    if (vis_quantum(v)) { sl_quantum(v, port); return true; }
    return false;
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
    if (vis_flonum(v) && !isfinite(vfloat(v))) {
        double d = vfloat(v);
        if (d != d)      pws(port, "\\mathrm{NaN}");
        else if (d < 0)  pws(port, "-\\infty");
        else             pws(port, "\\infty");
        return;
    }
    if (sl_num_tower(v, port)) return;
    if (vis_flonum(v)) { sl_flonum(vfloat(v), port); return; }
    if (vis_number(v)) { scm_display(v, port); return; }
    /* A string, or anything else that is not a number: its display text,
     * set upright and escaped so it cannot break out of the math. */
    val_t tmp = port_open_output_string();
    scm_display(v, tmp);
    val_t str = port_get_output_string(tmp);
    port_close(tmp);
    pws(port, "\\mathrm{");
    sl_write_escaped(port, str_data(as_str(str)), as_str(str)->len, true);
    port_write_char(port, '}');
}

/*
 * sl_latex(expr, ctx_prec, port)
 *
 * Writes expr in LaTeX notation. If sp_prec(expr) < ctx_prec the expression
 * is wrapped in \left(\right) for proper grouping.
 */
static void sl_latex(val_t expr, int ctx, val_t port) {
    /* Same unbounded-recursion class as sp_infix above (issue #134). */
    check_c_stack_depth("symbolic");
    if (!vis_symbolic(expr) && !vis_symfn(expr)) {
        bool paren = sl_is_sum_like(expr) && ctx >= SP_ADD;
        if (paren) pws(port, "\\left(");
        sl_num(expr, port);
        if (paren) pws(port, "\\right)");
        return;
    }
    if (vis_symvar(expr)) { sl_varname(expr, port); return; }
    if (vis_symfn(expr)) {
        Symbol *s = as_sym(as_symfn(expr)->name);
        sl_write_escaped(port, s->data, s->len, false);
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
    } else if (sp_is_cmp_op(op) && n == 2) {
        sl_latex(a[0], SP_ADD, port);
        if (op == SX_LT) pws(port, " < ");
        else if (op == SX_LE) pws(port, " \\leq ");
        else if (op == SX_GT) pws(port, " > ");
        else pws(port, " \\geq ");
        sl_latex(a[1], SP_ADD, port);
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
        sl_write_escaped(port, fsym->data, fsym->len, false);
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
            sl_write_escaped(port, ops->data, ops->len, true);
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
    /* Same unbounded-recursion class as sp_infix/sl_latex above (issue
     * #134): this prefix-notation writer self-recurses too. */
    check_c_stack_depth("symbolic");
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

/* ---- Cuneiform (best-effort) ---- */

/* op's registered Akkadian cuneiform alias, if any -- every core arithmetic
 * operator (+,-,*,/,...) already has one via akkadian_names.h/lang_pr_lookup
 * (the same mechanism modules.c uses to attach Akkadian aliases at import
 * time). Returns NULL for operators with no registered alias at all (most
 * CAS-specific ops: sin, expt, ...), or none of whose registered forms
 * happen to be the cuneiform one. */
static Symbol *sxc_op_glyph(val_t op) {
    val_t forms[LANG_PR_MAX_FORMS];
    int n = lang_pr_lookup(op, forms, LANG_PR_MAX_FORMS);
    for (int i = 0; i < n; i++) {
        if (!vis_symbol(forms[i])) continue;
        Symbol *s = as_sym(forms[i]);
        /* Symbol.data is NUL-terminated (object.h), so sex_decode_cp's
         * end-of-string check works directly on it; reuse it (validated
         * continuation-byte checking) rather than a second hand-rolled
         * UTF-8 decoder. */
        const char *p = s->data;
        if (SEX_IS_CUNEIFORM((uint32_t)sex_decode_cp(&p))) return s;
    }
    return NULL;
}

static void sxc_write(val_t expr, val_t port) {
    /* Same unbounded-recursion class as sx_write/sp_infix/sl_latex above
     * (issue #134): a fourth self-recursive symbolic-expression writer,
     * for the 'cuneiform notation. Not independently demonstrated to be
     * exploitable (its own per-level stack frame is far smaller than
     * the construction-side functions above, so in practice a tree
     * survives construction only if it's already too shallow to reach
     * this guard) -- added for defense-in-depth and consistency with
     * the other three writers rather than a confirmed crash. */
    check_c_stack_depth("symbolic");
    if (!vis_symbolic(expr) && !vis_symfn(expr)) {
        /* Numeric leaf: render in cuneiform when the value supports it
         * (sex_to_cuneiform already falls back to plain decimal itself). */
        val_t s = sex_to_cuneiform(expr);
        port_write_string(port, str_data(as_str(s)), as_str(s)->len);
        return;
    }
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

    SymExpr *se = as_symexpr(expr);
    val_t    op = se->op;
    int      n  = (int)se->nargs;
    Symbol  *glyph = sxc_op_glyph(op);

    if (glyph && n >= 2) {
        /* n-ary operator (+, -, *, /, ...): infix. */
        port_write_char(port, '(');
        sxc_write(se->args[0], port);
        for (int i = 1; i < n; i++) {
            port_write_string(port, glyph->data, glyph->len);
            sxc_write(se->args[i], port);
        }
        port_write_char(port, ')');
        return;
    }

    /* Unary function (sin, neg, ...) with a registered glyph: prefix,
     * using the glyph as the function name -- infix makes no sense for a
     * single argument, and would otherwise silently drop the operator
     * entirely (n==1 has no "remaining terms" to loop over). Also covers
     * n==0 defensively (falls through to the same shape with no args). */
    Symbol *name = glyph ? glyph : as_sym(op);
    port_write_string(port, name->data, name->len);
    port_write_char(port, '(');
    for (int i = 0; i < n; i++) {
        if (i > 0) port_write_char(port, ',');
        sxc_write(se->args[i], port);
    }
    port_write_char(port, ')');
}

void sx_write_cuneiform(val_t expr, val_t port) { sxc_write(expr, port); }
