#ifndef CURRY_FEATURES_H
#define CURRY_FEATURES_H

/*
 * R7RS feature identifiers and cond-expand support.
 *
 * R7RS 4.2.9 (cond-expand) tests a small requirement grammar --
 * feature identifiers, (library <name>), (and ...), (or ...), (not ...),
 * else -- against the implementation's supported-features list (also
 * exposed directly as the (features) procedure, R7RS 6.13.3). Both the
 * feature list and the requirement-testing logic live here so they have a
 * single definition shared by all three call sites that need to resolve a
 * cond-expand clause: eval.c (expression position), compiler.c (VM-compiled
 * expression position), and modules.c (define-library declaration position,
 * where a matched clause's body is itself a list of further declarations,
 * not expressions).
 */

#include "value.h"
#include <stdbool.h>

/* (features) -- proper list of feature-identifier symbols this build
 * supports. Built once, lazily, and cached. */
val_t features_list(void);

/* Test a single cond-expand feature-requirement (a feature-identifier
 * symbol, or an (and ...)/(or ...)/(not ...)/(library <name>) form) against
 * features_list() and modules_available(). Unrecognized/malformed
 * requirements test false rather than raising, matching this SRFI/R7RS
 * corner's general "absent means false" spirit. */
bool features_test(val_t requirement);

/* Given the clause list of a cond-expand form -- ((requirement expr...) ...
 * (else expr...)) -- return the body (expr...) of the first clause whose
 * requirement is satisfied via *matched = true, or set *matched = false if
 * none match (including an absent/no-op else). Does not evaluate or compile
 * anything; callers do that with the returned body list. */
val_t cond_expand_choose(val_t clauses, bool *matched);

#endif
