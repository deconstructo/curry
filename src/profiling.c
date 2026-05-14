#include "profiling.h"
#include "symbol.h"
#include "object.h"
#include "gc.h"
#include "env.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

int curry_profiling_level    = 0;
int curry_gc_profiling_level = 0;

/* ---- Hash table ---- */

#define PROF_SLOTS  4096
#define PROF_MASK   (PROF_SLOTS - 1)
#define PROF_LOAD   3072   /* resize/warn threshold — 75% */

typedef struct {
    val_t    key;       /* interned symbol val_t; 0 = empty slot */
    uint64_t calls;
    uint64_t ns_total;
    int      tco_calls; /* counted separately — no timing available */
} ProfSlot;

static ProfSlot          prof_table[PROF_SLOTS];
static int               prof_count = 0;
static pthread_mutex_t   prof_mutex = PTHREAD_MUTEX_INITIALIZER;

static ProfSlot *slot_for(val_t key) {
    uint32_t h = (uint32_t)(key >> 3) & PROF_MASK;
    for (uint32_t i = 0; i < PROF_SLOTS; i++) {
        uint32_t idx = (h + i) & PROF_MASK;
        if (prof_table[idx].key == 0 || prof_table[idx].key == key)
            return &prof_table[idx];
    }
    return NULL; /* table full — shouldn't happen at 75% load */
}

static void record_locked(val_t key, uint64_t ns) {
    ProfSlot *s = slot_for(key);
    if (!s) return;
    if (s->key == 0) { s->key = key; prof_count++; }
    s->calls++;
    s->ns_total += ns;
}

static void record_tco_locked(val_t key) {
    ProfSlot *s = slot_for(key);
    if (!s) return;
    if (s->key == 0) { s->key = key; prof_count++; }
    s->tco_calls++;
}

/* ---- Public hook functions ---- */

uint64_t profiling_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void profiling_record_call(val_t name_sym) {
    pthread_mutex_lock(&prof_mutex);
    record_locked(name_sym, 0);
    pthread_mutex_unlock(&prof_mutex);
}

void profiling_record_timed(val_t name_sym, uint64_t start_ns) {
    uint64_t elapsed = profiling_now_ns() - start_ns;
    pthread_mutex_lock(&prof_mutex);
    record_locked(name_sym, elapsed);
    pthread_mutex_unlock(&prof_mutex);
}

void profiling_record_call_tco(val_t name_sym) {
    pthread_mutex_lock(&prof_mutex);
    record_tco_locked(name_sym);
    pthread_mutex_unlock(&prof_mutex);
}

void profiling_record_prim(val_t name_sym) {
    pthread_mutex_lock(&prof_mutex);
    record_locked(name_sym, 0);
    pthread_mutex_unlock(&prof_mutex);
}

void profiling_reset(void) {
    pthread_mutex_lock(&prof_mutex);
    memset(prof_table, 0, sizeof(prof_table));
    prof_count = 0;
    pthread_mutex_unlock(&prof_mutex);
}

void profiling_set_level(int level) {
    curry_profiling_level = (level < 0) ? 0 : (level > 3) ? 3 : level;
}

void gc_profiling_set_level(int level) {
    curry_gc_profiling_level = (level < 0) ? 0 : (level > 1) ? 1 : level;
}

/* ---- Report ---- */

/* qsort comparator: descending by total calls (apply + tco) */
static int cmp_slots(const void *a, const void *b) {
    const ProfSlot *sa = (const ProfSlot *)a;
    const ProfSlot *sb = (const ProfSlot *)b;
    uint64_t ta = sa->calls + (uint64_t)sa->tco_calls;
    uint64_t tb = sb->calls + (uint64_t)sb->tco_calls;
    return (tb > ta) ? 1 : (tb < ta) ? -1 : 0;
}

static val_t make_pair_gc(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

val_t profiling_report(void) {
    pthread_mutex_lock(&prof_mutex);

    /* Collect non-empty slots into a scratch array */
    ProfSlot scratch[PROF_SLOTS];
    int n = 0;
    for (int i = 0; i < PROF_SLOTS; i++)
        if (prof_table[i].key != 0)
            scratch[n++] = prof_table[i];

    pthread_mutex_unlock(&prof_mutex);

    qsort(scratch, (size_t)n, sizeof(ProfSlot), cmp_slots);

    /* Build alist: ((name . (total-calls . ns)) ...) */
    val_t result = V_NIL;
    for (int i = n - 1; i >= 0; i--) {
        ProfSlot *s = &scratch[i];
        uint64_t total = s->calls + (uint64_t)s->tco_calls;
        val_t calls_val = vfix((intptr_t)total);
        val_t ns_val    = vfix((intptr_t)s->ns_total);
        val_t inner     = make_pair_gc(calls_val, ns_val);
        val_t entry     = make_pair_gc(s->key, inner);
        result          = make_pair_gc(entry, result);
    }
    return result;
}

/* ---- Init ---- */

void profiling_init(val_t env) {
    val_t sym_ep = sym_intern_cstr("**eval-profiler**");
    val_t sym_gp = sym_intern_cstr("**gc-profiler**");
    env_define(env, sym_ep, vfix(0));
    env_define(env, sym_gp, vfix(0));
}
