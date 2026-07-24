/*
 * Curry LLVM backend — public C interface.
 *
 * The backend is an optional compile target (BUILD_LLVM=ON).  When present,
 * hot BcClosure objects are JIT-compiled to native code via LLVM ORC v2
 * and run in place of the bytecode VM.  The bytecode VM is always available
 * as a fallback.
 *
 * Architecture overview
 * ---------------------
 * Source AST (tree-walker Closure)
 *   │
 *   ▼ curry::codegen_procedure()
 * LLVM IR Module  — opaque-pointer mode, gc "curry-generational" on every fn
 *   │
 *   ▼ LLJIT::addIRModule()
 * Native code  — called directly; GC stack maps emitted by LLVM
 *
 * GC invariants (MUST be maintained from day 1, cannot be retrofitted):
 *   - Every function bears  gc "curry-generational"  in its LLVM IR.
 *   - Every call site that can trigger GC is a gc.statepoint intrinsic.
 *   - Object references are  ptr  (opaque, LLVM 15+); no  i64*  casts.
 *   - Write barriers use  llvm.gcwrite  at every heap-to-heap pointer store.
 *
 * Threading: one LLJIT instance per thread (thread-local singleton).
 */

#ifndef CURRY_LLVM_H
#define CURRY_LLVM_H

#include "../value.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Initialisation ---- */

/* Initialise LLVM targets, JIT session, and register the curry-generational
 * GC strategy.  Call once after gc_init() and before any compile request. */
void curry_llvm_init(void);

/* Tear down — called at process exit. */
void curry_llvm_fini(void);

/* ---- Top-level entry points ---- */

/* JIT-compile and immediately call thunk (a Scheme lambda of 0 args).
 * Falls back to apply_arr if compilation fails.
 * Returns the Scheme value produced by the thunk. */
val_t curry_jit_call(val_t thunk);

/* JIT-compile and immediately evaluate a raw S-expression (bypasses the
 * bytecode compiler).  expr is a single Scheme form.
 * Falls back to scheme_eval() if LLVM compilation fails. */
val_t curry_jit_eval_expr(val_t expr);

/* ---- Query ---- */

/* True when LLVM backend is compiled in and initialised. */
bool curry_llvm_available(void);

/* Dump the LLVM IR for the most recently compiled module to stderr. */
void curry_llvm_dump_last(void);

/* Tiered JIT: compile a (lambda params body...) AST to a T_JITCLOSURE.
 * Returns V_VOID (and is silent) if JIT is unavailable or compilation fails.
 * Closures with captures are wrapped in a (let ...) that injects the current
 * upvalue values — see jit_wrap_upvals() in runtime.c. */
val_t curry_llvm_jit_compile(val_t src_lambda);

/*
 * Compile a list of top-level S-expressions (parsed from a .scm file) and
 * write the resulting LLVM IR to `out_path`.
 *
 * fmt: 'l' → .ll (human-readable IR text)
 *      'b' → .bc (LLVM bitcode)
 *
 * Returns true on success.
 */
bool curry_emit_llvm(val_t forms, const char *out_path, char fmt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CURRY_LLVM_H */
