#include "sx_algebra.h"
#include "symbol.h"
#include "object.h"
#include "gc.h"
#include "gc_semispace.h"
#include "symbolic.h"   /* SYM_ASSUME_* flags */
#include <string.h>

/* ---- Algebra table ---- */

#define ATAB_SIZE 64

static AlgebraInfo atab[ATAB_SIZE];
static int atab_initialised = 0;

void sx_algebra_init(void) {
    memset(atab, 0, sizeof(atab));
    for (int i = 0; i < ATAB_SIZE; i++) atab[i].op = V_VOID;
    atab_initialised = 1;
    gc_ss_register_ext_scanner(sx_algebra_gc_scan);
}

static int atab_slot(val_t op) {
    return (int)(((uintptr_t)op >> 3) % (unsigned)ATAB_SIZE);
}

AlgebraInfo *sx_algebra_lookup(val_t op) {
    if (!atab_initialised) return NULL;
    int start = atab_slot(op);
    for (int i = 0; i < ATAB_SIZE; i++) {
        int idx = (start + i) % ATAB_SIZE;
        if (atab[idx].op == op)    return &atab[idx];
        if (atab[idx].op == V_VOID) return NULL;
    }
    return NULL;
}

void sx_algebra_define(val_t op, bool commutative, bool associative,
                       val_t identity, val_t absorbing, val_t relations_fn) {
    if (!atab_initialised) return;
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
            return;
        }
    }
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
