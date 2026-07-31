/*
 * lang_registry.c — see lang_registry.h for the API contract.
 */

#include "lang_registry.h"
#include "object.h"

#define LANG_MAX_PACKS 8

static const LangPack *_packs[LANG_MAX_PACKS];
static int _pack_count = 0;

/* Each language's own setup function -- declared here rather than in a
 * shared header, since nothing outside lang_registry_init() needs to call
 * them directly. Add one line per new language. */
void akkadian_lang_setup(void);

/* eval_init() calls this unconditionally at startup; compiler_compile()
 * also calls it lazily on first use (its own static-bool guard doesn't
 * know eval_init() already ran it, since the two live in different
 * translation units). Idempotent here, not just at each call site: packs
 * accumulate in a list rather than overwriting flat arrays the way the
 * pre-registry code did, so a second call would otherwise double-register
 * every pack. */
void lang_registry_init(void) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    akkadian_lang_setup();
    /* future languages register themselves here, e.g.:
     *   sumerian_lang_setup();
     */
}

void lang_register_pack(const LangPack *pack) {
    if (_pack_count < LANG_MAX_PACKS) _packs[_pack_count++] = pack;
}

val_t lang_translate(val_t sym) {
    if (!vis_symbol(sym)) return sym;
    for (int p = 0; p < _pack_count; p++) {
        const LangPack *pack = _packs[p];
        for (int i = 0; i < pack->sf_count; i++)
            if (pack->sf[i].foreign == sym) return pack->sf[i].eng;
    }
    return sym;
}

int lang_pr_lookup(val_t english, val_t *out_forms, int max_forms) {
    if (!vis_symbol(english)) return 0;
    int n = 0;
    for (int p = 0; p < _pack_count && n < max_forms; p++) {
        const LangPack *pack = _packs[p];
        for (int i = 0; i < pack->pr_count; i++) {
            if (pack->pr[i].eng != english) continue;
            for (int f = 0; f < pack->pr[i].nforms && n < max_forms; f++)
                out_forms[n++] = pack->pr[i].forms[f];
            break;
        }
    }
    return n;
}

int lang_pr_count(void) {
    int total = 0;
    for (int p = 0; p < _pack_count; p++) total += _packs[p]->pr_count;
    return total;
}

bool lang_pr_at(int index, val_t *english, val_t *out_forms, int *nforms) {
    if (index < 0) return false;
    for (int p = 0; p < _pack_count; p++) {
        const LangPack *pack = _packs[p];
        if (index < pack->pr_count) {
            *english = pack->pr[index].eng;
            *nforms  = pack->pr[index].nforms;
            for (int f = 0; f < pack->pr[index].nforms; f++)
                out_forms[f] = pack->pr[index].forms[f];
            return true;
        }
        index -= pack->pr_count;
    }
    return false;
}
