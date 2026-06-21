/*
 * vm.c — Stack-based bytecode virtual machine for Curry.
 *
 * PURPOSE
 *   Executes Chunk bytecode produced by compiler.c.  Each Scheme lambda
 *   compiles to a Chunk; at runtime it becomes a BcClosure (chunk +
 *   captured upvalues).  vm_run() is the entry point for executing a
 *   BcClosure.
 *
 * VALUE STACK
 *   A flat array of val_t (VM_STACK_MAX entries).  vm->sp points one past
 *   the top of the stack.  Each call frame records a `slots` pointer into
 *   the stack; LOAD_LOCAL i / STORE_LOCAL i index relative to `slots`.
 *
 * CALLING CONVENTION
 *   Before OP_CALL n:  stack = [... | callee | arg0 | ... | argN-1 ]
 *                                              ^
 *                                         new frame's slots
 *   The callee itself sits one slot below slots (at slots[-1]).
 *   On RETURN: result replaces the callee + args window; sp is reset to
 *   slots - 1 (removing callee) and result is pushed.
 *
 * TAIL CALLS
 *   OP_TAIL_CALL reuses the current frame instead of pushing a new one:
 *   new args are memmove'd over the current slots, ip is reset to the
 *   new closure's code.  Non-BcClosure callables are dispatched via
 *   apply_arr() and the current frame is immediately returned.
 *
 * UPVALUES (open/closed protocol, same as Lua 5)
 *   While a captured variable is live on the stack, Upvalue.location
 *   points into the stack.  When the enclosing scope exits
 *   (vm_close_upvalues), the value is copied into Upvalue.closed and
 *   location is redirected there so the value lives on the GC heap.
 *
 * INTEROPERABILITY
 *   BcClosure   — fully executed by the VM's dispatch loop.
 *   Primitive   — called directly (fn pointer).
 *   Everything else (tree-walker Closure, continuations, …) is handed to
 *   apply_arr() from eval.c so both execution engines can call each other
 *   freely during the transition to full-VM execution.
 *
 * GLOBALS
 *   All global variable operations (OP_LOAD_GLOBAL, OP_STORE_GLOBAL,
 *   OP_DEF_GLOBAL) operate on GLOBAL_ENV, the same environment the
 *   tree-walker uses, keeping one shared namespace throughout the
 *   migration.
 *
 * OPCODE REFERENCE  (see also opcode.h for the full enum and comments)
 *
 *   Format codes used below:
 *     –       : no operand
 *     A       : 1-byte immediate (uint8_t)
 *     B       : 2-byte little-endian immediate (uint16_t)
 *
 *   Constants & literals
 *     OP_CONST   A   push constants[A]
 *     OP_CONST_W B   push constants[B]            (wide index, > 255 consts)
 *     OP_TRUE    –   push #t
 *     OP_FALSE   –   push #f
 *     OP_NIL     –   push '()
 *     OP_VOID    –   push void
 *
 *   Local variables  (frame->slots[A])
 *     OP_LOAD_LOCAL  A   push slots[A]
 *     OP_STORE_LOCAL A   slots[A] = pop()      (no push; define/set! follow with OP_VOID)
 *
 *   Global variables (constant pool holds the symbol)
 *     OP_LOAD_GLOBAL  A   push GLOBAL_ENV[constants[A]]
 *     OP_STORE_GLOBAL A   GLOBAL_ENV[constants[A]] = pop()   (set! — symbol must exist)
 *     OP_DEF_GLOBAL   A   GLOBAL_ENV[constants[A]] = pop()   (define — creates binding)
 *
 *   Upvalues (closure's upvals[] array)
 *     OP_LOAD_UP  A   push *upvals[A]->location
 *     OP_STORE_UP A   *upvals[A]->location = pop()
 *
 *   Stack manipulation
 *     OP_POP  –   discard TOS
 *     OP_DUP  –   push copy of TOS
 *     OP_SWAP –   exchange TOS and TOS-1
 *     OP_NOP  –   no-op
 *
 *   Arithmetic  (numeric tower: fixnum → bignum → rational → flonum → …)
 *     OP_ADD  –   push num_add(a, b)    ; a = second-from-top, b = top
 *     OP_SUB  –   push num_sub(a, b)
 *     OP_MUL  –   push num_mul(a, b)
 *     OP_DIV  –   push num_div(a, b)
 *     OP_NEG  –   push num_neg(TOS)
 *     OP_ABS  –   push num_abs(TOS)
 *     OP_EXPT –   push num_expt(a, b)   ; a^b
 *
 *   Comparison  (return #t or #f)
 *     OP_EQ    –   (= a b)   numeric equality
 *     OP_LT    –   (< a b)
 *     OP_LE    –   (<= a b)
 *     OP_GT    –   (> a b)
 *     OP_GE    –   (>= a b)
 *     OP_NUMEQ –   alias for OP_EQ (= a b)
 *
 *   Identity & equivalence
 *     OP_EQV   –   (eqv? a b)
 *     OP_EQUAL –   (equal? a b)
 *     OP_NOT   –   push #f iff TOS is #f, else #t
 *
 *   Pairs & lists
 *     OP_CONS   –   push (cons a b)
 *     OP_CAR    –   push car(TOS)
 *     OP_CDR    –   push cdr(TOS)
 *     OP_SETCAR –   set-car!(pair val)   → void
 *     OP_SETCDR –   set-cdr!(pair val)   → void
 *     OP_NULLP  –   push (null? TOS)
 *     OP_PAIRP  –   push (pair? TOS)
 *
 *   Strings & characters
 *     OP_STRINGLEN –   push (string-length TOS)
 *     OP_STRINGREF –   push (string-ref str idx)   ; UTF-8 aware via builtin
 *     OP_CHARTOFIX –   push (char->integer TOS)
 *     OP_FIXTOCHAR –   push (integer->char TOS)
 *
 *   Type predicates
 *     OP_NUMBERP –   (number? TOS)
 *     OP_STRINGP –   (string? TOS)
 *     OP_SYMBOLP –   (symbol? TOS)
 *     OP_CHARP   –   (char? TOS)
 *     OP_BOOLP   –   (boolean? TOS)
 *     OP_PROCP   –   (procedure? TOS)   ; true for BcClosure too
 *     OP_VECTORP –   (vector? TOS)
 *
 *   Vectors
 *     OP_MAKEVEC A   push make-vector(len fill)  ; A = arg count (1 or 2)
 *     OP_VECREF  –   push vector-ref(vec idx)
 *     OP_VECSET  –   vector-set!(vec idx val)    → void
 *     OP_VECLEN  –   push vector-length(vec)
 *
 *   Control flow  (targets are absolute byte offsets from chunk start)
 *     OP_JUMP       B   unconditional jump to B
 *     OP_JUMP_FALSE B   jump to B if TOS is #f  (pops TOS)
 *     OP_JUMP_TRUE  B   jump to B if TOS is not #f (pops TOS)
 *
 *   Calls
 *     OP_CALL      A   call; A = argc.  Stack: [... callee arg0 … argA-1]
 *                      For BcClosure: push new CallFrame.
 *                      For others: apply_arr() → push result.
 *     OP_TAIL_CALL A   tail call; A = argc.
 *                      For BcClosure: reuse current frame (proper TCO).
 *                      For others: apply_arr() + RETURN current frame.
 *     OP_RETURN    –   return TOS to caller; pops current CallFrame.
 *
 *   Closures
 *     OP_CLOSURE A     push new BcClosure wrapping constants[A] (a Chunk*).
 *                      The next 2*upval_count bytes are capture descriptors:
 *                        [is_local:u8, index:u8]  per upvalue.
 *                      is_local=1 → capture slots[index] of current frame.
 *                      is_local=0 → inherit upvals[index] of current closure.
 *     OP_CLOSE_UP –    close open upvalue at TOS (move to heap) and pop.
 *
 *   Apply & multiple values
 *     OP_APPLY         A   (apply fn arg1 … rest-list); A = total stack items
 *                          including fn.  Flattens intermediate args and
 *                          last-list, then calls fn.
 *     OP_VALUES        A   bundle A stack values; single value is a no-op.
 *     OP_CALL_WITH_VALUES –  (call-with-values thunk consumer); simplified.
 *
 *   Exceptions
 *     OP_PUSH_HANDLER B   push exception handler frame; B = fallback offset.
 *                         (Not fully implemented; reserved for future use.)
 *     OP_POP_HANDLER  –   pop exception handler frame.
 *     OP_RAISE        –   raise TOS as exception via tree-walker raise.
 *
 *   I/O
 *     OP_DISPLAY –   (display TOS)  → void
 *     OP_WRITE   –   (write TOS)    → void
 *     OP_NEWLINE –   emit '\n'      → void
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "vm.h"
#include "profiling.h"
#include "compiler.h"
#include "chunk.h"
#include "opcode.h"
#include "value.h"
#include "object.h"
#include "gc.h"
#include <gc/gc_mark.h>
#include "numeric.h"
#include "symbol.h"
#include "env.h"
#include "eval.h"
#include "set.h"
#include "builtins.h"

/* ── Global VM state ─────────────────────────────────────────────────── */

_Thread_local VM *vm = NULL;

__attribute__((noreturn)) void vm_stack_overflow(void) {
    fprintf(stderr, "[vm] vm_stack_overflow: sp=%p stack_end=%p\n",
            (void *)vm->sp, (void *)(vm->stack + VM_STACK_MAX));
    fflush(stderr);
    scm_raise(V_FALSE, "value stack overflow (max %d slots)", VM_STACK_MAX);
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void vm_init(void) {
    /* Use GC_MALLOC_UNCOLLECTABLE so Boehm GC never frees the VM struct even
     * though the only reference is a _Thread_local pointer (TLS is not scanned
     * as a GC root).  GC still scans the struct's interior for live val_t refs. */
    vm = (VM *)GC_MALLOC_UNCOLLECTABLE(sizeof(VM));
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->handler_count = 0;
}

void vm_free(void) {
    if (vm) GC_FREE(vm);
    vm = NULL;
}

void vm_reset(void) {
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->handler_count = 0;
}

/* Forward declaration — defined in eval.c where scm_cons / symbol.h are available. */
void maybe_jit_bcc(BcClosure *cl);

/* Quick inline guard: skip the call for the common case where jit_val is
 * already decided, or where no source AST was preserved. */
static inline void vm_maybe_jit(BcClosure *cl) {
    if (cl->jit_val != V_VOID) return;
    if (cl->chunk->src_lambda == V_VOID) return;
    if (cl->upval_count > 0 && !cl->chunk->upval_names) return;
    maybe_jit_bcc(cl);
}

/* ── BcClosure allocation ────────────────────────────────────────────── */

BcClosure *vm_make_closure(Chunk *chunk, int nupvals) {
    BcClosure *cl = (BcClosure *)gc_alloc_pinned(
        sizeof(BcClosure) + (size_t)nupvals * sizeof(Upvalue *));
    cl->hdr.type    = T_BCCLOSURE;
    cl->hdr.flags   = 0;
    cl->hdr.fwd     = 0;
    cl->chunk       = chunk;
    cl->upval_count = nupvals;
    cl->call_count  = 0;
    cl->jit_val     = V_VOID;
    for (int i = 0; i < nupvals; i++) cl->upvals[i] = NULL;
    return cl;
}

/* ── Upvalue management ──────────────────────────────────────────────── */

/* Find or create an open Upvalue for `slot` in the open-upvalue list.
   The list is kept in descending stack-address order. */
static Upvalue *open_upvalue(val_t *slot) {
    Upvalue *prev = NULL;
    Upvalue *up   = vm->open_upvalues;
    while (up && up->location > slot) { prev = up; up = up->next; }
    if (up && up->location == slot) return up;   /* already open */

    Upvalue *n  = CURRY_NEW_PINNED(Upvalue);
    n->hdr.type = T_UPVALUE; n->hdr.flags = 0; n->hdr.fwd = 0;
    n->location = slot;
    n->closed   = V_VOID;
    n->next     = up;
    if (prev) prev->next = n;
    else      vm->open_upvalues = n;
    return n;
}

/* Close all open upvalues at or above `last` on the stack.
   Called on scope exit (OP_CLOSE_UP) and frame pop (RETURN / TAIL_CALL). */
void vm_close_upvalues(val_t *last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        Upvalue *up  = vm->open_upvalues;
        gc_wb_slot(&up->closed, *up->location);
        up->location = &up->closed;
        vm->open_upvalues = up->next;
    }
}

/* ── Foreign call helper ─────────────────────────────────────────────── */

/* Call any non-BcClosure callable using the tree-walker's apply_arr(). */
static val_t call_foreign(val_t callee, int argc, val_t *args) {
    return apply_arr(callee, argc, args);
}

/* ── Return helper (shared by RETURN and non-BcClosure TAIL_CALL) ───── */

/* Pop the current frame and push `result` onto the caller's stack.
   Returns true if this was the outermost frame (caller should return). */
static bool pop_frame(CallFrame **frame_ptr, val_t result) {
    CallFrame *frame = *frame_ptr;
    vm_close_upvalues(frame->slots);
    vm->frame_count--;
    if (vm->frame_count == 0) {
        /* Restore stack to clean state and signal top-level return */
        vm->sp = vm->stack;
        *vm->sp++ = result;
        return true;
    }
    vm->sp = frame->slots - 1;   /* remove callee + args */
    *vm->sp++ = result;
    *frame_ptr = &vm->frames[vm->frame_count - 1];
    return false;
}

/* ── Main dispatch loop ──────────────────────────────────────────────── */

val_t vm_run(BcClosure *top_closure, int argc) {
    /* jmp_bufs for OP_PUSH_HANDLER frames active in this vm_run invocation.
     * Indexed by vm->handler_count at push time.  Must live on this C frame
     * so that longjmp targets remain valid for the duration of this call.
     *
     * Invariant: vm->handler_count at entry is entry_handler_count.  After
     * vm_run returns, handler_count is restored (handlers are always paired
     * with OP_POP_HANDLER in normal execution; longjmp unwinds the rest). */
    ExnHandler vm_exn_handlers[VM_HANDLERS_MAX];
    (void)vm_exn_handlers; /* suppress unused-var warning if no handlers pushed */

    int entry_depth = vm->frame_count;  /* used to detect return from nested calls */
    if (vm->frame_count >= VM_FRAMES_MAX)
        scm_raise(V_FALSE, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
    CallFrame *frame  = &vm->frames[vm->frame_count++];
    frame->closure    = top_closure;
    frame->ip         = top_closure->chunk->code;
    frame->slots      = vm->sp - argc;
    frame->slot_count = argc;

#define READ_U8()    (*frame->ip++)
#define READ_U16()   (frame->ip += 2, \
                      (uint16_t)(frame->ip[-2] | ((unsigned)frame->ip[-1] << 8)))
#define JUMP_ABS(t)  (frame->ip = frame->closure->chunk->code + (t))
#define CONSTS       (frame->closure->chunk->constants)
#define GCACHE       (frame->closure->chunk->glob_cache)
#define PUSH(v)      do { \
    if (vm->sp >= vm->stack + VM_STACK_MAX) \
        scm_raise(V_FALSE, "value stack overflow (max %d slots)", VM_STACK_MAX); \
    *vm->sp++ = (v); } while (0)
#define POP()        (*--vm->sp)
#define PEEK(n)      (vm->sp[-1-(n)])

#ifdef __GNUC__
    /* Threaded dispatch: each opcode handler jumps directly to the next
     * without going through a shared switch discriminator.  ~20% faster
     * on CPU-bound code (better branch-predictor utilisation per site). */
    static const void *const dt[OP_COUNT] = {
        [OP_CONST]       = &&L_OP_CONST,       [OP_CONST_W]      = &&L_OP_CONST_W,
        [OP_TRUE]        = &&L_OP_TRUE,        [OP_FALSE]        = &&L_OP_FALSE,
        [OP_NIL]         = &&L_OP_NIL,         [OP_VOID]         = &&L_OP_VOID,
        [OP_LOAD_LOCAL]  = &&L_OP_LOAD_LOCAL,  [OP_STORE_LOCAL]  = &&L_OP_STORE_LOCAL,
        [OP_LOAD_GLOBAL] = &&L_OP_LOAD_GLOBAL, [OP_STORE_GLOBAL] = &&L_OP_STORE_GLOBAL,
        [OP_DEF_GLOBAL]  = &&L_OP_DEF_GLOBAL,
        [OP_LOAD_UP]     = &&L_OP_LOAD_UP,     [OP_STORE_UP]     = &&L_OP_STORE_UP,
        [OP_POP]         = &&L_OP_POP,         [OP_DUP]          = &&L_OP_DUP,
        [OP_SWAP]        = &&L_OP_SWAP,        [OP_SLIDE]        = &&L_OP_SLIDE,
        [OP_ADD]         = &&L_OP_ADD,         [OP_SUB]          = &&L_OP_SUB,
        [OP_MUL]         = &&L_OP_MUL,         [OP_DIV]          = &&L_OP_DIV,
        [OP_NEG]         = &&L_OP_NEG,         [OP_ABS]          = &&L_OP_ABS,
        [OP_EXPT]        = &&L_OP_EXPT,
        [OP_EQ]          = &&L_OP_EQ,          [OP_LT]           = &&L_OP_LT,
        [OP_LE]          = &&L_OP_LE,          [OP_GT]           = &&L_OP_GT,
        [OP_GE]          = &&L_OP_GE,          [OP_NUMEQ]        = &&L_OP_NUMEQ,
        [OP_EQV]         = &&L_OP_EQV,         [OP_EQUAL]        = &&L_OP_EQUAL,
        [OP_NOT]         = &&L_OP_NOT,
        [OP_CONS]        = &&L_OP_CONS,        [OP_CAR]          = &&L_OP_CAR,
        [OP_CDR]         = &&L_OP_CDR,         [OP_SETCAR]       = &&L_OP_SETCAR,
        [OP_SETCDR]      = &&L_OP_SETCDR,      [OP_NULLP]        = &&L_OP_NULLP,
        [OP_PAIRP]       = &&L_OP_PAIRP,
        [OP_STRINGLEN]   = &&L_OP_STRINGLEN,   [OP_STRINGREF]    = &&L_OP_STRINGREF,
        [OP_CHARTOFIX]   = &&L_OP_CHARTOFIX,   [OP_FIXTOCHAR]    = &&L_OP_FIXTOCHAR,
        [OP_NUMBERP]     = &&L_OP_NUMBERP,     [OP_STRINGP]      = &&L_OP_STRINGP,
        [OP_SYMBOLP]     = &&L_OP_SYMBOLP,     [OP_CHARP]        = &&L_OP_CHARP,
        [OP_BOOLP]       = &&L_OP_BOOLP,       [OP_PROCP]        = &&L_OP_PROCP,
        [OP_VECTORP]     = &&L_OP_VECTORP,
        [OP_MAKEVEC]     = &&L_OP_MAKEVEC,     [OP_VECREF]       = &&L_OP_VECREF,
        [OP_VECSET]      = &&L_OP_VECSET,      [OP_VECLEN]       = &&L_OP_VECLEN,
        [OP_JUMP]        = &&L_OP_JUMP,        [OP_JUMP_FALSE]   = &&L_OP_JUMP_FALSE,
        [OP_JUMP_TRUE]   = &&L_OP_JUMP_TRUE,
        [OP_CALL]        = &&L_OP_CALL,        [OP_TAIL_CALL]    = &&L_OP_TAIL_CALL,
        [OP_RETURN]      = &&L_OP_RETURN,
        [OP_CLOSURE]     = &&L_OP_CLOSURE,     [OP_CLOSE_UP]     = &&L_OP_CLOSE_UP,
        [OP_APPLY]       = &&L_OP_APPLY,       [OP_VALUES]       = &&L_OP_VALUES,
        [OP_CALL_WITH_VALUES] = &&L_OP_CALL_WITH_VALUES,
        [OP_PUSH_HANDLER]= &&L_OP_PUSH_HANDLER,[OP_POP_HANDLER]  = &&L_OP_POP_HANDLER,
        [OP_RAISE]       = &&L_OP_RAISE,
        [OP_DISPLAY]     = &&L_OP_DISPLAY,     [OP_WRITE]        = &&L_OP_WRITE,
        [OP_NEWLINE]     = &&L_OP_NEWLINE,     [OP_NOP]          = &&L_OP_NOP,
    };
#   define CASE(op)  L_##op:
#   define NEXT      goto *dt[READ_U8()]
    NEXT;   /* prime the pump */
#else /* no computed goto */
    for (;;) { switch (READ_U8()) {
#   define CASE(op)  case op:
#   define NEXT      break
#endif

        /* ── Constants ──────────────────────────────────────────────── */
        CASE(OP_CONST)   PUSH(CONSTS[READ_U8()]);   NEXT;
        CASE(OP_CONST_W) PUSH(CONSTS[READ_U16()]);  NEXT;
        CASE(OP_TRUE)    PUSH(V_TRUE);               NEXT;
        CASE(OP_FALSE)   PUSH(V_FALSE);              NEXT;
        CASE(OP_NIL)     PUSH(V_NIL);                NEXT;
        CASE(OP_VOID)    PUSH(V_VOID);               NEXT;

        /* ── Locals ─────────────────────────────────────────────────── */
        CASE(OP_LOAD_LOCAL) {
            uint8_t i = READ_U8();
            PUSH(frame->slots[i]);
            NEXT;
        }
        CASE(OP_STORE_LOCAL) {
            uint8_t i = READ_U8();
            frame->slots[i] = POP();
            NEXT;
        }

        /* ── Globals ─────────────────────────────────────────────────── */
        CASE(OP_LOAD_GLOBAL) {
            uint8_t ci = READ_U8();
            GlobCacheEntry *cache = GCACHE;
            EnvFrame *root = as_env(GLOBAL_ENV);
            val_t loaded_gval;
            if (__builtin_expect(cache != NULL && cache[ci].slot != NULL &&
                                 cache[ci].version == root->version, 1)) {
                loaded_gval = *cache[ci].slot;
            } else {
                val_t sym = CONSTS[ci];
                val_t *slot = env_lookup_slot(GLOBAL_ENV, sym);
                if (!slot) scm_raise(V_FALSE, "unbound variable: %s", sym_cstr(sym));
                if (cache) { cache[ci].slot = slot; cache[ci].version = root->version; }
                loaded_gval = *slot;
            }
            PUSH(loaded_gval);
            NEXT;
        }
        CASE(OP_STORE_GLOBAL) {
            uint8_t ci = READ_U8();
            val_t val = POP();
            GlobCacheEntry *cache = GCACHE;
            EnvFrame *root = as_env(GLOBAL_ENV);
            if (__builtin_expect(cache != NULL && cache[ci].slot != NULL &&
                                 cache[ci].version == root->version, 1)) {
                gc_wb_slot(cache[ci].slot, val);
            } else {
                val_t sym = CONSTS[ci];
                val_t *slot = env_lookup_slot(GLOBAL_ENV, sym);
                if (!slot) fprintf(stderr, "vm: set! unbound variable\n");
                else {
                    gc_wb_slot(slot, val);
                    if (cache) { cache[ci].slot = slot; cache[ci].version = root->version; }
                }
            }
            NEXT;
        }
        CASE(OP_DEF_GLOBAL) {
            uint8_t ci = READ_U8();
            val_t sym = CONSTS[ci];
            val_t val = POP();
            env_define(GLOBAL_ENV, sym, val);
            /* Fill cache after define (frame may have grown, bumping version) */
            GlobCacheEntry *cache = GCACHE;
            if (cache) {
                EnvFrame *root = as_env(GLOBAL_ENV);
                val_t *slot = env_lookup_slot(GLOBAL_ENV, sym);
                if (slot) { cache[ci].slot = slot; cache[ci].version = root->version; }
            }
            NEXT;
        }

        /* ── Upvalues ───────────────────────────────────────────────── */
        CASE(OP_LOAD_UP) {
            uint8_t i = READ_U8();
            PUSH(*frame->closure->upvals[i]->location);
            NEXT;
        }
        CASE(OP_STORE_UP) {
            uint8_t i = READ_U8();
            val_t v = POP();
            gc_wb_slot(frame->closure->upvals[i]->location, v);
            NEXT;
        }

        /* ── Stack manipulation ─────────────────────────────────────── */
        CASE(OP_POP)  POP(); NEXT;
        CASE(OP_DUP)  { val_t v = PEEK(0); PUSH(v); NEXT; }
        CASE(OP_SWAP) { val_t a = POP(), b = POP(); PUSH(a); PUSH(b); NEXT; }
        CASE(OP_NOP)  NEXT;

        /* ── Arithmetic ─────────────────────────────────────────────── */
        CASE(OP_ADD) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1)) {
                intptr_t ia = vunfix(a), ib = vunfix(b), ir;
                if (!__builtin_add_overflow(ia, ib, &ir)) { PUSH(vfix(ir)); }
                else PUSH(num_add(a, b));
            } else PUSH(num_add(a, b));
            NEXT;
        }
        CASE(OP_SUB) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1)) {
                intptr_t ia = vunfix(a), ib = vunfix(b), ir;
                if (!__builtin_sub_overflow(ia, ib, &ir)) { PUSH(vfix(ir)); }
                else PUSH(num_sub(a, b));
            } else PUSH(num_sub(a, b));
            NEXT;
        }
        CASE(OP_MUL) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1)) {
                intptr_t ia = vunfix(a), ib = vunfix(b), ir;
                if (!__builtin_mul_overflow(ia, ib, &ir)) { PUSH(vfix(ir)); }
                else PUSH(num_mul(a, b));
            } else PUSH(num_mul(a, b));
            NEXT;
        }
        CASE(OP_DIV) { val_t b = POP(), a = POP(); PUSH(num_div(a, b)); NEXT; }
        CASE(OP_NEG) { PUSH(num_neg(POP())); NEXT; }
        CASE(OP_ABS) { PUSH(num_abs(POP())); NEXT; }
        CASE(OP_EXPT) { val_t b = POP(), a = POP(); PUSH(num_expt(a, b)); NEXT; }

        /* ── Comparison ─────────────────────────────────────────────── */
        CASE(OP_EQ)
        CASE(OP_NUMEQ) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH(a == b ? V_TRUE : V_FALSE);
            else
                PUSH(num_eq(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_LT) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH((intptr_t)a < (intptr_t)b ? V_TRUE : V_FALSE);
            else
                PUSH(num_lt(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_LE) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH((intptr_t)a <= (intptr_t)b ? V_TRUE : V_FALSE);
            else
                PUSH(num_le(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_GT) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH((intptr_t)a > (intptr_t)b ? V_TRUE : V_FALSE);
            else
                PUSH(num_gt(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_GE) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH((intptr_t)a >= (intptr_t)b ? V_TRUE : V_FALSE);
            else
                PUSH(num_ge(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }

        /* ── Identity / equivalence ─────────────────────────────────── */
        CASE(OP_EQV) {
            val_t b = POP(), a = POP();
            PUSH(scm_eqv(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_EQUAL) {
            val_t b = POP(), a = POP();
            PUSH(scm_equal(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_NOT) {
            PUSH(POP() == V_FALSE ? V_TRUE : V_FALSE);
            NEXT;
        }

        /* ── Pairs ──────────────────────────────────────────────────── */
        CASE(OP_CONS) {
            /* Allocate first: args still on VM stack so GC (if triggered)
             * can update them.  Re-read from stack after allocation. */
            Pair *p = (Pair *)gc_alloc(sizeof(Pair));
            p->hdr.type = T_PAIR; p->hdr.flags = 0;
            p->car = vm->sp[-2]; p->cdr = vm->sp[-1];
            vm->sp -= 2; *vm->sp++ = vptr(p);
            NEXT;
        }
        CASE(OP_CAR)  { PUSH(vcar(POP())); NEXT; }
        CASE(OP_CDR)  { PUSH(vcdr(POP())); NEXT; }
        CASE(OP_SETCAR) {
            val_t v = POP(), p = POP();
            GC_WB(as_pair(p), car, v);
            PUSH(V_VOID);
            NEXT;
        }
        CASE(OP_SETCDR) {
            val_t v = POP(), p = POP();
            GC_WB(as_pair(p), cdr, v);
            PUSH(V_VOID);
            NEXT;
        }
        CASE(OP_NULLP) { PUSH(vis_nil(POP())  ? V_TRUE : V_FALSE); NEXT; }
        CASE(OP_PAIRP) { PUSH(vis_pair(POP()) ? V_TRUE : V_FALSE); NEXT; }

        /* ── Strings / chars ─────────────────────────────────────────── */
        CASE(OP_STRINGLEN) {
            /* Delegate: string-length returns char count, not byte length. */
            val_t v = POP();
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("string-length"));
            val_t args[1] = {v};
            PUSH(call_foreign(fn, 1, args));
            NEXT;
        }
        CASE(OP_STRINGREF) {
            /* UTF-8 indexing is complex; delegate to the existing builtin. */
            val_t args[2]; args[1] = POP(); args[0] = POP();
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("string-ref"));
            PUSH(call_foreign(fn, 2, args));
            NEXT;
        }
        CASE(OP_CHARTOFIX) { PUSH(vfix((intptr_t)vunchr(POP()))); NEXT; }
        CASE(OP_FIXTOCHAR) { PUSH(vchr((uint32_t)vunfix(POP()))); NEXT; }

        /* ── Type predicates ─────────────────────────────────────────── */
        CASE(OP_NUMBERP) { PUSH(vis_number(POP())              ? V_TRUE : V_FALSE); NEXT; }
        CASE(OP_STRINGP) { PUSH(vis_string(POP())              ? V_TRUE : V_FALSE); NEXT; }
        CASE(OP_SYMBOLP) { PUSH(vis_symbol(POP())              ? V_TRUE : V_FALSE); NEXT; }
        CASE(OP_CHARP)   { PUSH(vis_char(POP())                ? V_TRUE : V_FALSE); NEXT; }
        CASE(OP_BOOLP)   {
            val_t v = POP();
            PUSH((v == V_TRUE || v == V_FALSE)             ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_PROCP) {
            val_t v = POP();
            PUSH((vis_proc(v) || vis_bcclosure(v))         ? V_TRUE : V_FALSE);
            NEXT;
        }
        CASE(OP_VECTORP) { PUSH(vis_vector(POP())              ? V_TRUE : V_FALSE); NEXT; }

        /* ── Vectors ─────────────────────────────────────────────────── */
        CASE(OP_MAKEVEC) {
            uint8_t argc2 = READ_U8();
            val_t *args   = vm->sp - argc2;
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("make-vector"));
            val_t result = call_foreign(fn, (int)argc2, args);
            vm->sp -= argc2;
            PUSH(result);
            NEXT;
        }
        CASE(OP_VECREF) {
            val_t idx = POP(), vec = POP();
            PUSH(as_vec(vec)->data[vunfix(idx)]);
            NEXT;
        }
        CASE(OP_VECSET) {
            val_t val = POP(), idx = POP(), vec = POP();
            gc_wb_slot(&as_vec(vec)->data[vunfix(idx)], val);
            PUSH(V_VOID);
            NEXT;
        }
        CASE(OP_VECLEN) {
            PUSH(vfix((intptr_t)as_vec(POP())->len));
            NEXT;
        }

        /* ── Control flow ────────────────────────────────────────────── */
        CASE(OP_JUMP) {
            uint16_t target = READ_U16();
            JUMP_ABS(target);
            NEXT;
        }
        CASE(OP_JUMP_FALSE) {
            uint16_t target = READ_U16();
            val_t cond = POP();
            if (cond == V_FALSE) JUMP_ABS(target);
            NEXT;
        }
        CASE(OP_JUMP_TRUE) {
            uint16_t target = READ_U16();
            val_t cond = POP();
            if (cond != V_FALSE) JUMP_ABS(target);
            NEXT;
        }

        /* ── Calls ───────────────────────────────────────────────────── */
        CASE(OP_CALL) {
            uint8_t argc2 = READ_U8();
            val_t callee  = PEEK(argc2);

            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
                /* Tiered JIT: hot-swap to native code once compiled. */
                if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
                    && curry_profiling_level == 0) {
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    val_t result = ((jit_fn_t)jc->fn)((int32_t)argc2,
                                                       (uint64_t *)call_args,
                                                       (uint64_t *)jc->caps);
                    g_jit_call_depth--;
                    vm->sp -= argc2 + 1;
                    PUSH(result);
                    NEXT;
                }
                vm_maybe_jit(cl);
                if (vm->frame_count >= VM_FRAMES_MAX)
                    scm_raise(V_FALSE, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
                CallFrame *nf  = &vm->frames[vm->frame_count++];
                nf->closure    = cl;
                nf->ip         = cl->chunk->code;
                nf->slots      = vm->sp - argc2;
                nf->slot_count = argc2;
                nf->prof_start_ns = 0;
                if (curry_profiling_level >= 1 && cl->chunk->name) {
                    val_t sym = sym_intern_cstr(cl->chunk->name);
                    if (curry_profiling_level >= 2)
                        nf->prof_start_ns = profiling_now_ns();
                    else
                        profiling_record_call(sym);
                }
                frame = nf;
            } else {
                val_t *call_args = vm->sp - argc2;
                val_t result     = call_foreign(callee, (int)argc2, call_args);
                vm->sp -= argc2 + 1;   /* pop args + callee */
                PUSH(result);
            }
            NEXT;
        }

        CASE(OP_TAIL_CALL) {
            uint8_t argc2 = READ_U8();
            val_t callee  = PEEK(argc2);
            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
                /* Tiered JIT: hot-swap to native code once compiled. */
                if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
                    && curry_profiling_level == 0) {
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    val_t result = ((jit_fn_t)jc->fn)((int32_t)argc2,
                                                       (uint64_t *)call_args,
                                                       (uint64_t *)jc->caps);
                    g_jit_call_depth--;
                    vm->sp -= argc2 + 1;
                    if (pop_frame(&frame, result)) return *--vm->sp;
                    if (vm->frame_count == entry_depth) return *--vm->sp;
                    NEXT;
                }
                vm_maybe_jit(cl);
                /* Record timing for the outgoing frame before reuse */
                if (curry_profiling_level >= 2 && frame->prof_start_ns &&
                        frame->closure->chunk->name) {
                    val_t sym = sym_intern_cstr(frame->closure->chunk->name);
                    profiling_record_timed(sym, frame->prof_start_ns);
                }
                /* Count cross-function tail calls (not self-tail-call loops) */
                if (curry_profiling_level >= 1 && cl != frame->closure &&
                        cl->chunk->name) {
                    profiling_record_call_tco(sym_intern_cstr(cl->chunk->name));
                }
                val_t *new_args = vm->sp - argc2;
                vm_close_upvalues(frame->slots);
                memmove(frame->slots, new_args, argc2 * sizeof(val_t));
                vm->sp            = frame->slots + argc2;
                frame->closure    = cl;
                frame->ip         = cl->chunk->code;
                frame->slot_count = argc2;
                frame->prof_start_ns = 0;
                if (curry_profiling_level >= 2 && cl->chunk->name)
                    frame->prof_start_ns = profiling_now_ns();
            } else {
                /* Non-BcClosure in tail position: call then return. */
                val_t *call_args = vm->sp - argc2;
                val_t result     = call_foreign(callee, (int)argc2, call_args);
                vm->sp -= argc2 + 1;
                if (pop_frame(&frame, result)) return *--vm->sp;
                if (vm->frame_count == entry_depth) return *--vm->sp;
            }
            NEXT;
        }

        CASE(OP_RETURN) {
            val_t result = POP();
            if (curry_profiling_level >= 2 && frame->prof_start_ns &&
                    frame->closure->chunk->name) {
                val_t sym = sym_intern_cstr(frame->closure->chunk->name);
                profiling_record_timed(sym, frame->prof_start_ns);
                frame->prof_start_ns = 0;
            }
            if (pop_frame(&frame, result)) return *--vm->sp;
            /* Nested vm_run: all our frames are done — return the result. */
            if (vm->frame_count == entry_depth) return *--vm->sp;
            NEXT;
        }

        /* ── Closures ────────────────────────────────────────────────── */
        CASE(OP_CLOSURE) {
            uint8_t ci  = READ_U8();
            Chunk *ch   = vunptr(Chunk, CONSTS[ci]);
            BcClosure *cl = vm_make_closure(ch, ch->upval_count);
            for (int i = 0; i < ch->upval_count; i++) {
                uint8_t is_local = READ_U8();
                uint8_t idx      = READ_U8();
                cl->upvals[i] = is_local
                    ? open_upvalue(&frame->slots[idx])
                    : frame->closure->upvals[idx];
            }
            PUSH(vptr(cl));
            NEXT;
        }

        CASE(OP_CLOSE_UP) {
            /* Close the open upvalue for frame->slots[A] (no stack pop).
               vm_close_upvalues closes all upvalues >= that address; since
               end_scope processes locals from highest slot down, any higher
               slots have already been closed before this is reached. */
            uint8_t slot = READ_U8();
            vm_close_upvalues(&frame->slots[slot]);
            NEXT;
        }

        CASE(OP_SLIDE) {
            /* Move TOS past A items below it (scope-exit cleanup).
               Stack before: [... | local0 | ... | localN-1 | result]
               Stack after:  [... | result]                           */
            uint8_t n = READ_U8();
            val_t result = POP();
            vm->sp -= n;
            PUSH(result);
            NEXT;
        }

        /* ── apply ───────────────────────────────────────────────────── */
        CASE(OP_APPLY) {
            /* Stack: [fn, arg1, …, argN-1, last-list]
               total = argc operand (includes fn + all args).
               Result: call fn with (append (list arg1..argN-1) last-list). */
            uint8_t total    = READ_U8();
            val_t *base      = vm->sp - total;
            val_t fn         = base[0];
            val_t last_list  = base[total - 1];
            int n_fixed      = (int)total - 2;   /* items between fn and last-list */

            /* Count tail list */
            int n_tail = 0;
            for (val_t p = last_list; vis_pair(p); p = vcdr(p)) n_tail++;

            int total_args = n_fixed + n_tail;
            /* Use a stack-local buffer for small calls; GC heap otherwise. */
            val_t buf[64];
            val_t *call_args = (total_args <= 64)
                ? buf
                : (val_t *)gc_alloc_raw_pinned((size_t)total_args * sizeof(val_t));

            for (int k = 0; k < n_fixed; k++) call_args[k] = base[1 + k];
            val_t lp = last_list;
            for (int k = 0; k < n_tail; k++) { call_args[n_fixed + k] = vcar(lp); lp = vcdr(lp); }

            vm->sp -= total;
            val_t result = call_foreign(fn, total_args, call_args);
            PUSH(result);
            NEXT;
        }

        /* ── Multiple values ─────────────────────────────────────────── */
        CASE(OP_VALUES) {
            uint8_t n = READ_U8();
            if (n == 1) { NEXT; }
            /* n == 0: push void (rarely needed but keep consistent) */
            if (n == 0) { PUSH(V_VOID); NEXT; }
            Values *mv = (Values *)gc_alloc(sizeof(Values) + (size_t)n * sizeof(val_t));
            mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = n;
            val_t *base = vm->sp - n;
            for (int i = 0; i < (int)n; i++) mv->vals[i] = base[i];
            vm->sp -= n;
            PUSH(vptr(mv));
            NEXT;
        }

        CASE(OP_CALL_WITH_VALUES) {
            val_t consumer = POP();
            val_t thunk    = POP();
            val_t produced = call_foreign(thunk, 0, NULL);
            val_t result;
            if (vis_values(produced)) {
                Values *mv = as_vals(produced);
                result = call_foreign(consumer, (int)mv->count, mv->vals);
            } else {
                val_t cargs[1] = {produced};
                result = call_foreign(consumer, 1, cargs);
            }
            PUSH(result);
            NEXT;
        }

        /* ── Exception handling ──────────────────────────────────────── */
        CASE(OP_PUSH_HANDLER) {
            /* Layout emitted by compile_with_exception_handler:
             *   <handler on stack>
             *   OP_PUSH_HANDLER catch_off   ← here; sp saved past handler
             *   <thunk on stack>
             *   OP_CALL 0                   ← runs thunk; result replaces it
             *   OP_POP_HANDLER
             *   OP_SWAP; OP_POP; OP_JUMP end
             *  catch_off:
             *   <exception pushed by recovery, handler still below>
             *   OP_CALL 1                   ← calls handler(exn)
             *  end: */
            uint16_t catch_off = READ_U16();
            int hi = vm->handler_count;
            if (hi >= VM_HANDLERS_MAX)
                scm_raise(V_FALSE, "handler stack overflow (max %d)", VM_HANDLERS_MAX);

            /* Save VM state so the catch block can restore it. */
            vm->handler_stack[hi].frame_count   = vm->frame_count;
            vm->handler_stack[hi].sp            = vm->sp;
            vm->handler_stack[hi].open_upvalues = vm->open_upvalues;
            vm->handler_stack[hi].catch_offset  = catch_off;
            vm->handler_stack[hi].frame_idx     = (int)(frame - vm->frames);
            vm->handler_count++;

            /* Link ExnHandler into the current_handler chain.  The jmp_buf
             * lives in vm_exn_handlers[] on this C frame (valid until vm_run
             * returns), so longjmp back here is always safe. */
            ExnHandler *eh = &vm_exn_handlers[hi];
            eh->exn  = V_FALSE;
            eh->prev = current_handler;
            current_handler = eh;

            if (setjmp(eh->jmp) != 0) {
                /* An exception was raised and caught by this handler.
                 * Restore VM state from the saved snapshot. */
                int chi = vm->handler_count - 1;
                val_t caught = vm_exn_handlers[chi].exn;
                current_handler   = vm_exn_handlers[chi].prev;
                vm->handler_count = chi;
                vm->frame_count   = vm->handler_stack[chi].frame_count;
                vm->sp            = vm->handler_stack[chi].sp;
                vm->open_upvalues = vm->handler_stack[chi].open_upvalues;
                frame = &vm->frames[vm->handler_stack[chi].frame_idx];
                PUSH(caught);
                JUMP_ABS(vm->handler_stack[chi].catch_offset);
                NEXT;
            }
            NEXT;
        }
        CASE(OP_POP_HANDLER) {
            /* Normal exit from a protected region — remove the handler. */
            if (vm->handler_count > 0) {
                vm->handler_count--;
                current_handler = vm_exn_handlers[vm->handler_count].prev;
            }
            NEXT;
        }
        CASE(OP_RAISE) {
            /* Raise TOS as an exception through the current_handler chain. */
            val_t exn = POP();
            scm_raise_val(exn);
            NEXT; /* unreachable */
        }

        /* ── I/O ─────────────────────────────────────────────────────── */
        CASE(OP_DISPLAY) {
            val_t v   = POP();
            val_t fn  = env_lookup(GLOBAL_ENV, sym_intern_cstr("display"));
            val_t args[1] = {v};
            call_foreign(fn, 1, args);
            PUSH(V_VOID);
            NEXT;
        }
        CASE(OP_WRITE) {
            val_t v   = POP();
            val_t fn  = env_lookup(GLOBAL_ENV, sym_intern_cstr("write"));
            val_t args[1] = {v};
            call_foreign(fn, 1, args);
            PUSH(V_VOID);
            NEXT;
        }
        CASE(OP_NEWLINE) {
            putchar('\n');
            fflush(stdout);
            PUSH(V_VOID);
            NEXT;
        }

#ifndef __GNUC__
        default:
            fprintf(stderr, "vm: unknown opcode %d at offset %d\n",
                    op, (int)(frame->ip - 1 - frame->closure->chunk->code));
            return V_VOID;
    } }  /* end switch; end for */
#endif

#undef READ_U8
#undef READ_U16
#undef JUMP_ABS
#undef CONSTS
#undef PUSH
#undef POP
#undef PEEK
}

/* ── vm_eval ─────────────────────────────────────────────────────────── */

/* Compile a single Scheme expression and immediately run it in the VM.
   The `env` argument is accepted for API compatibility with the tree-walker
   but is ignored; all global operations use GLOBAL_ENV. */
val_t vm_eval(val_t expr, val_t env) {
    (void)env;
    val_t cl_val  = compiler_compile(expr);
    BcClosure *cl = as_bcclosure(cl_val);
    return vm_run(cl, 0);
}
