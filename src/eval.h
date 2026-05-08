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
extern thread_local WindFrame *current_wind;
#else
extern _Thread_local WindFrame *current_wind;
#endif

/* ---- Exception system ---- */

typedef struct ExnHandler {
    jmp_buf         jmp;
    val_t           exn;    /* filled on raise */
    struct ExnHandler *prev;
} ExnHandler;

/* Thread-local current exception handler chain.
 * Use __thread (not C++ thread_local) in both branches so that C++ callers
 * (e.g. qt6.so) reference the TLV descriptor symbol _current_handler directly
 * rather than generating a __ZTW wrapper call.  thread_local in C++ emits a
 * lazy-init wrapper (__ZTW15current_handler) that is never defined by the main
 * binary (which compiles eval.c as C), so the dynamic lookup resolves to null
 * and the first SCM_PROTECT in a .so crashes at 0x0. */
#ifdef __cplusplus
<<<<<<< HEAD
extern __thread ExnHandler *current_handler;
=======
/* current_handler is a C _Thread_local defined in eval.c.  The C++ TLS
 * wrapper (__ZTW...) is NOT exported from the main binary, so C++ dylibs
 * must access it via this plain C function instead. */
extern "C" ExnHandler **curry_current_handler_ptr(void);
#define current_handler (*curry_current_handler_ptr())
>>>>>>> 1462a4e (  src/eval.c — added one function after the current_handler / current_wind TLS definitions:)
#else
extern _Thread_local ExnHandler *current_handler;
#endif

/* Raise an exception (never returns) */
void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));
void scm_raise_val(val_t exn) __attribute__((noreturn));

/* Install/remove a handler frame (used by guard, with-exception-handler) */
#define SCM_PROTECT(h, body, on_exn) do { \
    (h).prev = current_handler;           \
    current_handler = &(h);               \
    if (setjmp((h).jmp) == 0) {          \
        body;                             \
        current_handler = (h).prev;       \
    } else {                              \
        current_handler = (h).prev;       \
        on_exn;                           \
    }                                     \
} while (0)

/* ---- Load / include ---- */
val_t scm_load(const char *path, val_t env);

#endif /* CURRY_EVAL_H */
