/*
 * i18n.h — Pluggable language pack system for Curry Scheme.
 *
 * A language pack maps foreign-language names (special forms and procedures)
 * to their English canonical equivalents, and optionally supplies a localised
 * error preamble.  Packs are registered at runtime and one pack is active at
 * a time.
 *
 * Akkadian/cuneiform is the built-in pack, registered and activated by
 * lang_init() and therefore always available.  Additional packs can be
 * registered from Scheme via (register-language! spec) and activated with
 * (set-active-language! id).
 *
 * Thread safety: registration and activation are mutex-protected.
 * lang_translate() is lock-free once a pack is active (single pointer read).
 */

#ifndef CURRY_I18N_H
#define CURRY_I18N_H

#include <stddef.h>
#include <stdbool.h>
#include "value.h"

/* ── Data structures ─────────────────────────────────────────────────────── */

typedef struct {
    val_t       foreign;     /* interned symbol — the translated name */
    val_t       canonical;   /* interned symbol — English canonical */
    char       *description; /* optional cultural/conceptual note (UTF-8, heap) */
} LangMapping;

typedef struct {
    char        id[64];           /* e.g. "warlpiri" */
    char        display_name[128];/* e.g. "Warlpiri (Yapa)" */
    char        intro[512];       /* shown on activation */
    char        error_prefix[256];/* prefix for error messages, or empty */
    LangMapping *mappings;
    size_t      count;
    size_t      cap;
} LangPack;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/* Called from eval_init() after sym_init(). Registers the built-in Akkadian
 * pack and activates it so backward compatibility is preserved. */
void lang_init(void);

/* Called from builtins_register() after all procedures are defined.
 * Registers env aliases for the currently-active pack's procedure mappings. */
void lang_apply_active_aliases(void *env_frame);

/* ── Registration ────────────────────────────────────────────────────────── */

/* Register a new pack.  Copies the struct and all strings/mappings.
 * Idempotent on pack->id: re-registration replaces the existing pack.
 * Returns 0 on success, -1 on allocation failure. */
int lang_register(const LangPack *pack);

/* Unregister a pack by id.  If it was active, the active language becomes
 * NULL (English / no translation). */
void lang_unregister(const char *id);

/* Register the built-in Akkadian pack from akkadian_names.h data.
 * Called internally by lang_init(); exposed so tests can call it too. */
void lang_register_akkadian(void);

/* ── Activation ──────────────────────────────────────────────────────────── */

/* Activate a registered pack by id.  Pass NULL to disable translation
 * (English-only mode).  Returns false if id is not found.
 * After activation, env aliases for procedure mappings are registered via
 * lang_apply_active_aliases if builtins are already initialised. */
bool lang_set_active(const char *id);

/* Returns the id of the active pack, or NULL if none is active. */
const char *lang_active_id(void);

/* Returns the display name of the active pack, or NULL. */
const char *lang_active_display_name(void);

/* Returns the intro string of the active pack, or NULL. */
const char *lang_active_intro(void);

/* ── Translation ─────────────────────────────────────────────────────────── */

/* Map a symbol through the active pack's SF/PR table.
 * Returns the canonical English symbol, or sym unchanged if not found.
 * Fast path: returns sym immediately if no pack is active or sym is not a
 * symbol. */
val_t lang_translate(val_t sym);

/* True if the UTF-8 string matches any foreign name in ANY registered pack.
 * Used by reader.c to decide whether a cuneiform token is a symbol (not a
 * sexagesimal number). */
bool lang_is_known_sym(const char *utf8);

/* ── Error preamble ──────────────────────────────────────────────────────── */

/* Format a localised error preamble into buf.  Uses the active pack's
 * error_prefix if set, otherwise the Akkadian default (𒀭 ḫiṭītu). */
void lang_preamble(char *buf, size_t bufsz, const char *errmsg);

/* ── Introspection ───────────────────────────────────────────────────────── */

/* Return a Scheme list of id strings for all registered packs. */
val_t lang_list_registered(void);

/* Return the LangPack for a given id, or NULL. */
const LangPack *lang_find(const char *id);

#endif /* CURRY_I18N_H */
