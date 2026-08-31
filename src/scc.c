/*
 * scc.c — Scheme Compiled Cache: serialize / deserialize Chunk trees.
 *
 * The constant pool can contain:
 *   immediates  — fixnum, bool, char, nil, void, eof  (raw uint64_t)
 *   T_FLONUM    — double
 *   T_BIGNUM    — GMP mpz, written as hex string
 *   T_RATIONAL  — GMP mpq, written as "num/den" hex string
 *   T_COMPLEX   — two recursively-tagged constants (real, imag)
 *   T_QUATERNION— four doubles
 *   T_OCTONION  — eight doubles
 *   T_STRING    — uint32_t len + UTF-8 bytes
 *   T_SYMBOL    — uint32_t len + UTF-8 bytes (re-interned on load)
 *   T_PAIR      — two recursively-tagged constants (car, cdr); from quoted lists
 *   T_VECTOR    — uint32_t len + recursively-tagged elements; from quoted vectors
 *   T_BYTEVECTOR— uint32_t len + raw bytes; from #u8(...) literals
 *   T_RECORD_TYPE — name (recursively-tagged) + uint32_t nfields +
 *                   nfields recursively-tagged field names; from
 *                   define-record-type's compiled RTD constant
 *   Chunk*      — stored as raw val_t for OP_CLOSURE; serialized recursively
 *
 * const_kind() is a closed enumeration by design: any OTHER heap type
 * embedded as a bytecode constant (a Closure, Primitive, etc.) has no
 * serializable representation and is a compiler bug, not something this
 * format degrades gracefully for — see const_kind()'s default case.
 */

#include "scc.h"
#include "version.h"
#include "gc.h"
#include "object.h"
#include "symbol.h"
#include "numeric.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* ── Format constants ───────────────────────────────────────────────────── */

#define SCC_MAGIC       "CURRYBC"   /* 7 bytes, no NUL */
#define SCC_FMT_VER     '\x07'   /* v7: OP_TAIL_APPLY inserted into the opcode
                                    enum (issue #102 -- apply is now tail-
                                    callable), shifting every subsequent
                                    opcode's numeric value. Same bytecode-
                                    encoding-change treatment as v5 below.
                                  v6: persist Chunk.src_lambda (the original
                                    (lambda params . body) source form, used for
                                    tiered-JIT hot-swap and now also for the
                                    procedure-lambda/procedure-arglist introspection
                                    primitives) -- previously never written, so a
                                    cache HIT silently lost it (returned #f) even
                                    though the same script's first-ever run (a cache
                                    MISS, compiled fresh in memory) had it. Found via
                                    the (srfi 279) inspect test suite failing only
                                    on its second run, never its first.
                                  v5: OP_TAIL_CALL_WITH_VALUES inserted into the
                                    opcode enum, shifting every subsequent opcode's
                                    numeric value -- any .scc compiled by an older
                                    binary encodes bytecode using the OLD numbering,
                                    which the new binary would silently misinterpret
                                    as different opcodes rather than refuse to load.
                                    Bump this on any opcode.h reordering/insertion,
                                    not just cache-*structure* changes (v4 above was
                                    that kind); this one is a bytecode *encoding*
                                    change and needs the same treatment. */
#define SCC_SENTINEL    0xCAFEBEEFu
#define SCC_SHEBANG     "#!/usr/bin/env curry\n"

/* Constant-pool type tags written before each constant */
#define CTAG_IMM        0
#define CTAG_FLONUM     1
#define CTAG_BIGNUM     2
#define CTAG_RATIONAL   3
#define CTAG_COMPLEX    4
#define CTAG_QUAT       5
#define CTAG_OCT        6
#define CTAG_STRING     7
#define CTAG_SYMBOL     8
#define CTAG_CHUNK      9
#define CTAG_PAIR       10
#define CTAG_VECTOR     11
#define CTAG_BYTEVEC    12
#define CTAG_RECORD_TYPE 13

/* ── Write helpers ──────────────────────────────────────────────────────── */

static bool wb(FILE *f, uint8_t v) { return fputc(v, f) != EOF; }

static bool wu32(FILE *f, uint32_t v) {
    uint8_t b[4] = { v, v>>8, v>>16, v>>24 };
    return fwrite(b, 1, 4, f) == 4;
}

static bool wi32(FILE *f, int32_t v)  { return wu32(f, (uint32_t)v); }

static bool wu64(FILE *f, uint64_t v) {
    uint8_t b[8] = {
        (uint8_t)v,       (uint8_t)(v>>8),
        (uint8_t)(v>>16), (uint8_t)(v>>24),
        (uint8_t)(v>>32), (uint8_t)(v>>40),
        (uint8_t)(v>>48), (uint8_t)(v>>56)
    };
    return fwrite(b, 1, 8, f) == 8;
}

static bool wbytes(FILE *f, const void *p, uint32_t n) {
    return n == 0 || fwrite(p, 1, n, f) == n;
}

static bool wstr(FILE *f, const char *s, uint32_t len) {
    return wu32(f, len) && wbytes(f, s, len);
}

/* Skip a shebang line (#! ...\n) if present; rewind to 0 otherwise. */
static void skip_shebang(FILE *f) {
    int c1 = fgetc(f);
    if (c1 != '#') { fseek(f, 0, SEEK_SET); return; }
    int c2 = fgetc(f);
    if (c2 != '!') { fseek(f, 0, SEEK_SET); return; }
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {}
}

/* ── Read helpers ───────────────────────────────────────────────────────── */

static bool rb(FILE *f, uint8_t *v) {
    int c = fgetc(f);
    if (c == EOF) return false;
    *v = (uint8_t)c;
    return true;
}

static bool ru32(FILE *f, uint32_t *v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1]<<8)
       | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    return true;
}

static bool ri32(FILE *f, int32_t *v) {
    uint32_t u;
    if (!ru32(f, &u)) return false;
    *v = (int32_t)u;
    return true;
}

static bool ru64(FILE *f, uint64_t *v) {
    uint8_t b[8];
    if (fread(b, 1, 8, f) != 8) return false;
    *v = (uint64_t)b[0]        | ((uint64_t)b[1]<<8)
       | ((uint64_t)b[2]<<16)  | ((uint64_t)b[3]<<24)
       | ((uint64_t)b[4]<<32)  | ((uint64_t)b[5]<<40)
       | ((uint64_t)b[6]<<48)  | ((uint64_t)b[7]<<56);
    return true;
}

/* Read a length-prefixed byte string into a malloc'd NUL-terminated buffer.
   Returns NULL on failure.  Caller must free(). */
static char *rstr(FILE *f, uint32_t *len_out) {
    uint32_t len;
    if (!ru32(f, &len)) return NULL;
    if (len == UINT32_MAX) return NULL;  /* would wrap malloc(len+1) */
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    if (len && fread(buf, 1, len, f) != len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
}

/* ── Forward declarations ───────────────────────────────────────────────── */

static bool write_const(FILE *f, val_t v);
static bool write_chunk(FILE *f, const Chunk *c);
static bool read_const(FILE *f, val_t *out);
static bool read_chunk(FILE *f, Chunk *c);

/* ── Classify a constant-pool value ────────────────────────────────────── */

static int const_kind(val_t v) {
    if (!vis_ptr(v)) return CTAG_IMM;
    switch (vtype(v)) {
        case T_FLONUM:      return CTAG_FLONUM;
        case T_BIGNUM:      return CTAG_BIGNUM;
        case T_RATIONAL:    return CTAG_RATIONAL;
        case T_COMPLEX:     return CTAG_COMPLEX;
        case T_QUATERNION:  return CTAG_QUAT;
        case T_OCTONION:    return CTAG_OCT;
        case T_STRING:      return CTAG_STRING;
        case T_SYMBOL:      return CTAG_SYMBOL;
        case T_PAIR:        return CTAG_PAIR;
        case T_VECTOR:      return CTAG_VECTOR;
        case T_BYTEVECTOR:  return CTAG_BYTEVEC;
        case T_RECORD_TYPE: return CTAG_RECORD_TYPE;
        case T_CHUNK:       return CTAG_CHUNK;
        default:
            /* Every other heap type (Closure, Primitive, Continuation, ...)
             * has no serializable representation and must never reach here
             * as a bytecode constant — falling through to CTAG_CHUNK (as
             * this used to do, relying on Chunk* being the only "leftover"
             * case) would reinterpret an unrelated struct's bytes as a
             * Chunk and crash or corrupt the .scc file on write (found by
             * testing: define-record-type's compiled RTD constant hit
             * exactly this path before T_RECORD_TYPE got its own case
             * above). Any future compiler.c codegen that embeds a new kind
             * of heap object as a constant needs a matching case here —
             * this default is deliberately a hard stop, not a silent
             * guess. */
            fprintf(stderr,
                "scc: internal error: constant of type %d has no .scc "
                "serialization support (not written)\n", (int)vtype(v));
            return -1;
    }
}

/* ── Write one constant ─────────────────────────────────────────────────── */

static bool write_const(FILE *f, val_t v) {
    int kind = const_kind(v);
    if (!wb(f, (uint8_t)kind)) return false;
    switch (kind) {
    case CTAG_IMM:
        return wu64(f, (uint64_t)v);
    case CTAG_FLONUM: {
        uint64_t bits;
        double d = as_flo(v)->value;
        memcpy(&bits, &d, 8);
        return wu64(f, bits);
    }
    case CTAG_BIGNUM: {
        char *s = mpz_get_str(NULL, 16, as_big(v)->z);
        bool ok = wstr(f, s, (uint32_t)strlen(s));
        free(s);
        return ok;
    }
    case CTAG_RATIONAL: {
        char *s = mpq_get_str(NULL, 16, as_rat(v)->q);
        bool ok = wstr(f, s, (uint32_t)strlen(s));
        free(s);
        return ok;
    }
    case CTAG_COMPLEX: {
        Complex *cx = as_cpx(v);
        return write_const(f, cx->real) && write_const(f, cx->imag);
    }
    case CTAG_QUAT: {
        Quaternion *q = as_quat(v);
        uint64_t bits[4];
        memcpy(&bits[0], &q->a, 8); memcpy(&bits[1], &q->b, 8);
        memcpy(&bits[2], &q->c, 8); memcpy(&bits[3], &q->d, 8);
        return wu64(f, bits[0]) && wu64(f, bits[1])
            && wu64(f, bits[2]) && wu64(f, bits[3]);
    }
    case CTAG_OCT: {
        Octonion *o = as_oct(v);
        for (int i = 0; i < 8; i++) {
            uint64_t bits;
            memcpy(&bits, &o->e[i], 8);
            if (!wu64(f, bits)) return false;
        }
        return true;
    }
    case CTAG_STRING: {
        String *s = as_str(v);
        return wstr(f, str_data(s), s->len);
    }
    case CTAG_SYMBOL: {
        Symbol *s = as_sym(v);
        return wstr(f, s->data, s->len);
    }
    case CTAG_PAIR: {
        Pair *p = as_pair(v);
        return write_const(f, p->car) && write_const(f, p->cdr);
    }
    case CTAG_VECTOR: {
        Vector *vec = as_vec(v);
        if (!wu32(f, vec->len)) return false;
        for (uint32_t i = 0; i < vec->len; i++)
            if (!write_const(f, vec->data[i])) return false;
        return true;
    }
    case CTAG_BYTEVEC: {
        Bytevector *bv = as_bytes(v);
        return wu32(f, bv->len) && wbytes(f, bv->data, bv->len);
    }
    case CTAG_RECORD_TYPE: {
        /* define-record-type embeds its compile-time-built RTD as a
         * quoted constant (see record_type.c/compile_define_record_type);
         * name and field_names are always plain symbols, so this is fully
         * reconstructible on load — see the CTAG_RECORD_TYPE read_const
         * case. */
        RecordType *rtd = vunptr(RecordType, v);
        if (!write_const(f, rtd->name)) return false;
        if (!wu32(f, rtd->nfields)) return false;
        for (uint32_t i = 0; i < rtd->nfields; i++)
            if (!write_const(f, rtd->field_names[i])) return false;
        return true;
    }
    case CTAG_CHUNK:
        return write_chunk(f, vunptr(Chunk, v));
    }
    return false;
}

/* ── Write a Chunk ──────────────────────────────────────────────────────── */

static bool write_chunk(FILE *f, const Chunk *c) {
    if (!wi32(f, c->arity))       return false;
    if (!wi32(f, c->local_count)) return false;
    if (!wi32(f, c->upval_count)) return false;

    uint32_t nlen = c->name ? (uint32_t)strlen(c->name) : 0;
    if (!wu32(f, nlen)) return false;
    if (nlen && !wbytes(f, c->name, nlen)) return false;

    if (!wi32(f, c->code_len)) return false;
    if (!wbytes(f, c->code, (uint32_t)c->code_len)) return false;

    for (int i = 0; i < c->code_len; i++)
        if (!wi32(f, c->lines[i])) return false;

    if (!wi32(f, c->const_len)) return false;
    for (int i = 0; i < c->const_len; i++)
        if (!write_const(f, c->constants[i])) return false;

    /* Local-variable debug table (v3+): the debugger's slot→name map */
    if (!wi32(f, c->local_debug_len)) return false;
    for (int i = 0; i < c->local_debug_len; i++) {
        const LocalDebugEntry *e = &c->local_debug[i];
        Symbol *s = as_sym(e->name);
        if (!wstr(f, s->data, s->len)) return false;
        if (!wu32(f, e->slot))         return false;
        if (!wi32(f, e->start_pc))     return false;
        if (!wi32(f, e->end_pc))       return false;
    }

    /* Upvalue names (v3+): debugger + JIT metadata; may be absent */
    uint8_t have_upnames = (c->upval_names != NULL);
    if (!wb(f, have_upnames)) return false;
    if (have_upnames) {
        for (int i = 0; i < c->upval_count; i++) {
            Symbol *s = as_sym(c->upval_names[i]);
            if (!wstr(f, s->data, s->len)) return false;
        }
    }

    /* Source lambda form (v6+): tiered-JIT hot-swap + procedure-lambda/
     * procedure-arglist introspection. write_const already handles an
     * arbitrary S-expression (including V_VOID when unavailable), so
     * this is just one more constant, not a new serialization format.
     * Independent security review noted write_const's CTAG_PAIR case
     * recurses non-tail on cdr with no depth limit, and this is the
     * first caller to hand it something that can be an entire lambda
     * BODY (previously only ever short quoted literals from the
     * constant pool) -- a pre-existing exposure in shared code, not new
     * here, but this call makes it reachable more often. Not fixed as
     * part of this change (would mean converting write_const/read_const's
     * whole pair-recursion to iterative, a larger refactor of code this
     * change doesn't otherwise touch); flagged here for whoever picks
     * that up. */
    if (!write_const(f, c->src_lambda)) return false;

    return true;
}

/* ── Read one constant ──────────────────────────────────────────────────── */

static bool read_const(FILE *f, val_t *out) {
    uint8_t kind;
    if (!rb(f, &kind)) return false;

    switch (kind) {
    case CTAG_IMM: {
        uint64_t raw;
        if (!ru64(f, &raw)) return false;
        *out = (val_t)raw;
        return true;
    }
    case CTAG_FLONUM: {
        uint64_t bits;
        if (!ru64(f, &bits)) return false;
        double d;
        memcpy(&d, &bits, 8);
        *out = num_make_float(d);
        return true;
    }
    case CTAG_BIGNUM: {
        uint32_t len;
        char *buf = rstr(f, &len);
        if (!buf) return false;
        *out = num_make_bignum_str(buf, 16);
        free(buf);
        return true;
    }
    case CTAG_RATIONAL: {
        /* mpq_get_str writes "num/den" in base 16 */
        uint32_t len;
        char *buf = rstr(f, &len);
        if (!buf) return false;
        /* Split on '/' into numerator and denominator hex strings */
        char *slash = strchr(buf, '/');
        val_t num_v, den_v;
        if (slash) {
            *slash = '\0';
            num_v = num_make_bignum_str(buf,       16);
            den_v = num_make_bignum_str(slash + 1, 16);
        } else {
            /* No '/' means integer-valued rational (denominator 1) */
            num_v = num_make_bignum_str(buf, 16);
            den_v = vfix(1);
        }
        free(buf);
        *out = num_make_rational(num_v, den_v);
        return true;
    }
    case CTAG_COMPLEX: {
        val_t re, im;
        if (!read_const(f, &re) || !read_const(f, &im)) return false;
        *out = num_make_complex(re, im);
        return true;
    }
    case CTAG_QUAT: {
        uint64_t bits[4];
        for (int i = 0; i < 4; i++)
            if (!ru64(f, &bits[i])) return false;
        double a, b, c, d;
        memcpy(&a, &bits[0], 8); memcpy(&b, &bits[1], 8);
        memcpy(&c, &bits[2], 8); memcpy(&d, &bits[3], 8);
        *out = num_make_quat(a, b, c, d);
        return true;
    }
    case CTAG_OCT: {
        double e[8];
        for (int i = 0; i < 8; i++) {
            uint64_t bits;
            if (!ru64(f, &bits)) return false;
            memcpy(&e[i], &bits, 8);
        }
        *out = num_make_oct(e);
        return true;
    }
    case CTAG_STRING: {
        uint32_t len;
        char *buf = rstr(f, &len);
        if (!buf) return false;
        String *s = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
        s->hdr.type = T_STRING; s->hdr.flags = 0;
        s->len = len; s->hash = 0; s->orig_cap = len; s->ext = NULL;
        memcpy(s->data, buf, len + 1);
        free(buf);
        *out = vptr(s);
        return true;
    }
    case CTAG_SYMBOL: {
        uint32_t len;
        char *buf = rstr(f, &len);
        if (!buf) return false;
        *out = sym_intern(buf, len);
        free(buf);
        return true;
    }
    case CTAG_PAIR: {
        Pair *p = (Pair *)gc_alloc(sizeof(Pair));
        p->hdr.type = T_PAIR; p->hdr.flags = 0;
        p->car = V_NIL; p->cdr = V_NIL;
        *out = vptr(p);
        /* Read car and cdr after setting *out so GC can find the pair */
        val_t car, cdr;
        if (!read_const(f, &car) || !read_const(f, &cdr)) return false;
        p->car = car; p->cdr = cdr;
        return true;
    }
    case CTAG_VECTOR: {
        uint32_t len;
        if (!ru32(f, &len)) return false;
        Vector *vec = (Vector *)gc_alloc(sizeof(Vector) + len * sizeof(val_t));
        vec->hdr.type = T_VECTOR; vec->hdr.flags = 0; vec->len = len;
        *out = vptr(vec);
        for (uint32_t i = 0; i < len; i++) {
            val_t elem;
            if (!read_const(f, &elem)) return false;
            vec->data[i] = elem;
        }
        return true;
    }
    case CTAG_BYTEVEC: {
        /* rstr() only writes *len_out on its success path (every other
         * call site checks `if (!buf) return false;` without touching
         * len — this one read len on the failure path too, an
         * uninitialized-variable read (UB), and even in the case where
         * the garbage happened to be 0, `!buf && len > 0` is false, so a
         * genuine decode failure fell through to silently building an
         * empty bytevector instead of reporting the error. */
        uint32_t len;
        char *buf = rstr(f, &len);
        if (!buf) return false;
        Bytevector *bv = (Bytevector *)gc_alloc_atomic(sizeof(Bytevector) + len);
        bv->hdr.type = T_BYTEVECTOR; bv->hdr.flags = 0; bv->len = len;
        if (len) memcpy(bv->data, buf, len);
        if (buf) free(buf);
        *out = vptr(bv);
        return true;
    }
    case CTAG_RECORD_TYPE: {
        val_t name;
        if (!read_const(f, &name)) return false;
        uint32_t nfields;
        if (!ru32(f, &nfields)) return false;
        RecordType *rtd = (RecordType *)gc_alloc_pinned(
            sizeof(RecordType) + nfields * sizeof(val_t));
        rtd->hdr.type = T_RECORD_TYPE; rtd->hdr.flags = 0;
        rtd->name = name; rtd->nfields = nfields;
        for (uint32_t i = 0; i < nfields; i++)
            if (!read_const(f, &rtd->field_names[i])) return false;
        *out = vptr(rtd);
        return true;
    }
    case CTAG_CHUNK: {
        Chunk *sub = chunk_new();
        if (!read_chunk(f, sub)) return false;
        *out = vptr(sub);
        return true;
    }
    default:
        return false;
    }
}

/* ── Read a Chunk ───────────────────────────────────────────────────────── */

static bool read_chunk(FILE *f, Chunk *c) {
    if (!ri32(f, &c->arity))       return false;
    if (!ri32(f, &c->local_count)) return false;
    if (!ri32(f, &c->upval_count)) return false;

    /* Name */
    uint32_t nlen;
    if (!ru32(f, &nlen)) return false;
    if (nlen) {
        if (nlen == UINT32_MAX) return false;
        char *nbuf = malloc(nlen + 1);
        if (!nbuf) return false;
        if (fread(nbuf, 1, nlen, f) != nlen) { free(nbuf); return false; }
        nbuf[nlen] = '\0';
        /* Store in GC-managed memory so the pointer stays valid */
        char *gc_name = (char *)gc_alloc_atomic(nlen + 1);
        memcpy(gc_name, nbuf, nlen + 1);
        free(nbuf);
        c->name = gc_name;
    }

    /* Code */
    if (!ri32(f, &c->code_len)) return false;
    if (c->code_len < 0) return false;
    c->code_cap = c->code_len;
    c->code = (uint8_t *)gc_alloc_atomic((size_t)c->code_len + 1);
    if (!c->code) return false;
    if (c->code_len && fread(c->code, 1, (size_t)c->code_len, f) != (size_t)c->code_len)
        return false;

    /* Lines */
    c->line_cap = c->code_len;
    c->lines = (int *)gc_alloc_raw_pinned((size_t)c->code_len * sizeof(int));
    if (c->code_len && !c->lines) return false;
    for (int i = 0; i < c->code_len; i++)
        if (!ri32(f, &c->lines[i])) return false;

    /* Constants */
    if (!ri32(f, &c->const_len)) return false;
    if (c->const_len < 0) return false;
    c->const_cap = c->const_len;
    c->constants = (val_t *)gc_alloc_raw_pinned((size_t)c->const_len * sizeof(val_t));
    if (c->const_len && !c->constants) return false;
    for (int i = 0; i < c->const_len; i++)
        if (!read_const(f, &c->constants[i])) return false;

    /* Local-variable debug table (v3+) */
    if (!ri32(f, &c->local_debug_len)) return false;
    if (c->local_debug_len < 0) return false;
    c->local_debug_cap = c->local_debug_len;
    if (c->local_debug_len) {
        c->local_debug = (LocalDebugEntry *)
            gc_alloc_raw_pinned((size_t)c->local_debug_len * sizeof(LocalDebugEntry));
        if (!c->local_debug) return false;
        for (int i = 0; i < c->local_debug_len; i++) {
            LocalDebugEntry *e = &c->local_debug[i];
            uint32_t len;
            char *buf = rstr(f, &len);
            if (!buf) return false;
            e->name = sym_intern(buf, len);
            free(buf);
            uint32_t slot;
            if (!ru32(f, &slot)) return false;
            e->slot = (uint16_t)slot;
            if (!ri32(f, &e->start_pc)) return false;
            if (!ri32(f, &e->end_pc))   return false;
        }
    }

    /* Upvalue names (v3+) */
    uint8_t have_upnames;
    if (!rb(f, &have_upnames)) return false;
    if (have_upnames) {
        c->upval_names = (val_t *)
            gc_alloc_raw_pinned((size_t)c->upval_count * sizeof(val_t));
        if (c->upval_count && !c->upval_names) return false;
        for (int i = 0; i < c->upval_count; i++) {
            uint32_t len;
            char *buf = rstr(f, &len);
            if (!buf) return false;
            c->upval_names[i] = sym_intern(buf, len);
            free(buf);
        }
    }

    /* Source lambda form (v6+) */
    if (!read_const(f, &c->src_lambda)) return false;

    return true;
}

/* ── Source file content hash (cache key) ────────────────────────────────── */

/* FNV-1a 64-bit hash of the source file's full contents — keys .scc cache
 * validity. Content-hash keyed rather than mtime/size (the pre-v4 scheme):
 * mtime survives things that don't change content but do touch the
 * filesystem timestamp (git checkout, cp -p, some editors' save-in-place),
 * which either falsely invalidates a still-good cache or, worse, can look
 * "unchanged" after a real edit if a tool preserves the old mtime — a
 * content hash can't be fooled either way. Not a cryptographic hash: this
 * only needs to detect accidental changes to a build cache key, not resist
 * a deliberate collision attack.
 *
 * Refuses non-regular files (stat()'d up front, before any read — a stat
 * doesn't consume pipe data) rather than hashing them: a process
 * substitution or named-pipe "source path" (e.g. bash's `curry <(...)`,
 * used by tests/test_mcp.sh) is a one-shot, non-seekable stream — reading
 * it here to compute a hash would drain it, leaving nothing for the real
 * read-and-compile pass that follows a cache-miss verdict. mtime/size
 * (the pre-v4 scheme) didn't have this failure mode: stat() alone never
 * touches file content. Both write_scc and load_chunks_from_file already
 * treat a false return here as "cache unusable" (write fails harmlessly;
 * load falls through to a normal miss), so refusing up front is exactly
 * the right degradation — these inputs simply never get cached, matching
 * what already happened in practice (an ephemeral pipe's content differs
 * across invocations, so a stat-based cache never usefully hit on one
 * anyway; the difference is doing so no longer has a content side effect). */
static bool src_hash(const char *src_path, uint64_t *hash_out) {
    /* Open first, then fstat() the resulting descriptor rather than
     * stat()-then-fopen() on the path: the latter has a TOCTOU window in
     * which src_path could be replaced (e.g. a symlink swap) between the
     * check and the open, so the fread loop below could end up hashing a
     * different file than the one just verified regular. O_NONBLOCK keeps
     * the open from blocking if src_path names a FIFO with no writer yet
     * (POSIX says O_NONBLOCK has no effect on a regular file's own I/O, so
     * it's dropped again below once S_ISREG is confirmed, purely so a
     * blocking fread loop behaves exactly as it did before this fix). */
    int fd = open(src_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return false; }
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    FILE *f = fdopen(fd, "rb");
    if (!f) { close(fd); return false; }
    uint64_t h = 14695981039346656037ULL; /* FNV-1a 64 offset basis */
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL; /* FNV-1a 64 prime */
        }
    }
    bool ok = !ferror(f);
    fclose(f);
    if (ok) *hash_out = h;
    return ok;
}

/* ── Cache path helpers ─────────────────────────────────────────────────── */

/* Replace the last extension in path with ".scc" (or append if none).
   Writes into out (which must have room for strlen(path)+5 bytes). */
static void set_scc_ext(char *out, const char *path) {
    size_t len = strlen(path);
    memcpy(out, path, len + 1);
    char *dot = strrchr(out, '.');
    char *sep = strrchr(out, '/');
    if (dot && (!sep || dot > sep))
        strcpy(dot, ".scc");
    else
        strcpy(out + len, ".scc");
}

/* Primary cache path: source-adjacent <name>.scc.
   Returns malloc'd string; caller frees.  Never NULL unless OOM. */
static char *primary_path(const char *src_path) {
    size_t len = strlen(src_path);
    char *out = malloc(len + 5);
    if (out) set_scc_ext(out, src_path);
    return out;
}

/* Recursively create directories for the given file path (mkdir -p on parent). */
static void mkdirs_for(const char *file_path) {
    char tmp[4096];
    size_t len = strlen(file_path);
    if (len >= sizeof(tmp)) return;
    memcpy(tmp, file_path, len + 1);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Fallback cache path: ~/.cache/curry/<abs-path-mirrored>.scc
   Uses realpath to resolve symlinks / relative paths.
   Returns malloc'd string, or NULL if HOME is unset or realpath fails. */
static char *fallback_path(const char *src_path) {
    const char *home = getenv("HOME");
    if (!home) return NULL;

    char abs[4096];
    if (!realpath(src_path, abs)) return NULL;

    /* Build: home + "/.cache/curry" + abs (abs already starts with '/') */
    const char *infix = "/.cache/curry";
    size_t hlen  = strlen(home);
    size_t ilen  = strlen(infix);
    size_t alen  = strlen(abs);
    char *out = malloc(hlen + ilen + alen + 5);
    if (!out) return NULL;

    memcpy(out, home, hlen);
    memcpy(out + hlen, infix, ilen);
    memcpy(out + hlen + ilen, abs, alen + 1);
    set_scc_ext(out + hlen + ilen, out + hlen + ilen);
    return out;
}

/* ── Low-level write / read a single .scc file ──────────────────────────── */

static int write_scc(const char *scc_path, const char *src_path,
                     Chunk **chunks, int n_chunks, bool executable) {
    uint64_t hash;
    if (!src_hash(src_path, &hash)) return -1;

    FILE *f = fopen(scc_path, "wb");
    if (!f) return -1;

    bool ok = true;
    if (executable)
        ok = ok && (fwrite(SCC_SHEBANG, 1, sizeof(SCC_SHEBANG)-1, f) == sizeof(SCC_SHEBANG)-1);
    ok = ok && (fwrite(SCC_MAGIC, 1, 7, f) == 7);
    ok = ok && wb(f, (uint8_t)SCC_FMT_VER);

    uint8_t vlen = (uint8_t)strlen(CURRY_VERSION);
    ok = ok && wb(f, vlen);
    ok = ok && wbytes(f, CURRY_VERSION, vlen);

    ok = ok && wu64(f, hash);

    ok = ok && wu32(f, (uint32_t)n_chunks);
    for (int i = 0; i < n_chunks && ok; i++)
        ok = write_chunk(f, chunks[i]);

    ok = ok && wu32(f, SCC_SENTINEL);

    if (executable)
        ok = ok && (fchmod(fileno(f), 0755) == 0);

    fclose(f);
    if (!ok) { remove(scc_path); return -1; }
    return 0;
}

/* Load chunks from an already-open SCC file.  If src_path is non-NULL,
   validates the cached content hash against the source file; otherwise
   skips those 8 bytes (used when loading a .scc directly without a source). */
static bool load_chunks_from_file(FILE *f, const char *src_path,
                                   Chunk ***chunks_out, int *n_out) {
    skip_shebang(f);

    bool ok = false;
    Chunk **chunks = NULL;
    int n = 0;

    char magic[8];
    if (fread(magic, 1, 8, f) != 8) goto done;
    if (memcmp(magic, SCC_MAGIC, 7) != 0) goto done;
    if (magic[7] != SCC_FMT_VER) goto done;

    uint8_t vlen;
    if (fread(&vlen, 1, 1, f) != 1) goto done;
    char vbuf[256];
    if (fread(vbuf, 1, vlen, f) != vlen) goto done;
    vbuf[vlen] = '\0';
    if (strcmp(vbuf, CURRY_VERSION) != 0) goto done;

    if (src_path) {
        uint64_t src_h;
        if (!src_hash(src_path, &src_h)) goto done;
        uint64_t cached_h;
        if (!ru64(f, &cached_h)) goto done;
        if (cached_h != src_h) goto done;
    } else {
        if (fseek(f, 8, SEEK_CUR) != 0) goto done;
    }

    uint32_t n_chunks;
    if (!ru32(f, &n_chunks)) goto done;

    chunks = GC_MALLOC(n_chunks * sizeof(Chunk *));
    if (!chunks && n_chunks > 0) goto done;

    for (uint32_t i = 0; i < n_chunks; i++) {
        chunks[i] = chunk_new();
        if (!read_chunk(f, chunks[i])) { n = (int)i; goto done; }
    }
    n = (int)n_chunks;

    uint32_t sentinel;
    if (!ru32(f, &sentinel) || sentinel != SCC_SENTINEL) goto done;

    ok = true;

done:
    if (!ok) { chunks = NULL; n = 0; }
    *chunks_out = chunks;
    *n_out = n;
    return ok;
}

static bool read_scc(const char *scc_path, const char *src_path,
                     Chunk ***chunks_out, int *n_out) {
    FILE *f = fopen(scc_path, "rb");
    if (!f) return false;
    bool ok = load_chunks_from_file(f, src_path, chunks_out, n_out);
    fclose(f);
    return ok;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void scc_write(const char *src_path, Chunk **chunks, int n_chunks) {
    char *p = primary_path(src_path);
    if (p && write_scc(p, src_path, chunks, n_chunks, false) == 0) {
        free(p);
        return;
    }
    free(p);
    /* Primary failed (e.g. read-only directory) — try user cache */
    char *fb = fallback_path(src_path);
    if (fb) {
        mkdirs_for(fb);
        write_scc(fb, src_path, chunks, n_chunks, false);
        free(fb);
    }
}

void scc_write_to(const char *out_path, const char *src_path,
                  Chunk **chunks, int n_chunks, bool executable) {
    write_scc(out_path, src_path, chunks, n_chunks, executable);
}

void scc_clear(const char *src_path) {
    char *p = primary_path(src_path);
    if (p) { remove(p); free(p); }
    char *fb = fallback_path(src_path);
    if (fb) { remove(fb); free(fb); }
}

bool scc_load(const char *src_path, Chunk ***chunks_out, int *n_out) {
    char *p = primary_path(src_path);
    if (p && read_scc(p, src_path, chunks_out, n_out)) {
        free(p);
        return true;
    }
    free(p);
    /* Try user cache */
    char *fb = fallback_path(src_path);
    if (!fb) return false;
    bool hit = read_scc(fb, src_path, chunks_out, n_out);
    free(fb);
    return hit;
}

bool scc_load_direct(const char *scc_path, Chunk ***chunks_out, int *n_out) {
    FILE *f = fopen(scc_path, "rb");
    if (!f) return false;
    bool ok = load_chunks_from_file(f, NULL, chunks_out, n_out);
    fclose(f);
    return ok;
}
