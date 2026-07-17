#ifndef CURRY_UNICODE_TABLES_H
#define CURRY_UNICODE_TABLES_H

/* GENERATED FILE — do not hand-edit.
 * Produced by tools/gen-unicode-tables.py from the Unicode Character
 * Database version 16.0.0 (UnicodeData.txt, DerivedCoreProperties.txt,
 * PropList.txt, CaseFolding.txt — unicode.org/Public/16.0.0/ucd/).
 * Regenerate: python3 tools/gen-unicode-tables.py (run from repo root)
 *
 * Each range table is a sorted, non-overlapping array of inclusive
 * [start, end] codepoint pairs. Each mapping table is a pair of parallel
 * arrays (from[i] -> to[i]), sorted by from[], both length *_count.
 * Consumed by src/unicode.c via binary search.
 */

#include <stdint.h>
#include <stddef.h>

extern const uint32_t unicode_alpha_ranges[][2];
extern const size_t unicode_alpha_ranges_count;

extern const uint32_t unicode_numeric_ranges[][2];
extern const size_t unicode_numeric_ranges_count;

extern const uint32_t unicode_space_ranges[][2];
extern const size_t unicode_space_ranges_count;

extern const uint32_t unicode_upper_ranges[][2];
extern const size_t unicode_upper_ranges_count;

extern const uint32_t unicode_lower_ranges[][2];
extern const size_t unicode_lower_ranges_count;

extern const uint32_t unicode_upper_map_from[];
extern const uint32_t unicode_upper_map_to[];
extern const size_t unicode_upper_map_count;

extern const uint32_t unicode_lower_map_from[];
extern const uint32_t unicode_lower_map_to[];
extern const size_t unicode_lower_map_count;

extern const uint32_t unicode_fold_map_from[];
extern const uint32_t unicode_fold_map_to[];
extern const size_t unicode_fold_map_count;

#endif /* CURRY_UNICODE_TABLES_H */
