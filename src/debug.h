#ifndef CURRY_DEBUG_H
#define CURRY_DEBUG_H

/*
 * debug.h — interactive bytecode debugger.
 *
 * The VM checks vm_debug_active at every instruction dispatch (same
 * pattern as the minor-GC safepoint) and calls vm_debug_hook() when set.
 * The hook stops execution on breakpoints and stepping boundaries and
 * drives a gdb-style command loop on stdin: step/next/finish/continue,
 * backtrace, locals, expression printing.
 *
 * Main-thread only: the hook returns immediately on actor threads so a
 * breakpoint hit inside an actor never fights the REPL for stdin.
 */

#include <stdbool.h>
#include "value.h"
#include "vm.h"

/* Checked in the dispatch loop; true iff any breakpoint is enabled or a
 * stepping mode is armed.  Never write directly — use the functions below. */
extern volatile bool vm_debug_active;

/* Record the main thread.  Call once at startup, after vm_init(). */
void vm_debug_init(void);

/* Per-instruction hook; only called when vm_debug_active is set. */
void vm_debug_hook(CallFrame *frame);

/* Add a breakpoint from a spec string: a function name ("fib") or a
 * "file.scm:12" location.  Returns the breakpoint index, or -1 if the
 * table is full / the spec is empty. */
int vm_debug_break_add(const char *spec);

/* Remove breakpoint by index (as shown by vm_debug_break_list).
 * Returns false if no such breakpoint. */
bool vm_debug_break_remove(int idx);

/* Print the breakpoint table to stdout. */
void vm_debug_break_list(void);

/* Arm single-step mode: the next VM instruction dispatched on the main
 * thread stops in the debugger.  Backs the (breakpoint) builtin and the
 * ,debug REPL command. */
void vm_debug_request_step(void);

#endif /* CURRY_DEBUG_H */
