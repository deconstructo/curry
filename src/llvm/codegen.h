#pragma once
/*
 * Curry → LLVM IR code generator.
 *
 * Takes a Scheme lambda (from val_t AST) and emits an LLVM Module
 * containing a native function that implements it.
 *
 * Value representation in LLVM IR
 * ---------------------------------
 * Scheme val_t (uint64_t) is mapped to  i64  in LLVM IR.  Heap object
 * pointers are additionally available as  ptr  (opaque pointer, LLVM 15+).
 *
 * Every emitted function has:
 *   attribute gc "curry-generational"  — triggers statepoint stack maps
 *   attribute nounwind                 — Scheme unwind uses longjmp, not C++
 *
 * Statepoints
 * -----------
 * Every call to a GC-allocating helper (gc_alloc, curry_jit_apply_arr, …) is
 * wrapped in an llvm.experimental.gc.statepoint intrinsic call sequence so
 * the GC can relocate heap objects across call boundaries.  The relocation
 * projections (llvm.experimental.gc.relocate) are inserted immediately after
 * each statepoint; IR variables live across the call must use the relocated
 * copies, never the originals.
 *
 * This design was chosen from day 1 — retrofitting statepoints into a
 * working backend is prohibitive (every call site must be revisited).
 */

#ifndef CURRY_CODEGEN_H
#define CURRY_CODEGEN_H

#include "../value.h"
#include "../object.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>

namespace curry {

/* Opaque handle for a compilation unit in progress. */
struct CompileCtx;

/*
 * Compile one Scheme thunk (lambda of 0 args) to a fresh LLVM Module.
 *
 * Parameters:
 *   llvm_ctx  — shared LLVM context owned by the JIT session.
 *   thunk     — a T_CLOSURE with an S-expression body.
 *   name      — used as the LLVM function name and for diagnostics.
 *
 * Returns an owning Module ready to hand to LLJIT::addIRModule().
 * Throws std::runtime_error on unsupported AST nodes.
 */
std::unique_ptr<llvm::Module> codegen_thunk(
    llvm::LLVMContext &llvm_ctx,
    val_t              thunk,
    const std::string &name = "curry_thunk");

/*
 * Compile a raw S-expression body (list of forms) directly to LLVM IR,
 * bypassing the bytecode compiler.  This is the primary entry point for
 * tiered JIT and (curry-jit-eval 'expr).
 *
 * The expression is compiled in the global environment; free variables are
 * looked up at runtime via curry_jit_global_lookup.
 */
std::unique_ptr<llvm::Module> codegen_expr(
    llvm::LLVMContext &llvm_ctx,
    val_t              expr,
    const std::string &name = "curry_expr");

/*
 * Compile a top-level define'd procedure for ahead-of-time or eager JIT.
 * The procedure is stored in the module's global symbol table under `name`
 * with C linkage so the JIT can look it up by name.
 */
std::unique_ptr<llvm::Module> codegen_procedure(
    llvm::LLVMContext &llvm_ctx,
    val_t              proc,
    const std::string &name);

/*
 * Compile a list of top-level S-expressions (as parsed from a .scm file)
 * into a single LLVM Module.  Each top-level form becomes one basic block
 * inside a synthetic `_curry_toplevel` function.
 */
std::unique_ptr<llvm::Module> codegen_file(
    llvm::LLVMContext &llvm_ctx,
    val_t              forms,        /* list of top-level S-expressions */
    const std::string &module_name = "curry_module");

/* Emit module as LLVM IR text (.ll) to path.  Returns false on I/O error. */
bool codegen_emit_ir(llvm::Module &M, const std::string &path);

/* Emit module as LLVM bitcode (.bc) to path.  Returns false on I/O error. */
bool codegen_emit_bc(llvm::Module &M, const std::string &path);

} /* namespace curry */

#endif /* CURRY_CODEGEN_H */
