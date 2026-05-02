#ifndef CURRY_OBJECT_H
#define CURRY_OBJECT_H

/*
 * Heap object layout for Curry Scheme.
 *
 * Every heap object starts with an Hdr containing a 32-bit type tag.
 * Memory is managed by Boehm GC; no manual rooting or freeing is needed.
 *
 * Numeric tower (most specific first):
 *   fixnum  -> bignum  -> rational          (exact integer/rational)
 *   flonum                                  (inexact real)
 *   complex (real+imag, any numeric parts)  (inexact/exact complex)
 *   quaternion (4 inexact components)
 *   octonion   (8 inexact components)
 */

#include "value.h"
#include <gmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* ---- Object type tags ---- */
typedef enum {
    T_PAIR          =  1,
    T_VECTOR        =  2,
    T_STRING        =  3,
    T_SYMBOL        =  4,
    T_FLONUM        =  5,
    T_BIGNUM        =  6,
    T_RATIONAL      =  7,
    T_COMPLEX       =  8,
    T_QUATERNION    =  9,
    T_OCTONION      = 10,
    T_BYTEVECTOR    = 11,
    T_PORT          = 12,
    T_CLOSURE       = 13,
    T_PRIMITIVE     = 14,
    T_CONTINUATION  = 15,
    T_ACTOR         = 16,
    T_MAILBOX       = 17,
    T_SET           = 18,
    T_HASHTABLE     = 19,
    T_RECORD_TYPE   = 20,
    T_RECORD        = 21,
    T_MODULE        = 22,
    T_ENV           = 23,
    T_VALUES        = 24,
    T_SYNTAX        = 25,
    T_ERROR         = 26,
    T_PROMISE       = 27,
    T_PARAMETER     = 28,  /* dynamic parameter (make-parameter) */
    T_SYMVAR        = 29,  /* symbolic unknown (a variable in an expression) */
    T_SYMEXPR       = 30,  /* symbolic compound expression */
    T_QUANTUM       = 31,  /* quantum superposition of values */
    T_SURREAL       = 32,  /* surreal number (Hahn-series form) */
    T_MULTIVECTOR   = 33,  /* Clifford algebra element in Cl(p,q,r) */
    T_TRACED        = 34,  /* procedure wrapped with trace instrumentation */
    T_MATRIX        = 35,  /* 2D matrix of doubles (row-major) */
    T_TENSOR        = 36,  /* N-dimensional tensor of doubles (row-major) */
} ObjType;

/* All heap objects start with this header */
typedef struct {
    uint32_t type;   /* ObjType */
    uint32_t flags;  /* type-specific bit flags */
} Hdr;

/* Get the type of a heap value (0 if not a heap pointer) */
static inline uint32_t vtype(val_t v) {
    if (!vis_ptr(v)) return 0;
    return ((Hdr *)(uintptr_t)v)->type;
}

/* Type predicates */
#define vis_type(v,t)   (vis_ptr(v) && vtype(v) == (uint32_t)(t))
#define vis_pair(v)     vis_type(v, T_PAIR)
#define vis_vector(v)   vis_type(v, T_VECTOR)
#define vis_string(v)   vis_type(v, T_STRING)
#define vis_symbol(v)   vis_type(v, T_SYMBOL)
#define vis_flonum(v)   vis_type(v, T_FLONUM)
#define vis_bignum(v)   vis_type(v, T_BIGNUM)
#define vis_rational(v) vis_type(v, T_RATIONAL)
#define vis_complex(v)  vis_type(v, T_COMPLEX)
#define vis_quat(v)     vis_type(v, T_QUATERNION)
#define vis_oct(v)      vis_type(v, T_OCTONION)
#define vis_bytes(v)    vis_type(v, T_BYTEVECTOR)
#define vis_port(v)     vis_type(v, T_PORT)
#define vis_closure(v)  vis_type(v, T_CLOSURE)
#define vis_prim(v)     vis_type(v, T_PRIMITIVE)
#define vis_cont(v)     vis_type(v, T_CONTINUATION)
#define vis_actor(v)    vis_type(v, T_ACTOR)
#define vis_mailbox(v)  vis_type(v, T_MAILBOX)
#define vis_set(v)      vis_type(v, T_SET)
#define vis_hash(v)     vis_type(v, T_HASHTABLE)
#define vis_rtd(v)      vis_type(v, T_RECORD_TYPE)
#define vis_record(v)   vis_type(v, T_RECORD)
#define vis_module(v)   vis_type(v, T_MODULE)
#define vis_env(v)      vis_type(v, T_ENV)
#define vis_values(v)   vis_type(v, T_VALUES)
#define vis_syntax(v)   vis_type(v, T_SYNTAX)
#define vis_error(v)    vis_type(v, T_ERROR)
#define vis_promise(v)  vis_type(v, T_PROMISE)
#define vis_param(v)    vis_type(v, T_PARAMETER)
#define vis_symvar(v)   vis_type(v, T_SYMVAR)
#define vis_symexpr(v)  vis_type(v, T_SYMEXPR)
#define vis_symbolic(v) (vis_symvar(v) || vis_symexpr(v))
#define vis_quantum(v)  vis_type(v, T_QUANTUM)
#define vis_surreal(v)  vis_type(v, T_SURREAL)
#define vis_mv(v)       vis_type(v, T_MULTIVECTOR)
#define vis_traced(v)   vis_type(v, T_TRACED)
#define vis_matrix(v)   vis_type(v, T_MATRIX)
#define vis_tensor(v)   vis_type(v, T_TENSOR)

#define vis_proc(v)     (vis_closure(v) || vis_prim(v) || vis_cont(v) || vis_traced(v))
#define vis_number(v)   (vis_fixnum(v) || vis_flonum(v) || vis_bignum(v) || \
                         vis_rational(v) || vis_complex(v) || \
                         vis_quat(v) || vis_oct(v) || vis_surreal(v))
#define vis_exact(v)    (vis_fixnum(v) || vis_bignum(v) || vis_rational(v))
#define vis_inexact(v)  (vis_flonum(v))
#define vis_integer(v)  (vis_fixnum(v) || vis_bignum(v) || \
                         (vis_rational(v) /* denom=1 check in numeric.c */))
#define vis_list(v)     (vis_nil(v) || vis_pair(v))

/* ---- Concrete heap types ---- */

typedef struct {
    Hdr    hdr;
    val_t  car;
    val_t  cdr;
} Pair;

typedef struct {
    Hdr      hdr;
    uint32_t len;
    val_t    data[];
} Vector;

typedef struct {
    Hdr      hdr;
    uint32_t len;   /* byte length, excluding NUL */
    uint32_t hash;
    char     data[];  /* UTF-8, NUL-terminated */
} String;

typedef struct {
    Hdr      hdr;
    uint32_t len;
    uint32_t hash;
    char     data[];  /* UTF-8, NUL-terminated */
} Symbol;

typedef struct {
    Hdr    hdr;
    double value;
} Flonum;

typedef struct {
    Hdr   hdr;
    mpz_t z;
} Bignum;

typedef struct {
    Hdr   hdr;
    mpq_t q;   /* always in canonical form (GMP ensures this) */
} Rational;

/* Complex: exact or inexact parts (any two numeric val_t) */
typedef struct {
    Hdr   hdr;
    val_t real;
    val_t imag;
} Complex;

/* Quaternion: a + bi + cj + dk  (all inexact / IEEE double) */
typedef struct {
    Hdr    hdr;
    double a, b, c, d;
} Quaternion;

/* Octonion: e0..e7  (all inexact / IEEE double) */
typedef struct {
    Hdr    hdr;
    double e[8];
} Octonion;

typedef struct {
    Hdr      hdr;
    uint32_t len;
    uint8_t  data[];
} Bytevector;

/* Port flags */
#define PORT_INPUT   0x01u
#define PORT_OUTPUT  0x02u
#define PORT_BINARY  0x04u
#define PORT_STRING  0x08u
#define PORT_CLOSED  0x10u

typedef struct {
    Hdr     hdr;
    uint8_t flags;
    union {
        FILE *fp;
        struct {
            char   *buf;
            size_t  pos;
            size_t  len;
            size_t  cap;
        } str;
    } u;
} Port;

/* Forward declarations */
struct Env;
struct Actor;
struct VM;

/* Closure: compiled lambda */
typedef struct {
    Hdr        hdr;
    val_t      params;   /* symbol, or list of symbols, or improper list for rest */
    val_t      body;     /* list of expressions (implicit begin) */
    struct Env *env;
    val_t      name;     /* symbol or #f */
} Closure;

/* Primitive: built-in C procedure */
typedef val_t (*PrimFn)(int argc, val_t *argv, void *ud);

typedef struct {
    Hdr        hdr;
    const char *name;
    int         min_args;
    int         max_args;  /* -1 = variadic */
    PrimFn      fn;
    void       *ud;
} Primitive;

/* Continuation: captured for call/cc.
 * Phase 1: escape-only continuations via setjmp/longjmp.
 * Full delimited continuations are a future extension. */
typedef struct Continuation {
    Hdr   hdr;
    void *jmpbuf;    /* heap-allocated jmp_buf */
    val_t result;    /* written before longjmp */
    void *wind_top;  /* WindFrame * at capture time; for dynamic-wind unwind */
} Continuation;

/* Message queue (ring buffer, grown as needed) */
typedef struct {
    val_t  *msgs;
    size_t  head, tail, cap;
} MsgQueue;

/* Mailbox: per-actor message queue */
typedef struct Mailbox {
    Hdr            hdr;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    MsgQueue        q;
} Mailbox;

/* Actor / Scheme process */
typedef struct Actor {
    Hdr             hdr;
    uint64_t        id;
    pthread_t       thread;
    struct Mailbox *mailbox;
    val_t           closure;  /* the actor's body closure */
    struct Actor   *parent;
    val_t           name;     /* symbol or #f */
    bool            alive;
    pthread_mutex_t lock;
} Actor;

/* Hash-based set (open addressing) */
#define SET_CMP_EQ     0  /* eq?     - pointer identity */
#define SET_CMP_EQV    1  /* eqv?    */
#define SET_CMP_EQUAL  2  /* equal?  - structural */

typedef struct {
    Hdr      hdr;
    uint32_t size;
    uint32_t cap;
    val_t   *buckets;  /* V_UNDEF = empty, V_EOF = tombstone */
    int      cmp;
} Set;

/* Hash table (same open-addressing scheme) */
typedef struct {
    Hdr      hdr;
    uint32_t size;
    uint32_t cap;
    val_t   *keys;
    val_t   *vals;
    int      cmp;
} Hashtable;

/* Record type descriptor (RTD) */
typedef struct {
    Hdr      hdr;
    val_t    name;        /* symbol */
    uint32_t nfields;
    val_t    field_names[]; /* flexible array of symbols */
} RecordType;

/* Record instance */
typedef struct {
    Hdr        hdr;
    RecordType *rtd;
    val_t       fields[];
} Record;

/* Module */
typedef struct {
    Hdr        hdr;
    val_t      name;      /* symbol or list */
    struct Env *env;
    val_t      exports;   /* list of symbols */
    void      *dl_handle; /* dlopen handle for C modules, NULL otherwise */
} Module;

/* Environment frame: one lexical scope level.
 * Small frames (< FRAME_HASH_THRESHOLD) use linear scan.
 * Large frames build a parallel open-addressing hash index for O(1) lookup. */
#define FRAME_HASH_THRESHOLD 16
typedef struct EnvFrame {
    uint32_t        size;
    uint32_t        cap;
    val_t          *syms;
    val_t          *vals;
    struct EnvFrame *parent;
    uint32_t       *hidx;   /* hash index: hcap slots → index in syms/vals, or UINT32_MAX */
    uint32_t        hcap;   /* power-of-2; 0 = no hash */
} EnvFrame;

/* GC-tracked environment */
typedef struct Env {
    Hdr       hdr;
    EnvFrame *frame;
} Env;

/* Multiple return values */
typedef struct {
    Hdr      hdr;
    uint32_t count;
    val_t    vals[];
} Values;

/* Syntax transformer (syntax-rules / explicit renaming) */
typedef struct {
    Hdr   hdr;
    val_t transformer;  /* closure: (transformer form use-env def-env) */
} Syntax;

/* Error object (R7RS error-object?) */
typedef struct {
    Hdr   hdr;
    val_t message;   /* string */
    val_t irritants; /* list */
    val_t kind;      /* symbol: error | file-error | read-error */
} ErrorObj;

/* Promise (delay / delay-force) */
#define PROMISE_LAZY   0  /* not yet forced */
#define PROMISE_FORCED 1  /* already forced */
typedef struct {
    Hdr   hdr;
    int   state;   /* PROMISE_LAZY | PROMISE_FORCED */
    val_t val;     /* thunk or forced value */
} Promise;

/* Dynamic parameter (make-parameter) */
typedef struct {
    Hdr   hdr;
    val_t init;
    val_t converter; /* #f or a procedure */
} Parameter;

/* Symbolic variable — an unknown in a symbolic expression */
typedef struct {
    Hdr   hdr;
    val_t name;   /* a symbol */
} SymVar;

/* Symbolic compound expression: (op arg0 arg1 ...) */
typedef struct {
    Hdr      hdr;
    val_t    op;       /* a symbol: "+", "*", "expt", "sin", ... */
    uint32_t nargs;
    val_t    args[];   /* flex array of sub-expressions */
} SymExpr;

/* Quantum superposition: Σᵢ αᵢ|vᵢ⟩
 * data layout: amp0, val0, amp1, val1, ... (2n entries) */
typedef struct {
    Hdr   hdr;
    int   n;         /* number of basis states */
    val_t data[];    /* 2n entries: amplitude, value alternating */
} Quantum;

/* Multivector in Cl(p,q,r): 2^n double components (n = p+q+r, max 8).
 * Blades indexed by bitmap: bit k set → basis vector e_{k+1} present.
 * Storage is GC-atomic (doubles only, no pointer fields). */
typedef struct {
    Hdr      hdr;
    uint8_t  p, q, r;   /* metric signature */
    uint8_t  n;          /* p+q+r */
    uint32_t dim;        /* 2^n */
    double   c[];        /* dim components, c[blade_bitmap] */
} Multivector;

/* Surreal number in Hahn-series form: Σᵢ cᵢ·ωᵉⁱ
 * Terms are stored in DESCENDING order of exponent.
 * Exponents and coefficients are val_t numbers (typically rational).
 * data layout: exp0, coeff0, exp1, coeff1, ... (2*nterms entries) */
typedef struct {
    Hdr   hdr;
    int   nterms;
    val_t data[];    /* 2*nterms entries: exp, coeff alternating */
} Surreal;

/* Traced procedure — wraps a procedure with enter/exit printing */
typedef struct {
    Hdr   hdr;
    val_t proc;   /* the wrapped procedure */
    val_t name;   /* symbol name, or V_FALSE */
} Traced;

/* Matrix: a 2D array of doubles stored in row-major order.
 * GC-atomic (no interior val_t pointers). */
typedef struct {
    Hdr      hdr;
    uint32_t rows;
    uint32_t cols;
    double   data[];   /* rows*cols elements, row-major */
} Matrix;

/* Tensor: an N-dimensional array of doubles stored in row-major order.
 * Layout: Tensor header + dims[ndim] uint32_t + data[size] double.
 * The data pointer is obtained via tensor_data(t) = (double *)(t->dims + t->ndim).
 * GC-atomic (no interior val_t pointers). */
typedef struct {
    Hdr      hdr;
    uint32_t ndim;     /* number of dimensions */
    uint32_t size;     /* total element count = product of dims */
    uint32_t dims[];   /* ndim dimension sizes; doubles follow immediately after */
} Tensor;

/* ---- Convenience casts ---- */
#define as_pair(v)    vunptr(Pair,       v)
#define as_vec(v)     vunptr(Vector,     v)
#define as_str(v)     vunptr(String,     v)
#define as_sym(v)     vunptr(Symbol,     v)
#define as_flo(v)     vunptr(Flonum,     v)
#define as_big(v)     vunptr(Bignum,     v)
#define as_rat(v)     vunptr(Rational,   v)
#define as_cpx(v)     vunptr(Complex,    v)
#define as_quat(v)    vunptr(Quaternion, v)
#define as_oct(v)     vunptr(Octonion,   v)
#define as_bytes(v)   vunptr(Bytevector, v)
#define as_port(v)    vunptr(Port,       v)
#define as_clos(v)    vunptr(Closure,    v)
#define as_prim(v)    vunptr(Primitive,  v)
#define as_cont(v)    vunptr(Continuation, v)
#define as_actor(v)   vunptr(Actor,      v)
#define as_mbox(v)    vunptr(Mailbox,    v)
#define as_set(v)     vunptr(Set,        v)
#define as_hash(v)    vunptr(Hashtable,  v)
#define as_rtd(v)     vunptr(RecordType, v)
#define as_rec(v)     vunptr(Record,     v)
#define as_module(v)  vunptr(Module,     v)
#define as_env(v)     vunptr(Env,        v)
#define as_vals(v)    vunptr(Values,     v)
#define as_syntax(v)  vunptr(Syntax,     v)
#define as_err(v)     vunptr(ErrorObj,   v)
#define as_promise(v) vunptr(Promise,    v)
#define as_param(v)   vunptr(Parameter,  v)
#define as_symvar(v)  vunptr(SymVar,      v)
#define as_symexpr(v) vunptr(SymExpr,     v)
#define as_quantum(v) vunptr(Quantum,     v)
#define as_surreal(v) vunptr(Surreal,     v)
#define as_mv(v)      vunptr(Multivector, v)
#define as_traced(v)  vunptr(Traced,      v)
#define as_matrix(v)  vunptr(Matrix,      v)
#define as_tensor(v)  vunptr(Tensor,      v)

/* Pair accessors */
#define vcar(v)       (as_pair(v)->car)
#define vcdr(v)       (as_pair(v)->cdr)
#define vcaar(v)      vcar(vcar(v))
#define vcadr(v)      vcar(vcdr(v))
#define vcdar(v)      vcdr(vcar(v))
#define vcddr(v)      vcdr(vcdr(v))
#define vcaddr(v)     vcar(vcddr(v))
#define vcadddr(v)    vcar(vcdr(vcddr(v)))

/* Flonum value shorthand */
#define vfloat(v)     (as_flo(v)->value)

#endif /* CURRY_OBJECT_H */
