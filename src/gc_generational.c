/*
 * gc_generational.c — Two-generation GC for Curry Scheme.
 *
 * Milestones 1–4 implemented here.
 *
 * Architecture
 * ─────────────
 * Gen0 (nursery): single shared mmap region, mutex-protected bump pointer.
 *   All non-pinned heap objects are born here.  When the nursery fills,
 *   gen_minor_collect() is triggered.
 *
 * Gen1 (tenured): large mmap region (128 MB default).  Live nursery objects
 *   are promoted here during minor collection.  Major collection (milestone 6)
 *   will apply Cheney-copy to this region when it fills past 85%.
 *
 * Pinned objects: allocated via Boehm (GC_MALLOC) — never moved.  Their
 *   val_t fields are scanned during minor collection (same as semispace).
 *
 * Write barrier (card table): GC_WRITE_BARRIER macro in gc.h marks dirty
 *   cards when a tenured or Boehm object's val_t field is updated.  Minor
 *   collection scans dirty cards for old→young references.  Instrumentation
 *   of mutation sites is milestone 5.
 *
 * STW safepoints: polling (gc_stop_world flag).  Threads park at nursery
 *   exhaustion and actor receive/send! yield points.
 */

#define GC_THREADS
#include "gc_generational.h"
#include "gc.h"
#include "object.h"
#include "vm.h"
#include "env.h"
#include "symbol.h"
#include "chunk.h"
#include <gc/gc.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef BUILD_MPFR
#  include "mpfr_num.h"
#endif

/* ── Write barrier globals (extern in gc.h) ──────────────────────────────── */

volatile int  gc_gen_active       = 0;
uint8_t      *gc_gen_tenured_base  = NULL;
uint8_t      *gc_gen_tenured_limit = NULL;
uint8_t      *gc_gen_card_table    = NULL;

/* ── Nursery (Gen0) ──────────────────────────────────────────────────────── */

static uint8_t        *nursery_base  = NULL;
static size_t          nursery_size  = 0;
static uint8_t        *nursery_top   = NULL;
static pthread_mutex_t nursery_lock  = PTHREAD_MUTEX_INITIALIZER;

static inline bool in_nursery(const void *p) {
    return (const uint8_t *)p >= nursery_base &&
           (const uint8_t *)p <  nursery_base + nursery_size;
}

/* ── Tenured space (Gen1) ────────────────────────────────────────────────── */

static uint8_t        *tenured_top  = NULL;
static size_t          tenured_cap  = 0;
static size_t          card_count   = 0;
static pthread_mutex_t tenured_lock = PTHREAD_MUTEX_INITIALIZER;

void *gen_tenured_alloc(size_t n) {
    n = (n + 7u) & ~7u;
    pthread_mutex_lock(&tenured_lock);
    uint8_t *p = tenured_top;
    if (p + n > gc_gen_tenured_limit) {
        pthread_mutex_unlock(&tenured_lock);
        return NULL;
    }
    tenured_top += n;
    pthread_mutex_unlock(&tenured_lock);
    return p;
}

size_t gen_tenured_used(void) {
    return tenured_top ? (size_t)(tenured_top - gc_gen_tenured_base) : 0;
}

/* ── Pinned object list ───────────────────────────────────────────────────── */

#define PINNED_INIT_CAP 64
static void  **pinned_slots = NULL;
static size_t  pinned_count = 0;
static size_t  pinned_cap   = 0;
static pthread_mutex_t pinned_lock = PTHREAD_MUTEX_INITIALIZER;

static void pinned_add(void *obj) {
    pthread_mutex_lock(&pinned_lock);
    if (pinned_count == pinned_cap) {
        size_t new_cap = pinned_cap ? pinned_cap * 2 : PINNED_INIT_CAP;
        void **s = (void **)GC_MALLOC_UNCOLLECTABLE(new_cap * sizeof(void *));
        if (pinned_count) memcpy(s, pinned_slots, pinned_count * sizeof(void *));
        if (pinned_slots) GC_FREE(pinned_slots);
        pinned_slots = s;
        pinned_cap   = new_cap;
    }
    pinned_slots[pinned_count++] = obj;
    pthread_mutex_unlock(&pinned_lock);
}

/* ── VM registry ─────────────────────────────────────────────────────────── */

#define VM_REG_CAP 64
static VM     *gen_vms[VM_REG_CAP];
static int     gen_vm_count = 0;
static pthread_mutex_t vm_reg_lock = PTHREAD_MUTEX_INITIALIZER;

void gc_gen_register_vm(VM *v) {
    pthread_mutex_lock(&vm_reg_lock);
    if (gen_vm_count < VM_REG_CAP)
        gen_vms[gen_vm_count++] = v;
    pthread_mutex_unlock(&vm_reg_lock);
}

void gc_gen_unregister_vm(VM *v) {
    pthread_mutex_lock(&vm_reg_lock);
    for (int i = 0; i < gen_vm_count; i++) {
        if (gen_vms[i] == v) {
            gen_vms[i] = gen_vms[--gen_vm_count];
            break;
        }
    }
    pthread_mutex_unlock(&vm_reg_lock);
}

/* ── val_t root registry ─────────────────────────────────────────────────── */

#define ROOTS_INIT_CAP 64
static val_t **gen_roots     = NULL;
static size_t  gen_roots_count = 0;
static size_t  gen_roots_cap   = 0;
static pthread_mutex_t roots_lock = PTHREAD_MUTEX_INITIALIZER;

static void gen_register_root_impl(void *slot) {
    pthread_mutex_lock(&roots_lock);
    if (gen_roots_count == gen_roots_cap) {
        size_t nc = gen_roots_cap ? gen_roots_cap * 2 : ROOTS_INIT_CAP;
        val_t **r = (val_t **)GC_MALLOC_UNCOLLECTABLE(nc * sizeof(val_t *));
        if (gen_roots_count) memcpy(r, gen_roots, gen_roots_count * sizeof(val_t *));
        if (gen_roots) GC_FREE(gen_roots);
        gen_roots     = r;
        gen_roots_cap = nc;
    }
    gen_roots[gen_roots_count++] = (val_t *)slot;
    pthread_mutex_unlock(&roots_lock);
}

static void gen_unregister_root_impl(void *slot) {
    pthread_mutex_lock(&roots_lock);
    for (size_t i = 0; i < gen_roots_count; i++) {
        if (gen_roots[i] == (val_t *)slot) {
            gen_roots[i] = gen_roots[--gen_roots_count];
            break;
        }
    }
    pthread_mutex_unlock(&roots_lock);
}

/* ── Safepoint ────────────────────────────────────────────────────────────── */

volatile int gc_stop_world = 0;

static _Atomic int gc_gen_thread_count  = 0;
static _Atomic int gc_gen_parked_count  = 0;

static pthread_mutex_t gc_stw_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gc_stw_resume = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  gc_stw_parked = PTHREAD_COND_INITIALIZER;

void gc_gen_safepoint(void) {
    if (!gc_stop_world) return;
    pthread_mutex_lock(&gc_stw_mutex);
    if (gc_stop_world) {
        atomic_fetch_add(&gc_gen_parked_count, 1);
        pthread_cond_signal(&gc_stw_parked);
        while (gc_stop_world)
            pthread_cond_wait(&gc_stw_resume, &gc_stw_mutex);
        atomic_fetch_sub(&gc_gen_parked_count, 1);
    }
    pthread_mutex_unlock(&gc_stw_mutex);
}

void gc_gen_stop_the_world(void) {
    pthread_mutex_lock(&gc_stw_mutex);
    gc_stop_world = 1;
    int others = atomic_load(&gc_gen_thread_count) - 1;
    while (atomic_load(&gc_gen_parked_count) < others)
        pthread_cond_wait(&gc_stw_parked, &gc_stw_mutex);
}

void gc_gen_start_the_world(void) {
    gc_stop_world = 0;
    pthread_cond_broadcast(&gc_stw_resume);
    pthread_mutex_unlock(&gc_stw_mutex);
}

/* ── External root scanners ──────────────────────────────────────────────── */

#define MAX_EXT_SCANNERS 16
static void (*ext_scanners[MAX_EXT_SCANNERS])(void);
static int   ext_scanner_count = 0;

void gc_gen_register_ext_scanner(void (*cb)(void)) {
    if (ext_scanner_count < MAX_EXT_SCANNERS)
        ext_scanners[ext_scanner_count++] = cb;
}

/* ── Statistics ──────────────────────────────────────────────────────────── */

static uint64_t stat_minor        = 0;
static uint64_t stat_major        = 0;
static size_t   cfg_nursery_bytes = GC_NURSERY_DEFAULT_BYTES;
static size_t   stat_pinned_cnt   = 0;

static void (*gen_hook)(void) = NULL;

void gc_gen_set_hook(void (*hook)(void)) { gen_hook = hook; }
void gc_gen_bump_minor(void) { stat_minor++; if (gen_hook) gen_hook(); }
void gc_gen_bump_major(void) { stat_major++; if (gen_hook) gen_hook(); }

/* ── Object size (identical to semispace; must stay in sync with ObjType) ── */

static size_t obj_size(const Hdr *h) {
    switch (h->type) {
    case T_PAIR:         return (sizeof(Pair)        + 7u) & ~7u;
    case T_FLONUM:       return (sizeof(Flonum)      + 7u) & ~7u;
    case T_QUATERNION:   return (sizeof(Quaternion)  + 7u) & ~7u;
    case T_OCTONION:     return (sizeof(Octonion)    + 7u) & ~7u;
    case T_COMPLEX:      return (sizeof(Complex)     + 7u) & ~7u;
    case T_CPTR:         return (sizeof(CPtr)        + 7u) & ~7u;
    case T_PROMISE:      return (sizeof(Promise)     + 7u) & ~7u;
    case T_PARAMETER:    return (sizeof(Parameter)   + 7u) & ~7u;
    case T_SYMVAR:       return (sizeof(SymVar)       + 7u) & ~7u;
    case T_TRACED:       return (sizeof(Traced)       + 7u) & ~7u;
    case T_SYNTAX:       return (sizeof(Syntax)       + 7u) & ~7u;
    case T_ERROR:        return (sizeof(ErrorObj)     + 7u) & ~7u;
    case T_CONDITION:    return (sizeof(Condition)    + 7u) & ~7u;
    case T_RESTART:      return (sizeof(Restart)      + 7u) & ~7u;
    case T_CLOSURE:      return (sizeof(Closure)      + 7u) & ~7u;
    case T_MODULE:       return (sizeof(Module)       + 7u) & ~7u;
    case T_FOREIGN_LIB:  return (sizeof(ForeignLib)   + 7u) & ~7u;
    case T_FOREIGN_FN:   return (sizeof(ForeignFn)    + 7u) & ~7u;
    case T_SYMFN:        return (sizeof(SymFn)         + 7u) & ~7u;
    case T_CHUNK:        return (sizeof(Chunk)         + 7u) & ~7u;
    case T_UPVALUE:      return (sizeof(Upvalue)       + 7u) & ~7u;
    case T_ENV:          return (sizeof(EnvFrame)      + 7u) & ~7u;
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
        return ((sizeof(String) + s->len + 1u) + 7u) & ~7u;
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
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        return ((sizeof(Record) + nf * sizeof(val_t)) + 7u) & ~7u;
    }
    case T_SET:       return (sizeof(Set)       + 7u) & ~7u;
    case T_HASHTABLE: return (sizeof(Hashtable) + 7u) & ~7u;
#ifdef BUILD_MPFR
    case T_INTERVAL:  return (sizeof(Interval)  + 7u) & ~7u;
#endif
    default:
        fprintf(stderr, "[gc_gen] FATAL: unknown ObjType %u at %p\n",
                h->type, (void *)h);
        abort();
    }
}

/* ── Cheney evacuation ───────────────────────────────────────────────────── */

/* Tenured scan pointer — advances through newly-promoted objects during collection. */
static uint8_t *gen_to_scan;

/*
 * gen_evacuate — evacuate a nursery val_t to tenured space.
 * Leaves a GC_FORWARDED marker in the nursery copy.
 * Non-nursery val_t values are returned unchanged.
 */
static val_t gen_evacuate(val_t v) {
    if (!vis_ptr(v)) return v;
    Hdr *h = (Hdr *)((uintptr_t)v & ~(uintptr_t)3);
    if (!in_nursery(h)) return v;
    if (h->type == GC_FORWARDED)
        return vptr((void *)h->fwd);

    size_t sz = obj_size(h);
    void *new_obj = gen_tenured_alloc(sz);
    if (!new_obj) {
        /* Tenured full — leave in nursery; major GC will handle it (milestone 6). */
        return v;
    }
    memcpy(new_obj, h, sz);

    /* Upvalue interior pointer: if location pointed to &old->closed, redirect. */
    if (h->type == T_UPVALUE) {
        Upvalue *old_uv = (Upvalue *)h;
        Upvalue *new_uv = (Upvalue *)new_obj;
        if (old_uv->location == &old_uv->closed)
            new_uv->location = &new_uv->closed;
    }

    /* Mark as Gen1 */
    ((Hdr *)new_obj)->flags = (((Hdr *)new_obj)->flags & ~GC_AGE_MASK) | GC_AGE_GEN1;

    /* Forwarding marker */
    h->type = GC_FORWARDED;
    h->fwd  = (uintptr_t)new_obj;
    return vptr(new_obj);
}

/* Evacuate a raw C pointer to a nursery object (no tag bits). */
static void *gen_evacuate_obj(void *p) {
    if (!p) return p;
    Hdr *h = (Hdr *)p;
    if (!in_nursery(h)) return p;
    if (h->type == GC_FORWARDED) return (void *)h->fwd;

    size_t sz = obj_size(h);
    void *new_obj = gen_tenured_alloc(sz);
    if (!new_obj) return p;
    memcpy(new_obj, h, sz);

    if (h->type == T_UPVALUE) {
        Upvalue *old_uv = (Upvalue *)h;
        Upvalue *new_uv = (Upvalue *)new_obj;
        if (old_uv->location == &old_uv->closed)
            new_uv->location = &new_uv->closed;
    }

    ((Hdr *)new_obj)->flags = (((Hdr *)new_obj)->flags & ~GC_AGE_MASK) | GC_AGE_GEN1;
    h->type = GC_FORWARDED;
    h->fwd  = (uintptr_t)new_obj;
    return new_obj;
}

/* ── Object scanner (Cheney scan loop body) ──────────────────────────────── */

static void gen_scan_object(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {

    /* Atomic — no pointer fields */
    case T_FLONUM: case T_QUATERNION: case T_OCTONION:
    case T_F64VEC: case T_BYTEVECTOR: case T_SPINOR:
    case T_MULTIVECTOR: case T_MATRIX: case T_TENSOR:
    case T_STRING: case T_SYMBOL:
        break;

    case T_PAIR: {
        Pair *p = (Pair *)obj;
        p->car = gen_evacuate(p->car);
        p->cdr = gen_evacuate(p->cdr);
        break;
    }
    case T_COMPLEX: {
        Complex *c = (Complex *)obj;
        c->real = gen_evacuate(c->real);
        c->imag = gen_evacuate(c->imag);
        break;
    }
    case T_PROMISE: {
        Promise *p = (Promise *)obj;
        p->val = gen_evacuate(p->val);
        break;
    }
    case T_PARAMETER: {
        Parameter *p = (Parameter *)obj;
        p->init      = gen_evacuate(p->init);
        p->converter = gen_evacuate(p->converter);
        break;
    }
    case T_SYMVAR: {
        SymVar *sv = (SymVar *)obj;
        sv->name = gen_evacuate(sv->name);
        break;
    }
    case T_TRACED: {
        Traced *t = (Traced *)obj;
        t->proc = gen_evacuate(t->proc);
        t->name = gen_evacuate(t->name);
        break;
    }
    case T_SYNTAX: {
        Syntax *s = (Syntax *)obj;
        s->transformer = gen_evacuate(s->transformer);
        break;
    }
    case T_ERROR: {
        ErrorObj *e = (ErrorObj *)obj;
        e->message   = gen_evacuate(e->message);
        e->irritants = gen_evacuate(e->irritants);
        e->kind      = gen_evacuate(e->kind);
        break;
    }
    case T_CONDITION: {
        Condition *c = (Condition *)obj;
        c->type_sym = gen_evacuate(c->type_sym);
        c->fields   = gen_evacuate(c->fields);
        c->message  = gen_evacuate(c->message);
        break;
    }
    case T_RESTART: {
        Restart *r = (Restart *)obj;
        r->name        = gen_evacuate(r->name);
        r->description = gen_evacuate(r->description);
        r->thunk       = gen_evacuate(r->thunk);
        break;
    }
    case T_FOREIGN_LIB: {
        ForeignLib *fl = (ForeignLib *)obj;
        fl->path = gen_evacuate(fl->path);
        break;
    }
    case T_FOREIGN_FN: {
        ForeignFn *ff = (ForeignFn *)obj;
        ff->arg_tags = gen_evacuate(ff->arg_tags);
        ff->ret_tag  = gen_evacuate(ff->ret_tag);
        break;
    }
    case T_CPTR: break;

    case T_VECTOR: {
        Vector *v = (Vector *)obj;
        for (uint32_t i = 0; i < v->len; i++)
            v->data[i] = gen_evacuate(v->data[i]);
        break;
    }
    case T_JITCLOSURE: {
        JitClosure *j = (JitClosure *)obj;
        for (uint32_t i = 0; i < j->n_caps; i++)
            j->caps[i] = gen_evacuate(j->caps[i]);
        break;
    }
    case T_VALUES: {
        Values *v = (Values *)obj;
        for (uint32_t i = 0; i < v->count; i++)
            v->vals[i] = gen_evacuate(v->vals[i]);
        break;
    }
    case T_SYMEXPR: {
        SymExpr *e = (SymExpr *)obj;
        e->op = gen_evacuate(e->op);
        for (uint32_t i = 0; i < e->nargs; i++)
            e->args[i] = gen_evacuate(e->args[i]);
        break;
    }
    case T_SYMFN: {
        SymFn *sf = (SymFn *)obj;
        sf->name    = gen_evacuate(sf->name);
        sf->params  = gen_evacuate(sf->params);
        sf->parent  = gen_evacuate(sf->parent);
        sf->d_param = gen_evacuate(sf->d_param);
        break;
    }
    case T_SURREAL: {
        Surreal *s = (Surreal *)obj;
        int n = s->nterms * 2;
        for (int i = 0; i < n; i++)
            s->data[i] = gen_evacuate(s->data[i]);
        break;
    }
    case T_QUANTUM: {
        Quantum *q = (Quantum *)obj;
        int n = q->n * 2;
        for (int i = 0; i < n; i++)
            q->data[i] = gen_evacuate(q->data[i]);
        break;
    }
    case T_UP:
    case T_DOWN: {
        Tuple *t = (Tuple *)obj;
        for (uint32_t i = 0; i < t->len; i++)
            t->data[i] = gen_evacuate(t->data[i]);
        break;
    }
    case T_RECORD_TYPE: {
        RecordType *rt = (RecordType *)obj;
        rt->name = gen_evacuate(rt->name);
        for (uint32_t i = 0; i < rt->nfields; i++)
            rt->field_names[i] = gen_evacuate(rt->field_names[i]);
        break;
    }
    case T_RECORD: {
        Record *r = (Record *)obj;
        r->rtd = (RecordType *)gen_evacuate_obj(r->rtd);
        uint32_t nf = r->rtd ? r->rtd->nfields : 0;
        for (uint32_t i = 0; i < nf; i++)
            r->fields[i] = gen_evacuate(r->fields[i]);
        break;
    }
    case T_SET: {
        Set *s = (Set *)obj;
        for (uint32_t i = 0; i < s->cap; i++)
            s->buckets[i] = gen_evacuate(s->buckets[i]);
        break;
    }
    case T_HASHTABLE: {
        Hashtable *h2 = (Hashtable *)obj;
        for (uint32_t i = 0; i < h2->cap; i++) {
            h2->keys[i] = gen_evacuate(h2->keys[i]);
            h2->vals[i] = gen_evacuate(h2->vals[i]);
        }
        break;
    }
    case T_CLOSURE: {
        Closure *c = (Closure *)obj;
        c->params = gen_evacuate(c->params);
        c->body   = gen_evacuate(c->body);
        c->name   = gen_evacuate(c->name);
        c->env    = (EnvFrame *)gen_evacuate_obj(c->env);
        break;
    }
    case T_BCCLOSURE: {
        BcClosure *bc = (BcClosure *)obj;
        bc->jit_val = gen_evacuate(bc->jit_val);
        bc->chunk   = (Chunk *)gen_evacuate_obj(bc->chunk);
        for (int i = 0; i < bc->upval_count; i++)
            bc->upvals[i] = (Upvalue *)gen_evacuate_obj(bc->upvals[i]);
        break;
    }
    case T_CHUNK: {
        Chunk *ch = (Chunk *)obj;
        for (int i = 0; i < ch->const_len; i++)
            ch->constants[i] = gen_evacuate(ch->constants[i]);
        ch->src_lambda = gen_evacuate(ch->src_lambda);
        break;
    }
    case T_UPVALUE: {
        Upvalue *uv = (Upvalue *)obj;
        uv->closed = gen_evacuate(uv->closed);
        uv->next   = (Upvalue *)gen_evacuate_obj(uv->next);
        break;
    }
    case T_ENV: {
        EnvFrame *f = (EnvFrame *)obj;
        for (uint32_t i = 0; i < f->size; i++)
            f->vals[i] = gen_evacuate(f->vals[i]);
        f->parent = (EnvFrame *)gen_evacuate_obj(f->parent);
        break;
    }
    case T_MODULE: {
        Module *m = (Module *)obj;
        m->name    = gen_evacuate(m->name);
        m->exports = gen_evacuate(m->exports);
        m->env     = (EnvFrame *)gen_evacuate_obj(m->env);
        break;
    }
#ifdef BUILD_MPFR
    case T_INTERVAL: {
        Interval *iv = (Interval *)obj;
        iv->lo = gen_evacuate(iv->lo);
        iv->hi = gen_evacuate(iv->hi);
        break;
    }
#endif
    default:
        fprintf(stderr, "[gc_gen] FATAL: unexpected type %u in tenured scan at %p\n",
                h->type, obj);
        abort();
    }
}

/* ── Pinned object scanner (cross-heap: Boehm → nursery) ─────────────────── */

static void gen_scan_pinned(void *obj) {
    Hdr *h = (Hdr *)obj;
    switch (h->type) {
    case T_ACTOR: {
        Actor *a = (Actor *)obj;
        a->closure = gen_evacuate(a->closure);
        a->name    = gen_evacuate(a->name);
        break;
    }
    case T_MAILBOX: {
        Mailbox *m = (Mailbox *)obj;
        for (size_t head = m->q.head; head != m->q.tail;
             head = (head + 1) % m->q.cap)
            m->q.msgs[head] = gen_evacuate(m->q.msgs[head]);
        break;
    }
    case T_TVAR: {
        TVar *tv = (TVar *)obj;
        tv->value = gen_evacuate(tv->value);
        break;
    }
    case T_CHANNEL: {
        Channel *ch = (Channel *)obj;
        for (uint32_t i = 0; i < ch->cap; i++)
            ch->buf[i] = gen_evacuate(ch->buf[i]);
        break;
    }
    case T_CONTINUATION: {
        Continuation *cont = (Continuation *)obj;
        cont->result = gen_evacuate(cont->result);
        break;
    }
    default:
        break;
    }
}

/* ── Drain macro (Cheney scan loop) ─────────────────────────────────────── */

#define GEN_DRAIN_LOOP() \
    while (gen_to_scan < tenured_top) { \
        Hdr *_h = (Hdr *)gen_to_scan; \
        size_t _sz = obj_size(_h); \
        gen_scan_object(gen_to_scan); \
        gen_to_scan += _sz; \
    }

/* ── Minor collection ────────────────────────────────────────────────────── */

static void gen_minor_collect(void) {
    gc_gen_stop_the_world();

    stat_minor++;

    uint8_t *tenured_before = tenured_top;
    gen_to_scan = tenured_top;

    /* 1. Evacuate registered val_t* roots (GLOBAL_ENV etc.) */
    for (size_t i = 0; i < gen_roots_count; i++) {
        if (gen_roots[i])
            *gen_roots[i] = gen_evacuate(*gen_roots[i]);
    }

    /* 2. Evacuate all VM stacks and frame closures */
    for (int vi = 0; vi < gen_vm_count; vi++) {
        VM *v = gen_vms[vi];
        if (!v) continue;
        /* Value stack */
        for (val_t *slot = v->stack; slot < v->sp; slot++)
            *slot = gen_evacuate(*slot);
        /* Call frame raw closures */
        for (int fi = 0; fi < v->frame_count; fi++)
            v->frames[fi].closure = (BcClosure *)gen_evacuate_obj(v->frames[fi].closure);
        v->open_upvalues = (Upvalue *)gen_evacuate_obj(v->open_upvalues);
        for (int hi = 0; hi < v->handler_count; hi++)
            v->handler_stack[hi].open_upvalues =
                (Upvalue *)gen_evacuate_obj(v->handler_stack[hi].open_upvalues);
    }

    /* 3. First Cheney drain */
    GEN_DRAIN_LOOP();

    /* 4. Scan pinned objects for cross-heap Boehm → nursery refs */
    for (size_t i = 0; i < pinned_count; i++) {
        if (pinned_slots[i]) gen_scan_pinned(pinned_slots[i]);
    }

    /* 5. Scan remembered set (dirty cards in tenured space) */
    for (size_t ci = 0; ci < card_count; ci++) {
        if (!gc_gen_card_table[ci]) continue;
        uint8_t *card_start = gc_gen_tenured_base + (ci << GC_CARD_SHIFT);
        uint8_t *card_end   = card_start + GC_CARD_BYTES;
        if (card_end > tenured_before) card_end = tenured_before;
        /* Scan the card's memory for nursery-pointing val_t fields.
         * We conservatively scan every aligned 8-byte word as a potential val_t.
         * Objects in tenured space that point to nursery will have their
         * val_t fields updated when we call gen_evacuate on each candidate. */
        for (uint8_t *p = card_start; p < card_end; p += sizeof(val_t)) {
            val_t candidate = *(val_t *)p;
            if (vis_ptr(candidate)) {
                void *raw = (void *)((uintptr_t)candidate & ~(uintptr_t)3);
                if (in_nursery(raw)) {
                    val_t updated = gen_evacuate(candidate);
                    if (updated != candidate)
                        *(val_t *)p = updated;
                }
            }
        }
    }

    /* 6. External root scanners */
    for (int i = 0; i < ext_scanner_count; i++)
        ext_scanners[i]();

    /* 7. Re-drain after pinned/card/ext passes */
    GEN_DRAIN_LOOP();

    /* 8. Clear dirty cards (all tenured objects now have updated pointers) */
    memset(gc_gen_card_table, 0, card_count);

    /* 9. Zero and reset the nursery */
    memset(nursery_base, 0, nursery_size);
    nursery_top = nursery_base;

    if (gen_hook) gen_hook();

    gc_gen_start_the_world();

    (void)tenured_before;
}

/* ── Evacuation stub for ext scanners (milestone 4) ─────────────────────── */

uintptr_t gc_gen_evac(uintptr_t v) {
    return (uintptr_t)gen_evacuate((val_t)v);
}

/* ── gc_ops_t backend ────────────────────────────────────────────────────── */

static void *gen_alloc(size_t n, bool has_ptrs) {
    n = (n + 7u) & ~7u;
    pthread_mutex_lock(&nursery_lock);
    if (nursery_top + n > nursery_base + nursery_size) {
        pthread_mutex_unlock(&nursery_lock);
        /* Nursery full: trigger minor GC (also handles STW). */
        gen_minor_collect();
        pthread_mutex_lock(&nursery_lock);
        if (nursery_top + n > nursery_base + nursery_size) {
            /* Still no room — fall through to Boehm (tenured overflow, milestone 6). */
            pthread_mutex_unlock(&nursery_lock);
            return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
        }
    }
    void *p = nursery_top;
    nursery_top += n;
    pthread_mutex_unlock(&nursery_lock);
    memset(p, 0, n);  /* zero so Hdr.fwd starts as 0 */
    return p;
}

static void *gen_alloc_pinned(size_t n, bool has_ptrs) {
    void *p = has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
    if (p && has_ptrs) {
        stat_pinned_cnt++;
        pinned_add(p);
    }
    return p;
}
static void *gen_alloc_raw_pinned(size_t n, bool has_ptrs) {
    return has_ptrs ? GC_MALLOC(n) : GC_MALLOC_ATOMIC(n);
}
static void gen_collect(void) { gen_minor_collect(); }
static void gen_register_thread(void) {
    struct GC_stack_base sb;
    GC_get_stack_base(&sb);
    GC_register_my_thread(&sb);
    atomic_fetch_add(&gc_gen_thread_count, 1);
}
static void gen_pin(void *obj)   { (void)obj; }
static void gen_unpin(void *obj) { (void)obj; }
static void gen_write_barrier(void *obj) { (void)obj; }  /* fast path in macro */
static size_t gen_heap_size(void) {
    return (size_t)GC_get_heap_size() + gen_tenured_used();
}
static size_t gen_free_bytes(void) {
    return (size_t)GC_get_free_bytes() + (tenured_cap - gen_tenured_used());
}

void gc_gen_unregister_thread(void) {
    atomic_fetch_sub(&gc_gen_thread_count, 1);
}

gc_ops_t gc_gen_ops = {
    .alloc             = gen_alloc,
    .alloc_pinned      = gen_alloc_pinned,
    .alloc_raw_pinned  = gen_alloc_raw_pinned,
    .collect           = gen_collect,
    .register_thread   = gen_register_thread,
    .pin               = gen_pin,
    .unpin             = gen_unpin,
    .register_root     = gen_register_root_impl,
    .unregister_root   = gen_unregister_root_impl,
    .heap_size         = gen_heap_size,
    .free_bytes        = gen_free_bytes,
    .write_barrier     = gen_write_barrier,
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void gc_gen_init(size_t nursery_bytes, size_t tenured_bytes) {
    if (nursery_bytes == 0) nursery_bytes = GC_NURSERY_DEFAULT_BYTES;
    if (tenured_bytes == 0) tenured_bytes = GC_TENURED_DEFAULT_BYTES;

    cfg_nursery_bytes = nursery_bytes;
    nursery_size      = nursery_bytes;
    tenured_cap       = tenured_bytes;

    /* Nursery: single shared mmap, registered as Boehm root so pinned/Boehm
     * objects referenced from nursery are kept alive by Boehm's conservative scan. */
    void *n = mmap(NULL, nursery_bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (n == MAP_FAILED) { perror("[gc_gen] mmap nursery"); abort(); }
    nursery_base = (uint8_t *)n;
    nursery_top  = (uint8_t *)n;
    GC_add_roots((char *)n, (char *)n + nursery_bytes);

    /* Tenured region */
    void *t = mmap(NULL, tenured_bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (t == MAP_FAILED) { perror("[gc_gen] mmap tenured"); abort(); }
    gc_gen_tenured_base  = (uint8_t *)t;
    gc_gen_tenured_limit = (uint8_t *)t + tenured_bytes;
    tenured_top          = (uint8_t *)t;
    /* Register tenured as a Boehm root so Boehm-managed objects referenced
     * from tenured space are kept alive by Boehm's conservative scan. */
    GC_add_roots((char *)t, (char *)t + tenured_bytes);

    /* Card table */
    card_count        = (tenured_bytes + GC_CARD_BYTES - 1) >> GC_CARD_SHIFT;
    gc_gen_card_table = (uint8_t *)calloc(card_count, 1);
    if (!gc_gen_card_table) { perror("[gc_gen] card table"); abort(); }

    /* Pinned list */
    pinned_slots = (void **)GC_MALLOC_UNCOLLECTABLE(PINNED_INIT_CAP * sizeof(void *));
    pinned_cap   = PINNED_INIT_CAP;
    pinned_count = 0;

    /* val_t root list */
    gen_roots     = (val_t **)GC_MALLOC_UNCOLLECTABLE(ROOTS_INIT_CAP * sizeof(val_t *));
    gen_roots_cap = ROOTS_INIT_CAP;
    gen_roots_count = 0;

    gc_ops        = &gc_gen_ops;
    gc_gen_active = 1;

    /* Count the main thread. */
    atomic_fetch_add(&gc_gen_thread_count, 1);
}

/* ── Stats ────────────────────────────────────────────────────────────────── */

GcGenStats gc_gen_stats(void) {
    size_t nu = nursery_top ? (size_t)(nursery_top - nursery_base) : 0;
    return (GcGenStats){
        .minor_collections = stat_minor,
        .major_collections = stat_major,
        .nursery_bytes     = nursery_size,
        .nursery_used      = nu,
        .tenured_used      = gen_tenured_used(),
        .tenured_capacity  = tenured_cap,
        .pinned_count      = stat_pinned_cnt,
    };
}
