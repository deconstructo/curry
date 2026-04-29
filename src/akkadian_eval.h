/*
 * akkadian_eval.h — Special-form name translation for eval.c.
 *
 * Call akk_eval_setup() once after sym_init().
 * Then call akk_translate(op) on the head of every form before the
 * special-form dispatch chain.  Akkadian/cuneiform synonyms are remapped
 * to their canonical English symbols; everything else is returned unchanged.
 */

#ifndef CURRY_AKKADIAN_EVAL_H
#define CURRY_AKKADIAN_EVAL_H

#include "symbol.h"
#include "value.h"

#define AKK_MAX_SF_ENTRIES 256

typedef struct { val_t akk; val_t eng; } AkkSFEntry;

static AkkSFEntry _akk_sf_table[AKK_MAX_SF_ENTRIES];
static int        _akk_sf_count = 0;

static inline void akk_eval_setup(void) {
    int i = 0;

/* For each special-form entry, register two rows: transliterated and cuneiform. */
#define AKK(e, t, c)    /* procedure — handled in builtins, not here */
#define AKK_PR(e, t, c) /* procedure — skip */
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
/* macros are cleaned up by akkadian_names.h's own #undef at its end */
#undef AKK
}

static inline val_t akk_translate(val_t sym) {
    if (!vis_symbol(sym)) return sym;
    for (int i = 0; i < _akk_sf_count; i++)
        if (_akk_sf_table[i].akk == sym) return _akk_sf_table[i].eng;
    return sym;
}

#endif /* CURRY_AKKADIAN_EVAL_H */
