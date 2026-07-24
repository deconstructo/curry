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
#include "akkadian_eval.h"
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

_Thread_local ExnHandler  *current_handler      = NULL;
_Thread_local WindFrame   *current_wind         = NULL;
_Thread_local CondHandler *current_cond_handler = NULL;
_Thread_local RestartFrame *current_restart_frame = NULL;

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
    /* Unhandled: print and abort */
    fprintf(stderr, "Unhandled exception: ");
    scm_write(exn, PORT_STDERR);
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

val_t apply(val_t proc, val_t args) {
    if (vis_bcclosure(proc)) {
        BcClosure *cl = as_bcclosure(proc);
        int n = list_length(args);
        val_t arr[64];
        list_to_arr(args, arr, 64);
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
        val_t arr[64];
        int n = list_to_arr(args, arr, 64);
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
            scm_write(vcar(a), PORT_STDERR);
        }
        port_write_string(PORT_STDERR, ")\n", 2);
        val_t result = apply(t->proc, args);
        port_write_string(PORT_STDERR, "[trace] <-- ", 12);
        port_write_string(PORT_STDERR, nm, (uint32_t)strlen(nm));
        port_write_string(PORT_STDERR, " = ", 3);
        scm_write(result, PORT_STDERR);
        port_write_char(PORT_STDERR, '\n');
        return result;
    }
    if (vis_symfn(proc)) {
        int n = list_length(args);
        val_t arr[64];
        list_to_arr(args, arr, 64);
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
_Thread_local int g_jit_call_depth = 0;

/* C-linkage wrappers for jit.cpp — avoids C++ TLS wrapper ABI mismatch. */
void jit_depth_push(void) { g_jit_call_depth++; }
void jit_depth_pop(void)  { g_jit_call_depth--; }

/* Used by every exception-unwind path to keep g_jit_call_depth from leaking
 * across a caught exception raised from inside a JIT-to-JIT call chain —
 * see ExnHandler.saved_jit_depth in eval.h. */
int  jit_depth_save(void)        { return g_jit_call_depth; }
void jit_depth_restore(int saved) { g_jit_call_depth = saved; }

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
        if (vis_jitclosure(cl->jit_val) && g_jit_call_depth < JIT_CALL_DEPTH_LIMIT
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

/* ---- Load file ---- */

val_t scm_load(const char *path, val_t env) {
    val_t port = port_open_file(path, PORT_INPUT);
    if (vis_false(port))
        scm_raise(V_FALSE, "load: cannot open file: %s", path);

    val_t result = V_VOID;
    val_t v;
    while (!vis_eof((v = scm_read(port)))) {
        result = eval(v, env);
    }
    port_close(port);
    return result;
}

void eval_init(void) { akk_eval_setup(); symbolic_init(); surreal_init(); }
