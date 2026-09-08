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

/*
 * Issue #153: seq_begin_write/seq_end_write's own release/acquire pairing
 * on `version` is C11-sound on its own terms -- a reader whose acquire
 * load of `version` observes a given even value synchronizes-with
 * whichever seq_end_write produced it, establishing happens-before over
 * everything that writer did before that release store. The bug is a
 * layer down: syms/vals/hidx/cap/hcap/size themselves are PLAIN fields,
 * and a plain read racing a plain write to the SAME memory is a data
 * race by the letter of the C11 standard the instant it happens in real
 * time -- regardless of whether the reader's later version check (v2==v1)
 * discards the result afterward. Confirmed via a real TSan run (see the
 * comment on issue #153): frame_grow's plain `f->syms = ns` write racing
 * frame_lookup_unlocked's plain `f->syms[idx]` read, reliably, under
 * concurrent actors mixing `(define ...)` (forcing frame_grow) against
 * plain global reads.
 *
 * Fix: make the fields themselves properly atomic (relaxed suffices --
 * the ordering guarantee readers need still comes entirely from the
 * separate version-counter's acquire/release above; these just need to
 * stop being non-atomic so a racing access is well-defined instead of
 * UB). Gated on frame_is_global(f): these accessor functions are shared
 * between the global root frame (genuinely read/written by more than one
 * actor thread) and every other frame (function-call locals, let bodies,
 * ...), which is exclusively owned by whichever single call stack
 * created it and never has this problem at all -- see this file's own
 * top-of-file comment. Unconditionally atomic-ifying local-frame access
 * would tax the hottest path in the tree-walking interpreter (every
 * function-call frame's parameter binding) for zero benefit, so every
 * field touch below is gated on a single frame_is_global(f) check per
 * call, not applied blindly. */
static inline uint32_t g_load_u32(uint32_t *p, bool atomic) {
    return atomic ? atomic_load_explicit((_Atomic uint32_t *)p, memory_order_relaxed) : *p;
}
static inline void g_store_u32(uint32_t *p, uint32_t v, bool atomic) {
    if (atomic) atomic_store_explicit((_Atomic uint32_t *)p, v, memory_order_relaxed);
    else *p = v;
}
static inline val_t g_load_val(val_t *p, bool atomic) {
    return atomic ? atomic_load_explicit((_Atomic val_t *)p, memory_order_relaxed) : *p;
}
static inline void g_store_val(val_t *p, val_t v, bool atomic) {
    if (atomic) atomic_store_explicit((_Atomic val_t *)p, v, memory_order_relaxed);
    else *p = v;
}
static inline val_t *g_load_valptr(val_t **p, bool atomic) {
    return atomic ? (val_t *)atomic_load_explicit((_Atomic(val_t *) *)p, memory_order_relaxed) : *p;
}
static inline void g_store_valptr(val_t **p, val_t *v, bool atomic) {
    if (atomic) atomic_store_explicit((_Atomic(val_t *) *)p, v, memory_order_relaxed);
    else *p = v;
}
static inline uint32_t *g_load_u32ptr(uint32_t **p, bool atomic) {
    return atomic ? (uint32_t *)atomic_load_explicit((_Atomic(uint32_t *) *)p, memory_order_relaxed) : *p;
}
static inline void g_store_u32ptr(uint32_t **p, uint32_t *v, bool atomic) {
    if (atomic) atomic_store_explicit((_Atomic(uint32_t *) *)p, v, memory_order_relaxed);
    else *p = v;
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

/* `atomic`: true when writing into a hidx array a lock-free reader can
 * already see (the global frame's LIVE f->hidx, rehash's "just reinsert"
 * fast path) -- false when building a fresh array not yet published via
 * f->hidx (safe to touch plainly, no reader can reach it yet). syms is
 * read-only here and only ever touched by the thread already holding
 * g_global_frame_lock (hash_insert is only called from frame_build_hash/
 * frame_hash_rehash, both invoked from within frame_define_unlocked while
 * the writer lock is held), so no atomics are needed for that read. */
static void hash_insert(uint32_t *hidx, uint32_t hcap, val_t *syms, uint32_t idx, bool atomic) {
    uint32_t h = sym_hash(syms[idx], hcap);
    while (g_load_u32(&hidx[h], atomic) != UINT32_MAX) h = (h + 1) & (hcap - 1);
    g_store_u32(&hidx[h], idx, atomic);
}

static void frame_build_hash(EnvFrame *f) {
    /* hcap = smallest power-of-2 >= size * 2 (≤ 75% load) */
    uint32_t hcap = 4;
    while (hcap < f->size * 2) hcap <<= 1;
    uint32_t *hidx = (uint32_t *)gc_alloc_raw_pinned_atomic(hcap * sizeof(uint32_t));
    memset(hidx, 0xFF, hcap * sizeof(uint32_t)); /* UINT32_MAX = empty */
    for (uint32_t i = 0; i < f->size; i++)
        hash_insert(hidx, hcap, f->syms, i, false); /* fresh array, not yet published */
    bool g = frame_is_global(f);
    g_store_u32ptr(&f->hidx, hidx, g);
    g_store_u32(&f->hcap, hcap, g);
}

static void frame_hash_rehash(EnvFrame *f) {
    bool g = frame_is_global(f);
    uint32_t hcap = f->hcap;
    while (hcap < f->size * 2) hcap <<= 1;
    if (hcap == f->hcap) {
        /* Just re-insert the newest entry -- into the LIVE f->hidx a
         * lock-free reader can already be probing. */
        hash_insert(f->hidx, f->hcap, f->syms, f->size - 1, g);
        return;
    }
    /* Need larger table */
    uint32_t *hidx = (uint32_t *)gc_alloc_raw_pinned_atomic(hcap * sizeof(uint32_t));
    memset(hidx, 0xFF, hcap * sizeof(uint32_t));
    for (uint32_t i = 0; i < f->size; i++)
        hash_insert(hidx, hcap, f->syms, i, false); /* fresh array, not yet published */
    g_store_u32ptr(&f->hidx, hidx, g);
    g_store_u32(&f->hcap, hcap, g);
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
    bool g = frame_is_global(f);
    uint32_t new_cap = f->cap * 2;
    val_t *ns = (val_t *)gc_alloc_raw_pinned(new_cap * sizeof(val_t));
    val_t *nv = (val_t *)gc_alloc_raw_pinned(new_cap * sizeof(val_t));
    memcpy(ns, f->syms, f->size * sizeof(val_t));
    memcpy(nv, f->vals, f->size * sizeof(val_t));
    g_store_valptr(&f->syms, ns, g);
    g_store_valptr(&f->vals, nv, g);
    g_store_u32(&f->cap, new_cap, g);
}

/* Issue #153: the redefine-search reads below (hcap/hidx/syms/size) are
 * safe as plain reads regardless of frame_is_global -- this function only
 * ever runs while g_global_frame_lock is held (via frame_define), so no
 * OTHER writer can be concurrently mutating them, and a lock-free
 * reader's OWN concurrent read of the same fields is a benign read-read,
 * never a race. What DOES need gating: every WRITE a lock-free reader
 * could observe -- the redefine value-write, and the insert path's
 * syms/vals/size writes (matching gc_wb_slot_atomic_relaxed's own doc
 * comment: relaxed suffices, ordering comes from the seqlock). */
static bool frame_define_unlocked(EnvFrame *f, val_t sym, val_t val) {
    bool g = frame_is_global(f);
    /* Check if already in this frame (redefine) */
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) {
                if (g) gc_wb_slot_atomic_relaxed(&f->vals[idx], val);
                else   gc_wb_slot(&f->vals[idx], val);
                return true;
            }
            h = (h + 1) & (f->hcap - 1);
        }
    } else {
        for (uint32_t i = 0; i < f->size; i++)
            if (f->syms[i] == sym) {
                if (g) gc_wb_slot_atomic_relaxed(&f->vals[i], val);
                else   gc_wb_slot(&f->vals[i], val);
                return true;
            }
    }
    if (f->size >= f->cap) frame_grow(f);
    g_store_val(&f->syms[f->size], sym, g);
    if (g) gc_wb_slot_atomic_relaxed(&f->vals[f->size], val);
    else   gc_wb_slot(&f->vals[f->size], val);
    g_store_u32(&f->size, f->size + 1, g);
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
            if (f->syms[idx] == sym) {
                /* Reads above are safe plain reads regardless of global-
                 * ness: frame_set_unlocked only ever runs while
                 * g_global_frame_lock is held for the global case (via
                 * frame_set), ruling out a concurrent structural writer;
                 * a lock-free reader's own concurrent read of the same
                 * fields is a benign read-read. Only this value WRITE
                 * needs gating -- see gc_wb_slot_atomic_relaxed's doc
                 * comment. */
                if (frame_is_global(f)) gc_wb_slot_atomic_relaxed(&f->vals[idx], val);
                else                    gc_wb_slot(&f->vals[idx], val);
                return true;
            }
            h = (h + 1) & (f->hcap - 1);
        }
        return false;
    }
    for (uint32_t i = 0; i < f->size; i++)
        if (f->syms[i] == sym) {
            if (frame_is_global(f)) gc_wb_slot_atomic_relaxed(&f->vals[i], val);
            else                    gc_wb_slot(&f->vals[i], val);
            return true;
        }
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
     * the traversal below is safe without a separate retry loop.
     *
     * This only serializes writer-vs-writer for callers that go through
     * frame_set itself (env_set, i.e. tree-walked `(set! ...)`) — issue
     * #153's independent review flagged that a PRE-EXISTING, separate
     * overclaim here previously said this serializes "two actors calling
     * (set! shared-global ...) against EACH OTHER" unconditionally, which
     * is not true: VM-compiled `(set! ...)` (vm.c's OP_STORE_GLOBAL) writes
     * the value slot directly via gslot_store and has never taken this
     * lock, tree-walked or compiled, before or after #153's fix. Two
     * concurrent writes to the SAME global from mixed tree-walked/compiled
     * call sites are therefore each individually well-defined (gslot_store/
     * gc_wb_slot_atomic_relaxed rule out torn reads/writes, per #153) but
     * NOT ordered relative to each other -- an ordinary unsynchronized
     * last-write-wins race, same as `set!`'s memory model gives you in any
     * language without the caller adding their own synchronization. */
    pthread_mutex_lock(&g_global_frame_lock);
    bool result = frame_set_unlocked(f, sym, val);
    pthread_mutex_unlock(&g_global_frame_lock);
    return result;
}

/* Issue #153: the lock-free (global) read path -- every field touch below
 * is gated on frame_is_global(f), snapshotted ONCE at entry rather than
 * re-derived per access, so one lookup attempt is internally consistent
 * even under a concurrent writer (the outer seqlock retry in
 * frame_lookup_versioned discards the whole attempt if version changed
 * meanwhile; this function's job is only to make sure that discarding is
 * the WORST outcome -- never UB or an out-of-bounds access -- which
 * requires every one of these to be a proper atomic load when g is
 * true). Local (non-global) frames keep the exact original plain-access
 * behavior. */
static val_t *frame_lookup_unlocked(EnvFrame *f, val_t sym) {
    bool g = frame_is_global(f);
    uint32_t hcap = g_load_u32(&f->hcap, g);
    if (hcap) {
        uint32_t *hidx = g_load_u32ptr(&f->hidx, g);
        val_t    *syms = g_load_valptr(&f->syms, g);
        val_t    *vals = g_load_valptr(&f->vals, g);
        uint32_t h = sym_hash(sym, hcap);
        for (;;) {
            uint32_t idx = g_load_u32(&hidx[h], g);
            if (idx == UINT32_MAX) return NULL;
            if (g_load_val(&syms[idx], g) == sym) return &vals[idx];
            h = (h + 1) & (hcap - 1);
        }
    }
    uint32_t size = g_load_u32(&f->size, g);
    val_t   *syms = g_load_valptr(&f->syms, g);
    val_t   *vals = g_load_valptr(&f->vals, g);
    for (uint32_t i = 0; i < size; i++)
        if (g_load_val(&syms[i], g) == sym) return &vals[i];
    return NULL;
}

/* Issue #153 item 2: an uncapped seqlock retry loop is a theoretical
 * livelock under sustained contention (no backoff, restarts on ANY
 * concurrent writer starting or completing mid-read). After this many
 * failed attempts, fall back to taking g_global_frame_lock outright --
 * serializing with writers instead of racing them -- which guarantees
 * forward progress regardless of how hot the contention gets. Picked
 * generously: normal contention resolves in a handful of retries at
 * most (structural global-frame writes are load-time-rare, see this
 * file's own top-of-file comment), so this only ever engages under
 * genuinely pathological, sustained writer pressure. */
#define FRAME_SEQLOCK_RETRY_CAP 1000

val_t *frame_lookup_versioned(EnvFrame *f, val_t sym, uint32_t *out_ver) {
    if (!frame_is_global(f)) {
        if (out_ver) *out_ver = 0;
        return frame_lookup_unlocked(f, sym);
    }
    for (int tries = 0; tries < FRAME_SEQLOCK_RETRY_CAP; tries++) {
        uint32_t v1 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v1 & 1) continue;
        val_t *result = frame_lookup_unlocked(f, sym);
        uint32_t v2 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v2 != v1) continue;
        if (out_ver) *out_ver = v1;
        return result;
    }
    pthread_mutex_lock(&g_global_frame_lock);
    val_t *result = frame_lookup_unlocked(f, sym);
    if (out_ver) *out_ver = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
    pthread_mutex_unlock(&g_global_frame_lock);
    return result;
}

val_t *frame_lookup(EnvFrame *f, val_t sym) {
    return frame_lookup_versioned(f, sym, NULL);
}

/* Copies syms[0..n) / vals[0..n) element-by-element via atomic-relaxed
 * loads instead of memcpy -- issue #153: memcpy has no notion of
 * atomicity, so it can't safely read an array a concurrent writer might
 * be touching (the same UB-by-definition concern gc_wb_slot_atomic_relaxed's
 * doc comment explains for single-slot writes, just for a bulk read). */
static void frame_copy_global(val_t *dst, val_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        dst[i] = atomic_load_explicit((_Atomic val_t *)&src[i], memory_order_relaxed);
}

uint32_t frame_snapshot_bindings(EnvFrame *f, val_t **out_syms, val_t **out_vals) {
    if (!frame_is_global(f)) {
        uint32_t n = f->size;
        val_t *syms = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
        val_t *vals = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
        memcpy(syms, f->syms, n * sizeof(val_t));
        memcpy(vals, f->vals, n * sizeof(val_t));
        *out_syms = syms; *out_vals = vals;
        return n;
    }
    for (int tries = 0; tries < FRAME_SEQLOCK_RETRY_CAP; tries++) {
        uint32_t v1 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v1 & 1) continue;
        uint32_t n = g_load_u32(&f->size, true);
        val_t *fsyms = g_load_valptr(&f->syms, true);
        val_t *fvals = g_load_valptr(&f->vals, true);
        val_t *syms = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
        val_t *vals = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
        frame_copy_global(syms, fsyms, n);
        frame_copy_global(vals, fvals, n);
        uint32_t v2 = atomic_load_explicit((_Atomic uint32_t *)&f->version, memory_order_acquire);
        if (v2 != v1) continue;
        *out_syms = syms; *out_vals = vals;
        return n;
    }
    pthread_mutex_lock(&g_global_frame_lock);
    uint32_t n = f->size;
    val_t *syms = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
    val_t *vals = (val_t *)gc_alloc_raw_pinned(n * sizeof(val_t));
    memcpy(syms, f->syms, n * sizeof(val_t));
    memcpy(vals, f->vals, n * sizeof(val_t));
    pthread_mutex_unlock(&g_global_frame_lock);
    *out_syms = syms; *out_vals = vals;
    return n;
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
    if (env == GLOBAL_ENV) jit_maybe_taint_global_arith(sym, val);
    frame_define(as_env(env), sym, val);
}

bool env_set(val_t env, val_t sym, val_t val) {
    EnvFrame *f = as_env(env);
    while (f) {
        if (frame_set(f, sym, val)) {
            if (vptr(f) == GLOBAL_ENV) jit_maybe_taint_global_arith(sym, val);
            return true;
        }
        f = f->parent;
    }
    return false;
}

/* Issue #153: env.c's own internal helper -- a caller outside this file
 * should use env_slot_load (env.h) instead, which doesn't need a frame
 * argument since all its callers pass a root environment. */
static inline val_t frame_slot_load(EnvFrame *f, val_t *slot) {
    return frame_is_global(f) ? atomic_load_explicit((_Atomic val_t *)slot, memory_order_relaxed) : *slot;
}

val_t env_slot_load(val_t *slot) {
    return atomic_load_explicit((_Atomic val_t *)slot, memory_order_relaxed);
}

val_t env_lookup(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot) {
            val_t v = frame_slot_load(f, slot);
            if (v == V_UNDEF)
                scm_raise(V_FALSE, "variable used before initialization: %s", sym_cstr(sym));
            return v;
        }
        f = f->parent;
    }
    scm_raise_code(EC_UNBOUND_VARIABLE, "unbound variable: %s", sym_cstr(sym));
}

val_t env_lookup_or_false(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot) {
            val_t v = frame_slot_load(f, slot);
            if (v != V_UNDEF) return v;
        }
        f = f->parent;
    }
    return V_FALSE;
}

val_t *env_lookup_slot(val_t env, val_t sym) {
    EnvFrame *f = as_env(env);
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot && frame_slot_load(f, slot) != V_UNDEF) return slot;
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
