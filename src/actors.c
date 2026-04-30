#include "actors.h"
#include "object.h"
#include "eval.h"
#include "gc.h"
#include "port.h"
#include "symbol.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <assert.h>

_Thread_local Actor *current_actor = NULL;

static _Atomic uint64_t next_actor_id = 1;

/* ---- Mailbox ---- */

static Mailbox *mailbox_new(void) {
    Mailbox *m = CURRY_NEW(Mailbox);
    m->hdr.type  = T_MAILBOX;
    m->hdr.flags = 0;
    pthread_mutex_init(&m->mutex, NULL);
    pthread_cond_init(&m->cond, NULL);
    m->q.cap  = 8;
    m->q.head = m->q.tail = 0;
    m->q.msgs = (val_t *)gc_alloc(8 * sizeof(val_t));
    return m;
}

static void mailbox_push(Mailbox *m, val_t msg) {
    pthread_mutex_lock(&m->mutex);
    size_t next = (m->q.tail + 1) % m->q.cap;
    if (next == m->q.head) {
        /* Grow */
        size_t new_cap = m->q.cap * 2;
        val_t *new_msgs = (val_t *)gc_alloc(new_cap * sizeof(val_t));
        size_t i = 0, j = m->q.head;
        while (j != m->q.tail) { new_msgs[i++] = m->q.msgs[j]; j = (j + 1) % m->q.cap; }
        m->q.msgs = new_msgs;
        m->q.head = 0; m->q.tail = i;
        m->q.cap  = new_cap;
        next = (m->q.tail + 1) % m->q.cap;
    }
    m->q.msgs[m->q.tail] = msg;
    m->q.tail = next;
    pthread_cond_signal(&m->cond);
    pthread_mutex_unlock(&m->mutex);
}

static val_t mailbox_pop_wait(Mailbox *m, long timeout_ms) {
    pthread_mutex_lock(&m->mutex);
    if (timeout_ms < 0) {
        while (m->q.head == m->q.tail)
            pthread_cond_wait(&m->cond, &m->mutex);
    } else {
        while (m->q.head == m->q.tail) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            int rc = pthread_cond_timedwait(&m->cond, &m->mutex, &ts);
            if (rc != 0 && m->q.head == m->q.tail) {
                pthread_mutex_unlock(&m->mutex);
                return V_FALSE;  /* timeout */
            }
        }
    }
    val_t msg = m->q.msgs[m->q.head];
    m->q.head = (m->q.head + 1) % m->q.cap;
    pthread_mutex_unlock(&m->mutex);
    return msg;
}

/* ---- Actor thread entry ---- */

typedef struct {
    Actor *actor;
    val_t  closure;
    val_t  args;
} ActorStart;

static void *actor_thread(void *arg) {
    ActorStart *start = (ActorStart *)arg;
    gc_register_thread();

    Actor *self = start->actor;
    current_actor = self;

    val_t reason = V_FALSE;
    ExnHandler h;
    val_t result = V_VOID;

    h.prev = current_handler;
    current_handler = &h;
    if (setjmp(h.jmp) == 0) {
        result = apply(start->closure, start->args);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        reason = h.exn;
        /* Print unhandled actor exception */
        fprintf(stderr, "Actor %lu died: ", (unsigned long)self->id);
        scm_write(reason, PORT_STDERR);
        fprintf(stderr, "\n");
    }
    (void)result;

    pthread_mutex_lock(&self->lock);
    self->alive = false;
    pthread_mutex_unlock(&self->lock);

    /* Notify linked actors with {'EXIT, self, reason} */
    /* (simplified: just mark dead; full linking requires a registry) */

    return NULL;
}

/* ---- Public API ---- */

void actors_init(void) {
    /* Nothing needed: GC is already thread-safe after gc_init() */
}

val_t actor_spawn(val_t closure, val_t args) {
    Actor *a = CURRY_NEW(Actor);
    a->hdr.type  = T_ACTOR;
    a->hdr.flags = 0;
    a->id        = atomic_fetch_add(&next_actor_id, 1);
    a->mailbox   = mailbox_new();
    a->closure   = closure;
    a->parent    = current_actor;
    a->name      = V_FALSE;
    a->alive     = true;
    pthread_mutex_init(&a->lock, NULL);

    ActorStart *start = (ActorStart *)gc_alloc(sizeof(ActorStart));
    start->actor   = a;
    start->closure = closure;
    start->args    = args;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&a->thread, &attr, actor_thread, start);
    pthread_attr_destroy(&attr);

    return vptr(a);
}

void actor_send(val_t actor_val, val_t msg) {
    if (!vis_actor(actor_val)) return;
    Actor *a = as_actor(actor_val);
    mailbox_push(a->mailbox, msg);
}

val_t actor_receive(val_t actor_val, long timeout_ms) {
    Actor *a = vis_actor(actor_val) ? as_actor(actor_val) : current_actor;
    if (!a) return V_FALSE;
    return mailbox_pop_wait(a->mailbox, timeout_ms);
}

val_t actor_self(void) {
    if (!current_actor) return V_FALSE;
    return vptr(current_actor);
}

void actor_exit(val_t reason) {
    if (current_actor) {
        pthread_mutex_lock(&current_actor->lock);
        current_actor->alive = false;
        pthread_mutex_unlock(&current_actor->lock);
    }
    scm_raise_val(reason);
}

void actor_link(val_t a, val_t b) {
    /* TODO: full link registry */
    (void)a; (void)b;
}

void actor_monitor(val_t monitor, val_t target) {
    /* TODO: monitor registry */
    (void)monitor; (void)target;
}

bool actor_alive(val_t actor_val) {
    if (!vis_actor(actor_val)) return false;
    Actor *a = as_actor(actor_val);
    pthread_mutex_lock(&a->lock);
    bool alive = a->alive;
    pthread_mutex_unlock(&a->lock);
    return alive;
}

uint64_t actor_id(val_t actor_val) {
    if (!vis_actor(actor_val)) return 0;
    return as_actor(actor_val)->id;
}
