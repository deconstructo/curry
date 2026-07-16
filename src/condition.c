/*
 * CL-style condition system for Curry Scheme.
 *
 * Three thread-local chains:
 *
 *   current_cond_handler  — CondHandler list; non-unwinding handler-bind.
 *                           Declared in eval.h, defined in eval.c so that
 *                           all translation units share the same TLS slot.
 *
 *   current_restart_frame — RestartFrame list; with-restarts.
 *                           Likewise declared in eval.h, defined in eval.c.
 *
 * The condition type hierarchy is a global hash table mapping each type
 * symbol to its list of parent type symbols.  condition_is_a() walks this
 * graph breadth-first.
 */

#include "condition.h"
#include "gc.h"
#include "eval.h"
#include "vm.h"
#include "env.h"
#include "symbol.h"
#include "object.h"
#include "value.h"
#include "set.h"
#include "builtins.h"

#include <string.h>
#include <stdio.h>
#include <setjmp.h>

/* ---- Condition type hierarchy ---- */

/* Maps type-sym → list of parent type-syms (val_t list).
 * Uses the existing hash-table infrastructure. */
static val_t g_hierarchy = V_FALSE; /* hash table, initialised in condition_init */

void condition_init(void) {
    g_hierarchy = hash_make(SET_CMP_EQ); /* symbol keys → eq? */
}

void condition_type_register(val_t type_sym, val_t parents) {
    hash_set(g_hierarchy, type_sym, parents);
}

/* Breadth-first check: is exn's type a subtype of type_sym? */
bool condition_is_a(val_t exn, val_t type_sym) {
    val_t exn_type;
    if (vis_condition(exn))
        exn_type = as_condition(exn)->type_sym;
    else if (vis_error(exn))
        /* T_ERROR objects match the root condition type 'error */
        exn_type = sym_intern_cstr("error");
    else
        return false;

    /* Direct match */
    if (exn_type == type_sym) return true;
    if (vis_false(g_hierarchy)) return false;

    /* Walk hierarchy */
    val_t worklist = scm_cons(exn_type, V_NIL);
    val_t visited  = hash_make(SET_CMP_EQ);
    while (vis_pair(worklist)) {
        val_t cur  = vcar(worklist);
        worklist   = vcdr(worklist);
        if (!vis_false(hash_ref(visited, cur, V_FALSE))) continue;
        hash_set(visited, cur, V_TRUE);
        if (cur == type_sym) return true;
        val_t parents = hash_ref(g_hierarchy, cur, V_NIL);
        for (val_t p = parents; vis_pair(p); p = vcdr(p))
            worklist = scm_cons(vcar(p), worklist);
    }
    return false;
}

/* ---- Condition object ---- */

val_t condition_make(val_t type_sym, val_t fields, val_t message) {
    Condition *c = CURRY_NEW(Condition);
    c->hdr.type  = T_CONDITION;
    c->hdr.flags = 0;
    c->type_sym  = type_sym;
    c->fields    = fields;
    c->message   = message;
    return vptr(c);
}

val_t condition_field(val_t cond, val_t field_sym) {
    if (!vis_condition(cond)) return V_FALSE;
    val_t fields = as_condition(cond)->fields;
    for (val_t p = fields; vis_pair(p); p = vcdr(p)) {
        val_t kv = vcar(p);
        if (vis_pair(kv) && vcar(kv) == field_sym)
            return vcdr(kv);
    }
    return V_FALSE;
}

/* ---- Restart object ---- */

val_t restart_make(val_t name, val_t desc, val_t thunk) {
    Restart *r = CURRY_NEW(Restart);
    r->hdr.type  = T_RESTART;
    r->hdr.flags = 0;
    r->name        = name;
    r->description = desc;
    r->thunk       = thunk;
    return vptr(r);
}

/* ---- Signal (non-unwinding) ---- */

void condition_signal(val_t cond) {
    /* For explicit (signal cond): walk non-unwinding handlers but do NOT
     * fall through to the ExnHandler chain — signal returns normally if no
     * handler claims the condition.  walk_cond_handlers declared in eval.h. */
    if (current_cond_handler)
        walk_cond_handlers(cond, current_cond_handler);
}

/* ---- handler-bind-1 ---- */

val_t condition_handler_bind(val_t type_sym, val_t proc, val_t thunk) {
    CondHandler h;
    h.type_sym = type_sym;
    h.proc     = proc;
    h.prev     = current_cond_handler;
    current_cond_handler = &h;

    val_t result = V_VOID;
    bool  raised = false;
    val_t rexn   = V_FALSE;
    ExnHandler eh;
    SCM_PROTECT(eh, {
        result = apply_arr(thunk, 0, NULL);
    }, {
        raised = true; rexn = eh.exn;
    });

    current_cond_handler = h.prev;
    if (raised) scm_raise_val(rexn);
    return result;
}

/* ---- with-restarts ---- */

/*
 * invoke-restart raises the Restart object itself as an exception (via
 * scm_raise_val).  Restart objects are T_RESTART, which never matches any
 * condition type, so they propagate past all handler-bind handlers untouched.
 * Intermediate SCM_PROTECT frames (e.g. inside handler-bind) re-raise them.
 * with-restarts catches the exception here, checks ownership, and runs the
 * thunk — using the normal VM unwind path, so the VM state stays consistent.
 */
val_t condition_with_restarts(val_t restarts, val_t thunk) {
    RestartFrame frame;
    frame.restarts         = restarts;
    frame.prev             = current_restart_frame;
    current_restart_frame  = &frame;

    /* Mirror what OP_PUSH_HANDLER does: snapshot the VM state so we can
     * restore it if a restart (or any exception) unwinds through us.
     * After a longjmp the C stack is restored but the heap-allocated VM
     * struct retains the aborted execution's sp/frame_count. */
    val_t   *saved_sp            = vm->sp;
    int      saved_frame_count   = vm->frame_count;
    Upvalue *saved_open_upvalues = vm->open_upvalues;
    int      saved_handler_count = vm->handler_count;

    bool  raised = false;
    val_t result = V_VOID;
    val_t exn    = V_FALSE;
    ExnHandler eh;
    SCM_PROTECT(eh, {
        result = apply_arr(thunk, 0, NULL);
    }, {
        raised = true; exn = eh.exn;
    });
    /* SCM_PROTECT has restored current_handler before reaching here */

    current_restart_frame = frame.prev;

    if (!raised) return result;

    /* Restore VM state to what it was when with-restarts was entered,
     * so the restart thunk (or re-raised exception) starts from a clean slate. */
    vm->sp            = saved_sp;
    vm->frame_count   = saved_frame_count;
    vm->open_upvalues = saved_open_upvalues;
    vm->handler_count = saved_handler_count;

    if (vis_restart(exn)) {
        /* Check if this restart belongs to us */
        for (val_t p = restarts; vis_pair(p); p = vcdr(p)) {
            if (vcar(p) == exn)
                return apply_arr(as_restart(exn)->thunk, 0, NULL);
        }
    }
    scm_raise_val(exn);  /* not ours (or not a restart) — re-raise */
}

/* ---- invoke-restart / find-restart ---- */

val_t condition_find_restart(val_t name) {
    for (RestartFrame *f = current_restart_frame; f; f = f->prev) {
        for (val_t p = f->restarts; vis_pair(p); p = vcdr(p)) {
            val_t r = vcar(p);
            if (vis_restart(r) && as_restart(r)->name == name)
                return r;
        }
    }
    return V_FALSE;
}

val_t condition_invoke_restart(val_t name) {
    for (RestartFrame *f = current_restart_frame; f; f = f->prev) {
        for (val_t p = f->restarts; vis_pair(p); p = vcdr(p)) {
            val_t r = vcar(p);
            if (vis_restart(r) && as_restart(r)->name == name)
                scm_raise_val(r);  /* raise Restart object; with-restarts catches it */
        }
    }
    scm_raise(V_FALSE, "invoke-restart: no restart named '%s'",
              vis_symbol(name) ? sym_cstr(name) : "?");
}

/* ---- Scheme builtins ---- */

static void cond_def(val_t env, const char *name,
                     val_t (*fn)(int, val_t *, void *), int mn, int mx) {
    Primitive *p = (Primitive *)gc_alloc_pinned(sizeof(Primitive));
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->fn = fn; p->min_args = mn; p->max_args = mx; p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}

static val_t prim_make_condition(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "make-condition: type must be a symbol");
    val_t fields  = ac > 1 ? av[1] : V_NIL;
    val_t message = ac > 2 ? av[2] : V_FALSE;
    return condition_make(av[0], fields, message);
}
static val_t prim_condition_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_condition(av[0])); }
static val_t prim_condition_type(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_condition(av[0])) scm_raise(V_FALSE, "condition-type: not a condition");
    return as_condition(av[0])->type_sym;
}
static val_t prim_condition_fields(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_condition(av[0])) scm_raise(V_FALSE, "condition-fields: not a condition");
    return as_condition(av[0])->fields;
}
static val_t prim_condition_message(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (vis_condition(av[0])) return as_condition(av[0])->message;
    if (vis_error(av[0]))     return as_err(av[0])->message;
    return V_FALSE;
}
static val_t prim_condition_backtrace(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (vis_error(av[0])) return as_err(av[0])->backtrace;
    return V_NIL; /* user-signalled Condition objects don't capture frames yet */
}
static val_t prim_condition_field(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return condition_field(av[0], av[1]);
}
static val_t prim_condition_is_a(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return vbool(condition_is_a(av[0], av[1]));
}
static val_t prim_condition_register(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "%%condition-type-register!: type must be a symbol");
    condition_type_register(av[0], av[1]);
    return V_VOID;
}
static val_t prim_signal(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; condition_signal(av[0]); return V_VOID; }
static val_t prim_handler_bind_1(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return condition_handler_bind(av[0], av[1], av[2]); }
static val_t prim_with_restarts(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return condition_with_restarts(av[0], av[1]); }
static val_t prim_invoke_restart(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return condition_invoke_restart(av[0]); }
static val_t prim_find_restart(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return condition_find_restart(av[0]); }
static val_t prim_make_restart(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_symbol(av[0]))
        scm_raise(V_FALSE, "make-restart: name must be a symbol");
    return restart_make(av[0], av[1], av[2]);
}
static val_t prim_restart_p(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; return vbool(vis_restart(av[0])); }
static val_t prim_restart_name(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_restart(av[0])) scm_raise(V_FALSE, "restart-name: not a restart");
    return as_restart(av[0])->name;
}
static val_t prim_restart_description(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_restart(av[0])) scm_raise(V_FALSE, "restart-description: not a restart");
    return as_restart(av[0])->description;
}

void condition_register_builtins(val_t env) {
    cond_def(env, "%make-condition",         prim_make_condition,    1, 3);
    cond_def(env, "condition?",              prim_condition_p,       1, 1);
    cond_def(env, "condition-type",          prim_condition_type,    1, 1);
    cond_def(env, "condition-fields",        prim_condition_fields,  1, 1);
    cond_def(env, "condition-message",       prim_condition_message, 1, 1);
    cond_def(env, "condition-backtrace",     prim_condition_backtrace,1,1);
    cond_def(env, "condition-field",         prim_condition_field,   2, 2);
    cond_def(env, "condition-is-a?",         prim_condition_is_a,    2, 2);
    cond_def(env, "%condition-type-register!",prim_condition_register,2,2);
    cond_def(env, "%signal",                 prim_signal,            1, 1);
    cond_def(env, "%handler-bind-1",         prim_handler_bind_1,    3, 3);
    cond_def(env, "%with-restarts",          prim_with_restarts,     2, 2);
    cond_def(env, "%invoke-restart",         prim_invoke_restart,    1, 1);
    cond_def(env, "%find-restart",           prim_find_restart,      1, 1);
    cond_def(env, "%make-restart",           prim_make_restart,      3, 3);
    cond_def(env, "restart?",                prim_restart_p,         1, 1);
    cond_def(env, "restart-name",            prim_restart_name,      1, 1);
    cond_def(env, "restart-description",     prim_restart_description,1,1);
}
