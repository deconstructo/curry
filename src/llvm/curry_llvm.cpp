/*
 * curry_llvm.cpp — C-callable entry points for the LLVM backend.
 *
 * These functions implement the interface declared in curry_llvm.h and are
 * called from builtins.c / eval.c / vm.c (C code), so they have C linkage.
 */

#include "curry_llvm.h"
#include "jit.h"
#include "codegen.h"

extern "C" {
#include "../value.h"
#include "../eval.h"
#include "../vm.h"
}

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <stdexcept>
#include <atomic>
#include <mutex>
#include <string>

static std::atomic<bool> g_initialised{false};

/* Serializes add_module + lookup on the LLVM JIT session singleton.
 * The JIT session is not thread-safe for concurrent modifications; all
 * actor threads can race here when a hot closure hits the JIT threshold. */
static std::mutex g_jit_mutex;

/* Last compiled module IR for debugging. */
static std::string g_last_ir;

extern "C" {

void curry_llvm_init(void) {
    if (g_initialised.exchange(true)) return;
    try {
        curry::JITSession::instance(); /* triggers init */
    } catch (const std::exception &e) {
        fprintf(stderr, "[curry-llvm] init failed: %s\n", e.what());
        g_initialised.store(false);
    }
}

void curry_llvm_fini(void) {
    /* LLJIT is destroyed by the singleton dtor. */
}

bool curry_llvm_available(void) {
    return g_initialised.load();
}

val_t curry_jit_call(val_t thunk) {
    if (!g_initialised.load())
        return apply_arr(thunk, 0, nullptr);

    static std::atomic<uint64_t> seq{0};
    std::string fn_name = "curry_thunk_" + std::to_string(seq.fetch_add(1));

    try {
        auto ctx = curry::JITSession::make_context();
        auto *ctx_ptr = ctx.get();

        auto M = curry::codegen_thunk(*ctx_ptr, thunk, fn_name);

        {
            llvm::raw_string_ostream s(g_last_ir);
            M->print(s, nullptr);
        }

        curry::JITSession::instance().add_module(std::move(M), std::move(ctx));

        auto addr = curry::JITSession::instance().lookup(fn_name);
        auto *fn = reinterpret_cast<val_t (*)()>(addr);
        return fn();

    } catch (const std::exception &e) {
        fprintf(stderr, "[curry-llvm] JIT failed (%s), falling back: %s\n",
                fn_name.c_str(), e.what());
        return apply_arr(thunk, 0, nullptr);
    }
}

val_t curry_jit_eval_expr(val_t expr) {
    /* Compile-to-bytecode-and-interpret (vm_eval) rather than tree-walk
     * (eval()) for both the "JIT unavailable" and "JIT compile failed"
     * paths -- part of the eval-elimination migration (see
     * docs/thoughts/eval-elimination-migration-plan-2026-07-23.md's
     * "Other eval() callers" section and the project memory). A JIT
     * compile failure by definition already has `expr` in hand as a
     * plain, uncompiled Scheme form (this function's own argument, not
     * bytecode) -- vm_eval compiles it fresh against GLOBAL_ENV, exactly
     * as eval() would have tree-walked it, just through the VM instead. */
    if (!g_initialised.load())
        return vm_eval(expr, GLOBAL_ENV);

    static std::atomic<uint64_t> seq{0};
    std::string fn_name = "curry_expr_" + std::to_string(seq.fetch_add(1));

    try {
        auto ctx = curry::JITSession::make_context();
        auto *ctx_ptr = ctx.get();

        auto M = curry::codegen_expr(*ctx_ptr, expr, fn_name);

        {
            llvm::raw_string_ostream s(g_last_ir);
            M->print(s, nullptr);
        }

        curry::JITSession::instance().add_module(std::move(M), std::move(ctx));

        auto addr = curry::JITSession::instance().lookup(fn_name);
        auto *fn = reinterpret_cast<val_t (*)()>(addr);
        return fn();

    } catch (const std::exception &e) {
        fprintf(stderr, "[curry-llvm] JIT failed (%s), falling back: %s\n",
                fn_name.c_str(), e.what());
        return vm_eval(expr, GLOBAL_ENV);
    }
}

void curry_llvm_dump_last(void) {
    fprintf(stderr, "%s\n", g_last_ir.c_str());
}

val_t curry_llvm_jit_compile(val_t src_lambda) {
    if (!g_initialised.load()) return V_VOID;

    static std::atomic<uint64_t> seq{0};
    std::string fn_name = "curry_jit_" + std::to_string(seq.fetch_add(1));

    try {
        auto ctx = curry::JITSession::make_context();
        auto *ctx_ptr = ctx.get();
        /* IR generation uses a fresh LLVMContext per call — can run concurrently. */
        auto M = curry::codegen_expr(*ctx_ptr, src_lambda, fn_name);

        /* add_module + lookup on the shared JIT session must be serialized. */
        uint64_t addr;
        {
            std::lock_guard<std::mutex> lock(g_jit_mutex);
            curry::JITSession::instance().add_module(std::move(M), std::move(ctx));
            addr = curry::JITSession::instance().lookup(fn_name);
        }
        /* Call the compiled thunk outside the lock: it may trigger more JIT. */
        auto *fn = reinterpret_cast<val_t (*)()>(addr);
        val_t result = fn();
        return vis_jitclosure(result) ? result : V_VOID;
    } catch (...) {
        return V_VOID;  /* silent: keep using bytecode */
    }
}

bool curry_emit_llvm(val_t forms, const char *out_path, char fmt) {
    auto ctx = curry::JITSession::make_context();
    try {
        auto M = curry::codegen_file(*ctx, forms,
                                     out_path ? out_path : "curry_module");
        return (fmt == 'b') ? curry::codegen_emit_bc(*M, out_path)
                            : curry::codegen_emit_ir(*M, out_path);
    } catch (const std::exception &e) {
        fprintf(stderr, "[curry-llvm] emit failed: %s\n", e.what());
        return false;
    }
}

} /* extern "C" */
