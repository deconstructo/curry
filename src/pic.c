#include "pic.h"
#include "object.h"
#include "gc.h"
#include "builtins.h"

/*
 * See pic.h for the cache layout and the design rationale (why this is a
 * pair of ordinary builtins rather than a bytecode opcode or compiler.c
 * change, and why the cache is owned by the generic function rather than
 * living at the call site).
 */

/* (%%pic-lookup pic class-tuple generation) -> cached chain, or #f on a
 * miss (no matching slot, or a matching slot whose stamped generation is
 * stale — the method table changed since that entry was cached).
 *
 * Fast path: PIC_N (small, fixed) iterations, each a handful of val_t
 * equality compares — direct `==` on val_t is exactly eq? at this
 * representation (see scm_eq, src/set.c), which is the correct comparison
 * here since classes are singletons (one %class object per define-class,
 * see oop.scm) and generation is a plain fixnum. No Scheme-level
 * filter/every/is-a? machinery runs on a hit. */
static val_t prim_pic_lookup(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t pic_v = av[0], tuple_v = av[1], gen_v = av[2];
    if (!vis_vector(pic_v) || !vis_vector(tuple_v)) return V_FALSE;

    Vector *pic = as_vec(pic_v);
    Vector *tuple = as_vec(tuple_v);
    if (pic->len != PIC_VECTOR_LEN) return V_FALSE;   /* malformed/foreign vector */

    for (int i = 0; i < PIC_N; i++) {
        int base = 1 + 3 * i;
        val_t slot_tuple = pic->data[base];
        if (!vis_vector(slot_tuple)) continue;         /* empty slot */
        if (pic->data[base + 1] != gen_v) continue;    /* stale generation */

        Vector *st = as_vec(slot_tuple);
        if (st->len != tuple->len) continue;
        bool match = true;
        for (uint32_t j = 0; j < tuple->len; j++) {
            if (st->data[j] != tuple->data[j]) { match = false; break; }
        }
        if (match) return pic->data[base + 2];
    }
    return V_FALSE;
}

/* (%%pic-store! pic class-tuple generation chain) -> unspecified.
 * Writes into the next round-robin slot (slot 0 holds the write cursor).
 * No search for an existing matching entry to update in place — at PIC_N=4
 * a stale duplicate simply ages out within 4 more misses/stores, which is
 * simpler and cheap enough not to be worth the extra comparison pass. */
static val_t prim_pic_store(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    val_t pic_v = av[0], tuple_v = av[1], gen_v = av[2], chain_v = av[3];
    if (!vis_vector(pic_v)) return V_VOID;

    Vector *pic = as_vec(pic_v);
    if (pic->len != PIC_VECTOR_LEN) return V_VOID;

    intptr_t cursor = vis_fixnum(pic->data[0]) ? vunfix(pic->data[0]) : 0;
    if (cursor < 0 || cursor >= PIC_N) cursor = 0;

    int base = 1 + 3 * (int)cursor;
    gc_wb_slot(&pic->data[base],     tuple_v);
    gc_wb_slot(&pic->data[base + 1], gen_v);
    gc_wb_slot(&pic->data[base + 2], chain_v);
    gc_wb_slot(&pic->data[0], vfix((cursor + 1) % PIC_N));
    return V_VOID;
}

void pic_register_builtins(val_t env) {
    defprim(env, "%%pic-lookup", prim_pic_lookup, 3, 3);
    defprim(env, "%%pic-store!", prim_pic_store,  4, 4);
}
