#ifndef CURRY_EVAL_H
#define CURRY_EVAL_H

/*
 * Evaluator for Curry Scheme (R7RS).
 *
 * Uses an iterative trampoline with a `goto` for tail-call optimization.
 * All R7RS special forms are handled directly in eval().
 *
 * Continuations: escape-only in phase 1 (call/cc captures the C stack via
 * setjmp; only upward escapes work).  Full first-class continuations via
 * stack copying are deferred to phase 2.
 *
 * Error handling: scm_raise() throws a C exception via longjmp.  Each
 * eval() call site can install a handler with scm_with_exception_handler().
 */

#include "value.h"
#include "env.h"
#include <setjmp.h>
#include <stdarg.h>

/* Initialize the evaluator (call after sym_init, env_init, num_init) */
void eval_init(void);

/* Evaluate expr in env.  Implements proper tail calls. */
val_t eval(val_t expr, val_t env);

/* Apply a procedure to an argument list */
val_t apply(val_t proc, val_t args);

/* Apply with a C array of arguments */
val_t apply_arr(val_t proc, int argc, val_t *argv);

/* Evaluate a list of expressions, return the last value */
val_t eval_body(val_t exprs, val_t env);

/* ---- Dynamic wind stack ---- */

/* One frame per active dynamic-wind call.  Stack-allocated inside
 * prim_dynamic_wind; valid only while that C frame is live (escape
 * continuations only — sufficient for phase 1). */
typedef struct WindFrame {
    val_t             before;
    val_t             after;
    struct WindFrame *prev;
} WindFrame;

/* Thread-local wind stack (NULL = empty) */
#ifdef __cplusplus
extern "C" WindFrame **curry_current_wind_ptr(void);
#define current_wind (*curry_current_wind_ptr())
#else
extern _Thread_local WindFrame *current_wind;
#endif

/* ---- Exception system ---- */

typedef struct ExnHandler {
    jmp_buf         jmp;
    val_t           exn;    /* filled on raise */
    struct ExnHandler *prev;
    void           *saved_shadow; /* gc_shadow_stack at setjmp time — restored on longjmp */
} ExnHandler;

/* Thread-local current exception handler chain.
 * Use __thread (not C++ thread_local) in both branches so that C++ callers
 * (e.g. qt6.so) reference the TLV descriptor symbol _current_handler directly
 * rather than generating a __ZTW wrapper call.  thread_local in C++ emits a
 * lazy-init wrapper (__ZTW15current_handler) that is never defined by the main
 * binary (which compiles eval.c as C), so the dynamic lookup resolves to null
 * and the first SCM_PROTECT in a .so crashes at 0x0. */
#ifdef __cplusplus
/* current_handler is a C _Thread_local defined in eval.c.  The C++ TLS
 * wrapper (__ZTW...) is NOT exported from the main binary, so C++ dylibs
 * must access it via this plain C function instead. */
extern "C" ExnHandler **curry_current_handler_ptr(void);
#define current_handler (*curry_current_handler_ptr())
#else
extern _Thread_local ExnHandler *current_handler;
#endif

/* JIT call depth guard — defined in eval.c, C-linkage helpers used by jit.cpp
 * to avoid C++ TLS wrapper ABI mismatch on macOS/arm64. */
#ifdef __cplusplus
extern "C" {
    void jit_depth_push(void);
    void jit_depth_pop(void);
}
#else
extern _Thread_local int g_jit_call_depth;
#define JIT_CALL_DEPTH_LIMIT 512
#endif

/* ---- CL-style condition / restart chains ---- */

/* Non-unwinding handler established by handler-bind.
 * Handlers are called in-place (stack intact); if a handler returns
 * normally the next handler in the chain is tried. */
typedef struct CondHandler {
    val_t              type_sym; /* condition type to match, or V_FALSE = any */
    val_t              proc;     /* 1-arg Scheme procedure */
    struct CondHandler *prev;
} CondHandler;

/* Restart frame established by with-restarts.
 *
 * invoke-restart raises the Restart object itself as an exception rather than
 * longjmping directly.  The SCM_PROTECT inside with-restarts catches it; if
 * it belongs to this frame the thunk is run, otherwise it is re-raised.  This
 * uses the normal ExnHandler unwind path so the VM state is never corrupted. */
typedef struct RestartFrame {
    val_t               restarts;   /* list of Restart objects */
    struct RestartFrame *prev;
} RestartFrame;

#ifndef __cplusplus
extern _Thread_local CondHandler  *current_cond_handler;
extern _Thread_local RestartFrame *current_restart_frame;
#endif

/* Walk the non-unwinding CondHandler chain; called by scm_raise_val and
 * condition_signal.  Defined in eval.c. */
void walk_cond_handlers(val_t exn, CondHandler *h);

/* Raise an exception (never returns) */
void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));
void scm_raise_val(val_t exn) __attribute__((noreturn));

/*
 * longjmp-safe shadow-stack helpers.  Declared here (with C linkage so C++
 * modules can use SCM_PROTECT) and defined in gc.c.  Under C++ the save
 * returns NULL and restore is a no-op; C++ modules never use GC_AUTOFRAME.
 */
#ifdef __cplusplus
extern "C" {
#endif
void *gc_shadow_save(void);
void  gc_shadow_restore(void *saved);
#ifdef __cplusplus
}
#endif

/* Install/remove a handler frame (used by guard, with-exception-handler) */
#define SCM_PROTECT(h, body, on_exn) do {          \
    (h).prev          = current_handler;            \
    (h).saved_shadow  = gc_shadow_save();           \
    current_handler   = &(h);                       \
    if (setjmp((h).jmp) == 0) {                     \
        body;                                       \
        current_handler = (h).prev;                 \
    } else {                                        \
        current_handler = (h).prev;                 \
        gc_shadow_restore((h).saved_shadow);        \
        on_exn;                                     \
    }                                               \
} while (0)

/* ---- Load / include ---- */
val_t scm_load(const char *path, val_t env);

/* ---- Quasiquote expansion (used by compiler) ---- */
val_t expand_qq(val_t form, val_t env, int depth);

#endif /* CURRY_EVAL_H */
