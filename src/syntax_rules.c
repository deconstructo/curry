#include "syntax_rules.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "eval.h"
#include "env.h"
#include "builtins.h"
#include "set.h"
#include <stdbool.h>
#include <string.h>

/* ---- Interned symbols ---- */

static val_t SR_ELLIPSIS;   /* "..." */
static val_t SR_UNDERSCORE; /* "_"   */

/* ---- Compiled transformer data ---- */

typedef struct {
    val_t literals; /* list of literal symbols */
    val_t rules;    /* list of (pattern . template) pairs */
} SyntaxRulesData;

/* ---- Pattern helpers ---- */

static bool sr_is_literal(val_t sym, val_t literals) {
    for (val_t l = literals; vis_pair(l); l = vcdr(l))
        if (vcar(l) == sym) return true;
    return false;
}

static bool sr_is_pvar(val_t sym, val_t literals) {
    if (!vis_symbol(sym)) return false;
    if (sym == SR_ELLIPSIS || sym == SR_UNDERSCORE) return false;
    return !sr_is_literal(sym, literals);
}

/* Collect all pattern variable names from a pattern into a Scheme list. */
static val_t sr_pvars(val_t pat, val_t literals) {
    if (vis_symbol(pat))
        return sr_is_pvar(pat, literals) ? scm_cons(pat, V_NIL) : V_NIL;
    if (!vis_pair(pat)) return V_NIL;
    val_t result = V_NIL;
    while (vis_pair(pat)) {
        val_t elem = vcar(pat);
        val_t next = vcdr(pat);
        if (vis_pair(next) && vcar(next) == SR_ELLIPSIS) {
            result = scm_append(result, sr_pvars(elem, literals));
            pat = vcdr(next);
        } else {
            result = scm_append(result, sr_pvars(elem, literals));
            pat = next;
        }
    }
    /* dotted tail */
    if (vis_symbol(pat) && sr_is_pvar(pat, literals))
        result = scm_append(result, scm_cons(pat, V_NIL));
    return result;
}

/* ---- Pattern matching ---- */

/* Forward declaration */
static bool sr_match_list(val_t pat_rest, val_t form_rest, val_t literals,
                          val_t *bindings, val_t *ell_bindings);

/* Match a single pattern element against a single form value.
 * Appends to *bindings (non-ellipsis vars) and *ell_bindings (ellipsis vars). */
static bool sr_match_one(val_t pat, val_t form, val_t literals,
                         val_t *bindings, val_t *ell_bindings) {
    if (vis_symbol(pat)) {
        if (pat == SR_UNDERSCORE) return true;
        if (sr_is_literal(pat, literals))
            return vis_symbol(form) && form == pat;
        /* Pattern variable — bind it */
        *bindings = scm_cons(scm_cons(pat, form), *bindings);
        return true;
    }
    if (vis_pair(pat)) {
        if (!vis_pair(form) && !vis_nil(form)) return false;
        return sr_match_list(pat, form, literals, bindings, ell_bindings);
    }
    /* Datum: must be eqv? */
    return scm_equal(pat, form);
}

/* Match the rest of a pattern list against the rest of a form list.
 * Handles ellipsis sub-patterns. */
static bool sr_match_list(val_t pat_rest, val_t form_rest, val_t literals,
                          val_t *bindings, val_t *ell_bindings) {
    while (vis_pair(pat_rest)) {
        val_t pat_elem = vcar(pat_rest);
        val_t pat_next = vcdr(pat_rest);

        /* Check for ellipsis following this element */
        if (vis_pair(pat_next) && vcar(pat_next) == SR_ELLIPSIS) {
            val_t after_ellipsis = vcdr(pat_next);

            /* Count required elements after the ellipsis */
            int n_after = 0;
            for (val_t tmp = after_ellipsis; vis_pair(tmp); tmp = vcdr(tmp)) n_after++;

            /* Count available elements */
            int n_avail = 0;
            for (val_t tmp = form_rest; vis_pair(tmp); tmp = vcdr(tmp)) n_avail++;

            int n_ell = n_avail - n_after;
            if (n_ell < 0) return false;

            /* Discover pattern variables in pat_elem */
            val_t pvlist = sr_pvars(pat_elem, literals);
            int npv = scm_list_length(pvlist);

            /* Allocate accumulator arrays (in-order, built by prepend then reverse) */
            val_t *pnames = npv > 0 ? gc_alloc((size_t)npv * sizeof(val_t)) : NULL;
            val_t *paccs  = npv > 0 ? gc_alloc((size_t)npv * sizeof(val_t)) : NULL;
            int pi = 0;
            for (val_t pv = pvlist; vis_pair(pv); pv = vcdr(pv), pi++) {
                pnames[pi] = vcar(pv);
                paccs[pi]  = V_NIL;
            }

            /* Match each of the n_ell elements against pat_elem */
            for (int i = 0; i < n_ell; i++) {
                val_t sb = V_NIL, se = V_NIL;
                if (!sr_match_one(pat_elem, vcar(form_rest), literals, &sb, &se))
                    return false;
                /* Prepend each matched value to its accumulator */
                for (int j = 0; j < npv; j++) {
                    val_t val = V_NIL;
                    for (val_t s = sb; vis_pair(s); s = vcdr(s))
                        if (vcar(vcar(s)) == pnames[j]) { val = vcdr(vcar(s)); break; }
                    paccs[j] = scm_cons(val, paccs[j]);
                }
                form_rest = vcdr(form_rest);
            }

            /* Reverse accumulators and add to ell_bindings */
            for (int j = 0; j < npv; j++)
                *ell_bindings = scm_cons(scm_cons(pnames[j], scm_reverse(paccs[j])),
                                         *ell_bindings);

            pat_rest = after_ellipsis;
            continue;
        }

        /* Non-ellipsis element: form must have a matching element */
        if (!vis_pair(form_rest)) return false;
        if (!sr_match_one(pat_elem, vcar(form_rest), literals, bindings, ell_bindings))
            return false;

        pat_rest  = vcdr(pat_rest);
        form_rest = vcdr(form_rest);
    }

    /* Dotted pattern tail: match remaining form against the tail symbol */
    if (vis_symbol(pat_rest))
        return sr_match_one(pat_rest, form_rest, literals, bindings, ell_bindings);

    /* Both must be exhausted */
    return vis_nil(pat_rest) && vis_nil(form_rest);
}

/* ---- Template expansion ---- */

/* Return the subset of ell_bindings whose names appear in tmpl. */
static val_t sr_ell_refs(val_t tmpl, val_t ell_bindings) {
    if (vis_symbol(tmpl)) {
        for (val_t e = ell_bindings; vis_pair(e); e = vcdr(e))
            if (vcar(vcar(e)) == tmpl) return scm_cons(vcar(e), V_NIL);
        return V_NIL;
    }
    if (!vis_pair(tmpl)) return V_NIL;
    val_t result = V_NIL;
    while (vis_pair(tmpl)) {
        val_t sub = sr_ell_refs(vcar(tmpl), ell_bindings);
        for (val_t s = sub; vis_pair(s); s = vcdr(s)) {
            bool dup = false;
            for (val_t r = result; vis_pair(r); r = vcdr(r))
                if (vcar(vcar(r)) == vcar(vcar(s))) { dup = true; break; }
            if (!dup) result = scm_cons(vcar(s), result);
        }
        tmpl = vcdr(tmpl);
    }
    return result;
}

/* Expand a template given non-ellipsis bindings and ellipsis bindings. */
static val_t sr_expand(val_t tmpl, val_t bindings, val_t ell_bindings) {
    if (vis_symbol(tmpl)) {
        /* Look up in simple bindings first */
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            if (vcar(vcar(b)) == tmpl) return vcdr(vcar(b));
        /* Introduced identifier — emit as-is (unhygienic) */
        return tmpl;
    }

    if (!vis_pair(tmpl)) return tmpl; /* self-evaluating datum */

    /* Template is a list — process element by element */
    val_t result = V_NIL;
    val_t t = tmpl;
    while (vis_pair(t)) {
        val_t elem = vcar(t);
        val_t rest = vcdr(t);

        /* Ellipsis following this element? */
        if (vis_pair(rest) && vcar(rest) == SR_ELLIPSIS) {
            /* Find ellipsis-bound vars referenced by elem */
            val_t refs = sr_ell_refs(elem, ell_bindings);
            if (vis_pair(refs)) {
                int n = scm_list_length(vcdr(vcar(refs)));
                for (int i = 0; i < n; i++) {
                    /* Build per-iteration bindings: bind each ell var to its i-th value */
                    val_t iter_bindings = bindings;
                    for (val_t r = refs; vis_pair(r); r = vcdr(r)) {
                        val_t name = vcar(vcar(r));
                        val_t val  = scm_list_ref(vcdr(vcar(r)), i);
                        iter_bindings = scm_cons(scm_cons(name, val), iter_bindings);
                    }
                    result = scm_append(result,
                                 scm_cons(sr_expand(elem, iter_bindings, ell_bindings),
                                          V_NIL));
                }
            }
            /* refs is empty: zero repetitions — splice nothing */
            t = vcdr(rest); /* skip elem and ... */
            continue;
        }

        result = scm_append(result,
                     scm_cons(sr_expand(elem, bindings, ell_bindings), V_NIL));
        t = rest;
    }

    /* Dotted template tail */
    if (!vis_nil(t))
        result = scm_append(result, sr_expand(t, bindings, ell_bindings));

    return result;
}

/* ---- Transformer function (called when the macro is used) ---- */

static val_t sr_transformer_fn(int ac, val_t *av, void *ud) {
    (void)ac;
    val_t form = av[0]; /* entire unevaluated use-site form */
    SyntaxRulesData *sr = ud;

    for (val_t rules = sr->rules; vis_pair(rules); rules = vcdr(rules)) {
        val_t rule = vcar(rules);
        val_t pat  = vcar(rule);
        val_t tmpl = vcdr(rule);

        /* Skip the keyword position in the pattern */
        val_t pat_rest  = vcdr(pat);
        val_t form_rest = vcdr(form);

        val_t bindings = V_NIL, ell_bindings = V_NIL;
        if (sr_match_list(pat_rest, form_rest, sr->literals, &bindings, &ell_bindings))
            return sr_expand(tmpl, bindings, ell_bindings);
    }

    val_t kw = vis_pair(form) ? vcar(form) : V_FALSE;
    if (vis_symbol(kw))
        scm_raise(V_FALSE, "syntax-rules: no matching pattern for %s", sym_cstr(kw));
    scm_raise(V_FALSE, "syntax-rules: no matching pattern");
}

/* ---- Compile function (registered as the T_SYNTAX transformer) ---- */

/* Called by eval when it sees (syntax-rules ...) in a T_SYNTAX position.
 * av[0] = the whole unevaluated (syntax-rules literals rule...) form.
 * Returns a T_PRIMITIVE that acts as the macro transformer. */
static val_t sr_compile_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t form     = av[0];
    val_t literals = vcadr(form);
    val_t rules_kv = vcddr(form);

    /* Compile rules into (pattern . template) pairs */
    val_t compiled = V_NIL;
    for (val_t r = rules_kv; vis_pair(r); r = vcdr(r)) {
        val_t rule = vcar(r);
        val_t pat  = vcar(rule);
        val_t tmpl = vcadr(rule);
        compiled = scm_cons(scm_cons(pat, tmpl), compiled);
    }
    compiled = scm_reverse(compiled);

    SyntaxRulesData *sr = gc_alloc(sizeof(SyntaxRulesData));
    sr->literals = literals;
    sr->rules    = compiled;

    Primitive *p = CURRY_NEW(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name     = "syntax-rules-transformer";
    p->min_args = 1; p->max_args = 1;
    p->fn  = sr_transformer_fn;
    p->ud  = sr;
    return vptr(p);
}

/* ---- Registration ---- */

void syntax_rules_register(val_t env) {
    SR_ELLIPSIS   = sym_intern_cstr("...");
    SR_UNDERSCORE = sym_intern_cstr("_");

    /* syntax-rules is itself a T_SYNTAX: eval passes the unevaluated form to
     * sr_compile_fn, which returns a T_PRIMITIVE transformer.  That primitive
     * self-evaluates (non-pair heap object), so define-syntax receives it
     * directly and wraps it in its own T_SYNTAX struct. */
    Primitive *compile_prim = CURRY_NEW(Primitive);
    compile_prim->hdr.type = T_PRIMITIVE; compile_prim->hdr.flags = 0;
    compile_prim->name     = "syntax-rules-compile";
    compile_prim->min_args = 1; compile_prim->max_args = 1;
    compile_prim->fn  = sr_compile_fn;
    compile_prim->ud  = NULL;

    Syntax *syn = CURRY_NEW(Syntax);
    syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
    syn->transformer = vptr(compile_prim);

    env_define(env, sym_intern_cstr("syntax-rules"), vptr(syn));
}
