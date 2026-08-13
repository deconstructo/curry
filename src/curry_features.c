#include "curry_features.h"
#include "object.h"
#include "symbol.h"
#include "builtins.h"
#include "modules.h"
#include "eval.h"
#include <stdint.h>

/* Feature identifiers this build supports, in R7RS's own conventional
 * order (implementation name and dialect first, then capability tags,
 * then platform tags). 'curry is the identifier a portable library
 * (like SRFI-279's own 279.sld) writes a curry-specific cond-expand
 * branch against, the same way it already branches on 'chibi/'kawa/'guile. */
static const char *const FEATURE_NAMES[] = {
    "curry",
    "r7rs",
    "exact-closed",  /* GMP rationals: exact/exact division of nonzero is always exact */
    "ratios",        /* exact rationals in the numeric tower */
    "full-unicode",  /* strings/chars are Unicode codepoints throughout */
#if defined(__APPLE__)
    "darwin",
#elif defined(__linux__)
    "linux",
#endif
#if defined(__APPLE__) || defined(__linux__)
    "unix",
    "posix",
#endif
#if UINTPTR_MAX == 0xffffffffffffffffULL
    "lp64",
#else
    "ilp32",
#endif
    NULL
};

static val_t g_features_list = V_FALSE; /* V_FALSE = not yet built */

val_t features_list(void) {
    if (g_features_list == V_FALSE) {
        val_t list = V_NIL;
        /* Build in reverse so the resulting list preserves FEATURE_NAMES' order. */
        int n = 0;
        while (FEATURE_NAMES[n]) n++;
        for (int i = n - 1; i >= 0; i--)
            list = scm_cons(sym_intern_cstr(FEATURE_NAMES[i]), list);
        g_features_list = list;
    }
    return g_features_list;
}

static bool features_has(val_t id) {
    for (val_t f = features_list(); vis_pair(f); f = vcdr(f))
        if (vcar(f) == id) return true;
    return false;
}

bool features_test(val_t req) {
    if (vis_symbol(req)) return features_has(req);

    if (vis_pair(req)) {
        val_t head = vcar(req);

        if (head == S_AND) {
            for (val_t r = vcdr(req); vis_pair(r); r = vcdr(r))
                if (!features_test(vcar(r))) return false;
            return true;
        }
        if (head == S_OR) {
            for (val_t r = vcdr(req); vis_pair(r); r = vcdr(r))
                if (features_test(vcar(r))) return true;
            return false;
        }
        if (head == S_NOT) {
            return vis_pair(vcdr(req)) && !features_test(vcadr(req));
        }
        if (head == S_LIBRARY) {
            return vis_pair(vcdr(req)) && modules_available(vcadr(req));
        }
    }

    return false;
}

val_t cond_expand_choose(val_t clauses, bool *matched) {
    for (val_t cl = clauses; vis_pair(cl); cl = vcdr(cl)) {
        val_t clause = vcar(cl);
        if (!vis_pair(clause))
            scm_raise(V_FALSE, "cond-expand: malformed clause (not a list)");
        val_t req = vcar(clause);
        if (req == S_ELSE || features_test(req)) {
            *matched = true;
            return vcdr(clause);
        }
    }
    *matched = false;
    return V_NIL;
}
