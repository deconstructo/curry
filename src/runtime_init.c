#include "runtime_init.h"
#include "gc.h"
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

/* Every embedder of the curry runtime (the `curry` CLI binary, the
 * Jupyter kernel) must call this exact sequence -- pulled out of main.c
 * so a second embedder can't silently drift out of order with the
 * first. */
void curry_runtime_init(void) {
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
}
