#ifndef CURRY_SYMBOL_H
#define CURRY_SYMBOL_H

/*
 * Symbol interning table.
 *
 * Every unique symbol name maps to a unique heap address, so symbol
 * equality is O(1) pointer comparison (eq?).  The table is a GC-rooted
 * open-addressing hash map that grows as needed.
 *
 * Pre-interned symbols for all R7RS special forms and the actor primitives
 * are available as global val_t variables after sym_init().
 */

#include "value.h"
#include <stdint.h>

void       sym_init(void);
val_t      sym_intern(const char *name, uint32_t len);
val_t      sym_intern_cstr(const char *name);
const char *sym_cstr(val_t sym);   /* NUL-terminated name */
uint32_t   sym_len(val_t sym);

/* ---- Pre-interned special-form symbols ---- */
extern val_t
    /* core */
    S_QUOTE, S_QUASIQUOTE, S_UNQUOTE, S_UNQUOTE_SPLICING,
    S_DEFINE, S_DEFINE_SYNTAX, S_DEFINE_VALUES, S_DEFINE_RECORD_TYPE,
    S_LAMBDA, S_IF, S_BEGIN, S_SET,
    S_LET, S_LET_STAR, S_LETREC, S_LETREC_STAR,
    S_LET_VALUES, S_LET_STAR_VALUES,
    S_LET_SYNTAX, S_LETREC_SYNTAX, S_SYNTAX_RULES,
    S_AND, S_OR, S_NOT, S_COND, S_CASE, S_WHEN, S_UNLESS, S_DO,
    S_DELAY, S_DELAY_FORCE, S_MAKE_PROMISE,
    S_PARAMETERIZE, S_GUARD, S_INCLUDE, S_COND_EXPAND,
    S_VALUES, S_CALL_WITH_VALUES, S_CALL_CC, S_CALL_WITH_CC,
    S_WITH_EXCEPTION_HANDLER,
    /* module system */
    S_DEFINE_LIBRARY, S_LIBRARY, S_IMPORT, S_EXPORT,
    S_ONLY, S_EXCEPT, S_RENAME, S_PREFIX, S_DEFINED_P, S_FOR,
    /* R6RS record syntax */
    S_FIELDS, S_MUTABLE, S_IMMUTABLE,
    /* syntax helpers */
    S_ELSE, S_ARROW, S_DOT, S_REST,
    /* actor primitives */
    S_SPAWN, S_SEND, S_RECEIVE, S_SELF, S_LINK, S_MONITOR,
    /* common identifiers */
    S_ERROR, S_FILE_ERROR, S_READ_ERROR,
    /* stable machine-legible error codes — docs/reference/error-codes.md */
    EC_WRONG_TYPE_ARGUMENT, EC_WRONG_NUMBER_OF_ARGUMENTS, EC_UNBOUND_VARIABLE,
    EC_NOT_A_PROCEDURE, EC_DIVISION_BY_ZERO, EC_INDEX_OUT_OF_RANGE, EC_STACK_OVERFLOW,
    S_APPLY, S_MAP, S_FOR_EACH,
    /* symbolic / CAS */
    S_SYMBOLIC,
    S_DEFINE_RULE, S_DEFINE_RULESET, S_RULE_ARROW, S_KW_WHEN, S_UNDERSCORE,
    S_DEFINE_ALGEBRA, S_WITH_ASSUMPTIONS,
    S_KW_COMMUTATIVE, S_KW_ASSOCIATIVE, S_KW_IDENTITY, S_KW_ABSORBING, S_KW_RELATIONS,
    /* runtime profiling */
    S_EVAL_PROFILER, S_GC_PROFILER;

#endif /* CURRY_SYMBOL_H */
