#ifndef CURRY_COMPILER_H
#define CURRY_COMPILER_H

#include "value.h"
#include "chunk.h"
#include "vm.h"

/*
 * Compiler — AST (val_t) → bytecode (Chunk / Closure)
 *
 * The reader produces Scheme ASTs as standard cons-cell lists (val_t).
 * The compiler walks them and emits bytecode into a Chunk.
 *
 * Globals are resolved at runtime against the existing GLOBAL_ENV so
 * the VM and the tree-walker share one namespace during migration.
 */

/* Compile a single top-level expression; returns a Closure (val_t). */
val_t compiler_compile(val_t expr);

/* Set the source name (file path, "<repl>", "<expr>", ...) stamped onto
   every chunk compiled from this point on, for use in backtraces. The
   string must outlive any chunk compiled while it is set (pass a literal
   or a buffer with process lifetime, e.g. argv[]). */
void compiler_set_source_name(const char *name);

/* Set the target environment stamped onto every chunk compiled from this
   point on -- which environment that chunk's OP_LOAD_GLOBAL/STORE_GLOBAL/
   DEF_GLOBAL operate against. Pass V_VOID (the default at process start,
   and after compiler_clear_target_env()) for the ordinary "use GLOBAL_ENV"
   behavior every chunk had before this existed; pass a define-library
   body's own env_new_root() frame to compile that library's top-level
   forms against its isolated environment instead. Thread-local, like
   compiler_set_source_name -- must be reset (or explicitly re-set) before
   the next unrelated compile, since it otherwise stays in effect. */
void compiler_set_target_env(val_t env);
void compiler_clear_target_env(void);

/* Compile a list of top-level expressions as a script chunk.
   Returns a zero-argument Closure whose body executes all forms
   left-to-right and returns the value of the last one. */
val_t compiler_compile_script(val_t expr_list);

/* Tier 2.1 IR differential self-check (docs/thoughts/
 * performance-chez-kaappi.md §5, src/ir.h): compiles `expr` twice, once
 * via compile_classic() (compile()'s own original, pre-IR dispatch,
 * forced classic-all-the-way-down via a thread-local flag -- see its own
 * comment in compiler.c for why a plain call to compile() no longer
 * suffices now that compile() itself routes through ir_lower/ir_emit)
 * and once via the standalone ir_lower()+ir_emit() path, and returns
 * true iff the resulting bytecode is byte-for-byte identical. Prints a
 * disassembly of both sides to stderr on mismatch. Neither compile run
 * has any side effect outside its own scratch Compiler/Chunk (does not
 * touch GLOBAL_ENV, does not produce a runnable closure) -- this is a
 * verification tool, not part of any live compile path itself, though
 * compile() (the actual live path) now shares the same ir_lower/
 * ir_optimize/ir_emit machinery this function verifies. */
bool compiler_ir_self_check(val_t expr);

/* Tier 2.2 optimizer check: compiles `expr` twice -- once via
 * compile_classic() (unoptimized, genuinely IR-free -- see
 * compiler_ir_self_check's own comment for why compile() itself no
 * longer serves this role), once via ir_lower()+ir_optimize()+
 * ir_emit() (dead-branch elimination on IR_IF, boolean simplification on
 * IR_AND/IR_OR) -- actually RUNS both resulting closures and compares
 * their RESULTS with scm_equal, not their bytecode. Unlike
 * compiler_ir_self_check, byte-identical comparison doesn't apply here:
 * the whole point of ir_optimize is to produce DIFFERENT (shorter)
 * bytecode for inputs where it can prove something, so `expr` must be a
 * self-contained expression safe to actually evaluate (no free/unbound
 * variables, no observable side effects a second run would double up)
 * -- this is a stricter contract than compiler_ir_self_check's, which
 * never executes anything. */
bool compiler_ir_optimize_check(val_t expr);

#endif /* CURRY_COMPILER_H */
