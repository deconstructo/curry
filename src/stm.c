/*
 * Software Transactional Memory — TL2 algorithm.
 *
 * TL2 paper: Dice, Shalev, Shavit, "Transactional Locking II" (DISC 2006).
 *
 * Global version clock (g_vclock): atomic uint64, bumped on every commit.
 * Per-TVar: committed value, version-at-last-write, lock (held during commit
 *   only), changed condvar (broadcast after each write), and waiter count.
 * Per-thread transaction (TxState): read-set {(TVar*, recorded_ver)} and
 *   write-set {(TVar*, pending_val)}.
 *
 * Read inside transaction:
 *   1. Check write-set first (return pending value if present).
 *   2. Else: load tv->version (acquire), THEN tv->value; validate; record.
 *
 * Commit:
 *   1. Sort wset by TVar* address (deadlock prevention).
 *   2. Lock all write-set tvars.
 *   3. Bump g_vclock → write_ver.
 *   4. Validate rset: tv->version must equal recorded version.
 *   5. Apply writes: tv->value = new_val; tv->version = write_ver.
 *   6. Unlock all wset tvars; broadcast changed + global retry condvar.
 *
 * retry / or-else:
 *   stm_retry() longjmps to the retry_jmp set up inside stm_atomically().
 *   The outer loop resets the transaction state and calls tx_wait_for_change()
 *   before re-running the thunk.
 */

#include "stm.h"
#include "gc.h"
#include "eval.h"
#include "value.h"
#include "object.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdbool.h>

/* ---- Global version clock ---- */

static _Atomic uint64_t g_vclock = 0;

/* ---- Global retry notification ---- */
/* All commits broadcast here; retrying threads wait for any change. */
static pthread_mutex_t g_retry_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_retry_cv = PTHREAD_COND_INITIALIZER;

/* ---- Per-thread transaction state ---- */

#define TX_INIT_CAP 8

typedef struct { TVar *tv; uint64_t ver; } REntry;
typedef struct { TVar *tv; val_t    val; } WEntry;

typedef struct TxState {
    REntry  *rset;
    size_t   rlen, rcap;
    WEntry  *wset;
    size_t   wlen, wcap;
    uint64_t read_ver;     /* g_vclock snapshot at transaction start           */
    jmp_buf *retry_jmp;    /* longjmp target for stm_retry() inside atomically */
} TxState;

static CURRY_THREAD_LOCAL TxState *current_tx = NULL;

/* ---- TxState helpers ---- */

static TxState *tx_new(void) {
    TxState *tx = (TxState *)gc_alloc_raw_pinned(sizeof(TxState));
    tx->rset = gc_alloc_raw_pinned(TX_INIT_CAP * sizeof(REntry));
    tx->rcap = TX_INIT_CAP;
    tx->wset = gc_alloc_raw_pinned(TX_INIT_CAP * sizeof(WEntry));
    tx->wcap = TX_INIT_CAP;
    return tx;
}

static void tx_reset(TxState *tx) {
    tx->rlen     = 0;
    tx->wlen     = 0;
    tx->read_ver = atomic_load_explicit(&g_vclock, memory_order_acquire);
}

static void rset_add(TxState *tx, TVar *tv, uint64_t ver) {
    for (size_t i = 0; i < tx->rlen; i++)
        if (tx->rset[i].tv == tv) return;
    if (tx->rlen == tx->rcap) {
        size_t nc  = tx->rcap * 2;
        REntry *nb = gc_alloc_raw_pinned(nc * sizeof(REntry));
        memcpy(nb, tx->rset, tx->rlen * sizeof(REntry));
        tx->rset = nb;
        tx->rcap = nc;
    }
    tx->rset[tx->rlen++] = (REntry){tv, ver};
}

static val_t wset_lookup(TxState *tx, TVar *tv) {
    for (size_t i = 0; i < tx->wlen; i++)
        if (tx->wset[i].tv == tv) return tx->wset[i].val;
    return V_UNDEF;
}

static void wset_put(TxState *tx, TVar *tv, val_t val) {
    for (size_t i = 0; i < tx->wlen; i++) {
        if (tx->wset[i].tv == tv) { tx->wset[i].val = val; return; }
    }
    if (tx->wlen == tx->wcap) {
        size_t nc  = tx->wcap * 2;
        WEntry *nb = gc_alloc_raw_pinned(nc * sizeof(WEntry));
        memcpy(nb, tx->wset, tx->wlen * sizeof(WEntry));
        tx->wset = nb;
        tx->wcap = nc;
    }
    tx->wset[tx->wlen++] = (WEntry){tv, val};
}

/* ---- Commit ---- */

static int cmp_wentry(const void *a, const void *b) {
    const WEntry *wa = a, *wb = b;
    if ((uintptr_t)wa->tv < (uintptr_t)wb->tv) return -1;
    if ((uintptr_t)wa->tv > (uintptr_t)wb->tv) return  1;
    return 0;
}

/* Returns true if commit succeeded, false on read-set conflict. */
static bool tx_commit(TxState *tx) {
    qsort(tx->wset, tx->wlen, sizeof(WEntry), cmp_wentry);

    for (size_t i = 0; i < tx->wlen; i++)
        pthread_mutex_lock(&tx->wset[i].tv->lock);

    uint64_t write_ver =
        atomic_fetch_add_explicit(&g_vclock, 1, memory_order_acq_rel) + 1;

    bool valid = true;
    for (size_t i = 0; i < tx->rlen && valid; i++) {
        TVar    *tv  = tx->rset[i].tv;
        uint64_t cur = atomic_load_explicit(
            (_Atomic uint64_t *)&tv->version, memory_order_acquire);
        if (cur != tx->rset[i].ver) valid = false;
    }

    if (!valid) {
        for (size_t i = 0; i < tx->wlen; i++)
            pthread_mutex_unlock(&tx->wset[i].tv->lock);
        return false;
    }

    for (size_t i = 0; i < tx->wlen; i++) {
        TVar *tv  = tx->wset[i].tv;
        gc_wb_slot(&tv->value, tx->wset[i].val);
        atomic_store_explicit(
            (_Atomic uint64_t *)&tv->version, write_ver, memory_order_release);
    }

    for (size_t i = 0; i < tx->wlen; i++) {
        pthread_cond_broadcast(&tx->wset[i].tv->changed);
        pthread_mutex_unlock(&tx->wset[i].tv->lock);
    }

    pthread_mutex_lock(&g_retry_mx);
    pthread_cond_broadcast(&g_retry_cv);
    pthread_mutex_unlock(&g_retry_mx);

    return true;
}

/* ---- Retry wait ---- */

static void tx_wait_for_change(TxState *tx) {
    uint64_t snap_ver = tx->read_ver;

    for (size_t i = 0; i < tx->rlen; i++) {
        pthread_mutex_lock(&tx->rset[i].tv->lock);
        tx->rset[i].tv->n_waiters++;
        pthread_mutex_unlock(&tx->rset[i].tv->lock);
    }

    pthread_mutex_lock(&g_retry_mx);
    while (atomic_load_explicit(&g_vclock, memory_order_acquire) == snap_ver)
        pthread_cond_wait(&g_retry_cv, &g_retry_mx);
    pthread_mutex_unlock(&g_retry_mx);

    for (size_t i = 0; i < tx->rlen; i++) {
        pthread_mutex_lock(&tx->rset[i].tv->lock);
        tx->rset[i].tv->n_waiters--;
        pthread_mutex_unlock(&tx->rset[i].tv->lock);
    }
}

/* ---- Public API ---- */

void stm_init(void) {
    /* g_vclock and mutexes are statically initialised. */
}

val_t stm_make_tvar(val_t init) {
    TVar *tv = CURRY_NEW_PINNED(TVar);
    tv->hdr.type  = T_TVAR;
    tv->hdr.flags = 0;
    tv->value     = init;
    tv->version   = 0;
    tv->n_waiters = 0;
    pthread_mutex_init(&tv->lock,    NULL);
    pthread_cond_init(&tv->changed,  NULL);
    return vptr(tv);
}

val_t stm_tvar_read(val_t v) {
    if (!vis_tvar(v))
        scm_raise(V_FALSE, "tvar-read: not a tvar");
    TVar    *tv = as_tvar(v);
    TxState *tx = current_tx;

    if (!tx)
        return tv->value;

    val_t pending = wset_lookup(tx, tv);
    if (pending != V_UNDEF) return pending;

    /* Standard TL2 read protocol: version, then value, then re-read version
     * and compare — not just version-then-value. Without the post-read
     * recheck, a commit (tx_commit writes tv->value, THEN tv->version —
     * see there) landing in the window between our version read and our
     * value read is invisible to us: we'd read the pre-commit version but
     * the post-commit value, a torn (version, value) pair. Read-set
     * validation at our own commit time would eventually catch that this
     * tvar changed and abort us — but not before the transaction body
     * has already run on the inconsistent snapshot, which is exactly what
     * TL2's immediate per-read validation exists to prevent (a torn read
     * can drive control flow into states rset validation never gets a
     * chance to catch, e.g. an infinite loop or an out-of-bounds access
     * derived from combining an old and a new value). */
    uint64_t ver = atomic_load_explicit(
        (_Atomic uint64_t *)&tv->version, memory_order_acquire);

    if (ver > tx->read_ver)
        stm_retry();

    val_t val = tv->value;

    uint64_t ver2 = atomic_load_explicit(
        (_Atomic uint64_t *)&tv->version, memory_order_acquire);
    if (ver2 != ver)
        stm_retry();

    rset_add(tx, tv, ver);
    return val;
}

void stm_tvar_write(val_t v, val_t val) {
    if (!vis_tvar(v))
        scm_raise(V_FALSE, "tvar-write!: not a tvar");
    TVar    *tv = as_tvar(v);
    TxState *tx = current_tx;

    if (!tx) {
        pthread_mutex_lock(&tv->lock);
        gc_wb_slot(&tv->value, val);
        atomic_fetch_add_explicit(&g_vclock, 1, memory_order_acq_rel);
        uint64_t new_ver = atomic_load_explicit(&g_vclock, memory_order_acquire);
        atomic_store_explicit(
            (_Atomic uint64_t *)&tv->version, new_ver, memory_order_release);
        pthread_cond_broadcast(&tv->changed);
        pthread_mutex_unlock(&tv->lock);

        pthread_mutex_lock(&g_retry_mx);
        pthread_cond_broadcast(&g_retry_cv);
        pthread_mutex_unlock(&g_retry_mx);
        return;
    }

    wset_put(tx, tv, val);
}

#define RETRY_SIG 1

val_t stm_atomically(val_t thunk) {
    TxState *outer_tx = current_tx;

    if (outer_tx)
        return apply_arr(thunk, 0, NULL);

    TxState *tx = tx_new();
    current_tx  = tx;

    ExnHandler *saved_handler = current_handler;

    jmp_buf retry_jmp;
    tx->retry_jmp = &retry_jmp;

    val_t result = V_VOID;

    /* State to restore if stm_retry()'s longjmp lands here. retry_jmp is a
     * SEPARATE longjmp target from SCM_PROTECT's own h.jmp below, reached
     * directly from stm_tvar_read (or stm_or_else) deep inside whatever VM
     * call depth apply_arr(thunk, ...) is nested at when a conflicting
     * commit is detected — exactly like an uncaught exception unwinding
     * through vm_run(), but it bypasses SCM_PROTECT's own restore entirely
     * since it never touches h.jmp. Without saving/restoring the same
     * vm->sp/frame_count/open_upvalues (plus the shadow stack, GC-inhibit
     * count and JIT call depth) around it ourselves, each retry leaves
     * vm->sp/frame_count wherever the retrying read happened; the next
     * retry's apply_arr call then captures that already-drifted vm->sp as
     * its new baseline, leaking VM stack/frame slots on every single
     * retry. Under real contention (multiple actors transacting on one
     * tvar, as in tests/actors_tests.scm's STM section) retries are
     * frequent, and the leak corrupts vm->stack/vm->frames within a few
     * hundred retries: a later OP_CALL's PEEK() reads a stale/leftover
     * stack slot as the callee, invoking an unrelated live closure with
     * whatever argc happened to be encoded at that call site — surfacing
     * as a bogus "too many arguments" on some unrelated closure, or a
     * straight segfault in vm_run(), depending on what garbage lands
     * there. Confirmed via gdb/LD_PRELOAD SIGSEGV-trap reproduction: the
     * crash is always inside vm_run(), reached through apply_arr() called
     * from here, and disappears once retries restore this state exactly
     * as SCM_PROTECT already does for the exception path below. */
    void   *saved_shadow;
    int     saved_inhibit;
    int     saved_jit_depth;
    int     saved_vm_frame_count;
    void   *saved_vm_sp;
    void   *saved_vm_open_upvalues;

    for (;;) {
        tx_reset(tx);
        current_handler = saved_handler;

        saved_shadow    = gc_shadow_save();
        saved_inhibit   = gc_inhibit_save();
        saved_jit_depth = jit_depth_save();
        vm_exn_state_save(&saved_vm_frame_count, &saved_vm_sp, &saved_vm_open_upvalues);

        if (setjmp(retry_jmp) == RETRY_SIG) {
            gc_shadow_restore(saved_shadow);
            gc_inhibit_restore(saved_inhibit);
            jit_depth_restore(saved_jit_depth);
            vm_exn_state_restore(saved_vm_frame_count, saved_vm_sp, saved_vm_open_upvalues);
            tx_wait_for_change(tx);
            continue;
        }

        bool got_exn = false;
        ExnHandler h;
        SCM_PROTECT(h, {
            result = apply_arr(thunk, 0, NULL);
        }, {
            got_exn = true;
        });

        if (got_exn) {
            val_t exn   = h.exn;
            current_tx  = outer_tx;
            current_handler = saved_handler;
            scm_raise_val(exn);
        }

        if (tx_commit(tx)) {
            current_tx = outer_tx;
            return result;
        }
    }
}

val_t stm_or_else(val_t thunk1, val_t thunk2) {
    if (!current_tx)
        scm_raise(V_FALSE, "or-else called outside atomically");

    TxState *tx = current_tx;

    size_t   saved_rlen     = tx->rlen;
    size_t   saved_wlen     = tx->wlen;
    uint64_t saved_read_ver = tx->read_ver;
    jmp_buf *saved_retry    = tx->retry_jmp;

    jmp_buf local_jmp;
    tx->retry_jmp = &local_jmp;

    /* Same vm-state leak as stm_atomically's retry_jmp (see the comment
     * there): a stm_retry() inside thunk1/thunk2 longjmps straight to
     * local_jmp, skipping SCM_PROTECT's restore entirely, so it has to be
     * saved/restored here by hand around each attempt. */
    void *saved_shadow;
    int   saved_inhibit, saved_jit_depth;
    int   saved_vm_frame_count;
    void *saved_vm_sp, *saved_vm_open_upvalues;

    saved_shadow    = gc_shadow_save();
    saved_inhibit   = gc_inhibit_save();
    saved_jit_depth = jit_depth_save();
    vm_exn_state_save(&saved_vm_frame_count, &saved_vm_sp, &saved_vm_open_upvalues);

    if (setjmp(local_jmp) == 0) {
        val_t r1      = apply_arr(thunk1, 0, NULL);
        tx->retry_jmp = saved_retry;
        return r1;
    }
    gc_shadow_restore(saved_shadow);
    gc_inhibit_restore(saved_inhibit);
    jit_depth_restore(saved_jit_depth);
    vm_exn_state_restore(saved_vm_frame_count, saved_vm_sp, saved_vm_open_upvalues);

    tx->rlen     = saved_rlen;
    tx->wlen     = saved_wlen;
    tx->read_ver = saved_read_ver;
    tx->retry_jmp = &local_jmp;

    saved_shadow    = gc_shadow_save();
    saved_inhibit   = gc_inhibit_save();
    saved_jit_depth = jit_depth_save();
    vm_exn_state_save(&saved_vm_frame_count, &saved_vm_sp, &saved_vm_open_upvalues);

    if (setjmp(local_jmp) == 0) {
        val_t r2      = apply_arr(thunk2, 0, NULL);
        tx->retry_jmp = saved_retry;
        return r2;
    }
    gc_shadow_restore(saved_shadow);
    gc_inhibit_restore(saved_inhibit);
    jit_depth_restore(saved_jit_depth);
    vm_exn_state_restore(saved_vm_frame_count, saved_vm_sp, saved_vm_open_upvalues);

    tx->rlen     = saved_rlen;
    tx->wlen     = saved_wlen;
    tx->read_ver = saved_read_ver;
    tx->retry_jmp = saved_retry;
    longjmp(*saved_retry, RETRY_SIG);
}

void stm_retry(void) {
    if (!current_tx)
        scm_raise(V_FALSE, "retry called outside atomically");
    longjmp(*current_tx->retry_jmp, RETRY_SIG);
}
