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

/* Compile a list of top-level expressions as a script chunk.
   Returns a zero-argument Closure whose body executes all forms
   left-to-right and returns the value of the last one. */
val_t compiler_compile_script(val_t expr_list);

#endif /* CURRY_COMPILER_H */
