#ifndef CURRY_SYNTAX_RULES_H
#define CURRY_SYNTAX_RULES_H

/*
 * syntax_rules.h — R7RS syntax-rules macro transformer.
 *
 * Implements pattern-matching macro expansion. syntax-rules is registered
 * in the global environment as a T_SYNTAX, so eval passes the entire
 * unevaluated form to the compile function, which returns a T_PRIMITIVE
 * transformer.
 *
 * Hygiene note: this is partial, not full, hygiene. Full hygiene would
 * mean identifiers carry lexical context ("color") resolved against the
 * right environment at every reference — a rewrite of how identifiers are
 * represented throughout the interpreter, not a macro-expander-local fix.
 * What this implementation actually does (syntax_rules.c's "Partial
 * hygiene" header comment above sr_is_pattern_var has the full
 * rationale, including two earlier heuristics that were tried and
 * replaced after each broke a real case): after a pattern match, every
 * template-introduced symbol (not a pattern variable, not the ellipsis
 * identifier or "_", not inside a quoted literal) that ISN'T already a
 * real reference — a core special-form keyword, or something bound in
 * the macro's own DEFINING environment (an ordinary global procedure
 * like `apply`, or the macro's own name for a recursive self-call) —
 * gets one fresh gensym for that one expansion, substituted consistently
 * throughout its output. This is what makes recursive macros that
 * accumulate bindings (the standard SRFI-26 cut/cute reference
 * implementation, for one) work correctly instead of colliding separate
 * intended bindings into one literal name, while a genuine free
 * reference to something real — lambda, apply, a helper procedure name,
 * the macro's own name — still resolves normally. A macro intentionally
 * capturing/exposing an identifier to its caller (classic anaphoric
 * macros) is still possible for names that don't happen to also be
 * globally bound.
 */

#include "value.h"

/* Call once after sym_init() to intern internal symbols, then once per
 * environment to register the syntax-rules keyword. */
void syntax_rules_register(val_t env);

/* If `transformer` was produced by (syntax-rules ...) (i.e. is the
 * T_PRIMITIVE sr_transformer_fn wraps), fill the literals/rules/ellipsis
 * out-parameters with its underlying pattern/template data (always plain,
 * serializable Scheme data — lists/symbols, no closures or C pointers) and
 * return true.
 * Used by compiler.c's define-syntax codegen to detect the common case
 * where a compiled macro's runtime re-registration (needed for .scc
 * cache-hit persistence) can rebuild the transformer directly from this
 * pure data via sr_rebuild_syntax, instead of re-evaluating the original
 * transformer-expression source — which would risk re-running any
 * side-effecting code surrounding the (syntax-rules ...) form itself
 * (e.g. `(begin (side-effect!) (syntax-rules ...))`). Returns false for
 * any other (procedural) transformer, which has no such pure-data
 * decomposition and must fall back to re-evaluation. */
bool sr_transformer_data(val_t transformer, val_t *literals, val_t *rules,
                          val_t *ellipsis);

/* Rebuild a syntax-rules transformer procedure directly from previously-
 * extracted literals/rules/ellipsis data, bypassing both re-parsing a
 * (syntax-rules ...) form and re-evaluating any expression that produced
 * it. Returns a bare transformer (the same shape a (syntax-rules ...)
 * expression evaluates to) — deliberately NOT Syntax-wrapped, so that
 * define-syntax's own single wrapping step (in compiler.c/eval.c) is the
 * only place a Syntax struct gets built; raises a normal Scheme error
 * (never silently misbehaves) if literals/rules/ellipsis aren't
 * well-formed. */
/* def_env is the transformer's defining environment (V_VOID means
 * "unknown, default to GLOBAL_ENV") -- see sr_rebuild_syntax_env's own
 * comment in syntax_rules.c for why this exists (target_env-scoped
 * macros need their runtime-rebuilt transformer's def_env to match, not
 * always GLOBAL_ENV). */
val_t sr_rebuild_syntax_env(val_t literals, val_t rules, val_t ellipsis, val_t def_env);

/* The environment sr_compile_fn ("syntax-rules" itself) is currently
 * being evaluated in, for it to capture as a new transformer's def_env —
 * see syntax_rules.c's own header comment on this pair of functions for
 * why it's a thread-local rather than a parameter, and who's
 * responsible for saving/setting/restoring it (eval.c's T_SYNTAX
 * dispatch). V_FALSE means "not currently tracked" (the compiled path,
 * which can only ever see GLOBAL_ENV here regardless). */
val_t sr_get_current_env(void);
void  sr_set_current_env(val_t env);

/* Companion to sr_get_current_env/sr_set_current_env for compiler.c's
 * let-syntax/letrec-syntax and locally-scoped define-syntax: a list of
 * compile-time-local macro names in scope, since those live only in
 * compiler.c's own SyntaxLocal table, never in any runtime environment
 * sr_get_current_env's def_env could see. Set by compile_let_syntax/
 * compile_define_syntax around each compile_time_eval call (save/set/
 * restore, same discipline as sr_current_env). See syntax_rules.c's own
 * header comment on this pair for the full rationale, including the
 * self-recursive local-macro bug this exists to fix. */
val_t sr_get_current_local_macros(void);
void  sr_set_current_local_macros(val_t names);

#endif /* CURRY_SYNTAX_RULES_H */
