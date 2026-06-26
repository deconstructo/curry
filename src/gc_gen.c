/*
 * gc_gen.c — Generational GC backend (Phase 4: minor GC).
 *
 * Gen 0: per-thread bump-pointer nursery slab.
 * Gen 1: Boehm GC (tenured).  Promotion = memcpy into GC_MALLOC block.
 *
 * Minor GC procedure (gc_gen_minor_collect):
 *   1. Snapshot nursery.top → collect_top (all objects in [base, collect_top) may be live).
 *   2. Evacuate roots: shadow stack, VM value stacks, registered val_t roots.
 *   3. Process work list (BFS): scan_object each promoted object so its val_t
 *      fields are updated to point to the new Boehm copies.
 *   4. Scan pinned objects (cross-heap refs from Boehm → nursery).
 *   5. Call ext_scanner callbacks.
 *   6. Drain work list again.
 *   7. Reset nursery: set top = base (no zeroing needed; scan is [base,collect_top)).
 *   8. Clear dirty cards.
 */

#define GC_THREADS
#include "gc_gen.h"
#include "gc.h"
#include "object.h"
#include "vm.h"          /* VM struct, vm TLS pointer */
#include <gc/gc.h>
#include <gc/gc_mark.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

/* ── Pause ring buffer ───────────────────────────────────────────────────── */

static _Atomic uint64_t gc_pause_ring[GC_PAUSE_RING_N];
static _Atomic size_t   gc_pause_ring_head = 0;  /* next slot to write (mod N) */

size_t gc_get_pause_ring(uint64_t *out) {
    size_t head  = atomic_load_explicit(&gc_pause_ring_head, memory_order_acquire);
    size_t n     = head < GC_PAUSE_RING_N ? head : GC_PAUSE_RING_N;
    size_t start = head < GC_PAUSE_RING_N ? 0 : head % GC_PAUSE_RING_N;
    for (size_t i = 0; i < n; i++)
        out[i] = atomic_load_explicit(&gc_pause_ring[(start + i) % GC_PAUSE_RING_N],
                                      memory_order_acquire);
    return n;
}

void gc_reset_pause_ring(void) {
    atomic_store_explicit(&gc_pause_ring_head, 0, memory_order_relaxed);
}

/* ── Nursery size ─────────────────────────────────────────────────────────── */

#define GEN_NURSERY_DEFAULT_BYTES (512u * 1024u)
static size_t gen_nursery_size = GEN_NURSERY_DEFAULT_BYTES;

void gc_gen_set_nursery_size(size_t bytes) {
    gen_nursery_size = bytes ? bytes : GEN_NURSERY_DEFAULT_BYTES;
}

/* ── Per-thread nursery slab ──────────────────────────────────────────────── */

static void alloc_thread_nursery(void) {
    size_t sz = gen_nursery_size;
    uint8_t *slab = (uint8_t *)GC_MALLOC_UNCOLLECTABLE(sz);
    if (!slab) { fprintf(stderr, "[gc_gen] FATAL: nursery OOM\n"); abort(); }
    memset(slab, 0, sz);
    gc_nursery.base  = slab;
    gc_nursery.top   = slab;
    gc_nursery.limit = slab + sz;
}

/* ── Dirty-slot buffer allocation ────────────────────────────────────────── */

static void init_dirty_slots(void) {
    gc_dirty_slots = (val_t **)GC_MALLOC_UNCOLLECTABLE(GC_DIRTY_CAP * sizeof(val_t *));
    if (!gc_dirty_slots) { fprintf(stderr, "[gc_gen] FATAL: dirty_slots OOM\n"); abort(); }
    gc_dirty_count    = 0;
    gc_dirty_overflow = false;
}

/* ── Pinned list ──────────────────────────────────────────────────────────── */
/*
 * Pinned objects (closures, environments, upvalues, …) have val_t fields that
 * may point into the nursery and must be updated during minor GC.  We record
 * them here so gc_gen_minor_collect() can scan them.
 *
 * Lock-free fast path:
 *   pinned_count is _Atomic.  pinned_add() claims a slot with atomic_fetch_add
 *   then writes the pointer without holding pinned_lock.  The only structural
 *   invariant required is that pinned_slots[i] is valid memory for any i that
 *   has been claimed (i < pinned_cap).
 *
 * Resize slow path:
 *   When pinned_count reaches pinned_cap the caller falls back to pinned_lock,
 *   copies the array, and updates pinned_slots/pinned_cap atomically under the
 *   lock.  Fast-path callers already past the capacity check are not affected
 *   (they claimed valid indices before the resize).
 *
 * GC reset:
 *   gc_gen_minor_collect() holds pinned_lock around the step-6 scan and count
 *   reset (pinned_count = 0).  A fast-path add that claimed slot c just before
 *   the reset may write pinned_slots[c] after the count has been zeroed; that
 *   slot is effectively orphaned for one GC cycle.  This is safe because:
 *     (a) Initial fields of all pinned objects are Boehm pointers — they are
 *         set under gc_inhibit_minor(), which prevents the nursery from being
 *         collected while those fields are written.  Skipping their initial
 *         scan therefore loses nothing nursery-live.
 *     (b) Any post-construction mutation that stores a nursery pointer goes
 *         through gc_wb_slot(), which records it in gc_dirty_slots.  The next
 *         minor GC processes gc_dirty_slots and updates those fields regardless
 *         of whether the object was in the pinned scan list.
 *   In practice the race window is <10 CPU cycles; it cannot occur in
 *   single-threaded execution (GC fires on the same thread at L_DISPATCH,
 *   after gc_inhibit_count has already returned to zero).
 */
#define PINNED_INIT_CAP 256
static void         **pinned_slots;
static _Atomic size_t pinned_count;
static size_t         pinned_cap;
static size_t         pinned_stable;   /* objects below this index were fully
                                          scanned in the last minor GC */
static pthread_mutex_t pinned_lock = PTHREAD_MUTEX_INITIALIZER;

/* Exposed so env.c can register the root EnvFrame into the pinned scan list. */
void gc_gen_pin_permanent(void *obj);

static void pinned_add(void *obj) {
    /* Fast path: claim a slot without the lock. */
    size_t c = atomic_fetch_add_explicit(&pinned_count, 1, memory_order_relaxed);
    if (__builtin_expect(c < pinned_cap, 1)) {
        pinned_slots[c] = obj;
        return;
    }

    /* Slow path: capacity exceeded.  Back out and resize under pinned_lock. */
    atomic_fetch_sub_explicit(&pinned_count, 1, memory_order_relaxed);
    pthread_mutex_lock(&pinned_lock);
    /* Re-check after acquiring the lock — another thread may have already
     * resized.  If capacity is sufficient, just retry the fast path. */
    if (atomic_load_explicit(&pinned_count, memory_order_relaxed) < pinned_cap) {
        pthread_mutex_unlock(&pinned_lock);
        pinned_add(obj);   /* one recursive retry is enough */
        return;
    }
    size_t nc = pinned_cap * 2;
    void **ns = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(void *));
    if (!ns) { fprintf(stderr, "[gc_gen] FATAL: pinned_slots OOM\n"); abort(); }
    size_t cur = atomic_load_explicit(&pinned_count, memory_order_relaxed);
    memcpy(ns, pinned_slots, cur * sizeof(void *));
    memset(ns + cur, 0, (nc - cur) * sizeof(void *));
    GC_FREE(pinned_slots);
    pinned_slots = ns;
    pinned_cap   = nc;
    /* Claim a slot now that the array is large enough. */
    c = atomic_fetch_add_explicit(&pinned_count, 1, memory_order_relaxed);
    pinned_slots[c] = obj;
    pthread_mutex_unlock(&pinned_lock);
}

/* ── Nursery membership ───────────────────────────────────────────────────── */

static uint8_t *collect_top;  /* snapshot of nursery.top at collection start */

static inline bool in_nursery(const void *p) {
    return (const uint8_t *)p >= gc_nursery.base &&
           (const uint8_t *)p <  collect_top;
}

/* ── Work list (BFS queue of promoted objects to scan) ───────────────────── */

typedef struct { void *obj; } WLEntry;
static WLEntry *worklist;
static size_t   wl_len, wl_cap;

static void wl_push(void *obj) {
    if (wl_len == wl_cap) {
        size_t nc = wl_cap ? wl_cap * 2 : 512;
        worklist = realloc(worklist, nc * sizeof(WLEntry));
        if (!worklist) { fprintf(stderr, "[gc_gen] OOM work list\n"); abort(); }
        wl_cap = nc;
    }
    worklist[wl_len++].obj = obj;
}

/* ── Object size ──────────────────────────────────────────────────────────── */

static size_t obj_size(const Hdr *h) {
    switch (h->type) {
    case T_PAIR:        return (sizeof(Pair)        + 7u) & ~7u;
    case T_FLONUM:      return (sizeof(Flonum)       + 7u) & ~7u;
    case T_COMPLEX:     return (sizeof(Complex)      + 7u) & ~7u;
    case T_QUATERNION:  return (sizeof(Quaternion)   + 7u) & ~7u;
    case T_OCTONION:    return (sizeof(Octonion)     + 7u) & ~7u;
    case T_CPTR:        return (sizeof(CPtr)         + 7u) & ~7u;
    case T_PROMISE:     return (sizeof(Promise)      + 7u) & ~7u;
    case T_PARAMETER:   return (sizeof(Parameter)    + 7u) & ~7u;
    case T_SYMVAR:      return (sizeof(SymVar)        + 7u) & ~7u;
    case T_TRACED:      return (sizeof(Traced)        + 7u) & ~7u;
    case T_SYNTAX:      return (sizeof(Syntax)        + 7u) & ~7u;
    case T_ERROR:       return (sizeof(ErrorObj)      + 7u) & ~7u;
    case T_CONDITION:   return (sizeof(Condition)     + 7u) & ~7u;
    case T_RESTART:     return (sizeof(Restart)       + 7u) & ~7u;
    case T_SYMFN:       return (sizeof(SymFn)          + 7u) & ~7u;
    case T_SET:         return (sizeof(Set)           + 7u) & ~7u;
    case T_HASHTABLE:   return (sizeof(Hashtable)     + 7u) & ~7u;
    case T_MATRIX: {
        const Matrix *m = (const Matrix *)h;
        size_t elems;
        if (__builtin_mul_overflow((size_t)m->rows, (size_t)m->cols, &elems) ||
            elems > gen_nursery_size / sizeof(double))
            goto obj_size_overflow;
        return ((sizeof(Matrix) + elems * sizeof(double)) + 7u) & ~7u;
    }
    case T_MULTIVECTOR: {
        const Multivector *mv = (const Multivector *)h;
        return ((sizeof(Multivector) + mv->dim * sizeof(double)) + 7u) & ~7u;
    }
    case T_SPINOR: {
        const Spinor *s = (const Spinor *)h;
        size_t elems;
        if (__builtin_mul_overflow(2u, (size_t)s->ncomp, &elems) ||
            elems > gen_nursery_size / sizeof(double))
            goto obj_size_overflow;
        return ((sizeof(Spinor) + elems * sizeof(double)) + 7u) & ~7u;
    }
    case T_TENSOR: {
        const Tensor *t = (const Tensor *)h;
        if (t->size > gen_nursery_size / sizeof(double))
            goto obj_size_overflow;
        return ((sizeof(Tensor) + t->ndim * sizeof(uint32_t) +
                 t->size * sizeof(double)) + 7u) & ~7u;
    }
    case T_VECTOR: {
        const Vector *v = (const Vector *)h;
        return ((sizeof(Vector) + v->len * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_STRING: {
        const String *s = (const String *)h;
        return ((sizeof(String) + s->len + 1u) + 7u) & ~7u;
    }
    case T_BYTEVECTOR: {
        const Bytevector *b = (const Bytevector *)h;
        return ((sizeof(Bytevector) + b->len) + 7u) & ~7u;
    }
    case T_F64VEC: {
        const F64Vec *f = (const F64Vec *)h;
        return ((sizeof(F64Vec) + f->len * sizeof(double)) + 7u) & ~7u;
    }
    case T_VALUES: {
        const Values *v = (const Values *)h;
        return ((sizeof(Values) + v->count * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_SYMEXPR: {
        const SymExpr *e = (const SymExpr *)h;
        return ((sizeof(SymExpr) + e->nargs * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_SURREAL: {
        const Surreal *s = (const Surreal *)h;
        return ((sizeof(Surreal) + 2u * (size_t)s->nterms * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_QUANTUM: {
        const Quantum *q = (const Quantum *)h;
        return ((sizeof(Quantum) + 2u * (size_t)q->n * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_UP:
    case T_DOWN: {
        const Tuple *t = (const Tuple *)h;
        return ((sizeof(Tuple) + t->len * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_RECORD: {
        const Record *r = (const Record *)h;
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        return ((sizeof(Record) + nf * sizeof(val_t)) + 7u) & ~7u;
    }
#ifdef BUILD_MPFR
    case T_INTERVAL: {
        return (sizeof(Interval) + 7u) & ~7u;
    }
#endif
    obj_size_overflow:
        fprintf(stderr, "[gc_gen] FATAL: obj_size overflow for type %u at %p\n",
                h->type, (const void *)h);
        abort();
    default:
        fprintf(stderr, "[gc_gen] FATAL: unknown GC:MOVE type %u at %p\n",
                h->type, (const void *)h);
        /* Dump last 16 nursery allocations */
        {
            fprintf(stderr, "[gc_gen] last nursery allocations (oldest→newest):\n");
            size_t n_trace = gc_alloc_trace_idx < GC_ALLOC_TRACE_N
                           ? gc_alloc_trace_idx : GC_ALLOC_TRACE_N;
            for (size_t k = 0; k < n_trace; k++) {
                size_t slot = (gc_alloc_trace_idx - n_trace + k) % GC_ALLOC_TRACE_N;
                void *addr = gc_alloc_trace[slot];
                size_t sz  = gc_alloc_trace_sz[slot];
                uint32_t tp = 0;
                if (addr) { memcpy(&tp, addr, 4); }
                fprintf(stderr, "  [%2zu] addr=%p sz=%zu type=%u%s\n",
                        k, addr, sz, tp, addr == (void*)h ? " <-- CRASH" : "");
            }
        }
        abort();
    }
}

/* True for GC:MOVE types that contain val_t fields (need Boehm pointer scan). */
static bool type_has_ptrs(uint32_t t) {
    switch (t) {
    /* Atomic: no val_t fields */
    case T_FLONUM: case T_QUATERNION: case T_OCTONION:
    case T_F64VEC: case T_BYTEVECTOR: case T_SPINOR:
    case T_MULTIVECTOR: case T_MATRIX: case T_TENSOR:
    case T_STRING: case T_CPTR:
        return false;
    default:
        return true;
    }
}

/* ── Evacuation ───────────────────────────────────────────────────────────── */

static val_t evacuate(val_t v) {
    if (!vis_ptr(v)) return v;
    Hdr *h = (Hdr *)(uintptr_t)v;
    if (!in_nursery(h)) return v;
    if (h->type == GC_FORWARDED) return vptr((void *)h->fwd);
    size_t sz = obj_size(h);
    bool has_ptrs = type_has_ptrs(h->type);
    void *dst = has_ptrs ? GC_MALLOC(sz) : GC_MALLOC_ATOMIC(sz);
    if (!dst) { fprintf(stderr, "[gc_gen] OOM during promotion\n"); abort(); }
    memcpy(dst, h, sz);
    wl_push(dst);

    h->type = GC_FORWARDED;
    h->fwd  = (uintptr_t)dst;
    return vptr(dst);
}

static void *evacuate_raw(void *p) {
    if (!p) return p;
    Hdr *h = (Hdr *)p;
    if (!in_nursery(h)) return p;
    if (h->type == GC_FORWARDED) return (void *)h->fwd;
    /* Caller must call evacuate(vptr(p)) first for typed objects. */
    return p;
}

/* ── Scan promoted object — update all val_t fields ─────────────────────── */

static void scan_object(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {

    /* Atomic — nothing to scan */
    case T_FLONUM: case T_QUATERNION: case T_OCTONION:
    case T_F64VEC: case T_BYTEVECTOR: case T_SPINOR:
    case T_MULTIVECTOR: case T_MATRIX: case T_TENSOR:
    case T_STRING: case T_CPTR:
        break;

    case T_PAIR: {
        Pair *p = (Pair *)obj;
        p->car = evacuate(p->car);
        p->cdr = evacuate(p->cdr);
        break;
    }
    case T_COMPLEX: {
        Complex *c = (Complex *)obj;
        c->real = evacuate(c->real);
        c->imag = evacuate(c->imag);
        break;
    }
    case T_PROMISE: {
        Promise *p = (Promise *)obj;
        p->val = evacuate(p->val);
        break;
    }
    case T_PARAMETER: {
        Parameter *p = (Parameter *)obj;
        p->init      = evacuate(p->init);
        p->converter = evacuate(p->converter);
        break;
    }
    case T_SYMVAR: {
        SymVar *sv = (SymVar *)obj;
        sv->name = evacuate(sv->name);
        break;
    }
    case T_TRACED: {
        Traced *t = (Traced *)obj;
        t->proc = evacuate(t->proc);
        t->name = evacuate(t->name);
        break;
    }
    case T_SYNTAX: {
        Syntax *s = (Syntax *)obj;
        s->transformer = evacuate(s->transformer);
        break;
    }
    case T_ERROR: {
        ErrorObj *e = (ErrorObj *)obj;
        e->message   = evacuate(e->message);
        e->irritants = evacuate(e->irritants);
        e->kind      = evacuate(e->kind);
        break;
    }
    case T_CONDITION: {
        Condition *c = (Condition *)obj;
        c->type_sym = evacuate(c->type_sym);
        c->fields   = evacuate(c->fields);
        c->message  = evacuate(c->message);
        break;
    }
    case T_RESTART: {
        Restart *r = (Restart *)obj;
        r->name        = evacuate(r->name);
        r->description = evacuate(r->description);
        r->thunk       = evacuate(r->thunk);
        break;
    }
    case T_SYMFN: {
        SymFn *sf = (SymFn *)obj;
        sf->name    = evacuate(sf->name);
        sf->params  = evacuate(sf->params);
        sf->parent  = evacuate(sf->parent);
        sf->d_param = evacuate(sf->d_param);
        break;
    }
    case T_VECTOR: {
        Vector *v = (Vector *)obj;
        for (uint32_t i = 0; i < v->len; i++)
            v->data[i] = evacuate(v->data[i]);
        break;
    }
    case T_VALUES: {
        Values *v = (Values *)obj;
        for (uint32_t i = 0; i < v->count; i++)
            v->vals[i] = evacuate(v->vals[i]);
        break;
    }
    case T_SYMEXPR: {
        SymExpr *e = (SymExpr *)obj;
        e->op = evacuate(e->op);
        for (uint32_t i = 0; i < e->nargs; i++)
            e->args[i] = evacuate(e->args[i]);
        break;
    }
    case T_SURREAL: {
        Surreal *s = (Surreal *)obj;
        for (int i = 0; i < s->nterms * 2; i++)
            s->data[i] = evacuate(s->data[i]);
        break;
    }
    case T_QUANTUM: {
        Quantum *q = (Quantum *)obj;
        for (int i = 0; i < q->n * 2; i++)
            q->data[i] = evacuate(q->data[i]);
        break;
    }
    case T_UP:
    case T_DOWN: {
        Tuple *t = (Tuple *)obj;
        for (uint32_t i = 0; i < t->len; i++)
            t->data[i] = evacuate(t->data[i]);
        break;
    }
    case T_RECORD: {
        Record *r = (Record *)obj;
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        for (uint32_t i = 0; i < nf; i++)
            r->fields[i] = evacuate(r->fields[i]);
        break;
    }
    case T_SET: {
        Set *s = (Set *)obj;
        if (s->buckets)
            for (uint32_t i = 0; i < s->cap; i++)
                s->buckets[i] = evacuate(s->buckets[i]);
        break;
    }
    case T_HASHTABLE: {
        Hashtable *h2 = (Hashtable *)obj;
        if (h2->keys)
            for (uint32_t i = 0; i < h2->cap; i++) {
                h2->keys[i] = evacuate(h2->keys[i]);
                h2->vals[i] = evacuate(h2->vals[i]);
            }
        break;
    }
#ifdef BUILD_MPFR
    case T_INTERVAL: {
        Interval *iv = (Interval *)obj;
        iv->lo = evacuate(iv->lo);
        iv->hi = evacuate(iv->hi);
        break;
    }
#endif

    default:
        fprintf(stderr, "[gc_gen] FATAL: unexpected type %u in scan_object at %p\n",
                h->type, obj);
        abort();
    }
}

/* ── Scan pinned object — update cross-heap val_t refs ───────────────────── */
/*
 * Called for every GC:PIN object that has val_t fields (§3c of design doc).
 * These are in Boehm's heap and never move, but their val_t fields may point
 * into the nursery and must be updated after promotion.
 */
static void scan_pinned_object(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {
    /* Types without val_t fields — nothing to do */
    case T_SYMBOL: case T_BIGNUM: case T_RATIONAL: case T_PORT:
    case T_PRIMITIVE: case T_MPFR: case T_JITCLOSURE:
        break;
    case T_RECORD_TYPE: {
        RecordType *rt = (RecordType *)obj;
        rt->name = evacuate(rt->name);
        for (uint32_t i = 0; i < rt->nfields; i++)
            rt->field_names[i] = evacuate(rt->field_names[i]);
        break;
    }
    case T_UPVALUE: {
        Upvalue *up = (Upvalue *)obj;
        /* When closed, location == &up->closed; evacuate the stored value. */
        if (up->location == &up->closed)
            up->closed = evacuate(up->closed);
        break;
    }

    case T_ENV: {
        EnvFrame *f = (EnvFrame *)obj;
        for (uint32_t i = 0; i < f->size; i++)
            f->vals[i] = evacuate(f->vals[i]);
        break;
    }
    case T_CLOSURE: {
        Closure *c = (Closure *)obj;
        c->params = evacuate(c->params);
        c->body   = evacuate(c->body);
        c->name   = evacuate(c->name);
        break;
    }
    case T_BCCLOSURE: {
        BcClosure *bc = (BcClosure *)obj;
        bc->jit_val = evacuate(bc->jit_val);
        break;
    }
    case T_CHUNK: {
        Chunk *ch = (Chunk *)obj;
        for (int i = 0; i < ch->const_len; i++)
            ch->constants[i] = evacuate(ch->constants[i]);
        ch->src_lambda = evacuate(ch->src_lambda);
        break;
    }
    case T_MODULE: {
        Module *m = (Module *)obj;
        m->name    = evacuate(m->name);
        m->exports = evacuate(m->exports);
        break;
    }
    case T_ACTOR: {
        Actor *a = (Actor *)obj;
        a->closure = evacuate(a->closure);
        a->name    = evacuate(a->name);
        break;
    }
    case T_MAILBOX: {
        Mailbox *m = (Mailbox *)obj;
        for (size_t i = m->q.head; i != m->q.tail; i = (i + 1) % m->q.cap)
            m->q.msgs[i] = evacuate(m->q.msgs[i]);
        break;
    }
    case T_TVAR: {
        TVar *tv = (TVar *)obj;
        tv->value = evacuate(tv->value);
        break;
    }
    case T_CHANNEL: {
        Channel *ch = (Channel *)obj;
        for (uint32_t i = 0; i < ch->cap; i++)
            ch->buf[i] = evacuate(ch->buf[i]);
        break;
    }
    case T_CONTINUATION: {
        Continuation *cont = (Continuation *)obj;
        cont->result = evacuate(cont->result);
        break;
    }
    case T_FOREIGN_LIB: {
        ForeignLib *fl = (ForeignLib *)obj;
        fl->path = evacuate(fl->path);
        break;
    }
    case T_FOREIGN_FN: {
        ForeignFn *ff = (ForeignFn *)obj;
        ff->arg_tags = evacuate(ff->arg_tags);
        ff->ret_tag  = evacuate(ff->ret_tag);
        break;
    }
    default:
        /* Unknown pinned type — skip; conservative is safe. */
        break;
    }
}

/* ── gc_ss_evac/fwd bridge ───────────────────────────────────────────────── */

static uintptr_t gen_evac_fn(uintptr_t v) { return (uintptr_t)evacuate((val_t)v); }
static void     *gen_fwd_fn(void *p)      { return evacuate_raw(p); }

/* ── Minor GC ────────────────────────────────────────────────────────────── */

static pthread_mutex_t minor_gc_lock = PTHREAD_MUTEX_INITIALIZER;

void gc_gen_minor_collect(void) {
    pthread_mutex_lock(&minor_gc_lock);

    collect_top = gc_nursery.top;
    if (collect_top == gc_nursery.base) {
        pthread_mutex_unlock(&minor_gc_lock);
        return;
    }
    wl_len = 0;

    struct timespec _t0;
    clock_gettime(CLOCK_MONOTONIC, &_t0);

    /*
     * Inhibit Boehm major GC for the duration of this minor collection.
     * We call GC_MALLOC many times to promote objects; on macOS arm64,
     * Boehm's stop-the-world uses thread_suspend which fails if our thread
     * is mid-collection with minor_gc_lock held.  GC_disable/enable brackets
     * this region so Boehm defers any major collection until after we finish.
     */
    GC_disable();

    gc_evac_fn = gen_evac_fn;
    gc_fwd_fn  = gen_fwd_fn;

    /* 1. Evacuate shadow stack */
    for (GcFrame *f = gc_shadow_stack; f; f = f->prev)
        for (int i = 0; i < f->count; i++)
            *f->slots[i] = evacuate(*f->slots[i]);

    /* 2. Evacuate VM value stack */
    if (vm) {
        for (val_t *slot = vm->stack; slot < vm->sp; slot++)
            *slot = evacuate(*slot);
    }

    /* 3. Evacuate registered val_t roots */
    for (size_t i = 0; i < g_roots_count; i++) {
        *g_roots[i] = evacuate(*g_roots[i]);
        g_roots_shadow[i] = *g_roots[i];
    }

    /* 4. Evacuate registered VM stack ranges */
    for (size_t i = 0; i < g_stacks_count; i++) {
        val_t *base = g_stacks[i].base;
        val_t *sp   = *g_stacks[i].sp_ptr;
        for (val_t *slot = base; slot < sp; slot++)
            *slot = evacuate(*slot);
    }

    /* 5. Drain work list (BFS over promoted objects) */
    size_t wl_scan = 0;
    while (wl_scan < wl_len)
        scan_object(worklist[wl_scan++].obj);

    /* 6. Process dirty slots + initial scan of new pinned objects.
     *
     * Fast path (no overflow): update each recorded slot directly, then scan
     * only objects added since the last compact.  The stable range [0,
     * pinned_stable) is skipped because (a) their initial fields were Boehm
     * pointers at creation time (inhibit protocol), and (b) any subsequent
     * mutations were captured in gc_dirty_slots.
     *
     * Overflow fallback: more than GC_DIRTY_CAP tenured→nursery writes fired
     * since the last GC — fall back to a full pinned scan for correctness,
     * then compact.
     */
    if (gc_dirty_overflow) {
        pthread_mutex_lock(&pinned_lock);
        size_t pc = atomic_load_explicit(&pinned_count, memory_order_acquire);
        for (size_t i = 0; i < pc; i++) {
            void *obj = pinned_slots[i];
            if (obj) scan_pinned_object(obj);
            pinned_slots[i] = NULL;  /* release reference so Boehm can reclaim dead objects */
        }
        atomic_store_explicit(&pinned_count, 0, memory_order_release);
        pinned_stable = 0;
        pthread_mutex_unlock(&pinned_lock);
    } else {
        /* Update the recorded dirty slots in-place. */
        for (size_t i = 0; i < gc_dirty_count; i++)
            *gc_dirty_slots[i] = evacuate(*gc_dirty_slots[i]);
        /* Scan new pinned objects (those added since the last compact). */
        pthread_mutex_lock(&pinned_lock);
        size_t pc = atomic_load_explicit(&pinned_count, memory_order_acquire);
        for (size_t i = pinned_stable; i < pc; i++) {
            void *obj = pinned_slots[i];
            if (obj) scan_pinned_object(obj);
        }
        /* Null ALL slots [0, pc) so Boehm can reclaim dead objects.  The stable
         * range [0, pinned_stable) was already scanned in a prior cycle but still
         * holds stale pointers; clearing them is necessary now that pinned objects
         * are GC_MALLOC (not GC_MALLOC_UNCOLLECTABLE). */
        memset(pinned_slots, 0, pc * sizeof(void *));
        /* Compact: future mutations tracked via dirty_slots. */
        atomic_store_explicit(&pinned_count, 0, memory_order_release);
        pinned_stable = 0;
        pthread_mutex_unlock(&pinned_lock);
    }
    gc_dirty_count    = 0;
    gc_dirty_overflow = false;

    /* 7. Call ext_scanner callbacks.
     * minor_gc_lock is non-recursive — ext_scanners must not allocate from
     * the nursery (deadlock: gc_nursery_refill → gc_gen_minor_collect →
     * pthread_mutex_lock(&minor_gc_lock)).  gc_register_ext_scanner() prints
     * a warning when the first scanner is registered under the gen backend. */
    for (size_t i = 0; i < g_ext_count; i++)
        g_ext_scanners[i]();

    /* 8. Drain again (scanners may have promoted more objects) */
    while (wl_scan < wl_len)
        scan_object(worklist[wl_scan++].obj);

    gc_evac_fn = NULL;
    gc_fwd_fn  = NULL;

    /* 9. Reset nursery */
    gc_nursery.top = gc_nursery.base;

    /* Sync root shadows before Boehm's major GC can see them */
    for (size_t i = 0; i < g_roots_count; i++)
        g_roots_shadow[i] = *g_roots[i];

    /* ── Update GC statistics ── */
    {
        struct timespec _t1;
        clock_gettime(CLOCK_MONOTONIC, &_t1);
        uint64_t us = (uint64_t)(_t1.tv_sec  - _t0.tv_sec)  * 1000000u
                    + (uint64_t)(_t1.tv_nsec - _t0.tv_nsec) / 1000u;

        atomic_fetch_add_explicit(&gc_stat_minor_count,    1,  memory_order_relaxed);
        atomic_fetch_add_explicit(&gc_stat_minor_total_us, us, memory_order_relaxed);

        /* Update max pause with a CAS loop */
        uint64_t old = atomic_load_explicit(&gc_stat_minor_max_us, memory_order_relaxed);
        while (us > old &&
               !atomic_compare_exchange_weak_explicit(&gc_stat_minor_max_us, &old, us,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed))
            ;

        /* Write into the pause ring — release so readers see the slot value */
        size_t idx = atomic_fetch_add_explicit(&gc_pause_ring_head, 1, memory_order_relaxed);
        atomic_store_explicit(&gc_pause_ring[idx % GC_PAUSE_RING_N], us,
                              memory_order_release);
    }

    GC_enable();
    GC_invoke_finalizers();

    pthread_mutex_unlock(&minor_gc_lock);
}

/* ── vtable implementation ────────────────────────────────────────────────── */

static void *gen_alloc(size_t n, bool has_ptrs) {
    return gc_nursery_alloc(n, has_ptrs);
}

static void *gen_alloc_pinned(size_t n, bool has_ptrs) {
    /* Use GC_MALLOC so that dead pinned objects (closures, environments, …)
     * can be collected by Boehm's major GC once they are no longer reachable
     * from any live root.
     *
     * Live objects are always reachable through Boehm-visible roots:
     *   vm->stack (GC_MALLOC_UNCOLLECTABLE) carries val_t closures on-stack;
     *   GLOBAL_ENV and EnvFrame chains are themselves GC_MALLOC objects found
     *   by conservative scan; sub-allocations (Chunk code buffers, constant
     *   arrays) are reachable through their parent pinned object.
     *
     * Step 6 of gc_gen_minor_collect nulls out each slot after scanning it,
     * so pinned_slots no longer holds spurious references after a GC cycle. */
    void *obj = has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
    if (obj && has_ptrs) pinned_add(obj);
    return obj;
}

static void *gen_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}

static void gen_collect(void) {
    gc_gen_minor_collect();
    GC_gcollect();
}

static void gen_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    alloc_thread_nursery();
}

static void *gen_promote(void *obj, size_t bytes, bool has_ptrs) {
    void *dst = has_ptrs ? GC_MALLOC(bytes) : GC_MALLOC_ATOMIC(bytes);
    if (!dst) { fprintf(stderr, "[gc_gen] OOM in promote\n"); abort(); }
    memcpy(dst, obj, bytes);
    return dst;
}

static void  gen_pin(void *obj)              { (void)obj; }
static void  gen_unpin(void *obj)            { (void)obj; }
static void  gen_register_root(void *slot)   { (void)slot; }  /* via gc.c global */
static void  gen_unregister_root(void *slot) { (void)slot; }
static size_t gen_heap_size(void)  { return (size_t)GC_get_heap_size();  }
static size_t gen_free_bytes(void) { return (size_t)GC_get_free_bytes(); }

gc_ops_t gc_gen_ops = {
    .alloc             = gen_alloc,
    .alloc_pinned      = gen_alloc_pinned,
    .alloc_raw_pinned  = gen_alloc_raw_pinned,
    .collect           = gen_collect,
    .register_thread   = gen_register_thread,
    .pin               = gen_pin,
    .unpin             = gen_unpin,
    .register_root     = gen_register_root,
    .unregister_root   = gen_unregister_root,
    .heap_size         = gen_heap_size,
    .free_bytes        = gen_free_bytes,
    .promote           = gen_promote,
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_gen_init(size_t nursery_bytes) {
    if (nursery_bytes) gen_nursery_size = nursery_bytes;

    GC_INIT();
    GC_allow_register_threads();

    pinned_slots = GC_MALLOC_UNCOLLECTABLE(PINNED_INIT_CAP * sizeof(void *));
    memset(pinned_slots, 0, PINNED_INIT_CAP * sizeof(void *));
    pinned_cap = PINNED_INIT_CAP;

    init_dirty_slots();
    alloc_thread_nursery();
    /* Expose the main thread's nursery bounds as globals so gc_wb_slot can use
     * them without touching TLS — non-main threads check these globals and never
     * match (their Boehm allocations land outside this slab). */
    gc_main_nursery_base  = gc_nursery.base;
    gc_main_nursery_limit = gc_nursery.limit;
}

/*
 * Add an externally-allocated object (e.g. GC_MALLOC_UNCOLLECTABLE) to the
 * pinned scan list so scan_pinned_object keeps its val_t fields current during
 * minor GC.  Called from env.c for the root EnvFrame.
 */
void gc_gen_pin_permanent(void *obj) {
    if (obj) pinned_add(obj);
}
