#ifndef CURRY_PIC_H
#define CURRY_PIC_H

#include "value.h"

/*
 * PIC — polymorphic inline cache primitives backing (curry oop)'s Layer 2
 * (lib/curry/modules/curry/oop.scm), a small C-implemented fast path for
 * generic-function dispatch.
 *
 * Deliberately NOT a new bytecode opcode or a compiler.c change: generic
 * functions are a user-level Scheme construct (an ordinary closure over a
 * method-table vector — see oop.scm's %make-generic), so the compiler has
 * no reliable way to recognize "this call site's callee happens to be a
 * generic function" at compile time. Instead this is a pair of ordinary
 * builtins that oop.scm's dispatch code calls directly: %invoke-generic
 * tries %%pic-lookup before falling back to its existing filter+sort
 * dispatch, and %%pic-store!s the result on a miss.
 *
 * A cache embedded in a *caller's* bytecode (the classic call-site PIC) is
 * silently bypassed the moment that caller gets JIT-compiled — JIT'd code
 * calls apply_arr/native code directly and never re-executes the bytecode
 * instruction the cache would live in. So the cache is owned by the
 * generic function itself (reached uniformly via vm_run's bytecode loop or
 * apply_arr's interpreted fallback, regardless of the caller's tier) —
 * oop.scm additionally pins the generic function's own dispatch closure
 * permanently interpreted via jit-never!, so this cache is never itself
 * silently bypassed either.
 *
 * Cache layout (a plain GC-managed Scheme vector — deliberately not a new
 * object.h type, keeping this additive and low-risk to the GC): length
 * 1 + 3*PIC_N. Slot 0 is a fixnum round-robin write cursor. Each of the
 * PIC_N cache entries occupies 3 consecutive slots starting at
 * 1 + 3*i: [class-tuple-vector, generation, cached-dispatch-chain].
 * An empty entry has #f in the class-tuple-vector position.
 */

#define PIC_N 4
#define PIC_VECTOR_LEN (1 + 3 * PIC_N)

void pic_register_builtins(val_t env);

#endif /* CURRY_PIC_H */
