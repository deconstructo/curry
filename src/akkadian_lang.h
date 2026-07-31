/*
 * akkadian_lang.h — Akkadian's contribution to the multi-language name
 * registry (see lang_registry.h). Builds a LangPack from akkadian_names.h
 * and registers it via lang_register_pack().
 */

#ifndef CURRY_AKKADIAN_LANG_H
#define CURRY_AKKADIAN_LANG_H

/* Called once, from lang_registry_init(). */
void akkadian_lang_setup(void);

#endif /* CURRY_AKKADIAN_LANG_H */
