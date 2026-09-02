/*
 * Surviving runtime support for Curry Scheme, split out of eval.c
 * (docs/thoughts/eval-elimination-migration-plan-2026-07-23.md, phase 1).
 *
 * This file holds everything eval.h exposes that is NOT the tree-walker's
 * special-form dispatch: the exception/condition system, dynamic-wind,
 * the JIT call-depth guard, apply/apply_arr (the universal call trampoline
 * used by the VM's call_foreign(), map/sort/for-each, FFI callbacks, etc.),
 * eval_body, scm_load, expand_qq (a genuine compile-time dependency of
 * compiler.c), and eval_init.  eval.c keeps only eval() itself, its
 * exclusively-internal helpers, and eval_call_cc.
 */

/* pthread_getattr_np (used by check_c_stack_depth below to query the real
 * per-thread stack size on Linux) is a glibc extension gated behind
 * _GNU_SOURCE. Harmless on macOS, where the __APPLE__ branch uses
 * pthread_get_stacksize_np instead and never calls it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>

#include "eval.h"
#include "runtime_internal.h"
#include "vm.h"
#include "compiler.h"
#ifdef BUILD_LLVM
#  include "llvm/curry_llvm.h"
#endif
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "env.h"
#include "port.h"
#include "reader.h"
#include "gc.h"
#include "akkadian.h"
#include "lang_registry.h"
#include "symbolic.h"
#include "surreal.h"
#include "profiling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- Shared helpers (also used by eval.c's dispatch) ---- */

val_t make_pair(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

int list_length(val_t lst) {
    int n = 0;
    while (vis_pair(lst)) { n++; lst = vcdr(lst); }
    return n;
}

int list_to_arr(val_t lst, val_t *arr, int max) {
    int n = 0;
    while (vis_pair(lst) && n < max) {
        arr[n++] = vcar(lst);
        lst = vcdr(lst);
    }
    return n;
}

bool is_definition(val_t form) {
    if (!vis_pair(form)) return false;
    val_t op = vcar(form);
    return op == S_DEFINE || op == S_DEFINE_SYNTAX ||
           op == S_DEFINE_VALUES || op == S_DEFINE_RECORD_TYPE ||
           op == S_DEFINE_RULE || op == S_DEFINE_RULESET ||
           op == S_DEFINE_ALGEBRA;
}

/* ---- Exception handling and dynamic wind ---- */

CURRY_THREAD_LOCAL ExnHandler  *current_handler      = NULL;
CURRY_THREAD_LOCAL WindFrame   *current_wind         = NULL;
CURRY_THREAD_LOCAL CondHandler *current_cond_handler = NULL;
CURRY_THREAD_LOCAL RestartFrame *current_restart_frame = NULL;

/* C accessors for TLS vars — used by C++ dylibs (eval.h) to avoid the
 * C++ TLS wrappers (__ZTW...) which are not exported from the main binary. */
ExnHandler **curry_current_handler_ptr(void) { return &current_handler; }
WindFrame  **curry_current_wind_ptr(void)    { return &current_wind; }

void wind_unwind_to(WindFrame *target) {
    while (current_wind != target) {
        WindFrame *wf = current_wind;
        current_wind = wf->prev;
        apply(wf->after, V_NIL);
    }
}

/* Walk the non-unwinding handler chain.  Each matching handler is called
 * with the chain below it active (prevents re-entrance on the same handler).
 * If a handler calls invoke-restart it longjmps away and never returns here.
 * If all handlers return normally, this function returns. */
void walk_cond_handlers(val_t exn, CondHandler *h) {
    if (!h) return;
    bool matches = vis_false(h->type_sym);  /* V_FALSE = catch-all */
    if (!matches) {
        /* Match by condition type symbol or parent hierarchy.
         * condition_is_a is declared in condition.h and defined in condition.c. */
        extern bool condition_is_a(val_t exn, val_t type_sym);
        matches = condition_is_a(exn, h->type_sym);
    }
    if (matches) {
        CondHandler *saved    = current_cond_handler;
        current_cond_handler  = h->prev; /* prevent re-entrance on this handler */
        bool  raised = false;
        val_t rexn   = V_FALSE;
        ExnHandler eh;
        SCM_PROTECT(eh, { apply_arr(h->proc, 1, &exn); }, {
            raised = true; rexn = eh.exn;
        });
        current_cond_handler = saved;
        if (raised) scm_raise_val(rexn); /* handler itself raised — propagate */
        /* Handler returned normally: continue with handlers below h */
        walk_cond_handlers(exn, h->prev);
        return;
    }
    walk_cond_handlers(exn, h->prev);
}

void scm_raise_val(val_t exn) {
    /* Step 1: offer to non-unwinding handler-bind handlers.
     * If any handler calls invoke-restart, we never reach step 2. */
    if (current_cond_handler)
        walk_cond_handlers(exn, current_cond_handler);

    /* Step 2: unwind to the nearest ExnHandler (guard / handler-case). */
    if (current_handler) {
        current_handler->exn = exn;
        longjmp(current_handler->jmp, 1);
    }
    /* Unhandled: print and abort. */
    /* Independent security review of #133 (the printer's own stack-
     * depth guard) found this path could recurse without bound instead
     * of aborting: raising an UNCAUGHT exception whose payload is
     * itself deeply CAR-nested makes scm_write_shared below hit
     * check_c_stack_depth's guard (we're already deep in the C stack --
     * that's WHY it fired), which raises a stack-overflow condition by
     * calling scm_raise_val again; with no current_handler that lands
     * right back at this same "Unhandled: print" code, which tries to
     * print AGAIN while still at (or past) the same stack depth,
     * firing the guard again immediately -- an unbounded print-raise
     * cycle observed to print thousands of "Unhandled exception:"
     * lines before eventually SIGSEGVing anyway once even the
     * guard's own emergency margin is exhausted. A thread-local re-
     * entrancy flag breaks the cycle: the second (or later) entry
     * skips the recursive-printer call entirely and goes straight to
     * abort() with a plain, non-recursive fallback message. */
    static CURRY_THREAD_LOCAL bool in_unhandled_exception = false;
    if (in_unhandled_exception) {
        fprintf(stderr, "\n(unhandled exception while printing a previous unhandled exception)\n");
        abort();
    }
    in_unhandled_exception = true;
    fprintf(stderr, "Unhandled exception: ");
    scm_write_shared(exn, PORT_STDERR);
    fprintf(stderr, "\n");
    abort();
}

static void raise_formatted(val_t kind, val_t code, const char *fmt, va_list ap)
    __attribute__((noreturn));

static void raise_formatted(val_t kind, val_t code, const char *fmt, va_list ap) {
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    /* Prepend Akkadian preamble — as the scribes demanded */
    char preamble[256];
    akkadian_preamble(preamble, sizeof(preamble), msg);
    char full[800];
    snprintf(full, sizeof(full), "%s:\n  %s", preamble, msg);

    /* Build error object */
    ErrorObj *e = CURRY_NEW(ErrorObj);
    e->hdr.type  = T_ERROR;
    e->hdr.flags = 0;

    uint32_t len = (uint32_t)strlen(full);
    String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    s->hdr.type=T_STRING; s->hdr.flags=0; s->len=len; s->hash=0; s->orig_cap=len; s->ext=NULL;
    memcpy(s->data, full, len+1);
    e->message   = vptr(s);
    e->irritants = V_NIL;
    e->kind      = vis_symbol(kind) ? kind : S_ERROR;
    e->backtrace = vm_capture_backtrace();
    e->code      = code;
    scm_raise_val(vptr(e));
}

void scm_raise(val_t kind, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    raise_formatted(kind, V_FALSE, fmt, ap);
    va_end(ap); /* unreachable: raise_formatted never returns */
}

/* Like scm_raise, but stamps a stable machine-legible code (e.g.
 * 'wrong-type-argument) on the resulting error object, readable via
 * (error-object-code e) / (condition-code e). Used at the call sites most
 * frequently hit in practice; most scm_raise() sites remain uncoded — see
 * docs/reference/error-codes.md for the registry and rationale. */
void scm_raise_code(val_t code, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    raise_formatted(V_FALSE, code, fmt, ap);
    va_end(ap); /* unreachable: raise_formatted never returns */
}

/* ---- Quasiquote expansion (used by compiler at compile time) ---- */

val_t expand_qq(val_t form, val_t env, int depth) {
    if (!vis_pair(form)) return make_pair(S_QUOTE, make_pair(form, V_NIL));

    val_t head = vcar(form);
    val_t rest = vcdr(form);

    if (head == S_UNQUOTE) {
        if (depth == 0) return vcar(rest);
        return make_pair(sym_intern_cstr("list"), make_pair(
            make_pair(S_QUOTE, make_pair(S_UNQUOTE, V_NIL)),
            make_pair(expand_qq(vcar(rest), env, depth-1), V_NIL)));
    }
    if (head == S_QUASIQUOTE) {
        return make_pair(sym_intern_cstr("list"), make_pair(
            make_pair(S_QUOTE, make_pair(S_QUASIQUOTE, V_NIL)),
            make_pair(expand_qq(vcar(rest), env, depth+1), V_NIL)));
    }

    /* Check for ,@ (unquote-splicing) in car */
    if (vis_pair(head) && vcar(head) == S_UNQUOTE_SPLICING) {
        val_t splice = vcadr(head);
        val_t tail_qq = expand_qq(rest, env, depth);
        return make_pair(sym_intern_cstr("append"),
               make_pair(depth == 0 ? splice : expand_qq(splice, env, depth-1),
               make_pair(tail_qq, V_NIL)));
    }

    val_t car_qq = expand_qq(head, env, depth);
    val_t cdr_qq = expand_qq(rest, env, depth);
    return make_pair(sym_intern_cstr("cons"), make_pair(car_qq, make_pair(cdr_qq, V_NIL)));
}

/* ---- Apply ---- */

/* Copy a Scheme list's `n` elements (n = list_length(args), computed by
 * the caller) into a C array sized to match — `inline_buf` (caller's
 * on-stack array) for the common case, a fresh GC allocation for anything
 * larger. This replaces three call sites that used to copy into a fixed
 * 64-slot buffer via list_to_arr(args, arr, 64) while a SEPARATE, uncapped
 * list_length() supplied the count used afterward: for the BcClosure and
 * symbolic-apply paths, that meant reading arr[64..n-1] — past the end of
 * the 64-slot buffer, straight off the C stack, and pushing that garbage
 * as real Scheme values (a real, exploitable-from-Scheme information leak:
 * `(apply f big-list)` called indirectly, e.g. `(define ap apply) (ap f
 * big-list)`, since the compiler only special-cases a *literal*
 * `(apply f args)` call site into a safe, correctly-sized OP_APPLY). For
 * the primitive-call path it didn't read out of bounds (that one already
 * capped its own `n` at 64 via list_to_arr's return value) but silently
 * dropped every argument past #64 with no error — a separate, purely-
 * logic bug.
 *
 * Allocated via gc_alloc (GC-managed, holds val_t which may be heap
 * pointers — never GC_MALLOC_ATOMIC) rather than malloc/free or a
 * persistent scratch buffer, for two reasons found by review of an
 * earlier version of this fix that DID use malloc/free:
 *   1. Reentrancy: `apply` is reentrant (a primitive called through this
 *      path can itself call back into `apply`, e.g. map/for-each
 *      internals) — a single shared/thread-local buffer would let a
 *      nested large-argc call overwrite an outer call's still-in-use argv
 *      out from under it. Each call getting its own allocation avoids
 *      this regardless of how it's allocated.
 *   2. free()-based cleanup requires a free() call on every exit path,
 *      and this function's callers (below) call into things — prim->fn,
 *      vm_run, sx_make_apply — that can raise a Scheme condition (a
 *      longjmp) for entirely mundane reasons (a wrong-type argument, an
 *      arity mismatch two frames down), not just rare fatal errors.
 *      Every such raise skips a textually-later free(), leaking the
 *      buffer — confirmed empirically (~190MB over 200k iterations of a
 *      >64-arg call that raises inside a guard). GC allocation has no
 *      such window: nothing needs to free it, so nothing can forget to. */
static val_t *args_to_arr(val_t args, int n, val_t *inline_buf, int inline_cap) {
    val_t *arr = (n <= inline_cap) ? inline_buf : (val_t *)gc_alloc((size_t)n * sizeof(val_t));
    list_to_arr(args, arr, n);
    return arr;
}

val_t apply(val_t proc, val_t args) {
    if (vis_bcclosure(proc)) {
        BcClosure *cl = as_bcclosure(proc);
        int n = list_length(args);
        val_t stack_arr[64];
        val_t *arr = args_to_arr(args, n, stack_arr, 64);
        val_t *saved_sp = vm->sp;
        vm_push(proc);
        for (int i = 0; i < n; i++) vm_push(arr[i]);
        if (curry_profiling_level >= 1 && cl->chunk->name) {
            val_t sym = sym_intern_cstr(cl->chunk->name);
            if (curry_profiling_level >= 2) {
                uint64_t t0 = profiling_now_ns();
                val_t result = vm_run(cl, n);
                vm->sp = saved_sp;
                profiling_record_timed(sym, t0);
                return result;
            }
            profiling_record_call(sym);
        }
        val_t result = vm_run(cl, n);
        vm->sp = saved_sp;
        return result;
    }
    if (vis_prim(proc)) {
        Primitive *prim = as_prim(proc);
        int n = list_length(args);
        val_t stack_arr[64];
        val_t *arr = args_to_arr(args, n, stack_arr, 64);
        if (curry_profiling_level >= 3 && prim->name)
            profiling_record_prim(sym_intern_cstr(prim->name));
        gc_inhibit_minor();
        val_t r = prim->fn(n, arr, prim->ud);
        gc_resume_minor();
        return r;
    }
    if (vis_closure(proc)) {
        Closure *c = as_clos(proc);
        val_t env  = env_bind_args(vptr(c->env), c->params, args);
        if (curry_profiling_level >= 1 && vis_symbol(c->name)) {
            if (curry_profiling_level >= 2) {
                uint64_t t0 = profiling_now_ns();
                val_t r = eval_body(c->body, env);
                profiling_record_timed(c->name, t0);
                return r;
            }
            profiling_record_call(c->name);
        }
        return eval_body(c->body, env);
    }
    if (vis_cont(proc)) {
        Continuation *cont = as_cont(proc);
        cont->result = vis_pair(args) ? vcar(args) : V_VOID;
        __asm__ volatile("" ::: "memory");
        wind_unwind_to((WindFrame *)cont->wind_top);
        longjmp(*(jmp_buf *)cont->jmpbuf, 1);
    }
    if (vis_param(proc)) {
        Parameter *p = as_param(proc);
        if (vis_nil(args)) return p->init;
        val_t newval = vcar(args);
        if (!vis_false(p->converter)) newval = apply(p->converter, make_pair(newval, V_NIL));
        gc_wb_slot(&p->init, newval);
        return V_VOID;
    }
    if (vis_traced(proc)) {
        Traced *t = as_traced(proc);
        const char *nm = vis_symbol(t->name) ? as_sym(t->name)->data : "?";
        port_write_string(PORT_STDERR, "[trace] --> (", 13);
        port_write_string(PORT_STDERR, nm, (uint32_t)strlen(nm));
        for (val_t a = args; vis_pair(a); a = vcdr(a)) {
            port_write_char(PORT_STDERR, ' ');
            scm_write_shared(vcar(a), PORT_STDERR);
        }
        port_write_string(PORT_STDERR, ")\n", 2);
        val_t result = apply(t->proc, args);
        port_write_string(PORT_STDERR, "[trace] <-- ", 12);
        port_write_string(PORT_STDERR, nm, (uint32_t)strlen(nm));
        port_write_string(PORT_STDERR, " = ", 3);
        scm_write_shared(result, PORT_STDERR);
        port_write_char(PORT_STDERR, '\n');
        return result;
    }
    if (vis_symfn(proc)) {
        int n = list_length(args);
        val_t stack_arr[64];
        val_t *arr = args_to_arr(args, n, stack_arr, 64);
        return sx_make_apply(proc, n, arr);
    }
    scm_raise_code(EC_NOT_A_PROCEDURE, "apply: not a procedure");
}

/* ── Tiered JIT ──────────────────────────────────────────────────────── */

extern val_t scm_cons(val_t, val_t);

/* After this many calls, a BcClosure is compiled to native code. */
#define JIT_THRESHOLD 50

/* Max C-stack depth for JIT→JIT calls.  Once exceeded, apply_arr falls
 * back to the bytecode interpreter which uses O(1) C-stack via OP_TAIL_CALL.
 * This prevents stack overflow for deeply-recursive functions that hit the
 * JIT threshold before the recursion unwinds. */
#define JIT_CALL_DEPTH_LIMIT 512
CURRY_THREAD_LOCAL int g_jit_call_depth = 0;

/* C-linkage wrappers for jit.cpp — avoids C++ TLS wrapper ABI mismatch. */
void jit_depth_push(void) { g_jit_call_depth++; }
void jit_depth_pop(void)  { g_jit_call_depth--; }

/* Used by every exception-unwind path to keep g_jit_call_depth from leaking
 * across a caught exception raised from inside a JIT-to-JIT call chain —
 * see ExnHandler.saved_jit_depth in eval.h. */
int  jit_depth_save(void)        { return g_jit_call_depth; }
void jit_depth_restore(int saved) { g_jit_call_depth = saved; }

/* ---- Shared C-stack-depth guard (issue #125) ----
 *
 * Originally eval()'s own private mechanism (eval.c) to detect an
 * approaching real C-stack overflow from unbounded non-tail recursion
 * through the tree-walker -- goto-tail iterations don't grow the C
 * stack and never reach this check at all; only genuine recursive
 * C-level re-entry does. Without it, that recursion SIGSEGVs the whole
 * process once the OS stack is exhausted, unlike the bytecode VM's own
 * compiled path, which has an explicit, catchable frame-count guard
 * (vm.c, "call stack overflow (max N frames)").
 *
 * Extracted here, and made a single shared instance rather than one
 * private copy per subsystem, when ir_emit.c's own C-level recursion
 * (ir_emit <-> ir_emit_inline_call, unbounded by a flat let* / letrec*
 * chain of local bindings -- see issue #125) turned out to have the
 * identical gap: eval() and ir_emit() run on the SAME thread and
 * consume the SAME physical C stack, so there is exactly one real
 * "how close to the top" answer regardless of which caller asks --
 * duplicating the thread-local base/limit state per subsystem would
 * just be two answers to the same question, computed twice. */
static CURRY_THREAD_LOCAL void   *c_stack_base  = NULL;
static CURRY_THREAD_LOCAL size_t  c_stack_limit = 0;

/* Real per-thread stack size, queried once and cached alongside
 * c_stack_base. A fixed byte budget here (e.g. "assume ~8MB, leave 1MB
 * headroom") was found by independent security review to be unsafe:
 * curry's own parallel map/reduce/for-each worker pool (src/workpool.c)
 * spawns threads with a NULL pthread_attr_t, i.e. the platform default
 * stack size -- 512KB on macOS -- so a hardcoded 7MB threshold never
 * fires there and the real stack still exhausts and crashes first,
 * exactly as before this guard existed. Querying the actual size makes
 * the guard correct on any thread, whatever its real stack turns out to
 * be, instead of assuming every thread matches curry's own actor
 * convention (actors.c does explicitly request 8MB; workpool.c and an
 * externally-lowered main-thread ulimit do not). */
/* Clamp to a plausible ceiling: independent security review found that
 * `ulimit -s unlimited` (a real, common setting -- e.g. the default for
 * services on several Linux distros) makes pthread_getattr_np/
 * pthread_attr_getstack report a synthetic ~93TB stack size on Linux
 * (observed on Ubuntu 24.04 arm64), which would push c_stack_limit out
 * past any depth of recursion actually reachable before a real SIGSEGV
 * -- silently disabling this guard exactly on the systems where an
 * unbounded stack makes a runaway recursion most dangerous to run
 * unguarded. 512MB is comfortably above every real thread-stack size
 * curry itself ever requests (actors: 8MB, workpool: platform default,
 * typically 512KB-8MB) while still being a hard ceiling no legitimate
 * query should exceed. */
#define STACK_SIZE_CEILING (512u * 1024u * 1024u)

static size_t query_thread_stack_size(void) {
#if defined(__APPLE__)
    size_t size = pthread_get_stacksize_np(pthread_self());
#else
    size_t size = 8u * 1024u * 1024u; /* conservative fallback if the query itself fails */
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr;
        if (pthread_attr_getstack(&attr, &addr, &size) != 0)
            size = 8u * 1024u * 1024u;
        pthread_attr_destroy(&attr);
    }
#endif
    if (size > STACK_SIZE_CEILING) size = STACK_SIZE_CEILING;
    return size;
}

/* `context` names the caller for the raised error message (e.g. "eval",
 * "compile") -- purely diagnostic, no behavioral difference. */
void check_c_stack_depth(const char *context) {
    if (__builtin_expect(!c_stack_base, 0)) {
        struct GC_stack_base sb;
        /* GC_get_stack_base can fail (GC_UNIMPLEMENTED) on a platform
         * Boehm GC doesn't support this query on -- independent security
         * review found the unchecked case left sb.mem_base indeterminate,
         * which corrupts the "used" computation below into either a huge
         * bogus value (spurious stack-overflow raised on every single
         * eval/compile on this thread from then on, since c_stack_base is
         * cached) or silently disables the guard. Fall back to this
         * frame's own address as the base on failure: an underestimate of
         * the true base (stack grows down from something higher), so
         * "used" is an overestimate -- the guard fires somewhat earlier
         * than ideal instead of not at all, the safe direction to err. */
        c_stack_base = (GC_get_stack_base(&sb) == GC_SUCCESS) ? sb.mem_base : (void *)&sb;
        size_t stack_size = query_thread_stack_size();
        /* Reserve 1/8 of the real stack (min 64KB) for whatever C stack
         * the raise/longjmp unwind path itself still needs once
         * triggered -- scales down with small stacks (e.g. workpool's
         * 512KB) instead of a fixed byte count that could exceed the
         * whole stack on a small thread. */
        size_t margin = stack_size / 8;
        if (margin < 64u * 1024u) margin = 64u * 1024u;
        c_stack_limit = stack_size > margin ? stack_size - margin : stack_size / 2;
    }
    char stack_probe;
    size_t used = (size_t)((uintptr_t)c_stack_base - (uintptr_t)&stack_probe);
    if (__builtin_expect(used > c_stack_limit, 0))
        scm_raise_code(EC_STACK_OVERFLOW, "%s: call stack overflow", context);
}

/* ---- Arithmetic-fast-path deopt (issue #118) ----
 *
 * codegen.cpp's ARITH2/ARITH1 tables open-code +, -, *, /, <, sqrt, etc.
 * as direct C-helper calls, assuming those names are never redefined --
 * cheaper than a real global-variable lookup + generic call on every
 * arithmetic operation, but wrong if a program later does
 * (define (+ a b) ...) or (set! + ...). Rather than pay a per-call
 * runtime guard to check this on every arithmetic operation (which
 * would erase most of the fast path's benefit) or track precisely which
 * already-JIT-compiled closures assumed which specific operator names
 * (a much larger undertaking), this uses one global, permanent, one-way
 * flag: the instant ANY watched arithmetic name is redefined away from
 * its original builtin value, ALL arithmetic fast-pathing -- for every
 * closure, whether or not it uses the redefined name specifically --
 * stops forever. This is coarse (an unrelated closure loses a
 * performance optimization it never needed to lose) but simple and
 * definitely correct, matching the expectation that redefining core
 * arithmetic operators at runtime is vanishingly rare in practice.
 *
 * Two consumers:
 *   - Every already-JIT-compiled closure's native-fast-call dispatch
 *     (apply_arr below, and the OP_CALL/OP_TAIL_CALL-family inline fast
 *     paths in vm.c) checks jit_arith_tainted() before ever invoking a
 *     closure's compiled native code -- this is the actual "deopt": a
 *     closure whose native code still contains a now-stale direct call
 *     to num_add (baked in at compile time, impossible to patch in
 *     place) simply stops being reachable through the fast dispatch and
 *     falls through to the bytecode interpreter instead, which always
 *     reads GLOBAL_ENV's CURRENT binding correctly. No need to reset
 *     the closure's jit_val to force recompilation: since the taint is
 *     permanent, there would be nothing to gain from ever recompiling
 *     it, so it's simply never called through the native path again.
 *   - codegen.cpp checks jit_arith_tainted() before consulting ARITH2/
 *     ARITH1 at all, so any closure compiled (or recompiled) after the
 *     taint occurred never takes the fast path in the first place. */
#ifdef BUILD_LLVM
/* Plain int, NOT _Atomic-qualified: __atomic_load_n/__atomic_store_n are
 * GCC/Clang's low-level intrinsics, which operate directly on an
 * ordinary variable's address -- they reject a C11 _Atomic-qualified
 * type outright (that qualifier is for the separate stdatomic.h
 * atomic_load/atomic_store function-style API instead). Matches every
 * other atomic variable already in this file (cl->jit_val, cl->call_count
 * in BcClosure, vm.h), all plain-typed. */
static int g_jit_arith_snapshot_ready = 0;
static int g_jit_arith_tainted        = 0;

/* Every name ARITH2/ARITH1 (src/llvm/codegen.cpp) open-codes. Kept in
 * sync by hand -- small, stable list, not worth a shared header just
 * for this. */
static const char *const JIT_ARITH_WATCHED_NAMES[] = {
    "+", "-", "*", "/", "<", ">", "<=", ">=", "=",
    "inexact", "exact->inexact", "log", "exp", "sqrt",
    "sin", "cos", "abs", "floor", "ceiling", "truncate", "round",
};
#define JIT_ARITH_WATCHED_COUNT \
    (sizeof(JIT_ARITH_WATCHED_NAMES) / sizeof(JIT_ARITH_WATCHED_NAMES[0]))

static val_t g_jit_arith_watched_syms[JIT_ARITH_WATCHED_COUNT];
static val_t g_jit_arith_originals[JIT_ARITH_WATCHED_COUNT];
#endif

/* Called once, at the very end of builtins_register() (builtins.c) --
 * every watched name is guaranteed bound to its real builtin by then.
 * Snapshotting here, rather than treating "unset" as "never redefined",
 * means the registration writes builtins_register() itself performs
 * are never mistaken for a user redefinition. */
void jit_arith_snapshot_init(void) {
#ifdef BUILD_LLVM
    for (size_t i = 0; i < JIT_ARITH_WATCHED_COUNT; i++) {
        val_t sym = sym_intern_cstr(JIT_ARITH_WATCHED_NAMES[i]);
        g_jit_arith_watched_syms[i] = sym;
        g_jit_arith_originals[i]    = env_lookup_or_false(GLOBAL_ENV, sym);
    }
    __atomic_store_n(&g_jit_arith_snapshot_ready, true, __ATOMIC_RELEASE);
#endif
}

void jit_maybe_taint_global_arith(val_t sym, val_t new_val) {
#ifdef BUILD_LLVM
    if (__atomic_load_n(&g_jit_arith_tainted, __ATOMIC_RELAXED)) return;
    if (!__atomic_load_n(&g_jit_arith_snapshot_ready, __ATOMIC_ACQUIRE)) return;
    for (size_t i = 0; i < JIT_ARITH_WATCHED_COUNT; i++) {
        if (g_jit_arith_watched_syms[i] == sym) {
            if (new_val != g_jit_arith_originals[i])
                __atomic_store_n(&g_jit_arith_tainted, true, __ATOMIC_RELEASE);
            return;
        }
    }
#else
    (void)sym; (void)new_val;
#endif
}

bool jit_arith_tainted(void) {
#ifdef BUILD_LLVM
    return __atomic_load_n(&g_jit_arith_tainted, __ATOMIC_ACQUIRE);
#else
    return false;
#endif
}

#ifdef BUILD_LLVM
/* Build (let ((name0 'val0) ...) src_lambda) to inject captured upvalue
 * values as constants so the JIT compiles them in without needing runtime
 * cell indirection. */
val_t jit_wrap_upvals(BcClosure *cl) {
    val_t bindings = V_NIL;
    for (int i = cl->upval_count - 1; i >= 0; i--) {
        val_t uval    = *cl->upvals[i]->location;
        val_t name    = cl->chunk->upval_names[i];
        val_t quoted  = scm_cons(S_QUOTE, scm_cons(uval, V_NIL));
        val_t binding = scm_cons(name, scm_cons(quoted, V_NIL));
        bindings = scm_cons(binding, bindings);
    }
    return scm_cons(S_LET, scm_cons(bindings, scm_cons(cl->chunk->src_lambda, V_NIL)));
}
#endif /* BUILD_LLVM */

/* Called from vm.c (OP_CALL / OP_TAIL_CALL inline) and apply_arr below. */
void maybe_jit_bcc(BcClosure *cl) {
#ifdef BUILD_LLVM
    if (__atomic_load_n(&cl->jit_val, __ATOMIC_ACQUIRE) != V_VOID) return;
    if (cl->chunk->src_lambda == V_VOID) return;
    /* JIT-compiled native code's global-variable resolution (src/llvm/
     * jit.cpp's env_lookup_slot/env_define helpers) hard-codes GLOBAL_ENV,
     * with no knowledge of a chunk's own Chunk::target_env (chunk.h) --
     * unlike the bytecode VM's OP_LOAD_GLOBAL/etc, which read TARGET_ENV.
     * A chunk compiled against a define-library body's own env_new_root()
     * frame would, once JIT-promoted, silently start resolving its own
     * top-level references against GLOBAL_ENV instead -- appearing as
     * "unbound variable" for any reference to a same-library sibling that
     * doesn't happen to also exist in GLOBAL_ENV. Conservative fix,
     * matching this project's own established pattern for a closely
     * related problem (docs/thoughts/performance-chez-kaappi.md ss4.4's
     * call/cc side-exit design): such chunks stay on the bytecode VM tier
     * permanently, never promoted to native code, rather than teaching
     * the JIT's own codegen and runtime helpers about target_env too --
     * real follow-up work, not attempted here. Bytecode-VM execution
     * alone is still the actual goal of this migration (define-library
     * bodies no longer tree-walked); JIT promotion on top of that is a
     * further tier this specific case simply doesn't get yet. */
    if (cl->chunk->target_env != V_VOID) return;
    /* A let-syntax/letrec-syntax-local macro use has no GLOBAL_ENV binding
     * for codegen.cpp's own macro guard to find, and the enclosing
     * let-syntax form itself is never part of this chunk's own src_lambda
     * (it's evaluated once at define time to produce the closure, then
     * discarded) -- so codegen.cpp can silently compile a call to such a
     * macro's name as an ordinary procedure call with no way to catch it
     * from the AST alone. classify_head (compiler_classic.c) sets this
     * flag whenever compiling this chunk resolved a name via
     * resolve_syntax_local; bailing out here, same as target_env just
     * above, is the only place with the compile-time information needed
     * to catch it. See issue #114. */
    if (cl->chunk->uses_local_macro) return;
    if (cl->upval_count > 0 && !cl->chunk->upval_names) return;
    /* Skip JIT for self-referencing closures (named-let loops that capture
     * themselves as an upvalue).  The bytecode tail-call reuse gives O(1)
     * stack; JIT would bake the self-reference as a constant and recurse via
     * apply_arr → vm_run, growing the C stack by one frame per iteration. */
    for (int i = 0; i < cl->upval_count; i++) {
        if (*cl->upvals[i]->location == vptr(cl)) {
            __atomic_store_n(&cl->jit_val, V_FALSE, __ATOMIC_RELEASE);
            return;
        }
    }
    if (__atomic_fetch_add(&cl->call_count, 1u, __ATOMIC_RELAXED) + 1 < JIT_THRESHOLD) return;
    /* CAS V_VOID → V_FALSE: only the winning thread compiles this closure.
     * Losers see V_FALSE and fall back to bytecode until the result is stored. */
    val_t expected = V_VOID;
    if (!__atomic_compare_exchange_n(&cl->jit_val, &expected, V_FALSE,
                                     /*weak=*/false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return;
    val_t src = (cl->upval_count > 0)
        ? jit_wrap_upvals(cl)
        : cl->chunk->src_lambda;
    val_t compiled = curry_llvm_jit_compile(src);
    __atomic_store_n(&cl->jit_val,
                     vis_jitclosure(compiled) ? compiled : V_FALSE,
                     __ATOMIC_RELEASE);
#else
    (void)cl;
#endif
}

val_t apply_arr(val_t proc, int argc, val_t *argv) {
    if (vis_bcclosure(proc)) {
        BcClosure *cl = as_bcclosure(proc);

        /* Tiered JIT: redirect to native code once compiled.
         * Depth guard prevents C-stack overflow for deeply-recursive functions:
         * beyond JIT_CALL_DEPTH_LIMIT we fall through to the bytecode
         * interpreter which reuses its frame via OP_TAIL_CALL (O(1) C-stack). */
        if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT && !jit_arith_tainted()
            && curry_profiling_level == 0) {
            vm_check_arity(cl, argc);
            JitClosure *jc = as_jitclos(cl->jit_val);
            typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
            g_jit_call_depth++;
            gc_inhibit_minor();
            val_t r = (val_t)((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc,
                                                                    (uint64_t *)argv,
                                                                    (uint64_t *)jc->caps);
            gc_resume_minor();
            g_jit_call_depth--;
            return r;
        }
        maybe_jit_bcc(cl);
        val_t *saved_sp = vm->sp;
        vm_push(proc);
        for (int i = 0; i < argc; i++) vm_push(argv[i]);
        if (curry_profiling_level >= 1 && cl->chunk->name) {
            val_t sym = sym_intern_cstr(cl->chunk->name);
            if (curry_profiling_level >= 2) {
                uint64_t t0 = profiling_now_ns();
                val_t result = vm_run(cl, argc);
                vm->sp = saved_sp;
                profiling_record_timed(sym, t0);
                return result;
            }
            profiling_record_call(sym);
        }
        val_t result = vm_run(cl, argc);
        vm->sp = saved_sp;
        return result;
    }
    if (vis_jitclosure(proc)) {
        JitClosure *jc = as_jitclos(proc);
        typedef uint64_t (*jit_fn_t)(int32_t, uint64_t *, uint64_t *);
        gc_inhibit_minor();
        val_t r = (val_t)((jit_fn_t)GC_REVEAL_POINTER(jc->fn))((int32_t)argc,
                                                                (uint64_t *)argv,
                                                                (uint64_t *)jc->caps);
        gc_resume_minor();
        return r;
    }
    if (vis_symfn(proc)) return sx_make_apply(proc, argc, argv);
    if (vis_prim(proc)) {
        Primitive *prim = as_prim(proc);
        if (prim->min_args >= 0 && argc < prim->min_args)
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "%s: too few arguments (got %d, need %d)",
                      prim->name, argc, prim->min_args);
        if (prim->max_args >= 0 && argc > prim->max_args)
            scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "%s: too many arguments (got %d, max %d)",
                      prim->name, argc, prim->max_args);
        if (curry_profiling_level >= 3 && prim->name)
            profiling_record_prim(sym_intern_cstr(prim->name));
        gc_inhibit_minor();
        val_t r = prim->fn(argc, argv, prim->ud);
        gc_resume_minor();
        return r;
    }
    if (vis_closure(proc)) {
        Closure *c = as_clos(proc);
        val_t env = env_bind_arr(vptr(c->env), c->params, argc, argv);
        if (curry_profiling_level >= 1 && vis_symbol(c->name)) {
            if (curry_profiling_level >= 2) {
                uint64_t t0 = profiling_now_ns();
                val_t r = eval_body(c->body, env);
                profiling_record_timed(c->name, t0);
                return r;
            }
            profiling_record_call(c->name);
        }
        return eval_body(c->body, env);
    }
    if (vis_traced(proc)) {
        val_t args = V_NIL;
        for (int i = argc - 1; i >= 0; i--) args = make_pair(argv[i], args);
        return apply(proc, args);
    }
    /* cont, param, error — rare paths, build list */
    val_t args = V_NIL;
    for (int i = argc - 1; i >= 0; i--)
        args = make_pair(argv[i], args);
    return apply(proc, args);
}

val_t eval_body(val_t exprs, val_t env) {
    if (vis_nil(exprs)) return V_VOID;
    bool in_body = false;
    while (vis_pair(vcdr(exprs))) {
        val_t form = vcar(exprs);
        if (is_definition(form)) {
            if (in_body)
                scm_raise(V_FALSE, "internal definition after expression in body (R7RS violation)");
        } else {
            in_body = true;
        }
        eval(form, env);
        exprs = vcdr(exprs);
    }
    return eval(vcar(exprs), env);
}

/* ---- Load file ----
 *
 * A relative `path` (no leading '/') resolves against the directory of
 * whichever file is currently being loaded -- not the process's current
 * working directory -- via a small stack of directories, so a chain of
 * nested (include ...)/(load ...) calls (e.g. a define-library's own
 * (include "helper.scm") declaration, itself reached while loading some
 * other .sld from a directory that isn't the process's cwd) resolves
 * relative to the file that named it, the same way #include/require/
 * import typically works in other languages. Previously every load
 * resolved relative to cwd unconditionally, which meant a library whose
 * own directory wasn't the cwd could never portably (include ...) a
 * sibling file -- found while testing a multi-file SRFI-279 port that
 * follows exactly that layout.
 *
 * Mark/release, not push/pop: load_dir_mark() snapshots the current
 * depth; load_dir_release(mark) pops everything back down to it. This
 * (rather than a single paired push+pop per call) is what makes the API
 * safe under both of the ways a single push/pop pairing broke:
 *   - Exception unwind: if eval() below raises, a plain "push then pop
 *     after the loop" leaves the pushed entry (and any it nested)
 *     permanently on the stack -- not just a leak, but stale state that
 *     then corrupts a *later, unrelated* load's path resolution if the
 *     exception is caught (guard/with-exception-handler) and execution
 *     continues in the same process. scm_load and load_scheme_module
 *     both now release back to their entry mark from inside SCM_PROTECT's
 *     exception path before re-raising, the same pattern
 *     call-with-precision (builtins.c) uses to restore tl_mpfr_prec.
 *   - Stack overflow desync: load_push_dir silently no-ops past
 *     MAX_LOAD_DEPTH (pathological nesting) without incrementing
 *     load_dir_depth -- a fixed push/pop pairing would then have its
 *     matching "pop" fire anyway, freeing/popping the *outer*, still-
 *     active frame one level early. Releasing to a saved mark is
 *     self-correcting: a skipped push never advanced load_dir_depth
 *     past the mark, so releasing to that mark is a correct no-op for
 *     it regardless.
 *
 * load_scheme_module (modules.c) uses the same mark/release around its
 * own read/eval loop for .sld/.scm module files, since that's a second,
 * independent file-reading loop (not implemented in terms of scm_load)
 * that (include ...) declarations inside a define-library can be
 * reached from. main.c's positional-script-argument path marks/releases
 * around running the script itself, so a top-level script's own
 * (load "relative/path.scm") resolves against the script's directory
 * too, not just files reached via -l/(include ...)/module loading.
 */

/* CURRY_THREAD_LOCAL, matching current_handler/current_wind/g_jit_call_depth
 * above -- curry's actor system runs each actor in its own detached
 * POSIX thread (src/actors.c), any of which can call scm_load/
 * load_scheme_module concurrently. A plain (non-thread-local) static
 * stack here is a real cross-thread data race and worse -- verified via
 * ThreadSanitizer: load_dir_release()'s free() on a stack slot races
 * against a concurrent read of that same slot in another thread's
 * scm_load(), a genuine use-after-free of the dir_of()-allocated
 * buffer, not just a lost-update. "What directory am I currently
 * loading relative to" is inherently per-thread call-stack context
 * anyway, the same way current_handler's exception-handler chain is. */
#define MAX_LOAD_DEPTH 64
static CURRY_THREAD_LOCAL char *load_dir_stack[MAX_LOAD_DEPTH];
static CURRY_THREAD_LOCAL int   load_dir_depth = 0;

static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return NULL; /* no directory component -- caller's cwd-relative open already matches */
    size_t len = (size_t)(slash - path);
    char *dir = malloc(len + 1);
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

void load_push_dir(const char *path) {
    /* Pathological nesting (64+ deep): stop tracking rather than
     * overflow the array. load_dir_depth stops advancing, so relative-
     * path resolution beyond this point keeps resolving against the
     * last successfully-tracked directory (not truly cwd-relative)
     * until the stack unwinds back under the cap -- verified this
     * degrades to a clean "file not found" for a load whose real
     * parent is beyond the cap, not a crash or wrong-file open. */
    if (load_dir_depth >= MAX_LOAD_DEPTH) return;
    load_dir_stack[load_dir_depth++] = dir_of(path);
}

/* A freshly-spawned actor thread otherwise starts with an *empty*
 * load_dir_stack (that's the whole point of it being CURRY_THREAD_LOCAL --
 * no cross-thread sharing) -- so an actor's own (load "relative.scm")
 * would silently fall back to cwd instead of inheriting whatever
 * directory context the thread that spawned it was in. Confirmed by
 * testing: 20 actors each doing (load "dirA-or-dirB/inner.scm") pass
 * reliably when the process's cwd happens to equal the spawning
 * script's own directory (an easy coincidence to test under without
 * noticing), and fail every single time once cwd differs -- e.g. when
 * ctest runs a script from the build directory rather than the
 * script's own directory, exactly the scenario this whole feature
 * exists to make work correctly.
 *
 * actor_spawn (actors.c) calls load_dir_snapshot() on the *spawning*
 * thread (where the correct context still lives) and stashes the result
 * in ActorStart; actor_thread (running on the new thread) calls
 * load_dir_adopt_snapshot() once at startup, before running the actor's
 * body, to seed its own thread-local stack from it. */
char **load_dir_snapshot(int *out_count) {
    if (load_dir_depth == 0) { *out_count = 0; return NULL; }
    char **snap = malloc((size_t)load_dir_depth * sizeof(char *));
    for (int i = 0; i < load_dir_depth; i++)
        snap[i] = load_dir_stack[i] ? strdup(load_dir_stack[i]) : NULL;
    *out_count = load_dir_depth;
    return snap;
}

void load_dir_adopt_snapshot(char **snap, int count) {
    int n = count < MAX_LOAD_DEPTH ? count : MAX_LOAD_DEPTH;
    for (int i = 0; i < n; i++) load_dir_stack[i] = snap[i];
    load_dir_depth = n;
    /* Only reachable if the source thread's own stack was somehow deeper
     * than MAX_LOAD_DEPTH, which load_push_dir never lets happen -- kept
     * as a defensive leak-avoider, not an expected path. */
    for (int i = n; i < count; i++) free(snap[i]);
    free(snap);
}

int load_dir_mark(void) { return load_dir_depth; }

void load_dir_release(int mark) {
    while (load_dir_depth > mark) {
        load_dir_depth--;
        free(load_dir_stack[load_dir_depth]);
        load_dir_stack[load_dir_depth] = NULL;
    }
}

val_t scm_load(const char *path, val_t env) {
    char resolved[4096];
    if (path[0] != '/' && load_dir_depth > 0 && load_dir_stack[load_dir_depth - 1]) {
        snprintf(resolved, sizeof(resolved), "%s/%s", load_dir_stack[load_dir_depth - 1], path);
        path = resolved;
    }

    val_t port = port_open_file(path, PORT_INPUT);
    if (vis_false(port))
        scm_raise(V_FALSE, "load: cannot open file: %s", path);

    int mark = load_dir_mark();
    load_push_dir(path);
    val_t result = V_VOID;
    ExnHandler h;
    SCM_PROTECT(h, {
        val_t v;
        while (!vis_eof((v = scm_read(port)))) {
            result = eval(v, env);
        }
    }, { load_dir_release(mark); port_close(port); scm_raise_val(h.exn); });
    load_dir_release(mark);
    port_close(port);
    return result;
}

void eval_init(void) { lang_registry_init(); symbolic_init(); surreal_init(); }
