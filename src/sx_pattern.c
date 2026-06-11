#include "sx_pattern.h"
#include "symbol.h"
#include "object.h"
#include "builtins.h"  /* scm_cons */
#include "set.h"       /* scm_equal */
#include <string.h>

/* ---- helpers ---- */

int sx_is_pvar(val_t v) {
    if (!vis_symbol(v)) return 0;
    return as_sym(v)->data[0] == '?';
}

/* Append item to list only if not already present (pointer equality for pvars) */
static val_t list_adjoin(val_t lst, val_t item) {
    val_t scan = lst;
    while (vis_pair(scan)) {
        if (vcar(scan) == item) return lst;
        scan = vcdr(scan);
    }
    return scm_cons(item, lst);
}

/* Collect pvars in left-to-right DFS order into acc (reversed).
   We build reversed then reverse at the end for correct order. */
static val_t collect_pvars(val_t pattern, val_t acc) {
    if (sx_is_pvar(pattern))
        return list_adjoin(acc, pattern);
    if (vis_pair(pattern)) {
        /* (op arg ...) — skip op, walk args */
        val_t args = vcdr(pattern);
        while (vis_pair(args)) {
            acc = collect_pvars(vcar(args), acc);
            args = vcdr(args);
        }
    }
    return acc;
}

val_t sx_pattern_vars(val_t pattern) {
    val_t rev = collect_pvars(pattern, V_NIL);
    /* reverse so order matches left-to-right appearance */
    val_t result = V_NIL;
    while (vis_pair(rev)) { result = scm_cons(vcar(rev), result); rev = vcdr(rev); }
    return result;
}

/* ---- binding alist utilities ---- */

/* Look up key in alist; returns the value or V_VOID if absent */
static val_t alist_get(val_t alist, val_t key) {
    while (vis_pair(alist)) {
        val_t kv = vcar(alist);
        if (vcar(kv) == key) return vcdr(kv);
        alist = vcdr(alist);
    }
    return V_VOID;
}

/* Merge src into dst.  If the same key appears in both with different values
   (structural inequality via scm_equal) the merge fails and returns V_FALSE. */
static val_t merge_bindings(val_t dst, val_t src) {
    while (vis_pair(src)) {
        val_t kv  = vcar(src);
        val_t key = vcar(kv);
        val_t val = vcdr(kv);
        val_t existing = alist_get(dst, key);
        if (existing != V_VOID) {
            if (!scm_equal(existing, val)) return V_FALSE; /* conflict */
        } else {
            dst = scm_cons(kv, dst);
        }
        src = vcdr(src);
    }
    return dst;
}

/* ---- numeric literal equality ---- */

static int literal_eq(val_t pat, val_t expr) {
    if (pat == expr) return 1;
    /* both fixnums already caught by == above; handle rationals etc. */
    if (!vis_number(pat) || !vis_number(expr)) return 0;
    return scm_equal(pat, expr);
}

/* ---- main matcher ---- */

val_t sx_pattern_match(val_t pattern, val_t expr) {
    /* Pattern variable: matches anything, emits one binding */
    if (sx_is_pvar(pattern))
        return scm_cons(scm_cons(pattern, expr), V_NIL);

    /* Anonymous wildcard: matches anything, no binding */
    if (pattern == S_UNDERSCORE)
        return V_NIL;

    /* Numeric literal */
    if (vis_number(pattern))
        return literal_eq(pattern, expr) ? V_NIL : V_FALSE;

    /* Non-pvar symbol: must match exactly (e.g. operator in head position).
       This branch is reached when a bare symbol appears as an arg pattern,
       e.g. matching the exact operator sym-var 'x. */
    if (vis_symbol(pattern))
        return (pattern == expr) ? V_NIL : V_FALSE;

    /* Structural list pattern: (op arg...) matched against a SymExpr */
    if (vis_pair(pattern) && vis_symexpr(expr)) {
        SymExpr *se = as_symexpr(expr);
        val_t pop  = vcar(pattern);   /* expected operator */
        val_t pargs = vcdr(pattern);  /* arg sub-patterns */

        if (pop != se->op) return V_FALSE;

        /* Count pattern args */
        int n = 0;
        val_t tmp = pargs;
        while (vis_pair(tmp)) { n++; tmp = vcdr(tmp); }
        if (n != (int)se->nargs) return V_FALSE;

        val_t bindings = V_NIL;
        val_t pa = pargs;
        for (int i = 0; i < n; i++) {
            val_t sub = sx_pattern_match(vcar(pa), se->args[i]);
            if (sub == V_FALSE) return V_FALSE;
            bindings = merge_bindings(bindings, sub);
            if (bindings == V_FALSE) return V_FALSE;
            pa = vcdr(pa);
        }
        return bindings;
    }

    return V_FALSE;
}
