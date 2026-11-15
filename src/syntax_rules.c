#include "syntax_rules.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "eval.h"
#include "env.h"
#include "builtins.h"
#include "set.h"
#include "lang_registry.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* ---- Interned symbols ---- */

static val_t SR_DEFAULT_ELLIPSIS; /* "..." */
static val_t SR_UNDERSCORE;       /* "_"   */

/* ---- Compiled transformer data ---- */

typedef struct {
    val_t literals; /* list of literal symbols */
    val_t rules;    /* list of (pattern . template) pairs */
    val_t ellipsis; /* the ellipsis identifier this instance uses (usually
                        SR_DEFAULT_ELLIPSIS, but overridable per R7RS
                        4.3.2's (syntax-rules ellipsis (literal ...) rule ...)
                        form) */
    val_t def_env;  /* the environment (syntax-rules ...) was evaluated in
                        -- see sr_current_env's header comment below for why
                        this is captured once here rather than looked up
                        per-expansion. Used by sr_is_protected to decide
                        whether a template-introduced symbol already refers
                        to something real in the macro's OWN defining scope
                        (not the use site) before renaming it. */
    val_t local_macros; /* list of compile-time-local macro names (compiler.c
                        let-syntax/letrec-syntax/internal define-syntax) in
                        scope alongside def_env -- see sr_current_local_macros'
                        header comment for why def_env alone isn't enough for
                        these. Always V_NIL for a top-level or tree-walked
                        macro, where def_env already covers it. */
    val_t local_vars;   /* names of the local variables of the enclosing
                        function(s) at the point the macro was defined
                        (issue #361, second case). They are real
                        references the template may use, so they are never
                        renamed. Only the compiler fills this in: a tree-
                        walked macro's def_env is a real runtime frame that
                        already holds them. V_NIL for everything else. */
} SyntaxRulesData;

/* ---- Current environment for (syntax-rules ...) evaluation ----
 *
 * sr_compile_fn (the "syntax-rules" T_SYNTAX transformer itself) needs to
 * know what environment IT is being evaluated in, to capture as the new
 * transformer's def_env -- but like every Primitive.fn, it receives no
 * environment parameter at all (Primitive's calling convention is
 * (argc, argv, userdata), env isn't part of it). Rather than changing
 * that convention everywhere, eval.c's T_SYNTAX dispatch (the one place
 * that already has `env` in scope right where it calls apply() on any
 * transformer, syntax-rules included) saves/sets/restores this thread-
 * local around that one call, the same save-a-previous-value/restore-it
 * pattern current_handler's own chain uses elsewhere in this codebase.
 * compiler.c's compiled path never touches this at all: compile_time_eval
 * (compiler.c) already documents that a compiled define-syntax's
 * transformer-expr can only ever see GLOBAL_ENV (no enclosing lambda's
 * locals/upvalues) -- V_FALSE here means exactly that "not currently
 * tracked, must be the compiled path" case, and sr_compile_fn maps it to
 * GLOBAL_ENV directly, so no separate handling is needed on that path. */
static CURRY_THREAD_LOCAL val_t sr_current_env = V_FALSE;

val_t sr_get_current_env(void) { return sr_current_env; }
void  sr_set_current_env(val_t env) { sr_current_env = env; }

/* Companion to sr_current_env, for compiler.c's let-syntax/letrec-syntax
 * and locally-scoped (scope_depth != 0) define-syntax. Those macros'
 * names live ONLY in the compiler's own compile-time SyntaxLocal table
 * (add_syntax_local/resolve_syntax_local, compiler.c) -- never in any
 * runtime EnvFrame env_lookup_slot can see, unlike a top-level
 * define-syntax (env_define(GLOBAL_ENV, ...)) or a tree-walked library
 * body's define-syntax (a real, if isolated, runtime env). Without this,
 * sr_is_protected's def_env-based check has no way to know a locally-
 * scoped macro's own name (or a letrec-syntax sibling's), so a locally-
 * scoped macro's self-recursion got incorrectly renamed and broke --
 * found by review, verified via a minimal letrec-syntax self-recursion
 * repro. compile_let_syntax/compile_define_syntax set this to the
 * relevant local macro name(s) (unioned with whatever was already set,
 * so nested let-syntax blocks see outer local macros too) around each
 * compile_time_eval call, save/restore-style. */
static CURRY_THREAD_LOCAL val_t sr_current_local_macros = V_NIL;

val_t sr_get_current_local_macros(void) { return sr_current_local_macros; }
void  sr_set_current_local_macros(val_t names) { sr_current_local_macros = names; }

/* Companion to sr_current_local_macros: the local VARIABLES of the
 * enclosing function(s). Without it a macro defined inside a function and
 * referring to one of that function's locals -- (define (f y)
 * (define-syntax addy (syntax-rules () ((_ a) (+ a y)))) (addy 1)) --
 * had `y` renamed to a gensym, because the compiled path's def_env is
 * GLOBAL_ENV, which holds no locals, so `y` looked like an introduced
 * binder (issue #361). compile_define_syntax/compile_let_syntax set it
 * around each compile_time_eval call, save/restore-style. */
static CURRY_THREAD_LOCAL val_t sr_current_local_vars = V_NIL;

val_t sr_get_current_local_vars(void) { return sr_current_local_vars; }
void  sr_set_current_local_vars(val_t names) { sr_current_local_vars = names; }

/* ---- Vector <-> list helpers, so patterns/templates need only be taught
 * about pairs; a vector pattern/template is handled by converting to/from
 * a list at the entry point and reusing all the list logic unchanged. ---- */

static val_t sr_vector_to_list(val_t v) {
    Vector *vec = as_vec(v);
    val_t r = V_NIL;
    for (int i = (int)vec->len - 1; i >= 0; i--) r = scm_cons(vec->data[i], r);
    return r;
}

static val_t sr_list_to_vector(val_t lst) {
    int n = scm_list_length(lst);
    Vector *v = CURRY_NEW_FLEX(Vector, (uint32_t)n);
    v->hdr.type = T_VECTOR; v->hdr.flags = 0; v->len = (uint32_t)n;
    for (int i = 0; i < n; i++) { v->data[i] = vcar(lst); lst = vcdr(lst); }
    return vptr(v);
}

/* ---- Pattern helpers ---- */

static bool sr_is_literal(val_t sym, val_t literals) {
    for (val_t l = literals; vis_pair(l); l = vcdr(l))
        if (vcar(l) == sym) return true;
    return false;
}

static bool sr_is_pvar(val_t sym, val_t literals, val_t ellipsis) {
    if (!vis_symbol(sym)) return false;
    if (sym == ellipsis || sym == SR_UNDERSCORE) return false;
    return !sr_is_literal(sym, literals);
}

/* Collect all pattern variable names from a pattern into a Scheme list. */
static val_t sr_pvars(val_t pat, val_t literals, val_t ellipsis) {
    if (vis_vector(pat)) pat = sr_vector_to_list(pat);
    if (vis_symbol(pat))
        return sr_is_pvar(pat, literals, ellipsis) ? scm_cons(pat, V_NIL) : V_NIL;
    if (!vis_pair(pat)) return V_NIL;
    val_t result = V_NIL;
    while (vis_pair(pat)) {
        val_t elem = vcar(pat);
        val_t next = vcdr(pat);
        if (vis_pair(next) && vcar(next) == ellipsis) {
            result = scm_append(result, sr_pvars(elem, literals, ellipsis));
            pat = vcdr(next);
        } else {
            result = scm_append(result, sr_pvars(elem, literals, ellipsis));
            pat = next;
        }
    }
    /* dotted tail */
    if (vis_symbol(pat) && sr_is_pvar(pat, literals, ellipsis))
        result = scm_append(result, scm_cons(pat, V_NIL));
    return result;
}

/* ---- Pattern matching ---- */

/* Forward declaration */
static bool sr_match_list(val_t pat_rest, val_t form_rest, val_t literals,
                          val_t ellipsis, val_t *bindings, val_t *ell_bindings);

/* Match a single pattern element against a single form value.
 * Appends to *bindings (non-ellipsis vars) and *ell_bindings (ellipsis vars). */
static bool sr_match_one(val_t pat, val_t form, val_t literals, val_t ellipsis,
                         val_t *bindings, val_t *ell_bindings) {
    if (vis_symbol(pat)) {
        /* A macro may explicitly declare "_" as one of its own literals
         * (Alex Shinn's match.scm does exactly this, matching "_" used
         * literally as a match-pattern's own wildcard marker against the
         * caller's actual use-site symbol "_") — that declaration must
         * take priority over the universal wildcard special-case below,
         * or "_" would swallow every macro call regardless of what the
         * caller actually passed there, never reaching any later clause
         * (including whatever clause was meant to bind an ordinary new
         * pattern variable). Checked first, same as any other literal. */
        if (sr_is_literal(pat, literals))
            return (vis_symbol(form) && form == pat) ||
                   (vis_synobj(form) && as_synobj(form)->name == pat);
        if (pat == SR_UNDERSCORE) return true;
        /* Pattern variable — bind it */
        *bindings = scm_cons(scm_cons(pat, form), *bindings);
        return true;
    }
    if (vis_vector(pat)) {
        if (!vis_vector(form)) return false;
        return sr_match_list(sr_vector_to_list(pat), sr_vector_to_list(form),
                              literals, ellipsis, bindings, ell_bindings);
    }
    if (vis_pair(pat)) {
        if (!vis_pair(form) && !vis_nil(form)) return false;
        return sr_match_list(pat, form, literals, ellipsis, bindings, ell_bindings);
    }
    /* Datum: must be eqv? */
    return scm_equal(pat, form);
}

/* Match the rest of a pattern list against the rest of a form list.
 * Handles ellipsis sub-patterns. */
static bool sr_match_list(val_t pat_rest, val_t form_rest, val_t literals,
                          val_t ellipsis, val_t *bindings, val_t *ell_bindings) {
    while (vis_pair(pat_rest)) {
        val_t pat_elem = vcar(pat_rest);
        val_t pat_next = vcdr(pat_rest);

        /* Check for ellipsis following this element */
        if (vis_pair(pat_next) && vcar(pat_next) == ellipsis) {
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
            val_t pvlist = sr_pvars(pat_elem, literals, ellipsis);
            int npv = scm_list_length(pvlist);

            /* Allocate accumulator arrays (in-order, built by prepend then reverse) */
            val_t *pnames = npv > 0 ? gc_alloc_raw_pinned((size_t)npv * sizeof(val_t)) : NULL;
            val_t *paccs  = npv > 0 ? gc_alloc_raw_pinned((size_t)npv * sizeof(val_t)) : NULL;
            int pi = 0;
            for (val_t pv = pvlist; vis_pair(pv); pv = vcdr(pv), pi++) {
                pnames[pi] = vcar(pv);
                paccs[pi]  = V_NIL;
            }

            /* Match each of the n_ell elements against pat_elem */
            for (int i = 0; i < n_ell; i++) {
                val_t sb = V_NIL, se = V_NIL;
                if (!sr_match_one(pat_elem, vcar(form_rest), literals, ellipsis, &sb, &se))
                    return false;
                /* Prepend each matched value to its accumulator.
                 *
                 * pnames[j] can land in EITHER sb or se, depending on
                 * whether it has its own further ellipsis WITHIN pat_elem
                 * (a nested-ellipsis pattern like "(a b ...) ..." -- here
                 * `a` has no further ellipsis inside pat_elem, so its
                 * match lands in sb; `b` has ITS OWN "..." inside
                 * pat_elem, so its match lands in se instead, already
                 * correctly shaped as one whole nested list per group by
                 * this exact same logic applying recursively one level
                 * down). Previously this only ever checked sb, so any
                 * pattern variable with its own nested ellipsis silently
                 * got V_NIL every time (issue #101) -- confirmed
                 * minimal repro:
                 *   (define-syntax my-test
                 *     (syntax-rules () ((_ (a b ...) ...) '((a b ...) ...))))
                 *   (my-test (1 10 20) (2 30))
                 *   ; => ((1 () ())) instead of ((1 10 20) (2 30))
                 * Falling back to se when not found in sb fixes this, and
                 * composes correctly to arbitrary nesting depth by
                 * induction: se's own entries are already properly
                 * shaped by this same fallback having already applied
                 * one level further in, via the recursive
                 * sr_match_one -> sr_match_list call above. */
                for (int j = 0; j < npv; j++) {
                    val_t val = V_NIL;
                    bool found = false;
                    for (val_t s = sb; vis_pair(s); s = vcdr(s))
                        if (vcar(vcar(s)) == pnames[j]) { val = vcdr(vcar(s)); found = true; break; }
                    if (!found)
                        for (val_t s = se; vis_pair(s); s = vcdr(s))
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
        if (!sr_match_one(pat_elem, vcar(form_rest), literals, ellipsis, bindings, ell_bindings))
            return false;

        pat_rest  = vcdr(pat_rest);
        form_rest = vcdr(form_rest);
    }

    /* Dotted pattern tail: match remaining form against the tail symbol */
    if (vis_symbol(pat_rest))
        return sr_match_one(pat_rest, form_rest, literals, ellipsis, bindings, ell_bindings);

    /* Both must be exhausted */
    return vis_nil(pat_rest) && vis_nil(form_rest);
}

/* ---- Template expansion ---- */

/* Return the subset of ell_bindings whose names appear in tmpl. */
static val_t sr_ell_refs(val_t tmpl, val_t ell_bindings) {
    if (vis_vector(tmpl)) tmpl = sr_vector_to_list(tmpl);
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
    /* Dotted tail: the loop above only walks tmpl's proper-list portion,
     * so a variable captured through a dotted-pair tail (e.g. "rest" in
     * a (pat . rest) ... template) is otherwise never found here — and
     * sr_expand_list only substitutes per-iteration for names sr_ell_refs
     * reports, so a missed dotted-tail variable would fall through to
     * sr_expand's "unbound identifier, emit as-is" case and appear
     * unexpanded in the output. sr_match_list and sr_pvars both already
     * handle this same dotted-tail case; this mirrors them. */
    if (vis_symbol(tmpl)) {
        val_t sub = sr_ell_refs(tmpl, ell_bindings);
        for (val_t s = sub; vis_pair(s); s = vcdr(s)) {
            bool dup = false;
            for (val_t r = result; vis_pair(r); r = vcdr(r))
                if (vcar(vcar(r)) == vcar(vcar(s))) { dup = true; break; }
            if (!dup) result = scm_cons(vcar(s), result);
        }
    }
    return result;
}

/* ---- Partial hygiene: per-expansion renaming of template-introduced,
 * never-applied identifiers ----
 *
 * True hygiene (identifiers carrying lexical "color"/marks, resolved
 * against the right environment at every reference) would mean giving
 * every symbol a lexical-context tag and threading that through
 * evaluation/compilation everywhere identifiers are compared or looked
 * up -- compiler.c's variable resolution, env_lookup, every `==`
 * special-form-keyword dispatch in eval.c/compiler.c. That's a rewrite
 * of how identifiers are represented throughout the interpreter, not a
 * macro-expander-local fix.
 *
 * What actually breaks in practice -- confirmed via a minimal repro
 * during SRFI-26 porting: a recursive macro (the standard reference
 * `cut`/`cute` implementation) that builds up a growing lambda formals
 * list by accumulating a literal `x` at each recursive expansion step
 * ends up with the SAME literal symbol `x` used as multiple formals
 * (`(lambda (x x) ...)`), since template-introduced symbols were
 * previously always emitted completely unchanged (see this file's
 * former "emit as-is (unhygienic)" comment) -- each expansion step
 * needs its OWN fresh name for the identifier IT introduces, distinct
 * from a sibling or a later recursively-generated expansion's use of
 * the same name.
 *
 * An earlier version of this fix tried to detect this by syntactically
 * recognizing lambda/let/letrec/do binding-form shapes directly in the
 * template -- but that misses exactly the recursive case above: `x` in
 * `(step (n ... x) . rest)` isn't textually inside a `lambda` at the
 * point it's introduced; it only becomes lambda-bound several
 * *recursive* expansions later, once the macro's own base-case rule
 * finally fires.
 *
 * The heuristic that actually covers this: for the ONE template being
 * expanded right now, rename every symbol that (a) isn't a pattern
 * variable for this match, and (b) never appears in *operator* (call-
 * head) position anywhere in this same template. Genuine free
 * references a macro template relies on -- `lambda`, `apply`, `list`,
 * a helper procedure name, even the macro's own name for a recursive
 * self-call -- are always applied somewhere (`(lambda ...)`, `(apply
 * ...)`, `(step ...)`), so they're never renamed and keep resolving
 * normally. A symbol that's only ever passed around as a plain value
 * (never called) is, in practice, essentially always a variable name
 * the macro is threading through as a fresh binding-to-be -- give it
 * one fresh gensym for this expansion, substituted consistently
 * wherever it appears in this expansion's output. A DIFFERENT expansion
 * (a separate use of the macro, including a recursively-generated one)
 * gets its own distinct gensym, so accumulating recursive macros like
 * cut/cute now produce genuinely distinct formals instead of colliding
 * into one -- verified against exactly the SRFI-26 cut/cute reference
 * implementation this was found through.
 *
 * Two refinements (issue #361) sit around that rule:
 *
 *  - sr_scope_walk (below, before sr_expand's callers) runs first. It does
 *    recognise the core binding forms in the template, and renames the
 *    binders they introduce -- together with the references inside each
 *    binder's own scope -- even when the name is also bound globally. The
 *    syntactic detection rejected above is not enough on its own (it misses
 *    the recursive case), but it is exact where it applies, which the
 *    "already bound" test is not: `(let ((tmp 1)) ...)` in a template used
 *    to capture the caller's global `tmp`. The gensyms it makes are not
 *    renamed again by the rule here.
 *  - A macro defined inside a function treats that function's local
 *    variables as references (SyntaxRulesData.local_vars), because on the
 *    compiled path def_env is GLOBAL_ENV and holds no locals; they used to
 *    be taken for introduced binders and renamed to an unbound gensym.
 *
 * This does not attempt full hygiene in the other direction (a
 * macro-introduced free reference capturing a same-named user binding
 * at the use site) -- that's the harder half of the problem and is
 * unchanged. It's also a heuristic, not a proof: a template that uses
 * the same symbol both as an operator somewhere AND as a fresh-variable
 * placeholder elsewhere (unusual in practice) won't get that symbol
 * renamed -- no worse than the old fully-unhygienic behavior for that
 * rare shape, just not improved by this fix either. */

static bool sr_is_pattern_var(val_t sym, val_t bindings, val_t ell_bindings) {
    for (val_t b = bindings; vis_pair(b); b = vcdr(b))
        if (vcar(vcar(b)) == sym) return true;
    for (val_t b = ell_bindings; vis_pair(b); b = vcdr(b))
        if (vcar(vcar(b)) == sym) return true;
    return false;
}

static bool sr_sym_in_list(val_t sym, val_t lst) {
    for (val_t l = lst; vis_pair(l); l = vcdr(l))
        if (vcar(l) == sym) return true;
    return false;
}

/* "Protected" symbols that must never be renamed: every core special-
 * form keyword (reusing symbol_list.h's own X-macro list, the same
 * source of truth symbol.c builds S_QUOTE/S_LAMBDA/etc. from, rather
 * than hand-duplicating that enumeration here) plus, per-call, anything
 * ALREADY bound in GLOBAL_ENV at expansion time -- ordinary procedure
 * names like `list`/`apply`, and a macro's own recursive self-reference
 * (macros are stored as ordinary environment bindings too, so this one
 * check covers both without needing them special-cased separately). A
 * symbol that resolves to neither is, in practice, essentially always a
 * fresh local variable name the template is threading through as a
 * binding-to-be, not a reference to something that already exists.
 *
 * def_env is the macro's OWN defining environment (SyntaxRulesData.
 * def_env, captured once by sr_compile_fn — see sr_current_env's header
 * comment above for how it gets there), not the use site: an earlier
 * version of this checked GLOBAL_ENV unconditionally, which broke any
 * syntax-rules macro defined inside a define-library body (every
 * (curry X)/(srfi X) module in this codebase) — library bodies run in
 * their own isolated environment (env_new_root(), see modules.c), so a
 * macro's own recursive self-reference or a sibling helper macro/
 * procedure it calls was never found in GLOBAL_ENV and got incorrectly
 * renamed, breaking it. Confirmed via the akkadian/SRFI-shim test
 * regressions that heuristic caused. */
static val_t sr_protected_keywords = V_NIL;
static bool  sr_protected_ready = false;

static void sr_init_protected_keywords(void) {
#define SYM(var, str) sr_protected_keywords = scm_cons(var, sr_protected_keywords);
#include "symbol_list.h"
#undef SYM
    sr_protected_ready = true;
}

static bool sr_is_protected(val_t sym, val_t def_env, val_t local_macros, val_t local_vars) {
    if (!sr_protected_ready) sr_init_protected_keywords();
    if (sr_sym_in_list(sym, sr_protected_keywords)) return true;
    /* An Akkadian/cuneiform special-form synonym (šumma for if, epēšum
     * for lambda, ...) is never itself in sr_protected_keywords or
     * bound anywhere -- lang_translate() (lang_registry.h) resolves it
     * to its canonical English special-form symbol at eval/compile
     * dispatch time, not via any environment binding, unlike a
     * procedure alias (rēšum for car), which IS a real GLOBAL_ENV
     * binding builtins.c's startup loop creates and so is already
     * covered by the env_lookup_slot check below. Confirmed via a real
     * regression: akkadian_tests.scm defines a macro whose template
     * uses šumma as (curry's own) if -- without this, šumma got
     * renamed to a gensym and broke, since it's a genuine free
     * reference to a real special form, not an introduced binder. */
    if (sr_sym_in_list(lang_translate(sym), sr_protected_keywords)) return true;
    if (sr_sym_in_list(sym, local_macros)) return true;
    if (sr_sym_in_list(sym, local_vars)) return true;
    if (env_lookup_slot(def_env, sym) != NULL) return true;
    /* A macro's own top-level (scope_depth == 0) self-reference is
     * eagerly registered into GLOBAL_ENV regardless of def_env
     * (compile_define_syntax, compiler.c, and its eval.c tree-walker
     * counterpart both always do this -- the compiled path's target_env-
     * scoped def_env fix doesn't touch where the macro NAME itself is
     * bound, only where sr_is_protected looks for its free references).
     * Without this fallback, a target_env-scoped recursive macro (bound
     * only in GLOBAL_ENV, never its own library's target_env) failed to
     * recognize its OWN name as protected once def_env stopped being
     * GLOBAL_ENV, incorrectly renaming its self-recursive call and
     * breaking it (found via (curry schematic extract)'s %match-case
     * calling itself). Checked after def_env, not instead of it, so a
     * library-scoped helper that happens to share a name with something
     * unrelated in GLOBAL_ENV still resolves to the closer, correct
     * binding first. */
    return def_env != GLOBAL_ENV && env_lookup_slot(GLOBAL_ENV, sym) != NULL;
}

/* Every symbol appearing anywhere in tmpl (any position) that isn't a
 * pattern variable, the ellipsis identifier, or "_" -- candidates for
 * renaming, before sr_is_protected/the macro's own literals are
 * subtracted out in sr_transformer_fn.
 *
 * Does NOT descend into a literal (quote X) subform: a non-pattern-
 * variable symbol inside a template's own quote -- '(list 'outer x)
 * emitting the symbol `outer` as a literal tag, not a variable
 * reference -- is data the macro author wrote to be produced verbatim,
 * never a fresh binding to introduce. (A pattern variable inside quote
 * is unaffected either way: sr_is_pattern_var's check above already
 * excludes it from this collection regardless of quote context, and
 * sr_expand's own pattern-variable substitution -- separate from this
 * renaming pass -- still applies inside quote exactly as it always has.)
 * quasiquote/unquote is NOT specially handled the way plain quote is,
 * unlike the parenthetical above: a template using `(quasiquote (tag
 * ,x))` to emit a literal symbol tag DOES still get that tag symbol
 * renamed (verified) -- e.g. two separate expansions of such a template
 * would produce tags that are no longer eq? to each other, when the
 * macro author's intent was clearly one fixed, shared tag. Known gap,
 * not exercised by anything in this codebase; would need the same
 * "don't descend" treatment quote gets, done separately since
 * quasiquote's own unquote/unquote-splicing regions DO need normal
 * pattern-variable substitution to still apply within them. */
static int sr_cmp_val(const void *a, const void *b);

/* `skip` (sorted, may be NULL) lists symbols to leave out: the gensyms the
 * scoped pre-pass has already made. Without it each of them would be
 * collected, and the quadratic dedupe below made a template with many
 * binders slow. */
static void sr_collect_free_symbols(val_t tmpl, val_t bindings, val_t ell_bindings,
                                     val_t ellipsis, val_t *out_set,
                                     const val_t *skip, long nskip) {
    if (vis_vector(tmpl)) {
        Vector *v = as_vec(tmpl);
        for (uint32_t i = 0; i < v->len; i++)
            sr_collect_free_symbols(v->data[i], bindings, ell_bindings, ellipsis, out_set, skip, nskip);
        return;
    }
    if (vis_symbol(tmpl)) {
        if (tmpl == ellipsis || tmpl == SR_UNDERSCORE) return;
        if (sr_is_pattern_var(tmpl, bindings, ell_bindings)) return;
        if (skip && bsearch(&tmpl, skip, (size_t)nskip, sizeof(val_t), sr_cmp_val)) return;
        if (sr_sym_in_list(tmpl, *out_set)) return;
        *out_set = scm_cons(tmpl, *out_set);
        return;
    }
    if (!vis_pair(tmpl)) return;
    if (vcar(tmpl) == S_QUOTE && vis_pair(vcdr(tmpl)) && vis_nil(vcddr(tmpl)))
        return;
    val_t t = tmpl;
    while (vis_pair(t)) {
        sr_collect_free_symbols(vcar(t), bindings, ell_bindings, ellipsis, out_set, skip, nskip);
        t = vcdr(t);
    }
    /* dotted tail (t is whatever terminates the list -- a symbol for a
     * dotted pattern/template, V_NIL for a proper one) */
    if (!vis_nil(t))
        sr_collect_free_symbols(t, bindings, ell_bindings, ellipsis, out_set, skip, nskip);
}

/* Fresh name for one template-introduced binder, unique across the
 * whole process -- including across threads: macro expansion can run
 * concurrently (multiple actor threads compiling/expanding a shared
 * top-level macro for the first time, or any of them calling `eval` on
 * code that defines/uses one), and _Atomic here is load-bearing, not
 * just tidy -- a plain `static long counter` race was confirmed by
 * review to produce real gensym collisions under concurrent load (two
 * threads reading the same pre-increment value), corrupting unrelated
 * macro-introduced bindings into referring to each other. Matches
 * actors.c's own next_actor_id pattern.
 *
 * "\x01" can't appear in anything the reader can produce, so this can't
 * collide with a name the macro's own template, or an ordinary use-site
 * call, could ever actually spell. It CAN appear in a symbol built via
 * `(string->symbol "...")` with a literal 0x01 byte, and the counter is
 * a predictable, deterministic sequence -- so a
 * script that both controls a template (or rules data via
 * %rebuild-syntax-rules) AND deliberately embeds "\x01<N>" suffixes
 * matching future counter values could, in principle, engineer a
 * self-inflicted rename collision. Not a cross-boundary/privilege
 * concern (the "attacker" already has to be the one authoring the
 * colliding template in the same scope as its own target), and not
 * addressed further here -- documented as a known, narrow gap rather
 * than adding string-scanning defenses against it. */
static _Atomic long sr_gensym_counter = 0;

static val_t sr_gensym(val_t base) {
    long n = atomic_fetch_add(&sr_gensym_counter, 1);
    char buf[160];
    snprintf(buf, sizeof buf, "%s\x01%ld", sym_cstr(base), n);
    return sym_intern_cstr(buf);
}


/* ---- Scoped renaming of template-introduced binders (issue #361) ----
 *
 * The rule in sr_is_protected -- rename a template symbol unless it
 * already names something real -- cannot tell a binder from a reference.
 * A template that binds `tmp` with `let` was left alone whenever the
 * caller happened to have a global `tmp`, so the let captured the
 * caller's variable:
 *
 *   (define-syntax ctr (syntax-rules () ((_ a) (let ((tmp 1)) (+ tmp a)))))
 *   (define tmp 100)
 *   (ctr tmp)        ; 2, not 101
 *
 * This pass runs first, on the template of the rule that matched. Where the
 * template itself contains a core binding form (lambda, let, let*, letrec,
 * letrec*, named let, do, let-values, let*-values, receive, case-lambda,
 * guard, define with formals, and internal defines at the head of a body),
 * every template-introduced binder is given a fresh name for this
 * expansion, and exactly the references inside that binder's scope are
 * renamed with it -- whether or not the name is also bound somewhere else.
 * Occurrences outside the scope keep the name, so
 * (let ((list (list 1 2))) list) still reads the global `list` in the init.
 *
 * Not renamed here: pattern variables (they carry the caller's own
 * identifiers), the macro's literals, special-form keywords and local
 * macro names. Binders introduced through some other macro, or only
 * becoming binders after later recursive expansions (the accumulating
 * formals of SRFI-26's cut), are not seen by this pass; sr_is_protected's
 * rule still handles them as before. A template with no binding form it
 * understands is used unchanged.
 *
 * The gensyms made here are returned in `fresh` so that the caller does not
 * rename them a second time. */

static int sr_cmp_val(const void *a, const void *b) {
    val_t x = *(const val_t *)a, y = *(const val_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct {
    val_t bindings, ell_bindings, ellipsis, literals, local_macros;
    val_t fresh;       /* list of the gensyms made                         */
    int   n_binders;   /* binders renamed: 0 = the template is unchanged   */
    long  budget;      /* nodes the pass may still visit; at 0 it gives up
                          and the template is used unchanged (the old
                          behaviour), so a huge or machine-built template
                          cannot make this pass dominate the expansion  */
    bool  aborted;
    val_t inner_pvars; /* pattern variables and literals of the inner
                          (syntax-rules ...) whose template is being walked:
                          they are never binders of this pass             */
} SrScopeCtx;

static bool sr_is_special_keyword(val_t sym) {
    if (!sr_protected_ready) sr_init_protected_keywords();
    return sr_sym_in_list(sym, sr_protected_keywords) ||
           sr_sym_in_list(lang_translate(sym), sr_protected_keywords);
}

static bool sr_binder_eligible(SrScopeCtx *cx, val_t sym) {
    if (!vis_symbol(sym)) return false;
    if (sym == cx->ellipsis || sym == SR_UNDERSCORE) return false;
    if (sr_is_pattern_var(sym, cx->bindings, cx->ell_bindings)) return false;
    if (sr_is_literal(sym, cx->literals)) return false;
    if (sr_is_special_keyword(sym)) return false;
    if (sr_sym_in_list(sym, cx->local_macros)) return false;
    if (sr_sym_in_list(sym, cx->inner_pvars)) return false;
    return true;
}

/* scope: alist of (template-symbol . gensym), innermost first. */
static val_t sr_scope_lookup(val_t scope, val_t sym) {
    for (val_t l = scope; vis_pair(l); l = vcdr(l))
        if (vcar(vcar(l)) == sym) return vcdr(vcar(l));
    return V_FALSE;
}

static val_t sr_scope_add(SrScopeCtx *cx, val_t sym, val_t scope) {
    if (!sr_binder_eligible(cx, sym)) return scope;
    val_t g = sr_gensym(sym);
    cx->fresh = scm_cons(g, cx->fresh);
    cx->n_binders++;
    return scm_cons(scm_cons(sym, g), scope);
}

/* A formals list: a symbol, or a proper or improper list of them. */
static val_t sr_scope_add_formals(SrScopeCtx *cx, val_t formals, val_t scope) {
    val_t f = formals;
    for (; vis_pair(f); f = vcdr(f)) scope = sr_scope_add(cx, vcar(f), scope);
    if (vis_symbol(f)) scope = sr_scope_add(cx, f, scope);
    return scope;
}

/* Plain data (formals, binder names): rename the symbols that are in scope,
 * with no special-form handling. */
static val_t sr_scope_map_datum(val_t d, val_t scope, int depth) {
    if (depth > 2000) return d;
    if (vis_symbol(d)) {
        val_t g = sr_scope_lookup(scope, d);
        return g == V_FALSE ? d : g;
    }
    if (vis_vector(d))
        return sr_list_to_vector(sr_scope_map_datum(sr_vector_to_list(d), scope, depth + 1));
    if (!vis_pair(d)) return d;
    /* Iterate along the spine; recurse only into the cars. */
    val_t items = V_NIL;
    val_t t = d;
    for (; vis_pair(t); t = vcdr(t))
        items = scm_cons(sr_scope_map_datum(vcar(t), scope, depth + 1), items);
    val_t out = sr_scope_map_datum(t, scope, depth + 1);   /* improper tail or () */
    for (; vis_pair(items); items = vcdr(items)) out = scm_cons(vcar(items), out);
    return out;
}

static val_t sr_scope_walk(SrScopeCtx *cx, val_t t, val_t scope, int depth);
static val_t sr_scope_walk_list(SrScopeCtx *cx, val_t lst, val_t scope, int depth);

/* Quasiquoted data: symbols in it are literal data and are left alone, as
 * with quote; only the unquoted expressions are code. `level` is the
 * quasiquote nesting depth (1 = directly inside the outermost one). */
static val_t sr_scope_walk_qq(SrScopeCtx *cx, val_t t, val_t scope, int level, int depth) {
    if (depth > 2000) return t;
    if (vis_vector(t))
        return sr_list_to_vector(sr_scope_walk_qq(cx, sr_vector_to_list(t), scope, level, depth + 1));
    if (!vis_pair(t)) return t;
    if (vis_symbol(vcar(t)) && vis_pair(vcdr(t)) && vis_nil(vcdr(vcdr(t)))) {
        val_t k = lang_translate(vcar(t));
        if (k == sym_intern_cstr("quasiquote"))
            return scm_cons(vcar(t), scm_cons(
                sr_scope_walk_qq(cx, vcar(vcdr(t)), scope, level + 1, depth + 1), V_NIL));
        if (k == sym_intern_cstr("unquote") || k == sym_intern_cstr("unquote-splicing")) {
            val_t e = (level == 1)
                ? sr_scope_walk(cx, vcar(vcdr(t)), scope, depth + 1)
                : sr_scope_walk_qq(cx, vcar(vcdr(t)), scope, level - 1, depth + 1);
            return scm_cons(vcar(t), scm_cons(e, V_NIL));
        }
    }
    val_t items = V_NIL;
    val_t u = t;
    for (; vis_pair(u); u = vcdr(u))
        items = scm_cons(sr_scope_walk_qq(cx, vcar(u), scope, level, depth + 1), items);
    val_t out = sr_scope_walk_qq(cx, u, scope, level, depth + 1);
    for (; vis_pair(items); items = vcdr(items)) out = scm_cons(vcar(items), out);
    return out;
}

/* Every symbol in a pattern, as a list: the inner macro's pattern
 * variables and literals. */
static val_t sr_scope_pattern_syms(val_t pat, val_t acc, int depth) {
    if (depth > 2000) return acc;
    if (vis_symbol(pat)) return scm_cons(pat, acc);
    if (vis_vector(pat)) return sr_scope_pattern_syms(sr_vector_to_list(pat), acc, depth + 1);
    for (; vis_pair(pat); pat = vcdr(pat))
        acc = sr_scope_pattern_syms(vcar(pat), acc, depth + 1);
    if (vis_symbol(pat)) acc = scm_cons(pat, acc);
    return acc;
}

/* (syntax-rules [ellipsis] (literal...) (pattern template)...) inside a
 * template -- a macro-defining macro. The inner rules have their own
 * pattern variables, which are not this template's binders and which shadow
 * any outer binder of the same name; the patterns are left alone, the
 * templates are walked so that a reference to an OUTER binder is still
 * renamed with it. */
static val_t sr_scope_walk_syntax_rules(SrScopeCtx *cx, val_t t, val_t scope, int depth) {
    val_t head = vcar(t);
    val_t rest = vcdr(t);
    val_t first_parts = V_NIL;                 /* [ellipsis] (literal ...) */
    val_t literals = V_NIL;
    if (vis_pair(rest) && vis_symbol(vcar(rest)) && vis_pair(vcdr(rest))) {
        first_parts = scm_cons(vcar(rest), V_NIL);   /* custom ellipsis */
        rest = vcdr(rest);
    }
    if (!vis_pair(rest)) return t;
    literals = vcar(rest);
    val_t saved = cx->inner_pvars;
    val_t items = V_NIL;
    val_t r = vcdr(rest);
    for (; vis_pair(r); r = vcdr(r)) {
        val_t rule = vcar(r);
        if (!vis_pair(rule) || !vis_pair(vcdr(rule))) { items = scm_cons(rule, items); continue; }
        val_t pat = vcar(rule);
        val_t syms = sr_scope_pattern_syms(pat, sr_scope_pattern_syms(literals, saved, depth), depth);
        /* A name that is an inner pattern variable is not an outer binder
         * inside this rule: shadow it with itself. */
        val_t inner_scope = scope;
        for (val_t l = syms; l != saved && vis_pair(l); l = vcdr(l))
            inner_scope = scm_cons(scm_cons(vcar(l), vcar(l)), inner_scope);
        cx->inner_pvars = syms;
        val_t tmpl = sr_scope_walk_list(cx, vcdr(rule), inner_scope, depth + 1);
        cx->inner_pvars = saved;
        items = scm_cons(scm_cons(pat, tmpl), items);
    }
    val_t out = r;                              /* improper tail or () */
    for (; vis_pair(items); items = vcdr(items)) out = scm_cons(vcar(items), out);
    return scm_cons(head, scm_append(first_parts, scm_cons(literals, out)));
}

/* A list of forms, each walked in `scope`. Improper tails are mapped as
 * data. */
static val_t sr_scope_walk_list(SrScopeCtx *cx, val_t lst, val_t scope, int depth) {
    val_t items = V_NIL;
    val_t t = lst;
    for (; vis_pair(t); t = vcdr(t))
        items = scm_cons(sr_scope_walk(cx, vcar(t), scope, depth), items);
    val_t out = sr_scope_map_datum(t, scope, depth);
    for (; vis_pair(items); items = vcdr(items)) out = scm_cons(vcar(items), out);
    return out;
}

/* The forms of a body. Leading `(define name ...)` / `(define (name ...)
 * ...)` forms with a template-introduced name are internal definitions: the
 * name is bound over the whole body. */
static val_t sr_scope_walk_body(SrScopeCtx *cx, val_t body, val_t scope, int depth) {
    for (val_t f = body; vis_pair(f); f = vcdr(f)) {
        val_t form = vcar(f);
        if (!vis_pair(form) || !vis_symbol(vcar(form)) ||
            lang_translate(vcar(form)) != S_DEFINE || !vis_pair(vcdr(form)))
            continue;
        val_t target = vcar(vcdr(form));
        scope = sr_scope_add(cx, vis_pair(target) ? vcar(target) : target, scope);
    }
    return sr_scope_walk_list(cx, body, scope, depth);
}

/* An element of a binding list: (name init...). Anything else (an ellipsis
 * token, a pattern variable standing for several bindings) is kept as it
 * is. */
static bool sr_is_binding_pair(val_t e) { return vis_pair(e); }

static val_t sr_scope_walk(SrScopeCtx *cx, val_t t, val_t scope, int depth) {
    if (depth > 2000) return t;               /* unchanged: old behaviour */
    if (cx->budget <= 0) { cx->aborted = true; return t; }
    cx->budget--;
    if (vis_symbol(t)) {
        val_t g = sr_scope_lookup(scope, t);
        return g == V_FALSE ? t : g;
    }
    if (vis_vector(t))
        return sr_list_to_vector(sr_scope_walk_list(cx, sr_vector_to_list(t), scope, depth + 1));
    if (!vis_pair(t)) return t;

    val_t headsym = vcar(t);
    val_t rest    = vcdr(t);
    int   d       = depth + 1;

    if (vis_symbol(headsym) && sr_scope_lookup(scope, headsym) == V_FALSE) {
        val_t k = lang_translate(headsym);

        if (k == S_QUOTE) return t;

        if (k == S_QUASIQUOTE && vis_pair(rest) && vis_nil(vcdr(rest)))
            return scm_cons(headsym, scm_cons(
                sr_scope_walk_qq(cx, vcar(rest), scope, 1, d), V_NIL));

        /* A macro-defining macro: see sr_scope_walk_syntax_rules. */
        if (k == sym_intern_cstr("syntax-rules") && vis_pair(rest))
            return sr_scope_walk_syntax_rules(cx, t, scope, d);

        /* (lambda formals body...) */
        if (k == S_LAMBDA && vis_pair(rest)) {
            val_t inner = sr_scope_add_formals(cx, vcar(rest), scope);
            return scm_cons(headsym,
                     scm_cons(sr_scope_map_datum(vcar(rest), inner, d),
                              sr_scope_walk_body(cx, vcdr(rest), inner, d)));
        }

        /* (case-lambda (formals body...) ...) */
        if (k == sym_intern_cstr("case-lambda")) {
            val_t items = V_NIL;
            val_t c = rest;
            for (; vis_pair(c); c = vcdr(c)) {
                val_t clause = vcar(c);
                if (!vis_pair(clause)) { items = scm_cons(clause, items); continue; }
                val_t inner = sr_scope_add_formals(cx, vcar(clause), scope);
                items = scm_cons(scm_cons(sr_scope_map_datum(vcar(clause), inner, d),
                                          sr_scope_walk_body(cx, vcdr(clause), inner, d)),
                                 items);
            }
            val_t out = sr_scope_map_datum(c, scope, d);
            for (; vis_pair(items); items = vcdr(items)) out = scm_cons(vcar(items), out);
            return scm_cons(headsym, out);
        }

        /* (let [name] ((v init)...) body...), (let* ...), (letrec ...),
         * (letrec* ...) */
        if ((k == S_LET || k == S_LET_STAR || k == S_LETREC || k == S_LETREC_STAR) &&
            vis_pair(rest)) {
            val_t name = V_NIL, bindings = vcar(rest), body = vcdr(rest);
            bool named = false;
            if (k == S_LET && vis_symbol(bindings) && vis_pair(body)) {
                named = true; name = bindings;
                bindings = vcar(body); body = vcdr(body);
            }
            bool sequential = (k == S_LET_STAR);
            bool recursive  = (k == S_LETREC || k == S_LETREC_STAR);

            val_t body_scope = scope;                 /* names for the body */
            if (named) body_scope = sr_scope_add(cx, name, body_scope);
            if (recursive)
                for (val_t b = bindings; vis_pair(b); b = vcdr(b))
                    if (sr_is_binding_pair(vcar(b)))
                        body_scope = sr_scope_add(cx, vcar(vcar(b)), body_scope);

            val_t init_scope = recursive ? body_scope : scope;
            val_t cur = body_scope;      /* let*: grows as bindings are seen */
            if (sequential) cur = scope;
            val_t items = V_NIL;
            val_t b = bindings;
            for (; vis_pair(b); b = vcdr(b)) {
                val_t e = vcar(b);
                if (!sr_is_binding_pair(e)) { items = scm_cons(e, items); continue; }
                val_t scope_for_init = sequential ? cur : init_scope;
                val_t init = sr_scope_walk_list(cx, vcdr(e), scope_for_init, d);
                val_t nm   = vcar(e);
                if (sequential) cur = sr_scope_add(cx, nm, cur);
                else if (!recursive) body_scope = sr_scope_add(cx, nm, body_scope);
                val_t map_scope = sequential ? cur : body_scope;
                items = scm_cons(scm_cons(sr_scope_map_datum(nm, map_scope, d), init), items);
            }
            val_t blist = sr_scope_map_datum(b, scope, d);
            for (; vis_pair(items); items = vcdr(items)) blist = scm_cons(vcar(items), blist);
            val_t fin = sequential ? cur : body_scope;
            val_t new_body = sr_scope_walk_body(cx, body, fin, d);
            if (named)
                return scm_cons(headsym, scm_cons(sr_scope_map_datum(name, fin, d),
                                  scm_cons(blist, new_body)));
            return scm_cons(headsym, scm_cons(blist, new_body));
        }

        /* (do ((var init [step])...) (test expr...) body...) */
        if (k == S_DO && vis_pair(rest) && vis_pair(vcdr(rest))) {
            val_t specs = vcar(rest);
            val_t inner = scope;
            for (val_t b = specs; vis_pair(b); b = vcdr(b))
                if (sr_is_binding_pair(vcar(b)))
                    inner = sr_scope_add(cx, vcar(vcar(b)), inner);
            val_t items = V_NIL;
            val_t b = specs;
            for (; vis_pair(b); b = vcdr(b)) {
                val_t e = vcar(b);
                if (!sr_is_binding_pair(e)) { items = scm_cons(e, items); continue; }
                val_t parts = vcdr(e);       /* (init [step]) */
                val_t np = V_NIL;
                if (vis_pair(parts)) {
                    val_t init = sr_scope_walk(cx, vcar(parts), scope, d);
                    val_t steps = sr_scope_walk_list(cx, vcdr(parts), inner, d);
                    np = scm_cons(init, steps);
                } else np = sr_scope_walk_list(cx, parts, scope, d);
                items = scm_cons(scm_cons(sr_scope_map_datum(vcar(e), inner, d), np), items);
            }
            val_t blist = sr_scope_map_datum(b, scope, d);
            for (; vis_pair(items); items = vcdr(items)) blist = scm_cons(vcar(items), blist);
            return scm_cons(headsym, scm_cons(blist,
                              sr_scope_walk_list(cx, vcdr(rest), inner, d)));
        }

        /* (let-values (((formals...) init)...) body...) and let*-values */
        if ((k == S_LET_VALUES || k == S_LET_STAR_VALUES) && vis_pair(rest)) {
            bool sequential = (k == S_LET_STAR_VALUES);
            val_t all = scope, cur = scope;
            if (!sequential)
                for (val_t b = vcar(rest); vis_pair(b); b = vcdr(b))
                    if (sr_is_binding_pair(vcar(b)))
                        all = sr_scope_add_formals(cx, vcar(vcar(b)), all);
            val_t items = V_NIL;
            val_t b = vcar(rest);
            for (; vis_pair(b); b = vcdr(b)) {
                val_t e = vcar(b);
                if (!sr_is_binding_pair(e)) { items = scm_cons(e, items); continue; }
                val_t init = sr_scope_walk_list(cx, vcdr(e), sequential ? cur : scope, d);
                if (sequential) cur = sr_scope_add_formals(cx, vcar(e), cur);
                items = scm_cons(scm_cons(sr_scope_map_datum(vcar(e), sequential ? cur : all, d), init),
                                 items);
            }
            val_t blist = sr_scope_map_datum(b, scope, d);
            for (; vis_pair(items); items = vcdr(items)) blist = scm_cons(vcar(items), blist);
            return scm_cons(headsym, scm_cons(blist,
                              sr_scope_walk_body(cx, vcdr(rest), sequential ? cur : all, d)));
        }

        /* (receive formals expr body...) */
        if (k == S_RECEIVE && vis_pair(rest) && vis_pair(vcdr(rest))) {
            val_t inner = sr_scope_add_formals(cx, vcar(rest), scope);
            return scm_cons(headsym,
                     scm_cons(sr_scope_map_datum(vcar(rest), inner, d),
                       scm_cons(sr_scope_walk(cx, vcar(vcdr(rest)), scope, d),
                                sr_scope_walk_body(cx, vcdr(vcdr(rest)), inner, d))));
        }

        /* (guard (var clause...) body...): var is bound in the clauses. */
        if (k == S_GUARD && vis_pair(rest) && vis_pair(vcar(rest))) {
            val_t spec  = vcar(rest);
            val_t inner = sr_scope_add(cx, vcar(spec), scope);
            return scm_cons(headsym,
                     scm_cons(scm_cons(sr_scope_map_datum(vcar(spec), inner, d),
                                       sr_scope_walk_list(cx, vcdr(spec), inner, d)),
                              sr_scope_walk_list(cx, vcdr(rest), scope, d)));
        }

        /* (define (name . formals) body...): formals are bound in the body.
         * The name itself is left to sr_is_protected's rule, except when an
         * enclosing body already made it an internal definition. */
        if (k == S_DEFINE && vis_pair(rest) && vis_pair(vcar(rest)) &&
            vis_symbol(vcar(vcar(rest)))) {
            val_t target = vcar(rest);
            val_t inner = sr_scope_add_formals(cx, vcdr(target), scope);
            return scm_cons(headsym,
                     scm_cons(scm_cons(sr_scope_map_datum(vcar(target), scope, d),
                                       sr_scope_map_datum(vcdr(target), inner, d)),
                              sr_scope_walk_body(cx, vcdr(rest), inner, d)));
        }
    }

    /* Anything else -- calls, other special forms, uses of other macros:
     * every element, the head included, is walked. */
    return sr_scope_walk_list(cx, t, scope, d);
}

/* Forward declaration */
static val_t sr_expand(val_t tmpl, val_t bindings, val_t ell_bindings,
                       val_t rename_map, val_t ellipsis, bool literal_mode);

/* Expand a template list (car/cdr chain, not yet unwrapped from any vector)
 * element by element, honoring ellipsis repetition unless literal_mode. */
static val_t sr_expand_list(val_t tmpl, val_t bindings, val_t ell_bindings,
                            val_t rename_map, val_t ellipsis, bool literal_mode) {
    val_t result = V_NIL;
    val_t t = tmpl;
    while (vis_pair(t)) {
        val_t elem = vcar(t);
        val_t rest = vcdr(t);

        /* (ellipsis template) escape: expand template with repetition
         * disabled throughout its subtree, so a macro can emit a literal
         * "..." (or generate another ellipsis-using macro) instead of
         * triggering repetition. Per R7RS 4.3.2. Only recognized when not
         * already inside an escape — nested (ellipsis (ellipsis t)) does
         * not re-enable repetition; that double-escape corner case is not
         * supported. */
        if (!literal_mode && vis_pair(elem) && vcar(elem) == ellipsis &&
            vis_pair(vcdr(elem)) && vis_nil(vcddr(elem))) {
            result = scm_append(result,
                         scm_cons(sr_expand(vcadr(elem), bindings, ell_bindings,
                                             rename_map, ellipsis, true),
                                  V_NIL));
            t = rest;
            continue;
        }

        /* Ellipsis following this element? */
        if (!literal_mode && vis_pair(rest) && vcar(rest) == ellipsis) {
            /* Find ellipsis-bound vars referenced by elem */
            val_t refs = sr_ell_refs(elem, ell_bindings);
            if (vis_pair(refs)) {
                /* scm_list_length returns -1 for a non-proper-list (see
                 * its own definition) -- can happen here for a malformed
                 * macro use where refs' first entry turns out to be a
                 * scalar rather than the per-iteration list this code
                 * expects (e.g. a template mixing a depth-1 and a
                 * depth-2 variable in the same ellipsis-repeated
                 * sub-template, which R7RS forbids but nothing in this
                 * file actually rejects at match time). Clamping to 0
                 * repetitions here, and guarding the per-ref scm_list_ref
                 * call below the same way, turns what would otherwise be
                 * an unchecked vcar/vcdr on a non-pair (a real, reviewer-
                 * confirmed SIGSEGV, uncatchable since macro expansion
                 * runs at compile time -- see issue #101's fix writeup)
                 * into the same kind of "wrong but survivable" output a
                 * malformed macro already produced before this nested-
                 * ellipsis fix landed, rather than a worse failure mode
                 * than before. */
                int n = scm_list_length(vcdr(vcar(refs)));
                if (n < 0) n = 0;
                for (int i = 0; i < n; i++) {
                    /* Build per-iteration bindings: bind each ell var to
                     * its i-th value. Pushed into BOTH tables:
                     * iter_bindings (plain scalar substitution, for the
                     * common case where elem references the var bare,
                     * with no further ellipsis) AND iter_ell_bindings
                     * (shadowing the OUTER ell_bindings entry for that
                     * name, for the nested-ellipsis case where elem
                     * itself contains "name ...", which needs to find
                     * just THIS iteration's own slice, not the full
                     * across-all-outer-iterations list).
                     *
                     * Without iter_ell_bindings (issue #101's expand-side
                     * counterpart), a template like "((a b ...) ...)"
                     * would recurse into elem = "(a b ...)" still holding
                     * the ORIGINAL, un-scoped ell_bindings, so the inner
                     * "b ..." would iterate over b's full nested
                     * structure across every outer group instead of just
                     * group i's own slice. A pattern variable with
                     * further ellipsis in the template is never
                     * referenced bare (R7RS requires matching ellipsis
                     * depth at every reference), so iter_bindings' copy
                     * of it is simply never consulted in that case --
                     * harmless to set unconditionally on both tables
                     * rather than trying to know ahead of time which one
                     * a given name will actually need. */
                    val_t iter_bindings     = bindings;
                    val_t iter_ell_bindings = ell_bindings;
                    for (val_t r = refs; vis_pair(r); r = vcdr(r)) {
                        val_t name = vcar(vcar(r));
                        /* Guard this scm_list_ref too, not just the one
                         * used to compute `n` above -- a malformed macro
                         * mixing a depth-1 and a depth-2 variable in the
                         * SAME ellipsis-repeated sub-template can pick a
                         * well-formed list for `n` (from whichever ref
                         * sr_ell_refs happened to return first) while a
                         * DIFFERENT ref in this same loop holds a scalar
                         * -- confirmed by independent review: this exact
                         * shape still crashed even after the `n` guard
                         * above, since this second, per-ref call is the
                         * one that actually dereferences the scalar as a
                         * pair. Same fallback: a not-a-proper-list value
                         * degrades to V_NIL for this iteration instead of
                         * crashing. */
                        val_t vals = vcdr(vcar(r));
                        val_t val  = (scm_list_length(vals) > i) ? scm_list_ref(vals, i) : V_NIL;
                        iter_bindings     = scm_cons(scm_cons(name, val), iter_bindings);
                        iter_ell_bindings = scm_cons(scm_cons(name, val), iter_ell_bindings);
                    }
                    result = scm_append(result,
                                 scm_cons(sr_expand(elem, iter_bindings, iter_ell_bindings,
                                                     rename_map, ellipsis, literal_mode),
                                          V_NIL));
                }
            }
            /* refs is empty: zero repetitions — splice nothing */
            t = vcdr(rest); /* skip elem and ... */
            continue;
        }

        result = scm_append(result,
                     scm_cons(sr_expand(elem, bindings, ell_bindings, rename_map, ellipsis, literal_mode),
                              V_NIL));
        t = rest;
    }

    /* Dotted template tail */
    if (!vis_nil(t))
        result = scm_append(result, sr_expand(t, bindings, ell_bindings, rename_map, ellipsis, literal_mode));

    return result;
}

/* Expand a template given non-ellipsis bindings and ellipsis bindings.
 * literal_mode, once entered via an (ellipsis template) escape, suppresses
 * ellipsis-triggered repetition for the whole subtree — "..." tokens and
 * elem-followed-by-"..." pairs are then just ordinary data — while pattern
 * variable substitution still happens normally throughout.
 *
 * rename_map is the per-expansion (name . fresh-gensym) table built by
 * sr_transformer_fn from sr_collect_operators/sr_collect_free_symbols —
 * see the "Partial hygiene" header comment above them for what it does
 * and doesn't fix. */
static val_t sr_expand(val_t tmpl, val_t bindings, val_t ell_bindings,
                       val_t rename_map, val_t ellipsis, bool literal_mode) {
    if (vis_symbol(tmpl)) {
        /* Look up in simple bindings first */
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            if (vcar(vcar(b)) == tmpl) return vcdr(vcar(b));
        /* Template-introduced binder this expansion has a fresh name for */
        for (val_t r = rename_map; vis_pair(r); r = vcdr(r))
            if (vcar(vcar(r)) == tmpl) return vcdr(vcar(r));
        /* Free identifier (e.g. lambda, apply, a helper procedure name) —
         * emit as-is, resolved at the use site (still unhygienic in this
         * direction, see the header comment above sr_is_pattern_var). */
        return tmpl;
    }

    if (vis_vector(tmpl))
        return sr_list_to_vector(
            sr_expand_list(sr_vector_to_list(tmpl), bindings, ell_bindings,
                           rename_map, ellipsis, literal_mode));

    if (!vis_pair(tmpl)) return tmpl; /* self-evaluating datum */

    return sr_expand_list(tmpl, bindings, ell_bindings, rename_map, ellipsis, literal_mode);
}

/* ---- Transformer function (called when the macro is used) ---- */

static val_t sr_transformer_fn(int ac, val_t *av, void *ud) {
    (void)ac;
    val_t form = av[0]; /* entire unevaluated use-site form */
    SyntaxRulesData *sr = ud;

    /* Normal macro expansion always passes a pair (the whole use-site
     * form). But this Primitive is an ordinary first-class value once
     * obtained via (syntax-rules ...) or %rebuild-syntax-rules — nothing
     * stops user code from calling it directly as a procedure with an
     * arbitrary (non-pair) argument, e.g. a fixnum. vcdr(form) below would
     * blindly reinterpret that value's bits as a Pair* and crash; guard
     * against it with a clean error instead (latent bug, pre-existing
     * before %rebuild-syntax-rules — confirmed reachable via plain
     * (syntax-rules ...) too — made newly easy to hit by exposing the raw
     * transformer more directly, so fixed here). */
    if (!vis_pair(form))
        scm_raise(V_FALSE, "syntax-rules transformer: expected a use-site form, got a non-pair value");

    for (val_t rules = sr->rules; vis_pair(rules); rules = vcdr(rules)) {
        val_t rule = vcar(rules);
        val_t pat  = vcar(rule);
        val_t tmpl = vcdr(rule);

        /* Skip the keyword position in the pattern. A top-level vector
         * pattern (`#(keyword ...)`, valid per issue #135's own new
         * vis_pair(pat)||vis_vector(pat) check) must be converted to a
         * list first -- unlike every NESTED vector sub-pattern (handled
         * by sr_match_one at line 122/173), this top-level keyword-skip
         * previously called vcdr(pat) directly on a raw Vector object.
         * Pair.cdr and Vector.data[0] sit at different struct offsets,
         * so this was a real type-confusion / out-of-bounds read (found
         * by a third review round), not just a missing-check crash:
         * `vcdr` silently reinterpreted the vector's own length/first-
         * element word as a val_t and handed it to sr_match_list. */
        val_t pat_list  = vis_vector(pat) ? sr_vector_to_list(pat) : pat;
        /* `#()` -- a zero-length vector pattern, with no keyword
         * position at all -- converts to `()`, and vcdr(()) below
         * would crash the same way vcdr on a raw empty Vector did.
         * Treated as "this rule can never match" rather than raised,
         * matching how a genuinely unmatchable rule is handled further
         * down (sr_match_list returning false), since a macro can
         * have other rules that DO match the same use. */
        if (!vis_pair(pat_list)) continue;
        val_t pat_rest  = vcdr(pat_list);
        val_t form_rest = vcdr(form);

        val_t bindings = V_NIL, ell_bindings = V_NIL;
        if (sr_match_list(pat_rest, form_rest, sr->literals, sr->ellipsis,
                           &bindings, &ell_bindings)) {
            /* One fresh gensym per distinct template-introduced symbol
             * that isn't already a meaningful reference (a special-form
             * keyword, an existing global procedure/macro, or one of
             * this macro's own declared literals), for this one
             * expansion only -- see the "Partial hygiene" header
             * comment above sr_is_protected. */
            /* Binders the template itself introduces are renamed within
             * their own scope first (issue #361); a template with none is
             * used as it is. */
            SrScopeCtx cx = { bindings, ell_bindings, sr->ellipsis, sr->literals,
                              sr->local_macros, V_NIL, 0, 200000, false, V_NIL };
            val_t scoped = sr_scope_walk(&cx, tmpl, V_NIL, 0);
            if (cx.aborted) { cx.fresh = V_NIL; cx.n_binders = 0; }
            if (cx.n_binders > 0) tmpl = scoped;
            /* The gensyms just made, as a sorted array: one binary search per
             * free symbol below instead of a list scan (a template with many
             * binders has as many gensyms as it has free symbols). */
            long nfresh = 0;
            for (val_t f = cx.fresh; vis_pair(f); f = vcdr(f)) nfresh++;
            val_t *fresh_sorted = NULL;
            if (nfresh > 0) {
                fresh_sorted = (val_t *)gc_alloc_atomic((size_t)nfresh * sizeof(val_t));
                long k = 0;
                for (val_t f = cx.fresh; vis_pair(f); f = vcdr(f)) fresh_sorted[k++] = vcar(f);
                qsort(fresh_sorted, (size_t)nfresh, sizeof(val_t), sr_cmp_val);
            }
            val_t free_syms = V_NIL;
            sr_collect_free_symbols(tmpl, bindings, ell_bindings, sr->ellipsis, &free_syms,
                                    fresh_sorted, nfresh);      /* already fresh: skipped */
            val_t rename_map = V_NIL;
            for (val_t s = free_syms; vis_pair(s); s = vcdr(s)) {
                val_t sym = vcar(s);
                if (sr_is_literal(sym, sr->literals)) continue;
                if (sr_is_protected(sym, sr->def_env, sr->local_macros, sr->local_vars)) continue;
                rename_map = scm_cons(scm_cons(sym, sr_gensym(sym)), rename_map);
            }
            return sr_expand(tmpl, bindings, ell_bindings, rename_map, sr->ellipsis, false);
        }
    }

    val_t kw = vis_pair(form) ? vcar(form) : V_FALSE;
    if (vis_symbol(kw))
        scm_raise(V_FALSE, "syntax-rules: no matching pattern for %s", sym_cstr(kw));
    scm_raise(V_FALSE, "syntax-rules: no matching pattern");
}

/* ---- Compile function (registered as the T_SYNTAX transformer) ---- */

/* Called by eval when it sees (syntax-rules ...) in a T_SYNTAX position.
 * av[0] = the whole unevaluated
 *   (syntax-rules literals rule...)
 * or, with a custom ellipsis identifier (R7RS 4.3.2):
 *   (syntax-rules ellipsis literals rule...)
 * form. Returns a T_PRIMITIVE that acts as the macro transformer. */
static val_t sr_compile_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t form = av[0];
    /* A malformed `(syntax-rules)` (no ellipsis/literals/rules at all)
     * used to SIGSEGV here on vcadr(form) -- same widespread bug class as
     * compiler.c's require_min_args, confirmed present on main too. */
    if (!vis_pair(vcdr(form)))
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                        "syntax-rules: ill-formed special form");
    val_t second = vcadr(form);

    val_t ellipsis, literals, rules_kv;
    if (vis_symbol(second)) {
        /* (syntax-rules ellipsis (literal ...) rule ...) */
        /* Issue #135: `(syntax-rules x)` -- an ellipsis identifier with
         * no literals list (or anything) after it -- previously fell
         * straight into vcaddr(form) below and SIGSEGVed. */
        if (!vis_pair(vcddr(form)))
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                            "syntax-rules: ill-formed special form");
        ellipsis = second;
        literals = vcaddr(form);
        rules_kv = vcdr(vcddr(form));
    } else {
        ellipsis = SR_DEFAULT_ELLIPSIS;
        literals = second;
        rules_kv = vcddr(form);
    }

    /* Compile rules into (pattern . template) pairs */
    val_t compiled = V_NIL;
    for (val_t r = rules_kv; vis_pair(r); r = vcdr(r)) {
        val_t rule = vcar(r);
        /* `(syntax-rules () ())` -- an empty rule, not (pattern
         * template) -- previously fell straight into vcar(rule)/
         * vcadr(rule) below and SIGSEGVed. */
        if (!vis_pair(rule) || !vis_pair(vcdr(rule)))
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                            "syntax-rules: ill-formed rule");
        val_t pat  = vcar(rule);
        /* Issue #135 (second round, found by independent review): a
         * pattern that isn't itself a pair or vector -- `(syntax-rules
         * () (x 1))`, `(syntax-rules () (5 1))` -- previously passed
         * this rule-shape check (rule is a fine 2-element list) but
         * later crashed sr_transformer_fn's own vcdr(pat) the first
         * time the macro was actually USED, not when it was defined --
         * so a malformed macro could be shipped/loaded successfully
         * and only detonate for whoever calls it. Every R7RS pattern
         * has a keyword/sub-pattern head position, so the top-level
         * pattern is always list- or vector-shaped, never a bare atom. */
        if (!vis_pair(pat) && !vis_vector(pat))
            scm_raise_code(EC_WRONG_TYPE_ARGUMENT,
                            "syntax-rules: pattern must be a list or vector");
        val_t tmpl = vcadr(rule);
        compiled = scm_cons(scm_cons(pat, tmpl), compiled);
    }
    compiled = scm_reverse(compiled);

    SyntaxRulesData *sr = gc_alloc_raw_pinned(sizeof(SyntaxRulesData));
    sr->literals = literals;
    sr->rules    = compiled;
    sr->ellipsis = ellipsis;
    /* V_FALSE (the compiled path, which never sets this thread-local —
     * see its own header comment) maps to GLOBAL_ENV, matching
     * compile_time_eval's existing "GLOBAL_ENV only" constraint for a
     * compiled define-syntax's transformer-expr. */
    val_t cur_env = sr_get_current_env();
    sr->def_env = (cur_env == V_FALSE) ? GLOBAL_ENV : cur_env;
    sr->local_macros = sr_get_current_local_macros();
    sr->local_vars   = sr_get_current_local_vars();

    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name     = "syntax-rules-transformer";
    p->min_args = 1; p->max_args = 1;
    p->fn  = sr_transformer_fn;
    p->ud  = sr;
    return vptr(p);
}

/* ---- Extraction / direct reconstruction (compiler.c's define-syntax) ---- */

bool sr_transformer_data(val_t transformer, val_t *literals, val_t *rules,
                          val_t *ellipsis) {
    if (!vis_prim(transformer)) return false;
    Primitive *p = as_prim(transformer);
    if (p->fn != sr_transformer_fn) return false;
    SyntaxRulesData *sr = p->ud;
    *literals = sr->literals;
    *rules    = sr->rules;
    *ellipsis = sr->ellipsis;
    return true;
}

/* Returns the bare transformer procedure — the same shape sr_compile_fn's
 * (syntax-rules ...) produces, deliberately NOT wrapped in a Syntax struct.
 * The caller (compiler.c's compile_define_syntax, or the tree-walker's own
 * S_DEFINE_SYNTAX, both unconditionally wrap whatever a transformer-expr
 * evaluates to in exactly one Syntax layer) is the only place that should
 * ever do that wrapping — %rebuild-syntax-rules (below) is an ordinary,
 * discoverable global primitive, so if it returned an already-Syntax-wrapped
 * value, a user writing (define-syntax bogus (%rebuild-syntax-rules ...))
 * directly would end up with a Syntax-wrapping-a-Syntax value that nothing
 * else in the codebase expects, corrupting VM state when the resulting
 * "macro" is used (found by review). Matching sr_compile_fn's return shape
 * makes direct misuse behave exactly like using (syntax-rules ...) with
 * malformed literals/rules/ellipsis — validated below, then a normal
 * Scheme error, never a crash. */
val_t sr_rebuild_syntax_env(val_t literals, val_t rules, val_t ellipsis, val_t def_env) {
    if (scm_list_length(literals) < 0)
        scm_raise(V_FALSE, "%%rebuild-syntax-rules: literals must be a proper list");
    if (scm_list_length(rules) < 0)
        scm_raise(V_FALSE, "%%rebuild-syntax-rules: rules must be a proper list");
    for (val_t r = rules; vis_pair(r); r = vcdr(r)) {
        if (!vis_pair(vcar(r)))
            scm_raise(V_FALSE, "%%rebuild-syntax-rules: each rule must be a (pattern . template) pair");
        /* Same pattern-shape gap as sr_compile_fn's identical fix
         * (issue #135, second round): a non-pair/non-vector pattern
         * here crashes sr_transformer_fn's own vcdr(pat) at first use,
         * not at definition time. */
        if (!vis_pair(vcar(vcar(r))) && !vis_vector(vcar(vcar(r))))
            scm_raise(V_FALSE, "%%rebuild-syntax-rules: pattern must be a list or vector");
    }
    if (!vis_symbol(ellipsis))
        scm_raise(V_FALSE, "%%rebuild-syntax-rules: ellipsis must be a symbol");

    SyntaxRulesData *sr = gc_alloc_raw_pinned(sizeof(SyntaxRulesData));
    sr->literals = literals;
    sr->rules    = rules;
    sr->ellipsis = ellipsis;
    /* def_env defaults to GLOBAL_ENV (the historical behavior, for a
     * plain top-level macro or a user calling %rebuild-syntax-rules
     * directly per this function's own doc comment) unless the caller
     * -- compile_define_syntax's runtime re-registration form, for a
     * macro compiled against a library's own target_env (chunk.h) --
     * passes the real defining environment explicitly. Without this, a
     * target_env-scoped macro's runtime-rebuilt transformer always got
     * GLOBAL_ENV as its def_env, which made sr_is_protected miss any
     * library-local helper a template referenced (bound only in
     * target_env), incorrectly renaming it and breaking self-recursive
     * macros that call a sibling from their own library body (found via
     * (curry schematic extract)'s %match-case calling %match). See
     * Chunk::target_env's own doc comment for the parallel, still-open
     * gap this shares: a live env value embedded this way does not
     * survive a .scc cache reload across process runs -- deferred, same
     * as target_env itself. */
    sr->def_env      = (def_env == V_VOID) ? GLOBAL_ENV : def_env;
    sr->local_macros = V_NIL;
    sr->local_vars   = V_NIL;

    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name     = "syntax-rules-transformer";
    p->min_args = 1; p->max_args = 1;
    p->fn  = sr_transformer_fn;
    p->ud  = sr;
    return vptr(p);
}

/* Bytecode-callable wrapper around sr_rebuild_syntax — see compiler.c's
 * compile_define_syntax, which emits a call to this (with literals/rules/
 * ellipsis embedded as quoted constants) as a compiled top-level macro's
 * runtime re-registration, instead of re-evaluating the original
 * transformer-expression source, whenever that expression turns out to
 * have produced an ordinary syntax-rules transformer. Its result is wired
 * back through (define-syntax name (%rebuild-syntax-rules ...)), not a
 * plain define, so it goes through the same single Syntax-wrapping step as
 * every other transformer-expr. */
static val_t prim_rebuild_syntax_rules(int ac, val_t *av, void *ud) {
    (void)ud;
    return sr_rebuild_syntax_env(av[0], av[1], av[2], ac > 3 ? av[3] : V_VOID);
}

/* ---- Entry points shared with syntax_case.c ----
 *
 * syntax-case reuses this file's matcher and template expander rather than
 * growing its own. Only the template-introduced-identifier policy differs:
 * here such a symbol becomes a fresh gensym, there it becomes a syntax
 * object that the macro-apply boundary resolves later, so the caller
 * supplies the constructor. */

val_t sr_default_ellipsis(void) { return SR_DEFAULT_ELLIPSIS; }

bool sr_match_pattern(val_t pat, val_t form, val_t literals, val_t ellipsis,
                      val_t *bindings, val_t *ell_bindings) {
    return sr_match_one(pat, form, literals, ellipsis, bindings, ell_bindings);
}

val_t sr_instantiate(val_t tmpl, val_t bindings, val_t ell_bindings, val_t ellipsis,
                     val_t def_env, val_t local_macros,
                     val_t (*mk_ident)(val_t sym, void *ud), void *ud) {
    val_t free_syms = V_NIL;
    sr_collect_free_symbols(tmpl, bindings, ell_bindings, ellipsis, &free_syms, NULL, 0);
    val_t rename_map = V_NIL;
    for (val_t s = free_syms; vis_pair(s); s = vcdr(s)) {
        val_t sym = vcar(s);
        if (sr_is_protected(sym, def_env, local_macros, V_NIL)) continue;
        rename_map = scm_cons(scm_cons(sym, mk_ident(sym, ud)), rename_map);
    }
    return sr_expand(tmpl, bindings, ell_bindings, rename_map, ellipsis, false);
}

/* ---- Registration ---- */

void syntax_rules_register(val_t env) {
    SR_DEFAULT_ELLIPSIS = sym_intern_cstr("...");
    SR_UNDERSCORE        = sym_intern_cstr("_");

    /* syntax-rules is itself a T_SYNTAX: eval passes the unevaluated form to
     * sr_compile_fn, which returns a T_PRIMITIVE transformer.  That primitive
     * self-evaluates (non-pair heap object), so define-syntax receives it
     * directly and wraps it in its own T_SYNTAX struct. */
    Primitive *compile_prim = CURRY_NEW_PINNED(Primitive);
    compile_prim->hdr.type = T_PRIMITIVE; compile_prim->hdr.flags = 0;
    compile_prim->name     = "syntax-rules-compile";
    compile_prim->min_args = 1; compile_prim->max_args = 1;
    compile_prim->fn  = sr_compile_fn;
    compile_prim->ud  = NULL;

    Syntax *syn = CURRY_NEW(Syntax);
    syn->hdr.type = T_SYNTAX; syn->hdr.flags = 0;
    syn->transformer = vptr(compile_prim);

    env_define(env, sym_intern_cstr("syntax-rules"), vptr(syn));

    Primitive *rebuild_prim = CURRY_NEW_PINNED(Primitive);
    rebuild_prim->hdr.type = T_PRIMITIVE; rebuild_prim->hdr.flags = 0;
    rebuild_prim->name     = "%rebuild-syntax-rules";
    rebuild_prim->min_args = 3; rebuild_prim->max_args = 4;
    rebuild_prim->fn  = prim_rebuild_syntax_rules;
    rebuild_prim->ud  = NULL;
    env_define(env, sym_intern_cstr("%rebuild-syntax-rules"), vptr(rebuild_prim));
}
