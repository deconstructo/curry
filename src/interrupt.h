#ifndef CURRY_INTERRUPT_H
#define CURRY_INTERRUPT_H

/*
 * interrupt.h — cross-thread VM interrupt request.
 *
 * Unlike vm_debug_active (debug.h), which is documented main-thread-only
 * and checked/armed from the same thread, this flag is genuinely set from
 * one thread (e.g. a Jupyter kernel's control-channel handler, on its own
 * thread) and checked from another (whichever thread is running vm_run()
 * for the busy cell) -- hence the atomic store/load in interrupt.c rather
 * than a plain bool. The actual check happens in vm.c's dispatch loop,
 * the same per-instruction safepoint the minor-GC poll and the debugger
 * hook already use (see vm.c's L_DISPATCH). When pending, it raises an
 * EC_INTERRUPTED condition via scm_raise_code -- which unwinds through
 * the ordinary ExnHandler/SCM_PROTECT longjmp path, exactly like any
 * other Scheme exception, so a caller that already wraps vm_run() in
 * SCM_PROTECT (e.g. the Jupyter kernel's execute_request_impl, or a
 * future REPL ,interrupt command) needs no special-casing to catch it.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call from any thread to request that the VM currently running (if any)
 * stop at its next dispatch safepoint and raise EC_INTERRUPTED. A no-op
 * if nothing is running -- the flag is simply cleared at the next check,
 * whenever that happens, with no effect. */
void vm_interrupt_request(void);

/* Cheap fast-path check (relaxed load, no RMW) for the dispatch loop's
 * hot path -- same cost profile as the existing gc_gen_safepoint() call
 * at every instruction dispatch. Internal to vm.c. */
bool vm_interrupt_pending(void);

/* Slower, clears the flag: only called from the rare branch where
 * vm_interrupt_pending() was true. Internal to vm.c. */
bool vm_interrupt_pending_and_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* CURRY_INTERRUPT_H */
