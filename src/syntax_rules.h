#ifndef CURRY_SYNTAX_RULES_H
#define CURRY_SYNTAX_RULES_H

/*
 * syntax_rules.h — R7RS syntax-rules macro transformer.
 *
 * Implements unhygienic pattern-matching macro expansion. syntax-rules is
 * registered in the global environment as a T_SYNTAX, so eval passes the
 * entire unevaluated form to the compile function, which returns a T_PRIMITIVE
 * transformer. Introduced identifiers resolve at use-site (not definition-site).
 *
 * Hygiene note: this implementation is intentionally unhygienic. Template
 * symbols that are not pattern variables are emitted as-is and resolved in
 * the expansion environment, not the definition environment.
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
val_t sr_rebuild_syntax(val_t literals, val_t rules, val_t ellipsis);

#endif /* CURRY_SYNTAX_RULES_H */
