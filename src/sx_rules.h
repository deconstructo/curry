#pragma once
#include "value.h"

/* Initialise the rule registry.  Must be called after sym_init(). */
void  sx_rules_init(void);

/* Register a rule globally.
   pattern   — quoted Scheme list/tree (e.g. '(* 0 ?x))
   pvars     — ordered list of pattern variable symbols (e.g. '(?x))
   guard_fn  — V_FALSE, or a callable (lambda (pvars...) bool)
   action_fn — callable (lambda (pvars...) replacement)
   ruleset   — ruleset name symbol, or V_FALSE for the global ruleset */
void  sx_rule_add(val_t pattern, val_t pvars,
                  val_t guard_fn, val_t action_fn, val_t ruleset);

/* Try all registered rules against expr (which must be a SymExpr).
   Returns V_VOID if no rule matched; otherwise returns the (un-simplified)
   result produced by the first matching rule whose guard passed. */
val_t sx_rule_try(val_t expr);

/* Return a Scheme list of rule descriptors for introspection.
   If op is V_FALSE, returns all rules; otherwise filters by operator. */
val_t sx_rules_list(val_t op);

/* Remove all rules (optionally filtered by ruleset name; V_FALSE = all). */
void  sx_rules_clear(val_t ruleset);

/* Semispace GC external scanner — scans all val_t fields in rule structs.
   Registered automatically by sx_rules_init(). */
void  sx_rules_gc_scan(void);
