/*
 * ir_lower.c — Tier 2.1 IR lowering + Tier 2.2 optimization.
 *
 * Split out of the old single-file compiler.c (pure code motion, no
 * behavior change -- see compiler.c's own header comment for the full
 * five-way split). Holds ir_lower/ir_lower_* (the lowering pass that walks
 * raw S-expressions and produces an IR tree, deferring variable resolution
 * and nested-lambda-body lowering to ir_emit.c) and ir_optimize/
 * ir_optimize_andor (Tier 2.2's dead-branch-elimination and boolean-
 * simplification passes over an already-lowered tree), plus the Tier 2.3
 * local-inliner eligibility helpers (ir_count_ast_nodes/
 * params_proper_arity/expr_contains_symbol/body_contains_symbol and the
 * BoundScope "is this candidate lexically closed" cluster) that only
 * ir_lower_let/ir_lower_let_star actually consult.
 *
 * Shared symbols (the real `Compiler` struct, foundation helpers defined in
 * compiler.c, classify_head/compile() defined in compiler_classic.c, ir_emit
 * defined in ir_emit.c) are declared in compiler_internal.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "compiler.h"
#include "compiler_internal.h"
#include "chunk.h"
#include "ir.h"
#include "opcode.h"
#include "value.h"
#include "object.h"
#include "symbol.h"
#include "gc.h"
#include "numeric.h"
#include "env.h"
#include "eval.h"
#include "vm.h"
#include "builtins.h"
#include "lang_registry.h"
#include "profiling.h"
#include "reader.h"
#include "record_type.h"
#include "syntax_rules.h"
#include "sx_algebra.h"
#include "sx_pattern.h"
#include "curry_features.h"
#include "set.h"

/* Tier 2.3 local inliner: conservative self-recursion check for an
 * inlining candidate -- does `expr` contain `name` as a symbol ANYWHERE
 * (car or cdr position, including inside a quoted datum -- unlike
 * expr_mentions_set_target above, this deliberately does NOT special-case
 * quote or macro use, since any occurrence at all, even inert data, is
 * grounds to reject: over-conservative on purpose. A stray unrelated
 * occurrence just means a safe, correct candidate gets left un-inlined,
 * never the other way around. Does not descend into vector literals --
 * missing a symbol occurrence there is still safe, since a vector isn't
 * something that gets called. */
bool expr_contains_symbol(val_t expr, val_t name) {
    if (expr == name) return true;
    if (!vis_pair(expr)) return false;
    return expr_contains_symbol(vcar(expr), name) ||
           expr_contains_symbol(vcdr(expr), name);
}

bool body_contains_symbol(val_t body, val_t name) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_contains_symbol(vcar(p), name)) return true;
    return false;
}

/* Tier 2.4: multi-name sibling-reference guard for let-bound candidates
 * (let* only ever has one binding to check per registration -- see
 * ir_lower_let_star's own use of body_contains_symbol above -- but plain
 * let can have several siblings at once). Independent code review
 * caught a real, confirmed miscompilation this closes: lambda_is_closed
 * (below) only checks a candidate's free variables against the
 * ENCLOSING Compiler `c` -- correctly, since that's the only real scope
 * that exists yet at ir_lower_let time -- but a let's OWN sibling
 * binding names (including the candidate's own name) are NEITHER
 * locally bound within the candidate's own params NOR yet resolvable in
 * `c` at this point, so lambda_is_closed's existing check silently
 * treats a reference to one of them as "global, fine". Once
 * ir_emit_inline_call later splices the body into the wrapper's own
 * frame, that name IS a real local there if the call site happens to be
 * inside the same let's body -- so the reference silently resolves
 * instead of correctly raising unbound-variable, exactly the way a
 * genuine letrec-style self/mutual-reference would (but plain `let`
 * bindings are NOT visible to each other's inits or to any sibling's
 * own captured-closure environment -- only letrec makes that true).
 * Confirmed via direct repro before this fix: `(let ((f (lambda (x) f)))
 * (procedure? (f 1)))` printed #t (should raise unbound-variable);
 * `(let ((y 10) (f (lambda (x) (+ x y)))) (f 1))` returned 11 (should
 * also raise unbound-variable, since y is a SIBLING binding, not
 * visible to f's own closure under real let semantics). Any reference
 * to ANY of the let's own binding names -- checked as a flat list,
 * ignoring which specific sibling it is, including the candidate's own
 * name -- is grounds to reject: same over-conservative-but-safe
 * tradeoff body_contains_symbol's own single-name self-recursion check
 * already uses (a candidate that merely shares a name with an unrelated
 * OUTER variable also gets conservatively rejected here; that just
 * means a safe candidate falls back to a real, correct, non-inlined
 * call, never a miscompile). */
static bool expr_contains_any_symbol(val_t expr, val_t names) {
    if (vis_symbol(expr)) {
        for (val_t p = names; vis_pair(p); p = vcdr(p))
            if (vcar(p) == expr) return true;
        return false;
    }
    if (!vis_pair(expr)) return false;
    return expr_contains_any_symbol(vcar(expr), names) ||
           expr_contains_any_symbol(vcdr(expr), names);
}

static bool body_contains_any_symbol(val_t body, val_t names) {
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (expr_contains_any_symbol(vcar(p), names)) return true;
    return false;
}

/* Tier 2.3: total raw AST node count (every pair cell plus every atom
 * leaf counts as one) -- the inliner's size budget. Iterative over the
 * cdr-spine, not recursive -- a wide, flat body (many top-level forms,
 * plausible from macro-generated code) costs O(1) C-stack depth this
 * way, not one C frame per element; only car sub-structure still
 * recurses, bounded by genuine Scheme nesting depth rather than the
 * body's overall size. Also stops early, without finishing the walk,
 * once already over `budget` -- a plain unconditional recursive count
 * (the original shape of this function) doesn't actually avoid deep
 * C-stack recursion for a pathologically large candidate just by being
 * CALLED before body_contains_symbol's own (unbounded) walk, the way an
 * earlier version's ordering comment claimed -- caught by independent
 * code review. */
int ir_count_ast_nodes(val_t expr, int budget) {
    int count = 0;
    while (vis_pair(expr)) {
        count += 1 + ir_count_ast_nodes(vcar(expr), budget);
        if (count > budget) return count;
        expr = vcdr(expr);
    }
    return count + 1;   /* the terminal atom -- nil, or a dotted tail */
}

/* Tier 2.3: true (with *argc_out set) iff `params` is a proper list --
 * rest/dotted param lists are rejected as inlining candidates to keep
 * call-site argument substitution simple (exact argc match only).
 * scm_list_length (src/builtins.c) already performs exactly this walk
 * and returns -1 for an improper list. */
bool params_proper_arity(val_t params, int *argc_out) {
    int n = scm_list_length(params);
    if (n < 0) return false;
    *argc_out = n;
    return true;
}

/* Tier 2.4 (let/let*-bound local inlining): lambda_is_closed and its
 * helpers below. A new, side-effect-free, purely syntactic walker,
 * modeled directly on expr_mentions_set_target's own established shape
 * (conservative pre-expansion syntactic scanning, unconditional bail-out
 * on anything it can't cleanly reason about) -- see IRKnownParam's own
 * comment in ir.h for why this has to run at ir_lower time, before
 * either the candidate or its would-be enclosing wrapper exist as real
 * Compilers, unlike IR_DEFINE's "compile it, then read what it proved"
 * registration.
 *
 * A linked list of symbol sets, one per lexical scope nesting level
 * introduced while walking a candidate body -- NOT Compiler::locals[]:
 * no physical stack slots exist for any of this yet, this is pure
 * syntactic bookkeeping over raw val_t forms. */
typedef struct BoundScope {
    struct BoundScope *parent;
    val_t               names;   /* flat list of symbols bound at this level */
} BoundScope;

static bool bound_scope_has(BoundScope *scope, val_t name) {
    for (BoundScope *s = scope; s; s = s->parent)
        for (val_t p = s->names; vis_pair(p); p = vcdr(p))
            if (vcar(p) == name) return true;
    return false;
}

/* Flattens a param spec (proper list, dotted rest-arg, or a bare "all
 * rest" symbol) into a plain list of every name it binds -- order
 * doesn't matter, only membership. */
static val_t collect_param_names(val_t params) {
    if (vis_symbol(params)) return scm_cons(params, V_NIL);
    val_t result = V_NIL;
    val_t p = params;
    while (vis_pair(p)) { result = scm_cons(vcar(p), result); p = vcdr(p); }
    if (vis_symbol(p)) result = scm_cons(p, result);
    return result;
}

/* Same three-tier lookup expr_mentions_set_target already uses (local
 * syntax -> target_env -> GLOBAL_ENV) to decide "is this head a macro
 * use" -- kept as its own helper here since lambda_is_closed needs the
 * same check from more than one call site below. */
static bool head_is_macro(Compiler *c, val_t raw_head) {
    if (!vis_symbol(raw_head)) return false;
    val_t transformer;
    if (resolve_syntax_local(c, raw_head, &transformer)) return true;
    if (c->chunk->target_env != V_VOID &&
        vis_syntax(env_lookup_or_false(c->chunk->target_env, raw_head)))
        return true;
    if (vis_syntax(env_lookup_or_false(GLOBAL_ENV, raw_head))) return true;
    return false;
}

/* Collects every name a body's own front-of-body internal-definition-like
 * forms bind, mirroring lambda_prescan's dispatch table (compiler.c) --
 * reusing only the recognized-form list, never lambda_prescan's own
 * side-effecting add_local/emit calls. Returns false (give up on the
 * WHOLE candidate, treated as not-closed by every caller) the moment it
 * sees define-record-type/define-rule/define-ruleset/define-algebra/
 * define-syntax: these either need real, non-trivial parsing to know
 * what they bind (define-record-type goes through
 * record_type_build_spec) or don't bind a plain variable name at all in
 * a way worth specially modeling here -- reproducing that logic purely
 * to let a FEW more candidates qualify isn't worth the risk of getting
 * it subtly wrong; bailing out is always safe, just conservative. Scans
 * the whole body (not just a strict R7RS-ordered prefix): this function
 * only needs to avoid UNDER-binding a name that's genuinely visible
 * later (which would cause a false "captures!" verdict, safe but overly
 * conservative) -- it doesn't need to replicate R7RS's own
 * internal-definition-ordering validation, since a genuinely malformed
 * body still gets a real compile-time error at real compile time
 * regardless of whether this candidate gets inlined. */
static bool collect_body_def_names(val_t body, val_t *names_out) {
    val_t names = V_NIL;
    for (val_t p = body; vis_pair(p); p = vcdr(p)) {
        val_t form = vcar(p);
        if (!vis_pair(form) || !vis_symbol(vcar(form))) continue;
        val_t head = lang_translate(vcar(form));
        if (head == S_DEFINE_RECORD_TYPE || head == S_DEFINE_RULE ||
            head == S_DEFINE_RULESET || head == S_DEFINE_ALGEBRA ||
            head == S_DEFINE_SYNTAX) {
            return false;
        }
        if (head == S_SYMBOLIC) {
            for (val_t q = vcdr(form); vis_pair(q); q = vcdr(q))
                if (vis_symbol(vcar(q))) names = scm_cons(vcar(q), names);
        } else if (head == S_DEFINE_VALUES) {
            val_t vars = vis_pair(vcdr(form)) ? vcar(vcdr(form)) : V_NIL;
            for (val_t q = vars; vis_pair(q) && vis_symbol(vcar(q)); q = vcdr(q))
                names = scm_cons(vcar(q), names);
        } else if (head == S_DEFINE) {
            val_t defname = vcar(vcdr(form));
            if (vis_symbol(defname)) names = scm_cons(defname, names);
            else if (vis_pair(defname)) names = scm_cons(vcar(defname), names);
        }
    }
    *names_out = names;
    return true;
}

static bool expr_is_closed(Compiler *c, val_t expr, BoundScope *scope, int *budget);

/* Walks a body (a list of forms, e.g. a lambda/let/do body): collects
 * its own front-of-body internal-definition names FIRST (matching
 * lambda_prescan's letrec*-style "every internal define visible from
 * the first one onward" semantics -- not incremental, or a later
 * internal define referencing an earlier one would be wrongly flagged
 * as capturing something), extends `scope` with them, then checks every
 * form against the extended scope. */
static bool body_is_closed(Compiler *c, val_t body, BoundScope *scope, int *budget) {
    val_t def_names;
    if (!collect_body_def_names(body, &def_names)) return false;
    BoundScope inner = { scope, def_names };
    for (val_t p = body; vis_pair(p); p = vcdr(p))
        if (!expr_is_closed(c, vcar(p), &inner, budget)) return false;
    return true;
}

/* Checks a `let`-shaped bindings list `((name init) ...)` where every
 * init is checked against `outer_scope` (siblings NOT visible to each
 * other -- correct, not just conservative, for plain let: see
 * lambda_is_closed's own top-level comment) and returns the collected
 * names as a fresh scope layer for the caller to check body against. */
static bool let_bindings_closed(Compiler *c, val_t bindings, BoundScope *outer_scope,
                                 int *budget, BoundScope *out_scope) {
    val_t names = V_NIL;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        val_t binding = vcar(b);
        /* This is a closedness PREDICATE, not a compile step -- a
         * malformed binding here just means "not eligible for this
         * optimization," fail-safe, not a raised error (unlike the
         * actual compile-time checks in ir_lower_let/ir_lower_let_star/
         * etc for the exact same shape). Raising here would incorrectly
         * change behavior for a program that never actually compiles
         * the lambda body being analyzed. Issue #124, found via
         * independent review: (let ((f (lambda () (let (1) 1)))) 1)
         * previously dereferenced a non-pair binding here and SIGSEGV'd. */
        if (!vis_pair(binding)) return false;
        val_t name = vcar(binding);
        val_t init = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
        if (!expr_is_closed(c, init, outer_scope, budget)) return false;
        names = scm_cons(name, names);
    }
    out_scope->parent = outer_scope;
    out_scope->names  = names;
    return true;
}

static bool expr_is_closed(Compiler *c, val_t expr, BoundScope *scope, int *budget) {
    if (*budget <= 0) return false;   /* exhausted -- fail safe: not closed */
    (*budget)--;

    if (vis_symbol(expr)) {
        if (bound_scope_has(scope, expr)) return true;
        if (resolve_local(c, expr) >= 0 || is_upvalue_reachable(c, expr))
            return false;   /* a genuine free-variable capture */
        return true;        /* global, or genuinely unbound -- either way, fine */
    }
    if (!vis_pair(expr)) return true;   /* self-evaluating atom -- fine */

    val_t raw_head = vcar(expr);
    val_t head = vis_symbol(raw_head) ? lang_translate(raw_head) : raw_head;

    if (head == S_QUOTE) return true;   /* quoted data, not a reference */

    /* Macro bail-out -- see this function's own header comment. Checked
     * before any of the special-form dispatch below, since a locally
     * shadowed macro name could otherwise be misread as one of these
     * keywords (the same risk classify_head's own dispatch is built to
     * avoid, mirrored here). */
    if (vis_symbol(raw_head) && head_is_macro(c, raw_head)) return false;
    if (head == S_DEFINE_SYNTAX || head == S_LET_SYNTAX || head == S_LETREC_SYNTAX)
        return false;

    if (head == S_LAMBDA) {
        val_t params2 = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2   = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        BoundScope inner = { scope, collect_param_names(params2) };
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET) {
        val_t rest = vcdr(expr);
        if (vis_pair(rest) && vis_symbol(vcar(rest))) {
            /* Named let: (let loop ((x v) ...) body...) -- loop's own
             * name IS visible within its own body (it's how the loop
             * recurses), unlike a plain let's sibling bindings. */
            val_t loop_name = vcar(rest);
            val_t bindings  = vis_pair(vcdr(rest)) ? vcar(vcdr(rest)) : V_NIL;
            val_t body2     = vis_pair(vcdr(rest)) ? vcdr(vcdr(rest)) : V_NIL;
            BoundScope inner;
            if (!let_bindings_closed(c, bindings, scope, budget, &inner)) return false;
            inner.names = scm_cons(loop_name, inner.names);
            return body_is_closed(c, body2, &inner, budget);
        }
        val_t bindings = vis_pair(rest) ? vcar(rest) : V_NIL;
        val_t body2    = vis_pair(rest) ? vcdr(rest) : V_NIL;
        BoundScope inner;
        if (!let_bindings_closed(c, bindings, scope, budget, &inner)) return false;
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET_STAR) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        BoundScope *cur = scope;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding = vcar(b);
            if (!vis_pair(binding)) return false;   /* see let_bindings_closed's comment */
            val_t name    = vcar(binding);
            val_t init    = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            /* Sequential: each init is checked against everything bound
             * by EARLIER bindings in this same let*, not later ones. */
            if (!expr_is_closed(c, init, cur, budget)) return false;
            BoundScope *level = (BoundScope *)ir_arena_alloc(
                c->ir_arena, sizeof(BoundScope));
            level->parent = cur;
            level->names  = scm_cons(name, V_NIL);
            cur = level;
        }
        return body_is_closed(c, body2, cur, budget);
    }

    if (head == S_LETREC || head == S_LETREC_STAR) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        /* letrec(*): every binding name is visible to every init AND to
         * the body -- build the whole scope layer first. */
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            if (!vis_pair(vcar(b))) return false;   /* see let_bindings_closed's comment */
        val_t names = V_NIL;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b))
            names = scm_cons(vcar(vcar(b)), names);
        BoundScope inner = { scope, names };
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding = vcar(b);
            val_t init = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            if (!expr_is_closed(c, init, &inner, budget)) return false;
        }
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_DO) {
        /* (do ((var init step) ...) (test result...) body...) */
        val_t var_specs = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t rest2     = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t term      = vis_pair(rest2) ? vcar(rest2) : V_NIL;
        val_t body2     = vis_pair(rest2) ? vcdr(rest2) : V_NIL;
        val_t names = V_NIL;
        for (val_t vs = var_specs; vis_pair(vs); vs = vcdr(vs)) {
            val_t spec = vcar(vs);
            if (!vis_pair(spec)) return false;   /* see let_bindings_closed's comment */
            val_t init = vis_pair(vcdr(spec)) ? vcar(vcdr(spec)) : V_FALSE;
            /* init exprs see the OUTER scope only, matching do's own
             * semantics (vars aren't bound yet for their own inits). */
            if (!expr_is_closed(c, init, scope, budget)) return false;
            names = scm_cons(vcar(spec), names);
        }
        BoundScope inner = { scope, names };
        for (val_t vs = var_specs; vis_pair(vs); vs = vcdr(vs)) {
            val_t spec = vcar(vs);
            /* Already validated as a pair by the first loop above (both
             * walk the same var_specs list), but re-checked defensively
             * since this loop re-derives `spec` independently -- cheap,
             * and avoids relying on control flow across two separate
             * loops to keep this safe if either is ever reordered. */
            if (!vis_pair(spec)) return false;
            val_t step = vis_pair(vcdr(spec)) && vis_pair(vcdr(vcdr(spec)))
                             ? vcar(vcdr(vcdr(spec))) : vcar(spec);
            if (!expr_is_closed(c, step, &inner, budget)) return false;
        }
        for (val_t t = term; vis_pair(t); t = vcdr(t))
            if (!expr_is_closed(c, vcar(t), &inner, budget)) return false;
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_RECEIVE) {
        val_t formals  = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t rest2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t producer = vis_pair(rest2) ? vcar(rest2) : V_FALSE;
        val_t body2    = vis_pair(rest2) ? vcdr(rest2) : V_NIL;
        if (!expr_is_closed(c, producer, scope, budget)) return false;
        BoundScope inner = { scope, collect_param_names(formals) };
        return body_is_closed(c, body2, &inner, budget);
    }

    if (head == S_LET_VALUES || head == S_LET_STAR_VALUES) {
        val_t bindings = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2    = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        bool star = (head == S_LET_STAR_VALUES);
        BoundScope *cur = scope;
        val_t all_names = V_NIL;
        for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
            val_t binding  = vcar(b);
            val_t formals  = vcar(binding);
            val_t producer = vis_pair(vcdr(binding)) ? vcar(vcdr(binding)) : V_FALSE;
            /* let-values: every producer sees only the OUTER scope.
             * let*-values: sequential, like let*. */
            if (!expr_is_closed(c, producer, star ? cur : scope, budget)) return false;
            if (star) {
                BoundScope *level = (BoundScope *)ir_arena_alloc(
                    c->ir_arena, sizeof(BoundScope));
                level->parent = cur;
                level->names  = collect_param_names(formals);
                cur = level;
            } else {
                val_t names = collect_param_names(formals);
                for (val_t q = names; vis_pair(q); q = vcdr(q))
                    all_names = scm_cons(vcar(q), all_names);
            }
        }
        BoundScope inner_flat = { scope, all_names };
        return body_is_closed(c, body2, star ? cur : &inner_flat, budget);
    }

    if (head == S_GUARD) {
        /* (guard (var clause...) body...) -- var is bound ONLY within
         * clause, never within body (compile_guard, compiler.c: body
         * gets its own separate zero-arg thunk that never sees var). */
        val_t var_and_clauses = vis_pair(vcdr(expr)) ? vcar(vcdr(expr)) : V_NIL;
        val_t body2   = vis_pair(vcdr(expr)) ? vcdr(vcdr(expr)) : V_NIL;
        val_t var     = vis_pair(var_and_clauses) ? vcar(var_and_clauses) : V_FALSE;
        val_t clauses = vis_pair(var_and_clauses) ? vcdr(var_and_clauses) : V_NIL;
        BoundScope inner = { scope, vis_symbol(var) ? scm_cons(var, V_NIL) : V_NIL };
        for (val_t cl = clauses; vis_pair(cl); cl = vcdr(cl))
            if (!expr_is_closed(c, vcar(cl), &inner, budget)) return false;
        return body_is_closed(c, body2, scope, budget);
    }

    /* Ordinary form (call, or any special form with no extra binding
     * semantics of its own -- if/and/or/begin/when/unless/cond/case/
     * set!/quasiquote/unquote/parameterize/etc.): recurse into every
     * element, head included -- a head symbol shadowed by an outer local
     * (a rare edge case) is correctly caught by the same vis_symbol
     * branch above when THIS same function is re-entered for it. */
    val_t p = expr;
    for (; vis_pair(p); p = vcdr(p))
        if (!expr_is_closed(c, vcar(p), scope, budget)) return false;
    /* The final, non-pair cdr -- almost always V_NIL for a well-formed
     * form, but a genuinely dotted tail (nonstandard for a call/special
     * form, though not something this walker should simply assume can't
     * occur) could itself be a reference. Checking it too costs nothing
     * for the overwhelmingly common nil case and closes what would
     * otherwise be a silent under-shadowing gap for the uncommon one. */
    return expr_is_closed(c, p, scope, budget);
}

/* Tier 2.4 public entry point: true iff `body` (compiled with `params`
 * as its own parameters) captures NO free variable reachable from `c` --
 * see this whole block's header comment for why this has to be a static,
 * pre-compile, pre-expansion check rather than IR_DEFINE's "compile it,
 * then read what it proved" approach. `*budget` is decremented by every
 * AST node visited and must already be positive on entry; exhaustion
 * fails safe (returns false, i.e. "not closed") rather than continuing
 * to recurse -- this must be independent of call-order relative to any
 * other budget check (see ir_count_ast_nodes's own comment for why an
 * earlier version of THAT function got exactly this wrong: relying on
 * being called first doesn't bound this function's OWN recursion if it
 * doesn't carry its own budget). */
static bool lambda_is_closed(Compiler *c, val_t params, val_t body, int *budget) {
    BoundScope top = { NULL, collect_param_names(params) };
    return body_is_closed(c, body, &top, budget);
}


/* ── Tier 2.1 IR: lowering + codegen (src/ir.h) ──────────────────────────
 *
 * ir_lower walks the same raw S-expressions compile() does, natively
 * recognizing: self-evaluating literals, #:keyword symbols, ordinary
 * variable references (local/upvalue/global -- resolution decided at
 * ir_emit time, exactly as emit_load does), quote, if, begin, set!, and,
 * or, `(define sym expr)` and `(define (f params...) body...)`
 * lambda-sugar, ordinary/fused-global/self-tail calls, lambda/closures,
 * and let / let-star / letrec / letrec-star / named-let. Everything else
 * compile()
 * would otherwise handle further down its dispatch chain (cond, case,
 * define-record-type, define-syntax, the CAS forms, import/
 * define-library/library, macro uses, ...) is wrapped whole as an
 * IR_FALLBACK leaf, whose ir_emit case is simply `compile(c, expr, tail,
 * line)`: byte-for-byte whatever would have happened had ir_lower never
 * run for that subform.
 *
 * ir_lower is guaranteed to never return NULL: every input either matches
 * one of the native cases above or becomes an IR_FALLBACK leaf.
 *
 * As of the seventh landing, ir_lower/ir_emit/ir_optimize are ACTUALLY
 * LIVE: compile()'s own dispatch (via the IR_OR_CLASSIC macro, defined
 * just above compile() itself) routes every case this section covers
 * through them, for every Scheme program curry compiles -- the REPL,
 * -e, script files, everything. They are no longer test-only; see
 * IR_OR_CLASSIC's own comment for the malformed-input infinite-
 * recursion hazard that had to be closed before this was safe, and
 * compile_classic's comment for why compiler_ir_self_check/
 * compiler_ir_optimize_check needed a new, genuinely IR-free reference
 * implementation once compile() itself stopped being one. */


static IRNode *ir_lower_if(Compiler *c, val_t args, bool tail, int line) {
    require_min_args(args, 2, "if");
    val_t test = vcar(args);  args = vcdr(args);
    val_t then = vcar(args);  args = vcdr(args);
    val_t els  = vis_pair(args) ? vcar(args) : V_VOID;

    IRNode *n = ir_node_new(c->ir_arena, IR_IF, tail, line);
    n->as.iff.test = ir_lower(c, test, false, line);
    n->as.iff.then = ir_lower(c, then, tail, line);
    n->as.iff.els  = ir_lower(c, els,  tail, line);
    return n;
}

/* Mirrors compile_seq's exact line-tracking (a mutable `line` threaded
 * through the loop, updated from each cons cell's own hdr.flags when
 * present) so IR_SEQ's pop_lines[] carries the same per-item line
 * compile_seq would have used for that item's OP_POP -- see IR_SEQ's own
 * field comment in ir.h for why this must be tracked SEPARATELY from
 * items[i]->line (a real, differential-self-check-invisible bug, found
 * by independent code review, when the two were conflated). */
static IRNode *ir_lower_seq(Compiler *c, val_t list, bool tail, int line) {
    if (vis_nil(list)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = V_VOID;
        return n;
    }
    /* `body` is the raw list, stored unprocessed -- see ir.h's comment on
     * IRNode::as.seq for why per-item lowering has to wait for ir_emit's
     * own interleaved walk now, same reasoning as IR_LAMBDA's body. */
    IRNode *n = ir_node_new(c->ir_arena, IR_SEQ, tail, line);
    n->as.seq.body = list;
    return n;
}

/* Mirrors compile_set (compiler.c) exactly: value compiled non-tail,
 * name resolution deferred to ir_emit via emit_store (see IRNode::
 * as.set's comment in ir.h). */
static IRNode *ir_lower_set(Compiler *c, val_t args, int line) {
    require_min_args(args, 2, "set!");
    val_t name = vcar(args);
    val_t expr = vcar(vcdr(args));
    IRNode *n = ir_node_new(c->ir_arena, IR_SET, false, line);
    n->as.set.name  = name;
    n->as.set.value = ir_lower(c, expr, false, line);
    return n;
}

/* Mirrors compile_and exactly, including which per-item tail value each
 * item gets: only the last item inherits `tail`, matching `compile(c,
 * vcar(args), last && tail, line)`. */
static IRNode *ir_lower_and(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_AND, tail, line);
        n->as.andor.items = NULL;
        n->as.andor.count = 0;
        return n;
    }
    size_t count = 0;
    for (val_t p = args; vis_pair(p); p = vcdr(p)) count++;

    IRNode *n = ir_node_new(c->ir_arena, IR_AND, tail, line);
    n->as.andor.items = (IRNode **)ir_arena_alloc(c->ir_arena,
                                                   sizeof(IRNode *) * count);
    n->as.andor.count = (int)count;

    size_t i = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        bool  last = vis_nil(next);
        n->as.andor.items[i++] = ir_lower(c, vcar(args), last && tail, line);
        args = next;
    }
    return n;
}

/* Mirrors compile_or exactly -- deliberately, NOT symmetric with
 * ir_lower_and: compile_or hardcodes `false` for every item's tail
 * argument, including the last, unlike compile_and. Preserved as-is,
 * see this landing's plan for why "fixing" this asymmetry is explicitly
 * out of scope here. */
static IRNode *ir_lower_or(Compiler *c, val_t args, bool tail, int line) {
    if (vis_nil(args)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_OR, tail, line);
        n->as.andor.items = NULL;
        n->as.andor.count = 0;
        return n;
    }
    size_t count = 0;
    for (val_t p = args; vis_pair(p); p = vcdr(p)) count++;

    IRNode *n = ir_node_new(c->ir_arena, IR_OR, tail, line);
    n->as.andor.items = (IRNode **)ir_arena_alloc(c->ir_arena,
                                                   sizeof(IRNode *) * count);
    n->as.andor.count = (int)count;

    size_t i = 0;
    while (vis_pair(args)) {
        val_t next = vcdr(args);
        n->as.andor.items[i++] = ir_lower(c, vcar(args), false, line);
        args = next;
    }
    return n;
}

/* Mirrors compile_define's `(define sym expr)` case exactly. */
static IRNode *ir_lower_define(Compiler *c, val_t args, int line) {
    val_t name  = vcar(args);
    val_t rest  = vcdr(args);
    val_t value = vis_pair(rest) ? vcar(rest) : V_VOID;
    IRNode *n = ir_node_new(c->ir_arena, IR_DEFINE, false, line);
    n->as.def.name  = name;
    n->as.def.value = ir_lower(c, value, false, line);
    return n;
}

/* Mirrors compile_lambda's own signature. Deliberately does NOT lower the
 * body here -- see ir.h's comment on IRNode::as.lambda for why the body
 * has to wait for ir_emit to create the real child Compiler it belongs
 * to (a scope that doesn't exist, and can't be faked, until then). */
IRNode *ir_lower_lambda(Compiler *c, val_t params, val_t body,
                                const char *name, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_LAMBDA, false, line);
    n->as.lambda.params = params;
    n->as.lambda.body   = body;
    n->as.lambda.name   = name;
    return n;
}

/* Mirrors compile_define's `(define (f params...) body...)` lambda-sugar
 * case exactly (name = vcar(target), params = vcdr(target), rest = the
 * lambda body) -- builds IR_DEFINE{value = IR_LAMBDA} now that IR_LAMBDA
 * exists, instead of falling the whole form back to IR_FALLBACK the way
 * the third landing had to. */
static IRNode *ir_lower_define_lambda_sugar(Compiler *c, val_t args, int line) {
    val_t target = vcar(args);
    val_t rest   = vcdr(args);
    val_t name   = vcar(target);
    val_t params = vcdr(target);
    IRNode *n = ir_node_new(c->ir_arena, IR_DEFINE, false, line);
    n->as.def.name  = name;
    n->as.def.value = ir_lower_lambda(c, params, rest, as_sym(name)->data, line);
    return n;
}

/* Mirrors compile_call, but builds a single IR_CALL node rather than
 * deciding self-tail/fused-global/generic here -- see ir.h's comment on
 * IRNode::as.call for why that classification has to wait for ir_emit.
 * `callee` is lowered eagerly (cheap, side-effect-free, like any other
 * subtree); only used by ir_emit's generic-call fallback branch. */
static IRNode *ir_lower_call(Compiler *c, val_t head, val_t args, bool tail, int line) {
    int argc = 0;
    for (val_t a = args; vis_pair(a); a = vcdr(a)) argc++;

    IRNode *n = ir_node_new(c->ir_arena, IR_CALL, tail, line);
    n->as.call.head   = head;
    n->as.call.callee = ir_lower(c, head, false, line);
    n->as.call.argc   = argc;
    n->as.call.args   = argc > 0
        ? (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *) * (size_t)argc)
        : NULL;

    int i = 0;
    for (val_t a = args; vis_pair(a); a = vcdr(a))
        n->as.call.args[i++] = ir_lower(c, vcar(a), false, line);
    return n;
}

/* Builds an IR_CALL{callee=IR_LAMBDA{...}} node directly (not via
 * ir_lower_call, which expects a raw val_t callee to lower itself --
 * here the callee is ALREADY the IR_LAMBDA ir_lower_lambda just built,
 * no S-expression synthesis needed for it). `head` is V_FALSE: never a
 * symbol, so IR_CALL's own ir_emit classification always falls through
 * to its generic (non-self-tail, non-fused-global) branch -- exactly
 * matching compile_let/compile_let_star/compile_letrec, which always
 * emit a plain OP_CALL/OP_TAIL_CALL for these desugared forms, never
 * OP_CALL_GLOBAL. Shared by ir_lower_let/ir_lower_let_star/
 * ir_lower_letrec below. */
static IRNode *ir_lower_lambda_call(Compiler *c, IRNode *callee,
                                     IRNode **args, int argc,
                                     bool tail, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_CALL, tail, line);
    n->as.call.head   = V_FALSE;
    n->as.call.callee = callee;
    n->as.call.argc   = argc;
    n->as.call.args   = args;
    return n;
}

/* Mirrors compile_let's own PLAIN (non-named) branch exactly: `(let
 * ((x v) ...) body...)` desugars to `((lambda (x ...) body...) v ...)`.
 * No new IR node kind needed -- IR_LAMBDA already handles everything a
 * let's body needs (its own child scope, internal defines, internal
 * macros -- see IR_LAMBDA's own comment in ir.h), so this is pure
 * ir_lower-time construction reusing it unchanged. The named-let branch
 * is NOT handled here -- see ir_lower_named_let, called separately from
 * ir_lower's own dispatch (it needs ir_emit-time work this can't do). */
static IRNode *ir_lower_let(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    /* require_min_args already guards the outer form's own spine
     * (args itself, at this function's caller -- see classify_head's
     * S_LET dispatch), but never validated each individual BINDING
     * entry within it: (let ((a)) 1) has a well-formed two-element
     * args list, so that check passes, then vcar(vcdr(vcar(b))) below
     * dereferences (a)'s nonexistent cdr and SIGSEGVs the whole
     * process (issue #124, found via independent review). Reusing
     * require_min_args on each binding itself (rather than on `args`)
     * catches both "not a pair at all" and "pair with no init"
     * uniformly, matching the exact idiom this file already uses
     * everywhere else. */
    for (val_t b = bindings; vis_pair(b); b = vcdr(b))
        require_min_args(vcar(b), 2, "let");

    /* Forward-order params list -- same double-reverse compile_let itself
     * uses (bindings is walked once to reverse, then reversed back). */
    val_t params = V_NIL;
    int argc = 0;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        params = scm_cons(vcar(vcar(b)), params);
        argc++;
    }
    val_t fwd = V_NIL;
    while (vis_pair(params)) { fwd = scm_cons(vcar(params), fwd); params = vcdr(params); }

    IRNode *callee = ir_lower_lambda(c, fwd, body, c->name, line);
    IRNode **argv = argc > 0
        ? (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *) * (size_t)argc)
        : NULL;
    /* Tier 2.4: one KnownParam slot per binding, populated for whichever
     * ones are eligible known-lambda candidates -- see IRKnownParam's own
     * comment (ir.h) for why this has to be decided HERE, at ir_lower
     * time, rather than post-hoc the way IR_DEFINE's registration works. */
    IRKnownParam *kp = argc > 0
        ? (IRKnownParam *)ir_arena_alloc(c->ir_arena, sizeof(IRKnownParam) * (size_t)argc)
        : NULL;
    int i = 0;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        argv[i] = ir_lower(c, vcar(vcdr(vcar(b))), false, line);
        kp[i].closed = false;
        /* Only a bare (lambda ...) literal is ever eligible -- matches
         * IR_DEFINE's own `value->kind == IR_LAMBDA` gate exactly: an
         * expression that merely EVALUATES to a lambda isn't statically
         * inspectable. */
        if (argv[i]->kind == IR_LAMBDA) {
            val_t cand_params = argv[i]->as.lambda.params;
            val_t cand_body   = argv[i]->as.lambda.body;
            int cand_argc;
            int budget = INLINE_MAX_BODY_NODES;
            /* Tier 2.4: reject if the candidate's body references ANY of
             * THIS let's own binding names (including its own) -- see
             * body_contains_any_symbol's own comment for why: those
             * names aren't yet resolvable in `c` (so lambda_is_closed's
             * existing enclosing-scope check can't see them), but ARE
             * real locals once the wrapper is entered -- exactly the
             * gap a confirmed miscompilation was found through before
             * this check existed. */
            if (params_proper_arity(cand_params, &cand_argc) &&
                ir_count_ast_nodes(cand_body, INLINE_MAX_BODY_NODES) <= INLINE_MAX_BODY_NODES &&
                !body_contains_any_symbol(cand_body, fwd) &&
                lambda_is_closed(c, cand_params, cand_body, &budget)) {
                kp[i].closed = true;
                kp[i].params = cand_params;
                kp[i].body   = cand_body;
                kp[i].argc   = cand_argc;
            }
        }
        i++;
    }
    callee->as.lambda.known_params = kp;
    return ir_lower_lambda_call(c, callee, argv, argc, tail, line);
}

/* Mirrors compile_let_star exactly, including its base case (empty
 * bindings compiles the body directly via ir_lower_seq, no lambda
 * wrapper) and its single-binding-at-a-time nesting: `(let* ((x v)
 * rest...) body)` -> `((lambda (x) (let* rest... body)) v)`. The
 * synthesized inner `(let* rest body)` form is embedded as the lambda's
 * OWN body (via ir_lower_lambda, same as ir_lower_let above) -- it gets
 * lowered lazily, later, when ir_emit's IR_LAMBDA case walks that body,
 * re-entering this same function via ir_lower's own S_LET_STAR dispatch
 * hook. No manual recursion needed here -- IR_LAMBDA's deferred-body
 * design already gives this the same per-level laziness compile_let_star
 * itself relies on (each inner let* is only compiled when ITS enclosing
 * lambda's body is). */
static IRNode *ir_lower_let_star(Compiler *c, val_t args, bool tail, int line) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);

    if (vis_nil(bindings))
        return ir_lower_seq(c, body, tail, line);

    val_t binding = vcar(bindings);
    /* See ir_lower_let's identical comment (issue #124): validates this
     * one binding before destructuring it. Only the first binding needs
     * checking here -- each further one gets its own check the next
     * time this function recurses on the desugared inner `let*` form
     * below. */
    require_min_args(binding, 2, "let*");
    val_t name    = vcar(binding);
    val_t init    = vcar(vcdr(binding));
    val_t rest    = vcdr(bindings);

    val_t inner_body;
    if (vis_nil(rest)) {
        inner_body = body;
    } else {
        val_t let_star_sym = sym_intern_cstr("let*");
        val_t inner_form   = scm_cons(let_star_sym, scm_cons(rest, body));
        inner_body = scm_cons(inner_form, V_NIL);
    }
    val_t params = scm_cons(name, V_NIL);

    IRNode *callee = ir_lower_lambda(c, params, inner_body, c->name, line);
    IRNode **argv  = (IRNode **)ir_arena_alloc(c->ir_arena, sizeof(IRNode *));
    argv[0] = ir_lower(c, init, false, line);

    /* Tier 2.4: same single-slot registration as ir_lower_let above,
     * just for this one binding. `c` here is exactly the right enclosing
     * scope for THIS binding's own closedness check -- the "rest" of a
     * let* (any FURTHER bindings) doesn't exist as a real scope yet at
     * all; it's still raw, embedded body, only re-lowered later against
     * the real child Compiler this binding's own IR_LAMBDA eventually
     * gets (see this function's own header comment on why that's fine:
     * the same laziness that makes let*'s per-level nesting free also
     * gives each later binding's OWN lambda_is_closed check the correct,
     * progressively narrower `c` for free, the next time ir_lower_let_star
     * is re-entered). */
    IRKnownParam *kp = (IRKnownParam *)ir_arena_alloc(c->ir_arena, sizeof(IRKnownParam));
    kp[0].closed = false;
    if (argv[0]->kind == IR_LAMBDA) {
        val_t cand_params = argv[0]->as.lambda.params;
        val_t cand_body   = argv[0]->as.lambda.body;
        int cand_argc;
        int budget = INLINE_MAX_BODY_NODES;
        /* Tier 2.4: reject if the candidate body references its OWN
         * binding name -- see body_contains_any_symbol's comment
         * (compiler.c) for the full rationale; only one name to check
         * here (unlike plain let's possibly-several siblings), since
         * let*'s later bindings aren't visible to earlier ones at all
         * (sequential scoping), so there's no sibling-name hazard beyond
         * self-reference for this one binding. */
        if (params_proper_arity(cand_params, &cand_argc) &&
            ir_count_ast_nodes(cand_body, INLINE_MAX_BODY_NODES) <= INLINE_MAX_BODY_NODES &&
            !body_contains_symbol(cand_body, name) &&
            lambda_is_closed(c, cand_params, cand_body, &budget)) {
            kp[0].closed = true;
            kp[0].params = cand_params;
            kp[0].body   = cand_body;
            kp[0].argc   = cand_argc;
        }
    }
    callee->as.lambda.known_params = kp;

    return ir_lower_lambda_call(c, callee, argv, 1, tail, line);
}

/* Mirrors compile_letrec exactly (shared by S_LETREC and S_LETREC_STAR,
 * same as compile_letrec itself -- curry draws no stricter distinction
 * between them at the compiler level). Rather than replicate
 * compile_letrec's own bespoke "pre-declare all locals with void
 * placeholders, then compile+store each init in order" bytecode
 * sequence, this desugars to `((lambda () (define n1 v1) (define n2 v2)
 * ... body...)))`  -- lambda_prescan (compiler.c) ALREADY gives ordinary
 * internal defines this exact same "every name visible from the first
 * define onward, initialized in sequence" treatment for letrec*
 * semantics, which is what compile_letrec itself provides for both
 * letrec and letrec* today, so this produces identical observable
 * behavior through already-verified machinery instead of a parallel
 * bespoke implementation. */
static IRNode *ir_lower_letrec(Compiler *c, val_t args, bool tail, int line, val_t head) {
    val_t bindings = vcar(args);
    val_t body     = vcdr(args);
    const char *form_name = (head == S_LETREC_STAR) ? "letrec*" : "letrec";

    /* See ir_lower_let's identical comment (issue #124). form_name
     * distinguishes letrec/letrec* in the raised error -- previously
     * hardcoded to "letrec" even for a malformed letrec* binding, found
     * by independent code review. */
    for (val_t b = bindings; vis_pair(b); b = vcdr(b))
        require_min_args(vcar(b), 2, form_name);

    /* Build (define name init) forms in reverse, then prepend each (in
     * that reverse order) onto body -- restores original binding order
     * at the front, ahead of the original body forms. */
    val_t rev = V_NIL;
    for (val_t b = bindings; vis_pair(b); b = vcdr(b)) {
        val_t name = vcar(vcar(b));
        val_t init = vcar(vcdr(vcar(b)));
        val_t def_form = scm_cons(S_DEFINE, scm_cons(name, scm_cons(init, V_NIL)));
        rev = scm_cons(def_form, rev);
    }
    val_t new_body = body;
    for (val_t p = rev; vis_pair(p); p = vcdr(p))
        new_body = scm_cons(vcar(p), new_body);

    IRNode *callee = ir_lower_lambda(c, V_NIL, new_body, "<letrec>", line);
    return ir_lower_lambda_call(c, callee, NULL, 0, tail, line);
}

/* Named let: `(let loop ((x v) ...) body)` -- see compile_let's own
 * comment for the full "zero-arg outer wrapper" shape this mirrors.
 * Unlike every other let / let-star / letrec form above, this ISN'T
 * pure ir_lower-time desugaring -- see ir.h's comment on IRNode::as.named_let
 * for why the self-tail-call thread-local arming forces the whole
 * construction into ir_emit instead. This function only stores the raw
 * pieces ir_emit's IR_NAMED_LET case needs. */
static IRNode *ir_lower_named_let(Compiler *c, val_t loop_name, val_t bindings,
                                   val_t body, bool tail, int line) {
    IRNode *n = ir_node_new(c->ir_arena, IR_NAMED_LET, tail, line);
    n->as.named_let.loop_name = loop_name;
    n->as.named_let.bindings  = bindings;
    n->as.named_let.body      = body;
    return n;
}

IRNode *ir_lower(Compiler *c, val_t expr, bool tail, int line) {
    if (vis_pair(expr) && as_pair(expr)->hdr.flags)
        line = (int)as_pair(expr)->hdr.flags;

    /* ── Self-evaluating atoms (mirrors compile()'s own checks exactly) ── */
    if (vis_fixnum(expr) || vis_flonum(expr) || vis_bignum(expr) ||
        vis_rational(expr) || vis_complex(expr) || vis_string(expr) ||
        vis_char(expr)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }
    if (expr == V_TRUE || expr == V_FALSE || expr == V_NIL || expr == V_VOID) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }

    /* ── Symbol → variable reference ── */
    if (vis_symbol(expr)) {
        Symbol *ksym = as_sym(expr);
        if (ksym->len >= 2 && ksym->data[0] == '#' && ksym->data[1] == ':') {
            IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
            n->as.konst.value = expr;
            return n;
        }
        /* Resolution (local vs upvalue vs global) is deliberately NOT
         * decided here -- see IRNode::as.var_ref's comment in ir.h for
         * why: resolve_upvalue/chunk_add_const have ordering-sensitive
         * side effects that must happen in the same left-to-right order
         * ir_emit walks the tree, not the order ir_lower happens to
         * build it in. */
        IRNode *n = ir_node_new(c->ir_arena, IR_VAR_REF, tail, line);
        n->as.var_ref.name = expr;
        return n;
    }

    /* ── Non-pair non-symbol: quote it ── */
    if (!vis_pair(expr)) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value = expr;
        return n;
    }

    /* ── Compound form ── */
    val_t head = lang_translate(vcar(expr));
    val_t args = vcdr(expr);

    if (head == S_QUOTE) {
        IRNode *n = ir_node_new(c->ir_arena, IR_CONST, tail, line);
        n->as.konst.value  = vis_pair(args) ? vcar(args) : V_NIL;
        n->as.konst.quoted = true;
        return n;
    }
    if (head == S_IF)    return ir_lower_if(c, args, tail, line);
    if (head == S_BEGIN) return ir_lower_seq(c, args, tail, line);
    if (head == S_SET)   return ir_lower_set(c, args, line);
    if (head == S_AND)   return ir_lower_and(c, args, tail, line);
    if (head == S_OR)    return ir_lower_or(c, args, tail, line);
    /* No IR_FALLBACK path here (or on S_LET just below) for malformed
     * args -- this makes ir_lower NEVER produce IR_FALLBACK for
     * head==S_LAMBDA at all. Originally deliberate for a specific reason:
     * if it COULD fall back to IR_FALLBACK here, ir_emit's IR_FALLBACK
     * case would call compile() again on the same expr, which
     * re-classifies to this same SF_LAMBDA case and (once wired into
     * compile()'s live dispatch) recurses into this exact code path
     * again -- an infinite loop on malformed input, found during design
     * review before compile() ever called ir_lower live. The ORIGINAL fix
     * for that was accepting SIGSEGV-parity with classic's own equally
     * unchecked `vcar(args)`/`vcdr(args)` (a bare `(lambda)` crashed the
     * whole process, confirmed present on `main`) -- require_min_args
     * below is a real improvement on that, not a reintroduction of the
     * infinite-loop risk: raising via scm_raise_code is a longjmp, not a
     * return, so it never produces an IR_FALLBACK node for ir_emit to
     * reprocess at all -- it unwinds straight out of this whole compile
     * attempt instead. S_DEFINE can't use the ORIGINAL crash-parity trick
     * (compile_define's malformed-target case degrades gracefully with an
     * error message, not a crash -- silently mis-lowering it instead of
     * replicating that behavior would be a correctness bug, not crash-
     * parity), so compile()'s own SF_DEFINE case instead checks for
     * IR_FALLBACK and falls back to compile_define directly -- see
     * compile()'s dispatch for the full reasoning; unaffected by this
     * change either way, since S_DEFINE never reaches this file's own
     * IR_FALLBACK-avoidance concern in the first place. */
    if (head == S_LAMBDA) {
        require_min_args(args, 1, "lambda");
        return ir_lower_lambda(c, vcar(args), vcdr(args), NULL, line);
    }
    /* let -- named vs plain distinguished the same way compile_let
     * itself does (a leading symbol instead of a bindings list). Named
     * let routes to ir_lower_named_let (IR_NAMED_LET); everything else
     * routes to ir_lower_let (pure desugaring, see its own comment). */
    if (head == S_LET) {
        /* Same require_min_args + no-IR_FALLBACK reasoning as S_LAMBDA
         * just above -- a bare `(let)` used to SIGSEGV here identically
         * to `vcar(V_NIL)` crashing classic's own compile_let. */
        require_min_args(args, 1, "let");
        val_t bindings = vcar(args);
        if (vis_symbol(bindings)) {
            val_t let_body = vcdr(args);
            require_min_args(let_body, 1, "let");  /* named-let's own bindings list */
            return ir_lower_named_let(c, bindings, vcar(let_body), vcdr(let_body), tail, line);
        }
        return ir_lower_let(c, args, tail, line);
    }
    if (head == S_LET_STAR) {
        require_min_args(args, 1, "let*");
        return ir_lower_let_star(c, args, tail, line);
    }
    if (head == S_LETREC || head == S_LETREC_STAR) {
        require_min_args(args, 1, head == S_LETREC ? "letrec" : "letrec*");
        return ir_lower_letrec(c, args, tail, line, head);
    }
    /* Both `(define sym expr)` and `(define (f params...) body...)`
     * lambda-sugar are natively lowered (the latter via IR_LAMBDA, now
     * that it exists -- see ir_lower_define_lambda_sugar's comment);
     * anything else (malformed args) falls through to the generic
     * IR_FALLBACK wrap below, matching compile_define's own error path. */
    if (head == S_DEFINE && vis_pair(args)) {
        val_t target = vcar(args);
        if (vis_symbol(target)) return ir_lower_define(c, args, line);
        if (vis_pair(target))   return ir_lower_define_lambda_sugar(c, args, line);
    }

    /* Anything else compile() would treat as a special form or macro use
     * (classify_head -- the same function compile() itself switches on,
     * see its own comment) still falls back whole; only a genuine
     * ordinary call (SF_NONE) gets natively lowered here. */
    {
        val_t transformer = V_FALSE;
        if (classify_head(c, head, args, &transformer) == SF_NONE)
            return ir_lower_call(c, head, args, tail, line);
    }

    /* Not (yet) natively lowered -- delegate the whole subform. */
    IRNode *n = ir_node_new(c->ir_arena, IR_FALLBACK, tail, line);
    n->as.fallback.expr = expr;
    return n;
}

/* ── Tier 2.2: cheap IR optimizations (docs/thoughts/
 * performance-chez-kaappi.md §5, item 2.2) ─────────────────────────────
 *
 * ir_optimize_andor (below) recurses via ir_optimize before ir_optimize
 * itself is defined -- forward-declared here. */

/* ir_optimize runs on an already-lowered tree, between ir_lower and
 * ir_emit, and rewrites it in place (no arena allocation -- it only ever
 * splices existing subtrees into a parent's slot or leaves a node
 * untouched, never builds a new one). Two transforms land here:
 *   - dead-branch elimination on IR_IF: `(if <const> then else)` folds
 *     to whichever branch the constant's truthiness picks, discarding
 *     the OTHER branch's bytecode and the test's own load+jump
 *     instructions entirely.
 *   - boolean simplification on IR_AND/IR_OR (ir_optimize_andor, below):
 *     a non-last constant item that already decides the short-circuit
 *     outcome truncates the list there (everything after is dead); a
 *     non-last constant item that doesn't is spliced out (side-effect-
 *     free, contributes nothing).
 *
 * This is the first Tier 2.1/2.2 transform in this codebase that
 * deliberately produces DIFFERENT bytecode than compile()'s own
 * unoptimized output for affected inputs -- every prior landing
 * (IR_SET/AND/OR/DEFINE/CALL/LAMBDA) was required to be byte-identical,
 * verified by compiler_ir_self_check's memcmp. That comparison does NOT
 * apply here (an optimizer whose output always byte-matches unoptimized
 * code isn't optimizing anything); compiler_ir_optimize_check (below)
 * verifies this pass by actually RUNNING both compiled forms and
 * comparing their RESULTS instead.
 *
 * A folded branch's own `->tail` field is already correct without
 * adjustment: ir_lower_if lowers `then`/`els` with the SAME `tail` value
 * the IR_IF node itself received, so splicing the taken branch directly
 * into the parent's slot preserves tail-position correctness for free.
 *
 * IR_LAMBDA and IR_SEQ are deliberately NOT recursed into: their bodies
 * are raw val_t at this point in the pipeline (see IRNode::as.lambda/
 * as.seq's own comments in ir.h), not yet an IR tree -- there is nothing
 * for this pass to walk until their own ir_emit cases do their
 * interleaved lower+emit walks, a separate concern from this pass
 * (IR_SEQ's own deferred design exists for the same internal-define-
 * syntax-scoping reason IR_LAMBDA's already did -- found as a real
 * ctest-only regression, invisible to compiler_ir_self_check, once
 * compile() started routing through the IR live). IR_FALLBACK is opaque
 * for the same reason (raw val_t, not a tree) and IR_VAR_REF/IR_CONST
 * are leaves -- none of these five kinds get a case below; they fall
 * through to the default `return n` unchanged. */

/* Shared by IR_AND/IR_OR: compacts the items list in place, applying two
 * safe transforms to each NON-LAST item (after recursively optimizing
 * it, so an item that itself folds down to a constant -- e.g. a nested
 * `(if #t 1 2)` -- is caught by the same pass):
 *
 *   - a constant whose truthiness already decides the whole expression's
 *     short-circuit outcome (falsy for AND, truthy for OR) truncates the
 *     list right there: it becomes the new last item, and every item
 *     after it is unreachable dead code, discarded.
 *   - a constant whose truthiness does NOT decide the outcome (truthy
 *     for AND, falsy for OR) contributes nothing but its own position in
 *     the sequence, and -- being a literal -- has no side effect either:
 *     spliced out entirely.
 *
 * The last item is never touched by either rule (guarded by `!is_last`):
 * its VALUE is what the whole expression evaluates to when reached, not
 * just a truth test, so it's kept (and still recursively optimized)
 * regardless of its own truthiness. A dropped or truncation-point item
 * is, by construction, always an IR_CONST -- literals ignore `tail`
 * entirely in their own codegen, so this never disturbs the one item
 * (the ORIGINAL last item, when no truncation happens) whose `->tail`
 * ir_lower_and/or actually set meaningfully for tail-call purposes.
 * `short_circuits_on_falsy` selects AND's vs OR's roles. */
static void ir_optimize_andor(IRNode *n, bool short_circuits_on_falsy) {
    int in_count  = n->as.andor.count;
    int out_count = 0;
    for (int i = 0; i < in_count; i++) {
        bool     is_last = (i == in_count - 1);
        IRNode  *item    = ir_optimize(n->as.andor.items[i]);
        if (!is_last && item->kind == IR_CONST) {
            bool truthy  = item->as.konst.value != V_FALSE;
            bool ends_it = short_circuits_on_falsy ? !truthy : truthy;
            if (ends_it) {
                n->as.andor.items[out_count++] = item;
                break;  /* truncate: this becomes the new last item */
            }
            continue;   /* provably doesn't end it, no side effect: drop */
        }
        n->as.andor.items[out_count++] = item;
    }
    n->as.andor.count = out_count;
}

IRNode *ir_optimize(IRNode *n) {
    switch (n->kind) {
    case IR_IF: {
        IRNode *test = ir_optimize(n->as.iff.test);
        if (test->kind == IR_CONST) {
            bool truthy = test->as.konst.value != V_FALSE;
            return ir_optimize(truthy ? n->as.iff.then : n->as.iff.els);
        }
        n->as.iff.test = test;
        n->as.iff.then = ir_optimize(n->as.iff.then);
        n->as.iff.els  = ir_optimize(n->as.iff.els);
        return n;
    }
    /* IR_SEQ is NOT recursed into, same reasoning as IR_LAMBDA just
     * below: its body is raw val_t at this point in the pipeline (see
     * IRNode::as.seq's own comment in ir.h) -- there's no pre-lowered
     * tree here to optimize until ir_emit's own interleaved walk builds
     * one, item by item. Falls to the default `return n` unchanged. */
    case IR_SET:
        n->as.set.value = ir_optimize(n->as.set.value);
        return n;
    case IR_AND: ir_optimize_andor(n, true);  return n;
    case IR_OR:  ir_optimize_andor(n, false); return n;
    case IR_DEFINE:
        n->as.def.value = ir_optimize(n->as.def.value);
        return n;
    case IR_CALL:
        n->as.call.callee = ir_optimize(n->as.call.callee);
        for (int i = 0; i < n->as.call.argc; i++)
            n->as.call.args[i] = ir_optimize(n->as.call.args[i]);
        return n;
    default:
        return n;
    }
}
