#include "env.h"
#include "object.h"
#include "gc.h"
#include "symbol.h"
#include <gc/gc.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

// Scath was here

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));
extern void scm_raise_code(val_t code, const char *fmt, ...) __attribute__((noreturn));

val_t GLOBAL_ENV;
/* Raw C pointer to the root EnvFrame, kept in .data so Boehm's conservative
 * segment scan finds it and keeps the frame alive across major GCs. */
void *gc_env_frame_pin = NULL;

/*
 * GLOBAL_ENV's root frame is the only EnvFrame ever touched by more than one
 * thread: actors run on independent OS threads but all share one namespace,
 * and every OTHER frame (function-call locals, let bodies, ...) is owned
 * exclusively by whichever single C call stack created it — an escaping
 * closure gets its own private upvalue snapshot instead of sharing the
 * frame it closed over (see vm_snapshot_closure_for_escape in vm.c), so
 * those never need what follows.
 *
 * frame_grow/frame_hash_rehash reallocate f->syms/f->vals/f->hidx and bump
 * f->cap/f->hcap/f->size — four-plus plain-C-struct-field writes with zero
 * synchronization. Two actor threads concurrently running the same
 * top-level closure (spawned from a loop, a common actor-model pattern) can
 * race a frame_hash_rehash on GLOBAL_ENV (triggered by any (define ...)
 * elsewhere in the program while they run) against their own global-
 * variable reads: a reader can observe a NEW f->hcap paired with the OLD
 * f->hidx (or vice versa), computing a hash slot for a table that isn't the
 * one actually installed. hidx entries are raw array indices with no bounds
 * tag, so a hash bucket read against the wrong-generation table can yield
 * an index at or past the CURRENT f->size, and syms[garbage_idx] / a
 * dereference of &vals[garbage_idx] reads unrelated heap memory — observed
 * in practice as a live Scheme program calling an unrelated captured
 * closure (wrong arity, "too many arguments") in place of the primitive or
 * variable it actually asked for. Confirmed via repeated runs of
 * tests/actors_tests.scm's STM section (4 actor threads hammering one
 * shared tvar) under an LD_PRELOAD SIGSEGV trap and gdb, and confirmed the
 * race persists even with the VM's per-chunk global-variable inline cache
 * (glob_cache) forced off, isolating it to this frame, not the cache.
 *
 * Fix: treat f->version (already present to invalidate glob_cache entries)
 * as a seqlock. Writers (frame_define, only ever taken for the global
 * frame's slow, rare path) bump it to odd before touching structure and to
 * the next even value once done, serialized against each other by
 * g_global_frame_lock. Lock-free readers (frame_lookup/frame_set/
 * frame_lookup_versioned) snapshot version before and after their read and
 * retry if it was odd (writer in progress) or changed (writer completed
 * mid-read) — standard Linux-kernel-style seqlock, safe under C11's memory
 * model because the acquire/release pair on version establishes
 * happens-before around the plain (non-atomic) reads/writes of
 * syms/vals/hidx/size/cap sandwiched inside it. Local, single-thread-owned
 * frames skip all of this (frame_is_global short-circuits to the original
 * unprotected fast path) since they were never the problem.
 *
 * frame_is_global is keyed on "is this a ROOT frame" (parent == NULL), not
 * literal identity with GLOBAL_ENV — GLOBAL_ENV's own frame happens to be a
 * root frame too (env_new_root_permanent(), same shape as env_new_root()),
 * so this check already covered it correctly without special-casing it.
 * Widened deliberately (not merely "happens to also work") once Chunk::
 * target_env (chunk.h) made it possible for compiled code's OP_LOAD_GLOBAL/
 * OP_STORE_GLOBAL/OP_DEF_GLOBAL to target a define-library body's own
 * env_new_root() frame instead of GLOBAL_ENV: once such a frame's exported
 * closures can be called from more than one actor thread (two actors both
 * importing and calling the same library concurrently), it is exactly as
 * exposed to the frame_hash_rehash race above as GLOBAL_ENV always was —
 * the race was never about GLOBAL_ENV specifically, only about "a root
 * frame reachable from more than one thread," which used to be exactly one
 * frame and now, in principle, is not. Every non-root frame (function-call
 * locals, let bodies, C-extension module envs loaded via env_extend) stays
 * on the original unprotected fast path, unaffected.
 *
 * Coarse-grained by design, not yet optimized: every root frame currently
 * shares g_global_frame_lock, so a rare structural write to one
 * define-library's frame briefly serializes against a concurrent write to
 * an unrelated one (or to GLOBAL_ENV itself) even though they touch
 * disjoint memory. Root-frame writes are load-time-rare (each top-level
 * define, once, while a library loads) as opposed to per-call, so this is
 * expected to be negligible in practice; a per-frame lock would remove even
 * that if it ever shows up as real contention, not attempted here since
 * nothing yet exercises a non-GLOBAL_ENV root frame from more than one
 * thread (Track 2 of the eval-elimination migration, not yet started, is
 * what would first make that happen).
 */
static pthread_mutex_t g_global_frame_lock = PTHREAD_MUTEX_INITIALIZER;

static inline bool frame_is_global(const EnvFrame *f) {
    return f->parent == NULL;
}

static inline uint32_t seq_begin_write(EnvFrame *f) {
    uint32_t v = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_relaxed);
    atomic_store_explicit((_Atomic uint32_t *)&f->version, v | 1, memory_order_release);
    return v;
}
static inline void seq_end_write(EnvFrame *f, uint32_t v) {
    atomic_store_explicit((_Atomic uint32_t *)&f->version, v + 2, memory_order_release);
}

/* ---- Pair construction (needed for rest-arg list building) ---- */

static val_t env_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

/* ---- Hash helpers ---- */

/* Knuth multiplicative hash on the interned symbol pointer */
static uint32_t sym_hash(val_t sym, uint32_t hcap) {
    return (uint32_t)((sym >> 3) * 2654435761u) & (hcap - 1);
}

static void hash_insert(uint32_t *hidx, uint32_t hcap, val_t *syms, uint32_t idx) {
    uint32_t h = sym_hash(syms[idx], hcap);
    while (hidx[h] != UINT32_MAX) h = (h + 1) & (hcap - 1);
    hidx[h] = idx;
}

static void frame_build_hash(EnvFrame *f) {
    /* hcap = smallest power-of-2 >= size * 2 (≤ 75% load) */
    uint32_t hcap = 4;
    while (hcap < f->size * 2) hcap <<= 1;
    uint32_t *hidx = (uint32_t *)gc_alloc_raw_pinned_atomic(hcap * sizeof(uint32_t));
    memset(hidx, 0xFF, hcap * sizeof(uint32_t)); /* UINT32_MAX = empty */
    for (uint32_t i = 0; i < f->size; i++)
        hash_insert(hidx, hcap, f->syms, i);
    f->hidx = hidx;
    f->hcap = hcap;
}

static void frame_hash_rehash(EnvFrame *f) {
    uint32_t hcap = f->hcap;
    while (hcap < f->size * 2) hcap <<= 1;
    if (hcap == f->hcap) {
        /* Just re-insert the newest entry */
        hash_insert(f->hidx, f->hcap, f->syms, f->size - 1);
        return;
    }
    /* Need larger table */
    uint32_t *hidx = (uint32_t *)gc_alloc_raw_pinned_atomic(hcap * sizeof(uint32_t));
    memset(hidx, 0xFF, hcap * sizeof(uint32_t));
    for (uint32_t i = 0; i < f->size; i++)
        hash_insert(hidx, hcap, f->syms, i);
    f->hidx = hidx;
    f->hcap = hcap;
}

/* ---- Frame ---- */

EnvFrame *frame_new(uint32_t cap, EnvFrame *parent) {
    EnvFrame *f = CURRY_NEW_PINNED(EnvFrame);
    f->hdr.type  = T_ENV;
    f->hdr.flags = 0;
    f->size   = 0;
    f->cap    = cap ? cap : 8;
    f->syms   = (val_t *)gc_alloc_raw_pinned(f->cap * sizeof(val_t));
    f->vals   = (val_t *)gc_alloc_raw_pinned(f->cap * sizeof(val_t));
    f->parent = parent;
    f->hidx   = NULL;
    f->hcap   = 0;
    f->version = 0;
    return f;
}

/* Version bump for this structural change is the caller's (frame_define's)
 * job — it wraps the whole redefine-or-insert sequence (of which this is
 * one step) in a single seq_begin_write/seq_end_write pair. Bumping here
 * too would flip the shared frame's seqlock parity back to "even" (no
 * writer in progress) while frame_define is still mutating size/syms/vals/
 * hidx afterward, letting a concurrent lock-free reader through to observe
 * a torn intermediate state. */
static void frame_grow(EnvFrame *f) {
    uint32_t new_cap = f->cap * 2;
    val_t *ns = (val_t *)gc_alloc_raw_pinned(new_cap * sizeof(val_t));
    val_t *nv = (val_t *)gc_alloc_raw_pinned(new_cap * sizeof(val_t));
    memcpy(ns, f->syms, f->size * sizeof(val_t));
    memcpy(nv, f->vals, f->size * sizeof(val_t));
    f->syms = ns; f->vals = nv; f->cap = new_cap;
}

static bool frame_define_unlocked(EnvFrame *f, val_t sym, val_t val) {
    /* Check if already in this frame (redefine) */
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) { gc_wb_slot(&f->vals[idx], val); return true; }
            h = (h + 1) & (f->hcap - 1);
        }
    } else {
        for (uint32_t i = 0; i < f->size; i++)
            if (f->syms[i] == sym) { gc_wb_slot(&f->vals[i], val); return true; }
    }
    if (f->size >= f->cap) frame_grow(f);
    f->syms[f->size] = sym;
    gc_wb_slot(&f->vals[f->size], val);
    f->size++;
    /* Build or update hash index */
    if (f->hcap) {
        frame_hash_rehash(f);
    } else if (f->size >= FRAME_HASH_THRESHOLD) {
        frame_build_hash(f);
    }
    return true;
}

bool frame_define(EnvFrame *f, val_t sym, val_t val) {
    if (!frame_is_global(f))
        return frame_define_unlocked(f, sym, val);
    pthread_mutex_lock(&g_global_frame_lock);
    uint32_t v = seq_begin_write(f);
    bool result = frame_define_unlocked(f, sym, val);
    seq_end_write(f, v);
    pthread_mutex_unlock(&g_global_frame_lock);
    return result;
}

static bool frame_set_unlocked(EnvFrame *f, val_t sym, val_t val) {
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) { gc_wb_slot(&f->vals[idx], val); return true; }
            h = (h + 1) & (f->hcap - 1);
        }
        return false;
    }
    for (uint32_t i = 0; i < f->size; i++)
        if (f->syms[i] == sym) { gc_wb_slot(&f->vals[i], val); return true; }
    return false;
}

bool frame_set(EnvFrame *f, val_t sym, val_t val) {
    if (!frame_is_global(f))
        return frame_set_unlocked(f, sym, val);
    /* frame_set writes an existing slot's value (no structural change), so
     * unlike frame_define it doesn't need the odd/even seqlock transition —
     * but it still walks hidx/syms to find that slot, which races a
     * concurrent frame_define's restructuring the same way frame_lookup
     * does. Taking g_global_frame_lock here (the same lock frame_define
     * holds for its whole critical section) rules that out directly: no
     * frame_define can be reallocating syms/vals/hidx while we hold it, so
     * the traversal below is safe without a separate retry loop. This also
     * serializes two actors calling (set! shared-global ...) at the same
     * time against EACH OTHER, matching frame_define's writer-vs-writer
     * guarantee — without it, two concurrent gc_wb_slot stores to the same
     * slot are a plain, unsynchronized write/write race. */
    pthread_mutex_lock(&g_global_frame_lock);
    bool result = frame_set_unlocked(f, sym, val);
    pthread_mutex_unlock(&g_global_frame_lock);
    return result;
}

static val_t *frame_lookup_unlocked(EnvFrame *f, val_t sym) {
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) return &f->vals[idx];
            h = (h + 1) & (f->hcap - 1);
        }
        return NULL;
    }
    for (uint32_t i = 0; i < f->size; i++)
        if (f->syms[i] == sym) return &f->vals[i];
    return NULL;
}

val_t *frame_lookup_versioned(EnvFrame *f, val_t sym, uint32_t *out_ver) {
    if (!frame_is_global(f)) {
        if (out_ver) *out_ver = 0;
        return frame_lookup_unlocked(f, sym);
    }
    for (;;) {
        uint32_t v1 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v1 & 1) continue;
        val_t *result = frame_lookup_unlocked(f, sym);
        uint32_t v2 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v2 != v1) continue;
        if (out_ver) *out_ver = v1;
        return result;
    }
}

val_t *frame_lookup(EnvFrame *f, val_t sym) {
    return frame_lookup_versioned(f, sym, NULL);
}

/* ---- Environment ---- */

val_t env_new_root(void) {
    return vptr(frame_new(64, NULL));
}

/*
 * Allocate the global root EnvFrame as GC_MALLOC_UNCOLLECTABLE so Boehm
 * never frees it (Boehm's conservative scan of .data/.bss variables and
 * GC_MALLOC_UNCOLLECTABLE blocks reliably finds ordinary allocations but
 * can miss static pointers inside C .data sections on some macOS arm64
 * configurations when a Boehm GC fires during nursery-fill execution).
 * The root frame lives for the entire program lifetime — making it
 * uncollectable is both correct and leak-free.
 */
static val_t env_new_root_permanent(void) {
    EnvFrame *f = (EnvFrame *)GC_MALLOC_UNCOLLECTABLE(sizeof(EnvFrame));
    if (!f) { fprintf(stderr, "OOM: root EnvFrame\n"); abort(); }
    f->hdr.type  = T_ENV;
    f->hdr.flags = 0;
    f->size      = 0;
    f->cap       = 64;
    /* Use GC_MALLOC_UNCOLLECTABLE for the arrays too so Boehm's scan of
     * the uncollectable frame block keeps them reachable.  They are
     * effectively leaked (intentionally — they live for the process lifetime). */
    f->syms = (val_t *)GC_MALLOC_UNCOLLECTABLE(64 * sizeof(val_t));
    f->vals = (val_t *)GC_MALLOC_UNCOLLECTABLE(64 * sizeof(val_t));
    f->parent  = NULL;
    f->hidx    = NULL;
    f->hcap    = 0;
    f->version = 0;
    /* Register in pinned_slots so scan_pinned_object updates f->vals during
     * minor GC (required even though the frame is uncollectable). */
    extern void gc_gen_pin_permanent(void *);
    gc_gen_pin_permanent(f);
    return vptr(f);
}

val_t env_extend(val_t parent) {
    EnvFrame *pf = vis_env(parent) ? as_env(parent) : NULL;
    return vptr(frame_new(8, pf));
}

void env_define(val_t env, val_t sym, val_t val) {
    frame_define(as_env(env), sym, val);
}

bool env_set(val_t env, val_t sym, val_t val) {
    EnvFrame *f = as_env(env);
    while (f) {
        if (frame_set(f, sym, val)) return true;
        f = f->parent;
    }
    return false;
}

val_t env_lookup(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot) {
            if (*slot == V_UNDEF)
                scm_raise(V_FALSE, "variable used before initialization: %s", sym_cstr(sym));
            return *slot;
        }
        f = f->parent;
    }
    scm_raise_code(EC_UNBOUND_VARIABLE, "unbound variable: %s", sym_cstr(sym));
}

val_t env_lookup_or_false(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot && *slot != V_UNDEF) return *slot;
        f = f->parent;
    }
    return V_FALSE;
}

val_t *env_lookup_slot(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot && *slot != V_UNDEF) return slot;
        f = f->parent;
    }
    return NULL;
}

val_t env_bind_args(val_t parent_env, val_t params, val_t args) {
    EnvFrame *f = frame_new(8, vis_env(parent_env) ? as_env(parent_env) : NULL);
    val_t p = params, a = args;
    while (vis_pair(p)) {
        if (vis_nil(a)) scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "too few arguments");
        frame_define(f, vcar(p), vcar(a));
        p = vcdr(p); a = vcdr(a);
    }
    if (!vis_nil(p))
        frame_define(f, p, a);          /* rest arg */
    else if (!vis_nil(a))
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "too many arguments");
    return vptr(f);
}

/* Bind parameters from a C array — avoids building an intermediate cons list. */
val_t env_bind_arr(val_t parent_env, val_t params, int argc, val_t *argv) {
    EnvFrame *f = frame_new(8, vis_env(parent_env) ? as_env(parent_env) : NULL);
    val_t p = params;
    int i = 0;
    while (vis_pair(p)) {
        if (i >= argc) scm_raise(V_FALSE, "too few arguments");
        frame_define(f, vcar(p), argv[i++]);
        p = vcdr(p);
    }
    if (!vis_nil(p)) {
        /* Rest parameter: build list from remaining argv elements */
        val_t rest = V_NIL;
        for (int j = argc - 1; j >= i; j--)
            rest = env_cons(argv[j], rest);
        frame_define(f, p, rest);
    } else if (i < argc) {
        scm_raise_code(EC_WRONG_NUMBER_OF_ARGUMENTS, "too many arguments");
    }
    return vptr(f);
}

void env_init(void) {
    GLOBAL_ENV = env_new_root_permanent();
    gc_env_frame_pin = (void *)(uintptr_t)GLOBAL_ENV;
    gc_register_root(&GLOBAL_ENV);
}
