#ifndef CURRY_RUNTIME_INIT_H
#define CURRY_RUNTIME_INIT_H

/* Runs every *_init() call in the exact order the runtime requires,
 * wrapped in gc_inhibit_minor()/gc_resume_minor() -- see the doc comment
 * on curry_runtime_init() in runtime_init.c for why the ordering and the
 * GC-inhibit bracket both matter. Shared by every executable that embeds
 * the curry runtime (the `curry` CLI/REPL binary, the Jupyter kernel) so
 * they can't drift out of sync with each other.
 */
void curry_runtime_init(void);

#endif
