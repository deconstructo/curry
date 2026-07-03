/*
 * gc_semispace.c — Cheney stop-the-world semispace GC for Curry Scheme.
 *
 * See gc_semispace.h for the design overview.
 */

#define GC_THREADS
#include "gc_semispace.h"
#include "gc.h"
#include "object.h"
#include "value.h"
#include "vm.h"       /* VM, Upvalue, BcClosure, Chunk */
#include "chunk.h"    /* Chunk, GlobCacheEntry */
#include "env.h"      /* EnvFrame */
#include "symbol.h"
#include <gc/gc.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef BUILD_MPFR
#include "mpfr_num.h"
#endif

/* ── Semispace memory ────────────────────────────────────────────────────── */

static uint8_t  *ss_space[2];   /* two equal-sized spaces               */
static size_t    ss_size;        /* bytes in each space                  */
static int       ss_from = 0;   /* index of the current from-space      */
static uint8_t  *ss_top;         /* bump pointer (next free byte)        */
static uint8_t  *ss_to_scan;    /* Cheney scan pointer in to-space      */

#define ss_from_base() (ss_space[ss_from])
#define ss_from_limit() (ss_space[ss_from] + ss_size)
#define ss_to_base()   (ss_space[ss_from ^ 1])

static inline bool in_from_space(const void *p) {
    return (const uint8_t *)p >= ss_from_base() &&
           (const uint8_t *)p <  ss_from_limit();
}

/* ── Statistics ──────────────────────────────────────────────────────────── */

static uint64_t ss_collections;
static size_t   ss_bytes_allocated;
static size_t   ss_bytes_survived;

/* ── Pinned list ─────────────────────────────────────────────────────────── */

/*
 * Pinned objects are those allocated via alloc_pinned(has_ptrs=true) — they
 * live in Boehm's heap and are never moved.  Their val_t fields may point into
 * the semispace, so we scan them during each collection.
 *
 * We use GC_general_register_disappearing_link so that when Boehm collects a
 * pinned object, its slot in pinned_slots[] is zeroed automatically.  The
 * semispace collector skips NULL slots during scanning.
 */
#define PINNED_INIT_CAP 256

static void   **pinned_slots;   /* GC_MALLOC array of void*, disappearing links */
static size_t   pinned_cap;
static size_t   pinned_count;   /* total slots (including NULLs) */

static pthread_mutex_t pinned_lock = PTHREAD_MUTEX_INITIALIZER;

static void pinned_add(void *obj) {
    pthread_mutex_lock(&pinned_lock);
    /* Find a free slot (NULL from a prior disappearing link) */
    for (size_t i = 0; i < pinned_count; i++) {
        if (pinned_slots[i] == NULL) {
            pinned_slots[i] = obj;
            GC_general_register_disappearing_link(&pinned_slots[i], obj);
            pthread_mutex_unlock(&pinned_lock);
            return;
        }
    }
    /* No free slot: grow the array */
    if (pinned_count == pinned_cap) {
        size_t new_cap = pinned_cap * 2;
        void **new_slots = GC_MALLOC_UNCOLLECTABLE(new_cap * sizeof(void *));
        memcpy(new_slots, pinned_slots, pinned_count * sizeof(void *));
        memset(new_slots + pinned_count, 0,
               (new_cap - pinned_count) * sizeof(void *));
        /* Re-register disappearing links: register new before unregistering
         * old to avoid a window where a Boehm collection sees no link. */
        for (size_t i = 0; i < pinned_count; i++) {
            if (pinned_slots[i]) {
                GC_general_register_disappearing_link(&new_slots[i],
                                                       new_slots[i]);
                GC_unregister_disappearing_link(&pinned_slots[i]);
            }
        }
        /* Free old (GC_MALLOC_UNCOLLECTABLE) array. */
        GC_FREE(pinned_slots);
        pinned_slots = new_slots;
        pinned_cap   = new_cap;
    }
    pinned_slots[pinned_count] = obj;
    GC_general_register_disappearing_link(&pinned_slots[pinned_count], obj);
    pinned_count++;
    pthread_mutex_unlock(&pinned_lock);
}

/* ── Root registry ───────────────────────────────────────────────────────── */

/*
 * Individual val_t* roots registered via gc_register_root().
 * val_t** roots (pointers to val_t pointers) — not used directly; all roots
 * here are single val_t slots.
 */
#define ROOTS_INIT_CAP 64
static val_t  **roots;
static size_t   roots_count;
static size_t   roots_cap;

/*
 * Stack range roots: {base, *sp_ptr} pairs.  The semispace GC scans
 * [base, *sp_ptr) as val_t roots on each collection.
 */
typedef struct { val_t *base; val_t **sp_ptr; } StackRoot;
#define STACKS_INIT_CAP 8
static StackRoot *stack_roots;
static size_t     stack_count;
static size_t     stack_cap;

/*
 * Raw C pointer roots: void** pointing to a semispace object.  After
 * collection, if *rawptr has a GC_FORWARDED Hdr, we update it.
 */
#define RAWPTRS_INIT_CAP 32
static void  ***rawptrs;
static size_t   rawptrs_count;
static size_t   rawptrs_cap;

/* Symbol table fixup callback */
static void (*sym_fixup_cb)(void) = NULL;

/* Post-collection hook */
static void (*collection_hook)(void) = NULL;

/* External root scanners */
#define EXT_SCAN_MAX 32
static void (*ext_scanners[EXT_SCAN_MAX])(void);
static size_t ext_scan_count;

/* ── Object size computation ─────────────────────────────────────────────── */

static size_t obj_size(const Hdr *h) {
    switch (h->type) {
    case T_PAIR:        return (sizeof(Pair)  + 7u) & ~7u;
    case T_FLONUM:      return (sizeof(Flonum)+ 7u) & ~7u;
    case T_QUATERNION:  return (sizeof(Quaternion) + 7u) & ~7u;
    case T_OCTONION:    return (sizeof(Octonion)   + 7u) & ~7u;
    case T_COMPLEX:     return (sizeof(Complex)    + 7u) & ~7u;
    case T_CPTR:        return (sizeof(CPtr)       + 7u) & ~7u;
    case T_PROMISE:     return (sizeof(Promise)    + 7u) & ~7u;
    case T_PARAMETER:   return (sizeof(Parameter)  + 7u) & ~7u;
    case T_SYMVAR:      return (sizeof(SymVar)      + 7u) & ~7u;
    case T_TRACED:      return (sizeof(Traced)      + 7u) & ~7u;
    case T_SYNTAX:      return (sizeof(Syntax)      + 7u) & ~7u;
    case T_ERROR:       return (sizeof(ErrorObj)    + 7u) & ~7u;
    case T_CONDITION:   return (sizeof(Condition)   + 7u) & ~7u;
    case T_RESTART:     return (sizeof(Restart)     + 7u) & ~7u;
    case T_CLOSURE:     return (sizeof(Closure)     + 7u) & ~7u;
    case T_MODULE:      return (sizeof(Module)      + 7u) & ~7u;
    case T_FOREIGN_LIB: return (sizeof(ForeignLib)  + 7u) & ~7u;
    case T_FOREIGN_FN:  return (sizeof(ForeignFn)   + 7u) & ~7u;
    case T_SYMFN:       return (sizeof(SymFn)        + 7u) & ~7u;

    case T_MATRIX: {
        const Matrix *m = (const Matrix *)h;
        return ((sizeof(Matrix) + (size_t)m->rows * m->cols * sizeof(double)) + 7u) & ~7u;
    }
    case T_MULTIVECTOR: {
        const Multivector *mv = (const Multivector *)h;
        return ((sizeof(Multivector) + mv->dim * sizeof(double)) + 7u) & ~7u;
    }
    case T_SPINOR: {
        const Spinor *s = (const Spinor *)h;
        return ((sizeof(Spinor) + 2u * s->ncomp * sizeof(double)) + 7u) & ~7u;
    }
    case T_TENSOR: {
        const Tensor *t = (const Tensor *)h;
        return ((sizeof(Tensor) + t->ndim * sizeof(uint32_t) +
                 t->size * sizeof(double)) + 7u) & ~7u;
    }

    case T_VECTOR: {
        const Vector *v = (const Vector *)h;
        return ((sizeof(Vector) + v->len * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_STRING: {
        const String *s = (const String *)h;
        return ((sizeof(String) + s->orig_cap + 1u) + 7u) & ~7u;
    }
    case T_SYMBOL: {
        const Symbol *s = (const Symbol *)h;
        return ((sizeof(Symbol) + s->len + 1u) + 7u) & ~7u;
    }
    case T_BYTEVECTOR: {
        const Bytevector *b = (const Bytevector *)h;
        return ((sizeof(Bytevector) + b->len) + 7u) & ~7u;
    }
    case T_F64VEC: {
        const F64Vec *f = (const F64Vec *)h;
        return ((sizeof(F64Vec) + f->len * sizeof(double)) + 7u) & ~7u;
    }

    case T_JITCLOSURE: {
        const JitClosure *j = (const JitClosure *)h;
        return ((sizeof(JitClosure) + j->n_caps * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_BCCLOSURE: {
        const BcClosure *bc = (const BcClosure *)h;
        return ((sizeof(BcClosure) + (size_t)bc->upval_count * sizeof(Upvalue *)) + 7u) & ~7u;
    }
    case T_CHUNK:  return (sizeof(Chunk)   + 7u) & ~7u;
    case T_UPVALUE: return (sizeof(Upvalue) + 7u) & ~7u;

    case T_ENV: return (sizeof(EnvFrame)   + 7u) & ~7u;

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
    case T_RECORD_TYPE: {
        const RecordType *rt = (const RecordType *)h;
        return ((sizeof(RecordType) + rt->nfields * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_RECORD: {
        const Record *r = (const Record *)h;
        /* nfields lives in rtd; during scan rtd is valid (pinned or already
         * forwarded) — read it directly */
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        return ((sizeof(Record) + nf * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_SET:       return (sizeof(Set)       + 7u) & ~7u;
    case T_HASHTABLE: return (sizeof(Hashtable) + 7u) & ~7u;

#ifdef BUILD_MPFR
    case T_INTERVAL:  return (sizeof(Interval) + 7u) & ~7u;
#endif

    default:
        /* Unknown / unimplemented type: abort.  This should never happen if
         * allocation sites are correct.  Print diagnostic and crash. */
        fprintf(stderr, "[gc_semispace] FATAL: unknown object type %u in from-space"
                " at %p during collection\n", h->type, (void *)h);
        abort();
    }
}

/* ── Cheney evacuate ─────────────────────────────────────────────────────── */

static uint8_t *ss_to_top;  /* bump pointer in to-space during collection */

/*
 * Forward a val_t.  If v is a heap pointer into from-space, copy the object to
 * to-space and leave a GC_FORWARDED marker.  Returns the updated val_t.
 */
static val_t evacuate(val_t v) {
    if (!vis_ptr(v)) return v;
    Hdr *h = (Hdr *)((uintptr_t)v & ~(uintptr_t)3);
    if (!in_from_space(h)) return v;
    if (h->type == GC_FORWARDED)
        return vptr((void *)h->fwd);

    size_t sz = obj_size(h);

    /* Overflow guard — if collection produced too many survivors, fall back. */
    if (ss_to_top + sz > ss_to_base() + ss_size) {
        fprintf(stderr, "[gc_semispace] to-space overflow — survivors exceed "
                "semispace size; falling back to Boehm for this object\n");
        /* Leave object in from-space; it will be referenced by Boehm after we
         * add its address to Boehm's root set — not ideal but keeps us alive. */
        return v;
    }

    /* Copy object body to to-space. */
    void *new_obj = ss_to_top;
    memcpy(new_obj, h, sz);
    ss_to_top += sz;
    ss_bytes_survived += sz;

    /* Special post-copy fixup for Upvalue interior pointer.
     * If location pointed to &old->closed, redirect to &new->closed. */
    if (h->type == T_UPVALUE) {
        Upvalue *old_uv = (Upvalue *)h;
        Upvalue *new_uv = (Upvalue *)new_obj;
        if (old_uv->location == &old_uv->closed)
            new_uv->location = &new_uv->closed;
    }

    /* Set forwarding pointer in from-space. */
    h->type = GC_FORWARDED;
    h->fwd  = (uintptr_t)new_obj;

    return vptr(new_obj);
}

/*
 * Evacuate a raw C pointer to a typed semispace object.
 * Like evacuate(val_t), but for void* — no tag-bit handling needed.
 * Used in scan_object() for raw C pointer fields (env, chunk, parent, next…).
 */
static void *evacuate_obj(void *p) {
    if (!p) return p;
    Hdr *h = (Hdr *)p;
    if (!in_from_space(h)) return p;
    if (h->type == GC_FORWARDED) return (void *)h->fwd;

    size_t sz = obj_size(h);
    if (ss_to_top + sz > ss_to_base() + ss_size) {
        /* to-space overflow — leave in place (Boehm safety net) */
        return p;
    }
    void *new_obj = ss_to_top;
    memcpy(new_obj, h, sz);
    ss_to_top += sz;
    ss_bytes_survived += sz;

    /* Upvalue interior pointer fixup */
    if (h->type == T_UPVALUE) {
        Upvalue *old_uv = (Upvalue *)h;
        Upvalue *new_uv = (Upvalue *)new_obj;
        if (old_uv->location == &old_uv->closed)
            new_uv->location = &new_uv->closed;
    }

    h->type = GC_FORWARDED;
    h->fwd  = (uintptr_t)new_obj;
    return new_obj;
}

/* ── Object scanner (called from the Cheney scan loop) ───────────────────── */

/*
 * Scan a to-space object: evacuate all its val_t fields and fix up any raw
 * C pointers to other semispace objects (those that may have forwarded).
 */
static void scan_object(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {

    /* ── Atomic (no pointer fields) ── */
    case T_FLONUM: case T_QUATERNION: case T_OCTONION:
    case T_F64VEC: case T_BYTEVECTOR: case T_SPINOR:
    case T_MULTIVECTOR: case T_MATRIX: case T_TENSOR:
    case T_STRING: case T_SYMBOL:
        break;

    /* ── Simple pointer objects ── */
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
        e->message  = evacuate(e->message);
        e->irritants= evacuate(e->irritants);
        e->kind     = evacuate(e->kind);
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
    case T_CPTR:
        break;   /* just a void*, no val_t fields */

    /* ── Flexible arrays ── */
    case T_VECTOR: {
        Vector *v = (Vector *)obj;
        for (uint32_t i = 0; i < v->len; i++)
            v->data[i] = evacuate(v->data[i]);
        break;
    }
    case T_JITCLOSURE: {
        JitClosure *j = (JitClosure *)obj;
        for (uint32_t i = 0; i < j->n_caps; i++)
            j->caps[i] = evacuate(j->caps[i]);
        /* fn is a void* native code pointer — stable, no update needed */
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
    case T_SYMFN: {
        SymFn *sf = (SymFn *)obj;
        sf->name    = evacuate(sf->name);
        sf->params  = evacuate(sf->params);
        sf->parent  = evacuate(sf->parent);
        sf->d_param = evacuate(sf->d_param);
        break;
    }
    case T_SURREAL: {
        Surreal *s = (Surreal *)obj;
        int n = s->nterms * 2;
        for (int i = 0; i < n; i++)
            s->data[i] = evacuate(s->data[i]);
        break;
    }
    case T_QUANTUM: {
        Quantum *q = (Quantum *)obj;
        int n = q->n * 2;
        for (int i = 0; i < n; i++)
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
    case T_RECORD_TYPE: {
        RecordType *rt = (RecordType *)obj;
        rt->name = evacuate(rt->name);
        for (uint32_t i = 0; i < rt->nfields; i++)
            rt->field_names[i] = evacuate(rt->field_names[i]);
        break;
    }
    case T_RECORD: {
        Record *r = (Record *)obj;
        /* Update raw rtd C pointer if it was forwarded */
        r->rtd = (RecordType *)evacuate_obj(r->rtd);
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        for (uint32_t i = 0; i < nf; i++)
            r->fields[i] = evacuate(r->fields[i]);
        break;
    }
    case T_SET: {
        Set *s = (Set *)obj;
        /* buckets is a raw Boehm array — scan its val_t entries in place */
        for (uint32_t i = 0; i < s->cap; i++)
            s->buckets[i] = evacuate(s->buckets[i]);
        break;
    }
    case T_HASHTABLE: {
        Hashtable *h2 = (Hashtable *)obj;
        for (uint32_t i = 0; i < h2->cap; i++) {
            h2->keys[i] = evacuate(h2->keys[i]);
            h2->vals[i] = evacuate(h2->vals[i]);
        }
        break;
    }

    /* ── Objects with raw C pointers to other semispace objects ── */
    case T_CLOSURE: {
        Closure *c = (Closure *)obj;
        c->params = evacuate(c->params);
        c->body   = evacuate(c->body);
        c->name   = evacuate(c->name);
        /* env is an EnvFrame* — may have moved */
        c->env = (EnvFrame *)evacuate_obj(c->env);
        break;
    }
    case T_BCCLOSURE: {
        BcClosure *bc = (BcClosure *)obj;
        bc->jit_val = evacuate(bc->jit_val);
        /* chunk may have moved */
        bc->chunk = (Chunk *)evacuate_obj(bc->chunk);
        /* upvals[i] may have moved */
        for (int i = 0; i < bc->upval_count; i++)
            bc->upvals[i] = (Upvalue *)evacuate_obj(bc->upvals[i]);
        break;
    }
    case T_CHUNK: {
        Chunk *ch = (Chunk *)obj;
        /* constants[] is a raw Boehm array — scan val_t entries in place */
        for (int i = 0; i < ch->const_len; i++)
            ch->constants[i] = evacuate(ch->constants[i]);
        ch->src_lambda = evacuate(ch->src_lambda);
        /* upval_names[] are Symbols (pinned) — no evacuation needed */
        /* glob_cache slots point into Boehm EnvFrame::vals — valid */
        break;
    }
    case T_UPVALUE: {
        Upvalue *uv = (Upvalue *)obj;
        uv->closed = evacuate(uv->closed);
        /* uv->location: either points into vm->stack (never in semispace) or
         * was already fixed up to &uv->closed during evacuation */
        /* uv->next: update if it was forwarded */
        uv->next = (Upvalue *)evacuate_obj(uv->next);
        break;
    }
    case T_ENV: {
        EnvFrame *f = (EnvFrame *)obj;
        /* syms[] are pinned Symbols — no evacuation needed */
        /* vals[] is a raw Boehm array — scan val_t entries in place */
        for (uint32_t i = 0; i < f->size; i++)
            f->vals[i] = evacuate(f->vals[i]);
        /* parent EnvFrame may have moved */
        f->parent = (EnvFrame *)evacuate_obj(f->parent);
        break;
    }
    case T_MODULE: {
        Module *m = (Module *)obj;
        m->name    = evacuate(m->name);
        m->exports = evacuate(m->exports);
        m->env     = (EnvFrame *)evacuate_obj(m->env);
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
        fprintf(stderr, "[gc_semispace] FATAL: unexpected type %u in to-space scan"
                " at %p\n", h->type, obj);
        abort();
    }
}

/* ── Scan pinned objects ─────────────────────────────────────────────────── */

/*
 * Scan the val_t fields of a pinned (Boehm-allocated) typed object.
 * These cross-heap references (Boehm → semispace) must be updated so they
 * point to the new to-space addresses after collection.
 */
static void scan_pinned_object(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {
    case T_ACTOR: {
        Actor *a = (Actor *)obj;
        a->closure = evacuate(a->closure);
        a->name    = evacuate(a->name);
        break;
    }
    case T_MAILBOX: {
        Mailbox *m = (Mailbox *)obj;
        for (size_t head = m->q.head; head != m->q.tail; head = (head + 1) % m->q.cap)
            m->q.msgs[head] = evacuate(m->q.msgs[head]);
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
    default:
        /* Types without val_t fields: Symbol, Bignum, Rational, Mpfr,
         * Port, Primitive, ForeignLib/Fn (handled in semispace scanner) */
        break;
    }
}

/* ── Collect ─────────────────────────────────────────────────────────────── */

/*
 * Stop-the-world Cheney collection.
 *  1. Evacuate all roots.
 *  2. Cheney scan loop.
 *  3. Scan pinned objects for cross-heap Boehm → semispace refs.
 *  4. Update raw C pointer roots.
 *  5. Swap spaces.
 */
static void ss_collect(void) {
    ss_collections++;
    ss_bytes_survived = 0;

    uint8_t *old_from_top = ss_top;   /* amount used in old from-space  */
    uint8_t *to_base = ss_to_base();
    ss_to_top   = to_base;
    ss_to_scan  = to_base;

    /* ── 1. Evacuate registered val_t roots ── */
    for (size_t i = 0; i < roots_count; i++) {
        if (roots[i])
            *roots[i] = evacuate(*roots[i]);
    }

    /* ── 2. Evacuate VM value-stack ranges ── */
    for (size_t i = 0; i < stack_count; i++) {
        val_t *base = stack_roots[i].base;
        val_t *sp   = *stack_roots[i].sp_ptr;
        for (val_t *slot = base; slot < sp; slot++)
            *slot = evacuate(*slot);
    }

    /* Scan the VM: value stack, call-frame closures, open upvalues. */
    if (vm) {
        /* Value stack — all live slots from bottom to sp. */
        for (val_t *slot = vm->stack; slot < vm->sp; slot++)
            *slot = evacuate(*slot);
        /* Raw C-pointer fields in call frames. */
        for (int fi = 0; fi < vm->frame_count; fi++)
            vm->frames[fi].closure = (BcClosure *)evacuate_obj(vm->frames[fi].closure);
        vm->open_upvalues = (Upvalue *)evacuate_obj(vm->open_upvalues);
        for (int hi = 0; hi < vm->handler_count; hi++)
            vm->handler_stack[hi].open_upvalues =
                (Upvalue *)evacuate_obj(vm->handler_stack[hi].open_upvalues);
    }

#define SS_DRAIN_LOOP() \
    while (ss_to_scan < ss_to_top) { \
        Hdr *_h = (Hdr *)ss_to_scan; \
        size_t _sz = obj_size(_h); \
        scan_object(ss_to_scan); \
        ss_to_scan += _sz; \
    }

    /* ── 3. Cheney scan loop (first drain) ── */
    SS_DRAIN_LOOP();

    /* ── 4. Scan pinned objects for cross-heap refs ── */
    for (size_t i = 0; i < pinned_count; i++) {
        void *obj = pinned_slots[i];
        if (obj) scan_pinned_object(obj);
    }

    /* ── 4b. External root scanners (module registry, etc.) ── */
    for (size_t i = 0; i < ext_scan_count; i++)
        ext_scanners[i]();

    /* ── 4c. Re-drain: pinned/ext scanners may have added objects ── */
    SS_DRAIN_LOOP();
#undef SS_DRAIN_LOOP

    /* ── 5. Update raw C pointer roots ── */
    for (size_t i = 0; i < rawptrs_count; i++) {
        if (rawptrs[i]) {
            void *p = *rawptrs[i];
            if (p) {
                Hdr *ph = (Hdr *)p;
                if (in_from_space(ph) && ph->type == GC_FORWARDED)
                    *rawptrs[i] = (void *)ph->fwd;
            }
        }
    }

    /* ── 6. Symbol table fixup ── */
    if (sym_fixup_cb) sym_fixup_cb();

    /* ── 7. Swap spaces ── */
    ss_from  ^= 1;
    ss_top    = ss_to_top;

    (void)old_from_top;   /* used for future debug-mode clearing */

    if (collection_hook) collection_hook();
}

/* ── vtable implementation ───────────────────────────────────────────────── */

static void *ss_alloc(size_t n, bool has_ptrs) {
    n = (n + 7u) & ~7u;
    if (ss_top + n > ss_from_base() + ss_size) {
        ss_collect();
        if (ss_top + n > ss_from_base() + ss_size) {
            /* Still no room after collection — fall back to Boehm */
            return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
        }
    }
    void *p = ss_top;
    ss_top += n;
    ss_bytes_allocated += n;
    /* Zero-initialise so Hdr.fwd starts as 0 */
    memset(p, 0, n);
    return p;
}

static void *ss_alloc_pinned(size_t n, bool has_ptrs) {
    void *obj = has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
    if (obj && has_ptrs)
        pinned_add(obj);
    return obj;
}

static void *ss_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}

static void ss_collect_op(void) { ss_collect(); }

static void ss_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    /* Per-thread nursery disabled under semispace — alloc goes through ss_alloc */
}

static void ss_pin(void *obj)              { (void)obj; /* objects pinned via alloc_pinned */ }
static void ss_unpin(void *obj)            { (void)obj; }

static void ss_register_root(void *slot) {
    if (roots_count == roots_cap) {
        size_t nc = roots_cap * 2;
        val_t **nr = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(val_t *));
        memcpy(nr, roots, roots_count * sizeof(val_t *));
        GC_FREE(roots);
        roots = nr; roots_cap = nc;
    }
    roots[roots_count++] = (val_t *)slot;
}

static void ss_unregister_root(void *slot) {
    for (size_t i = 0; i < roots_count; i++) {
        if (roots[i] == (val_t *)slot) {
            roots[i] = roots[--roots_count];
            return;
        }
    }
}

static size_t ss_heap_size(void) {
    return (size_t)(ss_top - ss_from_base());
}
static size_t ss_free_bytes(void) {
    return ss_size - (size_t)(ss_top - ss_from_base());
}

gc_ops_t gc_ss_ops = {
    .alloc             = ss_alloc,
    .alloc_pinned      = ss_alloc_pinned,
    .alloc_raw_pinned  = ss_alloc_raw_pinned,
    .collect           = ss_collect_op,
    .register_thread   = ss_register_thread,
    .pin               = ss_pin,
    .unpin             = ss_unpin,
    .register_root     = ss_register_root,
    .unregister_root   = ss_unregister_root,
    .heap_size         = ss_heap_size,
    .free_bytes        = ss_free_bytes,
};

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void gc_semispace_init(size_t space_bytes) {
    if (space_bytes == 0) space_bytes = GC_SS_SPACE_BYTES;
    ss_size = space_bytes;
    /* Ensure Boehm is initialised before any GC_MALLOC/GC_add_roots calls. */
    GC_INIT();
    GC_allow_register_threads();

    ss_space[0] = (uint8_t *)GC_MALLOC_UNCOLLECTABLE(ss_size);
    ss_space[1] = (uint8_t *)GC_MALLOC_UNCOLLECTABLE(ss_size);
    if (!ss_space[0] || !ss_space[1]) {
        fprintf(stderr, "[gc_semispace] FATAL: cannot allocate semispace (%zu MB each)\n",
                ss_size / (1024 * 1024));
        abort();
    }
    ss_from = 0;
    ss_top  = ss_space[0];

    /* Register the from-space with Boehm as a root so it conservatively keeps
     * any Boehm objects referenced from within the semispace alive. */
    GC_add_roots((char *)ss_space[0], (char *)(ss_space[0] + ss_size));
    GC_add_roots((char *)ss_space[1], (char *)(ss_space[1] + ss_size));

    /* Use GC_MALLOC_UNCOLLECTABLE for GC metadata arrays so Boehm never
     * collects them between a registration and the next semispace collection. */
    pinned_slots = GC_MALLOC_UNCOLLECTABLE(PINNED_INIT_CAP * sizeof(void *));
    memset(pinned_slots, 0, PINNED_INIT_CAP * sizeof(void *));
    pinned_cap   = PINNED_INIT_CAP;
    pinned_count = 0;

    roots     = GC_MALLOC_UNCOLLECTABLE(ROOTS_INIT_CAP * sizeof(val_t *));
    memset(roots, 0, ROOTS_INIT_CAP * sizeof(val_t *));
    roots_cap = ROOTS_INIT_CAP; roots_count = 0;

    stack_roots = GC_MALLOC_UNCOLLECTABLE(STACKS_INIT_CAP * sizeof(StackRoot));
    memset(stack_roots, 0, STACKS_INIT_CAP * sizeof(StackRoot));
    stack_cap   = STACKS_INIT_CAP; stack_count = 0;

    rawptrs    = GC_MALLOC_UNCOLLECTABLE(RAWPTRS_INIT_CAP * sizeof(void **));
    memset(rawptrs, 0, RAWPTRS_INIT_CAP * sizeof(void **));
    rawptrs_cap = RAWPTRS_INIT_CAP; rawptrs_count = 0;
}

/* ── Global gc_register_rawptr / gc_register_stack ── (override Boehm no-ops) */

/*
 * These are called by gc.c's no-op versions under Boehm.
 * Under semispace we shadow them from this translation unit, but since gc.c
 * defines the bodies unconditionally we can't easily override them at link
 * time.  Instead, these are separate implementations registered via an init
 * function; gc_semispace_activate() patches gc_ops.
 */

/* Called from gc_register_rawptr() — no-ops under Boehm; filled in gc.c */

void gc_ss_register_sym_fixup(void (*cb)(void)) {
    sym_fixup_cb = cb;
}

/* ── External root scanner registry ─────────────────────────────────────── */

void gc_ss_register_ext_scanner(void (*cb)(void)) {
    if (ext_scan_count < EXT_SCAN_MAX)
        ext_scanners[ext_scan_count++] = cb;
}

/* Public helpers used inside ext scanner callbacks. */
uintptr_t gc_ss_evac(uintptr_t v) { return (uintptr_t)evacuate((val_t)v); }
void     *gc_ss_fwd(void *p)      { return evacuate_obj(p); }

void gc_ss_set_hook(void (*hook)(void)) {
    collection_hook = hook;
}

GcSsStats gc_ss_stats(void) {
    return (GcSsStats){
        .collections     = ss_collections,
        .bytes_allocated = ss_bytes_allocated,
        .bytes_survived  = ss_bytes_survived,
        .from_used       = (size_t)(ss_top - ss_from_base()),
        .space_size      = ss_size,
        .pinned_count    = pinned_count,
    };
}
