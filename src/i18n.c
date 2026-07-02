/*
 * i18n.c — Pluggable language pack runtime.
 *
 * One pack is active at a time.  Translation (lang_translate) is the hot
 * path; it loads g_active with acquire semantics and does a lock-free linear
 * scan.  Registration and activation are infrequent and take g_lang_mtx.
 *
 * Thread safety:
 *  - g_active is _Atomic(LangPack *).  Writers use release, readers acquire,
 *    so the fully-constructed LangPack is visible before the pointer update.
 *  - apply_aliases() must NOT be called while holding g_lang_mtx: it calls
 *    env_define() which may trigger GC or acquire internal env locks.
 *    Callers capture the pack pointer, release g_lang_mtx, then call
 *    apply_aliases().  Applying aliases for a transiently-wrong pack is
 *    harmless — it just adds extra env bindings.
 */

#include "i18n.h"
#include "symbol.h"
#include "env.h"
#include "gc.h"
#include "builtins.h"   /* scm_cons */
#include "akkadian.h"   /* akkadian_preamble fallback */

#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/* ── Registry ────────────────────────────────────────────────────────────── */

#define LANG_MAX_PACKS 64

static LangPack  *g_packs[LANG_MAX_PACKS];
static int        g_packs_count   = 0;
/* Atomic pointer-to-LangPack: writers use release, readers use acquire. */
static _Atomic(LangPack *) g_active = NULL;
static bool       g_builtins_ready  = false;

static pthread_mutex_t g_lang_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Convenience: load g_active with acquire semantics. */
static inline LangPack *active_load(void) {
    return atomic_load_explicit(&g_active, memory_order_acquire);
}
/* Convenience: store to g_active with release semantics (call under g_lang_mtx). */
static inline void active_store(LangPack *p) {
    atomic_store_explicit(&g_active, p, memory_order_release);
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

static char *heap_strdup(const char *s) {
    if (!s || !*s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = GC_MALLOC_ATOMIC(n);
    memcpy(p, s, n);
    return p;
}

static LangPack *pack_alloc(const LangPack *src) {
    LangPack *p = GC_MALLOC(sizeof(LangPack));
    memset(p, 0, sizeof(*p));
    strncpy(p->id,           src->id,           sizeof(p->id)           - 1);
    strncpy(p->display_name, src->display_name, sizeof(p->display_name) - 1);
    strncpy(p->intro,        src->intro,        sizeof(p->intro)        - 1);
    strncpy(p->error_prefix, src->error_prefix, sizeof(p->error_prefix) - 1);
    if (src->count > 0) {
        p->mappings = GC_MALLOC(src->count * sizeof(LangMapping));
        for (size_t i = 0; i < src->count; i++) {
            p->mappings[i].foreign     = src->mappings[i].foreign;
            p->mappings[i].canonical   = src->mappings[i].canonical;
            p->mappings[i].description = heap_strdup(src->mappings[i].description);
        }
    }
    p->count = src->count;
    p->cap   = src->count;
    return p;
}

static LangPack *pack_find_locked(const char *id) {
    for (int i = 0; i < g_packs_count; i++)
        if (strcmp(g_packs[i]->id, id) == 0) return g_packs[i];
    return NULL;
}

/* Apply env aliases for pack — MUST be called WITHOUT holding g_lang_mtx. */
static void apply_aliases(LangPack *pack) {
    if (!g_builtins_ready || !pack) return;
    for (size_t i = 0; i < pack->count; i++) {
        val_t val = env_lookup_or_false(GLOBAL_ENV, pack->mappings[i].canonical);
        if (!vis_false(val))
            env_define(GLOBAL_ENV, pack->mappings[i].foreign, val);
    }
}

/* ── Akkadian built-in pack ──────────────────────────────────────────────── */

void lang_register_akkadian(void) {
    /* Pass 1: count entries (2 per AKK entry: translit + cuneiform) */
    size_t n = 0;
#define AKK(e,t,c)  n += 2;
#define AKK_SF      AKK
#define AKK_PR      AKK
#include "akkadian_names.h"
    /* akkadian_names.h undefines AKK_SF and AKK_PR at its end */
#undef AKK

    /* Pass 2: fill mapping array */
    LangMapping *m = GC_MALLOC(n * sizeof(LangMapping));
    size_t idx = 0;
#define AKK(e,t,c) \
    m[idx].foreign = sym_intern_cstr(t); m[idx].canonical = sym_intern_cstr(e); \
    m[idx].description = NULL; idx++; \
    m[idx].foreign = sym_intern_cstr(c); m[idx].canonical = sym_intern_cstr(e); \
    m[idx].description = NULL; idx++;
#define AKK_SF AKK
#define AKK_PR AKK
#include "akkadian_names.h"
#undef AKK

    /* "𒀭 ḫiṭītu —" in UTF-8 */
    static const char akk_prefix[] =
        "\xf0\x92\x80\xad \xe1\xb8\xab"
        "i\xe1\xb9\xaditu \xe2\x80\x94";

    LangPack tmp;
    memset(&tmp, 0, sizeof(tmp));
    strncpy(tmp.id,           "akkadian",
            sizeof(tmp.id)-1);
    strncpy(tmp.display_name, "Akkadian / Cuneiform",
            sizeof(tmp.display_name)-1);
    strncpy(tmp.intro,
            "\xf0\x92\x80\xad Akkadian mode active \xe2\x80\x94 \xc5\xa1ulmu!",
            sizeof(tmp.intro)-1);
    strncpy(tmp.error_prefix, akk_prefix, sizeof(tmp.error_prefix)-1);
    tmp.mappings = m;
    tmp.count    = idx;
    tmp.cap      = idx;

    lang_register(&tmp);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void lang_init(void) {
    lang_register_akkadian();
    lang_set_active("akkadian");
}

void lang_apply_active_aliases(void *env_frame) {
    (void)env_frame;   /* we use GLOBAL_ENV directly */
    pthread_mutex_lock(&g_lang_mtx);
    g_builtins_ready = true;
    LangPack *active = active_load();
    pthread_mutex_unlock(&g_lang_mtx);
    apply_aliases(active);   /* env_define called outside the lock */
}

/* ── Registration ────────────────────────────────────────────────────────── */

int lang_register(const LangPack *pack) {
    if (!pack || !pack->id[0]) return -1;
    pthread_mutex_lock(&g_lang_mtx);
    LangPack *replaced_active = NULL;
    for (int i = 0; i < g_packs_count; i++) {
        if (strcmp(g_packs[i]->id, pack->id) == 0) {
            bool was_active = (active_load() == g_packs[i]);
            g_packs[i] = pack_alloc(pack);
            if (was_active) {
                active_store(g_packs[i]);
                replaced_active = g_packs[i];
            }
            pthread_mutex_unlock(&g_lang_mtx);
            if (replaced_active) apply_aliases(replaced_active);
            return 0;
        }
    }
    if (g_packs_count >= LANG_MAX_PACKS) {
        pthread_mutex_unlock(&g_lang_mtx);
        return -1;
    }
    g_packs[g_packs_count++] = pack_alloc(pack);
    pthread_mutex_unlock(&g_lang_mtx);
    return 0;
}

void lang_unregister(const char *id) {
    if (!id) return;
    pthread_mutex_lock(&g_lang_mtx);
    for (int i = 0; i < g_packs_count; i++) {
        if (strcmp(g_packs[i]->id, id) == 0) {
            if (active_load() == g_packs[i]) active_store(NULL);
            g_packs[i] = g_packs[--g_packs_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_lang_mtx);
}

/* ── Activation ──────────────────────────────────────────────────────────── */

bool lang_set_active(const char *id) {
    pthread_mutex_lock(&g_lang_mtx);
    if (!id) {
        active_store(NULL);
        pthread_mutex_unlock(&g_lang_mtx);
        return true;
    }
    LangPack *p = pack_find_locked(id);
    if (!p) { pthread_mutex_unlock(&g_lang_mtx); return false; }
    active_store(p);
    pthread_mutex_unlock(&g_lang_mtx);
    apply_aliases(p);   /* env_define outside the lock */
    return true;
}

const char *lang_active_id(void) {
    LangPack *p = active_load();
    return p ? p->id : NULL;
}

const char *lang_active_display_name(void) {
    LangPack *p = active_load();
    return p ? p->display_name : NULL;
}

const char *lang_active_intro(void) {
    LangPack *p = active_load();
    return (p && p->intro[0]) ? p->intro : NULL;
}

/* ── Translation ─────────────────────────────────────────────────────────── */

val_t lang_translate(val_t sym) {
    if (!vis_symbol(sym)) return sym;
    LangPack *p = active_load();
    if (!p) return sym;
    for (size_t i = 0; i < p->count; i++)
        if (p->mappings[i].foreign == sym) return p->mappings[i].canonical;
    return sym;
}

bool lang_is_known_sym(const char *utf8) {
    pthread_mutex_lock(&g_lang_mtx);
    for (int k = 0; k < g_packs_count; k++) {
        LangPack *p = g_packs[k];
        for (size_t i = 0; i < p->count; i++) {
            if (strcmp(sym_cstr(p->mappings[i].foreign), utf8) == 0) {
                pthread_mutex_unlock(&g_lang_mtx);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&g_lang_mtx);
    return false;
}

/* ── Error preamble ──────────────────────────────────────────────────────── */

void lang_preamble(char *buf, size_t bufsz, const char *errmsg) {
    LangPack *p = active_load();
    if (p && p->error_prefix[0]) {
        snprintf(buf, bufsz, "%s", p->error_prefix);
    } else {
        akkadian_preamble(buf, bufsz, errmsg);
    }
}

/* ── Introspection ───────────────────────────────────────────────────────── */

val_t lang_list_registered(void) {
    pthread_mutex_lock(&g_lang_mtx);
    val_t result = V_NIL;
    for (int i = g_packs_count - 1; i >= 0; i--)
        result = scm_cons(sym_intern_cstr(g_packs[i]->id), result);
    pthread_mutex_unlock(&g_lang_mtx);
    return result;
}

const LangPack *lang_find(const char *id) {
    if (!id) return NULL;
    pthread_mutex_lock(&g_lang_mtx);
    LangPack *p = pack_find_locked(id);
    pthread_mutex_unlock(&g_lang_mtx);
    return p;
}
