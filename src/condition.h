#ifndef CURRY_CONDITION_H
#define CURRY_CONDITION_H

/*
 * Common Lisp-style condition system for Curry Scheme.
 *
 * Two orthogonal mechanisms:
 *
 *   handler-bind  — non-unwinding handlers.  The handler is called with the
 *                   full call stack intact.  If it returns normally, the next
 *                   handler is tried.  If it calls invoke-restart, execution
 *                   jumps to the matching with-restarts frame.
 *
 *   with-restarts — named recovery points.  invoke-restart longjmps here and
 *                   runs the chosen restart thunk, returning its value as the
 *                   result of the with-restarts form.
 *
 * handler-case / guard remain the unwinding fallback (ExnHandler chain).
 *
 * Condition hierarchy:
 *   define-condition registers a type name and its parent types.
 *   condition-is-a? walks the hierarchy so a handler for <math-error> also
 *   catches <singular-matrix> if <singular-matrix> inherits from <math-error>.
 *
 * Scheme API (primitives — higher-level forms in lib/curry/modules/curry/conditions.scm):
 *   (%make-condition type-sym fields-alist message)  → condition
 *   (condition? v)
 *   (condition-type c)        → symbol
 *   (condition-fields c)      → alist
 *   (condition-message c)     → string or #f
 *   (condition-backtrace c)   → list of (name file line) frames, innermost
 *                                first; V_NIL for user-signalled conditions
 *                                or errors raised outside the VM
 *   (condition-code c)        → stable symbol (e.g. 'wrong-type-argument) or
 *                                #f; see docs/reference/error-codes.md
 *   (%condition-type-register! type-sym parent-list)
 *   (condition-is-a? c type-sym)
 *   (%signal c)               → void  (non-unwinding)
 *   (%handler-bind-1 type-sym proc thunk)  → thunk result
 *   (%with-restarts restarts-list thunk)   → thunk or restart result
 *   (%invoke-restart name-sym)             → never returns
 *   (%find-restart name-sym)               → restart or #f
 *   (%make-restart name desc thunk)        → restart
 *   (restart? v)
 *   (restart-name r)          → symbol
 *   (restart-description r)   → string
 */

#include "value.h"
#include "object.h"
#include <stdbool.h>

void condition_init(void);

/* Condition hierarchy */
void  condition_type_register(val_t type_sym, val_t parents);
bool  condition_is_a(val_t exn, val_t type_sym);

/* Condition construction */
val_t condition_make(val_t type_sym, val_t fields, val_t message);
val_t condition_field(val_t cond, val_t field_sym);

/* Restart construction */
val_t restart_make(val_t name, val_t desc, val_t thunk);

/* Signal — walk non-unwinding handler chain */
void  condition_signal(val_t cond);

/* handler-bind: push one (type, proc) handler, call thunk, pop handler */
val_t condition_handler_bind(val_t type_sym, val_t proc, val_t thunk);

/* with-restarts: push restart frame, call thunk; invoke-restart longjmps here */
val_t condition_with_restarts(val_t restarts, val_t thunk);

/* invoke-restart / find-restart */
val_t condition_invoke_restart(val_t name);   /* noreturn on success */
val_t condition_find_restart(val_t name);     /* → restart or #f */

/* Register Scheme primitives */
void condition_register_builtins(val_t env);

#endif /* CURRY_CONDITION_H */
