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
 * Cache layout (plain GC-managed Scheme vectors — deliberately not a new
 * object.h type, keeping this additive and low-risk to the GC): the outer
 * "pic" vector has length 1 + PIC_N. Slot 0 is a fixnum round-robin write
 * cursor. Each of slots 1..PIC_N holds either #f (empty) or an "entry"
 * vector — a separate, fixed 3-element [class-tuple-vector, generation,
 * cached-dispatch-chain] vector.
 *
 * Every entry is built as a fresh, private, never-mutated-again vector and
 * published into its outer slot with a single write (see %%pic-store!) —
 * deliberately NOT three separate field writes into the outer vector, even
 * though that's a simpler-looking layout. Curry's actors are real OS
 * threads sharing GLOBAL_ENV, so a top-level generic function's cache is
 * the same shared object across every actor calling it; three independent
 * field writes would let a concurrent reader observe a torn entry (e.g. a
 * new tuple already visible paired with the OLD chain from whatever
 * previously occupied that slot) and silently dispatch to the wrong
 * method. Publishing one already-fully-built entry object per slot with a
 * single pointer-sized write means a reader only ever sees a slot's
 * previous entry (fully consistent, already built before anything else
 * could see it) or its new one (ditto) — never a mix.
 */

#define PIC_N 4
#define PIC_VECTOR_LEN (1 + PIC_N)
#define PIC_ENTRY_LEN 3   /* [tuple, generation, chain] */

void pic_register_builtins(val_t env);

#endif /* CURRY_PIC_H */
