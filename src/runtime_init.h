#ifndef CURRY_RUNTIME_INIT_H
#define CURRY_RUNTIME_INIT_H

/* Runs every *_init() call in the exact order the runtime requires --
 * see the doc comment on curry_runtime_init() in runtime_init.c. Shared
 * by every executable that embeds the curry runtime (the `curry` CLI/REPL
 * binary, the Jupyter kernel) so they can't drift out of sync with each
 * other.
 */
void curry_runtime_init(void);

#endif
