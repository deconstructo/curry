/*
 * Tree-walking special-form dispatch for Curry Scheme (R7RS).
 *
 * This is the doomed half of the old eval.c
 * (docs/thoughts/eval-elimination-migration-plan-2026-07-23.md, phase 1):
 * the surviving exception/dynamic-wind/JIT-depth/apply machinery now lives
 * in runtime.c.  What remains here is eval() itself, plus helpers that are
 * exclusively internal to its dispatch (the parameterize WindFrame plumbing,
 * eval_call_cc).  Shared helpers (make_pair, list_length, is_definition,
 * wind_unwind_to) live in runtime.c and are declared in runtime_internal.h.
 *
 * Continuations: escape-only in phase 1 (call/cc captures the C stack via
 * setjmp; only upward escapes work).  Full first-class continuations via
 * stack copying are deferred to phase 2.
 *
 * Error handling: scm_raise() throws a C exception via longjmp (runtime.c).
 * Each eval() call site can install a handler with scm_with_exception_handler().
 */

/* pthread_getattr_np (used below to query the real per-thread stack size
 * on Linux) is a glibc extension gated behind _GNU_SOURCE. Harmless on
 * macOS, where the __APPLE__ branch uses pthread_get_stacksize_np
 * instead and never calls it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>

#include "eval.h"
#include "runtime_internal.h"
#include "sx_rules.h"
#include "sx_pattern.h"
#include "sx_algebra.h"
#include "record_type.h"
#include "vm.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"
#include "env.h"
#include "gc.h"
#include "akkadian.h"
#include "lang_registry.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include "profiling.h"
#include "syntax_rules.h"
#include "curry_features.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- parameterize WindFrame helpers ---- */

typedef struct {
    int    n;
    val_t *params;
    val_t *newvals;
    val_t *oldvals;
} ParamBindings;

static val_t param_before_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)av;
    ParamBindings *pb = ud;
    for (int i = 0; i < pb->n; i++) {
        Parameter *p = as_param(pb->params[i]);
        gc_wb_slot(&p->init, pb->newvals[i]);
    }
    return V_VOID;
}

static val_t param_after_fn(int ac, val_t *av, void *ud) {
    (void)ac; (void)av;
    ParamBindings *pb = ud;
    for (int i = 0; i < pb->n; i++) {
        Parameter *p = as_param(pb->params[i]);
        gc_wb_slot(&p->init, pb->oldvals[i]);
    }
    return V_VOID;
}

static val_t make_prim_thunk(PrimFn fn, void *ud) {
    Primitive *p = CURRY_NEW_PINNED(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = "thunk"; p->min_args = 0; p->max_args = 0;
    p->fn = fn; p->ud = ud;
    return vptr(p);
}

/* ---- call/cc helper ---- */

/* noinline ensures the setjmp jmp_buf lives in this function's own stable
 * stack frame.  eval() uses a goto-based TCO loop, so the optimizer may
 * allocate locals in caller-saved registers that longjmp doesn't restore;
 * isolating the setjmp in a dedicated noinline function avoids that.
 * All variables used in the longjmp path (cont, saved_*) are in callee-saved
 * registers for this smaller function and are valid after longjmp. */
__attribute__((noinline))
static val_t eval_call_cc(val_t proc) {
    Continuation *cont = CURRY_NEW_PINNED(Continuation);
    cont->hdr.type = T_CONTINUATION; cont->hdr.flags = 0;
    cont->jmpbuf   = gc_alloc_raw_pinned(sizeof(jmp_buf));
    cont->result   = V_VOID;
    cont->wind_top = current_wind;
    /* VM state — only meaningful when the VM is active (vm may be NULL in
     * pure tree-walker mode, e.g. test_core.c which skips vm_init). */
    int saved_fc   = vm ? vm->frame_count : 0;
    val_t *saved_sp = vm ? vm->sp : NULL;
    Upvalue *saved_uv = vm ? vm->open_upvalues : NULL;
    /* Mirrors SCM_PROTECT's own save/restore triple (eval.h) -- a longjmp
     * back into this frame bypasses every normal-return cleanup between
     * here and the jump's origin (longjmp restores registers/PC directly,
     * it doesn't run cleanup attributes or fall through pending restores),
     * which would otherwise leave all three of these permanently
     * unbalanced: gc_shadow_stack pointing at expired C stack frames
     * (matters only under --gc generational), gc_inhibit_count stuck
     * incremented if the jump crosses a gc_inhibit_minor()/gc_resume_minor()
     * bracket (e.g. invoking a captured continuation from inside a
     * primitive like for-each's own C-side dispatch) -- permanently
     * blocking minor GC on this thread, an unbounded nursery leak, not
     * just a missed optimization -- and g_jit_call_depth stuck incremented
     * if the jump crosses a JIT call boundary, permanently downgrading
     * this closure to the bytecode interpreter for the rest of the
     * process. Found missing here (only gc_shadow was originally added)
     * by independent code review after the shadow-stack part of this fix
     * landed. */
    void *saved_shadow = gc_shadow_save();
    int   saved_inhibit = gc_inhibit_save();
    int   saved_jit_depth = jit_depth_save();
    if (setjmp(*(jmp_buf *)cont->jmpbuf) != 0) {
        gc_shadow_restore(saved_shadow);
        gc_inhibit_restore(saved_inhibit);
        jit_depth_restore(saved_jit_depth);
        if (vm) {
            vm->frame_count   = saved_fc;
            vm->sp            = saved_sp;
            vm->open_upvalues = saved_uv;
        }
        /* Force a real memory load: clang (ARM64 -O2) constant-folds cont->result
         * to V_VOID because it was V_VOID at setjmp time and doesn't see the write
         * done by invoke_continuation before longjmp.  The volatile cast prevents
         * that folding and guarantees an ldr instruction. */
        return *(volatile val_t *)&cont->result;
    }
    return apply(proc, make_pair(vptr(cont), V_NIL));
}


/* ---- Evaluator ---- */

/* Per-thread cached stack base (Boehm's "bottom of stack" == the highest
 * address, for a downward-growing stack), lazily captured on first use.
 * Used to detect an approaching real C-stack overflow from unbounded
 * non-tail recursion through eval() -- goto-tail iterations below don't
 * grow the C stack and never reach this check at all; only genuine
 * recursive C-level re-entry into eval() (a non-tail sub-expression,
 * e.g. deep (+ 1 (deep-sum (- n 1)))) does. Without this, that recursion
 * SIGSEGVs the whole process once the OS stack is exhausted -- unlike
 * the bytecode VM's own compiled path, which has an explicit, catchable
 * frame-count guard (vm.c, "call stack overflow (max N frames)"). This
 * gives the tree-walker the same kind of catchable guard instead of a
 * hard crash. Reproduced: a define-library-defined function doing
 * non-tail recursion to depth ~1,000,000 previously segfaulted
 * immediately (define-library bodies are tree-walked, not compiled --
 * see modules.c's define_library_clause). */
static _Thread_local void   *eval_stack_base  = NULL;
static _Thread_local size_t  eval_stack_limit = 0;

/* Real per-thread stack size, queried once and cached alongside
 * eval_stack_base. A fixed byte budget here (e.g. "assume ~8MB, leave
 * 1MB headroom") was found by independent security review to be unsafe:
 * curry's own parallel map/reduce/for-each worker pool (src/workpool.c)
 * spawns threads with a NULL pthread_attr_t, i.e. the platform default
 * stack size -- 512KB on macOS -- so a hardcoded 7MB threshold never
 * fires there and the real stack still exhausts and crashes first,
 * exactly as before this guard existed. Querying the actual size makes
 * the guard correct on any thread, whatever its real stack turns out to
 * be, instead of assuming every thread matches curry's own actor
 * convention (actors.c does explicitly request 8MB; workpool.c and an
 * externally-lowered main-thread ulimit do not). */
static size_t eval_query_thread_stack_size(void) {
#if defined(__APPLE__)
    return pthread_get_stacksize_np(pthread_self());
#else
    size_t size = 8u * 1024u * 1024u; /* conservative fallback if the query itself fails */
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr;
        if (pthread_attr_getstack(&attr, &addr, &size) != 0)
            size = 8u * 1024u * 1024u;
        pthread_attr_destroy(&attr);
    }
    return size;
#endif
}

val_t eval(val_t expr, val_t env) {
    if (__builtin_expect(!eval_stack_base, 0)) {
        struct GC_stack_base sb;
        GC_get_stack_base(&sb);
        eval_stack_base = sb.mem_base;
        size_t stack_size = eval_query_thread_stack_size();
        /* Reserve 1/8 of the real stack (min 64KB) for whatever C stack
         * the raise/longjmp unwind path itself still needs once
         * triggered -- scales down with small stacks (e.g. workpool's
         * 512KB) instead of a fixed byte count that could exceed the
         * whole stack on a small thread. */
        size_t margin = stack_size / 8;
        if (margin < 64u * 1024u) margin = 64u * 1024u;
        eval_stack_limit = stack_size > margin ? stack_size - margin : stack_size / 2;
    }
    {
        char stack_probe;
        size_t used = (size_t)((uintptr_t)eval_stack_base - (uintptr_t)&stack_probe);
        if (__builtin_expect(used > eval_stack_limit, 0))
            scm_raise_code(EC_STACK_OVERFLOW, "eval: call stack overflow");
    }
    /* Shadow stack: register the two persistent locals so a moving GC can
     * update them after nursery evacuation.  op/rest are declared here so
     * they are in scope for GC_AUTOFRAME; goto tail re-assigns them each
     * iteration without crossing their declarations.
     * Under Boehm GC_AUTOFRAME is a no-op. */
    val_t op = V_NIL, rest = V_NIL;
    GC_AUTOFRAME(4, &expr, &env, &op, &rest);
tail:
    /* Minor GC safe point: all four GC_AUTOFRAME slots are updated by the
     * collector; op and rest are overwritten before use, so their values here
     * don't matter.  The provably-live roots (expr, env) are both tracked. */
    if (__builtin_expect(gc_minor_pending & (gc_inhibit_count == 0), 0)) {
        gc_minor_pending = false;
        extern void gc_gen_minor_collect(void);
        gc_gen_minor_collect();
    }
    /* Non-pointer immediates: fixnum (tag=01), char (tag=10), bool/nil/void/eof (tag=11) */
    if (expr & 3) return expr;
    /* Heap object: dispatch on type */
    {
        uint32_t t = ((Hdr *)(void *)expr)->type;
        if (t == T_SYMBOL) {
            /* #:keyword symbols (Guile/Racket-style) are self-evaluating */
            Symbol *sym = as_sym(expr);
            if (sym->len >= 2 && sym->data[0] == '#' && sym->data[1] == ':')
                return expr;
            return env_lookup(env, expr);
        }
        if (t != T_PAIR)   return expr;   /* string, number, vector, etc. — self-eval */
    }

    op   = lang_translate(vcar(expr));   /* Akkadian/cuneiform → English */
    rest = vcdr(expr);

    /* ---- Special forms ---- */

    if (op == S_QUOTE) {
        return vis_pair(rest) ? vcar(rest) : V_VOID;
    }

    if (op == S_IF) {
        val_t cond = eval(vcar(rest), env);
        rest = vcdr(rest);
        if (vis_true(cond)) {
            expr = vcar(rest); goto tail;
        } else {
            val_t alt = vcdr(rest);
            if (vis_nil(alt)) return V_VOID;
            expr = vcar(alt); goto tail;
        }
    }

    if (op == S_LAMBDA) {
        Closure *c = CURRY_NEW_PINNED(Closure);
        c->hdr.type  = T_CLOSURE;
        c->hdr.flags = 0;
        c->params = vcar(rest);
        c->body   = vcdr(rest);
        c->env    = as_env(env);
        c->name   = V_FALSE;
        return vptr(c);
    }

    if (op == S_BEGIN) {
        if (vis_nil(rest)) return V_VOID;
        while (vis_pair(vcdr(rest))) {
            eval(vcar(rest), env);
            rest = vcdr(rest);
        }
        expr = vcar(rest); goto tail;
    }

    if (op == S_DEFINE) {
        val_t name_form = vcar(rest);
        val_t val;
        if (vis_symbol(name_form)) {
            /* (define name expr) */
            val = vis_nil(vcdr(rest)) ? V_VOID : eval(vcadr(rest), env);
            if (vis_closure(val) && vis_false(as_clos(val)->name))
                as_clos(val)->name = name_form;
        } else if (vis_pair(name_form)) {
            /* (define (name params...) body...) */
            val_t name   = vcar(name_form);
            val_t params = vcdr(name_form);
            Closure *c   = CURRY_NEW_PINNED(Closure);
            c->hdr.type  = T_CLOSURE; c->hdr.flags = 0;
            c->params = params;
            c->body   = vcdr(rest);
            c->env    = as_env(env);
            c->name   = name;
            val  = vptr(c);
            name_form = name;
        } else {
            scm_raise(V_FALSE, "invalid define form");
        }
        env_define(env, name_form, val);
        return V_VOID;
    }

    if (op == S_SET) {
        val_t sym = vcar(rest);
        val_t val = eval(vcadr(rest), env);
        if (!env_set(env, sym, val))
            scm_raise_code(EC_UNBOUND_VARIABLE, "set!: unbound variable: %s", sym_cstr(sym));
        if (sym == S_EVAL_PROFILER && vis_fixnum(val))
            profiling_set_level((int)vunfix(val));
        else if (sym == S_GC_PROFILER && vis_fixnum(val))
            gc_profiling_set_level((int)vunfix(val));
        return V_VOID;
    }

    if (op == S_DEFINE_VALUES) {
        /* (define-values (var...) expr) */
        val_t vars = vcar(rest);
        val_t val  = eval(vcadr(rest), env);
        if (!vis_values(val)) {
            /* Single value -> first var */
            if (vis_pair(vars)) env_define(env, vcar(vars), val);
        } else {
            Values *mv = as_vals(val);
            int i = 0;
            while (vis_pair(vars) && (uint32_t)i < mv->count) {
                env_define(env, vcar(vars), mv->vals[i++]);
                vars = vcdr(vars);
            }
        }
        return V_VOID;
    }

    /* (let ((x v) ...) body...) */
    if (op == S_LET) {
        val_t bindings = vcar(rest);
        val_t body     = vcdr(rest);

        /* Named let: (let name ((var init)...) body...) */
        if (vis_symbol(bindings)) {
            val_t loop_name = bindings;
            bindings = vcar(body);
            body     = vcdr(body);

            val_t new_env = env_extend(env);
            /* Collect params and inits */
            val_t params = V_NIL, inits_list = V_NIL;
            val_t b = bindings;
            while (vis_pair(b)) {
                params      = make_pair(vcar(vcar(b)), params);
                inits_list  = make_pair(vcadr(vcar(b)), inits_list);
                b = vcdr(b);
            }
            /* Reverse both */
            val_t rp = V_NIL; b = params;
            while (vis_pair(b)) { rp = make_pair(vcar(b), rp); b = vcdr(b); }
            params = rp;
            val_t ri = V_NIL; b = inits_list;
            while (vis_pair(b)) { ri = make_pair(vcar(b), ri); b = vcdr(b); }
            inits_list = ri;

            Closure *c = CURRY_NEW_PINNED(Closure);
            c->hdr.type=T_CLOSURE; c->hdr.flags=0;
            c->params=params; c->body=body; c->env=as_env(new_env); c->name=loop_name;
            env_define(new_env, loop_name, vptr(c));

            /* Evaluate inits in original env */
            val_t arg_vals = V_NIL;
            b = inits_list;
            while (vis_pair(b)) {
                arg_vals = make_pair(eval(vcar(b), env), arg_vals); b = vcdr(b);
            }
            /* Reverse */
            val_t ra = V_NIL; b = arg_vals;
            while (vis_pair(b)) { ra = make_pair(vcar(b), ra); b = vcdr(b); }
            arg_vals = ra;

            /* Call: TCO via tail position */
            env = env_bind_args(new_env, params, arg_vals);
            expr = make_pair(S_BEGIN, body); goto tail;
        }

        /* Regular let: evaluate inits in current env */
        val_t new_env = env_extend(env);
        val_t b = bindings;
        while (vis_pair(b)) {
            val_t bind = vcar(b);
            val_t v    = eval(vcadr(bind), env);
            env_define(new_env, vcar(bind), v);
            b = vcdr(b);
        }
        env = new_env;
        if (vis_nil(body)) return V_VOID;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_LET_STAR) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t cur_env = env_extend(env);
        while (vis_pair(bindings)) {
            val_t bind = vcar(bindings);
            env_define(cur_env, vcar(bind), eval(vcadr(bind), cur_env));
            bindings = vcdr(bindings);
        }
        env = cur_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_LETREC || op == S_LETREC_STAR) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t new_env = env_extend(env);
        /* Pre-define all vars as undefined */
        val_t b = bindings;
        while (vis_pair(b)) { env_define(new_env, vcar(vcar(b)), V_UNDEF); b = vcdr(b); }
        /* Evaluate and set */
        b = bindings;
        while (vis_pair(b)) {
            val_t sym = vcar(vcar(b));
            val_t v   = eval(vcadr(vcar(b)), new_env);
            env_set(new_env, sym, v);
            b = vcdr(b);
        }
        env = new_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_LET_VALUES || op == S_LET_STAR_VALUES) {
        /* (let-values  (((a b) producer) ...) body...)
         * (let*-values (((a b) producer) ...) body...) */
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t new_env = env_extend(env);
        val_t b = bindings;
        while (vis_pair(b)) {
            val_t bind   = vcar(b);
            val_t formals = vcar(bind);
            val_t init_e  = vcadr(bind);
            val_t produced = eval(init_e, op == S_LET_STAR_VALUES ? new_env : env);
            /* Collect produced values into a list */
            val_t vals;
            if (vis_values(produced)) {
                Values *mv = as_vals(produced);
                vals = V_NIL;
                for (int i = (int)mv->count - 1; i >= 0; i--)
                    vals = make_pair(mv->vals[i], vals);
            } else {
                vals = make_pair(produced, V_NIL);
            }
            /* Bind formals */
            val_t fms = formals;
            while (vis_pair(fms) && vis_pair(vals)) {
                env_define(new_env, vcar(fms), vcar(vals));
                fms = vcdr(fms); vals = vcdr(vals);
            }
            /* Rest argument: (a . rest) or just symbol */
            if (vis_symbol(fms)) env_define(new_env, fms, vals);
            b = vcdr(b);
        }
        env = new_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_AND) {
        val_t v = V_TRUE;
        while (vis_pair(rest)) {
            if (vis_nil(vcdr(rest))) { expr = vcar(rest); goto tail; }
            v = eval(vcar(rest), env);
            if (vis_false(v)) return V_FALSE;
            rest = vcdr(rest);
        }
        return v;
    }

    if (op == S_OR) {
        while (vis_pair(rest)) {
            if (vis_nil(vcdr(rest))) { expr = vcar(rest); goto tail; }
            val_t v = eval(vcar(rest), env);
            if (vis_true(v)) return v;
            rest = vcdr(rest);
        }
        return V_FALSE;
    }

    if (op == S_COND) {
        while (vis_pair(rest)) {
            val_t clause = vcar(rest);
            val_t test   = vcar(clause);
            val_t body   = vcdr(clause);
            rest = vcdr(rest);

            if (test == S_ELSE) {
                if (vis_nil(body)) return V_VOID;
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
            val_t result = eval(test, env);
            if (vis_true(result)) {
                if (vis_nil(body)) return result;
                if (vcar(body) == S_ARROW) {
                    /* (test => proc) */
                    val_t proc = eval(vcadr(body), env);
                    return apply(proc, make_pair(result, V_NIL));
                }
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
        }
        return V_VOID;
    }

    if (op == S_CASE) {
        val_t key = eval(vcar(rest), env);
        val_t clauses = vcdr(rest);
        while (vis_pair(clauses)) {
            val_t clause = vcar(clauses);
            val_t datums = vcar(clause);
            val_t body   = vcdr(clause);
            clauses = vcdr(clauses);
            if (datums == S_ELSE) {
                if (vis_pair(body) && vcar(body) == S_ARROW) {
                    return apply(eval(vcadr(body), env), make_pair(key, V_NIL));
                }
                while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                expr = vcar(body); goto tail;
            }
            /* Check key against each datum using eqv? */
            val_t d = datums;
            while (vis_pair(d)) {
                val_t datum = vcar(d);
                /* eqv?: pointer eq or numeric eq */
                bool match = (datum == key) ||
                    (vis_fixnum(datum) && vis_fixnum(key) && vunfix(datum) == vunfix(key)) ||
                    (vis_char(datum) && vis_char(key) && vunchr(datum) == vunchr(key));
                if (match) {
                    if (vis_nil(body)) return V_VOID;
                    if (vcar(body) == S_ARROW) {
                        return apply(eval(vcadr(body), env), make_pair(key, V_NIL));
                    }
                    while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
                    expr = vcar(body); goto tail;
                }
                d = vcdr(d);
            }
        }
        return V_VOID;
    }

    if (op == S_WHEN) {
        val_t cond = eval(vcar(rest), env);
        if (vis_false(cond)) return V_VOID;
        val_t body = vcdr(rest);
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_UNLESS) {
        val_t cond = eval(vcar(rest), env);
        if (vis_true(cond)) return V_VOID;
        val_t body = vcdr(rest);
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_DO) {
        /* (do ((var init step)...) (test expr...) body...) */
        val_t var_specs = vcar(rest);
        val_t term      = vcadr(rest);
        val_t body      = vcddr(rest);

        val_t do_env = env_extend(env);
        /* Initialize */
        val_t vs = var_specs;
        while (vis_pair(vs)) {
            val_t spec = vcar(vs);
            env_define(do_env, vcar(spec), eval(vcadr(spec), env));
            vs = vcdr(vs);
        }

        while (1) {
            val_t test_expr = vcar(term);
            val_t test_val  = eval(test_expr, do_env);
            if (vis_true(test_val)) {
                /* Termination */
                val_t result = vcdr(term);
                if (vis_nil(result)) return V_VOID;
                while (vis_pair(vcdr(result))) { eval(vcar(result), do_env); result = vcdr(result); }
                expr = vcar(result); env = do_env; goto tail;
            }
            /* Execute body */
            val_t b = body;
            while (vis_pair(b)) { eval(vcar(b), do_env); b = vcdr(b); }
            /* Step: evaluate new values in current do_env, then assign */
            val_t new_vals = V_NIL;
            vs = var_specs;
            while (vis_pair(vs)) {
                val_t spec = vcar(vs);
                val_t step = vis_nil(vcddr(spec)) ? env_lookup(do_env, vcar(spec))
                                                   : eval(vcaddr(spec), do_env);
                new_vals = make_pair(make_pair(vcar(spec), step), new_vals);
                vs = vcdr(vs);
            }
            /* Assign */
            while (vis_pair(new_vals)) {
                env_set(do_env, vcar(vcar(new_vals)), vcdr(vcar(new_vals)));
                new_vals = vcdr(new_vals);
            }
        }
    }

    if (op == S_DELAY) {
        Promise *p = CURRY_NEW(Promise);
        p->hdr.type=T_PROMISE; p->hdr.flags=0;
        p->state = PROMISE_LAZY;
        /* Wrap body in a thunk */
        Closure *c = CURRY_NEW_PINNED(Closure);
        c->hdr.type=T_CLOSURE; c->hdr.flags=0;
        c->params=V_NIL; c->body=rest; c->env=as_env(env); c->name=V_FALSE;
        p->val = vptr(c);
        return vptr(p);
    }

    if (op == S_DELAY_FORCE) {
        Promise *p = CURRY_NEW(Promise);
        p->hdr.type=T_PROMISE; p->hdr.flags=0;
        p->state = PROMISE_LAZY;
        Closure *c = CURRY_NEW_PINNED(Closure);
        c->hdr.type=T_CLOSURE; c->hdr.flags=0;
        c->params=V_NIL; c->body=rest; c->env=as_env(env); c->name=V_FALSE;
        p->hdr.flags = 1; /* lazy-force flag */
        p->val = vptr(c);
        return vptr(p);
    }

    if (op == S_QUASIQUOTE) {
        val_t expanded = expand_qq(vcar(rest), env, 0);
        expr = expanded; goto tail;
    }

    if (op == S_DEFINE_SYNTAX) {
        val_t name        = vcar(rest);
        val_t transformer = eval(vcadr(rest), env);
        Syntax *syn = CURRY_NEW(Syntax);
        syn->hdr.type=T_SYNTAX; syn->hdr.flags=0;
        syn->transformer = transformer;
        env_define(env, name, vptr(syn));
        return V_VOID;
    }

    if (op == S_LET_SYNTAX || op == S_LETREC_SYNTAX) {
        val_t bindings = vcar(rest), body = vcdr(rest);
        val_t new_env = env_extend(env);
        while (vis_pair(bindings)) {
            val_t bind = vcar(bindings);
            val_t name = vcar(bind);
            val_t xfm  = eval(vcadr(bind), op == S_LET_SYNTAX ? env : new_env);
            Syntax *syn = CURRY_NEW(Syntax);
            syn->hdr.type=T_SYNTAX; syn->hdr.flags=0; syn->transformer=xfm;
            env_define(new_env, name, vptr(syn));
            bindings = vcdr(bindings);
        }
        env = new_env;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_DEFINE_RECORD_TYPE) {
        /* Parsing/RTD-construction is shared with compiler.c's native
         * codegen via record_type_build_spec — see record_type.h. This
         * tree-walker case stays even after the compiler gained native
         * support, because library/define-library bodies (modules.c) are
         * always tree-walked, never compiled. */
        RecordTypeSpec spec;
        record_type_build_spec(rest, V_FALSE, &spec);
        RecordType *rtd = as_rtd(spec.rtd_val);
        for (int i = 0; i < spec.count; i++) {
            Closure *c = CURRY_NEW_PINNED(Closure);
            c->hdr.type = T_CLOSURE; c->hdr.flags = 0;
            c->params = spec.bindings[i].params;
            c->body   = spec.bindings[i].body;
            c->env    = as_env(env);
            c->name   = spec.bindings[i].name;
            val_t cv = vptr(c);
            env_define(env, spec.bindings[i].name, cv);
            /* Stash each binding's closure back onto the RTD so
             * record-type-constructor/-predicate/-accessors/-mutators
             * can retrieve it later (SRFI-279's rtd-properties). */
            switch (spec.bindings[i].role) {
                case RTD_ROLE_CONSTRUCTOR: rtd->constructor = cv; break;
                case RTD_ROLE_PREDICATE:   rtd->predicate   = cv; break;
                case RTD_ROLE_ACCESSOR:    rtd->accessors[spec.bindings[i].field_index] = cv; break;
                case RTD_ROLE_MUTATOR:     rtd->mutators[spec.bindings[i].field_index]  = cv; break;
            }
        }
        return V_VOID;
    }

    if (op == S_INCLUDE) {
        val_t r = V_VOID;
        while (vis_pair(rest)) {
            if (!vis_string(vcar(rest)))
                scm_raise(V_FALSE, "include: filename must be a string");
            r = scm_load(str_data(as_str(vcar(rest))), env);
            rest = vcdr(rest);
        }
        return r;
    }

    if (op == S_COND_EXPAND) {
        bool matched;
        val_t body = cond_expand_choose(rest, &matched);
        if (!matched)
            scm_raise(V_FALSE, "cond-expand: no matching clause");
        if (!vis_pair(body)) return V_VOID;
        while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
        expr = vcar(body); goto tail;
    }

    if (op == S_VALUES) {
        int n = list_length(rest);
        if (n == 1) return eval(vcar(rest), env);
        Values *mv = (Values *)gc_alloc(sizeof(Values) + (size_t)n * sizeof(val_t));
        mv->hdr.type=T_VALUES; mv->hdr.flags=0; mv->count=(uint32_t)n;
        for (int i = 0; i < n; i++) { mv->vals[i] = eval(vcar(rest), env); rest = vcdr(rest); }
        return vptr(mv);
    }

    if (op == S_CALL_WITH_VALUES) {
        val_t producer = eval(vcar(rest), env);
        val_t consumer = eval(vcadr(rest), env);
        val_t produced = apply(producer, V_NIL);
        if (vis_values(produced)) {
            Values *mv = as_vals(produced);
            val_t args = V_NIL;
            for (int i = (int)mv->count - 1; i >= 0; i--)
                args = make_pair(mv->vals[i], args);
            return apply(consumer, args);
        }
        return apply(consumer, make_pair(produced, V_NIL));
    }

    if (op == S_CALL_CC || op == S_CALL_WITH_CC) {
        val_t proc = eval(vcar(rest), env);
        return eval_call_cc(proc);
    }

    if (op == S_PARAMETERIZE) {
        /* (parameterize ((param val)...) body...) */
        /* Register a WindFrame so escape continuations trigger parameter
         * restoration via wind_unwind_to, not just normal-exit cleanup. */
        val_t bindings = vcar(rest), body = vcdr(rest);
        int n = list_length(bindings);

        ParamBindings *pb = gc_alloc_raw_pinned(sizeof(ParamBindings));
        pb->n       = n;
        pb->params  = gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        pb->newvals = gc_alloc_raw_pinned((size_t)n * sizeof(val_t));
        pb->oldvals = gc_alloc_raw_pinned((size_t)n * sizeof(val_t));

        val_t b = bindings;
        for (int i = 0; i < n; i++, b = vcdr(b)) {
            val_t pair  = vcar(b);
            val_t param = eval(vcar(pair), env);
            val_t val   = eval(vcadr(pair), env);
            if (!vis_false(as_param(param)->converter))
                val = apply(as_param(param)->converter, make_pair(val, V_NIL));
            pb->params[i]  = param;
            pb->newvals[i] = val;
            pb->oldvals[i] = as_param(param)->init;
        }

        /* GC-heap WindFrame so longjmp cannot invalidate it. */
        WindFrame *wf = gc_alloc_raw_pinned(sizeof(WindFrame));
        wf->before = make_prim_thunk(param_before_fn, pb);
        wf->after  = make_prim_thunk(param_after_fn,  pb);
        wf->prev   = current_wind;

        apply(wf->before, V_NIL);
        current_wind = wf;

        val_t result = V_VOID;
        ExnHandler h;
        bool raised = false;
        val_t exn_val = V_VOID;
        h.prev = current_handler;
        h.saved_jit_depth = jit_depth_save();
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
            result = eval(vcar(body), env);
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            jit_depth_restore(h.saved_jit_depth);
            raised = true; exn_val = h.exn;
        }

        current_wind = wf->prev;
        apply(wf->after, V_NIL);

        if (raised) scm_raise_val(exn_val);
        return result;
    }

    if (op == S_GUARD) {
        /* (guard (var clause...) body...) */
        val_t var_clauses = vcar(rest);
        val_t var         = vcar(var_clauses);
        val_t clauses     = vcdr(var_clauses);
        val_t body        = vcdr(rest);

        val_t result = V_VOID;
        ExnHandler h;
        h.prev = current_handler;
        h.saved_shadow   = gc_shadow_save();
        h.saved_inhibit  = gc_inhibit_save();
        h.saved_jit_depth = jit_depth_save();
        vm_exn_state_save(&h.saved_vm_frame_count, &h.saved_vm_sp, &h.saved_vm_open_upvalues);
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            while (vis_pair(vcdr(body))) { eval(vcar(body), env); body = vcdr(body); }
            result = eval(vcar(body), env);
            current_handler = h.prev;
            return result;
        }
        current_handler = h.prev;
        gc_shadow_restore(h.saved_shadow);
        gc_inhibit_restore(h.saved_inhibit);
        jit_depth_restore(h.saved_jit_depth);
        vm_exn_state_restore(h.saved_vm_frame_count, h.saved_vm_sp, h.saved_vm_open_upvalues);
        val_t exn = h.exn;

        /* Try each clause */
        val_t guard_env = env_extend(env);
        env_define(guard_env, var, exn);
        val_t cs = clauses;
        while (vis_pair(cs)) {
            val_t clause = vcar(cs);
            val_t test   = vcar(clause);
            val_t cbody  = vcdr(clause);
            cs = vcdr(cs);
            if (test == S_ELSE) {
                while (vis_pair(vcdr(cbody))) { eval(vcar(cbody), guard_env); cbody = vcdr(cbody); }
                return eval(vcar(cbody), guard_env);
            }
            val_t tv = eval(test, guard_env);
            if (vis_true(tv)) {
                if (vis_nil(cbody)) return tv;
                while (vis_pair(vcdr(cbody))) { eval(vcar(cbody), guard_env); cbody = vcdr(cbody); }
                return eval(vcar(cbody), guard_env);
            }
        }
        /* No clause matched: re-raise */
        scm_raise_val(exn);
    }

    if (op == S_IMPORT) {
        /* Handled in modules.c; forward to the module system.
         *
         * Supports both the traditional R7RS filter wrapper forms:
         *   (import (prefix (curry redis) r/))
         *   (import (only   (curry redis) connect get))
         *
         * And the keyword-argument forms (more readable for single imports):
         *   (import (curry redis) #:prefix r/)
         *   (import (curry redis) #:only   (connect get))
         *   (import (curry redis) #:except (internal-helper))
         *   (import (curry redis) #:rename ((connect r/connect)))
         *
         * After consuming a module spec, peek at the next element: if it is
         * a #:keyword symbol (starts with "#:") treat it as a modifier and
         * the element after it as the argument, then build the equivalent
         * traditional filter form before passing to modules_import.
         */
        extern val_t modules_import(val_t spec, val_t env);
        extern val_t scm_cons(val_t, val_t);
        while (vis_pair(rest)) {
            val_t spec = vcar(rest);
            rest = vcdr(rest);

            /* Check for an immediately following #:keyword modifier */
            if (vis_pair(rest) && vis_symbol(vcar(rest))) {
                const char *kw = sym_cstr(vcar(rest));
                if (kw[0] == '#' && kw[1] == ':') {
                    val_t kw_sym = vcar(rest); rest = vcdr(rest);
                    val_t kw_arg = vcar(rest); rest = vcdr(rest);
                    const char *name = sym_cstr(kw_sym) + 2; /* skip "#:" */

                    if (!strcmp(name, "prefix")) {
                        /* (prefix spec pfx-sym) */
                        spec = scm_cons(S_PREFIX,
                               scm_cons(spec,
                               scm_cons(kw_arg, V_NIL)));
                    } else if (!strcmp(name, "only")) {
                        /* (only spec sym...) — kw_arg is a list */
                        spec = scm_cons(S_ONLY, scm_cons(spec, kw_arg));
                    } else if (!strcmp(name, "except")) {
                        spec = scm_cons(S_EXCEPT, scm_cons(spec, kw_arg));
                    } else if (!strcmp(name, "rename")) {
                        /* (rename spec (old new)...) — kw_arg is a list of pairs */
                        spec = scm_cons(S_RENAME, scm_cons(spec, kw_arg));
                    }
                    /* Unknown #:keyword: fall through, import spec as-is */
                }
            }

            modules_import(spec, env);
        }
        return V_VOID;
    }

    if (op == S_DEFINED_P) {
        /* (defined? sym) — #t if sym is bound in the current environment,
         * #f otherwise.  Special form so the symbol is not evaluated. */
        if (!vis_pair(rest) || !vis_symbol(vcar(rest)))
            scm_raise(V_FALSE, "defined?: expected a symbol");
        return !vis_false(env_lookup_or_false(env, vcar(rest))) ? V_TRUE : V_FALSE;
    }

    if (op == S_DEFINE_LIBRARY) {
        extern val_t modules_define_library(val_t form, val_t env);
        return modules_define_library(expr, env);
    }

    if (op == S_LIBRARY) {
        extern val_t modules_define_r6rs_library(val_t form, val_t env);
        return modules_define_r6rs_library(expr, env);
    }

    /* ---- (symbolic x y z ...) — bind names as symbolic unknowns ---- */
    if (op == S_SYMBOLIC) {
        val_t p = rest;
        while (vis_pair(p)) {
            val_t name = vcar(p);
            if (!vis_symbol(name))
                scm_raise(V_FALSE, "symbolic: expected symbol, got non-symbol");
            env_define(env, name, sx_make_var(name));
            p = vcdr(p);
        }
        return V_VOID;
    }

    /* ---- Macro / syntax transformer ---- */
    /* ---- define-rule / define-ruleset ---- */

    if (op == S_DEFINE_RULE) {
        /* (define-rule PATTERN → TEMPLATE [#:when GUARD]) */
        if (!vis_pair(rest) || !vis_pair(vcdr(rest)) || !vis_pair(vcddr(rest)))
            scm_raise(V_FALSE, "define-rule: expected (define-rule pattern → template)");
        val_t pattern  = vcar(rest);
        /* skip → (second element) */
        val_t tmpl     = vcar(vcddr(rest));
        val_t trailing = vcdr(vcddr(rest));

        val_t guard_expr = V_FALSE;
        if (vis_pair(trailing) && vcar(trailing) == S_KW_WHEN && vis_pair(vcdr(trailing)))
            guard_expr = vcadr(trailing);

        val_t pvars = sx_pattern_vars(pattern);

        /* Compile action: (lambda (pvars...) template) */
        val_t lam_form = make_pair(S_LAMBDA, make_pair(pvars, make_pair(tmpl, V_NIL)));
        val_t action_fn = eval(lam_form, env);

        val_t guard_fn = V_FALSE;
        if (guard_expr != V_FALSE) {
            val_t g_form = make_pair(S_LAMBDA, make_pair(pvars, make_pair(guard_expr, V_NIL)));
            guard_fn = eval(g_form, env);
        }

        sx_rule_add(pattern, pvars, guard_fn, action_fn, V_FALSE);
        return V_VOID;
    }

    if (op == S_DEFINE_RULESET) {
        /* (define-ruleset NAME [PATTERN → TEMPLATE [#:when GUARD]] ...) */
        if (!vis_pair(rest))
            scm_raise(V_FALSE, "define-ruleset: expected (define-ruleset name clause ...)");
        val_t name    = vcar(rest);
        val_t clauses = vcdr(rest);

        while (vis_pair(clauses)) {
            val_t clause = vcar(clauses);
            clauses = vcdr(clauses);
            if (!vis_pair(clause) || !vis_pair(vcdr(clause)) || !vis_pair(vcddr(clause)))
                continue; /* skip malformed clause */

            val_t pattern  = vcar(clause);
            /* skip → */
            val_t tmpl     = vcar(vcddr(clause));
            val_t trailing = vcdr(vcddr(clause));

            val_t guard_expr = V_FALSE;
            if (vis_pair(trailing) && vcar(trailing) == S_KW_WHEN && vis_pair(vcdr(trailing)))
                guard_expr = vcadr(trailing);

            val_t pvars = sx_pattern_vars(pattern);

            val_t lam_form  = make_pair(S_LAMBDA, make_pair(pvars, make_pair(tmpl, V_NIL)));
            val_t action_fn = eval(lam_form, env);

            val_t guard_fn = V_FALSE;
            if (guard_expr != V_FALSE) {
                val_t g_form = make_pair(S_LAMBDA, make_pair(pvars, make_pair(guard_expr, V_NIL)));
                guard_fn = eval(g_form, env);
            }

            sx_rule_add(pattern, pvars, guard_fn, action_fn, name);
        }
        return V_VOID;
    }

    if (op == S_DEFINE_ALGEBRA) {
        /* (define-algebra 'OP [#:commutative? BOOL] [#:associative? BOOL]
                              [#:identity VAL] [#:absorbing VAL] [#:relations FN]) */
        if (!vis_pair(rest))
            scm_raise(V_FALSE, "define-algebra: expected operator as first argument");
        val_t op_name = eval(vcar(rest), env);
        if (!vis_symbol(op_name))
            scm_raise(V_FALSE, "define-algebra: operator must be a symbol");

        bool commutative = false, associative = false;
        val_t identity = V_VOID, absorbing = V_VOID, relations_fn = V_FALSE;

        val_t kws = vcdr(rest);
        while (vis_pair(kws) && vis_pair(vcdr(kws))) {
            val_t kw  = vcar(kws);
            val_t val = eval(vcadr(kws), env);
            kws = vcdr(vcdr(kws));
            if (kw == S_KW_COMMUTATIVE) commutative  = !vis_false(val);
            else if (kw == S_KW_ASSOCIATIVE) associative = !vis_false(val);
            else if (kw == S_KW_IDENTITY)    identity     = val;
            else if (kw == S_KW_ABSORBING)   absorbing    = val;
            else if (kw == S_KW_RELATIONS)   relations_fn = val;
        }
        sx_algebra_define(op_name, commutative, associative,
                          identity, absorbing, relations_fn);
        /* Auto-bind operator name → (lambda args (apply sym-expr op args)) */
        {
            val_t sym_expr_sym = sym_intern_cstr("sym-expr");
            val_t apply_sym    = sym_intern_cstr("apply");
            val_t args_sym     = sym_intern_cstr("args");
            val_t op_quoted    = make_pair(S_QUOTE, make_pair(op_name, V_NIL));
            /* body: (apply sym-expr op 'args) → actually:
               (lambda args (apply sym-expr 'op args)) */
            val_t body = make_pair(apply_sym,
                           make_pair(sym_expr_sym,
                             make_pair(op_quoted,
                               make_pair(args_sym, V_NIL))));
            val_t rest_params = make_pair(args_sym, V_NIL);  /* dotted: just rest */
            /* (lambda args body) — variadic, rest args bound to 'args */
            val_t lam  = make_pair(S_LAMBDA,
                           make_pair(args_sym,     /* dotted rest parameter */
                             make_pair(body, V_NIL)));
            val_t proc = eval(lam, env);
            env_define(env, op_name, proc);
        }
        return V_VOID;
    }

    if (op == S_WITH_ASSUMPTIONS) {
        /* (with-assumptions ((VAR ASSUMPTION...) ...) BODY...) */
        if (!vis_pair(rest))
            scm_raise(V_FALSE, "with-assumptions: missing binding list");
        val_t clauses = vcar(rest);
        val_t body    = vcdr(rest);

        /* Save flags and apply new assumptions */
        val_t    saved_vars[32];
        uint32_t saved_flags[32];
        int nv = 0;
        val_t cl = clauses;
        while (vis_pair(cl) && nv < 32) {
            val_t clause = vcar(cl);
            if (!vis_pair(clause)) { cl = vcdr(cl); continue; }
            val_t var = eval(vcar(clause), env);
            if (!vis_symvar(var)) { cl = vcdr(cl); continue; }
            saved_vars[nv]  = var;
            saved_flags[nv] = as_symvar(var)->hdr.flags;
            val_t assumptions = vcdr(clause);
            while (vis_pair(assumptions)) {
                uint32_t flag = sx_assumption_flag(vcar(assumptions));
                if (flag) as_symvar(var)->hdr.flags |= flag;
                assumptions = vcdr(assumptions);
            }
            nv++;
            cl = vcdr(cl);
        }

        val_t result = V_VOID;
        ExnHandler h;
        h.prev = current_handler;
        h.saved_jit_depth = jit_depth_save();
        current_handler = &h;
        if (setjmp(h.jmp) == 0) {
            result = eval_body(body, env);
            current_handler = h.prev;
        } else {
            current_handler = h.prev;
            jit_depth_restore(h.saved_jit_depth);
            /* Restore flags before re-raising */
            for (int i = 0; i < nv; i++)
                as_symvar(saved_vars[i])->hdr.flags = saved_flags[i];
            scm_raise_val(h.exn);
        }
        for (int i = 0; i < nv; i++)
            as_symvar(saved_vars[i])->hdr.flags = saved_flags[i];
        return result;
    }

    val_t op_val = vis_symbol(op) ? env_lookup(env, op) : eval(op, env);
    if (vis_syntax(op_val)) {
        /* Apply transformer. Save/set/restore syntax_rules.c's "current
         * env" thread-local around this call -- it's how sr_compile_fn
         * (itself invoked exactly like this, since "syntax-rules" is
         * this same kind of T_SYNTAX value) learns the environment a
         * (syntax-rules ...) expression is being evaluated in, to
         * capture as the resulting transformer's def_env. Save/restore
         * (not a bare one-way set) so nested macro expansion -- this
         * transformer's own output itself containing another macro use,
         * or a (syntax-rules ...) evaluated while already inside some
         * outer transformer's apply() -- doesn't leak the wrong env
         * into an unrelated later expansion once this call returns.
         * SCM_PROTECT, not a bare set/call/set, so a transformer that
         * raises (a malformed macro use, a procedural transformer's own
         * error) still restores the saved value before the exception
         * continues propagating -- otherwise a later, unrelated
         * (syntax-rules ...) evaluation in the same thread could pick up
         * this stale env instead of its own correct one. */
        val_t saved_sr_env = sr_get_current_env();
        sr_set_current_env(env);
        val_t transformed = V_VOID;
        ExnHandler sr_h;
        SCM_PROTECT(sr_h,
            transformed = apply(as_syntax(op_val)->transformer, make_pair(expr, V_NIL)),
            { sr_set_current_env(saved_sr_env); scm_raise_val(sr_h.exn); });
        sr_set_current_env(saved_sr_env);
        expr = transformed; goto tail;
    }

    /* ---- Function application ---- */
    {
        val_t proc = vis_symbol(op) ? op_val : eval(op, env);

        /* Evaluate arguments directly into an array — no cons allocation.
         * Sized to the actual argument-expression count (list_length on
         * the still-unevaluated `rest`, which costs nothing extra to
         * compute), not a bare fixed 64-slot cap: the previous
         * `argc < 64` loop bound silently STOPPED EVALUATING expressions
         * past the 64th, so a >64-argument call not only truncated the
         * argument values but dropped the side effects of every later
         * argument expression entirely, with no error — worse than the
         * equivalent bug fixed in runtime.c's apply(), which just
         * mis-sized an already-evaluated array. Only reachable via the
         * tree-walker (`-l`/`load`, or library/define-library bodies,
         * which stay permanently tree-walked by design), not compiled
         * code.
         *
         * gc_alloc (GC-managed; never GC_MALLOC_ATOMIC — a val_t may be a
         * heap pointer) rather than malloc for the rare >64 case, and no
         * matching free() anywhere below: an earlier version of this fix
         * used malloc + a free() before every one of this block's many
         * exit points (several returns, a goto tail, a longjmp) — review
         * found several of those exits are reached by an ordinary raised
         * condition (a wrong-type/wrong-arity argument, arity mismatch in
         * env_bind_arr, etc.), which longjmps past the textually-later
         * free(), leaking the buffer on what is a completely mundane,
         * everyday error path, not a rare one (confirmed empirically:
         * ~190MB over 200k guard-wrapped iterations). GC allocation has
         * no such window — nothing needs to free it, so nothing can
         * forget to, on any exit path, present or future. */
        int argc_total = list_length(rest);
        val_t stack_arr[64];
        val_t *arr = (argc_total <= 64) ? stack_arr
                                         : (val_t *)gc_alloc((size_t)argc_total * sizeof(val_t));
        int argc = 0;
        for (val_t r = rest; vis_pair(r); r = vcdr(r))
            arr[argc++] = eval(vcar(r), env);

        if (vis_prim(proc)) {
            Primitive *prim = as_prim(proc);
            if (prim->min_args >= 0 && argc < prim->min_args)
                scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "%s: too few arguments (got %d, need %d)",
                          prim->name, argc, prim->min_args);
            if (prim->max_args >= 0 && argc > prim->max_args)
                scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "%s: too many arguments (got %d, max %d)",
                          prim->name, argc, prim->max_args);
            if (curry_profiling_level >= 3)
                profiling_record_prim(sym_intern_cstr(prim->name));
            return prim->fn(argc, arr, prim->ud);
        }

        if (vis_closure(proc)) {
            Closure *c = as_clos(proc);
            if (curry_profiling_level >= 2 && vis_symbol(c->name)) {
                /* Level 2+: sacrifice TCO for named closures to get wall-clock
                 * timing.  Skip when re-entering the same closure (self-tail-
                 * recursive loops) — otherwise the stack grows without bound. */
                static _Thread_local val_t prof2_current = 0;
                if (proc != prof2_current) {
                    val_t saved = prof2_current;
                    prof2_current = proc;
                    val_t benv = env_bind_arr(vptr(c->env), c->params, argc, arr);
                    uint64_t t0 = profiling_now_ns();
                    val_t r = eval_body(c->body, benv);
                    profiling_record_timed(c->name, t0);
                    prof2_current = saved;
                    return r;
                }
            }
            if (curry_profiling_level >= 1 && vis_symbol(c->name))
                profiling_record_call_tco(c->name);
            env = env_bind_arr(vptr(c->env), c->params, argc, arr);
            val_t body = c->body;
            bool body_in_body = false;
            while (vis_pair(vcdr(body))) {
                val_t bform = vcar(body);
                if (is_definition(bform)) {
                    if (body_in_body)
                        scm_raise(V_FALSE, "internal definition after expression in body (R7RS violation)");
                } else { body_in_body = true; }
                eval(bform, env);
                body = vcdr(body);
            }
            expr = vcar(body); goto tail;
        }

        if (vis_cont(proc)) {
            Continuation *cont = as_cont(proc);
            cont->result = argc > 0 ? arr[0] : V_VOID;
            /* Memory barrier: clang (ARM64 -O2) dead-store-eliminates the write
             * to cont->result because longjmp() is declared noreturn and the
             * store appears dead.  The barrier forces the write to memory so
             * eval_call_cc's volatile reload sees the correct value after longjmp. */
            __asm__ volatile("" ::: "memory");
            wind_unwind_to((WindFrame *)cont->wind_top);
            longjmp(*(jmp_buf *)cont->jmpbuf, 1);
        }

        if (vis_param(proc)) {
            Parameter *p = as_param(proc);
            if (argc == 0) return p->init;
            val_t newval = arr[0];
            if (!vis_false(p->converter)) newval = apply(p->converter, make_pair(newval, V_NIL));
            gc_wb_slot(&p->init, newval);
            return V_VOID;
        }

        if (vis_traced(proc)) {
            return apply_arr(proc, argc, arr);
        }

        if (vis_bcclosure(proc)) {
            return apply_arr(proc, argc, arr);
        }

        if (vis_symfn(proc)) {
            /* Applying a symbolic function — create SX_APPLY node */
            return sx_make_apply(proc, argc, arr);
        }

        scm_raise_code(EC_NOT_A_PROCEDURE, "not a procedure: %s",
                  vis_symbol(op) ? sym_cstr(op) : "#<value>");
    }
}

