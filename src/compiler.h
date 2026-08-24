#ifndef CURRY_COMPILER_H
#define CURRY_COMPILER_H

#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "ir.h"

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

/* Tier 2.3 local-inliner positive-firing check: compiler_ir_optimize_check
 * above already differentially verifies inlining's correctness for free
 * (its decision logic lives inside ir_emit, not ir_optimize), but a pure
 * result-comparison check can pass even with inlining silently disabled.
 * This proves the feature actually fired for `expr`: true iff the live-IR
 * compile produced strictly MORE bytecode than classic compilation, the
 * signature of a candidate's body being duplicated at its call site(s)
 * instead of compactly loaded-and-called. See its own comment in
 * compiler.c for the full contract. */
bool compiler_ir_inline_fired_check(val_t expr);

/* Tier 2.6 step 1 (docs/thoughts/performance-chez-kaappi.md §5, item 2.6):
 * exposes ir_lower()+ir_optimize() as a public entry point, real
 * groundwork toward eventually pointing src/llvm/codegen.cpp at the IR
 * instead of raw S-expressions -- but NOT, on its own, a tree codegen.cpp
 * (or anything else) can walk standalone yet. Read this contract
 * carefully before building on it:
 *
 * The IR is LAZILY lowered by design (see ir.h's own comments on
 * IR_VAR_REF and IR_LAMBDA's `body` field) -- ir_lower() alone does NOT
 * recursively lower an entire program in one pass the way a "the IR" of
 * a from-scratch compiler normally would:
 *   - IR_VAR_REF nodes carry only a raw symbol; resolving it to a local
 *     slot / upvalue index / global happens at ir_emit() time, against
 *     whichever Compiler is actively walking that node then -- there is
 *     no standalone, Compiler-independent "resolved" form.
 *   - IR_LAMBDA's body is deliberately left as a raw, un-lowered val_t
 *     list; ir_emit() lowers each body form immediately before emitting
 *     it (interleaved, one statement at a time), not upfront.
 * So the tree returned here is real IR, but every nested lambda body
 * inside it is still raw S-expression, and every variable reference is
 * still an unresolved symbol -- exactly the same lazy contract ir_emit()
 * itself has always worked under. A future codegen.cpp rewrite needs its
 * OWN resolution/nested-lowering machinery (mirroring resolve_local/
 * resolve_upvalue's role), not just a call to this function, to get a
 * tree it can walk standalone. That machinery is the real remaining
 * scope of Tier 2.6's IR-retargeting -- deliberately not attempted here.
 *
 * `expr` is lowered (and optimized) against a fresh, throwaway root
 * Compiler with no enclosing scope -- appropriate for a JIT-eligible
 * chunk's own top-level src_lambda (already isolated: JIT promotion is
 * refused for closures with unresolved free-variable upvalues that
 * aren't first baked in as constants -- see maybe_jit_bcc's own comment,
 * runtime.c), not for an arbitrary nested subexpression that depends on
 * an enclosing scope's locals.
 *
 * Returns the optimized tree; `*out_arena` receives the IRArena it was
 * allocated from (transferring ownership -- the caller must
 * ir_arena_free() it once done reading the tree, exactly the same
 * lifetime contract every other IRArena in this codebase already has,
 * see ir.h's own header comment). */
IRNode *compiler_ir_lower_for_jit(val_t expr, IRArena **out_arena);

#endif /* CURRY_COMPILER_H */
