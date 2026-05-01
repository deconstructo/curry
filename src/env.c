#include "env.h"
#include "object.h"
#include "gc.h"
#include "symbol.h"
#include <string.h>
#include <assert.h>

//Scath was here

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

val_t GLOBAL_ENV;

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
    uint32_t *hidx = (uint32_t *)gc_alloc_atomic(hcap * sizeof(uint32_t));
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
    uint32_t *hidx = (uint32_t *)gc_alloc_atomic(hcap * sizeof(uint32_t));
    memset(hidx, 0xFF, hcap * sizeof(uint32_t));
    for (uint32_t i = 0; i < f->size; i++)
        hash_insert(hidx, hcap, f->syms, i);
    f->hidx = hidx;
    f->hcap = hcap;
}

/* ---- Frame ---- */

EnvFrame *frame_new(uint32_t cap, EnvFrame *parent) {
    EnvFrame *f = CURRY_NEW(EnvFrame);
    f->size   = 0;
    f->cap    = cap ? cap : 8;
    f->syms   = (val_t *)gc_alloc(f->cap * sizeof(val_t));
    f->vals   = (val_t *)gc_alloc(f->cap * sizeof(val_t));
    f->parent = parent;
    f->hidx   = NULL;
    f->hcap   = 0;
    return f;
}

static void frame_grow(EnvFrame *f) {
    uint32_t new_cap = f->cap * 2;
    val_t *ns = (val_t *)gc_alloc(new_cap * sizeof(val_t));
    val_t *nv = (val_t *)gc_alloc(new_cap * sizeof(val_t));
    memcpy(ns, f->syms, f->size * sizeof(val_t));
    memcpy(nv, f->vals, f->size * sizeof(val_t));
    f->syms = ns; f->vals = nv; f->cap = new_cap;
}

bool frame_define(EnvFrame *f, val_t sym, val_t val) {
    /* Check if already in this frame (redefine) */
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) { f->vals[idx] = val; return true; }
            h = (h + 1) & (f->hcap - 1);
        }
    } else {
        for (uint32_t i = 0; i < f->size; i++)
            if (f->syms[i] == sym) { f->vals[i] = val; return true; }
    }
    if (f->size >= f->cap) frame_grow(f);
    f->syms[f->size] = sym;
    f->vals[f->size] = val;
    f->size++;
    /* Build or update hash index */
    if (f->hcap) {
        frame_hash_rehash(f);
    } else if (f->size >= FRAME_HASH_THRESHOLD) {
        frame_build_hash(f);
    }
    return true;
}

bool frame_set(EnvFrame *f, val_t sym, val_t val) {
    if (f->hcap) {
        uint32_t h = sym_hash(sym, f->hcap);
        while (f->hidx[h] != UINT32_MAX) {
            uint32_t idx = f->hidx[h];
            if (f->syms[idx] == sym) { f->vals[idx] = val; return true; }
            h = (h + 1) & (f->hcap - 1);
        }
        return false;
    }
    for (uint32_t i = 0; i < f->size; i++)
        if (f->syms[i] == sym) { f->vals[i] = val; return true; }
    return false;
}

val_t *frame_lookup(EnvFrame *f, val_t sym) {
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

/* ---- Environment ---- */

static val_t make_env(EnvFrame *frame) {
    Env *e = CURRY_NEW(Env);
    e->hdr.type  = T_ENV;
    e->hdr.flags = 0;
    e->frame     = frame;
    return vptr(e);
}

val_t env_new_root(void) {
    return make_env(frame_new(64, NULL));
}

val_t env_extend(val_t parent) {
    EnvFrame *pf = vis_env(parent) ? as_env(parent)->frame : NULL;
    return make_env(frame_new(8, pf));
}

void env_define(val_t env, val_t sym, val_t val) {
    frame_define(as_env(env)->frame, sym, val);
}

bool env_set(val_t env, val_t sym, val_t val) {
    EnvFrame *f = as_env(env)->frame;
    while (f) {
        if (frame_set(f, sym, val)) return true;
        f = f->parent;
    }
    return false;
}

val_t env_lookup(val_t env, val_t sym) {
    EnvFrame *f = as_env(env)->frame;
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot) {
            if (*slot == V_UNDEF)
                scm_raise(V_FALSE, "variable used before initialization: %s", sym_cstr(sym));
            return *slot;
        }
        f = f->parent;
    }
    scm_raise(V_FALSE, "unbound variable: %s", sym_cstr(sym));
}

val_t env_lookup_or_false(val_t env, val_t sym) {
    EnvFrame *f = as_env(env)->frame;
    while (f) {
        val_t *slot = frame_lookup(f, sym);
        if (slot && *slot != V_UNDEF) return *slot;
        f = f->parent;
    }
    return V_FALSE;
}

val_t env_bind_args(val_t parent_env, val_t params, val_t args) {
    val_t new_env = env_extend(parent_env);
    EnvFrame *f = as_env(new_env)->frame;
    val_t p = params, a = args;
    while (vis_pair(p)) {
        if (vis_nil(a)) scm_raise(V_FALSE, "too few arguments");
        frame_define(f, vcar(p), vcar(a));
        p = vcdr(p); a = vcdr(a);
    }
    if (!vis_nil(p))
        frame_define(f, p, a);          /* rest arg */
    else if (!vis_nil(a))
        scm_raise(V_FALSE, "too many arguments");
    return new_env;
}

/* Bind parameters from a C array — avoids building an intermediate cons list. */
val_t env_bind_arr(val_t parent_env, val_t params, int argc, val_t *argv) {
    val_t new_env = env_extend(parent_env);
    EnvFrame *f = as_env(new_env)->frame;
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
        scm_raise(V_FALSE, "too many arguments");
    }
    return new_env;
}

void env_init(void) {
    GLOBAL_ENV = env_new_root();
}
