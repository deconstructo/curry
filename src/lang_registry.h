/*
 * lang_registry.h — pluggable multi-language name-translation registry.
 *
 * Curry supports source code written in translated synonyms of every
 * special form and built-in procedure name (see akkadian_names.h /
 * akkadian_lang.c for the reference language, Akkadian). This header is
 * the language-agnostic machinery that makes that work; it knows nothing
 * about Akkadian specifically.
 *
 * A "pack" is one language's contribution: a table of special-form
 * synonyms (foreign symbol -> canonical English symbol) and a table of
 * procedure synonyms (canonical English symbol -> 1..N foreign spellings,
 * e.g. Akkadian bundles a transliterated form and a cuneiform form under
 * one pack). Packs register themselves once, from lang_registry_init(),
 * by calling lang_register_pack() with statically-allocated tables.
 *
 * lang_translate() and lang_pr_lookup()/lang_pr_at() scan every
 * registered pack, so eval.c, compiler.c, builtins.c, and modules.c never
 * need to change when a new language is added — only a new
 * <lang>_names.h + <lang>_lang.c pair, plus one line in
 * lang_registry_init().
 *
 * To add a new language:
 *   1. Write <lang>_names.h with your own local macros (mirroring
 *      AKK_SF/AKK_PR in akkadian_names.h) listing (english, form...)
 *      rows. It does not need to match Akkadian's two-script shape --
 *      a language with one written form just supplies one.
 *   2. Write <lang>_lang.c: expand your macros into static LangSFRow[]
 *      and LangPRRow[] arrays (see akkadian_lang.c for the pattern),
 *      then call lang_register_pack() from a <lang>_lang_setup()
 *      function.
 *   3. Call <lang>_lang_setup() from lang_registry_init() in
 *      lang_registry.c.
 */

#ifndef CURRY_LANG_REGISTRY_H
#define CURRY_LANG_REGISTRY_H

#include <stdbool.h>
#include "symbol.h"
#include "value.h"

/* Max written forms per procedure entry across all languages contributing
 * to that entry (Akkadian uses 2: transliterated + cuneiform). */
#define LANG_PR_MAX_FORMS 4

typedef struct { val_t foreign; val_t eng; } LangSFRow;
typedef struct { val_t eng; val_t forms[LANG_PR_MAX_FORMS]; int nforms; } LangPRRow;

typedef struct LangPack {
    const char *id;           /* e.g. "akkadian" */
    const LangSFRow *sf; int sf_count;
    const LangPRRow *pr; int pr_count;
} LangPack;

/* Call exactly once, from eval_init(), before any of the functions below
 * are used. Populates every registered language's tables. */
void lang_registry_init(void);

/* Register one language's pack. Called by each language's own setup
 * function during lang_registry_init(). The arrays pointed to by
 * pack->sf/pack->pr must outlive the program (static storage) -- the
 * registry keeps the pointer, it does not copy. */
void lang_register_pack(const LangPack *pack);

/* Special-form translation: foreign-language symbol -> canonical English
 * symbol. Scans every registered pack. Symbols with no match (including
 * already-English symbols) are returned unchanged. Safe to call
 * regardless of what has or hasn't been imported -- built purely from
 * static tables, independent of any environment. */
val_t lang_translate(val_t sym);

/* Procedure-alias lookup used by modules_import() (src/modules.c): for a
 * canonical English procedure name, collect every foreign-language
 * spelling from every registered pack into out_forms (capacity
 * max_forms) and return how many were written. */
int lang_pr_lookup(val_t english, val_t *out_forms, int max_forms);

/* Flat iteration over every procedure entry in every registered pack,
 * used by builtins.c's startup alias pass (which needs to walk all
 * entries rather than look up one english name at a time). Returns false
 * once index is out of range. */
int  lang_pr_count(void);
bool lang_pr_at(int index, val_t *english, val_t *out_forms, int *nforms);

#endif /* CURRY_LANG_REGISTRY_H */
