/*
 * akkadian_eval.h — Special-form name translation, plus a procedure-name
 * -> (translit, cuneiform) lookup used to alias imported module procedures
 * at import time.
 *
 * Call akk_eval_setup() exactly once, from eval_init() — the tables below
 * are real (non-static) globals defined once in akkadian_eval.c, so every
 * translation unit that includes this header shares the same populated
 * state. (An earlier version of this header made these `static`, giving
 * each including .c file its own private, independently-uninitialized
 * copy; modules.c silently got an empty table this way — see the fix in
 * the commit that introduced akk_pr_lookup.)
 *
 * akk_translate(op): call on the head of every form before the special-form
 * dispatch chain. Akkadian/cuneiform synonyms are remapped to their
 * canonical English symbols; everything else is returned unchanged. Built
 * purely from AKK_SF entries and independent of any environment — safe to
 * consult regardless of what has or hasn't been imported yet.
 *
 * akk_pr_lookup(eng, &translit, &cuneiform): the AKK_PR analogue. Builtins
 * registered directly in builtins_register() get their aliases installed
 * once at startup (see builtins.c's own AKK_PR loop, which must run last in
 * that function — see the comment there). Procedures that only come to
 * exist inside a module's own environment (every optional/lazy-loaded C
 * module, and every Scheme (curry ...) library) are never seen by that
 * startup loop, since they're not bound anywhere until something imports
 * them. modules_import() (src/modules.c) calls this lookup for each name it
 * copies out of the imported module's environment, so a module's Akkadian
 * aliases become available at the point of import instead of never.
 */

#ifndef CURRY_AKKADIAN_EVAL_H
#define CURRY_AKKADIAN_EVAL_H

#include <stdbool.h>
#include "symbol.h"
#include "value.h"

void akk_eval_setup(void);
val_t akk_translate(val_t sym);
bool akk_pr_lookup(val_t english, val_t *translit, val_t *cuneiform);

#endif /* CURRY_AKKADIAN_EVAL_H */
