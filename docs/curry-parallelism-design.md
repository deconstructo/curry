# Curry Parallelism Design Notes

*Captured from design session, June 2026. Intended as input to Curry 2 design and the Anarchist's Cookbook.*

---

## Background: ParaSail Review

ParaSail (Parallel Specification and Implementation Language, S. Tucker Taft / AdaCore) was reviewed as a reference point for parallelism models worth considering for Curry and Cill.

### What ParaSail Does

ParaSail's core move is *pervasive implicit parallelism achieved through structural simplification* rather than programmer annotation. Every expression has parallel evaluation semantics by default. `F(X) + G(Y)` is safe to evaluate in parallel because the language rules guarantee it — not because you annotated it. The compiler decides whether to actually spawn parallel activities based on complexity heuristics; a work-stealing scheduler maps those activities to physical cores.

Safety derives from three eliminations:

- No global variables
- No parameter aliasing (if either parameter is updatable)
- No re-assignable pointers

The net effect: the compiler can determine parallel safety from local analysis alone, without examining function bodies. Parameter passing uses *hand-off semantics* — when a variable is passed with read/write access, the caller may not manipulate it or pass it elsewhere until the function returns. Ownership transfer without Rust's syntax overhead.

Memory is region-based rather than a global GC heap. Work stealing uses a small number of heavyweight worker processes (~one per core), each with a LIFO picothread queue, periodically stealing from others in FIFO order (so stolen threads are those that have been waiting longest).

### What Is and Isn't Worth Importing

**Worth serious consideration:**

**Hand-off ownership semantics for actor message passing.** Curry's actor model handles isolation at the mailbox level, but formalising a hand-off discipline at the language level — where passing a value to an actor *moves* it rather than copying or sharing — would eliminate a class of race conditions by construction rather than convention. Cill especially, if it gets a static type system, could enforce this at compile time.

**Work-stealing scheduler for the actor system.** Per-actor LIFO queues with FIFO stealing gives better multicore utilisation without programmer involvement. Interaction with Boehm GC (pause behaviour across worker threads) needs thought but is tractable.

**No-global-variables as convention or lint advisory** in actor contexts, particularly for pedagogical purposes in the Cookbook.

**Not worth importing:**

The pointer-free region-based memory model is load-bearing for ParaSail's parallelism guarantees but is inseparable from the language being designed around it. It doesn't transplant onto a Scheme substrate with Boehm GC already in place. The module/interface system is Ada-flavoured OOP and doesn't map onto Curry's architecture usefully.

**Summary principle:** ParaSail's interesting insight is *achieve safety through subtraction, not addition*. Remove what makes parallelism unsafe and parallelism becomes the default rather than the exception. This is philosophically congenial to Curry's sensibility. The specific mechanisms — hand-off ownership, work-stealing — are portable ideas. The rest isn't.

---

## Parallel Argument Evaluation in S-Expressions

### The Problem

Given `(f a b c)`, we want to evaluate `a`, `b`, `c` in parallel before applying `f`. This is safe if and only if none of `a`, `b`, `c` share mutable state. In a pure functional subset that's trivially guaranteed. In Scheme with side effects it is not.

### The R7RS Gift

R7RS explicitly leaves argument evaluation order **unspecified**. Implementations may evaluate left-to-right, right-to-left, or in any order. This is deliberate — it leaves room for implementations to do something clever, including parallel evaluation, without violating the standard.

Parallel argument evaluation is therefore a *conforming implementation choice*, not a standards violation. The race condition problem is real but it is an implementation problem, not a standards problem.

### The AST Boundary

The design space is clean: **function application** gets the parallel treatment; **special forms** do not.

`(if a b c)`, `(and a b c)`, `(let ...)` etc. pass arguments unevaluated — the form itself controls evaluation. `b` and `c` in `(if a b c)` must not be evaluated until `a` is known. But these are syntactic forms, not function application, and the distinction is unambiguous in the AST.

So: function application → parallel candidate. Special form → sequential by definition. The AST already tells you which is which.

### Approaches

**Static dependency analysis at the form level.** Walk the form, build a dependency graph. Subexpressions sharing no free mutable variables — no common actors, no common mutable bindings — evaluate in parallel. Dependent subexpressions get sequenced. This is what ParaSail's compiler does, but it can do it statically because it has no mutable globals. In Curry this would be partially dynamic, at actor-local scope.

**Actor-boundary parallelism as the safe default.** Rather than parallelising within an expression, parallelise across actors. Argument expressions that are themselves actor sends or receives are already isolated by the mailbox discipline — spawn those concurrently, collect results. Conservative but compositionally safe, and it maps naturally onto Curry's existing actor system. The unit of parallelism is the message boundary, not the subexpression.

**Type-directed parallelism.** If Curry 2 acquires a partial or optional type system, tag values as pure or effectful. Pure subexpressions parallelise freely; effectful ones sequence. Used as a hint to the scheduler rather than a proof obligation.

**Speculative evaluation with rollback (STM).** Evaluate optimistically in parallel, track writes, detect conflicts, roll back and resequence on conflict. Expensive and complex. Probably overkill for Curry but worth knowing it exists.

**The most Curry-native answer** is actor-boundary parallelism — it is already the unit Curry's concurrency model thinks in, the safety guarantee is already there structurally, and it composes with the epistemological plugin system cleanly.

---

## Map-Reduce: Parallel by Default

### Map

`map` over a list is embarrassingly parallel by definition. Each application of `f` to an element is independent of every other by the *contract* of `map`. If `f` has side effects that create dependencies between elements, the programmer has violated the semantic contract of `map`. Parallel evaluation makes the misbehaviour visible faster — arguably a feature.

**Decision: `map` is parallel by default in Curry.**

This is conforming — R7RS makes no guarantees about evaluation order across elements for the same reason argument evaluation order is unspecified.

A sequential `map` is the special case that needs justifying, not the parallel one. If someone writes a Curry `map` with a side-effecting `f` and gets surprising results, the answer is: you used `map` wrong. `map` is a declaration that applications are independent. If they are not independent, use something else. The language teaches correct thinking by making the correct case fast and the incorrect case visible.

**Implementation shape:**

- Below a threshold (tunable, worth exposing as a parameter), `map` falls back to sequential evaluation — actor spawning overhead would dwarf the computation for small lists.
- Above the threshold, spawn actors, collect from mailbox, reassemble in order.
- Reassembly: each actor returns `(index . value)`; the collector reassembles by index. Simple and correct.

**Naming convention:**

- `map` — parallel (the default, the correct case)
- `map/seq` — sequential escape hatch for the rare case where evaluation order genuinely matters

**`for-each`:** same question applies; same answer.

### Reduce

Parallel `reduce` requires the combining function to be associative — which the programmer is implicitly asserting when using `reduce`. This gives a parallel reduction tree rather than a left fold, which changes the result if the operation is not associative. That is the programmer's contract to honour.

---

## Actor Model as Natural Parallelism Unit

The actor model is not just an abstraction — it is a practical parallelism tool. Each `map` application is a natural unit of actor work. Spawn an actor per element (above threshold), collect into a mailbox, done. The work-stealing scheduler handles load balancing. Parallel `map` follows almost for free from the actor infrastructure, without dependency analysis.

For the *Anarchist's Cookbook*: parallel `map` is a natural first concrete example of the actor model as performance tool rather than merely an abstraction. The reader sees embarrassing parallelism emerge from the structure of computation rather than being bolted on.

---

## Explicit Parallelism: `par`

Because Curry cannot make ParaSail's eliminations (no globals, no aliasing) without breaking Scheme compatibility, the honest position is:

- `par` as an explicit opt-in form — the programmer asserts "I know these are independent"
- Static analysis as an advisory layer that can *suggest* where `par` would be safe
- `map` parallel by default handles the overwhelming majority of the practical cases

---

## Open Questions

- Threshold value for `map` parallelism fallback: what is the right default, and should it be runtime-tunable or compile-time? *(resolved: tunable at runtime via `set-map-parallel-threshold!`, default 8)*
- Interaction of work-stealing scheduler with Boehm GC pause behaviour across worker threads. *(addressed below)*
- Whether `for-each` follows the same parallel-by-default decision as `map`. *(resolved: `for-each` stays sequential; `for-each/par` is explicit opt-in)*
- Hand-off ownership semantics: at the language level (Cill type system) or as a convention enforced by the actor runtime?
- Whether the epistemological plugin system (Nyāya, songline, whakapapa etc.) evaluators running as actors gives implicit parallelism across epistemological modules for free.

---

## Thread Pool + Work-Stealing Scheduler

*Design session, June 2026. Implementation target: Curry 0.9.x.*

### Status quo and its cost

The current parallel `map`/`reduce`/`for-each/par` implementation (introduced in 0.8.19–0.8.20) spawns N fresh pthreads per call and joins them on completion. Thread creation on Linux/macOS costs ~50–200 µs each; with 8 threads that is ~1 ms of overhead per `map` call regardless of list size, paid every time. For programs that call `map` in a loop — render loops, simulation ticks, server request processing — this dominates.

The fix is a **persistent thread pool**: create threads once at process startup, keep them alive between calls, and dispatch work via a queue rather than by spawning.

### Architecture

Two layers:

1. **Thread pool** — N persistent worker threads (N = `hw_concurrency()`), each owning a work deque and a `VM` + GC registration that persists for the process lifetime.

2. **Work-stealing scheduler** — when a worker's deque is empty, it steals work from another worker's deque rather than blocking. This eliminates static load imbalance (a slow chunk in one thread is picked up by idle threads) without programmer involvement.

The public API (`map`, `reduce`, `for-each/par`) is unchanged. The pool is an implementation detail.

### Data structures

```c
/* A single unit of work — function applied to a slice of an element array */
typedef struct WorkItem {
    val_t    fn;
    val_t   *elems;       /* NULL for reduce */
    val_t   *results;     /* NULL for for-each/par and reduce workers */
    int      start;
    int      end;
    bool     error;
    val_t    exn;
    /* completion bookkeeping (shared with dispatcher) */
    atomic_int     *n_done;
    pthread_mutex_t *done_mutex;
    pthread_cond_t  *done_cond;
} WorkItem;
```

**Chase-Lev work-stealing deque** (one per worker thread):

```c
typedef struct {
    _Atomic int64_t  top;     /* thieves steal from here (FIFO end) */
    _Atomic int64_t  bottom;  /* owner pushes/pops here  (LIFO end) */
    WorkItem       **buf;     /* power-of-2 circular buffer          */
    int64_t          mask;    /* buf_size - 1                        */
} WSDeque;
```

- Owner pushes and pops from `bottom` — no contention with thieves, no lock needed for the common case.
- Thieves CAS `top` — contention only between thieves, and only when a deque is nearly empty.
- When `bottom - top ≤ 1` the deque is empty or has one item; the owner uses a CAS to resolve the race with a potential thief.

**Thread pool:**

```c
typedef struct {
    int         n_workers;
    pthread_t  *threads;
    WSDeque    *deques;       /* one per worker */
    atomic_bool  shutdown;
    /* fallback: workers with empty deques park here */
    pthread_mutex_t park_mutex;
    pthread_cond_t  park_cond;
    atomic_int      n_parked;
} WorkPool;

static WorkPool *global_pool = NULL;  /* initialised in pool_init() */
```

### Lifecycle

**Initialisation** (`pool_init()`, called from `builtins_curry_register`):

```c
void pool_init(void) {
    int n = hw_concurrency();
    global_pool = /* allocate */;
    global_pool->n_workers = n;
    for (int i = 0; i < n; i++) {
        deque_init(&global_pool->deques[i], 256);   /* initial capacity */
        pthread_create(&global_pool->threads[i], NULL, worker_loop,
                       (void*)(intptr_t)i);
    }
}
```

Each worker calls `gc_register_thread()` and `vm_init()` once on startup, then enters its loop — these are never called again for the lifetime of the pool.

**Worker loop:**

```c
static void *worker_loop(void *arg) {
    int id = (int)(intptr_t)arg;
    gc_register_thread();
    vm_init();

    while (!atomic_load(&global_pool->shutdown)) {
        WorkItem *item = deque_pop(&global_pool->deques[id]);

        if (!item) {
            /* try to steal from a random other worker */
            item = steal_from_random(id);
        }

        if (item) {
            execute_item(item);   /* run fn over [start, end) */
        } else {
            /* no work anywhere — park on condvar */
            park(global_pool);
        }
    }
    return NULL;
}
```

**Dispatching a `map` call:**

```c
/* Split into OVERSUBSCRIPTION_FACTOR × n_workers chunks for finer granularity */
#define OVERSUBSCRIPTION_FACTOR 4

val_t parallel_map(val_t fn, val_t *elems, int n, val_t *results) {
    int nw    = global_pool->n_workers;
    int nchunks = min(n, nw * OVERSUBSCRIPTION_FACTOR);

    atomic_int n_done = 0;
    pthread_mutex_t done_mutex; pthread_mutex_init(&done_mutex, NULL);
    pthread_cond_t  done_cond;  pthread_cond_init(&done_cond,  NULL);

    /* Push chunks onto the *calling thread's* deque if it's a worker,
     * else distribute round-robin across all worker deques */
    int pos = 0;
    for (int c = 0; c < nchunks; c++) {
        int sz = (n - pos) / (nchunks - c);   /* even distribution */
        WorkItem *item = make_work_item(fn, elems, results,
                                        pos, pos+sz, &n_done,
                                        &done_mutex, &done_cond);
        deque_push(&global_pool->deques[c % nw], item);
        pos += sz;
    }

    /* Wake parked workers */
    pthread_cond_broadcast(&global_pool->park_cond);

    /* Wait for all chunks to complete */
    pthread_mutex_lock(&done_mutex);
    while (atomic_load(&n_done) < nchunks)
        pthread_cond_wait(&done_cond, &done_mutex);
    pthread_mutex_unlock(&done_mutex);
}
```

The oversubscription factor (4×) means there are more chunks than threads. If one chunk is slow (e.g., a deep mandelbrot region), idle threads steal its neighbours rather than sitting idle for the duration.

### GC interaction

Boehm GC stops all registered threads for collection. This is already handled correctly: pool threads call `gc_register_thread()` once at startup, so GC knows about them permanently. The concern from the original open questions — pause behaviour across worker threads — resolves cleanly because:

- Pool threads are always registered; no registration/deregistration per `map` call.
- During a GC stop-the-world, all pool threads are suspended at safe points (they're either blocked in `pthread_cond_wait` while parked, or executing Scheme via the evaluator which allocates through GC-visible paths).
- The `WorkItem` structs themselves are allocated via `gc_alloc` (interior pointers) so the GC can trace them.

One subtlety: `WorkItem` is stack-allocated in the current implementation. Moving to a pool means items need to live on the heap long enough for a thief to execute them. Allocate via `GC_NEW(WorkItem)` — the GC will keep them live as long as any deque references them.

### Exception propagation

Unchanged from the current model: each `WorkItem` has `bool error` and `val_t exn` fields. If a worker catches an exception, it sets these and signals completion normally. The dispatcher checks all items after `n_done == nchunks` and re-raises the first error it finds.

With stealing, the same item might be partially executed by a thief — but work items are indivisible (one item = one contiguous slice, executed atomically by whoever picks it up). There is no partial execution of a single item.

### Stealing strategy

`steal_from_random(id)` picks a victim thread at random (excluding self) and attempts `deque_steal`. On failure (empty or lost CAS race), it tries up to `n_workers - 1` victims before giving up and parking. Random victim selection is sufficient for the target workloads; round-robin or locality-aware strategies are not worth the complexity here.

### Granularity guidance for callers

The oversubscription factor of 4 is a compile-time constant, not user-tunable. The existing `map-parallel-threshold` controls whether the pool is used at all. These two knobs are independent:

- **Threshold**: is this list long enough to bother with parallelism? (default: 8 elements)
- **Oversubscription**: given we're going parallel, how many chunks? (fixed: 4× workers)

### Path to M:N actors

Once the pool exists, the actor system can be migrated onto it incrementally:

1. **Phase 1 (current):** actors are 1:1 OS threads. Pool is used only by `map`/`reduce`/`for-each/par`.
2. **Phase 2:** actors become lightweight coroutines. `spawn` pushes a coroutine onto the pool's deques rather than creating an OS thread. `receive` yields the coroutine back to the pool when no message is available. Pool workers run coroutines to completion or until they yield.
3. **Phase 3:** blocking I/O in actors is handled by a separate I/O thread that wakes the coroutine when data is available, rather than blocking a pool worker.

Phase 2 is the substantial rearchitecting previously deferred. Phase 1 (this design) is self-contained and delivers most of the performance benefit without touching the actor system.
