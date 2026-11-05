#ifndef CURRY_SYNTAX_CASE_H
#define CURRY_SYNTAX_CASE_H

/*
 * syntax_case.h -- R6RS-style syntax-case, built on syntax_rules.c's matcher
 * and template expander.
 *
 * Syntax objects here are the identifiers a template introduces (SynObj in
 * object.h); everything else stays an ordinary S-expression. Each macro
 * expansion by a procedural transformer gets a fresh mark, and
 * sc_apply_transformer turns the identifiers it created back into plain
 * symbols (a fresh name per mark) before the result reaches the compilers,
 * so no other file has to know the type exists.
 *
 * This is partial hygiene, the same level syntax-rules has. A name a
 * template introduces is renamed per expansion unless it already names
 * something real in the macro's defining environment (a special form, a
 * global procedure or macro), so an introduced binder does not capture a
 * use-site local. Two gaps remain, both shared with syntax-rules: a binder
 * whose name is also a global (`tmp` with a global `tmp`) is not renamed,
 * and a use-site binding can still shadow a name the template refers to
 * freely. See docs/thoughts/syntax-case-hygiene-plan.md for the phases
 * that close them.
 */

#include "value.h"

/* Registers syntax-case, syntax, quasisyntax, with-syntax and the
 * %sc-* / identifier procedures in `env`. Call after syntax_rules_register. */
void syntax_case_register(val_t env);

/* Applies a macro transformer to the whole use-site form and returns its
 * expansion, ready for compile()/eval(). Every place that expands a macro
 * goes through this. A built-in (primitive) transformer such as
 * syntax-rules is applied directly; a user's procedural transformer gets a
 * fresh expansion mark and has its syntax objects resolved afterwards. */
val_t sc_apply_transformer(val_t transformer, val_t form);

#endif /* CURRY_SYNTAX_CASE_H */
