#include "runtime_init.h"
#include "gc.h"
#include "gc_gen.h"
#ifdef BUILD_LLVM
#  include "llvm/curry_llvm.h"
#endif
#include "symbol.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "eval.h"
#include "actors.h"
#include "stm.h"
#include "channel.h"
#include "condition.h"
#ifdef BUILD_FFI
#  include "curry_ffi.h"
#endif
#include "sx_rules.h"
#include "sx_algebra.h"
#include "modules.h"
#include "profiling.h"
#include "vm.h"
#include "debug.h"

/* Issue #215: every one of these *_init() functions is plain C code
 * that builds up val_t structures (module registries, alias lists,
 * etc. -- e.g. modules_init()'s scm_cons() calls building up the
 * (rnrs X)/(rnrs X Y) name lists) directly, not via apply_arr() (which
 * brackets every ordinary Scheme-invoked primitive call in
 * gc_inhibit_minor()/gc_resume_minor() already) and not via eval()'s
 * tree-walker (which shadow-stacks its own C locals instead). Under
 * --gc generational with a small enough nursery, a minor GC firing
 * mid-construction here -- e.g. between allocating a pair and using
 * its address in the next nested scm_cons() call -- moves an object
 * whose new location no caller here is prepared to notice, corrupting
 * state (confirmed: reliably crashes with "unknown GC:MOVE type" at
 * --gc-nursery-size 1K, deterministically, before any user script
 * code runs at all -- `curry --gc generational --gc-nursery-size 1K
 * -e '(display 1)'` alone reproduces it). Bracketing the whole
 * one-time startup sequence in gc_inhibit_minor()/gc_resume_minor()
 * closes this the same way apply_arr() already does for ordinary
 * primitive calls -- see gc.h's own doc comment on this pair of
 * functions for the general rule this follows. Negligible cost:
 * this runs once per process, falling back to Boehm-only allocation
 * for the (small, bounded) startup working set instead of the
 * nursery, not a hot path.
 *
 * Every embedder of the curry runtime (the `curry` CLI binary, the
 * Jupyter kernel) must call this exact sequence -- pulled out of
 * main.c so a second embedder can't silently drift out of order with
 * the first and reintroduce the crash above. */
void curry_runtime_init(void) {
    gc_inhibit_minor();
    gc_init();
    sym_init();
    num_init();
    port_init();
    env_init();
    eval_init();
    sx_rules_init();
    sx_algebra_init();
    actors_init();
    stm_init();
    channel_init();
    condition_init();
#ifdef BUILD_FFI
    ffi_init();
#endif
    modules_init();
    profiling_init(GLOBAL_ENV);
    vm_init();
    vm_debug_init();
#ifdef BUILD_LLVM
    curry_llvm_init();
#endif
    gc_resume_minor();
}
