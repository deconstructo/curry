#ifndef CURRY_VERSION_INFO_H
#define CURRY_VERSION_INFO_H

#include "value.h"
#include <stdio.h>

/* SRFI-176: the association list returned by (version-alist). Each entry is
 * (property value ...). Needs the runtime initialised (symbols, builtins,
 * module search path). */
val_t curry_version_alist(void);

/* Writes the same list in LOSE form (one property per line, each line
 * starting with an opening parenthesis in column 1), as the -V flag prints. */
void curry_version_print_lose(FILE *out);

#endif
