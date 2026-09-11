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
#include "env.h"         /* env_global_frame_lock_for_gc/unlock_for_gc, issue #198 */
#include "modules.h"      /* modules_registry_*lock_for_gc, issue #150 */
#include "vm.h"          /* VM struct, vm TLS pointer */
#include <gc/gc.h>
#include <gc/gc_mark.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <stdint.h>

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

/*
 * Issue #220 Candidate B: shared table of every live thread's nursery, so a
 * minor collection triggered on one thread can recognize and promote a
 * pointer that is resident in a DIFFERENT thread's nursery (not just its
 * own) -- e.g. a value an actor wrote into an already-tenured TVar's field,
 * or a value received via a mailbox message and bound to a local on the
 * receiving thread. Safe to read/copy directly from another thread's slab
 * here because gc_gen_stop_the_world() (issue #200 Phase B) genuinely
 * pauses every other registered thread for the full duration of a minor
 * collection -- see gc_gen_minor_collect()'s own comment on that bracket.
 *
 * Each entry stores a POINTER to the owning thread's own `gc_nursery` TLS
 * instance (taken from &gc_nursery while running ON that thread, inside
 * alloc_thread_nursery() below) rather than a copy of its bounds -- TLS
 * storage is ordinary per-thread-backed memory, valid to dereference from
 * any thread once its address is known, so this lets a reader always see
 * that thread's CURRENT base/top/limit (in particular `top`, which only
 * that thread's own future collection ever resets) without needing a
 * separate update path every time a thread allocates.
 *
 * The array only ever grows (GC_MALLOC_UNCOLLECTABLE + realloc-style
 * doubling under a lock, mirroring pinned_add's own history -- see that
 * function's comment on issue #205 for why a lock-free version of this
 * exact shape of growable-array-scanned-by-another-thread was tried once
 * already and found to have a real, reproducible data race). Entries are
 * NEVER removed on thread exit: gc_gen_unregister_thread()/
 * gc_gen_thread_leave() already deliberately leave a dead thread's nursery
 * slab allocated forever (see that function's own comment -- Boehm owns
 * the memory regardless via GC_MALLOC_UNCOLLECTABLE, and reclaiming it is
 * out of scope), so leaving the table entry inert costs nothing extra: a
 * dead thread's slab can never again receive a fresh allocation, so it can
 * only ever contain objects already forwarded (harmless re-hit -- GC_FORWARDED
 * short-circuits in evacuate()) or, in the pathological case of a thread
 * that exited while still holding the only reference to a value nobody else
 * ever forwards, an already-unreachable object nothing will ever ask about
 * again. Compacting the array on unregister would need the same lock this
 * read path takes on every table-fallback lookup, for no correctness
 * benefit -- not worth it.
 *
 * g_nursery_table_min/max is a global bounding box (min base, max limit)
 * updated whenever a new thread registers, checked BEFORE touching the
 * lock or walking the table -- evacuate()/in_nursery() fires on essentially
 * every live pointer during every minor collection, so the fallback lookup
 * below must stay cheap for the overwhelmingly common case (pointer isn't
 * in ANY nursery, or is in the collecting thread's own -- both handled by
 * cheaper checks before this is ever reached).
 */
#define THREAD_NURSERY_TABLE_INIT_CAP 64
typedef struct {
    GcNursery *nursery;  /* &gc_nursery, taken on the owning thread itself */
} ThreadNurseryEntry;

static ThreadNurseryEntry *g_thread_nurseries;
static size_t               g_thread_nurseries_count;
static size_t               g_thread_nurseries_cap;
static pthread_mutex_t      g_thread_nurseries_lock = PTHREAD_MUTEX_INITIALIZER;

static _Atomic uintptr_t g_nursery_table_min = (uintptr_t)UINTPTR_MAX;
static _Atomic uintptr_t g_nursery_table_max = 0;

static void register_thread_nursery(void) {
    pthread_mutex_lock(&g_thread_nurseries_lock);
    if (g_thread_nurseries_count == g_thread_nurseries_cap) {
        size_t nc = g_thread_nurseries_cap ? g_thread_nurseries_cap * 2
                                            : THREAD_NURSERY_TABLE_INIT_CAP;
        ThreadNurseryEntry *ns = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(ThreadNurseryEntry));
        if (!ns) { fprintf(stderr, "[gc_gen] FATAL: thread_nurseries OOM\n"); abort(); }
        if (g_thread_nurseries)
            memcpy(ns, g_thread_nurseries, g_thread_nurseries_count * sizeof(ThreadNurseryEntry));
        /* Safe to free: every reader/writer holds g_thread_nurseries_lock for
         * its entire touch of this array, mirroring pinned_add's own fix
         * (issue #205) for the identical shape of bug. */
        if (g_thread_nurseries) GC_FREE(g_thread_nurseries);
        g_thread_nurseries = ns;
        g_thread_nurseries_cap = nc;
    }
    g_thread_nurseries[g_thread_nurseries_count++].nursery = &gc_nursery;

    uintptr_t base  = (uintptr_t)gc_nursery.base;
    uintptr_t limit = (uintptr_t)gc_nursery.limit;
    uintptr_t old_min = atomic_load_explicit(&g_nursery_table_min, memory_order_relaxed);
    while (base < old_min &&
           !atomic_compare_exchange_weak_explicit(&g_nursery_table_min, &old_min, base,
                                                   memory_order_relaxed, memory_order_relaxed))
        ;
    uintptr_t old_max = atomic_load_explicit(&g_nursery_table_max, memory_order_relaxed);
    while (limit > old_max &&
           !atomic_compare_exchange_weak_explicit(&g_nursery_table_max, &old_max, limit,
                                                   memory_order_relaxed, memory_order_relaxed))
        ;
    pthread_mutex_unlock(&g_thread_nurseries_lock);
}

/*
 * Is `p` resident in some OTHER currently-registered thread's nursery slab
 * (the calling thread's own bounds are checked separately, and more
 * cheaply, by each caller before this is ever reached)? Cheap global range
 * pre-check first (single pair of atomic loads, no lock) so the common case
 * -- pointer isn't in any nursery, e.g. an ordinary Boehm-tenured object --
 * never touches g_thread_nurseries_lock at all.
 *
 * `precise` selects which bound to check a candidate thread's slab against:
 *
 *   - true  (used by in_nursery(), below, during a minor collection only):
 *     bound against that thread's CURRENT `top`. Every other registered
 *     thread is parked under stop-the-world for the full duration of a
 *     collection (see gc_gen_minor_collect()'s own comment), so reading
 *     another thread's `top` with a plain load is race-free and gives an
 *     exact answer -- important here because a false positive would hand
 *     evacuate() a bogus header to interpret from unallocated/zeroed nursery
 *     space (obj_size()/scan_object() would abort on the garbage type tag).
 *
 *   - false (used by gc_gen_ptr_in_any_nursery(), below, for gc_wb_slot's
 *     recording-side check -- issue #220 point 5): bound against that
 *     thread's `limit` instead. This runs from ordinary mutator code with
 *     NO stop-the-world in effect, so another thread's `top` can be
 *     concurrently advancing via its own lock-free bump-pointer allocator
 *     (gc_nursery_alloc's plain `gc_nursery.top = next`) -- reading it here
 *     would be a data race. `base`/`limit` are written exactly once, at
 *     that thread's own registration, and never touched again, so they are
 *     always safe to read cross-thread. Bounding against `limit` instead of
 *     `top` is over-inclusive (may treat some not-yet-allocated tail bytes
 *     as "nursery-resident"), but that is harmless here: gc_wb_slot never
 *     dereferences the header itself, it only records the SLOT address into
 *     gc_dirty_slots for evacuate() to re-examine later, under the precise/
 *     stop-the-world check above, at the next minor GC -- a spurious record
 *     just costs one redundant (and safe) evacuate() call on a value that
 *     turns out not to be nursery-resident after all. */
static bool ptr_in_other_thread_nursery(const void *p, bool precise) {
    uintptr_t addr = (uintptr_t)p;
    if (addr < atomic_load_explicit(&g_nursery_table_min, memory_order_relaxed) ||
        addr >= atomic_load_explicit(&g_nursery_table_max, memory_order_relaxed))
        return false;
    bool found = false;
    pthread_mutex_lock(&g_thread_nurseries_lock);
    for (size_t i = 0; i < g_thread_nurseries_count; i++) {
        GcNursery *gn = g_thread_nurseries[i].nursery;
        if (gn == &gc_nursery) continue;  /* self -- already checked by caller */
        const uint8_t *hi = precise ? gn->top : gn->limit;
        if ((const uint8_t *)p >= gn->base && (const uint8_t *)p < hi) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_thread_nurseries_lock);
    return found;
}

/* gc_wb_slot/gc_wb_slot_atomic_relaxed's recording-side check (src/gc.h,
 * issue #220 point 5): is `p` resident in ANY currently-registered thread's
 * nursery, including the calling thread's own? Previously that check only
 * recognized the MAIN thread's own nursery bounds (gc_main_nursery_base/
 * limit), so an actor writing a value resident in ITS OWN nursery into an
 * already-tenured slot (the exact shape of issue #220's repro) was silently
 * never recorded -- the dirty-slot mechanism is what makes a minor GC look
 * at that slot again at all (a "stable" pinned object is not re-scanned by
 * type), so under-recording here is NOT made moot by evacuate()'s own
 * any-thread fix above: that fix only helps once something actually asks
 * evacuate() to look at the slot's value again, and dirty-slot recording is
 * what causes that ask for an already-stable tenured object. The two
 * mechanisms are complementary, not redundant -- both are required. */
bool gc_gen_ptr_in_any_nursery(const void *p) {
    if ((const uint8_t *)p >= gc_nursery.base && (const uint8_t *)p < gc_nursery.limit)
        return true;
    return ptr_in_other_thread_nursery(p, false);
}

static void alloc_thread_nursery(void) {
    size_t sz = gen_nursery_size;
    uint8_t *slab = (uint8_t *)GC_MALLOC_UNCOLLECTABLE(sz);
    if (!slab) { fprintf(stderr, "[gc_gen] FATAL: nursery OOM\n"); abort(); }
    memset(slab, 0, sz);
    gc_nursery.base  = slab;
    gc_nursery.top   = slab;
    gc_nursery.limit = slab + sz;
    register_thread_nursery();
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
 * Issue #205: this used to have a lock-free fast path (pinned_count claimed
 * via atomic_fetch_add, the pointer written into pinned_slots without
 * holding pinned_lock at all) with the resize slow path rewriting
 * pinned_slots/pinned_cap under the lock. That shape had a real data race
 * (pinned_cap/pinned_slots read as plain globals) which a first attempt at
 * fixing -- making them _Atomic with an acquire/release pairing between the
 * two -- did close, but that rewrite introduced its own follow-on bug: a
 * resize's memcpy snapshot could run strictly between a fast-path write and
 * that thread's own post-write recheck, capturing the pre-write value, with
 * the resize's publish itself landing AFTER the recheck -- silently
 * dropping the written object from the pinned set forever (found by code
 * review, confirmed reproducible with only ordinary thread preemption, no
 * adversarial timing). A hand-rolled retry loop can be made to converge
 * against a MOVED pointer, but not against this "snapshot ran in between"
 * shape without reinventing a full seqlock protocol.
 *
 * Rather than get this exactly right with more lock-free machinery, this
 * function now just takes pinned_lock for the whole claim+write, mirroring
 * gc.c's g_roots (register_root_locked) -- the identical growable-
 * pointer-array-scanned-by-GC problem, already solved correctly there the
 * simple way. pinned_add is not on any path where lock contention matters
 * relative to the GC_MALLOC it accompanies (that allocation call itself
 * already dominates any cost here), so there is no performance reason to
 * prefer lock-free over provably correct.
 *
 * GC reset:
 *   gc_gen_minor_collect() holds pinned_lock around the step-6 scan and count
 *   reset (pinned_count = 0), the same lock pinned_add now holds for its
 *   entire body -- so there is no window where an add can land between the
 *   scan and the reset at all; the old "<10 CPU cycles, orphaned for one GC
 *   cycle" race this comment used to describe no longer exists.
 */
#define PINNED_INIT_CAP 256
static void          **pinned_slots;
static size_t           pinned_count;
static size_t           pinned_cap;
static size_t           pinned_stable;   /* objects below this index were fully
                                             scanned in the last minor GC */
static pthread_mutex_t pinned_lock = PTHREAD_MUTEX_INITIALIZER;

/* Exposed so env.c can register the root EnvFrame into the pinned scan list. */
void gc_gen_pin_permanent(void *obj);

static void pinned_add(void *obj) {
    pthread_mutex_lock(&pinned_lock);
    if (__builtin_expect(pinned_count >= pinned_cap, 0)) {
        size_t nc = pinned_cap * 2;
        void **ns = GC_MALLOC_UNCOLLECTABLE(nc * sizeof(void *));
        if (!ns) { fprintf(stderr, "[gc_gen] FATAL: pinned_slots OOM\n"); abort(); }
        memcpy(ns, pinned_slots, pinned_count * sizeof(void *));
        memset(ns + pinned_count, 0, (nc - pinned_count) * sizeof(void *));
        /* Safe to free here (unlike the lock-free design this replaced):
         * every reader and writer of pinned_slots, including this
         * function's own fast case below, holds pinned_lock for the
         * entire time it touches the array, so nothing can still be
         * referencing `pinned_slots` (the old array) once we're inside
         * this critical section. */
        GC_FREE(pinned_slots);
        pinned_slots = ns;
        pinned_cap   = nc;
    }
    pinned_slots[pinned_count++] = obj;
    pthread_mutex_unlock(&pinned_lock);
}

/* ── Nursery membership ───────────────────────────────────────────────────── */

static uint8_t *collect_top;  /* snapshot of nursery.top at collection start */

/*
 * Issue #220 Candidate B: fast path checks the COLLECTING thread's own
 * nursery first (cheap TLS reads, no lock) -- this is the overwhelmingly
 * common case (an object a thread itself allocated and is now evacuating
 * via its own roots) and MUST stay this cheap since it fires on essentially
 * every live pointer touched during every minor collection. Only on a miss
 * does this fall back to ptr_in_other_thread_nursery(), which checks every
 * OTHER live thread's nursery (see that function's own comment for why that
 * fallback itself stays cheap on ITS common case too). */
static inline bool in_nursery(const void *p) {
    if ((const uint8_t *)p >= gc_nursery.base && (const uint8_t *)p < collect_top)
        return true;
    return ptr_in_other_thread_nursery(p, true);
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
        return ((sizeof(String) + s->orig_cap + 1u) + 7u) & ~7u;
    }
    case T_BYTEVECTOR: {
        const Bytevector *b = (const Bytevector *)h;
        return ((sizeof(Bytevector) + b->len) + 7u) & ~7u;
    }
    case T_F64VEC: {
        const F64Vec *f = (const F64Vec *)h;
        return ((sizeof(F64Vec) + f->len * sizeof(double)) + 7u) & ~7u;
    }
    case T_TYPEDVEC: {
        const TypedVec *t = (const TypedVec *)h;
        return ((sizeof(TypedVec) + (size_t)t->len * tv_elem_size((TVKind)h->flags)) + 7u) & ~7u;
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
    default: {
        unsigned long raw[3];
        memcpy(raw, h, sizeof(raw));
        fprintf(stderr, "[gc_gen] FATAL: unknown GC:MOVE type %u at %p raw=[0x%lx,0x%lx,0x%lx]\n",
                h->type, (const void *)h, raw[0], raw[1], raw[2]);
    }
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
    case T_STRING: case T_CPTR: case T_TYPEDVEC:
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
    case T_STRING: case T_CPTR: case T_TYPEDVEC:
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
        e->backtrace = evacuate(e->backtrace);
        e->code      = evacuate(e->code);
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
    case T_PRIMITIVE: case T_MPFR:
        break;
    case T_JITCLOSURE: {
        /* caps[] is a mixed array: direct val_t captures and raw cell pointers
         * (Boehm heap addresses stored as uint64_t).  evacuate() is safe for
         * both: cell pointers are in Boehm (not nursery), so they pass through
         * unchanged; direct val_t caps are promoted normally. */
        JitClosure *j = (JitClosure *)obj;
        for (uint32_t i = 0; i < j->n_caps; i++)
            j->caps[i] = evacuate(j->caps[i]);
        break;
    }
    case T_RECORD_TYPE: {
        RecordType *rt = (RecordType *)obj;
        rt->name = evacuate(rt->name);
        rt->constructor = evacuate(rt->constructor);
        rt->predicate   = evacuate(rt->predicate);
        for (uint32_t i = 0; i < rt->nfields; i++) {
            rt->field_names[i] = evacuate(rt->field_names[i]);
            if (rt->accessors) rt->accessors[i] = evacuate(rt->accessors[i]);
            if (rt->mutators)  rt->mutators[i]  = evacuate(rt->mutators[i]);
        }
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
        /* Issue #198: a root (GLOBAL_ENV, or a define-library body's own
         * env_new_root() frame) is genuinely shared across actor threads
         * (see env.c's own seqlock, issue #153) -- this evacuation loop
         * used to read f->size and read/write f->vals[i] as plain fields
         * with no synchronization at all, racing a concurrent
         * frame_grow/frame_define/frame_set on whichever thread actually
         * OWNS/mutates this frame. This runs once per frame (the pinned-
         * object scan that fires on the NEXT minor GC after a frame is
         * created, per this file's own pinned_add/compact design) --
         * for a long-lived, actively-shared frame like GLOBAL_ENV, that
         * one scan can easily land well after actors are already running
         * and mutating it concurrently.
         *
         * Fixed by taking the SAME g_global_frame_lock frame_define/
         * frame_set already use (env_global_frame_lock_for_gc, env.h) --
         * safe against self-deadlock since minor GC only ever fires at an
         * explicit safepoint, never synchronously from inside a call
         * already holding that lock (see env.h's own doc comment on
         * these wrappers) -- which rules out any OTHER writer racing
         * size/vals for the loop's duration. A lock-free READER
         * (frame_lookup_versioned) can still be running concurrently
         * though, so each element is still read/written via atomic-
         * relaxed (not plain) to keep that side well-defined too, exactly
         * matching env.c's own gcache/vals-slot convention (relaxed
         * suffices: correctness against evacuation moving an object is
         * "reader sees the pre- or post-move pointer, either is a live,
         * valid reference to the same logical value," not something that
         * needs its own ordering -- from-space stays readable until the
         * whole collection cycle completes). Skip the store when nothing
         * moved, same as chunk.h's tree_eval_cache evacuation just above,
         * to avoid a redundant atomic write racing a concurrent reader
         * for no reason.
         *
         * LOCAL (non-root) frames are deliberately left on the original
         * plain, lock-free loop -- matching #153's own local-frame
         * exemption (single-owner, never raced by another MUTATOR
         * thread) -- see issue #198's follow-up note for why a local
         * frame's OWN one-time pinned-scan window is a separate, much
         * narrower/lower-probability concern not fixed here. */
        EnvFrame *f = (EnvFrame *)obj;
        if (f->parent == NULL) {
            env_global_frame_lock_for_gc();
            uint32_t n = f->size;
            val_t *vals = f->vals;
            for (uint32_t i = 0; i < n; i++) {
                _Atomic val_t *slot = (_Atomic val_t *)&vals[i];
                val_t old = atomic_load_explicit(slot, memory_order_relaxed);
                val_t moved = evacuate(old);
                if (moved != old)
                    atomic_store_explicit(slot, moved, memory_order_relaxed);
            }
            env_global_frame_unlock_for_gc();
        } else {
            for (uint32_t i = 0; i < f->size; i++)
                f->vals[i] = evacuate(f->vals[i]);
        }
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
        ch->src_lambda  = evacuate(ch->src_lambda);
        ch->target_env  = evacuate(ch->target_env);
        /* tree_eval_cache (chunk.h) holds real cached val_t RESULT
         * values (not raw pointers validated some other way, unlike
         * glob_cache, which is deliberately NOT evacuated here since its
         * slot pointers are re-validated via a version check instead) --
         * these must move with everything else. 0 is the "not yet
         * cached" sentinel (never a valid tagged val_t), skip it rather
         * than handing evacuate() a non-value bit pattern.
         *
         * This runs on the main thread mid-collection while OTHER actor
         * threads may be concurrently executing OP_TREE_EVAL_CACHED on
         * this exact chunk (chunks are shared verbatim across actors, see
         * vm_snapshot_closure_for_escape) -- gc_minor_pending is a
         * per-thread flag, so a minor collection here is NOT a
         * stop-the-world pause for other threads. Use the same
         * acquire/release atomics OP_TREE_EVAL_CACHED itself uses so a
         * concurrent reader either sees the pre- or post-evacuation
         * value, never a torn one. Skip the store entirely when
         * evacuation didn't move anything (the common case today: every
         * current caller of OP_TREE_EVAL_CACHED -- import/define-library/
         * library -- always evaluates to the immediate V_VOID, which
         * evacuate() passes through unchanged) to avoid a redundant
         * atomic write racing a concurrent reader for no reason. */
        if (ch->tree_eval_cache)
            for (int i = 0; i < ch->const_len; i++) {
                _Atomic val_t *slot = (_Atomic val_t *)&ch->tree_eval_cache[i];
                val_t old = atomic_load_explicit(slot, memory_order_acquire);
                if (old == 0) continue;
                val_t moved = evacuate(old);
                if (moved != old)
                    atomic_store_explicit(slot, moved, memory_order_release);
            }
        break;
    }
    case T_MODULE: {
        /* Issue #150: modules.c's modules_import reads mod->exports
         * outside module_registry_lock's normal critical sections
         * (registry_lookup/registry_insert/scan_module_registry) -- take
         * the same lock here (its write side, since this mutates) so
         * that read has something to synchronize against, mirroring
         * #198's identical fix for GLOBAL_ENV's frame_set/env_slot_load. */
        Module *m = (Module *)obj;
        modules_registry_wrlock_for_gc();
        m->name    = evacuate(m->name);
        m->exports = evacuate(m->exports);
        modules_registry_unlock_for_gc();
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
        /* Issues #144/#215/#217: this used to silently skip anything not
         * among the GC:PIN cases above ("conservative is safe") -- but as
         * of gc_nursery_refill()'s fallback fix (src/gc.c), pinned_slots can
         * now also hold ordinary GC:MOVE-tagged objects (T_PAIR, T_VECTOR,
         * ...) that escaped straight to Boehm because a minor collection
         * wasn't safe to run at allocation time. Those genuinely do have
         * val_t fields that may point into the nursery and need the exact
         * per-type evacuate() logic scan_object() already implements for
         * every GC:MOVE type -- "skip" was never actually safe for them,
         * it just happened not to matter until an object allocated this way
         * held a live nursery reference. scan_object()'s own default case
         * still aborts on a type unrecognized by EITHER switch, so this
         * isn't a silent behavior change for genuinely unexpected data. */
        scan_object(obj);
        break;
    }
}

/* ── gc_ss_evac/fwd bridge ───────────────────────────────────────────────── */

static uintptr_t gen_evac_fn(uintptr_t v) { return (uintptr_t)evacuate((val_t)v); }
static void     *gen_fwd_fn(void *p)      { return evacuate_raw(p); }

/* ── Safepoint: live thread count and stop-the-world handshake ───────────── */
/*
 * Issue #200 Phase A. Ported from gc_generational.c's safepoint core
 * (gc_generational.c:181-242) -- that file is an orphaned, never-built
 * earlier GC rewrite attempt (CMakeLists.txt only compiles gc.c and THIS
 * file), but its stop-the-world mechanism itself is architecture-agnostic
 * (it doesn't depend on that file's shared-nursery design) and is a clean
 * fit here, adapted to this file's actually-live per-thread-nursery model
 * and its own gc_inhibit_count (gc.c)/gc_minor_pending conventions rather
 * than duplicating them.
 *
 * This section is PLUMBING ONLY -- gc_stop_world is never set to 1 by
 * anything in this phase (that's Phase B, wiring it into
 * gc_gen_minor_collect()'s pinned-object-scan window so a scan of any
 * object reachable from more than one actor thread runs with every other
 * actor genuinely paused, closing the whole class of race #198/#150/#200
 * found piecemeal). Landing the plumbing on its own first: it's pure
 * addition with no behavior change (gc_gen_safepoint() is an always-false
 * fast-path check until Phase B), independently testable, and keeps this
 * change reviewable at the size the rest of this session's core-touching
 * work has been kept at.
 *
 * gc_gen_thread_count is the piece that didn't exist at all before this:
 * gen_register_thread (below) sets up a thread's nursery but never
 * tracked a live-thread count anywhere, and no actor-exit path called
 * anything symmetric to unregister one (see gc_gen_unregister_thread's
 * own comment). A safepoint's "has everyone else paused" wait needs an
 * accurate count to wait against.
 *
 * gc_stop_world/gc_gen_parked_count/gc_gen_thread_count are _Atomic
 * rather than plain, even though every real synchronization decision
 * still happens under gc_stw_mutex/the condvars -- gc_gen_safepoint()'s
 * fast path deliberately reads gc_stop_world OUTSIDE the mutex before
 * falling back to the locked slow path (the whole point of a cheap
 * poll), and a plain read racing a plain write from another thread is a
 * data race by the letter of C11 regardless of what the mutex elsewhere
 * guarantees -- the same class of issue #153/#198's fixes closed for
 * GLOBAL_ENV's seqlock-protected fields, so the same fix applies here.
 */
static _Atomic int gc_gen_thread_count = 0;
static _Atomic int gc_gen_parked_count = 0;
static _Atomic int gc_stop_world       = 0;

static pthread_mutex_t gc_stw_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gc_stw_resume = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  gc_stw_parked = PTHREAD_COND_INITIALIZER;

/* Called from the same poll points gc_minor_pending's self-collection
 * check already uses (vm.c's L_DISPATCH, eval.c's tail: label), under
 * the same gc_inhibit_count == 0 gate -- see those call sites' own
 * comments for why that gate is what makes this correct: a thread
 * mid-primitive-call (which may transiently hold a subsystem lock this
 * scan needs, e.g. rtab_lock/pinned_lock) is never asked to park until
 * it returns here with the inhibit count back at zero, so
 * gc_gen_stop_the_world()'s wait naturally waits out any in-flight
 * critical section instead of racing it. */
void gc_gen_safepoint(void) {
    if (!atomic_load_explicit(&gc_stop_world, memory_order_relaxed)) return;
    pthread_mutex_lock(&gc_stw_mutex);
    if (atomic_load_explicit(&gc_stop_world, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&gc_gen_parked_count, 1, memory_order_relaxed);
        pthread_cond_signal(&gc_stw_parked);
        while (atomic_load_explicit(&gc_stop_world, memory_order_relaxed))
            pthread_cond_wait(&gc_stw_resume, &gc_stw_mutex);
        atomic_fetch_sub_explicit(&gc_gen_parked_count, 1, memory_order_relaxed);
    }
    pthread_mutex_unlock(&gc_stw_mutex);
}

/* Requester side: block until every OTHER live registered thread has
 * reached gc_gen_safepoint() and parked (thread_count - 1, excluding the
 * calling/requesting thread itself). Not yet called from anywhere in
 * Phase A -- Phase B calls this around gc_gen_minor_collect()'s
 * pinned-object-scan window. */
void gc_gen_stop_the_world(void) {
    pthread_mutex_lock(&gc_stw_mutex);
    atomic_store_explicit(&gc_stop_world, 1, memory_order_relaxed);
    /* Recompute the target each iteration: gc_gen_thread_park() and
     * gc_gen_unregister_thread() can both decrement gc_gen_thread_count
     * while we wait (a thread that's about to block on a non-GC condvar,
     * or that's exiting for good, doesn't need to reach a safepoint at
     * all -- see their own comments), each signaling gc_stw_parked when
     * they do, so we re-check here rather than waiting on a target
     * that's gone stale. */
    while (atomic_load_explicit(&gc_gen_parked_count, memory_order_relaxed) <
           atomic_load_explicit(&gc_gen_thread_count, memory_order_relaxed) - 1)
        pthread_cond_wait(&gc_stw_parked, &gc_stw_mutex);
}

void gc_gen_start_the_world(void) {
    atomic_store_explicit(&gc_stop_world, 0, memory_order_relaxed);
    pthread_cond_broadcast(&gc_stw_resume);
    pthread_mutex_unlock(&gc_stw_mutex);
}

/* Shared by gc_gen_thread_park() and gc_gen_unregister_thread(): both need
 * to decrement gc_gen_thread_count and signal gc_stw_parked under
 * gc_stw_mutex, for the identical reason -- gc_gen_stop_the_world()'s wait
 * predicate (parked_count < thread_count - 1) is a function of this
 * counter, so any decrement can be the one that satisfies an already-
 * blocked collector's wait, and pairing the mutation with the signal under
 * the mutex the waiter also holds during pthread_cond_wait is what makes
 * that race-free. Issue #208: gc_gen_unregister_thread() used to skip the
 * signal half of this (a plain atomic decrement, no mutex, no signal) --
 * a classic missed-wakeup, confirmed via targeted tracing (register/park/
 * unpark/unregister events plus parked_count/thread_count at each
 * transition): thread_count dropped via exactly that unsignaled path
 * while a collector sat blocked in pthread_cond_wait with its predicate
 * already satisfied, and it never woke up -- reproduced as a genuine hang
 * (confirmed via sustained near-zero CPU usage, not just slowness) under
 * nested actor spawn combined with dynamic define-library/import. Having
 * both call sites share one implementation, rather than hand-duplicating
 * this exact lock/decrement/signal/unlock sequence, is what keeps them
 * from drifting apart into this same bug again later. */
static void gc_gen_thread_leave(void) {
    pthread_mutex_lock(&gc_stw_mutex);
    atomic_fetch_sub_explicit(&gc_gen_thread_count, 1, memory_order_relaxed);
    pthread_cond_signal(&gc_stw_parked);
    pthread_mutex_unlock(&gc_stw_mutex);
}

/* Bracket a genuinely long/unbounded blocking call (mailbox receive, a
 * blocking accept()/recv(), a work-queue park) with thread_park()/
 * thread_unpark(): a thread parked in a real blocking OS wait never
 * returns to a poll point on its own, so without this a
 * gc_gen_stop_the_world() request would wait for it indefinitely (e.g.
 * until a message arrives or a connection comes in, which may be never).
 * Safe to exclude such a thread from the count entirely while it's
 * blocked: it holds no Curry heap pointers that need protecting from a
 * concurrent scan beyond what's already reachable through the pinned
 * mailbox/socket object itself, since it isn't touching Curry objects at
 * all while parked in the syscall/condvar wait. */
void gc_gen_thread_park(void) {
    gc_gen_thread_leave();
}

/* Re-register BEFORE calling gc_gen_safepoint(): if the safepoint call
 * came first, a stop_the_world() that starts in the window between it
 * returning and the count increment below would proceed without waiting
 * for this (now-running-again) thread. Incrementing first means any such
 * request either starts before the increment (and this thread's park()
 * call already excluded it, correctly) or starts after (and now correctly
 * waits for it via gc_gen_safepoint() below). */
void gc_gen_thread_unpark(void) {
    atomic_fetch_add_explicit(&gc_gen_thread_count, 1, memory_order_relaxed);
    gc_gen_safepoint();
}

/* Actor exit has no symmetric call to gen_register_thread's nursery setup
 * today (src/actors.c's cleanup path never called anything to undo it) --
 * this doesn't reclaim the nursery slab (out of scope for Phase A, and
 * Boehm already owns that memory via GC_MALLOC_UNCOLLECTABLE regardless),
 * just keeps gc_gen_thread_count accurate so a future stop_the_world()'s
 * wait target reflects threads that are actually still live. See
 * gc_gen_thread_leave()'s own comment (issue #208) for why this must
 * signal gc_stw_parked, not just decrement the counter. */
void gc_gen_unregister_thread(void) {
    gc_gen_thread_leave();
}

/* ── Minor GC ────────────────────────────────────────────────────────────── */

static pthread_mutex_t minor_gc_lock = PTHREAD_MUTEX_INITIALIZER;

void gc_gen_minor_collect(void) {
    /* Issue #200 Phase B: a thread blocked here waiting to become the next
     * collector (minor_gc_lock is already held by whichever thread is
     * mid-collection) is NOT at a safepoint poll point any more -- it left
     * L_DISPATCH/tail: to make this call. Without parking across the
     * acquisition specifically, the ALREADY-collecting thread's own
     * gc_gen_stop_the_world() (below) would wait forever for this thread
     * to park, since this thread can't reach another poll point until it
     * gets the lock, and it can't get the lock until the collector
     * finishes -- which won't happen until its stop_the_world() wait is
     * satisfied. Parking here breaks that cycle: this thread is excluded
     * from the live-thread count for exactly as long as it's blocked
     * waiting to become the collector, then re-included once it actually
     * has the lock (at which point, if a stop-the-world happens to be
     * active from some other collector that started in the meantime,
     * gc_gen_thread_unpark()'s own gc_gen_safepoint() call correctly
     * parks again -- impossible in practice since minor_gc_lock has at
     * most one holder, so only the thread that itself just acquired it
     * could be mid-stop_the_world, and a thread never waits on itself). */
    gc_gen_thread_park();
    pthread_mutex_lock(&minor_gc_lock);
    gc_gen_thread_unpark();

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

    /* Issue #200 Phase B: stop the world before ANY evacuation step, not just
     * steps 6-8. TSan's stress repro (16 actors, --gc-nursery-size 4K)
     * disproved the original placement here: step 3's root scan writes
     * `*g_roots[i] = evacuate(*g_roots[i])`, and GLOBAL_ENV is one of those
     * roots. Even though the EnvFrame it points to is pinned (so the write
     * stores back the same pointer value), it is still an unsynchronized
     * write racing every other actor thread's lock-free `load_global_cached`
     * read of that same global -- a data race in the C memory model
     * regardless of whether the value actually changes. Steps 1-2 (this
     * thread's own shadow/VM stack) don't need protection on their own, but
     * there's no benefit to carving them out of the bracket, and doing so
     * previously hid this exact bug -- so the whole collection now runs
     * start-to-finish between stop_the_world()/start_the_world(). */
    gc_gen_stop_the_world();

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

    /* 3. Evacuate registered val_t roots.
     * Hold g_roots_lock from here through step 9 (nursery reset).
     *
     * Invariant required: no new root may be registered with a nursery
     * address while the GC is between the root scan and the nursery reset.
     * If a concurrent gc_register_root_val() stored a nursery address after
     * we scanned roots but before we reset the nursery, that address would
     * become a dangling pointer the moment the nursery slab is reused.
     * Holding g_roots_lock for the entire critical section prevents this:
     * concurrent callers block until AFTER the nursery is reset, at which
     * point any fresh allocation they make comes from the clean nursery. */
    gc_roots_lock();
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
    /* Issue #200 Phase B: steps 6-8 below are the ones that touch state
     * genuinely reachable from more than one actor thread -- dirty tenured
     * slots (mutations to promoted objects since the last cycle), pinned
     * objects (EnvFrame/Module/Actor/Mailbox/Upvalue/... -- anything with
     * gc_alloc_pinned's own val_t fields), and every registered ext
     * scanner (rtab/atab, the module registry). Every one of those was,
     * before this issue, scanned/evacuated here while every OTHER actor
     * thread kept running and could concurrently mutate the exact same
     * object -- the root cause behind #198/#150/#146/#200's own text and
     * the four object types (T_UPVALUE/T_BCCLOSURE/T_ACTOR/T_MAILBOX)
     * #200 found still unprotected after those three per-type fixes.
     * Phase A (already landed) built the mechanism; the stop_the_world()
     * call up near GC_disable() (see its own comment) is what actually
     * engages it for the whole collection, steps 6-8 included: every other
     * registered thread is genuinely paused (via gc_gen_safepoint()'s poll
     * points in vm.c/eval.c) for the duration, so no concurrent mutation is
     * possible regardless of object type -- closing the whole class at once
     * rather than needing a bespoke lock per type. The existing per-type
     * locks from #198/#150 (env_global_frame_lock_for_gc,
     * modules_registry_*lock_for_gc) are now redundant under this bracket
     * but are deliberately left in place rather than removed -- harmless
     * defense-in-depth, and removing them is its own, separately-reviewable
     * follow-up, not bundled into the PR that first activates
     * stop-the-world. */

    pthread_mutex_lock(&gc_dirty_lock);
    bool dirty_overflow_snapshot = gc_dirty_overflow;
    pthread_mutex_unlock(&gc_dirty_lock);

    if (dirty_overflow_snapshot) {
        pthread_mutex_lock(&pinned_lock);
        size_t pc = pinned_count;
        void **slots = pinned_slots;
        for (size_t i = 0; i < pc; i++) {
            void *obj = slots[i];
            if (obj) scan_pinned_object(obj);
            slots[i] = NULL;  /* release reference so Boehm can reclaim dead objects */
        }
        pinned_count  = 0;
        pinned_stable = 0;
        pthread_mutex_unlock(&pinned_lock);
    } else {
        /* Update the recorded dirty slots in-place. Issue #213: gc_dirty_lock
         * held here to match "always touched under this lock" now that
         * gc_wb_slot/gc_wb_slot_atomic_relaxed's bookkeeping isn't provably
         * main-thread-only -- see gc_dirty_lock's own declaration comment.
         * Not strictly required for THIS particular read given issue #200
         * Phase B's stop-the-world already excludes every other thread by
         * the time this runs, but costs nothing extra on the already-rare
         * collection path. */
        pthread_mutex_lock(&gc_dirty_lock);
        for (size_t i = 0; i < gc_dirty_count; i++)
            *gc_dirty_slots[i] = evacuate(*gc_dirty_slots[i]);
        pthread_mutex_unlock(&gc_dirty_lock);
        /* Scan new pinned objects (those added since the last compact). */
        pthread_mutex_lock(&pinned_lock);
        size_t pc = pinned_count;
        void **slots = pinned_slots;
        for (size_t i = pinned_stable; i < pc; i++) {
            void *obj = slots[i];
            if (obj) scan_pinned_object(obj);
        }
        /* Null ALL slots [0, pc) so Boehm can reclaim dead objects.  The stable
         * range [0, pinned_stable) was already scanned in a prior cycle but still
         * holds stale pointers; clearing them is necessary now that pinned objects
         * are GC_MALLOC (not GC_MALLOC_UNCOLLECTABLE). */
        memset(slots, 0, pc * sizeof(void *));
        /* Compact: future mutations tracked via dirty_slots. */
        pinned_count = 0;
        pinned_stable = 0;
        pthread_mutex_unlock(&pinned_lock);
    }
    pthread_mutex_lock(&gc_dirty_lock);
    gc_dirty_count    = 0;
    gc_dirty_overflow = false;
    pthread_mutex_unlock(&gc_dirty_lock);

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

    /* Issue #200 Phase B: every other thread resumes here -- see
     * gc_gen_stop_the_world()'s own call site comment above for the full
     * scope of what's protected between these two calls. */
    gc_gen_start_the_world();

    gc_evac_fn = NULL;
    gc_fwd_fn  = NULL;

    /* 9. Reset nursery — still holding g_roots_lock (acquired at step 3).
     * The lock prevents concurrent gc_register_root_val from storing a
     * nursery address between the root scan and the reset; callers unblock
     * after the reset and allocate from the clean (fresh) nursery slab. */
    gc_nursery.top = gc_nursery.base;

    /* Sync root shadows before Boehm's major GC can see them, then release
     * the roots lock so blocked registrations can proceed. */
    for (size_t i = 0; i < g_roots_count; i++)
        g_roots_shadow[i] = *g_roots[i];
    gc_roots_unlock();

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
    /* Issue #200 Phase A: see gc_gen_thread_count's own declaration
     * comment. Paired with gc_gen_unregister_thread(), this backend's
     * gc_ops_t.unregister_thread implementation -- reached only through
     * the backend-dispatched public gc_unregister_thread() (src/gc.c),
     * called from every registered thread's exit path (src/actors.c,
     * src/workpool.c, modules/mcp/mcp.c), never called directly. */
    atomic_fetch_add_explicit(&gc_gen_thread_count, 1, memory_order_relaxed);
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
    .unregister_thread = gc_gen_unregister_thread,
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

    /* Issue #200 Phase A: the main thread never goes through
     * gen_register_thread (it sets up its own nursery directly, above),
     * so it needs its own count increment here to be included in
     * gc_gen_thread_count -- see that variable's declaration comment. */
    atomic_fetch_add_explicit(&gc_gen_thread_count, 1, memory_order_relaxed);
}

/*
 * Add an externally-allocated object (e.g. GC_MALLOC_UNCOLLECTABLE) to the
 * pinned scan list so scan_pinned_object keeps its val_t fields current during
 * minor GC.  Called from env.c for the root EnvFrame.
 */
void gc_gen_pin_permanent(void *obj) {
    if (obj) pinned_add(obj);
}
