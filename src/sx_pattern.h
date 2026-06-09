#pragma once
#include "value.h"

/* Returns 1 if v is a pattern variable (symbol whose name starts with '?') */
int   sx_is_pvar(val_t v);

/* Collect all pattern variables from a pattern in left-to-right depth-first
   order, without duplicates.  Returns a Scheme list of symbols. */
val_t sx_pattern_vars(val_t pattern);

/* Match pattern against expr.
   Pattern is a Scheme list/tree where ?-symbols are variables, _ is a
   wildcard, numbers/other-symbols match literally, and (op arg ...) matches
   a SymExpr with the same operator and arity.
   Returns V_FALSE on failure; on success returns an alist ((sym . val) ...).
   The alist is in reverse depth-first order — callers should look up by key,
   not by position. */
val_t sx_pattern_match(val_t pattern, val_t expr);
