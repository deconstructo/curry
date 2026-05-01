/* Synchronisation primitives module for Curry Scheme.
 * Provides mutex, condition variable, and semaphore bindings over pthreads.
 * Uses public curry.h API only.
 *
 * Handles are tagged pairs: ("mutex" . bv), ("condvar" . bv), ("semaphore" . bv)
 * where bv is a bytevector holding the malloc'd C struct pointer.
 * Call (mutex-destroy! mx), (condvar-destroy! cv), (semaphore-destroy! sem)
 * before releasing handles to avoid resource leaks.
 *
 * Intentionally does NOT expose pthread_create — use Curry's actor model
 * (spawn / send! / receive) as the preferred concurrency primitive.
 */

#include <curry.h>
#include <pthread.h>
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

/* ---- Mutex ---- */
static curry_val wrap_mutex(pthread_mutex_t *m) {
    return curry_make_pair(curry_make_symbol("mutex"), pack_ptr(m));
}
static pthread_mutex_t *get_mutex(curry_val v, const char *ctx) {
    if (!has_tag(v, "mutex")) curry_error("%s: expected mutex", ctx);
    return (pthread_mutex_t *)unpack_ptr(curry_cdr(v));
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

/* (with-mutex mutex thunk) — lock, call thunk, always unlock */
static curry_val fn_with_mutex(int ac, curry_val *av, void *ud) {
    (void)ac; (void)ud;
    pthread_mutex_t *m = get_mutex(av[0], "with-mutex");
    if (!curry_is_procedure(av[1])) curry_error("with-mutex: expected procedure");
    pthread_mutex_lock(m);
    curry_val result = curry_apply(av[1], 0, NULL);
    pthread_mutex_unlock(m);
    return result;
}

/* ---- Condition variable ---- */
static curry_val wrap_cond(pthread_cond_t *cv) {
    return curry_make_pair(curry_make_symbol("condvar"), pack_ptr(cv));
}
static pthread_cond_t *get_cond(curry_val v, const char *ctx) {
    if (!has_tag(v, "condvar")) curry_error("%s: expected condvar", ctx);
    return (pthread_cond_t *)unpack_ptr(curry_cdr(v));
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
    pthread_cond_wait(cv, m);
    return curry_void();
}
/* (cond-wait-timeout! cv mutex seconds) → #t signalled, #f timed out */
static curry_val fn_cond_wait_timeout(int ac, curry_val *av, void *ud) {
    (void)ud;
    pthread_cond_t  *cv = get_cond(av[0], "cond-wait-timeout!");
    pthread_mutex_t *m  = get_mutex(av[1], "cond-wait-timeout!");
    double secs = curry_is_fixnum(av[2]) ? (double)curry_fixnum(av[2]) : curry_float(av[2]);
    (void)ac;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)secs;
    ts.tv_nsec += (long)((secs - (long)secs) * 1e9);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    int rc = pthread_cond_timedwait(cv, m, &ts);
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
    if (!has_tag(v, "semaphore")) curry_error("%s: expected semaphore", ctx);
    return (ScmSem *)unpack_ptr(curry_cdr(v));
}

static curry_val fn_sem_make(int ac, curry_val *av, void *ud) {
    (void)ud;
    unsigned initial = (ac >= 1 && curry_is_fixnum(av[0])) ? (unsigned)curry_fixnum(av[0]) : 0;
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
    while (s->count == 0) pthread_cond_wait(&s->cnd, &s->mtx);
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
#undef DEF
}
