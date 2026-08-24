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
 *   Global variable operations (OP_LOAD_GLOBAL, OP_STORE_GLOBAL,
 *   OP_DEF_GLOBAL) operate on each chunk's own target environment (the
 *   TARGET_ENV macro below, reading Chunk::target_env), which defaults to
 *   GLOBAL_ENV -- every chunk compiled without an explicit
 *   compiler_set_target_env() call gets GLOBAL_ENV, the same environment
 *   the tree-walker uses for ordinary top-level code, keeping one shared
 *   namespace there. A define-library body compiled against its own
 *   isolated env_new_root() frame instead is the one exception: see
 *   chunk.h's own comment on Chunk::target_env.
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
 *   Global variables (constant pool holds the symbol; TARGET_ENV is
 *   GLOBAL_ENV unless the chunk was compiled with an explicit target env)
 *     OP_LOAD_GLOBAL  A   push TARGET_ENV[constants[A]]
 *     OP_STORE_GLOBAL A   TARGET_ENV[constants[A]] = pop()   (set! — symbol must exist)
 *     OP_DEF_GLOBAL   A   TARGET_ENV[constants[A]] = pop()   (define — creates binding)
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
 *   Pairs & lists (OP_CONS/OP_CAR/OP_CDR/OP_NULLP/OP_PAIRP: Tier 2.5
 *   open-coding, not the unconditional 0-operand ops this summary once
 *   described -- each takes a const-pool operand and only takes the fast
 *   path shown below if that global hasn't been redefined since compile
 *   time; see opcode.h's own comment on this opcode group for the full
 *   redefinition-safety design, and never open-coded at all in tail
 *   position -- see compiler.c's own emission-site comment for why)
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
#include <stdatomic.h>

#include "vm.h"
#include "debug.h"
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
    scm_raise_code(EC_STACK_OVERFLOW, "value stack overflow (max %d slots)", VM_STACK_MAX);
}

static val_t vm_cstr_to_string(const char *s) {
    if (!s) return V_FALSE;
    uint32_t len = (uint32_t)strlen(s);
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type = T_STRING; str->hdr.flags = 0;
    str->len = len; str->hash = 0; str->orig_cap = len; str->ext = NULL;
    memcpy(str->data, s, len + 1);
    return vptr(str);
}

val_t vm_capture_backtrace(void) {
    if (!vm) return V_NIL;
    val_t bt = V_NIL;
    for (int i = 0; i < vm->frame_count; i++) {
        CallFrame *fr = &vm->frames[i];
        Chunk *ch = fr->closure->chunk;
        int off = (int)(fr->ip - ch->code);
        int line = 0;
        bool have_line = ch->lines && ch->code_len > 0;
        if (have_line) {
            int idx = off - 1;
            if (idx < 0) idx = 0;
            if (idx >= ch->code_len) idx = ch->code_len - 1;
            line = ch->lines[idx];
        }
        val_t name = vm_cstr_to_string(ch->name);
        val_t file = vm_cstr_to_string(ch->source_name);
        val_t lineval = have_line ? vfix(line) : V_FALSE;
        val_t frame = scm_cons(name, scm_cons(file, scm_cons(lineval, V_NIL)));
        bt = scm_cons(frame, bt);
    }
    return bt;
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

/* See the SCM_PROTECT / ExnHandler comment in eval.h: this is the partial,
 * dynamic-extent-scoped counterpart to vm_reset()'s full reset-to-base — it
 * rewinds only what the protected region itself pushed, so it's safe to call
 * from a nested guard/with-exception-handler without discarding legitimate
 * outer VM frames. */
void vm_exn_state_save(int *frame_count, void **sp, void **open_upvalues) {
    if (!vm) { *frame_count = 0; *sp = NULL; *open_upvalues = NULL; return; }
    *frame_count = vm->frame_count;
    *sp = (void *)vm->sp;
    *open_upvalues = (void *)vm->open_upvalues;
}

void vm_exn_state_restore(int frame_count, void *sp, void *open_upvalues) {
    if (!vm) return;
    vm->frame_count = frame_count;
    vm->sp = (val_t *)sp;
    vm->open_upvalues = (Upvalue *)open_upvalues;
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
    if (vm_debug_active) return;  /* JIT'd code bypasses breakpoints */
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

/* See vm.h for why this exists (actor_spawn's cross-thread upvalue race).
 *
 * This does NOT close `bc`'s upvalues in place — an earlier version of
 * this fix did, and review caught a real regression: an open Upvalue is
 * shared (by pointer) by every closure that captured the same variable
 * from the same still-live scope, e.g. two sibling closures over one
 * named-let loop variable, or the enclosing frame's own OP_LOAD_LOCAL/
 * OP_STORE_LOCAL access to that exact stack slot. Force-closing the
 * shared object in place freezes the value for ALL of them the instant
 * one of them escapes to spawn — not just the one being spawned — which
 * is observably wrong for anything still running in the spawning thread:
 * confirmed with a repro where a sibling closure sharing the spawned
 * closure's loop variable, mutated and re-read after the spawn call,
 * silently saw the pre-spawn value instead of the mutation. vm_close_upvalues
 * itself never has this problem because it only ever runs at genuine scope
 * exit, when the local slot will provably never be read again by anyone.
 *
 * Instead, this builds an independent COPY of `bc` — same chunk, but with
 * every upvalue slot replaced by a fresh, already-closed snapshot of its
 * current value. The original closure object (and everything else that
 * may still reference its shared, still-open upvalues) is left completely
 * untouched, so same-thread sharers keep observing the live, mutable
 * variable exactly as they always have, right up to its normal scope-exit
 * close. Only the actor's private copy is frozen — snapshotting even an
 * already-closed upvalue too, rather than sharing it, since a "closed"
 * Upvalue can still be mutated later via OP_STORE_UP through any other
 * closure that shares it, which would otherwise still be an unsynchronized
 * cross-thread write/read race on that shared object. */
BcClosure *vm_snapshot_closure_for_escape(BcClosure *bc) {
    BcClosure *copy = vm_make_closure(bc->chunk, bc->upval_count);
    for (int i = 0; i < bc->upval_count; i++) {
        Upvalue *up  = bc->upvals[i];
        Upvalue *snap = CURRY_NEW_PINNED(Upvalue);
        snap->hdr.type = T_UPVALUE; snap->hdr.flags = 0; snap->hdr.fwd = 0;
        snap->closed = *up->location;
        snap->location = &snap->closed;
        snap->next = NULL;
        copy->upvals[i] = snap;
    }
    return copy;
}

/* ── Foreign call helper ─────────────────────────────────────────────── */

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
    /* Remove callee (if this frame's calling convention reserved one --
     * see CallFrame::has_callee_slot's own comment, vm.h) + args. */
    vm->sp = frame->slots - (frame->has_callee_slot ? 1 : 0);
    *vm->sp++ = result;
    *frame_ptr = &vm->frames[vm->frame_count - 1];
    return false;
}

/*
 * Validate argc against cl's declared arity, raising
 * EC_WRONG_NUMBER_OF_ARGUMENTS on mismatch. Fixed-arity (chunk->arity >= 0):
 * argc must equal arity exactly. Variadic (chunk->arity < 0, meaning
 * `fixed = -arity-1` required params plus one rest-list param): argc must
 * be >= fixed. Does not touch the stack or build the rest-arg list —
 * callers that need that done (the bytecode interpreter's calling
 * convention, see vm_bind_args below) do it themselves; the LLVM JIT's
 * generated function prologue does it internally via curry_jit_list_tail
 * once this check has confirmed argc is valid (src/llvm/codegen.cpp
 * emit_lambda, src/llvm/jit.cpp curry_jit_list_tail).
 *
 * Shared by vm_bind_args and every JIT-fast-path call site (apply_arr in
 * eval.c, and the vis_jitclosure branches of OP_CALL/OP_TAIL_CALL below) —
 * before this existed, JIT-compiled closures had no arity check at all,
 * same bug class as the bytecode interpreter, fixed the same way.
 */
void vm_check_arity(BcClosure *cl, int argc) {
    int arity = cl->chunk->arity;
    const char *nm = cl->chunk->name ? cl->chunk->name : "#<procedure>";
    if (arity >= 0) {
        if (argc < arity)
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                "%s: too few arguments (got %d, need %d)", nm, argc, arity);
        if (argc > arity)
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
                "%s: too many arguments (got %d, need %d)", nm, argc, arity);
        return;
    }
    int fixed = -arity - 1;
    if (argc < fixed)
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS,
            "%s: too few arguments (got %d, need at least %d)", nm, argc, fixed);
}

/*
 * Validate (via vm_check_arity) and, for variadic closures, repack the
 * trailing arguments into a proper list — the bytecode interpreter's
 * calling convention, where arguments live as raw values in the callee's
 * local-variable window on vm->stack.
 *
 * `base` points at the `argc` already-pushed argument values (the callee's
 * about-to-be local-variable window). For a fixed-arity closure this
 * leaves the stack untouched. For a variadic closure the trailing
 * `argc - fixed` values at base[fixed..argc) are consed into a list stored
 * at base[fixed], and vm->sp is truncated to base+fixed+1 — the exact
 * local-window size compile_params() declared.
 *
 * Without this, OP_CALL/OP_TAIL_CALL/vm_run previously just set
 * slots=base, slot_count=argc unconditionally: a fixed-arity closure called
 * with the wrong argc silently read uninitialized stack slots for missing
 * params (or left extra args as live but unreferenced stack cells), and a
 * variadic closure's rest parameter ended up holding the raw last-pushed
 * argument instead of a list.
 *
 * Returns the slot_count to store in the CallFrame.
 */
static int vm_bind_args(BcClosure *cl, val_t *base, int argc) {
    vm_check_arity(cl, argc);
    int arity = cl->chunk->arity;
    if (arity >= 0) return argc;
    int fixed = -arity - 1;
    val_t rest = V_NIL;
    for (int i = argc - 1; i >= fixed; i--) rest = scm_cons(base[i], rest);
    base[fixed] = rest;
    vm->sp = base + fixed + 1;
    return fixed + 1;
}

/* Shared cached global-variable lookup for OP_CALL_GLOBAL/
 * OP_TAIL_CALL_GLOBAL, which need this exact lookup fused with a call
 * rather than as a separate dispatch. Mirrors (does not replace)
 * OP_LOAD_GLOBAL's own inline copy below -- deliberately NOT refactored
 * to share one implementation, since OP_LOAD_GLOBAL's copy carries a
 * detailed comment about the acquire/release memory-ordering contract
 * with env.c's seqlock and is exactly the kind of already-reviewed,
 * concurrency-sensitive code not worth touching for a de-dup that isn't
 * otherwise necessary. Takes `chunk` explicitly (rather than the frame-
 * local CONSTS/GCACHE/TARGET_ENV macros below) so it reads as a plain,
 * self-contained function. Raises unbound-variable the same way. */
static inline val_t load_global_cached(Chunk *chunk, uint8_t ci) {
    GlobCacheEntry *cache = chunk->glob_cache;
    EnvFrame *root = as_env(chunk->target_env == V_VOID ? GLOBAL_ENV : chunk->target_env);
    uint32_t root_ver = atomic_load_explicit((_Atomic uint32_t *)&root->version, memory_order_acquire);
    if (__builtin_expect(cache != NULL && cache[ci].slot != NULL &&
                         cache[ci].version == root_ver, 1)) {
        return *cache[ci].slot;
    }
    val_t sym = chunk->constants[ci];
    uint32_t ver;
    val_t *slot = frame_lookup_versioned(root, sym, &ver);
    if (!slot) scm_raise_code(EC_UNBOUND_VARIABLE, "unbound variable: %s", sym_cstr(sym));
    if (cache) { cache[ci].slot = slot; cache[ci].version = ver; }
    return *slot;
}

/* Tier 2.5 step 1: shared fast path for every open-coded 1-argument
 * primitive opcode (OP_CAR/OP_CDR/OP_NULLP/OP_PAIRP) -- see opcode.h's
 * own comment on this whole opcode group for the full redefinition-
 * safety design this implements. `expected` is the specific prim_* this
 * call site was compiled for (compiler.c only ever emits one of these
 * opcodes when it already knows which primitive it's checking against);
 * calling THROUGH that same function pointer on a match, rather than a
 * per-opcode hardcoded call, is what lets one helper serve all four
 * opcodes instead of four near-identical copies (flagged by independent
 * code review). */
static inline val_t open_coded_unary1(Chunk *chunk, uint8_t ci, val_t x, PrimFn expected) {
    val_t current = load_global_cached(chunk, ci);
    if (__builtin_expect(vis_prim(current) && as_prim(current)->fn == expected, 1))
        return expected(1, &x, NULL);
    return call_foreign(current, 1, &x);
}

/* Tier 2.5 step 2: same shape as open_coded_unary1 above, for the
 * 2-argument open-coded arithmetic/comparison opcodes (OP_ADD/OP_SUB/
 * OP_MUL/OP_NUMEQ/OP_LT/OP_LE/OP_GT/OP_GE). Calls THROUGH `expected`
 * (the real prim_add/prim_num_lt/etc.) on a match rather than any
 * separate inline fast-path logic -- see builtins.h's own comment on
 * these opcodes' declarations for why that's required for correctness
 * (prim_num_lt/le/gt/ge's own 2-arg case dispatches to sx_lt/sx_le/
 * sx_gt/sx_ge for a symbolic operand, which a hand-rolled fixnum-or-
 * num_lt fast path would not replicate). */
static inline val_t open_coded_binary2(Chunk *chunk, uint8_t ci, val_t a, val_t b, PrimFn expected) {
    val_t current = load_global_cached(chunk, ci);
    val_t args2[2]; args2[0] = a; args2[1] = b;
    if (__builtin_expect(vis_prim(current) && as_prim(current)->fn == expected, 1))
        return expected(2, args2, NULL);
    return call_foreign(current, 2, args2);
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
        scm_raise_code(EC_STACK_OVERFLOW, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
    CallFrame *frame  = &vm->frames[vm->frame_count++];
    frame->closure    = top_closure;
    frame->has_callee_slot = true;  /* irrelevant in practice: pop_frame's
                                        frame_count==0 path never reads it
                                        for the outermost frame, set for
                                        consistency anyway */
    frame->ip         = top_closure->chunk->code;
    frame->slots      = vm->sp - argc;
    frame->slot_count = vm_bind_args(top_closure, frame->slots, argc);

#define READ_U8()    (*frame->ip++)
#define READ_U16()   (frame->ip += 2, \
                      (uint16_t)(frame->ip[-2] | ((unsigned)frame->ip[-1] << 8)))
#define JUMP_ABS(t)  (frame->ip = frame->closure->chunk->code + (t))
#define CONSTS       (frame->closure->chunk->constants)
#define GCACHE       (frame->closure->chunk->glob_cache)
/* V_VOID means "this chunk was compiled without an explicit target env"
 * (every chunk before this field existed, and every ordinary top-level/
 * REPL/script compile today) -- falls back to GLOBAL_ENV. See chunk.h's
 * own comment on Chunk::target_env. */
#define TARGET_ENV   (frame->closure->chunk->target_env == V_VOID \
                        ? GLOBAL_ENV : frame->closure->chunk->target_env)
#define PUSH(v)      do { \
    if (vm->sp >= vm->stack + VM_STACK_MAX) \
        scm_raise_code(EC_STACK_OVERFLOW, "value stack overflow (max %d slots)", VM_STACK_MAX); \
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
        [OP_DEF_GLOBAL]  = &&L_OP_DEF_GLOBAL, [OP_DEFINED_GLOBAL] = &&L_OP_DEFINED_GLOBAL,
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
        [OP_SELF_TAIL_CALL]   = &&L_OP_SELF_TAIL_CALL,
        [OP_CALL_GLOBAL]      = &&L_OP_CALL_GLOBAL,
        [OP_TAIL_CALL_GLOBAL] = &&L_OP_TAIL_CALL_GLOBAL,
        [OP_TREE_EVAL_CACHED] = &&L_OP_TREE_EVAL_CACHED,
        [OP_CLOSURE]     = &&L_OP_CLOSURE,     [OP_CLOSE_UP]     = &&L_OP_CLOSE_UP,
        [OP_APPLY]       = &&L_OP_APPLY,       [OP_VALUES]       = &&L_OP_VALUES,
        [OP_VALUES_REF]  = &&L_OP_VALUES_REF,
        [OP_CALL_WITH_VALUES] = &&L_OP_CALL_WITH_VALUES,
        [OP_TAIL_CALL_WITH_VALUES] = &&L_OP_TAIL_CALL_WITH_VALUES,
        [OP_PUSH_HANDLER]= &&L_OP_PUSH_HANDLER,[OP_POP_HANDLER]  = &&L_OP_POP_HANDLER,
        [OP_RAISE]       = &&L_OP_RAISE,
        [OP_DISPLAY]     = &&L_OP_DISPLAY,     [OP_WRITE]        = &&L_OP_WRITE,
        [OP_NEWLINE]     = &&L_OP_NEWLINE,     [OP_NOP]          = &&L_OP_NOP,
    };
#   define CASE(op)  L_##op:
#   define NEXT      goto L_DISPATCH
    goto L_DISPATCH;  /* prime the pump */
#else /* no computed goto */
#   define CASE(op)  case op:
#   define NEXT      break
    for (;;) {
        if (__builtin_expect(gc_minor_pending, 0) && gc_inhibit_count == 0) {
            gc_minor_pending = false;
            extern void gc_gen_minor_collect(void);
            gc_gen_minor_collect();
        }
        if (__builtin_expect(vm_debug_active, 0)) vm_debug_hook(frame);
        switch (READ_U8()) {
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
            EnvFrame *root = as_env(TARGET_ENV);
            /* Acquire load: pairs with the release store in env.c's
             * seq_end_write, so a match against cache[ci].version here
             * guarantees cache[ci].slot (stamped from the same
             * frame_lookup_versioned call that validated that version) is
             * still a slot in the CURRENT root->vals, not a stale one from
             * before a since-completed frame_grow. See env.c for why an
             * unsynchronized global-environment frame is unsafe when actor
             * threads share it. */
            uint32_t root_ver = atomic_load_explicit((_Atomic uint32_t *)&root->version, memory_order_acquire);
            val_t loaded_gval;
            if (__builtin_expect(cache != NULL && cache[ci].slot != NULL &&
                                 cache[ci].version == root_ver, 1)) {
                loaded_gval = *cache[ci].slot;
            } else {
                val_t sym = CONSTS[ci];
                uint32_t ver;
                val_t *slot = frame_lookup_versioned(root, sym, &ver);
                if (!slot) scm_raise_code(EC_UNBOUND_VARIABLE, "unbound variable: %s", sym_cstr(sym));
                if (cache) { cache[ci].slot = slot; cache[ci].version = ver; }
                loaded_gval = *slot;
            }
            PUSH(loaded_gval);
            NEXT;
        }
        CASE(OP_STORE_GLOBAL) {
            uint8_t ci = READ_U8();
            val_t val = POP();
            GlobCacheEntry *cache = GCACHE;
            EnvFrame *root = as_env(TARGET_ENV);
            uint32_t root_ver = atomic_load_explicit((_Atomic uint32_t *)&root->version, memory_order_acquire);
            if (__builtin_expect(cache != NULL && cache[ci].slot != NULL &&
                                 cache[ci].version == root_ver, 1)) {
                gc_wb_slot(cache[ci].slot, val);
            } else {
                val_t sym = CONSTS[ci];
                uint32_t ver;
                val_t *slot = frame_lookup_versioned(root, sym, &ver);
                if (!slot) fprintf(stderr, "vm: set! unbound variable\n");
                else {
                    gc_wb_slot(slot, val);
                    if (cache) { cache[ci].slot = slot; cache[ci].version = ver; }
                }
            }
            NEXT;
        }
        CASE(OP_DEF_GLOBAL) {
            uint8_t ci = READ_U8();
            val_t sym = CONSTS[ci];
            val_t val = POP();
            env_define(TARGET_ENV, sym, val);
            /* Fill cache after define, using the (slot, version) pair from
             * one atomic frame_lookup_versioned call rather than fetching
             * the slot and then separately re-reading root->version — the
             * latter leaves a window where a second, concurrent define (on
             * another actor thread) could grow the frame again in between,
             * stamping this slot (valid for the generation env_define just
             * produced) with a newer version than the one it's actually
             * from, which would make every future cache hit trust it
             * regardless of whether it's since gone stale. */
            GlobCacheEntry *cache = GCACHE;
            if (cache) {
                EnvFrame *root = as_env(TARGET_ENV);
                uint32_t ver;
                val_t *slot = frame_lookup_versioned(root, sym, &ver);
                if (slot) { cache[ci].slot = slot; cache[ci].version = ver; }
            }
            NEXT;
        }
        CASE(OP_DEFINED_GLOBAL) {
            /* Non-raising counterpart to OP_LOAD_GLOBAL (see opcode.h's
             * own comment) -- same TARGET_ENV-aware lookup, no inline
             * cache (not a hot path), pushes #t/#f instead of raising or
             * pushing the value itself. */
            uint8_t ci = READ_U8();
            val_t sym = CONSTS[ci];
            EnvFrame *root = as_env(TARGET_ENV);
            uint32_t ver;
            val_t *slot = frame_lookup_versioned(root, sym, &ver);
            PUSH(slot ? V_TRUE : V_FALSE);
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
        /* Tier 2.5 step 2: open-coded +, -, * -- see opcode.h's own comment
         * on this opcode group, and builtins.h's comment on prim_add/
         * prim_sub/prim_mul, for the full design (same redefinition-
         * safety guard as OP_CAR/OP_CDR/OP_CONS above, via
         * open_coded_binary2). Never emitted for a tail call -- same
         * reason as OP_CAR/OP_CDR/OP_CONS, see compiler.c's emission-site
         * comment. */
        CASE(OP_ADD) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_add));
            NEXT;
        }
        CASE(OP_SUB) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_sub));
            NEXT;
        }
        CASE(OP_MUL) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_mul));
            NEXT;
        }
        CASE(OP_DIV) { val_t b = POP(), a = POP(); PUSH(num_div(a, b)); NEXT; }
        CASE(OP_NEG) { PUSH(num_neg(POP())); NEXT; }
        CASE(OP_ABS) { PUSH(num_abs(POP())); NEXT; }
        CASE(OP_EXPT) { val_t b = POP(), a = POP(); PUSH(num_expt(a, b)); NEXT; }

        /* ── Comparison ─────────────────────────────────────────────── */
        /* OP_EQ: genuinely unemitted dead code (see opcode.h), left with
         * its old 0-operand behavior -- deliberately NOT merged with
         * OP_NUMEQ below anymore (they used to share this body via
         * fallthrough; separated so OP_NUMEQ could take the new operand
         * without changing OP_EQ's contract). */
        CASE(OP_EQ) {
            val_t b = POP(), a = POP();
            if (__builtin_expect(vis_fixnum(a) & vis_fixnum(b), 1))
                PUSH(a == b ? V_TRUE : V_FALSE);
            else
                PUSH(num_eq(a, b) ? V_TRUE : V_FALSE);
            NEXT;
        }
        /* Tier 2.5 step 2: open-coded =/</<=/>/>= -- see the arithmetic
         * group's own comment just above for the design; open_coded_binary2
         * is what makes this safe for symbolic operands (see builtins.h). */
        CASE(OP_NUMEQ) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_num_eq));
            NEXT;
        }
        CASE(OP_LT) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_num_lt));
            NEXT;
        }
        CASE(OP_LE) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_num_le));
            NEXT;
        }
        CASE(OP_GT) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_num_gt));
            NEXT;
        }
        CASE(OP_GE) {
            uint8_t ci = READ_U8();
            val_t b = POP(), a = POP();
            PUSH(open_coded_binary2(frame->closure->chunk, ci, a, b, prim_num_ge));
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
        /* Tier 2.5 step 1: open-coded cons -- see opcode.h's own comment
         * on this group of opcodes for the full redefinition-safety
         * design. On the fast path this keeps the args on the REAL VM
         * stack and allocates directly from it, unchanged from before
         * this landing (gc_alloc may trigger a collection; args still on
         * vm->sp are found by it same as always). Only the rare mismatch
         * (redefined) path pops into C locals to call_foreign -- the same
         * "pass the address of a C-stack val_t" pattern OP_CAR/OP_CDR's
         * own fallback already uses, safe for the same reason (Boehm's
         * conservative stack scan covers it same as any other C local). */
        CASE(OP_CONS) {
            uint8_t ci = READ_U8();
            val_t current = load_global_cached(frame->closure->chunk, ci);
            if (__builtin_expect(vis_prim(current) && as_prim(current)->fn == prim_cons, 1)) {
                /* Allocate first: args still on VM stack so GC (if
                 * triggered) can update them. Re-read from stack after
                 * allocation. */
                Pair *p = (Pair *)gc_alloc(sizeof(Pair));
                p->hdr.type = T_PAIR; p->hdr.flags = 0;
                p->car = vm->sp[-2]; p->cdr = vm->sp[-1];
                vm->sp -= 2; *vm->sp++ = vptr(p);
            } else {
                val_t args2[2];
                args2[1] = POP(); args2[0] = POP();
                PUSH(call_foreign(current, 2, args2));
            }
            NEXT;
        }
        /* Tier 2.5 step 1: open-coded car/cdr/null?/pair? -- see opcode.h's
         * own comment on this whole opcode group for the full
         * redefinition-safety design. Each CASE below is a thin wrapper
         * around open_coded_unary1 (this file, just above the dispatch
         * loop) -- collapsed into one shared helper (flagged by
         * independent code review: four near-identical 9-line blocks
         * differing only in which prim_* to compare/call) parameterized
         * on the specific primitive's own function pointer, so a future
         * fix to the guard logic only needs to change one place. Never
         * emitted at all for a call already known to be in tail position
         * -- see compiler.c's own emission-site comment for why: this
         * opcode's fallback (redefined-to-a-BcClosure) path always goes
         * through call_foreign, which recurses via apply_arr -> a nested
         * vm_run() rather than reusing the current CallFrame the way
         * OP_TAIL_CALL/OP_TAIL_CALL_GLOBAL do, so open-coding a genuine
         * tail call here would silently break proper tail-call
         * optimization for a redefined name called recursively -- a real,
         * confirmed regression caught by independent code review before
         * this comment existed, fixed by restricting open-coding to
         * non-tail call sites entirely rather than teaching this opcode
         * its own frame-reuse path. */
        CASE(OP_CAR) {
            uint8_t ci = READ_U8();
            val_t x = POP();
            PUSH(open_coded_unary1(frame->closure->chunk, ci, x, prim_car));
            NEXT;
        }
        CASE(OP_CDR) {
            uint8_t ci = READ_U8();
            val_t x = POP();
            PUSH(open_coded_unary1(frame->closure->chunk, ci, x, prim_cdr));
            NEXT;
        }
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
        CASE(OP_NULLP) {
            uint8_t ci = READ_U8();
            val_t x = POP();
            PUSH(open_coded_unary1(frame->closure->chunk, ci, x, prim_null_p));
            NEXT;
        }
        CASE(OP_PAIRP) {
            uint8_t ci = READ_U8();
            val_t x = POP();
            PUSH(open_coded_unary1(frame->closure->chunk, ci, x, prim_pair_p));
            NEXT;
        }

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
                    && curry_profiling_level == 0 && !vm_debug_active) {
                    vm_check_arity(cl, argc2);
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    /* Inhibit minor GC for the duration of the JIT call.
                     * JIT'd code holds intermediate nursery pointers in registers
                     * and alloca slots that the GC cannot enumerate without full
                     * stackmap support.  While inhibited, nursery overflow falls
                     * back to Boehm rather than moving objects under JIT code. */
                    gc_inhibit_minor();
                    val_t result = ((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc2,
                                                                         (uint64_t *)call_args,
                                                                         (uint64_t *)jc->caps);
                    gc_resume_minor();
                    g_jit_call_depth--;
                    vm->sp -= argc2 + 1;
                    PUSH(result);
                    NEXT;
                }
                vm_maybe_jit(cl);
                if (vm->frame_count >= VM_FRAMES_MAX)
                    scm_raise_code(EC_STACK_OVERFLOW, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
                CallFrame *nf  = &vm->frames[vm->frame_count++];
                nf->closure    = cl;
                nf->has_callee_slot = true;  /* PEEK(argc2) above found the
                                                 callee already on the
                                                 stack below the args */
                nf->ip         = cl->chunk->code;
                nf->slots      = vm->sp - argc2;
                nf->slot_count = vm_bind_args(cl, nf->slots, argc2);
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
                    && curry_profiling_level == 0 && !vm_debug_active) {
                    vm_check_arity(cl, argc2);
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    gc_inhibit_minor();
                    val_t result = ((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc2,
                                                                         (uint64_t *)call_args,
                                                                         (uint64_t *)jc->caps);
                    gc_resume_minor();
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
                frame->slot_count = vm_bind_args(cl, frame->slots, argc2);
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

        CASE(OP_SELF_TAIL_CALL) {
            /* Tail-call the CURRENTLY EXECUTING closure -- the compiler
             * only ever emits this when it has proven (compile_call,
             * compiler.c) the callee is always this exact closure (a
             * named-let loop's own private, never-set!'d upvalue self-
             * reference), so no PEEK/vis_bcclosure branch is needed at
             * all: cl is definitionally frame->closure, and there is no
             * callee slot on the stack to skip past when reading args or
             * adjusting sp -- unlike OP_TAIL_CALL, argc2 args are the
             * ENTIRE stack contribution of this call. */
            uint8_t argc2 = READ_U8();
            BcClosure *cl = frame->closure;
            /* Tiered JIT: hot-swap to native code once compiled. jit_val
             * is dynamic per-closure state a hot self-recursive loop's
             * own iterations can cross the promotion threshold during --
             * unlike callee identity (statically proven), jit_val is NOT
             * statically known and must still be checked every
             * iteration. */
            if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
                && curry_profiling_level == 0 && !vm_debug_active) {
                vm_check_arity(cl, argc2);
                JitClosure *jc = as_jitclos(cl->jit_val);
                val_t *call_args = vm->sp - argc2;
                typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                g_jit_call_depth++;
                gc_inhibit_minor();
                val_t result = ((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc2,
                                                                     (uint64_t *)call_args,
                                                                     (uint64_t *)jc->caps);
                gc_resume_minor();
                g_jit_call_depth--;
                vm->sp -= argc2;
                if (pop_frame(&frame, result)) return *--vm->sp;
                if (vm->frame_count == entry_depth) return *--vm->sp;
                NEXT;
            }
            vm_maybe_jit(cl);
            if (curry_profiling_level >= 2 && frame->prof_start_ns &&
                    frame->closure->chunk->name) {
                val_t sym = sym_intern_cstr(frame->closure->chunk->name);
                profiling_record_timed(sym, frame->prof_start_ns);
            }
            /* Not counted as a cross-function tail call -- matches
             * OP_TAIL_CALL's own "not self-tail-call loops" comment. */
            val_t *new_args = vm->sp - argc2;
            vm_close_upvalues(frame->slots);
            memmove(frame->slots, new_args, argc2 * sizeof(val_t));
            vm->sp            = frame->slots + argc2;
            frame->ip         = cl->chunk->code;
            frame->slot_count = vm_bind_args(cl, frame->slots, argc2);
            frame->prof_start_ns = 0;
            if (curry_profiling_level >= 2 && cl->chunk->name)
                frame->prof_start_ns = profiling_now_ns();
            NEXT;
        }

        CASE(OP_CALL_GLOBAL) {
            /* Fused OP_LOAD_GLOBAL + OP_CALL: one dispatch instead of two
             * for the most common call shape (calling a plain top-level
             * function), with NO callee slot pushed at all -- unlike an
             * earlier version of this opcode, which inserted the looked-
             * up callee back onto the stack via memmove to match OP_CALL's
             * layout. That worked but measured no real performance win
             * (the memmove roughly cancelled out the dispatch savings);
             * CallFrame::has_callee_slot (vm.h) now lets pop_frame handle
             * a frame with no callee slot correctly, so the callee is
             * simply never pushed, matching Kaappi's own design (see
             * docs/thoughts/performance-chez-kaappi.md §4.1) and actually
             * delivering the dispatch-iteration saving. Every non-
             * BcClosure callee still goes through call_foreign here
             * exactly as OP_CALL itself does, which is what makes this
             * safe for every callee type curry has (primitives,
             * continuations, parameter objects, FFI functions), not just
             * closures -- the specific failure Kaappi's own postmortem
             * documented for a naively-fused call opcode that only
             * dispatched two callee types directly instead of reusing the
             * universal trampoline. */
            uint8_t ci     = READ_U8();
            uint8_t argc2  = READ_U8();
            val_t callee   = load_global_cached(frame->closure->chunk, ci);

            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
                if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
                    && curry_profiling_level == 0 && !vm_debug_active) {
                    vm_check_arity(cl, argc2);
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    gc_inhibit_minor();
                    val_t result = ((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc2,
                                                                         (uint64_t *)call_args,
                                                                         (uint64_t *)jc->caps);
                    gc_resume_minor();
                    g_jit_call_depth--;
                    vm->sp -= argc2;
                    PUSH(result);
                    NEXT;
                }
                vm_maybe_jit(cl);
                if (vm->frame_count >= VM_FRAMES_MAX)
                    scm_raise_code(EC_STACK_OVERFLOW, "call stack overflow (max %d frames)", VM_FRAMES_MAX);
                CallFrame *nf  = &vm->frames[vm->frame_count++];
                nf->closure    = cl;
                nf->has_callee_slot = false;  /* no callee pushed above */
                nf->ip         = cl->chunk->code;
                nf->slots      = vm->sp - argc2;
                nf->slot_count = vm_bind_args(cl, nf->slots, argc2);
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
                vm->sp -= argc2;
                PUSH(result);
            }
            NEXT;
        }

        CASE(OP_TAIL_CALL_GLOBAL) {
            /* Fused OP_LOAD_GLOBAL + OP_TAIL_CALL -- see OP_CALL_GLOBAL's
             * comment just above for the full rationale (no callee slot
             * pushed at all, CallFrame::has_callee_slot lets pop_frame
             * handle it). This branch never creates a new CallFrame (a
             * tail call reuses the current one in place), so it never
             * touches has_callee_slot -- that flag describes how the
             * CURRENT frame was originally entered, unaffected by which
             * opcode later tail-calls within it. */
            uint8_t ci     = READ_U8();
            uint8_t argc2  = READ_U8();
            val_t callee   = load_global_cached(frame->closure->chunk, ci);

            if (vis_bcclosure(callee)) {
                BcClosure *cl = as_bcclosure(callee);
                if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
                    && curry_profiling_level == 0 && !vm_debug_active) {
                    vm_check_arity(cl, argc2);
                    JitClosure *jc = as_jitclos(cl->jit_val);
                    val_t *call_args = vm->sp - argc2;
                    typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
                    g_jit_call_depth++;
                    gc_inhibit_minor();
                    val_t result = ((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc2,
                                                                         (uint64_t *)call_args,
                                                                         (uint64_t *)jc->caps);
                    gc_resume_minor();
                    g_jit_call_depth--;
                    vm->sp -= argc2;
                    if (pop_frame(&frame, result)) return *--vm->sp;
                    if (vm->frame_count == entry_depth) return *--vm->sp;
                    NEXT;
                }
                vm_maybe_jit(cl);
                if (curry_profiling_level >= 2 && frame->prof_start_ns &&
                        frame->closure->chunk->name) {
                    val_t sym = sym_intern_cstr(frame->closure->chunk->name);
                    profiling_record_timed(sym, frame->prof_start_ns);
                }
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
                frame->slot_count = vm_bind_args(cl, frame->slots, argc2);
                frame->prof_start_ns = 0;
                if (curry_profiling_level >= 2 && cl->chunk->name)
                    frame->prof_start_ns = profiling_now_ns();
            } else {
                val_t *call_args = vm->sp - argc2;
                val_t result     = call_foreign(callee, (int)argc2, call_args);
                vm->sp -= argc2;
                if (pop_frame(&frame, result)) return *--vm->sp;
                if (vm->frame_count == entry_depth) return *--vm->sp;
            }
            NEXT;
        }

        CASE(OP_TREE_EVAL_CACHED) {
            /* See chunk.h's Chunk::tree_eval_cache and opcode.h's own
             * comment. Not tail/non-tail-distinguished -- eval()'s
             * dispatch for import/define-library/library always returns
             * normally (side-effecting declarations, never altering
             * control flow), so this is a plain value-producing
             * instruction regardless of the surrounding call's tail
             * position, unlike an ordinary call.
             *
             * The chunk this cache lives in is shared VERBATIM (not
             * copied) across actor threads (vm_snapshot_closure_for_escape
             * only snapshots upvalues), so two actors can race on the same
             * cache slot for the same chunk. A plain load/store of a
             * val_t across threads with no ordering is a data race under
             * the C11 memory model even though every write here happens
             * to store the exact same value (V_VOID -- import/
             * define-library/library's result is always immediate, never
             * a heap pointer, so there is nothing to lose from two
             * threads both losing the race and both calling eval() once
             * each; the redundant eval() is no worse than what happened
             * every time before this cache existed). Use acquire/release
             * atomics on the slot itself so the race is well-defined
             * instead of relying on that coincidence.
             *
             * Deliberately do NOT also call gc_wb_slot() on this slot
             * (independent security review caught this: an earlier
             * version called both gc_wb_slot() -- a plain, non-atomic
             * `*slot = newval` -- and the atomic store below on the same
             * slot, which is undefined behavior under C11 regardless of
             * whether the two writes agree, and gc_wb_slot's own dirty-
             * slot bookkeeping (gc_dirty_slots/gc_dirty_count, gc.h) is
             * itself unsynchronized, so a future value that actually took
             * its vis_ptr(newval) branch concurrently from two racing
             * actor threads would corrupt that fixed-size array via an
             * unlocked, non-atomic gc_dirty_count++). No write barrier is
             * needed here in the first place: unlike a general heap
             * mutation, this array is fully re-scanned every minor GC via
             * gc_gen.c's T_CHUNK evacuation case (every chunk is a pinned
             * Boehm object, walked in full each pass), so there is no
             * "remembered set" for the barrier to populate. */
            uint8_t ci = READ_U8();
            Chunk *chunk = frame->closure->chunk;
            _Atomic val_t *slot = chunk->tree_eval_cache
                ? (_Atomic val_t *)&chunk->tree_eval_cache[ci] : NULL;
            val_t cached = slot ? atomic_load_explicit(slot, memory_order_acquire) : 0;
            if (cached != 0) {
                PUSH(cached);
            } else {
                val_t result = eval(chunk->constants[ci], GLOBAL_ENV);
                if (slot)
                    atomic_store_explicit(slot, result, memory_order_release);
                PUSH(result);
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
            /* n == 0 falls through to the general path below and builds a
             * genuine zero-count Values object, matching eval.c's S_VALUES
             * tree-walker case -- NOT V_VOID. A prior version special-cased
             * n == 0 as V_VOID, which broke call-with-values: its consumer
             * checks vis_values(produced) to decide how many arguments to
             * apply, and V_VOID (indistinguishable from any OTHER
             * void-returning single value) doesn't say "zero values" --
             * it was applied as ONE argument, so
             * (call-with-values (lambda () (values)) (lambda () ...))
             * failed with "too many arguments (got 1, need 0)". */
            Values *mv = (Values *)gc_alloc(sizeof(Values) + (size_t)n * sizeof(val_t));
            mv->hdr.type = T_VALUES; mv->hdr.flags = 0; mv->count = n;
            val_t *base = vm->sp - n;
            for (int i = 0; i < (int)n; i++) mv->vals[i] = base[i];
            vm->sp -= n;
            PUSH(vptr(mv));
            NEXT;
        }

        CASE(OP_VALUES_REF) {
            uint8_t a = READ_U8();
            val_t top = POP();
            /* Generic message, not "define-values:" -- this opcode is a
             * general read-side counterpart to OP_VALUES (see opcode.h),
             * not exclusive to define-values' own codegen even though
             * that's its only caller today; a future second caller
             * shouldn't inherit a misleading error prefix (independent
             * code review). */
            if (vis_values(top)) {
                Values *mv = as_vals(top);
                if (a >= mv->count)
                    scm_raise(V_FALSE,
                        "too few values (need index %d, got %u)",
                        (int)a, mv->count);
                PUSH(mv->vals[a]);
            } else {
                if (a != 0)
                    scm_raise(V_FALSE,
                        "too few values (need index %d, got 1)",
                        (int)a);
                PUSH(top);
            }
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

        /* Same idea as OP_TAIL_CALL vs OP_CALL: reuses the current frame
         * when the consumer is a BcClosure, instead of always going
         * through call_foreign (which, for a BcClosure argument, starts a
         * whole NEW nested vm_run() that only unwinds once the consumer's
         * entire continuation finishes -- exactly the bug this opcode
         * exists to avoid, since a call-with-values/receive/let-values in
         * tail position of a self-recursive loop previously accumulated
         * one such nested frame PER ITERATION, hitting the call-stack
         * limit instead of looping forever. */
        CASE(OP_TAIL_CALL_WITH_VALUES) {
            val_t consumer = POP();
            val_t thunk    = POP();
            val_t produced = call_foreign(thunk, 0, NULL);
            int argc2;
            if (vis_values(produced)) {
                Values *mv = as_vals(produced);
                argc2 = (int)mv->count;
                for (int i = 0; i < argc2; i++) PUSH(mv->vals[i]);
            } else {
                argc2 = 1;
                PUSH(produced);
            }
            if (vis_bcclosure(consumer)) {
                BcClosure *cl = as_bcclosure(consumer);
                vm_maybe_jit(cl);
                if (curry_profiling_level >= 2 && frame->prof_start_ns &&
                        frame->closure->chunk->name) {
                    val_t sym = sym_intern_cstr(frame->closure->chunk->name);
                    profiling_record_timed(sym, frame->prof_start_ns);
                }
                if (curry_profiling_level >= 1 && cl != frame->closure && cl->chunk->name)
                    profiling_record_call_tco(sym_intern_cstr(cl->chunk->name));
                val_t *new_args = vm->sp - argc2;
                vm_close_upvalues(frame->slots);
                memmove(frame->slots, new_args, (size_t)argc2 * sizeof(val_t));
                vm->sp            = frame->slots + argc2;
                frame->closure    = cl;
                frame->ip         = cl->chunk->code;
                frame->slot_count = vm_bind_args(cl, frame->slots, argc2);
                frame->prof_start_ns = 0;
                if (curry_profiling_level >= 2 && cl->chunk->name)
                    frame->prof_start_ns = profiling_now_ns();
            } else {
                val_t *call_args = vm->sp - argc2;
                val_t result     = call_foreign(consumer, argc2, call_args);
                vm->sp -= argc2;
                if (pop_frame(&frame, result)) return *--vm->sp;
                if (vm->frame_count == entry_depth) return *--vm->sp;
            }
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
                scm_raise_code(EC_STACK_OVERFLOW, "handler stack overflow (max %d)", VM_HANDLERS_MAX);

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
            eh->saved_jit_depth = jit_depth_save();
            current_handler = eh;

            if (setjmp(eh->jmp) != 0) {
                /* An exception was raised and caught by this handler.
                 * Restore VM state from the saved snapshot. */
                int chi = vm->handler_count - 1;
                val_t caught = vm_exn_handlers[chi].exn;
                current_handler   = vm_exn_handlers[chi].prev;
                jit_depth_restore(vm_exn_handlers[chi].saved_jit_depth);
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

#ifdef __GNUC__
    /* Minor-GC safepoint: fires between instructions, after the current op has
     * fully committed its result to vm->stack and before the next op begins.
     * At this point all Scheme values are on vm->stack[0..sp) — the registered
     * GC root range — so gc_gen_minor_collect() sees every live root. */
    L_DISPATCH:
        if (__builtin_expect(gc_minor_pending, 0) && gc_inhibit_count == 0) {
            gc_minor_pending = false;
            extern void gc_gen_minor_collect(void);
            gc_gen_minor_collect();
        }
        if (__builtin_expect(vm_debug_active, 0)) vm_debug_hook(frame);
        goto *dt[READ_U8()];
#else  /* no computed goto */
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

/* Compile a single Scheme expression and immediately run it in the VM,
   with global operations in the compiled code targeting `env` (GLOBAL_ENV
   for every caller today; a define-library body's own isolated
   env_new_root() frame once Track 2 of the eval-elimination migration
   switches modules.c to call this instead of eval() — see chunk.h's
   Chunk::target_env comment).

   SCM_PROTECT'd around compiler_compile(): a library body that fails to
   compile (e.g. a malformed internal-define form) raises mid-compile, and
   without this the thread-local g_compile_target_env was left pointing at
   this call's `env` instead of being cleared — confirmed via a real repro
   (independent code review): after such a failure, a later, unrelated
   top-level `(define ...)` compiled with no g_compile_target_env caller of
   its own silently inherited the stale value and got written into that
   orphaned library env instead of GLOBAL_ENV, invisible to `,env` and any
   other code for the rest of the process. Re-raises the same exception
   after restoring, so callers still see the original failure. Doesn't
   protect vm_run() itself (a runtime error, as opposed to a compile
   error, occurs after compiler_clear_target_env() already ran normally).

   The closure is pushed as the callee before vm_run: pop_frame's normal
   return path does sp = slots - 1 to drop callee + args, so a frame
   entered without a pushed callee eats one slot of the caller's stack.
   Top-level callers never noticed (the frame_count == 0 path resets sp
   to the stack base), but nested evaluation — the debugger's p command
   runs inside a paused vm_run — corrupts the suspended frame without it. */
val_t vm_eval(val_t expr, val_t env) {
    compiler_set_target_env(env);
    val_t cl_val = V_VOID;
    ExnHandler h;
    SCM_PROTECT(h,
        cl_val = compiler_compile(expr),
        { compiler_clear_target_env(); scm_raise_val(h.exn); });
    compiler_clear_target_env();
    BcClosure *cl = as_bcclosure(cl_val);
    vm_push(cl_val);
    return vm_run(cl, 0);
}
