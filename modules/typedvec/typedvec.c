/* typedvec.c — SRFI-4 homogeneous numeric vector datatypes: u8vector,
 * s8vector, u16vector, s16vector, u32vector, s32vector, u64vector,
 * s64vector. (curry f64vector, a separate pre-existing module with a
 * much larger BLAS-flavored operation set, already covers f64vector --
 * see object.h's TVKind comment for why that ninth SRFI-4 type is
 * deliberately not duplicated here.)
 *
 * One shared TypedVec struct backs all 8 kinds (object.h) rather than
 * 8 separate structs/ObjTypes; the operations below are similarly
 * generic, parameterized by TVKind passed through curry_define_fn's
 * `ud` argument rather than hand-duplicated 8 times each.
 *
 * (import (srfi 4))
 */

#include <curry.h>
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <gmp.h>

/* ---- kind metadata ---- */

typedef struct {
    const char *prefix;   /* "u8", "s8", ... */
    bool        is_signed;
    int64_t     min;      /* only meaningful when is_signed */
    uint64_t    max;      /* signed: max positive value; unsigned: max value */
} TVKindInfo;

static const TVKindInfo TV_INFO[8] = {
    [TV_U8]  = { "u8",  false, 0,               UINT8_MAX  },
    [TV_S8]  = { "s8",  true,  INT8_MIN,        INT8_MAX   },
    [TV_U16] = { "u16", false, 0,               UINT16_MAX },
    [TV_S16] = { "s16", true,  INT16_MIN,       INT16_MAX  },
    [TV_U32] = { "u32", false, 0,               UINT32_MAX },
    [TV_S32] = { "s32", true,  INT32_MIN,       INT32_MAX  },
    [TV_U64] = { "u64", false, 0,               UINT64_MAX },
    [TV_S64] = { "s64", true,  INT64_MIN,       INT64_MAX  },
};

static TVKind ud_kind(void *ud) { return (TVKind)(intptr_t)ud; }

/* ---- allocation ---- */

static val_t alloc_typedvec(TVKind kind, uint32_t n) {
    uint32_t esz = tv_elem_size(kind);
    TypedVec *tv = (TypedVec *)gc_alloc_atomic(sizeof(TypedVec) + (size_t)n * esz);
    tv->hdr.type  = T_TYPEDVEC;
    tv->hdr.flags = (uint32_t)kind;
    tv->len = n;
    memset(tv->data, 0, (size_t)n * esz);
    return vptr(tv);
}

static TypedVec *get_tv(val_t v, TVKind kind, const char *ctx) {
    if (!vis_typedvec(v) || (TVKind)as_typedvec(v)->hdr.flags != kind)
        curry_error("%s: expected %svector", ctx, TV_INFO[kind].prefix);
    return as_typedvec(v);
}

static uint32_t tv_idx(val_t v, uint32_t len, const char *ctx) {
    if (!vis_fixnum(v)) curry_error("%s: expected fixnum index", ctx);
    intptr_t i = vunfix(v);
    if (i < 0 || (uint32_t)i >= len)
        curry_error("%s: index %ld out of range [0, %u)", ctx, (long)i, (unsigned)len);
    return (uint32_t)i;
}

/* start/end pair shared by ->list, copy, copy!, fill! -- defaults to
 * the whole vector when the optional argument(s) are absent. */
static void tv_range(int ac, val_t *av, int start_idx, uint32_t len,
                      const char *ctx, uint32_t *start, uint32_t *end) {
    *start = ac > start_idx ? tv_idx(av[start_idx], len + 1, ctx) : 0;
    /* tv_idx's own upper bound is exclusive and len+1 lets `end == len`
     * (one past the last element, the valid "whole rest" case) pass;
     * a true out-of-range end is caught by the *end > len check below. */
    if (*start > len) curry_error("%s: start out of range", ctx);
    *end = ac > start_idx + 1 ? (uint32_t)vunfix(av[start_idx + 1]) : len;
    if (!(ac > start_idx + 1) ) *end = len;
    if (*end > len || *end < *start)
        curry_error("%s: end out of range", ctx);
}

/* ---- element get/set: the numeric-tower <-> raw-bytes boundary ---- */

static val_t tv_get(TypedVec *tv, uint32_t i) {
    TVKind k = (TVKind)tv->hdr.flags;
    const uint8_t *p = tv->data + (size_t)i * tv_elem_size(k);
    switch (k) {
        case TV_U8:  return vfix(*(const uint8_t  *)p);
        case TV_S8:  return vfix(*(const int8_t   *)p);
        case TV_U16: { uint16_t x; memcpy(&x,p,2); return vfix(x); }
        case TV_S16: { int16_t  x; memcpy(&x,p,2); return vfix(x); }
        case TV_U32: { uint32_t x; memcpy(&x,p,4); return vfix((intptr_t)x); }
        case TV_S32: { int32_t  x; memcpy(&x,p,4); return vfix(x); }
        case TV_S64: {
            int64_t x; memcpy(&x,p,8);
            if (in_fixnum_range(x)) return vfix((intptr_t)x);
            return num_make_bignum_i((long)x); /* long == int64_t on curry's supported platforms */
        }
        case TV_U64: {
            uint64_t x; memcpy(&x,p,8);
            if (x <= (uint64_t)FIXNUM_MAX) return vfix((intptr_t)x);
            if (x <= (uint64_t)INT64_MAX) return num_make_bignum_i((long)x);
            /* Top bit set: exceeds signed long's range entirely --
             * num_make_bignum_i has no unsigned counterpart, so build
             * the bignum via GMP directly rather than round-tripping
             * through a decimal string (correct either way; this just
             * avoids the snprintf+reparse for what's still a plausible,
             * not exotic, value for a real u64vector). */
            { mpz_t z; mpz_init(z); mpz_set_ui(z, (unsigned long)(x >> 32));
              mpz_mul_2exp(z, z, 32); mpz_add_ui(z, z, (unsigned long)(x & 0xFFFFFFFFu));
              char *s = mpz_get_str(NULL, 10, z); mpz_clear(z);
              val_t r = num_make_bignum_str(s, 10); free(s); return r; }
        }
    }
    return V_FALSE; /* unreachable */
}

/* Reads an exact-integer val_t into a signed int64_t range check
 * against [lo, hi], raising a clear error on the wrong type or an
 * out-of-range value rather than silently truncating. */
static int64_t tv_check_signed(val_t v, int64_t lo, int64_t hi, const char *ctx) {
    if (vis_fixnum(v)) {
        intptr_t x = vunfix(v);
        if (x < lo || x > hi) curry_error("%s: value %ld out of range", ctx, (long)x);
        return x;
    }
    if (vis_bignum(v)) {
        if (!mpz_fits_slong_p(as_big(v)->z))
            curry_error("%s: value out of range", ctx);
        long x = mpz_get_si(as_big(v)->z);
        if (x < lo || x > hi) curry_error("%s: value %ld out of range", ctx, x);
        return x;
    }
    curry_error("%s: expected an exact integer", ctx);
    return 0; /* unreachable */
}

/* Same, but for the unsigned kinds (u8/u16/u32/u64) -- checked
 * against [0, hi] as a uint64_t, since u64's own range exceeds what a
 * signed int64_t comparison could represent without ambiguity. */
static uint64_t tv_check_unsigned(val_t v, uint64_t hi, const char *ctx) {
    if (vis_fixnum(v)) {
        intptr_t x = vunfix(v);
        if (x < 0 || (uint64_t)x > hi) curry_error("%s: value %ld out of range", ctx, (long)x);
        return (uint64_t)x;
    }
    if (vis_bignum(v)) {
        if (mpz_sgn(as_big(v)->z) < 0) curry_error("%s: value out of range (negative)", ctx);
        if (mpz_fits_ulong_p(as_big(v)->z)) {
            unsigned long x = mpz_get_ui(as_big(v)->z);
            if ((uint64_t)x > hi) curry_error("%s: value out of range", ctx);
            return (uint64_t)x;
        }
        /* Doesn't fit unsigned long (only possible on a platform where
         * long is narrower than 64 bits -- not one of curry's actual
         * supported platforms, but handled rather than assumed). */
        curry_error("%s: value out of range", ctx);
    }
    curry_error("%s: expected an exact integer", ctx);
    return 0; /* unreachable */
}

static void tv_set(TypedVec *tv, uint32_t i, val_t v, const char *ctx) {
    TVKind k = (TVKind)tv->hdr.flags;
    uint8_t *p = tv->data + (size_t)i * tv_elem_size(k);
    const TVKindInfo *info = &TV_INFO[k];
    if (info->is_signed) {
        int64_t x = tv_check_signed(v, info->min, (int64_t)info->max, ctx);
        switch (k) {
            case TV_S8:  *(int8_t  *)p = (int8_t)x;  break;
            case TV_S16: { int16_t xx = (int16_t)x; memcpy(p,&xx,2); break; }
            case TV_S32: { int32_t xx = (int32_t)x; memcpy(p,&xx,4); break; }
            case TV_S64: { int64_t xx = x;          memcpy(p,&xx,8); break; }
            default: break;
        }
    } else {
        uint64_t x = tv_check_unsigned(v, info->max, ctx);
        switch (k) {
            case TV_U8:  *(uint8_t  *)p = (uint8_t)x;  break;
            case TV_U16: { uint16_t xx = (uint16_t)x; memcpy(p,&xx,2); break; }
            case TV_U32: { uint32_t xx = (uint32_t)x; memcpy(p,&xx,4); break; }
            case TV_U64: { uint64_t xx = x;           memcpy(p,&xx,8); break; }
            default: break;
        }
    }
}

/* ==== Constructors / predicates / access ==== */

static val_t fn_make_typedvector(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    if (!vis_fixnum(av[0])) curry_error("make-%svector: length must be fixnum", TV_INFO[kind].prefix);
    intptr_t n = vunfix(av[0]);
    if (n < 0) curry_error("make-%svector: negative length", TV_INFO[kind].prefix);
    val_t r = alloc_typedvec(kind, (uint32_t)n);
    if (ac > 1) {
        TypedVec *tv = as_typedvec(r);
        char ctx[32]; snprintf(ctx, sizeof(ctx), "make-%svector", TV_INFO[kind].prefix);
        /* Validate once, then fill -- tv_set re-validates per element,
         * which is fine (n is typically small; correctness over a
         * micro-optimization here). */
        for (uint32_t i = 0; i < (uint32_t)n; i++) tv_set(tv, i, av[1], ctx);
    }
    return r;
}

static val_t fn_typedvector(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    val_t r = alloc_typedvec(kind, (uint32_t)ac);
    TypedVec *tv = as_typedvec(r);
    char ctx[16]; snprintf(ctx, sizeof(ctx), "%svector", TV_INFO[kind].prefix);
    for (int i = 0; i < ac; i++) tv_set(tv, (uint32_t)i, av[i], ctx);
    return r;
}

static val_t fn_typedvector_p(int ac, val_t *av, void *ud) {
    (void)ac; TVKind kind = ud_kind(ud);
    return curry_make_bool(vis_typedvec(av[0]) && (TVKind)as_typedvec(av[0])->hdr.flags == kind);
}

static val_t fn_typedvector_length(int ac, val_t *av, void *ud) {
    (void)ac; TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-length", TV_INFO[kind].prefix);
    return curry_make_fixnum((intptr_t)get_tv(av[0], kind, ctx)->len);
}

static val_t fn_typedvector_ref(int ac, val_t *av, void *ud) {
    (void)ac; TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-ref", TV_INFO[kind].prefix);
    TypedVec *tv = get_tv(av[0], kind, ctx);
    return tv_get(tv, tv_idx(av[1], tv->len, ctx));
}

static val_t fn_typedvector_set(int ac, val_t *av, void *ud) {
    (void)ac; TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-set!", TV_INFO[kind].prefix);
    TypedVec *tv = get_tv(av[0], kind, ctx);
    uint32_t i = tv_idx(av[1], tv->len, ctx);
    tv_set(tv, i, av[2], ctx);
    return V_VOID;
}

/* ==== Conversion ==== */

static val_t fn_typedvector_to_list(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    char ctx[28]; snprintf(ctx, sizeof(ctx), "%svector->list", TV_INFO[kind].prefix);
    TypedVec *tv = get_tv(av[0], kind, ctx);
    uint32_t start, end; tv_range(ac, av, 1, tv->len, ctx, &start, &end);
    val_t out = V_NIL;
    for (uint32_t i = end; i > start; i--) out = curry_make_pair(tv_get(tv, i - 1), out);
    return out;
}

static val_t fn_list_to_typedvector(int ac, val_t *av, void *ud) {
    (void)ac; TVKind kind = ud_kind(ud);
    char ctx[28]; snprintf(ctx, sizeof(ctx), "list->%svector", TV_INFO[kind].prefix);
    int n = 0;
    for (val_t p = av[0]; curry_is_pair(p); p = curry_cdr(p)) n++;
    val_t r = alloc_typedvec(kind, (uint32_t)n);
    TypedVec *tv = as_typedvec(r);
    uint32_t i = 0;
    for (val_t p = av[0]; curry_is_pair(p); p = curry_cdr(p), i++)
        tv_set(tv, i, curry_car(p), ctx);
    return r;
}

/* ==== Copy / append / fill ==== */

static val_t fn_typedvector_copy(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-copy", TV_INFO[kind].prefix);
    TypedVec *src = get_tv(av[0], kind, ctx);
    uint32_t start, end; tv_range(ac, av, 1, src->len, ctx, &start, &end);
    uint32_t n = end - start;
    val_t r = alloc_typedvec(kind, n);
    memcpy(as_typedvec(r)->data, src->data + (size_t)start * tv_elem_size(kind),
           (size_t)n * tv_elem_size(kind));
    return r;
}

static val_t fn_typedvector_copy_bang(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    char ctx[26]; snprintf(ctx, sizeof(ctx), "%svector-copy!", TV_INFO[kind].prefix);
    TypedVec *to = get_tv(av[0], kind, ctx);
    uint32_t at = tv_idx(av[1], to->len + 1, ctx);
    TypedVec *from = get_tv(av[2], kind, ctx);
    uint32_t start, end;
    /* tv_range's start_idx counts from av[0]; here the optional
     * start/end refer to `from` and begin at av[3]/av[4]. */
    uint32_t fstart = ac > 3 ? tv_idx(av[3], from->len + 1, ctx) : 0;
    uint32_t fend   = ac > 4 ? (uint32_t)vunfix(av[4]) : from->len;
    if (fend > from->len || fend < fstart) curry_error("%s: end out of range", ctx);
    start = fstart; end = fend;
    uint32_t n = end - start;
    if (at + n > to->len) curry_error("%s: destination too short", ctx);
    memmove(to->data + (size_t)at * tv_elem_size(kind),
            from->data + (size_t)start * tv_elem_size(kind),
            (size_t)n * tv_elem_size(kind));
    return V_VOID;
}

static val_t fn_typedvector_append(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-append", TV_INFO[kind].prefix);
    uint32_t total = 0;
    for (int i = 0; i < ac; i++) total += get_tv(av[i], kind, ctx)->len;
    val_t r = alloc_typedvec(kind, total);
    uint8_t *out = as_typedvec(r)->data;
    for (int i = 0; i < ac; i++) {
        TypedVec *tv = as_typedvec(av[i]);
        size_t bytes = (size_t)tv->len * tv_elem_size(kind);
        memcpy(out, tv->data, bytes);
        out += bytes;
    }
    return r;
}

static val_t fn_typedvector_fill_bang(int ac, val_t *av, void *ud) {
    TVKind kind = ud_kind(ud);
    char ctx[24]; snprintf(ctx, sizeof(ctx), "%svector-fill!", TV_INFO[kind].prefix);
    TypedVec *tv = get_tv(av[0], kind, ctx);
    uint32_t start, end; tv_range(ac, av, 2, tv->len, ctx, &start, &end);
    for (uint32_t i = start; i < end; i++) tv_set(tv, i, av[1], ctx);
    return V_VOID;
}

/* ==== Registration ==== */

void curry_module_init(CurryVM *vm) {
    for (int k = 0; k < 8; k++) {
        const char *pfx = TV_INFO[k].prefix;
        void *ud = (void *)(intptr_t)k;
        char name[32];
#define REG(fmt, fn, lo, hi) \
        (snprintf(name, sizeof(name), fmt, pfx), curry_define_fn(vm, name, fn, lo, hi, ud))
        REG("make-%svector",   fn_make_typedvector,      1, 2);
        REG("%svector",        fn_typedvector,           0, -1);
        REG("%svector?",       fn_typedvector_p,         1, 1);
        REG("%svector-length", fn_typedvector_length,    1, 1);
        REG("%svector-ref",    fn_typedvector_ref,       2, 2);
        REG("%svector-set!",   fn_typedvector_set,       3, 3);
        REG("%svector->list",  fn_typedvector_to_list,   1, 3);
        REG("list->%svector",  fn_list_to_typedvector,   1, 1);
        REG("%svector-copy",   fn_typedvector_copy,      1, 3);
        REG("%svector-copy!",  fn_typedvector_copy_bang, 3, 5);
        REG("%svector-append", fn_typedvector_append,    0, -1);
        REG("%svector-fill!",  fn_typedvector_fill_bang, 2, 4);
#undef REG
    }
}
