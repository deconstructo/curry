#ifndef CURRY_UNICODE_H
#define CURRY_UNICODE_H

/*
 * Unicode-aware character classification and simple case mapping.
 *
 * C's <ctype.h> functions are only defined for values representable as
 * unsigned char (0-255) or EOF — passing a full Unicode codepoint (curry
 * chars store up to 0x10FFFF, see vchr()/vunchr() in value.h) is
 * undefined behavior for any codepoint above 0xFF. These functions cover
 * the full Unicode range: an ASCII fast path (behavior identical to the
 * previous ctype.h-based implementation) plus a binary-search fallback
 * over generated classification/case-mapping tables (src/unicode_tables.c,
 * produced by tools/gen-unicode-tables.py) for everything else.
 *
 * Case mapping is simple (1:1 codepoint) only, matching R7RS's allowance
 * for char-upcase/char-downcase/char-foldcase to skip full Unicode
 * special casing (e.g. German ß -> "SS", which isn't a single character
 * and so cannot be a char-upcase result at all).
 */

#include <stdbool.h>
#include <stdint.h>

bool     unicode_is_alphabetic(uint32_t cp);
bool     unicode_is_numeric(uint32_t cp);
bool     unicode_is_whitespace(uint32_t cp);
bool     unicode_is_upper(uint32_t cp);
bool     unicode_is_lower(uint32_t cp);

/* Identity if the codepoint has no simple case mapping. */
uint32_t unicode_to_upper(uint32_t cp);
uint32_t unicode_to_lower(uint32_t cp);

/* Simple case folding (CaseFolding.txt common+simple status) — distinct
 * from unicode_to_lower: a small number of codepoints fold differently
 * than they lowercase (e.g. U+212A KELVIN SIGN folds to 'k' but has no
 * lowercase mapping of its own, since it's already "lowercase-shaped"). */
uint32_t unicode_fold_case(uint32_t cp);

#endif /* CURRY_UNICODE_H */
