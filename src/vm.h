#ifndef CURRY_VM_H
#define CURRY_VM_H

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
    val_t        *location; /* points to the live value                  */
    val_t         closed;   /* storage once closed off the stack         */
    struct Upvalue *next;   /* intrusive list of open upvalues           */
} Upvalue;

/*
 * Closure — a compiled chunk + captured upvalues.
 * Every lambda in Curry compiles to a Chunk; when evaluated it becomes
 * a Closure (which may capture zero upvalues for top-level functions).
 */
typedef struct {
    Hdr      hdr;         /* T_CLOSURE                                   */
    Chunk   *chunk;
    int      upval_count;
    Upvalue *upvals[];    /* flexible array                               */
} Closure;

#define T_CLOSURE 41
#define vis_closure(v)  vis_type(v, T_CLOSURE)
#define as_closure(v)   vunptr(Closure, v)

/*
 * Call frame — one activation record on the call stack.
 */
typedef struct {
    Closure  *closure;    /* function being executed                     */
    uint8_t  *ip;         /* instruction pointer into closure->chunk     */
    val_t    *slots;      /* base of this frame's window into vm->stack  */
    int       slot_count; /* number of slots (locals + args)             */
} CallFrame;

/*
 * VM — the top-level execution state.
 * One VM per thread (thread-local in the future; global for now).
 */
typedef struct {
    val_t      stack[VM_STACK_MAX];
    val_t     *sp;                   /* stack pointer (next free slot)   */

    CallFrame  frames[VM_FRAMES_MAX];
    int        frame_count;

    Upvalue   *open_upvalues;        /* linked list of open upvalues     */
} VM;

/* Global VM instance (thread-local in future) */
extern VM *vm;

/* Lifecycle */
void vm_init(void);
void vm_free(void);

/* Execute a closure with argc arguments already on the stack.
   Returns the result value. */
val_t vm_run(Closure *closure, int argc);

/* Convenience: compile and run a top-level expression */
val_t vm_eval(val_t expr, val_t env);

/* Stack helpers (inline for speed) */
static inline void   vm_push(val_t v)  { *vm->sp++ = v; }
static inline val_t  vm_pop(void)      { return *--vm->sp; }
static inline val_t  vm_peek(int dist) { return vm->sp[-1 - dist]; }

/* Make a closure from a chunk (upvalues filled in by OP_CLOSURE) */
Closure *vm_make_closure(Chunk *chunk, int nupvals);

/* Close all open upvalues up to and including `last` stack slot */
void vm_close_upvalues(val_t *last);

#endif /* CURRY_VM_H */
