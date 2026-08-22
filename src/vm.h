#ifndef CURRY_VM_H
#define CURRY_VM_H

#include <stdint.h>
#include "value.h"
#include "object.h"
#include "chunk.h"

#define VM_STACK_MAX  4096
#define VM_FRAMES_MAX  256

/*
 * Upvalue — a captured variable that may be open (still on the stack)
 * or closed (moved to the heap when the enclosing scope exits).
 *
 * Open:  location points into the stack
 * Closed: location points to the closed field within this struct
 */
typedef struct Upvalue {
    Hdr            hdr;      /* type = T_UPVALUE; must be first           */
    val_t        *location; /* points to the live value                  */
    val_t         closed;   /* storage once closed off the stack         */
    struct Upvalue *next;   /* intrusive list of open upvalues           */
} Upvalue;

/*
 * BcClosure — a compiled chunk + captured upvalues (bytecode VM closure).
 * Distinct from the tree-walker's Closure (object.h T_CLOSURE=13).
 * Every lambda compiles to a Chunk; when evaluated it becomes a BcClosure
 * (which may capture zero upvalues for top-level functions).
 */
typedef struct {
    Hdr      hdr;         /* T_BCCLOSURE                                 */
    Chunk   *chunk;
    int      upval_count;
    uint32_t call_count;  /* tiered JIT: invocation counter              */
    val_t    jit_val;     /* V_VOID until compiled; T_JITCLOSURE after   */
    Upvalue *upvals[];    /* flexible array                               */
} BcClosure;

#define vis_bcclosure(v)  vis_type(v, T_BCCLOSURE)
#define as_bcclosure(v)   vunptr(BcClosure, v)

/*
 * Call frame — one activation record on the call stack.
 */
typedef struct {
    BcClosure *closure;   /* function being executed                     */
    uint8_t  *ip;         /* instruction pointer into closure->chunk     */
    val_t    *slots;      /* base of this frame's window into vm->stack  */
    int       slot_count; /* number of slots (locals + args)             */
    uint64_t  prof_start_ns; /* profiling: call entry time (level 2+)   */
    /* Was a callee value pushed onto the stack directly below `slots`
     * when this frame was created (true: OP_CALL's convention -- callee,
     * then args) or not (false: OP_CALL_GLOBAL's convention -- the
     * callee is resolved by the opcode handler itself via a fused
     * lookup, never pushed at all)? pop_frame() (vm.c) needs this to
     * know how many slots to remove on return: `frame->slots - 1` only
     * when a callee slot genuinely exists. Set once, at frame creation
     * (OP_CALL/OP_CALL_GLOBAL); UNCHANGED by a tail call that reuses
     * this same frame in place (OP_TAIL_CALL/OP_SELF_TAIL_CALL/
     * OP_TAIL_CALL_GLOBAL/OP_TAIL_CALL_WITH_VALUES) -- a tail call
     * doesn't add or remove the callee slot this frame was originally
     * entered with, it just changes which closure/code is running in it.
     * Found the hard way: an earlier version of OP_CALL_GLOBAL skipped
     * the callee slot without telling pop_frame, which unconditionally
     * assumed one always existed and silently clobbered the CALLER's own
     * next-lower stack slot on every return -- see git history / PR
     * discussion for the (+ 1 (g 5)) repro that caught it. */
    bool      has_callee_slot;
} CallFrame;

/*
 * VmHandlerInfo — saved VM state for one OP_PUSH_HANDLER frame.
 * The jmp_buf lives in vm_run()'s local vm_exn_handlers[] array (C stack),
 * indexed by the same handler_count slot.  That array stays valid until
 * vm_run() returns, which is always after any longjmp that targets it.
 */
#define VM_HANDLERS_MAX 64
typedef struct {
    int      frame_count;    /* vm->frame_count at push time             */
    val_t   *sp;             /* vm->sp at push time (before pushing thunk) */
    Upvalue *open_upvalues;  /* vm->open_upvalues at push time           */
    uint16_t catch_offset;   /* bytecode byte offset for the catch block */
    int      frame_idx;      /* which vm->frames[] entry is catching     */
} VmHandlerInfo;

/*
 * VM — the top-level execution state.
 * One VM per thread (thread-local in the future; global for now).
 */
typedef struct VM {
    val_t      stack[VM_STACK_MAX];
    val_t     *sp;                   /* stack pointer (next free slot)   */

    CallFrame  frames[VM_FRAMES_MAX];
    int        frame_count;

    Upvalue   *open_upvalues;        /* linked list of open upvalues     */

    VmHandlerInfo handler_stack[VM_HANDLERS_MAX]; /* exception save state */
    int           handler_count;                  /* active handler depth */
} VM;

/* Per-thread VM instance — each thread must call vm_init() before use.
 * Hidden from C++ TUs: _Thread_local is not a C++ keyword (C++ uses
 * thread_local, which mangles to a different, non-interoperable TLS
 * wrapper symbol -- see the current_handler precedent in eval.h). The
 * LLVM backend's .cpp files only need the vm_* functions declared below,
 * not this variable or the vm->-dereferencing inline helpers that follow. */
#ifndef __cplusplus
extern _Thread_local VM *vm;
#endif

/* Lifecycle */
void vm_init(void);
void vm_free(void);

/* Reset stack after a caught exception (longjmp leaves VM in unknown state). */
void vm_reset(void);

/* Execute a closure with argc arguments already on the stack.
   Returns the result value. */
val_t vm_run(BcClosure *closure, int argc);

/* Convenience: compile and run a top-level expression */
val_t vm_eval(val_t expr, val_t env);

/* Stack overflow — noreturn, defined in vm.c */
void vm_stack_overflow(void);

/* Validate argc against cl's declared arity, raising
 * EC_WRONG_NUMBER_OF_ARGUMENTS on mismatch (exact match if fixed-arity,
 * argc >= required-fixed-count if variadic). Used by every call path that
 * bypasses the bytecode interpreter's own argument binding — currently the
 * LLVM JIT fast path in apply_arr (eval.c) and the vis_jitclosure branches
 * of OP_CALL/OP_TAIL_CALL (vm.c) — since the JIT-compiled function's own
 * prologue handles rest-arg collection but never checked argc first. */
void vm_check_arity(BcClosure *cl, int argc);

/* Capture the current call stack as a list of (name file line) frames,
   innermost first, for attaching to a raised error. name/file are strings
   or #f when unknown; line is a fixnum or #f. Returns V_NIL if no VM frame
   is active (e.g. error raised outside vm_run, such as during read). */
val_t vm_capture_backtrace(void);

/* Stack helpers (inline for speed) */
#ifndef __cplusplus
static inline void   vm_push(val_t v)  {
    if (vm->sp >= vm->stack + VM_STACK_MAX) vm_stack_overflow();
    *vm->sp++ = v;
}
static inline val_t  vm_pop(void)      { return *--vm->sp; }
static inline val_t  vm_peek(int dist) { return vm->sp[-1 - dist]; }
#endif

/* Make a closure from a chunk (upvalues filled in by OP_CLOSURE) */
BcClosure *vm_make_closure(Chunk *chunk, int nupvals);

/* Close all open upvalues up to and including `last` stack slot */
void vm_close_upvalues(val_t *last);

/* Build a private copy of `bc` — same chunk, but every upvalue replaced
 * with an independent, already-closed snapshot of its current value —
 * safe to hand to a brand-new thread (actor_spawn). The original closure
 * is left completely untouched: this does NOT close `bc`'s own upvalues
 * in place, since an open Upvalue can be shared by other same-thread
 * closures (or the enclosing frame's own local-variable access) that must
 * keep observing the live, mutable variable normally. Without a snapshot
 * of some kind, an actor spawned from inside a tail-recursive loop can
 * race the spawning thread's next iteration reusing the same stack slot
 * and read a stale/wrong value with no error (see actors.c: actor_spawn,
 * and vm.c for the full reasoning and the in-place-close approach this
 * replaced after review found it corrupts same-thread sharers). Must be
 * called from the thread that owns the current `vm` (the one that opened
 * `bc`'s upvalues, if any are still open). */
BcClosure *vm_snapshot_closure_for_escape(BcClosure *bc);

#endif /* CURRY_VM_H */
