#include "interrupt.h"
#include <stdatomic.h>

static _Atomic bool vm_interrupt_flag = false;

void vm_interrupt_request(void) {
    atomic_store_explicit(&vm_interrupt_flag, true, memory_order_release);
}

bool vm_interrupt_pending(void) {
    /* Cheap fast-path check for the dispatch loop's hot path (called on
     * every single instruction, same as gc_gen_safepoint() already is --
     * see vm.c's L_DISPATCH) -- a plain relaxed load, no read-modify-
     * write. Only the rare true case pays for the heavier exchange
     * below. */
    return atomic_load_explicit(&vm_interrupt_flag, memory_order_relaxed);
}

bool vm_interrupt_pending_and_clear(void) {
    /* exchange, not a load-then-store: if two threads somehow both call
     * vm_interrupt_request() before the flag is next checked, that's
     * still exactly one interrupt observed here (the intended
     * coalescing behavior -- an interrupt request is "please stop",
     * not a counted signal), and there's no race window between a
     * separate load and a separate store where a fresh request could
     * be silently dropped by this call's own clear. */
    return atomic_exchange_explicit(&vm_interrupt_flag, false, memory_order_acquire);
}
