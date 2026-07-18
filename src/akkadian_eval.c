/*
 * akkadian_eval.c — shared storage and setup for akkadian_eval.h.
 *
 * See akkadian_eval.h for the API contract. akk_eval_setup() must be
 * called exactly once, from eval_init(), before either table is queried.
 */

#include "akkadian_eval.h"
#include "object.h"

#define AKK_MAX_SF_ENTRIES 256
#define AKK_MAX_PR_ENTRIES 2048

typedef struct { val_t akk; val_t eng; } AkkSFEntry;
typedef struct { val_t eng; val_t translit; val_t cuneiform; } AkkPREntry;

static AkkSFEntry _akk_sf_table[AKK_MAX_SF_ENTRIES];
static int        _akk_sf_count = 0;

static AkkPREntry _akk_pr_table[AKK_MAX_PR_ENTRIES];
static int        _akk_pr_count = 0;

void akk_eval_setup(void) {
    int i = 0;
    int j = 0;

/* For each special-form entry, register two rows: transliterated and
 * cuneiform. For each procedure entry, register one english->(translit,
 * cuneiform) row. */
#define AKK(e, t, c) /* neither — unused marker in akkadian_names.h */
#define AKK_PR(e, t, c) \
    if (j < AKK_MAX_PR_ENTRIES) { \
        _akk_pr_table[j].eng       = sym_intern_cstr(e); \
        _akk_pr_table[j].translit  = sym_intern_cstr(t); \
        _akk_pr_table[j].cuneiform = sym_intern_cstr(c); \
        j += 1; \
    }
#define AKK_SF(e, t, c) \
    if (i + 2 <= AKK_MAX_SF_ENTRIES) { \
        _akk_sf_table[i  ].akk = sym_intern_cstr(t); \
        _akk_sf_table[i  ].eng = sym_intern_cstr(e); \
        _akk_sf_table[i+1].akk = sym_intern_cstr(c); \
        _akk_sf_table[i+1].eng = sym_intern_cstr(e); \
        i += 2; \
    }

#include "akkadian_names.h"

    _akk_sf_count = i;
    _akk_pr_count = j;
/* macros are cleaned up by akkadian_names.h's own #undef at its end */
#undef AKK
}

val_t akk_translate(val_t sym) {
    if (!vis_symbol(sym)) return sym;
    for (int i = 0; i < _akk_sf_count; i++)
        if (_akk_sf_table[i].akk == sym) return _akk_sf_table[i].eng;
    return sym;
}

bool akk_pr_lookup(val_t english, val_t *translit, val_t *cuneiform) {
    if (!vis_symbol(english)) return false;
    for (int i = 0; i < _akk_pr_count; i++) {
        if (_akk_pr_table[i].eng == english) {
            *translit  = _akk_pr_table[i].translit;
            *cuneiform = _akk_pr_table[i].cuneiform;
            return true;
        }
    }
    return false;
}
