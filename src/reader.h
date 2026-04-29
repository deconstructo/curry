#ifndef CURRY_READER_H
#define CURRY_READER_H

/*
 * S-expression reader for Curry Scheme (R7RS §7.1).
 *
 * Reads from any port.  Supports:
 *   - Atoms: booleans, numbers, characters, strings, symbols
 *   - Lists and dotted pairs
 *   - Vectors: #(...)
 *   - Bytevectors: #u8(...)
 *   - Quote shorthands: ' ` , ,@
 *   - Block comments: #| ... |#
 *   - Datum comments: #;
 *   - #true / #false
 *   - Exact/inexact prefixes: #e #i #b #o #d #x
 *
 * Returns V_EOF when input is exhausted.
 * Raises a read error (via scm_error) on malformed input.
 */

#include "value.h"
#include <stdbool.h>

/* Parse a number from a string (used by builtins string->number) */
val_t parse_number(const char *s, int radix, bool exact_force, bool inexact_force);

/* Read one datum from port.  Returns the value or V_EOF. */
val_t scm_read(val_t port);

/* Read from a C string (convenience) */
val_t scm_read_cstr(const char *src);

/* Read all datums from port into a list (used for file loading) */
val_t scm_read_all(val_t port);

#endif /* CURRY_READER_H */
