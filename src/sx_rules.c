#include "sx_rules.h"
#include "sx_pattern.h"
#include "symbol.h"
#include "object.h"
#include "gc.h"
#include "builtins.h"  /* scm_cons */
#include "eval.h"      /* apply_arr */
#include "symbolic.h"  /* sx_invalidate_simplify_cache */

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

    SxRule **chain = find_chain(op);
    if (!chain) return; /* table full */

    SxRule *r = GC_NEW(SxRule);
    r->pattern   = pattern;
    r->pvars     = pvars;
    r->guard_fn  = guard_fn;
    r->action_fn = action_fn;
    r->ruleset   = ruleset;
    r->next      = NULL;

    /* Append to end so rules fire in definition order */
    if (*chain == NULL) {
        *chain = r;
    } else {
        SxRule *tail = *chain;
        while (tail->next) tail = tail->next;
        tail->next = r;
    }

    /* Issue #137: a node sx_simplify already cached as "fully
     * simplified" before this rule existed must not keep being served
     * stale from that cache now that a new rule could change what
     * simplifying its operator actually does. */
    sx_invalidate_simplify_cache();
}

val_t sx_rule_try(val_t expr) {
    if (!rtab_initialised || !vis_symexpr(expr)) return V_VOID;

    SymExpr *se  = as_symexpr(expr);
    val_t    op  = se->op;

    int start = slot_for(op);
    SxRule *chain = NULL;
    for (int i = 0; i < RTAB_SIZE; i++) {
        int idx = (start + i) % RTAB_SIZE;
        if (rtab[idx].op == op)   { chain = rtab[idx].head; break; }
        if (rtab[idx].op == V_VOID) break;
    }
    if (!chain) return V_VOID;

    for (SxRule *r = chain; r; r = r->next) {
        val_t bindings = sx_pattern_match(r->pattern, expr);
        if (bindings == V_FALSE) continue;

        /* Build argv in pvars order */
        int nparams = 0;
        val_t pv = r->pvars;
        while (vis_pair(pv)) { nparams++; pv = vcdr(pv); }

        val_t argv[64];
        if (nparams > 64) continue; /* safety guard */

        pv = r->pvars;
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
        if (r->guard_fn != V_FALSE) {
            val_t ok = apply_arr(r->guard_fn, nparams, argv);
            if (vis_false(ok)) continue;
        }

        /* Fire action */
        return apply_arr(r->action_fn, nparams, argv);
    }

    return V_VOID;
}

val_t sx_rules_list(val_t op_filter) {
    val_t result = V_NIL;
    for (int i = 0; i < RTAB_SIZE; i++) {
        if (rtab[i].op == V_VOID) continue;
        if (op_filter != V_FALSE && rtab[i].op != op_filter) continue;
        for (SxRule *r = rtab[i].head; r; r = r->next) {
            /* Descriptor: (pattern ruleset) */
            val_t desc = scm_cons(r->pattern, scm_cons(r->ruleset, V_NIL));
            result = scm_cons(desc, result);
        }
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
}

void sx_rules_gc_scan(void) {
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
}
