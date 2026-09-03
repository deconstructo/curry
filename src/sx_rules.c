#include "sx_rules.h"
#include "sx_pattern.h"
#include "symbol.h"
#include "object.h"
#include "gc.h"
#include "builtins.h"  /* scm_cons */
#include "eval.h"      /* apply_arr */
#include "symbolic.h"  /* sx_invalidate_simplify_cache */
#include <pthread.h>

/* ---- Rule struct ---- */

typedef struct SxRule {
    val_t pattern;    /* quoted Scheme list/tree */
    val_t pvars;      /* ordered list of ?-symbols (lambda param order) */
    val_t guard_fn;   /* V_FALSE or callable */
    val_t action_fn;  /* callable */
    val_t ruleset;    /* name symbol or V_FALSE */
    struct SxRule *next;
} SxRule;

/* ---- Hash table: operator symbol → SxRule* ---- */

#define RTAB_SIZE 128

typedef struct {
    val_t    op;    /* V_VOID = empty slot */
    SxRule  *head;
} RuleSlot;

static RuleSlot rtab[RTAB_SIZE];
static int rtab_initialised = 0;

/* Issue #141: curry actors are real OS threads with no global interpreter
 * lock, and define-rule/clear-rules! (writers, via sx_rule_add/
 * sx_rules_clear) can run concurrently with simplify (the reader, via
 * sx_rule_try) on a different actor. Before this lock, sx_rule_add's
 * append (an unsynchronized "walk to tail, then write ->next") could lose
 * a concurrent registration, and sx_rule_try's traversal could race with
 * sx_rules_clear's unlinking. A single-writer/many-reader rwlock is
 * enough: mutation is rare (explicit define-rule/clear-rules! calls) next
 * to lookups (every sx_simplify call on a user-defined operator).
 *
 * sx_rule_try releases this lock BEFORE invoking any rule's guard_fn/
 * action_fn (arbitrary Scheme callables) -- see the snapshot-then-release
 * pattern there. A guard or action is free to call define-rule itself
 * (recursively re-entering this file), and pthread_rwlock_t is not
 * recursive: a thread that still held the read lock while attempting the
 * write lock inside sx_rule_add would deadlock against itself on most
 * implementations' writer-preference. */
static pthread_rwlock_t rtab_lock = PTHREAD_RWLOCK_INITIALIZER;

void sx_rules_init(void) {
    for (int i = 0; i < RTAB_SIZE; i++) rtab[i].op = V_VOID;
    rtab_initialised = 1;
    gc_ss_register_ext_scanner(sx_rules_gc_scan);
}

/* Hash by interned symbol pointer (all symbols are unique pointers) */
static int slot_for(val_t op) {
    unsigned h = (unsigned)((uintptr_t)op >> 3);
    return (int)(h % (unsigned)RTAB_SIZE);
}

static SxRule **find_chain(val_t op) {
    int start = slot_for(op);
    for (int i = 0; i < RTAB_SIZE; i++) {
        int idx = (start + i) % RTAB_SIZE;
        if (rtab[idx].op == op)  return &rtab[idx].head;
        if (rtab[idx].op == V_VOID) {
            rtab[idx].op   = op;
            rtab[idx].head = NULL;
            return &rtab[idx].head;
        }
    }
    return NULL; /* table full — caller must handle */
}

/* ---- Public API ---- */

void sx_rule_add(val_t pattern, val_t pvars,
                 val_t guard_fn, val_t action_fn, val_t ruleset) {
    if (!rtab_initialised) return;

    /* Determine the operator: head of the list pattern */
    if (!vis_pair(pattern)) return; /* degenerate pattern — ignore */
    val_t op = vcar(pattern);
    if (!vis_symbol(op)) return;

    /* Allocate before taking the lock: GC_NEW never needs rtab_lock, and
     * keeping allocation out of the critical section keeps it short. */
    SxRule *r = GC_NEW(SxRule);
    r->pattern   = pattern;
    r->pvars     = pvars;
    r->guard_fn  = guard_fn;
    r->action_fn = action_fn;
    r->ruleset   = ruleset;
    r->next      = NULL;

    pthread_rwlock_wrlock(&rtab_lock);
    SxRule **chain = find_chain(op);
    if (!chain) { pthread_rwlock_unlock(&rtab_lock); return; } /* table full */

    /* Append to end so rules fire in definition order */
    if (*chain == NULL) {
        *chain = r;
    } else {
        SxRule *tail = *chain;
        while (tail->next) tail = tail->next;
        tail->next = r;
    }
    pthread_rwlock_unlock(&rtab_lock);

    /* Issue #137: a node sx_simplify already cached as "fully
     * simplified" before this rule existed must not keep being served
     * stale from that cache now that a new rule could change what
     * simplifying its operator actually does. */
    sx_invalidate_simplify_cache();
}

/* Fields sx_rule_try actually needs, copied out from under rtab_lock
 * before any guard_fn/action_fn callback runs (see rtab_lock's comment
 * above for why the lock cannot still be held at that point). */
typedef struct {
    val_t pattern, pvars, guard_fn, action_fn;
} SxRuleSnap;

val_t sx_rule_try(val_t expr) {
    if (!rtab_initialised || !vis_symexpr(expr)) return V_VOID;

    SymExpr *se  = as_symexpr(expr);
    val_t    op  = se->op;

    /* Same count/allocate-outside-lock/copy pattern as sx_rules_list,
     * for the same reason: sx_rule_try is called on every sx_simplify
     * for a user-defined operator, so it holds rtab_lock (for reading)
     * during the count+copy passes below. Under --gc generational, a
     * minor collection is per-thread, not stop-the-world, and
     * sx_rules_gc_scan (which mutates rtab) takes rtab_lock for
     * WRITING -- so a GC_MALLOC while this thread still held the read
     * lock would self-deadlock trying to promote that read lock's
     * owner into the scanner's write-lock wait. An earlier version of
     * this function used a fixed 256-entry on-stack array instead
     * (avoiding allocation entirely), but that silently and
     * permanently dropped every rule past the 256th registered for one
     * operator from ever firing again -- a real correctness regression
     * found by independent security review (issue #145), not just a
     * theoretical one: reachable via ordinary define-ruleset use
     * accumulating rules for a common operator over a program's
     * lifetime, with no diagnostic. The count+headroom approach here
     * only truncates if the operator's rule count grows by more than
     * 25%+8 in the microseconds between the two lock sections, which
     * is what sx_rules_list already accepts as a best-effort snapshot
     * bound rather than a hard, easily-hit cap. */
    pthread_rwlock_rdlock(&rtab_lock);
    int start = slot_for(op);
    SxRule *chain = NULL;
    for (int i = 0; i < RTAB_SIZE; i++) {
        int idx = (start + i) % RTAB_SIZE;
        if (rtab[idx].op == op)   { chain = rtab[idx].head; break; }
        if (rtab[idx].op == V_VOID) break;
    }
    int count = 0;
    for (SxRule *r = chain; r; r = r->next) count++;
    pthread_rwlock_unlock(&rtab_lock);

    if (count == 0) return V_VOID;

    int cap = count + count / 4 + 8;
    SxRuleSnap *snap = (SxRuleSnap *)GC_MALLOC(sizeof(SxRuleSnap) * (size_t)cap);

    pthread_rwlock_rdlock(&rtab_lock);
    start = slot_for(op);
    chain = NULL;
    for (int i = 0; i < RTAB_SIZE; i++) {
        int idx = (start + i) % RTAB_SIZE;
        if (rtab[idx].op == op)   { chain = rtab[idx].head; break; }
        if (rtab[idx].op == V_VOID) break;
    }
    int n = 0;
    for (SxRule *r = chain; r && n < cap; r = r->next, n++) {
        snap[n].pattern   = r->pattern;
        snap[n].pvars     = r->pvars;
        snap[n].guard_fn  = r->guard_fn;
        snap[n].action_fn = r->action_fn;
    }
    pthread_rwlock_unlock(&rtab_lock);

    for (int idx = 0; idx < n; idx++) {
        val_t pattern   = snap[idx].pattern;
        val_t pvars     = snap[idx].pvars;
        val_t guard_fn  = snap[idx].guard_fn;
        val_t action_fn = snap[idx].action_fn;

        val_t bindings = sx_pattern_match(pattern, expr);
        if (bindings == V_FALSE) continue;

        /* Build argv in pvars order */
        int nparams = 0;
        val_t pv = pvars;
        while (vis_pair(pv)) { nparams++; pv = vcdr(pv); }

        val_t argv[64];
        if (nparams > 64) continue; /* safety guard */

        pv = pvars;
        for (int i = 0; i < nparams; i++) {
            val_t sym = vcar(pv);
            /* Look up in bindings alist */
            val_t found = V_VOID;
            val_t scan  = bindings;
            while (vis_pair(scan)) {
                val_t kv = vcar(scan);
                if (vcar(kv) == sym) { found = vcdr(kv); break; }
                scan = vcdr(scan);
            }
            argv[i] = (found != V_VOID) ? found : V_FALSE;
            pv = vcdr(pv);
        }

        /* Check guard */
        if (guard_fn != V_FALSE) {
            val_t ok = apply_arr(guard_fn, nparams, argv);
            if (vis_false(ok)) continue;
        }

        /* Fire action */
        return apply_arr(action_fn, nparams, argv);
    }

    return V_VOID;
}

/* Fields sx_rules_list needs, copied out from under rtab_lock. */
typedef struct { val_t pattern, ruleset; } SxRuleDesc;

val_t sx_rules_list(val_t op_filter) {
    /* Unlike sx_rule_try, scm_cons never calls back into Scheme, so the
     * usual reentrancy hazard doesn't apply here -- but scm_cons DOES
     * allocate, and under --gc generational that can trigger a minor GC
     * on this same thread, which runs sx_rules_gc_scan, which (below)
     * takes rtab_lock for writing. pthread_rwlock_t is not recursive, so
     * calling scm_cons while still holding rtab_lock (even for reading)
     * would risk this exact thread self-deadlocking against itself the
     * moment a collection actually triggers. Snapshot-then-release, same
     * pattern as sx_rule_try, sidesteps it: count under the lock (no
     * allocation), copy under the lock into an already-allocated buffer
     * (no allocation), then build the result list with scm_cons entirely
     * outside the lock. */
    pthread_rwlock_rdlock(&rtab_lock);
    int count = 0;
    for (int i = 0; i < RTAB_SIZE; i++) {
        if (rtab[i].op == V_VOID) continue;
        if (op_filter != V_FALSE && rtab[i].op != op_filter) continue;
        for (SxRule *r = rtab[i].head; r; r = r->next) count++;
    }
    pthread_rwlock_unlock(&rtab_lock);

    /* Allocate outside the lock, with headroom in case concurrent
     * registrations grow the table between the count pass and the copy
     * pass below -- a copy that hits this cap simply stops early rather
     * than overflowing, since list-rules is inherently a best-effort
     * snapshot of a table other actors may be mutating concurrently. */
    int cap = count + count / 4 + 8;
    SxRuleDesc *buf = (SxRuleDesc *)GC_MALLOC(sizeof(SxRuleDesc) * (size_t)cap);

    pthread_rwlock_rdlock(&rtab_lock);
    int n = 0;
    for (int i = 0; i < RTAB_SIZE && n < cap; i++) {
        if (rtab[i].op == V_VOID) continue;
        if (op_filter != V_FALSE && rtab[i].op != op_filter) continue;
        for (SxRule *r = rtab[i].head; r && n < cap; r = r->next, n++) {
            buf[n].pattern = r->pattern;
            buf[n].ruleset = r->ruleset;
        }
    }
    pthread_rwlock_unlock(&rtab_lock);

    val_t result = V_NIL;
    for (int i = 0; i < n; i++) {
        /* Descriptor: (pattern ruleset) -- consing forward over the
         * snapshot reproduces the original single-pass loop's ordering
         * (which built the list by prepending in visit order). */
        val_t desc = scm_cons(buf[i].pattern, scm_cons(buf[i].ruleset, V_NIL));
        result = scm_cons(desc, result);
    }
    return result;
}

void sx_rules_clear(val_t ruleset) {
    /* Issue #137 follow-up (found by independent code review): removing
     * a rule can change what simplifying its operator does (a node
     * cached as "simplified" under the removed rule's rewrite may no
     * longer be a fixpoint once the rule is gone), so this needs the
     * same invalidation sx_rule_add already does for the opposite
     * (adding) direction. */
    sx_invalidate_simplify_cache();
    pthread_rwlock_wrlock(&rtab_lock);
    for (int i = 0; i < RTAB_SIZE; i++) {
        if (rtab[i].op == V_VOID) continue;
        if (ruleset == V_FALSE) {
            rtab[i].head = NULL;
        } else {
            /* Remove rules belonging to this ruleset */
            SxRule **prev = &rtab[i].head;
            SxRule  *cur  = rtab[i].head;
            while (cur) {
                if (cur->ruleset == ruleset) {
                    *prev = cur->next;
                    cur   = *prev;
                } else {
                    prev = &cur->next;
                    cur  = cur->next;
                }
            }
        }
    }
    pthread_rwlock_unlock(&rtab_lock);
}

void sx_rules_gc_scan(void) {
    /* Issue #141 follow-up (found by independent security review): under
     * --gc generational, a minor collection is per-thread, NOT a
     * stop-the-world pause for other actors (see gc_gen.c) -- so this
     * scanner runs concurrently with every other actor's rtab_lock-
     * protected access, and was mutating rtab's fields completely
     * unguarded. Takes the same write lock sx_rule_add/sx_rules_clear
     * use. Safe against self-deadlock: gc_ss_evac/gc_ss_fwd (identity
     * functions under Boehm, real evacuation under generational) do not
     * allocate, and every OTHER place in this file that takes rtab_lock
     * is now structured to never allocate while holding it either (see
     * sx_rule_add's pre-lock GC_NEW and sx_rules_list's snapshot-then-
     * release pattern) -- so no thread can already be holding rtab_lock
     * at the moment its own allocation triggers the minor collection
     * that calls this scanner. */
    pthread_rwlock_wrlock(&rtab_lock);
    for (int i = 0; i < RTAB_SIZE; i++) {
        if (rtab[i].op == V_VOID) continue;
        rtab[i].op   = (val_t)gc_ss_evac((uintptr_t)rtab[i].op);
        rtab[i].head = (SxRule *)gc_ss_fwd(rtab[i].head);
        for (SxRule *r = rtab[i].head; r; r = r->next) {
            r->pattern   = (val_t)gc_ss_evac((uintptr_t)r->pattern);
            r->pvars     = (val_t)gc_ss_evac((uintptr_t)r->pvars);
            r->guard_fn  = (val_t)gc_ss_evac((uintptr_t)r->guard_fn);
            r->action_fn = (val_t)gc_ss_evac((uintptr_t)r->action_fn);
            r->ruleset   = (val_t)gc_ss_evac((uintptr_t)r->ruleset);
            r->next      = (SxRule *)gc_ss_fwd(r->next);
        }
    }
    pthread_rwlock_unlock(&rtab_lock);
}
