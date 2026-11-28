/* Synchronisation primitives module for Curry Scheme.
 * Provides mutex, condition variable, semaphore, read-write lock, once and
 * barrier bindings over pthreads.
 * Uses public curry.h API only.
 *
 * Handles are tagged pairs: ("mutex" . bv), ("condvar" . bv), ("semaphore" . bv),
 * ("rwlock" . bv), ("once" . bv), ("barrier" . bv)
 * where bv is a bytevector holding the malloc'd C struct pointer.
 * Call (mutex-destroy! mx), (condvar-destroy! cv), (semaphore-destroy! sem),
 * (rwlock-destroy! rw), (once-destroy! o), (barrier-destroy! b)
 * before releasing handles to avoid resource leaks.
 *
 * Intentionally does NOT expose pthread_create — use Curry's actor model
 * (spawn / send! / receive) as the preferred concurrency primitive.
 */

#include <curry.h>
#include <curry_checked_args.h>
#include "eval.h"
#include <pthread.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ---- generic pointer packing ---- */
static curry_val pack_ptr(void *ptr) {
    curry_val bv = curry_make_bytevector(sizeof(void *), 0);
    for (size_t i = 0; i < sizeof(void *); i++)
        curry_bytevector_set(bv, (uint32_t)i, ((uint8_t *)&ptr)[i]);
    return bv;
}
static void *unpack_ptr(curry_val bv) {
    void *ptr = NULL;
    for (size_t i = 0; i < sizeof(void *); i++)
        ((uint8_t *)&ptr)[i] = curry_bytevector_ref(bv, (uint32_t)i);
    return ptr;
}

/* ---- tag check helper ---- */
static int has_tag(curry_val v, const char *tag) {
    return curry_is_pair(v) &&
           curry_is_symbol(curry_car(v)) &&
           strcmp(curry_symbol(curry_car(v)), tag) == 0;
}

/* Issue #165: has_tag checked the car, but every get_mutex/get_cond/
 * get_sem below passed curry_cdr(v) straight to unpack_ptr() with no
 * curry_is_bytevector/length check -- a forged handle like
 * (cons 'mutex 42) has the right tag but a non-bytevector cdr, and
 * curry_bytevector_ref's own unchecked as_bytes() cast then dereferences
 * the fixnum's raw bit pattern as a pointer. Confirmed reproducible
 * SIGSEGV via (mutex-lock! (cons 'mutex 42)). */
static void *checked_unpack_ptr(curry_val v, const char *tag, const char *ctx) {
    if (!has_tag(v, tag)) curry_error("%s: expected %s", ctx, tag);
    curry_val bv = curry_cdr(v);
    if (!curry_is_bytevector(bv) || curry_bytevector_length(bv) != sizeof(void *))
        curry_error("%s: expected %s", ctx, tag);
    return unpack_ptr(bv);
}

/* ---- Mutex ---- */
static curry_val wrap_mutex(pthread_mutex_t *m) {
    return curry_make_pair(curry_make_symbol("mutex"), pack_ptr(m));
}
static pthread_mutex_t *get_mutex(curry_val v, const char *ctx) {
    return (pthread_mutex_t *)checked_unpack_ptr(v, "mutex", ctx);
}

static curry_val fn_mutex_make(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (!m) curry_error("make-mutex: out of memory");
    if (pthread_mutex_init(m, NULL) != 0) {
        free(m); curry_error("make-mutex: pthread_mutex_init failed");
    }
    return wrap_mutex(m);
}
static curry_val fn_mutex_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_mutex_t *m = get_mutex(av[0], "mutex-destroy!");
    pthread_mutex_destroy(m); free(m);
    return curry_void();
}
static curry_val fn_mutex_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_mutex_lock(get_mutex(av[0], "mutex-lock!"));
    return curry_void();
}
static curry_val fn_mutex_unlock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_mutex_unlock(get_mutex(av[0], "mutex-unlock!"));
    return curry_void();
}
static curry_val fn_mutex_trylock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int rc = pthread_mutex_trylock(get_mutex(av[0], "mutex-trylock!"));
    if (rc == 0)     return curry_make_bool(true);
    if (rc == EBUSY) return curry_make_bool(false);
    curry_error("mutex-trylock!: error %d", rc);
}
static curry_val fn_mutex_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "mutex"));
}

static void unlock_mutex_cleanup(void *m) { pthread_mutex_unlock((pthread_mutex_t *)m); }

/* Call thunk with `cleanup(arg)` on the wind list, so it runs on a normal
 * return, on an exception (re-raised afterwards) and on a continuation escape
 * (#428). `on_ok`, if not NULL, runs after a normal return and before the
 * cleanup. The lock it undoes is taken by the caller, before this call. */
static curry_val call_with_cleanup(curry_val thunk, void (*cleanup)(void *), void *arg,
                                   void (*on_ok)(void *)) {
    curry_val result = curry_void();
    WindFrame *wf = wind_push_cleanup(cleanup, arg);
    ExnHandler h;
    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    h.handler_proc = V_TRUE; current_handler = &h;   /* cleanup and re-raise */
    bool raised = false; curry_val exn_val = curry_void();
    if (setjmp(h.jmp) == 0) {
        result = curry_apply(thunk, 0, NULL);
        current_handler = h.prev;
        if (on_ok) on_ok(arg);
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        raised = true; exn_val = h.exn;
    }
    wind_pop_cleanup(wf);
    if (raised) scm_raise_val(exn_val);
    return result;
}

/* (with-mutex mutex thunk) — lock, call thunk, always unlock: on a normal
 * return, an exception, or a continuation escape (#428) */
static curry_val fn_with_mutex(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_mutex_t *m = get_mutex(av[0], "with-mutex");
    if (!curry_is_procedure(av[1])) curry_error("with-mutex: expected procedure");
    pthread_mutex_lock(m);
    return call_with_cleanup(av[1], unlock_mutex_cleanup, m, NULL);
}

/* ---- Condition variable ---- */
static curry_val wrap_cond(pthread_cond_t *cv) {
    return curry_make_pair(curry_make_symbol("condvar"), pack_ptr(cv));
}
static pthread_cond_t *get_cond(curry_val v, const char *ctx) {
    return (pthread_cond_t *)checked_unpack_ptr(v, "condvar", ctx);
}

static curry_val fn_cond_make(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    pthread_cond_t *cv = malloc(sizeof(pthread_cond_t));
    if (!cv) curry_error("make-condvar: out of memory");
    if (pthread_cond_init(cv, NULL) != 0) {
        free(cv); curry_error("make-condvar: pthread_cond_init failed");
    }
    return wrap_cond(cv);
}
static curry_val fn_cond_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_cond_t *cv = get_cond(av[0], "condvar-destroy!");
    pthread_cond_destroy(cv); free(cv);
    return curry_void();
}
static curry_val fn_cond_wait(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_cond_t  *cv = get_cond(av[0], "cond-wait!");
    pthread_mutex_t *m  = get_mutex(av[1], "cond-wait!");
    /* Issue #200 Phase A: an untimed wait can block indefinitely -- park
     * so a future GC safepoint doesn't wait on this thread meanwhile. */
    curry_gc_thread_park();
    pthread_cond_wait(cv, m);
    curry_gc_thread_unpark();
    return curry_void();
}
/* (cond-wait-timeout! cv mutex seconds) → #t signalled, #f timed out */
static curry_val fn_cond_wait_timeout(int ac, curry_val *av, void *ud) {
    (void)ud;
    pthread_cond_t  *cv = get_cond(av[0], "cond-wait-timeout!");
    pthread_mutex_t *m  = get_mutex(av[1], "cond-wait-timeout!");
    double secs = curry_is_fixnum(av[2]) ? (double)checked_fixnum(av[2], 3, "cond-wait-timeout!") : checked_float(av[2], 3, "cond-wait-timeout!");
    (void)ac;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)secs;
    ts.tv_nsec += (long)((secs - (long)secs) * 1e9);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    /* Issue #200 Phase A: bounded by the timeout, but that can still be
     * arbitrarily long -- see fn_cond_wait's identical comment. */
    curry_gc_thread_park();
    int rc = pthread_cond_timedwait(cv, m, &ts);
    curry_gc_thread_unpark();
    return curry_make_bool(rc != ETIMEDOUT);
}
static curry_val fn_cond_signal(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_cond_signal(get_cond(av[0], "cond-signal!"));
    return curry_void();
}
static curry_val fn_cond_broadcast(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_cond_broadcast(get_cond(av[0], "cond-broadcast!"));
    return curry_void();
}
static curry_val fn_cond_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "condvar"));
}

/* ---- Counting semaphore (mutex+condvar — portable; avoids sem_init which
 *      is unimplemented on macOS for unnamed semaphores) ---- */
typedef struct { pthread_mutex_t mtx; pthread_cond_t cnd; unsigned count; } ScmSem;

static curry_val wrap_sem(ScmSem *s) {
    return curry_make_pair(curry_make_symbol("semaphore"), pack_ptr(s));
}
static ScmSem *get_sem(curry_val v, const char *ctx) {
    return (ScmSem *)checked_unpack_ptr(v, "semaphore", ctx);
}

static curry_val fn_sem_make(int ac, curry_val *av, void *ud) {
    (void)ud;
    unsigned initial = (ac >= 1 && curry_is_fixnum(av[0])) ? (unsigned)checked_fixnum(av[0], 1, "make-semaphore") : 0;
    ScmSem *s = malloc(sizeof(ScmSem));
    if (!s) curry_error("make-semaphore: out of memory");
    if (pthread_mutex_init(&s->mtx, NULL) != 0) { free(s); curry_error("make-semaphore: mutex init failed"); }
    if (pthread_cond_init(&s->cnd, NULL)  != 0) { pthread_mutex_destroy(&s->mtx); free(s); curry_error("make-semaphore: cond init failed"); }
    s->count = initial;
    return wrap_sem(s);
}
static curry_val fn_sem_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmSem *s = get_sem(av[0], "semaphore-destroy!");
    pthread_cond_destroy(&s->cnd);
    pthread_mutex_destroy(&s->mtx);
    free(s);
    return curry_void();
}
static curry_val fn_sem_wait(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmSem *s = get_sem(av[0], "sem-wait!");
    pthread_mutex_lock(&s->mtx);
    if (s->count == 0) {
        /* Issue #200 Phase A: can block indefinitely -- see fn_cond_wait's
         * identical comment. */
        curry_gc_thread_park();
        while (s->count == 0) pthread_cond_wait(&s->cnd, &s->mtx);
        curry_gc_thread_unpark();
    }
    s->count--;
    pthread_mutex_unlock(&s->mtx);
    return curry_void();
}
static curry_val fn_sem_post(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmSem *s = get_sem(av[0], "sem-post!");
    pthread_mutex_lock(&s->mtx);
    s->count++;
    pthread_cond_signal(&s->cnd);
    pthread_mutex_unlock(&s->mtx);
    return curry_void();
}
static curry_val fn_sem_trywait(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmSem *s = get_sem(av[0], "sem-trywait!");
    pthread_mutex_lock(&s->mtx);
    int ok = (s->count > 0);
    if (ok) s->count--;
    pthread_mutex_unlock(&s->mtx);
    return curry_make_bool(ok);
}
static curry_val fn_sem_value(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmSem *s = get_sem(av[0], "sem-value");
    pthread_mutex_lock(&s->mtx);
    unsigned val = s->count;
    pthread_mutex_unlock(&s->mtx);
    return curry_make_fixnum((intptr_t)val);
}
static curry_val fn_sem_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "semaphore"));
}

/* ---- Read-write lock ----
 * Any number of readers or one writer (pthread_rwlock_t). Not recursive: a
 * thread that takes a lock it already holds for writing gets an error where the
 * platform reports it (macOS, glibc) and otherwise deadlocks. The try variants
 * answer #f. Unlocking a lock the thread does not hold is an error where the
 * platform reports it. */
static curry_val wrap_rwlock(pthread_rwlock_t *l) {
    return curry_make_pair(curry_make_symbol("rwlock"), pack_ptr(l));
}
static pthread_rwlock_t *get_rwlock(curry_val v, const char *ctx) {
    return (pthread_rwlock_t *)checked_unpack_ptr(v, "rwlock", ctx);
}
static curry_val fn_rwlock_make(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    pthread_rwlock_t *l = malloc(sizeof(pthread_rwlock_t));
    if (!l) curry_error("make-rwlock: out of memory");
    if (pthread_rwlock_init(l, NULL) != 0) {
        free(l); curry_error("make-rwlock: pthread_rwlock_init failed");
    }
    return wrap_rwlock(l);
}
static curry_val fn_rwlock_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_rwlock_t *l = get_rwlock(av[0], "rwlock-destroy!");
    pthread_rwlock_destroy(l); free(l);
    return curry_void();
}
static void rwlock_fail(int rc, const char *ctx) {
    if (rc == EDEADLK) curry_error("%s: this thread already holds the lock (it is not recursive)", ctx);
    curry_error("%s: error %d", ctx, rc);
}
/* Try first; park for the collector only when it has to block (see fn_cond_wait). */
static void rwlock_acquire(pthread_rwlock_t *l, bool write, const char *ctx) {
    int rc = write ? pthread_rwlock_trywrlock(l) : pthread_rwlock_tryrdlock(l);
    if (rc == 0) return;
    if (rc != EBUSY) rwlock_fail(rc, ctx);   /* EDEADLK: this thread holds it for writing */
    curry_gc_thread_park();
    rc = write ? pthread_rwlock_wrlock(l) : pthread_rwlock_rdlock(l);
    curry_gc_thread_unpark();
    if (rc != 0) rwlock_fail(rc, ctx);
}
static curry_val fn_rwlock_read_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    rwlock_acquire(get_rwlock(av[0], "rwlock-read-lock!"), false, "rwlock-read-lock!");
    return curry_void();
}
static curry_val fn_rwlock_write_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    rwlock_acquire(get_rwlock(av[0], "rwlock-write-lock!"), true, "rwlock-write-lock!");
    return curry_void();
}
static curry_val fn_rwlock_try_read_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int rc = pthread_rwlock_tryrdlock(get_rwlock(av[0], "rwlock-try-read-lock!"));
    if (rc == 0)     return curry_make_bool(true);
    if (rc == EBUSY || rc == EDEADLK) return curry_make_bool(false);   /* EDEADLK: macOS, this thread holds it for writing */
    rwlock_fail(rc, "rwlock-try-read-lock!");
}
static curry_val fn_rwlock_try_write_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int rc = pthread_rwlock_trywrlock(get_rwlock(av[0], "rwlock-try-write-lock!"));
    if (rc == 0)     return curry_make_bool(true);
    if (rc == EBUSY || rc == EDEADLK) return curry_make_bool(false);
    rwlock_fail(rc, "rwlock-try-write-lock!");
}
static curry_val fn_rwlock_unlock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    int rc = pthread_rwlock_unlock(get_rwlock(av[0], "rwlock-unlock!"));
    if (rc != 0) curry_error("rwlock-unlock!: the lock is not held by this thread (error %d)", rc);
    return curry_void();
}
static curry_val fn_rwlock_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "rwlock"));
}
static void unlock_rwlock_cleanup(void *l) { pthread_rwlock_unlock((pthread_rwlock_t *)l); }
/* (with-read-lock rwlock thunk) and (with-write-lock rwlock thunk): take the
 * lock, call thunk, release it on a return, an exception or an escape. */
static curry_val with_rwlock(curry_val *av, bool write, const char *ctx) {
    pthread_rwlock_t *l = get_rwlock(av[0], ctx);
    if (!curry_is_procedure(av[1])) curry_error("%s: expected procedure", ctx);
    rwlock_acquire(l, write, ctx);
    return call_with_cleanup(av[1], unlock_rwlock_cleanup, l, NULL);
}
static curry_val fn_with_read_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return with_rwlock(av, false, "with-read-lock");
}
static curry_val fn_with_write_lock(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return with_rwlock(av, true, "with-write-lock");
}

/* ---- Once ----
 * (once! o thunk) runs thunk the first time any thread calls it; other callers
 * wait until it has finished. If the thunk raises or escapes, the once is not
 * used up: the next caller runs its own thunk. */
enum { ONCE_NEW, ONCE_RUNNING, ONCE_DONE };
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cnd;
    int             state;
    pthread_t       owner;     /* the running thread, for the recursion check */
} ScmOnce;

static curry_val wrap_once(ScmOnce *o) {
    return curry_make_pair(curry_make_symbol("once"), pack_ptr(o));
}
static ScmOnce *get_once(curry_val v, const char *ctx) {
    return (ScmOnce *)checked_unpack_ptr(v, "once", ctx);
}
static curry_val fn_once_make(int ac, curry_val *av, void *ud) {
    (void)ac; (void)av; (void)ud;
    ScmOnce *o = malloc(sizeof(ScmOnce));
    if (!o) curry_error("make-once: out of memory");
    if (pthread_mutex_init(&o->mtx, NULL) != 0) { free(o); curry_error("make-once: mutex init failed"); }
    if (pthread_cond_init(&o->cnd, NULL)  != 0) { pthread_mutex_destroy(&o->mtx); free(o); curry_error("make-once: cond init failed"); }
    o->state = ONCE_NEW;
    return wrap_once(o);
}
static curry_val fn_once_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmOnce *o = get_once(av[0], "once-destroy!");
    pthread_cond_destroy(&o->cnd);
    pthread_mutex_destroy(&o->mtx);
    free(o);
    return curry_void();
}
static void once_ok(void *arg) {
    ScmOnce *o = (ScmOnce *)arg;
    pthread_mutex_lock(&o->mtx);
    o->state = ONCE_DONE;
    pthread_mutex_unlock(&o->mtx);
}
/* Runs after the thunk however it was left: a thunk that did not finish
 * (state still RUNNING) gives the once back, and every waiter wakes. */
static void once_cleanup(void *arg) {
    ScmOnce *o = (ScmOnce *)arg;
    pthread_mutex_lock(&o->mtx);
    if (o->state == ONCE_RUNNING) o->state = ONCE_NEW;
    pthread_cond_broadcast(&o->cnd);
    pthread_mutex_unlock(&o->mtx);
}
/* → #t if this call ran the thunk, #f if the once was already used up */
static curry_val fn_once_run(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmOnce *o = get_once(av[0], "once!");
    if (!curry_is_procedure(av[1])) curry_error("once!: expected procedure");
    pthread_mutex_lock(&o->mtx);
    for (;;) {
        if (o->state == ONCE_DONE) { pthread_mutex_unlock(&o->mtx); return curry_make_bool(false); }
        if (o->state == ONCE_NEW) break;
        if (pthread_equal(o->owner, pthread_self())) {
            pthread_mutex_unlock(&o->mtx);
            curry_error("once!: called again from inside its own thunk");
        }
        curry_gc_thread_park();         /* the thunk can take any time */
        pthread_cond_wait(&o->cnd, &o->mtx);
        curry_gc_thread_unpark();
    }
    o->state = ONCE_RUNNING;
    o->owner = pthread_self();
    pthread_mutex_unlock(&o->mtx);
    call_with_cleanup(av[1], once_cleanup, o, once_ok);
    return curry_make_bool(true);
}
static curry_val fn_once_done_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmOnce *o = get_once(av[0], "once-done?");
    pthread_mutex_lock(&o->mtx);
    int done = (o->state == ONCE_DONE);
    pthread_mutex_unlock(&o->mtx);
    return curry_make_bool(done);
}
static curry_val fn_once_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "once"));
}

/* ---- Barrier ----
 * (make-barrier n): (barrier-wait! b) blocks until n threads have called it,
 * then releases all of them and the barrier is ready again. One of the n gets
 * #t, the others #f (the "serial" thread, as pthread_barrier_wait). Built on a
 * mutex, a condition variable and a generation count: macOS has no
 * pthread_barrier_t. */
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cnd;
    unsigned        parties, waiting;
    unsigned long   generation;
} ScmBarrier;

static curry_val wrap_barrier(ScmBarrier *b) {
    return curry_make_pair(curry_make_symbol("barrier"), pack_ptr(b));
}
static ScmBarrier *get_barrier(curry_val v, const char *ctx) {
    return (ScmBarrier *)checked_unpack_ptr(v, "barrier", ctx);
}
static curry_val fn_barrier_make(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    intptr_t n = checked_fixnum(av[0], 1, "make-barrier");
    if (n < 1 || n > (1 << 20)) curry_error("make-barrier: the number of threads must be from 1 to %d", 1 << 20);
    ScmBarrier *b = malloc(sizeof(ScmBarrier));
    if (!b) curry_error("make-barrier: out of memory");
    if (pthread_mutex_init(&b->mtx, NULL) != 0) { free(b); curry_error("make-barrier: mutex init failed"); }
    if (pthread_cond_init(&b->cnd, NULL)  != 0) { pthread_mutex_destroy(&b->mtx); free(b); curry_error("make-barrier: cond init failed"); }
    b->parties = (unsigned)n; b->waiting = 0; b->generation = 0;
    return wrap_barrier(b);
}
static curry_val fn_barrier_destroy(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmBarrier *b = get_barrier(av[0], "barrier-destroy!");
    pthread_cond_destroy(&b->cnd);
    pthread_mutex_destroy(&b->mtx);
    free(b);
    return curry_void();
}
static curry_val fn_barrier_wait(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    ScmBarrier *b = get_barrier(av[0], "barrier-wait!");
    pthread_mutex_lock(&b->mtx);
    unsigned long gen = b->generation;
    if (++b->waiting == b->parties) {
        b->waiting = 0;
        b->generation++;
        pthread_cond_broadcast(&b->cnd);
        pthread_mutex_unlock(&b->mtx);
        return curry_make_bool(true);
    }
    curry_gc_thread_park();             /* waits for the other threads */
    while (gen == b->generation) pthread_cond_wait(&b->cnd, &b->mtx);
    curry_gc_thread_unpark();
    pthread_mutex_unlock(&b->mtx);
    return curry_make_bool(false);
}
static curry_val fn_barrier_parties(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_fixnum((intptr_t)get_barrier(av[0], "barrier-parties")->parties);
}
static curry_val fn_barrier_p(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(has_tag(av[0], "barrier"));
}

/* ---- Module init ---- */
void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    /* Mutex */
    DEF("make-mutex",         fn_mutex_make,        0, 0);
    DEF("mutex-destroy!",     fn_mutex_destroy,     1, 1);
    DEF("mutex-lock!",        fn_mutex_lock,        1, 1);
    DEF("mutex-unlock!",      fn_mutex_unlock,      1, 1);
    DEF("mutex-trylock!",     fn_mutex_trylock,     1, 1);
    DEF("mutex?",             fn_mutex_p,           1, 1);
    DEF("with-mutex",         fn_with_mutex,        2, 2);
    /* Condition variable */
    DEF("make-condvar",       fn_cond_make,         0, 0);
    DEF("condvar-destroy!",   fn_cond_destroy,      1, 1);
    DEF("cond-wait!",         fn_cond_wait,         2, 2);
    DEF("cond-wait-timeout!", fn_cond_wait_timeout, 3, 3);
    DEF("cond-signal!",       fn_cond_signal,       1, 1);
    DEF("cond-broadcast!",    fn_cond_broadcast,    1, 1);
    DEF("condvar?",           fn_cond_p,            1, 1);
    /* Semaphore */
    DEF("make-semaphore",     fn_sem_make,          0, 1);
    DEF("semaphore-destroy!", fn_sem_destroy,       1, 1);
    DEF("sem-wait!",          fn_sem_wait,          1, 1);
    DEF("sem-post!",          fn_sem_post,          1, 1);
    DEF("sem-trywait!",       fn_sem_trywait,       1, 1);
    DEF("sem-value",          fn_sem_value,         1, 1);
    DEF("semaphore?",         fn_sem_p,             1, 1);
    /* Read-write lock */
    DEF("make-rwlock",            fn_rwlock_make,           0, 0);
    DEF("rwlock-destroy!",        fn_rwlock_destroy,        1, 1);
    DEF("rwlock-read-lock!",      fn_rwlock_read_lock,      1, 1);
    DEF("rwlock-write-lock!",     fn_rwlock_write_lock,     1, 1);
    DEF("rwlock-try-read-lock!",  fn_rwlock_try_read_lock,  1, 1);
    DEF("rwlock-try-write-lock!", fn_rwlock_try_write_lock, 1, 1);
    DEF("rwlock-unlock!",         fn_rwlock_unlock,         1, 1);
    DEF("rwlock?",                fn_rwlock_p,              1, 1);
    DEF("with-read-lock",         fn_with_read_lock,        2, 2);
    DEF("with-write-lock",        fn_with_write_lock,       2, 2);
    /* Once */
    DEF("make-once",              fn_once_make,             0, 0);
    DEF("once-destroy!",          fn_once_destroy,          1, 1);
    DEF("once!",                  fn_once_run,              2, 2);
    DEF("once-done?",             fn_once_done_p,           1, 1);
    DEF("once?",                  fn_once_p,                1, 1);
    /* Barrier */
    DEF("make-barrier",           fn_barrier_make,          1, 1);
    DEF("barrier-destroy!",       fn_barrier_destroy,       1, 1);
    DEF("barrier-wait!",          fn_barrier_wait,          1, 1);
    DEF("barrier-parties",        fn_barrier_parties,       1, 1);
    DEF("barrier?",               fn_barrier_p,             1, 1);
#undef DEF
}
