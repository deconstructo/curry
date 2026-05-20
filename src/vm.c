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

#include "vm.h"
#include "profiling.h"
#include "compiler.h"
#include "chunk.h"
#include "opcode.h"
#include "value.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "symbol.h"
#include "env.h"
#include "eval.h"
#include "set.h"
#include "builtins.h"

/* ── Global VM state ─────────────────────────────────────────────────── */

_Thread_local VM *vm = NULL;

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void vm_init(void) {
    vm = CURRY_NEW(VM);
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
}

void vm_free(void) {
    vm = NULL;
}

void vm_reset(void) {
    vm->sp = vm->stack;
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
}

/* ── BcClosure allocation ────────────────────────────────────────────── */

BcClosure *vm_make_closure(Chunk *chunk, int nupvals) {
    BcClosure *cl = (BcClosure *)gc_alloc(
        sizeof(BcClosure) + (size_t)nupvals * sizeof(Upvalue *));
    cl->hdr.type    = T_BCCLOSURE;
    cl->hdr.flags   = 0;
    cl->chunk       = chunk;
    cl->upval_count = nupvals;
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

    Upvalue *n  = CURRY_NEW(Upvalue);
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
        up->closed   = *up->location;
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
#define PUSH(v)      do { \
    if (vm->sp >= vm->stack + VM_STACK_MAX) \
        scm_raise(V_FALSE, "value stack overflow (max %d slots)", VM_STACK_MAX); \
    *vm->sp++ = (v); } while (0)
#define POP()        (*--vm->sp)
#define PEEK(n)      (vm->sp[-1-(n)])

    for (;;) {
        uint8_t op = READ_U8();
        switch ((OpCode)op) {

        /* ── Constants ──────────────────────────────────────────────── */
        case OP_CONST:   PUSH(CONSTS[READ_U8()]);   break;
        case OP_CONST_W: PUSH(CONSTS[READ_U16()]);  break;
        case OP_TRUE:    PUSH(V_TRUE);               break;
        case OP_FALSE:   PUSH(V_FALSE);              break;
        case OP_NIL:     PUSH(V_NIL);                break;
        case OP_VOID:    PUSH(V_VOID);               break;

        /* ── Locals ─────────────────────────────────────────────────── */
        case OP_LOAD_LOCAL: {
            uint8_t i = READ_U8();
            PUSH(frame->slots[i]);
            break;
        }
        case OP_STORE_LOCAL: {
            uint8_t i = READ_U8();
            frame->slots[i] = POP();
            break;
        }

        /* ── Globals ─────────────────────────────────────────────────── */
        case OP_LOAD_GLOBAL: {
            val_t sym = CONSTS[READ_U8()];
            PUSH(env_lookup(GLOBAL_ENV, sym));
            break;
        }
        case OP_STORE_GLOBAL: {
            val_t sym = CONSTS[READ_U8()];
            val_t val = POP();
            if (!env_set(GLOBAL_ENV, sym, val)) {
                fprintf(stderr, "vm: set! unbound variable\n");
            }
            break;
        }
        case OP_DEF_GLOBAL: {
            val_t sym = CONSTS[READ_U8()];
            val_t val = POP();
            env_define(GLOBAL_ENV, sym, val);
            break;
        }

        /* ── Upvalues ───────────────────────────────────────────────── */
        case OP_LOAD_UP: {
            uint8_t i = READ_U8();
            PUSH(*frame->closure->upvals[i]->location);
            break;
        }
        case OP_STORE_UP: {
            uint8_t i = READ_U8();
            *frame->closure->upvals[i]->location = POP();
            break;
        }

        /* ── Stack manipulation ─────────────────────────────────────── */
        case OP_POP:  POP(); break;
        case OP_DUP:  { val_t v = PEEK(0); PUSH(v); break; }
        case OP_SWAP: { val_t a = POP(), b = POP(); PUSH(a); PUSH(b); break; }
        case OP_NOP:  break;

        /* ── Arithmetic ─────────────────────────────────────────────── */
        case OP_ADD: { val_t b = POP(), a = POP(); PUSH(num_add(a, b)); break; }
        case OP_SUB: { val_t b = POP(), a = POP(); PUSH(num_sub(a, b)); break; }
        case OP_MUL: { val_t b = POP(), a = POP(); PUSH(num_mul(a, b)); break; }
        case OP_DIV: { val_t b = POP(), a = POP(); PUSH(num_div(a, b)); break; }
        case OP_NEG: { PUSH(num_neg(POP())); break; }
        case OP_ABS: { PUSH(num_abs(POP())); break; }
        case OP_EXPT: { val_t b = POP(), a = POP(); PUSH(num_expt(a, b)); break; }

        /* ── Comparison ─────────────────────────────────────────────── */
        case OP_EQ:
        case OP_NUMEQ: { val_t b = POP(), a = POP(); PUSH(num_eq(a,b) ? V_TRUE:V_FALSE); break; }
        case OP_LT:    { val_t b = POP(), a = POP(); PUSH(num_lt(a,b) ? V_TRUE:V_FALSE); break; }
        case OP_LE:    { val_t b = POP(), a = POP(); PUSH(num_le(a,b) ? V_TRUE:V_FALSE); break; }
        case OP_GT:    { val_t b = POP(), a = POP(); PUSH(num_gt(a,b) ? V_TRUE:V_FALSE); break; }
        case OP_GE:    { val_t b = POP(), a = POP(); PUSH(num_ge(a,b) ? V_TRUE:V_FALSE); break; }

        /* ── Identity / equivalence ─────────────────────────────────── */
        case OP_EQV: {
            val_t b = POP(), a = POP();
            PUSH(scm_eqv(a, b) ? V_TRUE : V_FALSE);
            break;
        }
        case OP_EQUAL: {
            val_t b = POP(), a = POP();
            PUSH(scm_equal(a, b) ? V_TRUE : V_FALSE);
            break;
        }
        case OP_NOT: {
            PUSH(POP() == V_FALSE ? V_TRUE : V_FALSE);
            break;
        }

        /* ── Pairs ──────────────────────────────────────────────────── */
        case OP_CONS: { val_t d = POP(), a = POP(); PUSH(scm_cons(a, d)); break; }
        case OP_CAR:  { PUSH(vcar(POP())); break; }
        case OP_CDR:  { PUSH(vcdr(POP())); break; }
        case OP_SETCAR: {
            val_t v = POP(), p = POP();
            as_pair(p)->car = v;
            PUSH(V_VOID);
            break;
        }
        case OP_SETCDR: {
            val_t v = POP(), p = POP();
            as_pair(p)->cdr = v;
            PUSH(V_VOID);
            break;
        }
        case OP_NULLP: { PUSH(vis_nil(POP())  ? V_TRUE : V_FALSE); break; }
        case OP_PAIRP: { PUSH(vis_pair(POP()) ? V_TRUE : V_FALSE); break; }

        /* ── Strings / chars ─────────────────────────────────────────── */
        case OP_STRINGLEN: {
            /* Delegate: string-length returns char count, not byte length. */
            val_t v = POP();
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("string-length"));
            val_t args[1] = {v};
            PUSH(call_foreign(fn, 1, args));
            break;
        }
        case OP_STRINGREF: {
            /* UTF-8 indexing is complex; delegate to the existing builtin. */
            val_t args[2]; args[1] = POP(); args[0] = POP();
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("string-ref"));
            PUSH(call_foreign(fn, 2, args));
            break;
        }
        case OP_CHARTOFIX: { PUSH(vfix((intptr_t)vunchr(POP()))); break; }
        case OP_FIXTOCHAR: { PUSH(vchr((uint32_t)vunfix(POP()))); break; }

        /* ── Type predicates ─────────────────────────────────────────── */
        case OP_NUMBERP: { PUSH(vis_number(POP())              ? V_TRUE : V_FALSE); break; }
        case OP_STRINGP: { PUSH(vis_string(POP())              ? V_TRUE : V_FALSE); break; }
        case OP_SYMBOLP: { PUSH(vis_symbol(POP())              ? V_TRUE : V_FALSE); break; }
        case OP_CHARP:   { PUSH(vis_char(POP())                ? V_TRUE : V_FALSE); break; }
        case OP_BOOLP:   {
            val_t v = POP();
            PUSH((v == V_TRUE || v == V_FALSE)             ? V_TRUE : V_FALSE);
            break;
        }
        case OP_PROCP: {
            val_t v = POP();
            PUSH((vis_proc(v) || vis_bcclosure(v))         ? V_TRUE : V_FALSE);
            break;
        }
        case OP_VECTORP: { PUSH(vis_vector(POP())              ? V_TRUE : V_FALSE); break; }

        /* ── Vectors ─────────────────────────────────────────────────── */
        case OP_MAKEVEC: {
            uint8_t argc2 = READ_U8();
            val_t *args   = vm->sp - argc2;
            val_t fn = env_lookup(GLOBAL_ENV, sym_intern_cstr("make-vector"));
            val_t result = call_foreign(fn, (int)argc2, args);
            vm->sp -= argc2;
            PUSH(result);
            break;
        }
        case OP_VECREF: {
            val_t idx = POP(), vec = POP();
            PUSH(as_vec(vec)->data[vunfix(idx)]);
            break;
        }
        case OP_VECSET: {
            val_t val = POP(), idx = POP(), vec = POP();
            as_vec(vec)->data[vunfix(idx)] = val;
            PUSH(V_VOID);
            break;
        }
        case OP_VECLEN: {
            PUSH(vfix((intptr_t)as_vec(POP())->len));
            break;
        }

        /* ── Control flow ────────────────────────────────────────────── */
        case OP_JUMP: {
            uint16_t target = READ_U16();
            JUMP_ABS(target);
            break;
        }
        case OP_JUMP_FALSE: {
            uint16_t target = READ_U16();
            val_t cond = POP();
            if (cond == V_FALSE) JUMP_ABS(target);
            break;
        }
        case OP_JUMP_TRUE: {
            uint16_t target = READ_U16();
            val_t cond = POP();
            if (cond != V_FALSE) JUMP_ABS(target);
            break;
        }

        /* ── Calls ───────────────────────────────────────────────────── */
        case OP_CALL: {
            uint8_t argc2 = READ_U8();
            val_t callee  = PEEK(argc2);

            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
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
            break;
        }

        case OP_TAIL_CALL: {
            uint8_t argc2 = READ_U8();
            val_t callee  = PEEK(argc2);

            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
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
            break;
        }

        case OP_RETURN: {
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
            break;
        }

        /* ── Closures ────────────────────────────────────────────────── */
        case OP_CLOSURE: {
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
            break;
        }

        case OP_CLOSE_UP: {
            /* Close the open upvalue for frame->slots[A] (no stack pop).
               vm_close_upvalues closes all upvalues >= that address; since
               end_scope processes locals from highest slot down, any higher
               slots have already been closed before this is reached. */
            uint8_t slot = READ_U8();
            vm_close_upvalues(&frame->slots[slot]);
            break;
        }

        case OP_SLIDE: {
            /* Move TOS past A items below it (scope-exit cleanup).
               Stack before: [... | local0 | ... | localN-1 | result]
               Stack after:  [... | result]                           */
            uint8_t n = READ_U8();
            val_t result = POP();
            vm->sp -= n;
            PUSH(result);
            break;
        }

        /* ── apply ───────────────────────────────────────────────────── */
        case OP_APPLY: {
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
                : (val_t *)gc_alloc((size_t)total_args * sizeof(val_t));

            for (int k = 0; k < n_fixed; k++) call_args[k] = base[1 + k];
            val_t lp = last_list;
            for (int k = 0; k < n_tail; k++) { call_args[n_fixed + k] = vcar(lp); lp = vcdr(lp); }

            vm->sp -= total;
            val_t result = call_foreign(fn, total_args, call_args);
            PUSH(result);
            break;
        }

        /* ── Multiple values ─────────────────────────────────────────── */
        case OP_VALUES: {
            uint8_t n = READ_U8();
            if (n == 1) break;
            /* n == 0: push void (rarely needed but keep consistent) */
            if (n == 0) { PUSH(V_VOID); break; }
            Values *mv = (Values *)gc_alloc(sizeof(Values) + (size_t)n * sizeof(val_t));
            mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = n;
            val_t *base = vm->sp - n;
            for (int i = 0; i < (int)n; i++) mv->vals[i] = base[i];
            vm->sp -= n;
            PUSH(vptr(mv));
            break;
        }

        case OP_CALL_WITH_VALUES: {
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
            break;
        }

        /* ── Exception handling ──────────────────────────────────────── */
        case OP_PUSH_HANDLER: {
            READ_U16(); /* reserved — full handler integration pending */
            break;
        }
        case OP_POP_HANDLER: break;
        case OP_RAISE: {
            /* Route through the tree-walker's exception machinery. */
            val_t exn  = POP();
            val_t rfn  = env_lookup(GLOBAL_ENV, sym_intern_cstr("raise"));
            val_t args[1] = {exn};
            call_foreign(rfn, 1, args);
            break; /* unreachable if raise unwinds */
        }

        /* ── I/O ─────────────────────────────────────────────────────── */
        case OP_DISPLAY: {
            val_t v   = POP();
            val_t fn  = env_lookup(GLOBAL_ENV, sym_intern_cstr("display"));
            val_t args[1] = {v};
            call_foreign(fn, 1, args);
            PUSH(V_VOID);
            break;
        }
        case OP_WRITE: {
            val_t v   = POP();
            val_t fn  = env_lookup(GLOBAL_ENV, sym_intern_cstr("write"));
            val_t args[1] = {v};
            call_foreign(fn, 1, args);
            PUSH(V_VOID);
            break;
        }
        case OP_NEWLINE: {
            putchar('\n');
            fflush(stdout);
            PUSH(V_VOID);
            break;
        }

        default:
            fprintf(stderr, "vm: unknown opcode %d at offset %d\n",
                    op, (int)(frame->ip - 1 - frame->closure->chunk->code));
            return V_VOID;
        }
    }

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
