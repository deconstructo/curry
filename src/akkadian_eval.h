/*
 * akkadian_eval.h — Backward-compat shim for the pluggable i18n system.
 *
 * eval.c and compiler.c call akk_translate() / akk_eval_setup() via this
 * header.  Both now delegate to lang_translate() / lang_init() in i18n.c,
 * so the call sites do not need to change.
 */

#ifndef CURRY_AKKADIAN_EVAL_H
#define CURRY_AKKADIAN_EVAL_H

#include "i18n.h"
#include "value.h"

static inline void    akk_eval_setup(void)    { lang_init(); }
static inline val_t   akk_translate(val_t sym) { return lang_translate(sym); }

#endif /* CURRY_AKKADIAN_EVAL_H */
