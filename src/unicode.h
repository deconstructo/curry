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
 * unicode_to_upper/lower and unicode_fold_case are simple (1:1 codepoint),
 * for char-upcase/char-downcase/char-foldcase (German ß -> "SS" isn't a
 * single character, so cannot be a char-upcase result). The
 * unicode_full_* functions below give the full mapping (SpecialCasing.txt
 * and CaseFolding.txt F entries) that the string procedures use.
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

/* Full case mappings: write the mapped codepoints (at most 3) to out and
 * return how many. A codepoint with no multi-character mapping gives its
 * simple mapping (1 codepoint). unicode_full_lower has no context: the
 * final-sigma rule is applied by the caller. */
int unicode_full_upper(uint32_t cp, uint32_t out[3]);
int unicode_full_lower(uint32_t cp, uint32_t out[3]);
int unicode_full_fold(uint32_t cp, uint32_t out[3]);

#endif /* CURRY_UNICODE_H */
