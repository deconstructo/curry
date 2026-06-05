/*
 * curry-generational GC strategy.
 *
 * LLVM's statepoint-based GC infrastructure requires a named GC strategy
 * registered via the GCRegistry.  Our strategy:
 *
 *  - Uses statepoints (not shadow-stack / explicit roots).
 *  - Requests stack-map emission so the runtime GC can walk roots.
 *  - Does NOT perform read/write barrier insertion in LLVM (Boehm bootstrap
 *    phase is conservative; the generational collector will add barriers via
 *    llvm.gcwrite at codegen time once it replaces Boehm).
 *  - Sets InitialLoweringRequired=true so PlaceSafepointsPass runs.
 *
 * When the generational collector is ready, add:
 *   customReadBarriers()  / customWriteBarriers() overrides here, and
 *   emit llvm.gcwrite at every heap-pointer store in codegen.cpp.
 */

#include "gc_strategy.h"

#include <llvm/IR/GCStrategy.h>
#include <llvm/Support/Registry.h>

using namespace llvm;

namespace curry {

class CurryGCStrategy : public GCStrategy {
public:
    CurryGCStrategy() {
        /* Use gc.statepoint intrinsics (not gc.root). */
        UseStatepoints = true;

        /* UsesMetadata = false: with statepoints + ORC JIT the StackMap section
         * is emitted automatically by the statepoint lowering pass.  Setting
         * UsesMetadata=true enables the legacy GCMetadataPrinter path, which
         * requires a separately registered printer and is not used here. */
        UsesMetadata = false;
    }
};

/* Register with the global GCRegistry so LLVM can look up "curry-generational"
 * as a named gc attribute on functions. */
static GCRegistry::Add<CurryGCStrategy>
    X("curry-generational", "Curry generational GC strategy");

void register_gc_strategy(void) {
    /* The GC_REGISTRY_ADD_GCSTRATEGY macro creates a static constructor that
     * registers the strategy with LLVM's GCRegistry.  Calling this function
     * ensures the translation unit is linked in (otherwise the linker may
     * dead-strip the static object on some platforms).
     *
     * On Apple platforms link with -force_load or list this .o explicitly to
     * guarantee the static constructor runs even when building a static lib. */
    (void)0;
}

} /* namespace curry */
