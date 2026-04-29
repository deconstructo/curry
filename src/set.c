#include "set.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include <string.h>
#include <assert.h>

/* ---- Equality predicates ---- */

bool scm_eq(val_t a, val_t b) {
    return a == b;
}

bool scm_eqv(val_t a, val_t b) {
    if (a == b) return true;
    /* Same-type numeric comparisons */
    if (vis_fixnum(a) && vis_fixnum(b)) return vunfix(a) == vunfix(b);
    if (vis_flonum(a) && vis_flonum(b)) return vfloat(a) == vfloat(b);
    if (vis_bignum(a) && vis_bignum(b)) return mpz_cmp(as_big(a)->z, as_big(b)->z) == 0;
    if (vis_rational(a) && vis_rational(b)) return mpq_equal(as_rat(a)->q, as_rat(b)->q);
    if (vis_char(a) && vis_char(b)) return vunchr(a) == vunchr(b);
    if (vis_symbol(a) && vis_symbol(b)) return a == b; /* interned, so pointer eq */
    return false;
}

bool scm_equal(val_t a, val_t b) {
    if (scm_eqv(a, b)) return true;
    if (vis_pair(a) && vis_pair(b))
        return scm_equal(vcar(a), vcar(b)) && scm_equal(vcdr(a), vcdr(b));
    if (vis_string(a) && vis_string(b)) {
        String *sa = as_str(a), *sb = as_str(b);
        return sa->len == sb->len && memcmp(sa->data, sb->data, sa->len) == 0;
    }
    if (vis_vector(a) && vis_vector(b)) {
        Vector *va = as_vec(a), *vb = as_vec(b);
        if (va->len != vb->len) return false;
        for (uint32_t i = 0; i < va->len; i++)
            if (!scm_equal(va->data[i], vb->data[i])) return false;
        return true;
    }
    if (vis_bytes(a) && vis_bytes(b)) {
        Bytevector *ba = as_bytes(a), *bb = as_bytes(b);
        return ba->len == bb->len && memcmp(ba->data, bb->data, ba->len) == 0;
    }
    return false;
}

/* ---- Hashing ---- */

static uint32_t hash_u64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return (uint32_t)(x ^ (x >> 31));
}

uint32_t val_hash(val_t v, int cmp_type) {
    if (cmp_type == SET_CMP_EQ || vis_imm(v) || vis_fixnum(v) || vis_char(v))
        return hash_u64((uint64_t)v);
    if (vis_flonum(v)) {
        uint64_t bits; memcpy(&bits, &as_flo(v)->value, 8);
        return hash_u64(bits);
    }
    if (vis_string(v)) {
        /* FNV-1a over bytes */
        String *s = as_str(v);
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < s->len; i++) { h ^= (uint8_t)s->data[i]; h *= 16777619u; }
        return h;
    }
    if (vis_symbol(v)) return as_sym(v)->hash;
    if (vis_pair(v))   return hash_u64((uint64_t)v); /* structural hash too expensive */
    return hash_u64((uint64_t)v);
}

/* ---- Open-addressing hash set ---- */

#define EMPTY_SLOT  V_UNDEF
#define DEAD_SLOT   V_EOF   /* tombstone */
#define LOAD_NUM    3
#define LOAD_DEN    4       /* max 75% load */

static bool slot_matches(val_t a, val_t b, int cmp) {
    if (cmp == SET_CMP_EQ)    return scm_eq(a, b);
    if (cmp == SET_CMP_EQV)   return scm_eqv(a, b);
    return scm_equal(a, b);
}

static void set_rehash(Set *s) {
    uint32_t new_cap = s->cap ? s->cap * 2 : 16;
    val_t *new_bkt   = (val_t *)gc_alloc(new_cap * sizeof(val_t));
    for (uint32_t i = 0; i < new_cap; i++) new_bkt[i] = EMPTY_SLOT;
    uint32_t mask = new_cap - 1;
    for (uint32_t i = 0; i < s->cap; i++) {
        val_t e = s->buckets[i];
        if (e == EMPTY_SLOT || e == DEAD_SLOT) continue;
        uint32_t idx = val_hash(e, s->cmp) & mask;
        while (new_bkt[idx] != EMPTY_SLOT) idx = (idx + 1) & mask;
        new_bkt[idx] = e;
    }
    s->buckets = new_bkt;
    s->cap = new_cap;
}

val_t set_make(int cmp_type) {
    Set *s = CURRY_NEW(Set);
    s->hdr.type=T_SET; s->hdr.flags=0;
    s->size=0; s->cap=0; s->buckets=NULL; s->cmp=cmp_type;
    set_rehash(s);
    return vptr(s);
}

bool set_member(val_t sv, val_t elem) {
    Set *s = as_set(sv);
    uint32_t mask = s->cap - 1;
    uint32_t idx  = val_hash(elem, s->cmp) & mask;
    while (1) {
        val_t e = s->buckets[idx];
        if (e == EMPTY_SLOT) return false;
        if (e != DEAD_SLOT && slot_matches(e, elem, s->cmp)) return true;
        idx = (idx + 1) & mask;
    }
}

void set_add_mut(val_t sv, val_t elem) {
    Set *s = as_set(sv);
    if (set_member(sv, elem)) return;
    if (s->size * LOAD_DEN >= s->cap * LOAD_NUM) set_rehash(s);
    uint32_t mask = s->cap - 1;
    uint32_t idx  = val_hash(elem, s->cmp) & mask;
    while (s->buckets[idx] != EMPTY_SLOT && s->buckets[idx] != DEAD_SLOT)
        idx = (idx + 1) & mask;
    s->buckets[idx] = elem;
    s->size++;
}

void set_delete_mut(val_t sv, val_t elem) {
    Set *s = as_set(sv);
    uint32_t mask = s->cap - 1;
    uint32_t idx  = val_hash(elem, s->cmp) & mask;
    while (1) {
        val_t e = s->buckets[idx];
        if (e == EMPTY_SLOT) return;
        if (e != DEAD_SLOT && slot_matches(e, elem, s->cmp)) {
            s->buckets[idx] = DEAD_SLOT;
            s->size--;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

val_t set_add(val_t sv, val_t elem) {
    /* Copy and add */
    Set *old = as_set(sv);
    val_t nv = set_make(old->cmp);
    Set *s = as_set(nv);
    for (uint32_t i = 0; i < old->cap; i++) {
        val_t e = old->buckets[i];
        if (e != EMPTY_SLOT && e != DEAD_SLOT) set_add_mut(nv, e);
    }
    set_add_mut(nv, elem);
    (void)s;
    return nv;
}

val_t set_to_list(val_t sv) {
    Set *s = as_set(sv);
    val_t lst = V_NIL;
    for (uint32_t i = 0; i < s->cap; i++) {
        val_t e = s->buckets[i];
        if (e == EMPTY_SLOT || e == DEAD_SLOT) continue;
        Pair *p = CURRY_NEW(Pair);
        p->hdr.type=T_PAIR; p->hdr.flags=0; p->car=e; p->cdr=lst;
        lst = vptr(p);
    }
    return lst;
}

val_t list_to_set(val_t lst, int cmp_type) {
    val_t s = set_make(cmp_type);
    while (vis_pair(lst)) { set_add_mut(s, vcar(lst)); lst = vcdr(lst); }
    return s;
}

uint32_t set_size(val_t sv) { return as_set(sv)->size; }

val_t set_union(val_t a, val_t b) {
    val_t r = set_make(as_set(a)->cmp);
    val_t la = set_to_list(a), lb = set_to_list(b);
    while (vis_pair(la)) { set_add_mut(r, vcar(la)); la = vcdr(la); }
    while (vis_pair(lb)) { set_add_mut(r, vcar(lb)); lb = vcdr(lb); }
    return r;
}

val_t set_intersection(val_t a, val_t b) {
    val_t r = set_make(as_set(a)->cmp);
    val_t la = set_to_list(a);
    while (vis_pair(la)) { if (set_member(b, vcar(la))) set_add_mut(r, vcar(la)); la = vcdr(la); }
    return r;
}

val_t set_difference(val_t a, val_t b) {
    val_t r = set_make(as_set(a)->cmp);
    val_t la = set_to_list(a);
    while (vis_pair(la)) { if (!set_member(b, vcar(la))) set_add_mut(r, vcar(la)); la = vcdr(la); }
    return r;
}

bool set_subset(val_t a, val_t b) {
    val_t la = set_to_list(a);
    while (vis_pair(la)) { if (!set_member(b, vcar(la))) return false; la = vcdr(la); }
    return true;
}

bool set_equal(val_t a, val_t b) {
    return as_set(a)->size == as_set(b)->size && set_subset(a, b);
}

/* ---- Hash table ---- */

static void hash_rehash(Hashtable *h) {
    uint32_t new_cap = h->cap ? h->cap * 2 : 16;
    val_t *nk = (val_t *)gc_alloc(new_cap * sizeof(val_t));
    val_t *nv = (val_t *)gc_alloc(new_cap * sizeof(val_t));
    for (uint32_t i = 0; i < new_cap; i++) { nk[i]=EMPTY_SLOT; nv[i]=V_VOID; }
    uint32_t mask = new_cap - 1;
    for (uint32_t i = 0; i < h->cap; i++) {
        if (h->keys[i] == EMPTY_SLOT || h->keys[i] == DEAD_SLOT) continue;
        uint32_t idx = val_hash(h->keys[i], h->cmp) & mask;
        while (nk[idx] != EMPTY_SLOT) idx = (idx + 1) & mask;
        nk[idx] = h->keys[i]; nv[idx] = h->vals[i];
    }
    h->keys = nk; h->vals = nv; h->cap = new_cap;
}

val_t hash_make(int cmp_type) {
    Hashtable *h = CURRY_NEW(Hashtable);
    h->hdr.type=T_HASHTABLE; h->hdr.flags=0;
    h->size=0; h->cap=0; h->keys=NULL; h->vals=NULL; h->cmp=cmp_type;
    hash_rehash(h);
    return vptr(h);
}

void hash_set(val_t hv, val_t key, val_t val) {
    Hashtable *h = as_hash(hv);
    if (h->size * LOAD_DEN >= h->cap * LOAD_NUM) hash_rehash(h);
    uint32_t mask = h->cap - 1;
    uint32_t idx  = val_hash(key, h->cmp) & mask;
    while (h->keys[idx] != EMPTY_SLOT && h->keys[idx] != DEAD_SLOT) {
        if (slot_matches(h->keys[idx], key, h->cmp)) { h->vals[idx]=val; return; }
        idx = (idx + 1) & mask;
    }
    h->keys[idx] = key; h->vals[idx] = val; h->size++;
}

val_t hash_ref(val_t hv, val_t key, val_t def) {
    Hashtable *h = as_hash(hv);
    uint32_t mask = h->cap - 1;
    uint32_t idx  = val_hash(key, h->cmp) & mask;
    while (1) {
        if (h->keys[idx] == EMPTY_SLOT) return def;
        if (h->keys[idx] != DEAD_SLOT && slot_matches(h->keys[idx], key, h->cmp))
            return h->vals[idx];
        idx = (idx + 1) & mask;
    }
}

bool hash_has(val_t hv, val_t key) {
    return hash_ref(hv, key, V_UNDEF) != V_UNDEF;
}

void hash_delete(val_t hv, val_t key) {
    Hashtable *h = as_hash(hv);
    uint32_t mask = h->cap - 1;
    uint32_t idx  = val_hash(key, h->cmp) & mask;
    while (1) {
        if (h->keys[idx] == EMPTY_SLOT) return;
        if (h->keys[idx] != DEAD_SLOT && slot_matches(h->keys[idx], key, h->cmp)) {
            h->keys[idx] = DEAD_SLOT; h->size--; return;
        }
        idx = (idx + 1) & mask;
    }
}

val_t hash_to_alist(val_t hv) {
    Hashtable *h = as_hash(hv);
    val_t lst = V_NIL;
    for (uint32_t i = 0; i < h->cap; i++) {
        if (h->keys[i]==EMPTY_SLOT || h->keys[i]==DEAD_SLOT) continue;
        Pair *pair = CURRY_NEW(Pair); pair->hdr.type=T_PAIR; pair->hdr.flags=0;
        pair->car=h->keys[i]; pair->cdr=h->vals[i];
        Pair *cons = CURRY_NEW(Pair); cons->hdr.type=T_PAIR; cons->hdr.flags=0;
        cons->car=vptr(pair); cons->cdr=lst;
        lst = vptr(cons);
    }
    return lst;
}

val_t hash_keys(val_t hv) {
    Hashtable *h = as_hash(hv);
    val_t lst = V_NIL;
    for (uint32_t i = 0; i < h->cap; i++) {
        if (h->keys[i]==EMPTY_SLOT || h->keys[i]==DEAD_SLOT) continue;
        Pair *p = CURRY_NEW(Pair); p->hdr.type=T_PAIR; p->hdr.flags=0;
        p->car=h->keys[i]; p->cdr=lst; lst=vptr(p);
    }
    return lst;
}

val_t hash_values(val_t hv) {
    Hashtable *h = as_hash(hv);
    val_t lst = V_NIL;
    for (uint32_t i = 0; i < h->cap; i++) {
        if (h->keys[i]==EMPTY_SLOT || h->keys[i]==DEAD_SLOT) continue;
        Pair *p = CURRY_NEW(Pair); p->hdr.type=T_PAIR; p->hdr.flags=0;
        p->car=h->vals[i]; p->cdr=lst; lst=vptr(p);
    }
    return lst;
}

uint32_t hash_size(val_t hv) { return as_hash(hv)->size; }
