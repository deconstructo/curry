#ifndef CURRY_BCJIT_RT_H
#define CURRY_BCJIT_RT_H

/*
 * Runtime side of the bytecode-to-LLVM JIT (src/llvm/bcjit.cpp).
 *
 * The translator turns one Chunk into one native function that does what
 * vm_run does for one CallFrame of that chunk. The function keeps every
 * value at the same place the interpreter would -- stack position k of the
 * frame is slots[k] on the VM stack -- and calls the helpers below for
 * everything that is not a plain load, store or branch. The helpers are
 * defined at the end of vm.c so they run the interpreter's own code
 * (load_global_cached, open_coded_binary2, vm_bind_args, ...), not a copy.
 *
 * Native function:  val_t fn(int32_t argc, val_t *args, BcClosure *self)
 *
 * This is the same C signature as the source-based JIT's functions
 * (src/llvm/codegen.cpp), which take a capture array as third argument.
 * JITC_BYTECODE in JitClosure.hdr.flags says which one a T_JITCLOSURE
 * holds; jit_invoke below passes the right third argument.
 *
 * See docs/thoughts/jit-bytecode-translator-plan-2026-11-15.md.
 */

#include <stdint.h>
#include "value.h"
#include "object.h"
#include "vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JitClosure.hdr.flags: the function was translated from bytecode. */
#define JITC_BYTECODE 1u

/* Written by bcjit_enter for the native function's own use. */
typedef struct {
    val_t  *entry_sp;   /* vm->sp when the function was entered           */
    val_t **sp_addr;    /* &vm->sp of the thread running the function     */
} BcJitFrame;

/* Function entry: copy the arguments to the top of the VM stack, check
 * that max_height slots fit there, push a CallFrame, check arity and build
 * the rest list (vm_bind_args). Returns the frame's slots. */
val_t *bcjit_enter(BcClosure *self, int32_t argc, val_t *args,
                   BcJitFrame *out, int32_t max_height);

/* Function exit: close upvalues that point into the frame, pop the
 * CallFrame, put vm->sp back where it was at entry. */
void bcjit_leave(val_t *slots, val_t *entry_sp);

/* Tail call to the function itself: what OP_SELF_TAIL_CALL does. Moves the
 * argc values at new_args to slots[0..], rebinds them, polls for an
 * interrupt. The native code then jumps to its first instruction. */
void bcjit_self_tail(BcClosure *self, val_t *slots, int32_t argc, val_t *new_args);

/* Backward jump: the interpreter's between-instructions interrupt check. */
void bcjit_poll(void);

val_t bcjit_load_global(Chunk *ch, int32_t ci);
void  bcjit_store_global(Chunk *ch, int32_t ci, val_t v);
void  bcjit_def_global(Chunk *ch, int32_t ci, val_t v);
void  bcjit_store_up(BcClosure *self, int32_t i, val_t v);

/* `op` is the opcode (OP_ADD, OP_LT, OP_CAR, ...): it selects the primitive
 * the call site was compiled for. */
val_t bcjit_binary(Chunk *ch, int32_t ci, val_t a, val_t b, int32_t op);
val_t bcjit_unary(Chunk *ch, int32_t ci, val_t x, int32_t op);
val_t bcjit_cons(Chunk *ch, int32_t ci, val_t a, val_t b);

val_t bcjit_call(val_t callee, int32_t argc, val_t *args);

/* OP_CLOSURE at code offset pc (the opcode byte). */
val_t bcjit_closure(BcClosure *self, val_t *slots, int32_t pc);
void  bcjit_close_up(val_t *slot);

#ifdef __cplusplus
} /* extern "C" */
#else

#include "eval.h"   /* g_jit_call_depth */
#include "gc.h"

/* Run the native code in cl->jit_val (which the caller has checked is a
 * T_JITCLOSURE). Used by every call site: vm.c's call opcodes and
 * apply_arr. */
static inline val_t jit_invoke(BcClosure *cl, int argc, val_t *args) {
    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
    JitClosure *jc = as_jitclos(cl->jit_val);
    uint64_t *third = (jc->hdr.flags & JITC_BYTECODE) ? (uint64_t *)cl
                                                      : (uint64_t *)jc->caps;
    g_jit_call_depth++;
    val_t r = (val_t)((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc,
                                                            (uint64_t *)args, third);
    g_jit_call_depth--;
    return r;
}

#endif /* __cplusplus */

#endif /* CURRY_BCJIT_RT_H */
