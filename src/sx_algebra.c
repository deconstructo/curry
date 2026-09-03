#include "sx_algebra.h"
#include "symbol.h"
#include "object.h"
#include "gc.h"
#include "gc_semispace.h"
#include "symbolic.h"   /* SYM_ASSUME_* flags */
#include <string.h>
#include <pthread.h>

/* ---- Algebra table ---- */

#define ATAB_SIZE 64

static AlgebraInfo atab[ATAB_SIZE];
static int atab_initialised = 0;

/* Issue #141: same reasoning as sx_rules.c's rtab_lock -- define-algebra
 * (writer, via sx_algebra_define) can run concurrently with simplify
 * (reader, via sx_algebra_lookup) on a different actor thread, and
 * AlgebraInfo's five fields were being written one at a time with no
 * barrier, so a concurrent reader could see a torn combination of old
 * and new fields (e.g. a new `commutative` paired with a stale
 * `relations_fn`). sx_algebra_lookup now copies a whole-struct snapshot
 * out under the read lock (see its own comment) instead of handing back
 * a raw pointer into the table, so the caller's later reads of that
 * snapshot's fields -- including invoking relations_fn, an arbitrary
 * Scheme callable -- happen outside the lock and against a single
 * consistent copy, not a live, potentially-mid-write table slot. */
static pthread_rwlock_t atab_lock = PTHREAD_RWLOCK_INITIALIZER;

void sx_algebra_init(void) {
    memset(atab, 0, sizeof(atab));
    for (int i = 0; i < ATAB_SIZE; i++) atab[i].op = V_VOID;
    atab_initialised = 1;
    gc_ss_register_ext_scanner(sx_algebra_gc_scan);
}

static int atab_slot(val_t op) {
    return (int)(((uintptr_t)op >> 3) % (unsigned)ATAB_SIZE);
}

bool sx_algebra_lookup(val_t op, AlgebraInfo *out) {
    if (!atab_initialised) return false;
    pthread_rwlock_rdlock(&atab_lock);
    int start = atab_slot(op);
    bool found = false;
    for (int i = 0; i < ATAB_SIZE; i++) {
        int idx = (start + i) % ATAB_SIZE;
        if (atab[idx].op == op) { *out = atab[idx]; found = true; break; }
        if (atab[idx].op == V_VOID) break;
    }
    pthread_rwlock_unlock(&atab_lock);
    return found;
}

void sx_algebra_define(val_t op, bool commutative, bool associative,
                       val_t identity, val_t absorbing, val_t relations_fn) {
    if (!atab_initialised) return;
    pthread_rwlock_wrlock(&atab_lock);
    int start = atab_slot(op);
    for (int i = 0; i < ATAB_SIZE; i++) {
        int idx = (start + i) % ATAB_SIZE;
        if (atab[idx].op == op || atab[idx].op == V_VOID) {
            atab[idx].op           = op;
            atab[idx].commutative  = commutative;
            atab[idx].associative  = associative;
            atab[idx].identity     = identity;
            atab[idx].absorbing    = absorbing;
            atab[idx].relations_fn = relations_fn;
            pthread_rwlock_unlock(&atab_lock);
            /* Issue #137: same reasoning as sx_rule_add's identical
             * call -- a node cached as "fully simplified" before this
             * operator's algebra properties were (re-)defined must not
             * keep being served stale now that they've changed. */
            sx_invalidate_simplify_cache();
            return;
        }
    }
    pthread_rwlock_unlock(&atab_lock);
    /* Table full — silently ignore (extremely unlikely with 64 slots) */
}

void sx_algebra_gc_scan(void) {
    for (int i = 0; i < ATAB_SIZE; i++) {
        if (atab[i].op == V_VOID) continue;
        atab[i].op           = (val_t)gc_ss_evac((uintptr_t)atab[i].op);
        atab[i].identity     = (val_t)gc_ss_evac((uintptr_t)atab[i].identity);
        atab[i].absorbing    = (val_t)gc_ss_evac((uintptr_t)atab[i].absorbing);
        atab[i].relations_fn = (val_t)gc_ss_evac((uintptr_t)atab[i].relations_fn);
    }
}

/* ---- Assumption keyword → flag ---- */

uint32_t sx_assumption_flag(val_t sym) {
    if (!vis_symbol(sym)) return 0;
    const char *s = sym_cstr(sym);
    if (!strcmp(s, "positive"))   return SYM_ASSUME_POSITIVE;
    if (!strcmp(s, "negative"))   return SYM_ASSUME_NEGATIVE;
    if (!strcmp(s, "real"))       return SYM_ASSUME_REAL;
    if (!strcmp(s, "integer"))    return SYM_ASSUME_INTEGER;
    if (!strcmp(s, "nonzero"))    return SYM_ASSUME_NONZERO;
    if (!strcmp(s, "quaternion")) return SYM_ASSUME_QUATERNION;
    return 0;
}
