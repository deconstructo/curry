/*
 * unicode.c — lookup layer over the generated Unicode tables.
 * See unicode.h for the rationale.
 */

#include "unicode.h"
#include "unicode_tables.h"
#include <ctype.h>
#include <stddef.h>

/* Binary search a sorted, non-overlapping [start, end] range table.
 * Returns true if cp falls within any range. */
static bool range_contains(const uint32_t (*ranges)[2], size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < ranges[mid][0]) hi = mid;
        else if (cp > ranges[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

/* Binary search a sorted `from` array; returns the matching `to` entry,
 * or cp unchanged if not present (identity — no case mapping defined). */
static uint32_t map_lookup(const uint32_t *from, const uint32_t *to,
                           size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < from[mid]) hi = mid;
        else if (cp > from[mid]) lo = mid + 1;
        else return to[mid];
    }
    return cp;
}

bool unicode_is_alphabetic(uint32_t cp) {
    if (cp < 128) return isalpha((int)cp) != 0;
    return range_contains(unicode_alpha_ranges, unicode_alpha_ranges_count, cp);
}

bool unicode_is_numeric(uint32_t cp) {
    if (cp < 128) return isdigit((int)cp) != 0;
    return range_contains(unicode_numeric_ranges, unicode_numeric_ranges_count, cp);
}

bool unicode_is_whitespace(uint32_t cp) {
    if (cp < 128) return isspace((int)cp) != 0;
    return range_contains(unicode_space_ranges, unicode_space_ranges_count, cp);
}

bool unicode_is_cased(uint32_t cp) {
    return range_contains(unicode_cased_ranges, unicode_cased_ranges_count, cp);
}

bool unicode_is_case_ignorable(uint32_t cp) {
    return range_contains(unicode_case_ignorable_ranges, unicode_case_ignorable_ranges_count, cp);
}

bool unicode_is_upper(uint32_t cp) {
    if (cp < 128) return isupper((int)cp) != 0;
    return range_contains(unicode_upper_ranges, unicode_upper_ranges_count, cp);
}

bool unicode_is_lower(uint32_t cp) {
    if (cp < 128) return islower((int)cp) != 0;
    return range_contains(unicode_lower_ranges, unicode_lower_ranges_count, cp);
}

uint32_t unicode_to_upper(uint32_t cp) {
    if (cp < 128) return (uint32_t)toupper((int)cp);
    return map_lookup(unicode_upper_map_from, unicode_upper_map_to,
                       unicode_upper_map_count, cp);
}

uint32_t unicode_to_lower(uint32_t cp) {
    if (cp < 128) return (uint32_t)tolower((int)cp);
    return map_lookup(unicode_lower_map_from, unicode_lower_map_to,
                       unicode_lower_map_count, cp);
}

uint32_t unicode_fold_case(uint32_t cp) {
    if (cp < 128) return (uint32_t)tolower((int)cp);
    return map_lookup(unicode_fold_map_from, unicode_fold_map_to,
                       unicode_fold_map_count, cp);
}

static int full_map(const uint32_t *from, const uint32_t (*to)[3], size_t count,
                    uint32_t cp, uint32_t simple, uint32_t out[3]) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < from[mid]) hi = mid;
        else if (cp > from[mid]) lo = mid + 1;
        else {
            int n = 0;
            while (n < 3 && to[mid][n]) { out[n] = to[mid][n]; n++; }
            return n;
        }
    }
    out[0] = simple;
    return 1;
}

int unicode_full_upper(uint32_t cp, uint32_t out[3]) {
    return full_map(unicode_full_upper_from, unicode_full_upper_to,
                    unicode_full_upper_count, cp, unicode_to_upper(cp), out);
}

int unicode_full_lower(uint32_t cp, uint32_t out[3]) {
    return full_map(unicode_full_lower_from, unicode_full_lower_to,
                    unicode_full_lower_count, cp, unicode_to_lower(cp), out);
}

int unicode_full_fold(uint32_t cp, uint32_t out[3]) {
    return full_map(unicode_full_fold_from, unicode_full_fold_to,
                    unicode_full_fold_count, cp, unicode_fold_case(cp), out);
}
