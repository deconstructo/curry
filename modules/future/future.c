/* Futures for Curry Scheme, on the work-stealing pool (src/workpool.c).
 *
 *   (pool-submit thunk)       -> future   run thunk on a pool worker
 *   (future-get f)            -> value    wait for it; raises what the thunk raised
 *   (future-wait f seconds)   -> boolean  wait, but not for ever
 *   (future-done? f)          -> boolean  finished, by a value or by raising
 *   (future-then f proc)      -> future   (proc value-of-f), run when f is done
 *   (future? x)
 *
 * A future is a Scheme vector #(%future state thunk result exn dep conts), so the
 * collector sees everything it refers to and no handle has to be freed. Every
 * slot but the tag is read and written under one global mutex, with one global
 * condition variable broadcast whenever a future finishes: simple, and a
 * future's lifetime is not tied to any pthread object.
 *
 * Waiting cannot starve the pool. future-get on a future nobody has started
 * runs it in the calling thread (the pool finds it claimed and skips it), and
 * on one that waits for another future it first works on that one. A future
 * that another worker is already running is waited for; that can only hang if
 * futures wait for each other in a cycle.
 *
 * A thunk runs inside a handler of its own: whatever it raises becomes the
 * future's result and future-get raises it again, as often as it is asked.
 * Continuations do not cross threads (#443), so a continuation captured outside
 * the thunk cannot be invoked from it.
 */

#include <curry.h>
#include <curry_checked_args.h>
#include "eval.h"
#include "workpool.h"
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>

enum { F_TAG, F_STATE, F_THUNK, F_RESULT, F_EXN, F_DEP, F_CONTS, F_SIZE };
enum { ST_WAITING, ST_QUEUED, ST_RUNNING, ST_DONE, ST_FAILED };

static pthread_mutex_t fut_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  fut_cv = PTHREAD_COND_INITIALIZER;

static bool is_future(val_t v) {
    if (!curry_is_vector(v) || curry_vector_length(v) != F_SIZE) return false;
    val_t t = curry_vector_ref(v, F_TAG);
    return curry_is_symbol(t) && strcmp(curry_symbol(t), "%future") == 0;
}
/* A hand-made vector with the right tag must not be able to hang a waiter or
 * send it through a wrong slot: the state has to be one of the five, and a
 * future that waits has to wait for a future. */
static val_t checked_future(val_t v, const char *ctx) {
    if (!is_future(v)) curry_error("%s: expected a future", ctx);
    val_t s = curry_vector_ref(v, F_STATE);
    if (!curry_is_fixnum(s) || curry_fixnum(s) < ST_WAITING || curry_fixnum(s) > ST_FAILED ||
        (curry_fixnum(s) == ST_WAITING && !is_future(curry_vector_ref(v, F_DEP))))
        curry_error("%s: expected a future", ctx);
    return v;
}
static int fstate(val_t f) {
    val_t s = curry_vector_ref(f, F_STATE);
    return curry_is_fixnum(s) ? (int)curry_fixnum(s) : ST_FAILED;
}
static void set_state(val_t f, int st) { curry_vector_set(f, F_STATE, curry_make_fixnum(st)); }
static bool finished(int st) { return st == ST_DONE || st == ST_FAILED; }

static val_t make_future(int state, val_t thunk, val_t dep) {
    val_t f = curry_make_vector(F_SIZE, curry_make_bool(false));
    curry_vector_set(f, F_TAG, curry_make_symbol("%future"));
    curry_vector_set(f, F_STATE, curry_make_fixnum(state));
    curry_vector_set(f, F_THUNK, thunk);
    curry_vector_set(f, F_DEP, dep);
    curry_vector_set(f, F_CONTS, curry_nil());
    return f;
}

/* Call proc(args) in a handler of its own; true with *out set, or false with
 * *exn set to what was raised. */
static bool call_captured(val_t proc, int argc, val_t *argv, val_t *out, val_t *exn) {
    bool ok = false;
    ExnHandler h;
    val_t r = curry_void();
    SCM_PROTECT(h, { r = curry_apply(proc, argc, argv); ok = true; }, { ok = false; });
    if (ok) *out = r; else *exn = h.exn;
    return ok;
}

static void future_run(val_t f);
static void dep_done(val_t g);

static void task_entry(void *arg) { future_run((val_t)(uintptr_t)arg); }

/* f is QUEUED: hand it to the pool, or run it here if the pool is full. */
static void enqueue(val_t f) {
    if (!pool_submit_task(task_entry, (void *)(uintptr_t)f)) future_run(f);
}

/* Cleanup on the wind list while a thunk runs. Normally the outcome is already
 * recorded when this runs and it does nothing. If a continuation escaped from
 * the thunk (a thunk run by future-get in the thread that holds the
 * continuation), f is still RUNNING: fail it, so nobody waits for it for ever. */
static void future_abandon(void *arg) {
    val_t f = (val_t)(uintptr_t)arg;
    val_t msg = curry_make_string("future: the thunk was left by a continuation");
    pthread_mutex_lock(&fut_mx);
    if (fstate(f) != ST_RUNNING) { pthread_mutex_unlock(&fut_mx); return; }
    curry_vector_set(f, F_EXN, msg);
    set_state(f, ST_FAILED);
    curry_vector_set(f, F_THUNK, curry_make_bool(false));
    val_t conts = curry_vector_ref(f, F_CONTS);
    curry_vector_set(f, F_CONTS, curry_nil());
    pthread_cond_broadcast(&fut_cv);
    pthread_mutex_unlock(&fut_mx);
    for (; curry_is_pair(conts); conts = curry_cdr(conts)) dep_done(curry_car(conts));
}

/* Claim f if it is still QUEUED, run it, record the outcome, and let the
 * futures that were waiting for it go on. Does nothing if someone else has it. */
static void future_run(val_t f) {
    pthread_mutex_lock(&fut_mx);
    if (fstate(f) != ST_QUEUED) { pthread_mutex_unlock(&fut_mx); return; }
    set_state(f, ST_RUNNING);
    val_t thunk = curry_vector_ref(f, F_THUNK);
    val_t dep   = curry_vector_ref(f, F_DEP);
    val_t arg   = curry_void();
    if (is_future(dep)) arg = curry_vector_ref(dep, F_RESULT);   /* (future-then dep proc) */
    pthread_mutex_unlock(&fut_mx);

    val_t out = curry_void(), exn = curry_void();
    WindFrame *wf = wind_push_cleanup(future_abandon, (void *)(uintptr_t)f);
    bool ok = is_future(dep) ? call_captured(thunk, 1, &arg, &out, &exn)
                             : call_captured(thunk, 0, NULL, &out, &exn);

    pthread_mutex_lock(&fut_mx);
    if (ok) { curry_vector_set(f, F_RESULT, out); set_state(f, ST_DONE); }
    else    { curry_vector_set(f, F_EXN, exn);    set_state(f, ST_FAILED); }
    curry_vector_set(f, F_THUNK, curry_make_bool(false));       /* let it go */
    val_t conts = curry_vector_ref(f, F_CONTS);
    curry_vector_set(f, F_CONTS, curry_nil());
    pthread_cond_broadcast(&fut_cv);
    pthread_mutex_unlock(&fut_mx);
    wind_pop_cleanup(wf);        /* the outcome is recorded: future_abandon does nothing */

    for (; curry_is_pair(conts); conts = curry_cdr(conts)) dep_done(curry_car(conts));
}

/* g was waiting for its dependency, which has finished: queue g, or fail it
 * with the dependency's exception. Safe to call twice, and from any thread. */
static void dep_done(val_t g) {
    pthread_mutex_lock(&fut_mx);
    val_t dep = curry_vector_ref(g, F_DEP);
    if (fstate(g) != ST_WAITING || !is_future(dep) || !finished(fstate(dep))) {
        pthread_mutex_unlock(&fut_mx);
        return;
    }
    if (fstate(dep) == ST_DONE) {
        set_state(g, ST_QUEUED);
        pthread_mutex_unlock(&fut_mx);
        enqueue(g);
        return;
    }
    curry_vector_set(g, F_EXN, curry_vector_ref(dep, F_EXN));
    set_state(g, ST_FAILED);
    curry_vector_set(g, F_THUNK, curry_make_bool(false));
    val_t conts = curry_vector_ref(g, F_CONTS);
    curry_vector_set(g, F_CONTS, curry_nil());
    pthread_cond_broadcast(&fut_cv);
    pthread_mutex_unlock(&fut_mx);
    for (; curry_is_pair(conts); conts = curry_cdr(conts)) dep_done(curry_car(conts));
}

/* Wait until f has finished. With `deadline` the wait is bounded and nothing is
 * run here (a timed wait must not run an unbounded computation); without one,
 * a future nobody has started is run in this thread. true when finished. */
static bool await(val_t f, const struct timespec *deadline) {
    for (;;) {
        pthread_mutex_lock(&fut_mx);
        int st = fstate(f);
        if (finished(st)) { pthread_mutex_unlock(&fut_mx); return true; }
        if (!deadline && st == ST_QUEUED) {
            pthread_mutex_unlock(&fut_mx);
            future_run(f);                       /* claims it, or finds it taken */
            continue;
        }
        if (!deadline && st == ST_WAITING) {
            val_t dep = curry_vector_ref(f, F_DEP);
            pthread_mutex_unlock(&fut_mx);
            if (is_future(dep)) await(dep, NULL);   /* work on what f is waiting for */
            dep_done(f);
            continue;
        }
        /* RUNNING elsewhere, or a timed wait: block until something finishes. */
        curry_gc_thread_park();
        int rc = 0;
        if (deadline) rc = pthread_cond_timedwait(&fut_cv, &fut_mx, deadline);
        else          pthread_cond_wait(&fut_cv, &fut_mx);
        curry_gc_thread_unpark();
        bool done = finished(fstate(f));
        pthread_mutex_unlock(&fut_mx);
        if (done) return true;
        if (deadline && rc == ETIMEDOUT) return false;
    }
}

/* ---- procedures ---- */

static val_t fn_pool_submit(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_procedure(av[0])) curry_error("pool-submit: expected a procedure");
    val_t f = make_future(ST_QUEUED, av[0], curry_make_bool(false));
    enqueue(f);
    return f;
}

static val_t fn_future_get(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = checked_future(av[0], "future-get");
    await(f, NULL);
    pthread_mutex_lock(&fut_mx);
    int st = fstate(f);
    val_t v = curry_vector_ref(f, st == ST_DONE ? F_RESULT : F_EXN);
    pthread_mutex_unlock(&fut_mx);
    if (st == ST_DONE) return v;
    scm_raise_val(v);
}

static val_t fn_future_wait(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = checked_future(av[0], "future-wait");
    double secs = curry_is_fixnum(av[1]) ? (double)checked_fixnum(av[1], 2, "future-wait")
                                         : checked_float(av[1], 2, "future-wait");
    if (secs < 0) secs = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)secs;
    ts.tv_nsec += (long)((secs - (double)(long)secs) * 1e9);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return curry_make_bool(await(f, &ts));
}

static val_t fn_future_done_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = checked_future(av[0], "future-done?");
    pthread_mutex_lock(&fut_mx);
    bool done = finished(fstate(f));
    pthread_mutex_unlock(&fut_mx);
    return curry_make_bool(done);
}

/* (future-then f proc): a future for (proc value-of-f), run once f is done. If
 * f raised, the new future fails with the same exception and proc is not called. */
static val_t fn_future_then(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t f = checked_future(av[0], "future-then");
    if (!curry_is_procedure(av[1])) curry_error("future-then: expected a procedure");
    val_t g = make_future(ST_WAITING, av[1], f);
    pthread_mutex_lock(&fut_mx);
    int st = fstate(f);
    if (!finished(st)) {
        /* One small allocation under the lock; no other thread waits for the
         * collector while holding it. */
        curry_vector_set(f, F_CONTS, curry_make_pair(g, curry_vector_ref(f, F_CONTS)));
        pthread_mutex_unlock(&fut_mx);
        return g;
    }
    pthread_mutex_unlock(&fut_mx);
    dep_done(g);                                        /* f is done: queue g or fail it */
    return g;
}

static val_t fn_future_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return curry_make_bool(is_future(av[0]));
}

void curry_module_init(CurryVM *vm) {
#define DEF(n, f, mn, mx) curry_define_fn(vm, n, f, mn, mx, NULL)
    DEF("pool-submit",  fn_pool_submit,   1, 1);
    DEF("future-get",   fn_future_get,    1, 1);
    DEF("future-wait",  fn_future_wait,   2, 2);
    DEF("future-done?", fn_future_done_p, 1, 1);
    DEF("future-then",  fn_future_then,   2, 2);
    DEF("future?",      fn_future_p,      1, 1);
#undef DEF
}
