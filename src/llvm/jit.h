#pragma once
/*
 * Curry ORC JIT v2 session.
 *
 * Wraps llvm::orc::LLJIT.  One instance per process (singleton).
 *
 * Design notes
 * ------------
 * LLJIT provides:
 *   - A single ExecutionSession with a main JITDylib
 *   - IRTransformLayer for optimisation passes
 *   - Object-code layer backed by llvm::orc::RTDyldObjectLinkingLayer
 *   - Symbol lookup with lazy compilation support
 *
 * We add:
 *   - A "curry-rt" JITDylib containing all C runtime symbols (apply_arr,
 *     gc_alloc, all builtins) so compiled code can call them directly.
 *   - A stack-map reader installed in the ObjectLinkingLayer's notifyLoaded
 *     callback so GC stack maps are registered with the collector.
 *
 * Thread safety: LLJIT is designed to be used from multiple threads
 * simultaneously.  Curry acquires no extra locks around JIT calls.
 */

#ifndef CURRY_JIT_H
#define CURRY_JIT_H

#include "curry_llvm.h"    /* val_t via ../value.h */

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>

#include <memory>
#include <string>

namespace curry {

class JITSession {
public:
    /* Create and initialise the LLJIT instance.  Registers the curry-rt dylib
     * with all exported C runtime symbols.  Throws on failure. */
    static JITSession &instance();

    /* Compile an LLVM Module and add it to the main JITDylib.
     * The module's data layout is set to match the JIT target before adding.
     * Thread-safe. */
    void add_module(std::unique_ptr<llvm::Module> M,
                    std::unique_ptr<llvm::LLVMContext> ctx);

    /* Look up a symbol by name.  Returns the address or throws. */
    uint64_t lookup(const std::string &name);

    /* Shared LLVMContext.  One context per module is safer for MT; use
     * make_context() to get a fresh one per compilation unit. */
    static std::unique_ptr<llvm::LLVMContext> make_context();

    ~JITSession();

private:
    JITSession();
    void register_runtime_symbols();

    std::unique_ptr<llvm::orc::LLJIT> lljit_;
};

} /* namespace curry */

#endif /* CURRY_JIT_H */
