/*
 * akkadian_lang.c — builds Akkadian's LangPack from akkadian_names.h and
 * registers it with the language registry (lang_registry.h).
 *
 * This is the reference implementation for adding a new language: expand
 * your names header into static tables, wrap them in a LangPack, and call
 * lang_register_pack() from a <lang>_lang_setup() function that
 * lang_registry_init() calls.
 *
 * akkadian_lang_setup() must be called exactly once, from
 * lang_registry_init(), before lang_translate()/lang_pr_lookup() are
 * queried. The tables below are real (non-static-to-a-TU-other-than-this-
 * one... they ARE file-static, but that's fine: every consumer goes
 * through lang_registry.c's pack pointer, never touches these arrays
 * directly) globals populated once here.
 */

#include "akkadian_lang.h"
#include "lang_registry.h"

#define AKK_MAX_SF_ENTRIES 256
#define AKK_MAX_PR_ENTRIES 2048

static LangSFRow _akk_sf_table[AKK_MAX_SF_ENTRIES];
static LangPRRow _akk_pr_table[AKK_MAX_PR_ENTRIES];

static LangPack _akkadian_pack;

void akkadian_lang_setup(void) {
    int i = 0;
    int j = 0;

/* For each special-form entry, register two rows: transliterated and
 * cuneiform. For each procedure entry, register one english->(translit,
 * cuneiform) row bundling both written forms under this one pack. */
#define AKK(e, t, c) /* neither — unused marker in akkadian_names.h */
#define AKK_PR(e, t, c) \
    if (j < AKK_MAX_PR_ENTRIES) { \
        _akk_pr_table[j].eng      = sym_intern_cstr(e); \
        _akk_pr_table[j].forms[0] = sym_intern_cstr(t); \
        _akk_pr_table[j].forms[1] = sym_intern_cstr(c); \
        _akk_pr_table[j].nforms   = 2; \
        j += 1; \
    }
#define AKK_SF(e, t, c) \
    if (i + 2 <= AKK_MAX_SF_ENTRIES) { \
        _akk_sf_table[i  ].foreign = sym_intern_cstr(t); \
        _akk_sf_table[i  ].eng     = sym_intern_cstr(e); \
        _akk_sf_table[i+1].foreign = sym_intern_cstr(c); \
        _akk_sf_table[i+1].eng     = sym_intern_cstr(e); \
        i += 2; \
    }

#include "akkadian_names.h"

/* macros are cleaned up by akkadian_names.h's own #undef at its end */
#undef AKK

    _akkadian_pack.id       = "akkadian";
    _akkadian_pack.sf       = _akk_sf_table;
    _akkadian_pack.sf_count = i;
    _akkadian_pack.pr       = _akk_pr_table;
    _akkadian_pack.pr_count = j;
    lang_register_pack(&_akkadian_pack);
}
