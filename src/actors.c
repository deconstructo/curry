#include "actors.h"
#include "object.h"
#include "eval.h"
#include "gc.h"
#include "vm.h"
#include "port.h"
#include "symbol.h"
#include "builtins.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <assert.h>

_Thread_local Actor *current_actor = NULL;

static _Atomic uint64_t next_actor_id = 1;

/* ---- Global actor registry (backs list-actors introspection) ----------
 * A fixed-capacity slot table: plain static val_t array -- Boehm scans
 * static/global data already (the same reason GLOBAL_ENV, a plain val_t
 * global in env.c, needs no explicit GC_ADD_ROOT), so this needs none
 * either -- guarded by one mutex. An actor is registered right before its thread
 * actually starts running (actor_spawn) and unregistered right as it
 * exits (actor_thread, in the same critical section that already flips
 * self->alive to false) -- so list-actors only ever reports actors that
 * are live or were live a moment ago, never an unbounded history of every
 * actor a long-running process has ever spawned. A hard cap (rather than
 * a growable array) matches this codebase's existing precedent for
 * fixed-size debug/introspection tables (DBG_BREAKS_MAX, the profiler's
 * 4096-slot hash map) -- if the cap is ever hit, spawning still succeeds,
 * the actor just doesn't appear in list-actors (documented below), rather
 * than either failing the spawn or growing unbounded for what is
 * diagnostic-only bookkeeping. */
#define ACTOR_REGISTRY_MAX 4096
static val_t             actor_registry[ACTOR_REGISTRY_MAX];
static int                actor_registry_count = 0; /* high-water mark of used slots */
static pthread_mutex_t    actor_registry_lock = PTHREAD_MUTEX_INITIALIZER;

/* Registers `a`; silently a no-op if the table is full (see comment
 * above) -- introspection visibility, not correctness, so there is
 * nothing to raise here. */
static void actor_registry_add(Actor *a) {
    pthread_mutex_lock(&actor_registry_lock);
    for (int i = 0; i < ACTOR_REGISTRY_MAX; i++) {
        if (actor_registry[i] == 0) {
            actor_registry[i] = vptr(a);
            if (i >= actor_registry_count) actor_registry_count = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&actor_registry_lock);
}

static void actor_registry_remove(Actor *a) {
    val_t target = vptr(a);
    pthread_mutex_lock(&actor_registry_lock);
    for (int i = 0; i < actor_registry_count; i++) {
        if (actor_registry[i] == target) { actor_registry[i] = 0; break; }
    }
    pthread_mutex_unlock(&actor_registry_lock);
}

/* (list-actors) -> list of actor objects currently registered (live, or
 * exiting right now -- see the registry comment above for the exact
 * window). */
val_t actors_list(void) {
    val_t result = V_NIL;
    pthread_mutex_lock(&actor_registry_lock);
    for (int i = 0; i < actor_registry_count; i++) {
        if (actor_registry[i] != 0) result = scm_cons(actor_registry[i], result);
    }
    pthread_mutex_unlock(&actor_registry_lock);
    return result;
}

static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ---- Mailbox ---- */

static Mailbox *mailbox_new(void) {
    Mailbox *m = CURRY_NEW_PINNED(Mailbox);
    m->hdr.type  = T_MAILBOX;
    m->hdr.flags = 0;
    pthread_mutex_init(&m->mutex, NULL);
    pthread_cond_init(&m->cond, NULL);
    m->q.cap  = 8;
    m->q.head = m->q.tail = 0;
    m->q.msgs = (val_t *)gc_alloc_raw_pinned(8 * sizeof(val_t));
    return m;
}

static void mailbox_push(Mailbox *m, val_t msg, Actor *target) {
    pthread_mutex_lock(&m->mutex);
    size_t next = (m->q.tail + 1) % m->q.cap;
    if (next == m->q.head) {
        /* Grow */
        size_t new_cap = m->q.cap * 2;
        val_t *new_msgs = (val_t *)gc_alloc_raw_pinned(new_cap * sizeof(val_t));
        size_t i = 0, j = m->q.head;
        while (j != m->q.tail) { new_msgs[i++] = m->q.msgs[j]; j = (j + 1) % m->q.cap; }
        m->q.msgs = new_msgs;
        m->q.head = 0; m->q.tail = i;
        m->q.cap  = new_cap;
        next = (m->q.tail + 1) % m->q.cap;
    }
    gc_wb_slot(&m->q.msgs[m->q.tail], msg);
    m->q.tail = next;
    uint32_t depth = (uint32_t)((m->q.tail - m->q.head + m->q.cap) % m->q.cap);
    pthread_cond_signal(&m->cond);
    pthread_mutex_unlock(&m->mutex);

    if (target) {
        atomic_store_explicit(&target->mailbox_depth, depth, memory_order_relaxed);
        /* Credit the send to the sending actor, if any */
        if (current_actor)
            atomic_fetch_add_explicit(&current_actor->msgs_sent, 1, memory_order_relaxed);
    }
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
    uint32_t depth = (uint32_t)((m->q.tail - m->q.head + m->q.cap) % m->q.cap);
    pthread_mutex_unlock(&m->mutex);

    if (current_actor) {
        atomic_fetch_add_explicit(&current_actor->msgs_received, 1, memory_order_relaxed);
        atomic_store_explicit(&current_actor->mailbox_depth, depth, memory_order_relaxed);
    }
    return msg;
}

/* ---- Actor thread entry ---- */

typedef struct {
    Actor *actor;
    val_t  closure;
    val_t  args;
    /* Directory-context inheritance -- see load_dir_snapshot's header
     * comment in runtime.c. Captured on the spawning thread in
     * actor_spawn, adopted on the new thread in actor_thread. */
    char **load_dir_snap;
    int    load_dir_snap_count;
} ActorStart;

static void actor_thread_cleanup(void *arg) {
    (void)arg;
}

static void *actor_thread(void *arg) {
    ActorStart *start = (ActorStart *)arg;
    gc_register_thread();
    load_dir_adopt_snapshot(start->load_dir_snap, start->load_dir_snap_count);
    pthread_cleanup_push(actor_thread_cleanup, NULL);
    vm_init();

    Actor *self = start->actor;
    current_actor = self;

    val_t reason = V_FALSE;
    ExnHandler h;
    val_t result = V_VOID;

    h.prev = current_handler;
    h.saved_jit_depth = jit_depth_save();
    current_handler = &h;
    uint64_t t0 = mono_ns();
    if (setjmp(h.jmp) == 0) {
        result = apply(start->closure, start->args);
        current_handler = h.prev;
    } else {
        current_handler = h.prev;
        jit_depth_restore(h.saved_jit_depth);
        reason = h.exn;
        fprintf(stderr, "Actor %lu died: ", (unsigned long)self->id);
        scm_write_shared(reason, PORT_STDERR);
        fprintf(stderr, "\n");
    }
    atomic_fetch_add_explicit(&self->ns_in_body, mono_ns() - t0, memory_order_relaxed);
    (void)result;

    pthread_mutex_lock(&self->lock);
    self->alive = false;
    pthread_mutex_unlock(&self->lock);
    actor_registry_remove(self);

    /* Release the directory-context entries load_dir_adopt_snapshot
     * seeded this thread's stack with above -- this thread is about to
     * exit, so releasing back to depth 0 (rather than whatever depth it
     * started at) frees everything it holds. Every other caller of this
     * stack (scm_load, load_scheme_module, main.c's script-run path)
     * already marks/releases what it pushes; this was the one path that
     * adopted entries without ever releasing them, confirmed via `leaks`
     * to leak one allocation per actor spawned from an active load
     * context (i.e. nearly every actor in practice). */
    load_dir_release(0);

    vm_free();
    pthread_cleanup_pop(1);
    return NULL;
}

/* ---- Public API ---- */

void actors_init(void) {
    /* Nothing needed: GC is already thread-safe after gc_init() */
}

val_t actor_spawn(val_t closure, val_t args) {
    /* If closure has upvalues, give the ACTOR its own private, frozen
     * snapshot of them before handing it to a brand-new thread below —
     * never mutate the original closure/its upvalues in place. Without
     * some fix here, the new actor thread's OP_LOAD_UP (a plain,
     * unsynchronized `*upval->location` read) can race this thread's own
     * continuing execution — e.g. a tail-recursive loop's very next
     * iteration reusing that exact stack slot for its next parameter —
     * with no lock, no atomic, no fence anywhere in the path. Confirmed:
     * 2000 actors spawned from inside a loop capturing the loop variable
     * produced several actors reading a duplicated/skipped value with no
     * error, even on a plain non-TSan build.
     *
     * An open Upvalue is shared by pointer with anything else that
     * captured the same variable from the same still-live scope (a
     * sibling closure, or the enclosing frame's own local-variable
     * access), so closing it IN PLACE — an earlier version of this fix —
     * is wrong: it freezes the value for all of them the instant one
     * escapes to spawn, not just the one being spawned. Building a
     * fresh, independent closure (vm_snapshot_closure_for_escape) instead
     * leaves the original completely untouched. */
    if (vis_bcclosure(closure))
        closure = vptr(vm_snapshot_closure_for_escape(as_bcclosure(closure)));

    Actor *a = CURRY_NEW_PINNED(Actor);
    a->hdr.type  = T_ACTOR;
    a->hdr.flags = 0;
    a->id        = atomic_fetch_add(&next_actor_id, 1);
    a->mailbox   = mailbox_new();
    a->closure   = closure;
    a->parent    = current_actor;
    a->name      = V_FALSE;
    a->alive     = true;
    pthread_mutex_init(&a->lock, NULL);
    atomic_init(&a->msgs_received, 0);
    atomic_init(&a->msgs_sent,     0);
    atomic_init(&a->ns_in_body,    0);
    atomic_init(&a->mailbox_depth, 0);
    a->spawn_ns  = mono_ns();

    /* Register BEFORE pthread_create, not after: a fast-exiting actor can
     * run actor_thread to completion (including actor_registry_remove)
     * before the SPAWNING thread ever gets scheduled again to run the
     * add call below it -- pthread_create returning success only means
     * the thread now exists and may run concurrently, not that it hasn't
     * already finished. Registering first means the worst case is a
     * harmless double-write (add then immediately remove), never a
     * remove-before-add that permanently strands a dead entry (confirmed
     * by independent review: 368/500 short-lived actors leaked this way
     * with the old post-create ordering, and once the fixed-size table
     * fills from leaked entries, every later actor silently stops
     * appearing in list-actors for the rest of the process). */
    actor_registry_add(a);

    ActorStart *start = (ActorStart *)gc_alloc_raw_pinned(sizeof(ActorStart));
    start->actor   = a;
    start->closure = closure;
    start->args    = args;
    start->load_dir_snap = load_dir_snapshot(&start->load_dir_snap_count);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    if (pthread_create(&a->thread, &attr, actor_thread, start) != 0) {
        /* actor_thread — the only code that would otherwise adopt and
         * later release start->load_dir_snap — never runs, so free it
         * here or it leaks (along with every strdup'd string in it).
         * a->alive stays accurate: no thread is actually running. Also
         * undo the optimistic registration above -- actor_thread's own
         * exit path (which normally does this) never runs either. */
        for (int i = 0; i < start->load_dir_snap_count; i++)
            free(start->load_dir_snap[i]);
        free(start->load_dir_snap);
        /* No other thread can hold a reference to `a` yet at this point
         * (actor_spawn hasn't returned it to its caller), so this write
         * is provably race-free even unlocked -- but take the lock
         * anyway to match actor_thread's own exit-path pattern for the
         * same field, removing any doubt for a future reader (found by
         * independent review). */
        pthread_mutex_lock(&a->lock);
        a->alive = false;
        pthread_mutex_unlock(&a->lock);
        actor_registry_remove(a);
    }
    pthread_attr_destroy(&attr);

    return vptr(a);
}

void actor_send(val_t actor_val, val_t msg) {
    if (!vis_actor(actor_val)) return;
    Actor *a = as_actor(actor_val);
    mailbox_push(a->mailbox, msg, a);
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
    (void)a; (void)b;
}

void actor_monitor(val_t monitor, val_t target) {
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

/* ---- Stats snapshot ---- */

val_t actor_stats(val_t actor_val) {
    if (!vis_actor(actor_val)) return V_FALSE;
    Actor *a = as_actor(actor_val);

    uint64_t msgs_rx  = atomic_load_explicit(&a->msgs_received,  memory_order_relaxed);
    uint64_t msgs_tx  = atomic_load_explicit(&a->msgs_sent,      memory_order_relaxed);
    uint64_t ns_body  = atomic_load_explicit(&a->ns_in_body,     memory_order_relaxed);
    uint32_t depth    = atomic_load_explicit(&a->mailbox_depth,  memory_order_relaxed);
    uint64_t age_ns   = mono_ns() - a->spawn_ns;

    pthread_mutex_lock(&a->lock);
    bool alive = a->alive;
    pthread_mutex_unlock(&a->lock);

#define APAIR(k,v) result = scm_cons(scm_cons(sym_intern_cstr(k),(v)), result)
    val_t result = V_NIL;
    APAIR("alive",          vbool(alive));
    APAIR("age-ns",         vfix((intptr_t)age_ns));
    APAIR("mailbox-depth",  vfix((intptr_t)depth));
    APAIR("ns-in-body",     vfix((intptr_t)ns_body));
    APAIR("msgs-sent",      vfix((intptr_t)msgs_tx));
    APAIR("msgs-received",  vfix((intptr_t)msgs_rx));
    APAIR("id",             vfix((intptr_t)a->id));
    APAIR("name",           a->name);
#undef APAIR
    return result;
}
