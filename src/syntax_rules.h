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

#endif /* CURRY_SYNTAX_RULES_H */
