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

/*
 * GC classification for moving collectors (see docs/gc-moving-design.md §3):
 *
 *   GC:MOVE  — may live in the nursery/semispace; only referenced via val_t.
 *   GC:PIN   — always in Boehm; raw C pointer from outside GC, or OS resource,
 *              or GMP/MPFR internal pointer.  Address never changes.
 *   GC:PIN val_t: <fields>  — pinned AND contains val_t fields that point into
 *              the moveable heap; scan_pinned_object() MUST handle this type.
 *
 * Every type must be assigned to exactly one class.  When in doubt, PIN.
 * Allocation macros must match: CURRY_NEW → MOVE, CURRY_NEW_PINNED → PIN.
 */

/* ---- Object type tags ---- */
typedef enum {
    T_PAIR          =  1,  /* GC:MOVE */
    T_VECTOR        =  2,  /* GC:MOVE */
    T_STRING        =  3,  /* GC:MOVE */
    T_SYMBOL        =  4,  /* GC:PIN  — interned; pointer equality is identity */
    T_FLONUM        =  5,  /* GC:MOVE */
    T_BIGNUM        =  6,  /* GC:PIN  — GMP mpz_t internal limb pointer */
    T_RATIONAL      =  7,  /* GC:PIN  — GMP mpq_t internal limb pointer */
    T_COMPLEX       =  8,  /* GC:MOVE */
    T_QUATERNION    =  9,  /* GC:MOVE */
    T_OCTONION      = 10,  /* GC:MOVE */
    T_BYTEVECTOR    = 11,  /* GC:MOVE */
    T_PORT          = 12,  /* GC:PIN  — OS file descriptor */
    T_CLOSURE       = 13,  /* GC:PIN val_t: params, body, name — eval.c holds raw Closure* */
    T_PRIMITIVE     = 14,  /* GC:PIN  — function pointer, no heap data */
    T_CONTINUATION  = 15,  /* GC:PIN val_t: result — setjmp buffer cannot move */
    T_ACTOR         = 16,  /* GC:PIN val_t: closure, name — POSIX thread reference */
    T_MAILBOX       = 17,  /* GC:PIN val_t: q.msgs[] — mutex/condvar inside */
    T_SET           = 18,  /* GC:MOVE — buckets[] is GC_MALLOC, scanned in place */
    T_HASHTABLE     = 19,  /* GC:MOVE — keys[]/vals[] are GC_MALLOC, scanned in place */
    T_RECORD_TYPE   = 20,  /* GC:MOVE */
    T_RECORD        = 21,  /* GC:MOVE */
    T_MODULE        = 22,  /* GC:PIN val_t: name, exports — module registry holds raw Module* */
    T_ENV           = 23,  /* GC:PIN val_t: vals[] — eval.c holds raw EnvFrame* */
    T_VALUES        = 24,  /* GC:MOVE */
    T_SYNTAX        = 25,  /* GC:MOVE */
    T_ERROR         = 26,  /* GC:MOVE */
    T_PROMISE       = 27,  /* GC:MOVE */
    T_PARAMETER     = 28,  /* GC:MOVE — dynamic parameter (make-parameter) */
    T_SYMVAR        = 29,  /* GC:MOVE — symbolic unknown (a variable in an expression) */
    T_SYMEXPR       = 30,  /* GC:MOVE — symbolic compound expression */
    T_QUANTUM       = 31,  /* GC:MOVE — quantum superposition of values */
    T_SURREAL       = 32,  /* GC:MOVE — surreal number (Hahn-series form) */
    T_MULTIVECTOR   = 33,  /* GC:MOVE — Clifford algebra element in Cl(p,q,r) */
    T_TRACED        = 34,  /* GC:MOVE — procedure wrapped with trace instrumentation */
    T_MATRIX        = 35,  /* GC:MOVE — 2D matrix of doubles (row-major) */
    T_TENSOR        = 36,  /* GC:MOVE — N-dimensional tensor of doubles (row-major) */
    T_F64VEC        = 37,  /* GC:MOVE — typed flat double[] */
    T_SYMFN         = 38,  /* GC:MOVE — symbolic unknown function */
    T_UP            = 39,  /* GC:MOVE — contravariant up-tuple */
    T_DOWN          = 40,  /* GC:MOVE — covariant down-tuple */
    T_BCCLOSURE     = 41,  /* GC:PIN val_t: jit_val — vm.c frames hold raw BcClosure* */
    T_TVAR          = 42,  /* GC:PIN val_t: value — mutex/condvar inside */
    T_CHANNEL       = 43,  /* GC:PIN val_t: buf[] — mutex/condvar inside */
    T_JITCLOSURE    = 44,  /* GC:PIN  — JIT native code page cannot move */
    T_SPINOR        = 45,  /* GC:MOVE — Weyl/Dirac/Majorana spinor */
    T_CONDITION     = 46,  /* GC:MOVE — CL-style condition object */
    T_RESTART       = 47,  /* GC:MOVE — named restart */
    T_CPTR          = 48,  /* GC:MOVE — opaque user void* (not a GC pointer) */
    T_FOREIGN_LIB   = 49,  /* GC:PIN val_t: path — dlopen handle inside */
    T_FOREIGN_FN    = 50,  /* GC:PIN val_t: arg_tags, ret_tag — libffi CIF inside */
    T_MPFR          = 51,  /* GC:PIN  — MPFR mpfr_t internal pointer */
    T_INTERVAL      = 52,  /* GC:MOVE — lo/hi are val_t (the MPFR values they point to are pinned) */
    T_CHUNK         = 53,  /* GC:PIN val_t: constants[], src_lambda — BcClosure.chunk is raw ptr */
    T_UPVALUE       = 54,  /* GC:PIN  — vm->open_upvalues raw linked list; location is interior ptr */
} ObjType;

/*
 * All heap objects start with this header.
 *
 * fwd: zero during normal execution.  During a copying/compacting GC pass the
 * collector sets fwd to the new address and sets type = GC_FORWARDED.  The
 * mutator must never read fwd directly — use vis_forwarded() / hdr_fwd().
 * Placing fwd here (not in the first payload word) makes concurrent GC
 * safe: a concurrent mutator can always read type to detect a forwarded
 * object without racing with a payload write.
 *
 * Size: 16 bytes on all 64-bit platforms.  This is the price of compaction.
 */
typedef struct {
    uint32_t  type;   /* ObjType, or GC_FORWARDED when object has been moved */
    uint32_t  flags;  /* type-specific bit flags                              */
    uintptr_t fwd;    /* forwarding address (GC only); 0 = not forwarded      */
} Hdr;

/* Sentinel type tag written by the GC when an object is evacuated. */
#define GC_FORWARDED  0xFFFFFFFFu

#define vis_forwarded(v) (vis_ptr(v) && ((Hdr *)(uintptr_t)(v))->type == GC_FORWARDED)
#define hdr_fwd(v)       (((Hdr *)(uintptr_t)(v))->fwd)

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
#define vis_symfn(v)    vis_type(v, T_SYMFN)
#define vis_symbolic(v) (vis_symvar(v) || vis_symexpr(v))
#define vis_quantum(v)  vis_type(v, T_QUANTUM)
#define vis_surreal(v)  vis_type(v, T_SURREAL)
#define vis_mv(v)       vis_type(v, T_MULTIVECTOR)
#define vis_traced(v)   vis_type(v, T_TRACED)
#define vis_matrix(v)   vis_type(v, T_MATRIX)
#define vis_tensor(v)   vis_type(v, T_TENSOR)
#define vis_f64vec(v)   vis_type(v, T_F64VEC)
#define vis_up(v)       vis_type(v, T_UP)
#define vis_down(v)     vis_type(v, T_DOWN)
#define vis_tuple(v)    (vis_up(v) || vis_down(v))

#define vis_jitclosure(v) vis_type(v, T_JITCLOSURE)
#define vis_proc(v)     (vis_closure(v) || vis_type(v, T_BCCLOSURE) || vis_prim(v) || vis_cont(v) || vis_traced(v) || vis_jitclosure(v))

#ifdef BUILD_MPFR
#define vis_mpfr(v)     vis_type(v, T_MPFR)
#define vis_ival(v)     vis_type(v, T_INTERVAL)
#define as_mpfr(v)      vunptr(Mpfr,     v)
#define as_ival(v)      vunptr(Interval, v)
#else
#define vis_mpfr(v)     (0)
#define vis_ival(v)     (0)
#endif

#define vis_number(v)   (vis_fixnum(v) || vis_flonum(v) || vis_bignum(v) || \
                         vis_rational(v) || vis_complex(v) || \
                         vis_quat(v) || vis_oct(v) || vis_surreal(v) || \
                         vis_mpfr(v))
#define vis_exact(v)    (vis_fixnum(v) || vis_bignum(v) || vis_rational(v))
#define vis_inexact(v)  (vis_flonum(v) || vis_mpfr(v))
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

#ifdef BUILD_MPFR
#include <mpfr.h>
/* MPFR arbitrary-precision float (atomic — mpfr_t cleaned by GC finalizer) */
typedef struct {
    Hdr    hdr;
    mpfr_t x;
} Mpfr;

/* Interval [lo, hi] — both endpoints are arbitrary real numeric val_t's
 * (typically MPFR with directed rounding for certified bounds). */
typedef struct {
    Hdr   hdr;
    val_t lo;
    val_t hi;
} Interval;
#endif

/* F64Vec: typed flat array of doubles — no GC pointers, use CURRY_NEW_FLEX_ATOM */
typedef struct {
    Hdr      hdr;
    uint32_t len;
    double   data[];
} F64Vec;

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
    int     peeked_cp;   /* one-codepoint lookahead buffer; -2 = empty */
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
struct EnvFrame;
struct Actor;
struct VM;

/* Closure: compiled lambda */
typedef struct {
    Hdr              hdr;
    val_t            params;   /* symbol, or list of symbols, or improper list for rest */
    val_t            body;     /* list of expressions (implicit begin) */
    struct EnvFrame *env;
    val_t            name;     /* symbol or #f */
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

/* JIT-compiled native closure produced by the LLVM backend.
 * fn: i64 (*)(i32 argc, i64 *argv, i64 *caps) */
typedef struct {
    Hdr      hdr;
    void    *fn;
    uint32_t n_caps;
    val_t    caps[];
} JitClosure;

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

/* STM transactional variable */
typedef struct TVar {
    Hdr              hdr;
    val_t            value;     /* current committed value                */
    uint64_t         version;   /* global-clock version when last written */
    pthread_mutex_t  lock;      /* held only during commit                */
    pthread_cond_t   changed;   /* signalled after every successful write */
    int              n_waiters; /* count of threads blocked in retry      */
} TVar;

#define vis_tvar(v)   vis_type(v, T_TVAR)
#define as_tvar(v)    vunptr(TVar, v)

/* CSP buffered channel — ring buffer protected by one mutex + two condvars */
typedef struct Channel {
    Hdr             hdr;
    pthread_mutex_t lock;
    pthread_cond_t  not_full;   /* senders wait here when buf is full     */
    pthread_cond_t  not_empty;  /* receivers wait here when buf is empty  */
    val_t          *buf;        /* GC-managed ring buffer                 */
    uint32_t        head;
    uint32_t        tail;
    uint32_t        count;
    uint32_t        cap;        /* 0 = synchronous (rendezvous)           */
    bool            closed;
} Channel;

#define vis_channel(v)  vis_type(v, T_CHANNEL)
#define as_channel(v)   vunptr(Channel, v)

/* Spinor kinds — stored in Spinor.kind */
#define SPINOR_WEYL_L   0   /* left-handed Weyl ψ_α  (2 complex components) */
#define SPINOR_WEYL_R   1   /* right-handed Weyl ψ̄_α̇ (2 complex components) */
#define SPINOR_DIRAC    2   /* Dirac bispinor (ψ_L, ψ_R); 4 complex components */
#define SPINOR_MAJORANA 3   /* Majorana (self-conjugate Dirac); 4 complex components */

/* Spinor: complex spinor with proper SL(2,C) / Lorentz transform law.
 * Spinors are NOT tensors — the transform law is distinct and enforced by type.
 * Layout: Spinor header + 2*ncomp doubles (re0,im0, re1,im1, ...). */
typedef struct {
    Hdr     hdr;
    uint8_t kind;    /* SPINOR_WEYL_L / _R / SPINOR_DIRAC / SPINOR_MAJORANA */
    uint8_t ncomp;   /* 2 for Weyl, 4 for Dirac/Majorana */
    double  data[];  /* 2*ncomp doubles: re0,im0,re1,im1,... */
} Spinor;

#define vis_spinor(v)   vis_type(v, T_SPINOR)
#define as_spinor(v)    vunptr(Spinor, v)

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
    Hdr              hdr;
    val_t            name;      /* symbol or list */
    struct EnvFrame *env;
    val_t            exports;   /* list of symbols */
    void            *dl_handle; /* dlopen handle for C modules, NULL otherwise */
} Module;

/* Environment frame — also the T_ENV heap object.
 * Small frames (< FRAME_HASH_THRESHOLD) use linear scan.
 * Large frames build a parallel open-addressing hash index for O(1) lookup.
 * The frame IS the GC-managed env value; no separate Env wrapper is needed. */
#define FRAME_HASH_THRESHOLD 16
typedef struct EnvFrame {
    Hdr              hdr;    /* type = T_ENV; must be first */
    uint32_t         size;
    uint32_t         cap;
    val_t           *syms;
    val_t           *vals;
    struct EnvFrame *parent;
    uint32_t        *hidx;   /* hash index: hcap slots → index in syms/vals, or UINT32_MAX */
    uint32_t         hcap;   /* power-of-2; 0 = no hash */
    uint32_t         version; /* bumped on frame_grow, used to invalidate glob caches */
} EnvFrame;

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

/* CL-style condition object */
typedef struct {
    Hdr   hdr;
    val_t type_sym;  /* symbol naming the condition type */
    val_t fields;    /* alist of (field-name . value) pairs */
    val_t message;   /* string or #f */
} Condition;

#define vis_condition(v)  vis_type(v, T_CONDITION)
#define as_condition(v)   vunptr(Condition, v)

/* Named restart */
typedef struct {
    Hdr   hdr;
    val_t name;        /* symbol */
    val_t description; /* string */
    val_t thunk;       /* 0-arg procedure */
} Restart;

#define vis_restart(v)  vis_type(v, T_RESTART)
#define as_restart(v)   vunptr(Restart, v)

/* Opaque C pointer — wraps a void* for passing to/from foreign functions */
typedef struct { Hdr hdr; void *ptr; } CPtr;
#define vis_cptr(v)      vis_type(v, T_CPTR)
#define as_cptr(v)       vunptr(CPtr, v)

/* Loaded shared library (dlopen handle) */
typedef struct { Hdr hdr; void *handle; val_t path; } ForeignLib;
#define vis_foreignlib(v) vis_type(v, T_FOREIGN_LIB)
#define as_foreignlib(v)  vunptr(ForeignLib, v)

/* Foreign function descriptor.
 * cif and cif_atypes are malloc'd (permanent, never freed).
 * arg_tags and ret_tag are Scheme values kept in this struct so the GC
 * can find them. */
typedef struct {
    Hdr     hdr;
    void   *fn;          /* resolved symbol address                 */
    void   *cif;         /* ffi_cif* — opaque to non-FFI code       */
    void   *cif_atypes;  /* ffi_type** array — kept for GC scanning */
    val_t   arg_tags;    /* list of type symbols                    */
    val_t   ret_tag;     /* return type symbol                      */
    int     nargs;
    char   *name;        /* C function name (for diagnostics)       */
} ForeignFn;
#define vis_foreignfn(v) vis_type(v, T_FOREIGN_FN)
#define as_foreignfn(v)  vunptr(ForeignFn, v)

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

/* Symbolic function object: an unknown function f(x,y,...) whose ∂ produces derivative sym-fns */
typedef struct {
    Hdr   hdr;
    val_t name;    /* interned symbol — 'u, 'f, etc. */
    val_t params;  /* Scheme list of sym-vars (natural variables); V_NIL if none */
    val_t parent;  /* parent T_SYMFN if this is a derivative of another; V_FALSE otherwise */
    val_t d_param; /* sym-var this was differentiated w.r.t. (element of parent's params); V_FALSE */
} SymFn;

/* Tuple: up (contravariant) or down (covariant) ordered collection.
 * Componentwise addition/negation; (down u) · (up v) = Σ uᵢvᵢ (contraction). */
typedef struct {
    Hdr      hdr;    /* type = T_UP or T_DOWN */
    uint32_t len;
    val_t    data[]; /* len components */
} Tuple;

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
#define as_f64v(v)    vunptr(F64Vec,     v)
#define as_bytes(v)   vunptr(Bytevector, v)
#define as_port(v)    vunptr(Port,       v)
#define as_clos(v)    vunptr(Closure,    v)
#define as_prim(v)    vunptr(Primitive,  v)
#define as_cont(v)    vunptr(Continuation, v)
#define as_jitclos(v) vunptr(JitClosure, v)
#define as_actor(v)   vunptr(Actor,      v)
#define as_mbox(v)    vunptr(Mailbox,    v)
#define as_set(v)     vunptr(Set,        v)
#define as_hash(v)    vunptr(Hashtable,  v)
#define as_rtd(v)     vunptr(RecordType, v)
#define as_rec(v)     vunptr(Record,     v)
#define as_module(v)  vunptr(Module,     v)
#define as_env(v)     vunptr(EnvFrame,   v)
#define as_vals(v)    vunptr(Values,     v)
#define as_syntax(v)  vunptr(Syntax,     v)
#define as_err(v)     vunptr(ErrorObj,   v)
#define as_promise(v) vunptr(Promise,    v)
#define as_param(v)   vunptr(Parameter,  v)
#define as_symvar(v)  vunptr(SymVar,      v)
#define as_symexpr(v) vunptr(SymExpr,     v)
#define as_symfn(v)   vunptr(SymFn,       v)
#define as_tuple(v)   vunptr(Tuple,       v)
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
