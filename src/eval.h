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

/* Everything below has C linkage. Wrapping the whole header (rather than
 * guarding each declaration individually) means a C++ translation unit that
 * includes eval.h unwrapped (e.g. a module's .cpp) can never again hit the
 * qt6.so bug: vm_exn_state_save/vm_exn_state_restore were declared without
 * extern "C" here, so qt6.cpp's C++ compilation mangled the names while
 * vm.c (compiled as C) exported them unmangled, and qt6.so failed to dlopen
 * with an undefined symbol at runtime. The per-declaration extern "C" blocks
 * below (for WindFrame/current_handler/jit_depth/etc.) still special-case
 * their own C-vs-C++ ABI differences (thread_local wrapper avoidance) and
 * are left as-is; this outer wrap is a backstop for everything else. */
#ifdef __cplusplus
extern "C" {
#endif

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
extern CURRY_THREAD_LOCAL WindFrame *current_wind;
#endif

/* ---- Exception system ---- */

typedef struct ExnHandler {
    jmp_buf         jmp;
    val_t           exn;    /* filled on raise */
    struct ExnHandler *prev;
    void           *saved_shadow;  /* gc_shadow_stack at setjmp time — restored on longjmp */
    int             saved_inhibit; /* gc_inhibit_count at setjmp time — restored on longjmp */
    int             saved_jit_depth; /* g_jit_call_depth at setjmp time — restored on longjmp;
                                         without this, an exception raised from inside a
                                         JIT-to-JIT call chain (curry_jit_apply_arr) skips the
                                         matching jit_depth_pop()/g_jit_call_depth-- on unwind,
                                         permanently inflating the counter until it pins at
                                         JIT_CALL_DEPTH_LIMIT and silently disables the JIT
                                         fast path for the rest of the thread's lifetime. */
    int             saved_vm_frame_count; /* vm->frame_count at setjmp time — restored on longjmp */
    void           *saved_vm_sp;          /* vm->sp at setjmp time — restored on longjmp */
    void           *saved_vm_open_upvalues; /* vm->open_upvalues at setjmp time — restored on longjmp;
                                         together with the two fields above, mirrors what
                                         OP_PUSH_HANDLER already saves/restores for purely
                                         in-VM catches (VmHandlerInfo in vm.h). Without this, a
                                         protected body that calls into VM-compiled bytecode
                                         (e.g. a guard around a call to a bytecode closure) which
                                         then raises leaves vm->sp/frame_count pointing wherever
                                         the raise happened deep inside vm_run(), because the
                                         longjmp back to this handler unwinds straight past
                                         apply()/apply_arr()'s own "restore saved_sp after
                                         vm_run() returns" cleanup — that cleanup only runs on a
                                         normal return, never on a longjmp past it. The corrupted
                                         vm->sp then makes the *next* VM opcode read garbage
                                         stack slots as if they were arguments, surfacing as
                                         nonsensical "too few arguments" or type errors on
                                         whatever unrelated code runs after the handler catches. */
} ExnHandler;

/* Thread-local current exception handler chain.
 * Use __thread (not C++ thread_local) in both branches so that C++ callers
 * (e.g. qt6.so) reference the TLV descriptor symbol _current_handler directly
 * rather than generating a __ZTW wrapper call.  thread_local in C++ emits a
 * lazy-init wrapper (__ZTW15current_handler) that is never defined by the main
 * binary (which compiles eval.c as C), so the dynamic lookup resolves to null
 * and the first SCM_PROTECT in a .so crashes at 0x0. */
#ifdef __cplusplus
/* current_handler is a C CURRY_THREAD_LOCAL defined in eval.c.  The C++ TLS
 * wrapper (__ZTW...) is NOT exported from the main binary, so C++ dylibs
 * must access it via this plain C function instead. */
extern "C" ExnHandler **curry_current_handler_ptr(void);
#define current_handler (*curry_current_handler_ptr())
#else
extern CURRY_THREAD_LOCAL ExnHandler *current_handler;
#endif

/* JIT call depth guard — defined in eval.c, C-linkage helpers used by jit.cpp
 * to avoid C++ TLS wrapper ABI mismatch on macOS/arm64. save/restore are used
 * by every exception-unwind path (SCM_PROTECT, the VM's OP_PUSH_HANDLER, and
 * eval.c's guard) to keep g_jit_call_depth from leaking across a caught
 * exception raised from inside a JIT-to-JIT call. */
#ifdef __cplusplus
extern "C" {
    void jit_depth_push(void);
    void jit_depth_pop(void);
    int  jit_depth_save(void);
    void jit_depth_restore(int saved);
}
#else
extern CURRY_THREAD_LOCAL int g_jit_call_depth;
int  jit_depth_save(void);
void jit_depth_restore(int saved);
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
extern CURRY_THREAD_LOCAL CondHandler  *current_cond_handler;
extern CURRY_THREAD_LOCAL RestartFrame *current_restart_frame;
#endif

/* Walk the non-unwinding CondHandler chain; called by scm_raise_val and
 * condition_signal.  Defined in eval.c. */
void walk_cond_handlers(val_t exn, CondHandler *h);

/* Raise an exception (never returns) */
void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void scm_raise_val(val_t exn) __attribute__((noreturn));

/* Like scm_raise, but stamps a stable machine-legible symbol code (e.g.
 * 'wrong-type-argument) on the error object; see docs/reference/error-codes.md. */
void scm_raise_code(val_t code, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));

/*
 * longjmp-safe shadow-stack and inhibit-counter helpers.
 * Declared here (with C linkage so C++ modules can use SCM_PROTECT) and
 * defined in gc.c.  Under C++ the shadow save returns NULL and restore is a
 * no-op; C++ modules never use GC_AUTOFRAME.
 */
#ifdef __cplusplus
extern "C" {
#endif
void *gc_shadow_save(void);
void  gc_shadow_restore(void *saved);
int   gc_inhibit_save(void);
void  gc_inhibit_restore(int saved);
#ifdef __cplusplus
}
#endif

/* VM operand-stack/frame/upvalue state — saved/restored across an ExnHandler
 * unwind the same way OP_PUSH_HANDLER already does for purely in-VM catches
 * (VmHandlerInfo in vm.h). Defined in vm.c; opaque void* types here so
 * SCM_PROTECT can be used from translation units that don't include vm.h.
 * Without this, a protected body that calls into VM-compiled bytecode which
 * then raises leaves vm->sp/frame_count pointing wherever the raise happened
 * deep inside vm_run() — the longjmp back to the handler unwinds straight
 * past apply()/apply_arr()'s own "restore saved sp after vm_run() returns"
 * cleanup, which only runs on a normal return, never on a longjmp past it. */
#ifdef __cplusplus
extern "C" {
#endif
void vm_exn_state_save(int *frame_count, void **sp, void **open_upvalues);
void vm_exn_state_restore(int frame_count, void *sp, void *open_upvalues);
#ifdef __cplusplus
}
#endif

/* Install/remove a handler frame (used by guard, with-exception-handler) */
#define SCM_PROTECT(h, body, on_exn) do {              \
    (h).prev           = current_handler;               \
    (h).saved_shadow   = gc_shadow_save();              \
    (h).saved_inhibit  = gc_inhibit_save();             \
    (h).saved_jit_depth = jit_depth_save();             \
    vm_exn_state_save(&(h).saved_vm_frame_count,        \
                       &(h).saved_vm_sp,                \
                       &(h).saved_vm_open_upvalues);    \
    current_handler    = &(h);                          \
    if (setjmp((h).jmp) == 0) {                        \
        body;                                           \
        current_handler = (h).prev;                     \
    } else {                                            \
        current_handler = (h).prev;                     \
        gc_shadow_restore((h).saved_shadow);            \
        gc_inhibit_restore((h).saved_inhibit);          \
        jit_depth_restore((h).saved_jit_depth);         \
        vm_exn_state_restore((h).saved_vm_frame_count,  \
                              (h).saved_vm_sp,           \
                              (h).saved_vm_open_upvalues); \
        on_exn;                                         \
    }                                                   \
} while (0)

/* ---- Load / include ---- */
val_t scm_load(const char *path, val_t env);

/* Directory-context stack scm_load() resolves relative paths against
 * (runtime.c) -- mark/release, not push/pop, so releasing back to a
 * saved mark is correct even across an exception unwind or a silently-
 * skipped push past MAX_LOAD_DEPTH (see the header comment on this
 * block in runtime.c for why a fixed push/pop pairing isn't safe here).
 * Exposed so load_scheme_module (modules.c) and main.c's positional-
 * script-argument path, both independent file-reading loops not
 * implemented in terms of scm_load, can mark/release the same way
 * around their own read/eval loops. */
void load_push_dir(const char *path);
int  load_dir_mark(void);
void load_dir_release(int mark);

/* Cross-thread inheritance for actor_spawn (actors.c): call
 * load_dir_snapshot() on the spawning thread to capture its current
 * directory-context stack, then load_dir_adopt_snapshot() on the new
 * actor thread (before it runs any Scheme code) to seed its own
 * thread-local stack from that snapshot -- otherwise a freshly-spawned
 * thread's CURRY_THREAD_LOCAL stack starts empty and its own relative loads
 * silently fall back to cwd instead of inheriting context. */
char **load_dir_snapshot(int *out_count);
void   load_dir_adopt_snapshot(char **snap, int count);

/* ---- Quasiquote expansion (used by compiler) ---- */
val_t expand_qq(val_t form, val_t env, int depth);

#ifdef __cplusplus
}
#endif

#endif /* CURRY_EVAL_H */
