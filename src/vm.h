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

/* Per-thread VM instance — each thread must call vm_init() before use */
extern _Thread_local VM *vm;

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

/* Stack helpers (inline for speed) */
static inline void   vm_push(val_t v)  {
    if (vm->sp >= vm->stack + VM_STACK_MAX) vm_stack_overflow();
    *vm->sp++ = v;
}
static inline val_t  vm_pop(void)      { return *--vm->sp; }
static inline val_t  vm_peek(int dist) { return vm->sp[-1 - dist]; }

/* Make a closure from a chunk (upvalues filled in by OP_CLOSURE) */
BcClosure *vm_make_closure(Chunk *chunk, int nupvals);

/* Close all open upvalues up to and including `last` stack slot */
void vm_close_upvalues(val_t *last);

#endif /* CURRY_VM_H */
