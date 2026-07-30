# Curry — Implementation Roadmap

*Drafted 2026-06-06. Updated 2026-06-14 for v1.5.0. Corrected 2026-07-17
against actual shipped state through v1.8.0 — see "Where we are now" and
"Decided against" below. Source: cill_spec.pdf + design sessions.*

Curry is at v1.8.0 (released). Since this roadmap was last updated: Phase 4
(Extensible CAS) finished completely, generational GC shipped as an
experimental opt-in backend, a `(curry sets)`/`(curry logic)` pair of
modules realized part of Phase 7's pluggable-foundations idea in pure
Scheme, real error backtraces + machine-legible error codes landed, and an
interactive debugger shipped standalone ahead of Phase 8. Some things were
also explicitly decided *against* rather than simply not gotten to yet —
see the "Decided against" section before the summary timeline. This
document maps the path from here to a compiled Scheme for scientific
computing — the full cill specification. It is ordered by dependency, not
ambition; each phase unblocks the phases above it.

---

## Where we are now (v1.8.0)

| Capability | Status |
|---|---|
| R7RS base + numeric tower (fixnum → octonion → symbolic CAS) | ✓ complete |
| Bytecode compiler + VM | ✓ complete |
| LLVM ORC v2 JIT (tiered) | ✓ complete |
| Actors, STM, CSP channels | ✓ complete |
| Parallel map/reduce (Chase-Lev work-stealing) | ✓ complete |
| Qt6, PLplot visualisation | ✓ complete |
| Matrices, tensors (+ transpose/contract/einsum), spinors | ✓ complete |
| Basic profiling (call counts, wall-clock) | ✓ basic |
| CAS rule engine (internal, not user-extensible) | ✓ partial |
| Polynomial ops (expand, collect, degree) | ✓ partial |
| **CL-style condition system with restarts** | ✓ **v1.1.0** |
| **General C FFI (libffi, zero-copy matrix/tensor)** | ✓ **v1.1.0** (`BUILD_FFI=ON`) |
| **SRFI compatibility layer (`srfi s1`, `srfi s27`)** | ✓ **v1.1.1** |
| **MPFR arbitrary-precision floats + interval arithmetic** | ✓ **v1.2.0** (`BUILD_MPFR=ON`) |
| **Number theory** (primality, factoring, modular arithmetic, combinatorics, CF) | ✓ **v1.2.0** |
| **FFI design guidance** (when to use FFI vs C module) | ✓ **v1.2.1** (docs) |
| **Sexagesimal / Babylonian number system** | ✓ **v1.2.5** |
| **Cheney semispace GC** (`--gc semispace`, `gc-collect!`, `gc-stats`) | ✓ **v1.3.0** |
| User rewrite rules (`define-rule`, `define-ruleset`) | ✓ **v1.4.0 Phase 4a** |
| User algebra declarations (`define-algebra`, `with-assumptions`) | ✓ **v1.4.0 Phase 4b** |
| **Extensible CAS — polynomial, Groebner, Risch (partial), special functions** | ✓ **v1.4.0 Phase 4c–4g, complete** |
| **Generational GC** (nursery+tenured Cheney, write barriers, safepoints) | ✓ **v1.5.0–1.6.3, experimental** (`--gc generational`; Boehm remains default) |
| **Real error backtraces + machine-legible error codes** | ✓ **v1.7.0** |
| **`(curry sets)` + `(curry logic)` modules** (multisets, 6 pluggable logics) | ✓ **v1.7.0** — partial, differently-shaped realization of "pluggable set theory foundations" below |
| **Interactive debugger** (breakpoints, step/next/finish/continue, locals, backtrace) | ✓ **v1.8.0** — a slice of Phase 8 below, arrived standalone/early |
| Green threads | ✗ |
| Hot code reloading | ✗ |
| Pluggable scheduler | ✗ |
| Slim CLOS | ✗ |
| Pluggable set theory foundations (C-level `curry_foundations_ops_t`) | ✗ (see `(curry sets)`/`(curry logic)` above for the Scheme-level equivalent) |
| Topology | ✗ |
| GR / QM libraries | ✗ |
| Full introspection (heap-walk, compiler IR, actor debug) | ✗ (debugger above covers breakpoints/locals only) |
| Sampling profiler, Tracy/Perfetto tracing, SIMD tower | ✗ |
| Property-based testing | ✗ |
| Scientific I/O (HDF5, NetCDF, FITS) | ✗ |
| Package manager | ✗ **deferred by decision**, not just unstarted — see `docs/pkg-design.md` and "Decided against" below |
| Notebook interface | ✗ |
| Units and dimensions system | ✗ |
| Step-by-step CAS (`explain-simplify`) | ✗ |
| LLM-integrated notebook | ✗ (LLM module exists; notebook does not yet) |
| Exploration sharing (`.curry-nb` format) | ✗ |

---

## Phase 1 — v1.1: Condition System + FFI ✓ done

*Shipped in v1.1.0 (2026-06-06). v1.1.1 added SRFI compatibility (`srfi s1
lists`, `srfi s27 random-bits`). The two features that unblock everything
else. No new dependencies for the condition system; LIBFFI for FFI.*

**1a. Common Lisp condition system with restarts**

Replace `setjmp`/`longjmp` error handling with a proper CL-style condition
system. `signal` does not unwind — it walks the handler chain looking for a
claim. `error` signals then unwinds if unclaimed. `with-restarts` establishes
named recovery options at the point of failure; `invoke-restart` selects one
from a handler without unwinding to it.

```scheme
(with-restarts
  [('use-pseudoinverse  (lambda () (pseudoinverse M)))
   ('use-identity       (lambda () (matrix-identity (matrix-rows M))))]
  (matrix-inverse M))

(handler-bind
  [(singular-matrix (lambda (c) (invoke-restart 'use-pseudoinverse)))]
  (run-geodesic-integrator 10000))
```

Key for scientific computing: a 40-hour simulation that hits a singular matrix
at step 9999 should not discard its state. `handler-bind` + `invoke-restart`
makes that a two-line fix instead of an architectural problem.

Deliverables: `define-condition`, `make-condition`, `signal`, `error`, `warn`,
`with-restarts`, `handler-bind`, `handler-case`, `ignore-errors`,
`invoke-restart`, `find-restart`, `condition-backtrace`.

**1b. General FFI with zero-copy tensor/matrix passthrough**

`define-foreign-library`, `define-foreign`, type mapping table, callback
trampolines. Dense tensors and matrices pass their `double*` directly to C
with the GC pinning the region for the duration of the call — no copy, no
marshal. This turns BLAS, LAPACK, GSL, HDF5, NetCDF, and FITS from months of
C module work into thin binding files.

```scheme
(define-foreign-library libblas "libcblas.so")
(define-foreign (cblas-dgemm
  (order int) (transA int) (transB int)
  (M int) (N int) (K int)
  (alpha double) (A c-ptr) (lda int)
  (B c-ptr) (ldb int) (beta double)
  (C c-ptr) (ldc int)) → void
  #:from libblas #:c-name "cblas_dgemm")

(define (blas-mat* A B)
  (with-pinned-matrix A pa
    (with-pinned-matrix B pb
      (let ([C (make-matrix (matrix-rows A) (matrix-cols B))])
        (with-pinned-matrix C pc
          (cblas-dgemm CblasRowMajor CblasNoTrans CblasNoTrans ...))
        C))))
```

Deliverables: `define-foreign-library`, `define-foreign`, `with-pinned-matrix`,
`with-pinned-tensor`, `c-ptr`, `callback` type, type coercion table.

**Effort:** 10–14 weeks total.
**Unlocks:** BLAS/LAPACK bindings, HDF5, GSL, NumPy interop, proper error
recovery in all mathematical code.

---

## Phase 2 — v1.2: Extended Numeric Tower (MPFR + Number Theory) ✓ done

*Shipped in v1.2.0 (2026-06-07); Pollard rho bug fixes and expanded test
coverage in v1.2.1. Decimal floating-point excluded: Curry's exact rational
tower already covers the "0.1 + 0.2 = 0.3" use case without a third inexact
type.*

MPFR is a hard dependency for GR at physically meaningful precision — IEEE 754
double gives you ~15 significant digits; Schwarzschild geodesics near the
horizon need more.

MPFR sits between `rational` and `complex` in the tower — an arbitrary-precision
inexact real. Mixed arithmetic with an MPFR operand returns MPFR. At ≤53 bits
of precision, `(with-precision N ...)` returns a `double` instead of MPFR to
avoid two representations of the same value.

```scheme
(mpfr-pi #:precision 10000)            ; π to 10000 bits
(with-precision 512
  (+ (mpfr-pi) (mpfr-e)))              ; 512-bit arithmetic context
(define-values (result ternary)
  (mpfr-sin (mpfr-pi #:precision 256) #:rounding 'nearest))
; ternary: +1 rounded up, -1 down, 0 exact

(with-interval-arithmetic               ; certified error bounds
  (+ (interval 1/3) (interval (mpfr-pi #:precision 53))))
```

MPFR carries naturally through complex (stored as val_t pairs — both components
become MPFR). Quaternion, octonion, and multivector remain double-only; the
precision they require for geometric computation does not justify the
implementation cost of MPFR component arrays.

Deliverables: `T_MPFR` type in numeric tower, `(with-precision N ...)`,
`mpfr-pi/e/phi/log2/catalan`, rounding modes, interval arithmetic,
number theory functions (`prime?`, `next-prime`, `factor`, `modular-expt`,
`jacobi`, `continued-fraction`, `convergents`).

**Effort:** 6–8 weeks.
**Unlocks:** GR at correct precision, interval-certified numerical results,
cryptographic number theory.

---

## Phase 2.5 — v1.2.5: Babylonian/Sumerian Number System ✓ done

*See full design spec: `docs/thoughts/babylonian-numbers.md`.*

Curry already translates Akkadian/cuneiform operator names in the evaluator.
Numbers are the other half of a written language. This phase adds sexagesimal
(base-60) number I/O in two representations, the convenience layer for
time/angle arithmetic, and fixes a latent binary-printing bug.

**Two representations:**

*Neugebauer notation* — the modern scholarly standard (Neugebauer 1935):
commas separate sexagesimal digits, semicolon is the sexagesimal point.

```scheme
#s1,24,51,10         ; → rational 1 + 24/60 + 51/3600 + 10/216000
                     ;   the YBC 7289 tablet approximation of √2

(number->string (sqrt 2) 'neugebauer #:places 4)  ; → "1;24,51,10"
(string->number "1;30" 'neugebauer)                ; → 3/2
```

*Cuneiform Unicode* — 𒁹 (ASH, U+12079) = 1, 𒌋 (U, U+1230B) = 10,
𒑊 (SHAR2, U+12469) = 0 (Seleucid placeholder):

```scheme
𒁹𒌋𒌋𒁹              ; reader recognises cuneiform glyphs → 23
(number->string 71 'cuneiform)   ; → "𒁹 𒌋𒁹"   (1×60 + 11)
```

**Convenience layer (pure Scheme module):**

```scheme
(import (curry sexagesimal))

(hms->seconds '(1 30 0))          ; → 5400
(seconds->hms 5400)               ; → (1 30 0)
(dms->degrees '(23 27 0))         ; → 23.45
(rational->sexagesimal (sqrt 2) #:places 4)  ; → (1 24 51 10)
(cuneiform->neugebauer "𒁹 𒌋𒁹")  ; → "1,11"
```

**Dynamic notation:**

```scheme
(current-number-notation 'neugebauer)
(* 60 60)    ; REPL displays: 1,0,0
(+ 1/2 1/3)  ; REPL displays: 0;50
```

Deliverables: cuneiform lexer extension, `#s` reader prefix, `number->string`
and `string->number` cuneiform/Neugebauer branches, `(curry sexagesimal)` pure
Scheme module, `current-number-notation`, test suite, reference doc, YBC 7289
and Plimpton 322 worked examples.

Note: the `number->string` binary radix bug (`(number->string 255 2)` →
`"11111111"`) was already fixed in v1.2.0 and is no longer part of this
deliverable.

**Effort:** 2–3 weeks.
**Unlocks:** Nothing critical — this is cultural infrastructure. But it means
Curry can read and write mathematics the way it was written when mathematics
was invented.

---

## Phase 3 — v1.3: Pluggable GC — Semispace ✓ done

*Shipped in v1.3.0 (2026-06-08).*  See `docs/reference/gc.md` for the full reference.

Two 32 MB semispaces (Cheney stop-the-world). Bump-pointer allocation;
on exhaustion or explicit `(gc-collect!)` all live objects are evacuated
to to-space; spaces swap. Root set: VM value stack, GLOBAL_ENV, pinned-list
(Actor/TVar/Channel/Mailbox/Continuation), module-registry ext-scanner.
Type-specific scanners for all 54 ObjTypes. Boehm remains the default
(`--gc boehm`); semispace selected with `--gc semispace`.

Pinned types (Boehm, never moved): Symbol, Bignum, Rational, Mpfr, Port,
Actor, Mailbox, TVar, Channel, Continuation, Primitive. All others moveable.
All raw C-array allocation sites updated to `gc_alloc_raw_pinned`.

Deliverables: `gc_ss_ops`, `gc-collect!`, `gc-stats`, `gc-on-collection`,
`gc_ss_register_ext_scanner` hook, T_CHUNK/T_UPVALUE type tags.

Known limitation: sicm tests partially pass (93/167) under `--gc semispace`
due to stale-pointer residuals in complex multi-level closure environments
under ODE integration GC pressure. All other suites pass.

**Unlocks:** Phase 5 (generational GC) can build directly on this vtable.
**Version note:** C extensions must use `gc_pin`/`gc_unpin` for heap pointers
stored off the C stack; the FFI `with-pinned-*` macros handle this automatically.

---

## Phase 4 — v1.4: Extensible CAS ✓ done (v1.4.0)

Makes the CAS user-extensible with pattern-matching rewrite rules and declared
algebraic structure — essential for user-defined rings, groups, and quantum
operators. **All of 4a–4g shipped in v1.4.0** (2026-06-10); nothing remains
open on this phase.

### Phase 4a — User rewrite rules ✓ done (2026-06-10)

`define-rule`, `define-ruleset`, `list-rules`, `clear-rules!`.
Pattern language: `?x` variables, `_` wildcard, structural matching,
optional `#:when` guard. Rules fire before built-in dispatch.
See `docs/reference/symbolic.md` § User-defined rewrite rules.

### Phase 4b — Algebra declarations + dynamic assumptions ✓ done (2026-06-10)

`define-algebra` (`#:commutative?`, `#:associative?`, `#:identity`,
`#:absorbing`, `#:relations`). Auto-creates operator procedure.
`with-assumptions`, `assume!`, `can-assume?`, `drop-assumption!`.
`sym-expr`, `sym-expr-op`, `sym-expr-nargs`, `sym-expr-arg`.
See `docs/reference/symbolic.md` § User-defined operator algebras.

### Phase 4c–4g — ✓ done (v1.4.0, 2026-06-10)

**Polynomial machinery (4c):** `poly-gcd`, `poly-resultant`,
`poly-pseudo-remainder`, `poly-factor` (Yun squarefree + Kronecker),
`poly-roots` (companion-matrix eigenvalues, degree ≤ 8), Groebner bases
(`groebner`, Buchberger's algorithm, lex ordering — shipped alongside the
rest of 4c but missed the v1.4.0 CHANGELOG entry, which is why this
roadmap had it marked as still-planned).

**Equation solving (4d):** `solve` (univariate polynomial + simple
transcendental), `solve-system` (linear, Gaussian elimination).

**Risch integration (4e) — partial:** rational-function integration
(partial fractions over ℚ) and the log-polynomial extension
(`f(x)·log(g(x))` forms). Not the full Risch algorithm — no exponential
tower, no Liouvillian-extension general case.

**Special functions (4f):** orthogonal polynomials (`legendre`,
`assoc-legendre`, `hermite`, `hermite-prob`, `chebyshev-t`, `chebyshev-u`,
`laguerre`, `assoc-laguerre`), `spherical-harmonic`, gamma family (`gamma`,
`log-gamma`, `digamma`, `beta`), Bessel (`bessel-j/y/i/k`), elliptic
integrals (`elliptic-k/e/f/pi`).

**Extended series (4g):** `laurent`, `puiseux`.

**Unlocked:** User-defined algebraic structures, GR symbolic computation,
QM operator algebra — all now available for Phase 5 to build on.

---

## Phase 5 — v1.5: GR and QM Libraries

Built on top of FFI (for LAPACK eigensolvers), MPFR (for precision), and the
extensible CAS. Pure Scheme where possible; C for numerical kernels.

**General relativity:**
```scheme
(define g (schwarzschild-metric 'M))
(christoffel g)            ; Γ^ρ_{μν}
(riemann-tensor G)         ; R^ρ_{σμν}
(einstein-tensor g)        ; G_{μν}
(geodesic g ic)            ; numerical ODE integration
(geodesic-symbolic g)      ; symbolic form
(killing-equation g xi)
(find-killing-vectors g)
(light-cone g event)
```

**Quantum mechanics:**
```scheme
(hilbert-space 'finite 2)  ; qubit
(ket 'up) (bra 'up)
(commutator A B)           ; AB - BA
(density-matrix ...)
(von-neumann-entropy rho)  ; -Tr(ρ ln ρ)
(schrodinger-evolve psi H t)
(dirac-equation mass field)
```

**Gamma matrices:** Dirac, Weyl, and Majorana conventions; `(gamma-matrices
'dirac)`, `gamma5`, `(clifford-algebra metric)`. Spin connection for
`spinor-covariant-derivative`.

**Effort:** 4–5 months.
**Unlocks:** End-to-end: symbolic metric → compiled geodesic integrator →
parallel ensemble → Qt/PLplot visualisation.

---

## Phase 6 — v2.0: Generational GC + Green Threads + Pluggable Scheduler

**Status: generational GC shipped, differently than planned here. Green
threads, hot reload, and the pluggable scheduler are still entirely
unstarted.** This phase originally bundled all of these into one breaking
"v2.0" release where the GC needed to understand green-thread stacks. In
practice the GC work shipped alone, non-breaking, four versions ago:

### ✓ Generational GC — shipped v1.5.0, refined through v1.6.3 (experimental)

What actually got built, and how it differs from the sketch below:
- **Two generations, not three** — nursery (2 MB `mmap`, bump-pointer) and
  a single tenured generation (128 MB `mmap`, Cheney copy on promotion).
  No separate survivor space, no tri-colour incremental marking for the
  old generation — both minor and major collections are stop-the-world
  Cheney copies, not incremental.
- **Card-marking write barrier** (`GC_WRITE_BARRIER`, 512-byte dirty
  cards), not the Dijkstra/`llvm.gcwrite` barrier originally sketched.
- **Polling safepoints**, landed at the VM's `L_DISPATCH` bytecode
  dispatch point specifically (between instructions, so no live C local
  can be caught mid-computation by a minor collection) plus actor
  send/receive and parallel-pool work-item boundaries — not a dedicated
  GC thread running concurrently with mutators.
- **Opt-in, not default:** `--gc generational` / `--gc-nursery-size N` /
  `--gc-tenured-size N`. Boehm (conservative, non-moving) remains the
  default backend. This was never a breaking v2.0 release — no C
  extension had to change.
- **Known limitation, still open:** the tree-walking `eval`/`apply`
  interpreter keeps intermediate `val_t`s as untracked C locals; a minor
  GC firing mid-computation there can corrupt them. SICM suite: 167/167
  under Boehm, 93/167 under `--gc generational` regardless of nursery
  size — this is the actual current blocker, not incidental polish.
- Large-object space, generational hashtable/env promotion, and a proper
  concurrent/precise design **were not built** as part of this — see the
  in-progress rewrite below, which picks these up properly instead of
  patching the v1.5.0 design further.

**The actual current GC roadmap is a separate, more ambitious rewrite**
in progress on the `gc-rewrite`/`gc-perf` branches, informed by
`docs/thoughts/performance-chez-kaappi.md` (Chez/Kaappi lessons,
2026-07-16): P0 benchmarking+CI → P1 LLVM statepoints → P2 safepoint
protocol → P3 three-zone/BiBOP heap (homogeneous type+generation segments,
replacing the ad hoc two-generation `mmap` regions above) → P4 precise
mark-sweep (closing the tree-walker gap noted above) → P5 concurrent
marking → P6 per-actor minor GC. This supersedes the generational-GC
sketch in this section; treat the section above as historical record of
what v1.5.0–1.6.3 actually shipped, not as the forward plan.

### ✗ Green threads, hot reload, pluggable scheduler — not started

Nothing below has a branch, a commit, or a design doc yet. Kept as the
original design sketch since it's still the intended shape, but note one
open dependency: the continuation strategy this depends on (`(yield)`,
coroutine frames as GC roots) needs to be checked against the hybrid
`call/cc` decision in `docs/thoughts/performance-chez-kaappi.md` §4.4
(VM frame-stack copying for multi-shot capture, native + `setjmp` for the
escape-only case, explicitly **not** CPS) before implementation starts —
see "Decided against" below. LLVM coroutines and that continuation
strategy are not obviously the same mechanism and this was never
reconciled.

**Green threads via LLVM coroutines** *(original sketch, unstarted)*:
- `(spawn thunk)` creates a green thread; returns a thread-id
- `(yield)`, `(sleep-ms n)`, `(thread-join tid)`
- Coroutine frames are GC-managed heap objects, always treated as GC roots
- Blocking I/O yields to scheduler via libuv (epoll/kqueue/IOCP)
- Looks synchronous; is async underneath

**Pluggable scheduler:**
```c
typedef struct curry_sched_ops {
    void  (*spawn)(curry_coro_t *co);
    void  (*yield)(void);
    void  (*park)(curry_coro_t *co, curry_cond_t *cond);
    void  (*unpark)(curry_coro_t *co);
    void  (*on_io_ready)(int fd, curry_coro_t *co);
} curry_sched_ops_t;
extern curry_sched_ops_t *SCHEDULER;
```

Shipped schedulers: **work-stealing** (default — one OS thread per logical CPU,
per-thread LIFO deque, FIFO stealing), **cooperative** (single-threaded, for
deterministic testing), **priority** (for real-time control applications).

**Hot code reloading** (fits naturally here — requires green threads for the
actor protocol):
- `(reload '(my module))` — recompile + replace atomically
- Running actors finish current message with old code; next message uses new
- `define-upgrade` for state schema migrations
- `(watch-and-reload ...)` for file-watching automatic reload
- `(live-patch! 'fn new-lambda)` — single-definition patch without full reload

**Effort:** 5–7 months.
**Unlocks:** Async networking, long-running simulations with live updates, true
M:N concurrency, pause-bounded GC for real-time work.

---

## Phase 7 — v2.1: Slim CLOS + Set Theory + Topology

Three conceptually related features: user-defined types that participate in
dispatch, formal set semantics as a foundation, and the topological structure
that connects sets to geometry.

**Slim CLOS** (no MOP, no method qualifiers):
```scheme
(define-class <point> ()
  (x #:init 0 #:accessor point-x)   ; immutable by default
  (y #:init 0 #:accessor point-y))

(define-generic distance (a b))
(define-method distance ((a <point>) (b <point>))
  (sqrt (+ (square (- (point-x b) (point-x a)))
           (square (- (point-y b) (point-y a))))))
(define-method + ((a <polynomial>) (b <polynomial>)) ...)

(is-a? p <point>)
(class-of p)                              ; → <point>
(class-precedence-list <point-3d>)        ; C3 linearisation
```

Built-in type hierarchy: every existing type (`<number>`, `<matrix>`,
`<tensor>`, `<spinor>`, `<actor>`, ...) is a pre-defined class. Generic
functions dispatch on them without wrappers. `call-next-method` in primary
methods only.

Layer 1: pure Scheme macro layer (no C changes).
Layer 2: polymorphic inline cache in VM for hot dispatch paths.
Layer 3: numeric tower operators (`+`, `*`, `simplify`, `∂`) become generic.

**Pluggable set theory foundations:**

*Status: the underlying idea — set semantics parameterized by a pluggable
foundation — shipped in v1.7.0, but as a pure-Scheme realization rather
than this C vtable.* `(curry sets)` provides multisets (hash-backed
element→count bags); `(curry logic)` provides six first-class
non-classical logics, and set operations (membership, comprehension,
etc.) can be parameterized by any of them — the same shape as swapping
`FOUNDATIONS` below, just implemented at the Scheme level instead of via
a C ops-table. Ordinals/cardinals, ZFC-style bounded comprehension
enforcement, and ambient fuzzy sets (as opposed to logic-parameterized
ones) were not part of what shipped. The C-level interface below remains
useful only if a future need for non-Scheme-level pluggability (e.g. a
foundation implemented in a C extension) actually arises — treat it as
optional, not required follow-up work.

```c
typedef struct curry_foundations_ops {
    val_t (*member)(val_t x, val_t S);
    val_t (*comprehend)(val_t pred, val_t domain);
    bool  has_universal_set;
    bool  has_axiom_of_choice;
    bool  is_constructive;
    const char *name;
} curry_foundations_ops_t;
extern curry_foundations_ops_t *FOUNDATIONS;
```

```scheme
(set-foundations! 'naive)       ; unrestricted — Russell's paradox is yours
(set-foundations! 'zfc)         ; bounded comprehension, regularity enforced
(set-foundations! 'constructive); future
(set-foundations! 'hott)        ; future

; ZFC: comprehension requires a domain
(set-comprehension x (integers) #:where (prime? x))  ; fine
(set-comprehension x #:where (prime? x))              ; error

; Ordinals and cardinals
(ordinal 'omega)
(cardinal 'aleph 0)
(cardinal-expt k1 k2)
(assuming 'continuum-hypothesis
  (assert (cardinal= (cardinal 'aleph 1) (cardinal 'beth 1))))

; Multisets, fuzzy sets
(multiset 1 1 2 3 3 3)
(fuzzy-set (lambda (x) (/ 1 (+ 1 (exp (- x))))))
```

**Topology:**
```scheme
(topological-space S (list empty-set S U1 U2 ...))
(metric-topology S metric-fn)
(product-topology X Y)
(hausdorff? space)
(compact? space)
(simply-connected? space)
(manifold? space 4)
(atlas space)

; Spacetime is a pseudo-Riemannian 4-manifold
(define spacetime (pseudo-riemannian-manifold 4 'lorentzian))
(attach-metric! spacetime schwarzschild-metric)
```

**Effort:** 4–5 months.
**Unlocks:** User-defined algebraic structures in CAS, formal foundations for
GR/QM computations, manifold layer for differential geometry.

---

## Phase 8 — v2.2: Introspection + Profiling + SIMD

**Status: a slice of this shipped standalone in v1.8.0, well ahead of and
separately from the rest of the phase.** An interactive bytecode debugger
landed with breakpoints (by function name or `file:line`), step/next/
finish/continue, named locals (including captured upvalues), backtrace,
and expression printing at a `dbg>` prompt (`,break`/`,unbreak`/`,breaks`/
`,debug` REPL commands, `-b` CLI flag, `(breakpoint)` builtin — see
`docs/reference/debugger.md`). Everything else in this phase — heap-walk,
compiler-IR dumps (`compile->ast/hir/mir/llvm/asm`), the sampling
profiler, Tracy/Perfetto tracing, the SIMD tower — remains unstarted;
only basic call-count/wall-clock profiling (already listed as "✓ basic"
above) exists beyond the debugger.

**Introspection** — the running system fully inspectable from Scheme:

```scheme
; Object inspection
(type-of x)          ; → 'matrix, 'tensor, 'actor, ...
(size-of x)          ; → bytes including header
(gc-generation x)    ; → 'nursery 'survivor 'old 'large
(references x)       ; → list of objects x points to

; Heap inspection
(heap-walk (lambda (obj) (when (tensor? obj) (collect obj))))
(heap-snapshot)      ; → alist: type → (count bytes)
(retention-path obj) ; → root → obj chain (why is this alive?)
(let ([before (heap-mark)])
  (run-something)
  (heap-diff before))  ; → new allocations since mark

; Compiler introspection
(compile->ast  '(+ 1 (* x x)))   ; → AST sexp
(compile->hir  '(+ 1 (* x x)))   ; → HIR sexp (after macro expansion)
(compile->mir  '(+ 1 (* x x)))   ; → MIR sexp (ANF)
(compile->llvm '(+ 1 (* x x)))   ; → LLVM IR string
(compile->asm  '(+ 1 (* x x)))   ; → native asm string
(disassemble mat*)
(optimisations-applied mat*)

; Actor + thread introspection
(list-actors)
(actor-mailbox aid)       ; snapshot of queued messages
(actor-trace aid)         ; recent message history
(list-threads)
(thread-stack-trace tid)
(list-tvars)
(tvar-info tv)            ; → (value waiters conflict-count)

; REPL commands
,profile (expr)    ; profile, print report
,trace   (expr)    ; trace all calls within expr
,expand  (expr)    ; macro-expand
,asm     proc      ; disassemble
,heap              ; heap snapshot
,actors            ; list live actors
,gc                ; GC stats
,threads           ; list green threads
```

**Sampling profiler:**
- SIGPROF handler at configurable Hz (default 1kHz)
- Output: flamegraph SVG, flat top-N, call graph (graphviz)
- `(profiler-start #:hz 1000 #:mode 'cpu)` / `(profiler-stop)` / `(profiler-report ...)`
- `(with-profiling #:hz 500 body ...)`
- Frame pointer preservation required: compile with equivalent of
  `-fno-omit-frame-pointer`

**Event tracing:**
- Tracy (real-time) and Perfetto (post-hoc) backends
- `(with-trace "tensor-contract" #:tags '(...) body ...)`
- Actor messages auto-traced; GC events emit trace events; green thread
  schedule events emit trace events

**Allocation + concurrency profilers:**
- Allocation profiler: per-site, per-type, lifetime tracking; heaptrack/massif
  compatible output
- Concurrency profiler: actor queue depths, channel backpressure, STM conflict
  rates, pool utilisation

**LLVM SIMD for numeric tower:**
- complex → `<2 x double>` SSE
- quaternion → `<4 x double>` AVX
- octonion → `<8 x double>` AVX-512 (or two AVX registers)
- matrix/tensor loop nests: Polly hints for auto-vectorisation and tiling
- This is where the GPU cores finally get used — via BLAS (Phase 1) and
  vectorised tower arithmetic

**Effort:** 3–4 months.

---

## Phase 9 — v2.3: Property-Based Testing + Scientific I/O

**Property-based testing:**
```scheme
(define-property mat-multiply-associative
  #:forall [(A (gen-matrix 3 3)) (B (gen-matrix 3 3)) (C (gen-matrix 3 3))]
  (mat-approx= (mat* A (mat* B C)) (mat* (mat* A B) C) 1e-10))

(define-property symplectic-energy-conservation
  #:forall [(ic (gen-hamiltonian-ic))]
  #:assuming (well-conditioned? ic)
  (let* ([traj (ode-integrate harmonic-rhs ic #:method 'stormer-verlet)]
         [H0 (hamiltonian (first traj))]
         [Hf (hamiltonian (last traj))])
    (< (abs (- Hf H0)) 1e-6)))

(check-property mat-multiply-associative #:trials 1000)
(check-all)
```

Generators: `gen-integer`, `gen-real`, `gen-boolean`, `gen-list`, `gen-vector`,
`gen-matrix`, `gen-tensor`, `gen-symbolic`, `gen-finite-set`,
`gen-hamiltonian-ic`. Automatic shrinking on failure.

**Scientific I/O:**
```scheme
(import (curry io hdf5))
(hdf5-read f "/measurements/temperature")  ; → tensor
(hdf5-write f "/results/geodesics" tensor)
(hdf5-attributes f "/measurements")        ; → metadata alist

(import (curry io netcdf))
(netcdf-variable nc "temperature")         ; → tensor + dimension info

(import (curry io fits))
(fits-read-image "image.fits")             ; → matrix + header alist

; Native fast tensor serialisation
(tensor-save T "result.curry-tensor")
(tensor-load "result.curry-tensor")
```

**Effort:** 2–3 months.

---

## Phase 10 — v3.0: Package Manager + Notebook Interface + Learning Tools

**Package manager** (`curry pkg`) — **explicitly deferred by decision, not
just unstarted.** A design evaluation already exists —
`docs/pkg-design.md` — comparing registry models, lock files, environments,
C extension handling, versioning, package identity, and security across
CHICKEN/Akku/cargo/pip/npm/Julia/Quicklisp. The decision was to finish
that survey before writing any implementation; the sketch below is a
candidate design from that evaluation, not a committed spec:
- Registry: git-index model (Julia General style) — metadata only, source at
  author-hosted URLs, checksums in index
- Manifest: `curry.pkg` (what you write) + `curry.lock` (what the resolver
  writes); both committed
- Global install with per-project lock pinning
- CLI: `curry pkg install`, `curry pkg update`, `curry pkg build`, `curry pkg
  test`, `curry pkg publish`
- C extension packages compile to `.so` as part of install; build recipe in
  manifest
- Environment name field in lock file (default `"global"`) for future isolation

**Notebook interface:**
- Native Qt6 application — not Jupyter, not a web app
- Three cell types: prose (Markdown + LaTeX), code (Curry), output (rendered)
- Symbolic output renders as mathematical notation via PLplot or MathML-to-SVG:
  `3x²sin(x) + x³cos(x)` not `(+ (* 3 (expt x 2) (sin x)) ...)`
- Inline PLplot figures, embedded in notebook
- Export: PDF, HTML, LaTeX
- Primary interface for interactive scientific sessions and the Anarchist's
  Cookbook

**Units and dimensions system:**

First-class dimensional analysis. Physical quantities carry their units; the
type system catches dimensional errors before the mathematics does.

```scheme
(define v  (quantity 3 'm/s))
(define v2 (quantity 5 'm/s))
(+ v v2)                  ; → 8 m/s
(* v (quantity 10 's))    ; → 30 m
(+ v (quantity 2 'kg))    ; error: dimension mismatch (length/time vs mass)

; SI base dimensions inferred
(define F (quantity 10 'N))
(define m (quantity 2  'kg))
(/ F m)                   ; → 5 m/s²  (acceleration)

; Declare named units
(define-unit 'parsec (* 3.0857e16 'm))
(define-unit 'solar-mass (* 1.989e30 'kg))

; Works with the numeric tower — quantities of exact rationals, MPFR, symbolic
(define G (quantity 6.674e-11 'N⋅m²/kg²))
(* G (quantity 'M 'solar-mass) (quantity 'r 'm))  ; symbolic quantity
```

Dimensions propagate through all arithmetic, CAS operations, and ODE solvers.
`(ode-integrate rhs ic)` checks that the RHS has consistent units before
integrating. Physical constants (`speed-of-light`, `planck`, `boltzmann`,
`gravitational-constant`) are pre-defined as quantities.

**Step-by-step CAS evaluator:**

Makes the rule engine transparent — shows each simplification step so you
understand *why* a result holds, not just *that* it does. Essential for
learning, and for debugging custom rule sets.

```scheme
(explain-simplify (+ (* x x) (* x x)))
; Step 1: (+ (* x x) (* x x))
;   rule: (+ ?a ?a) → (* 2 ?a)   [addition identity]
; Step 2: (* 2 (* x x))
;   rule: (* ?a (?op ?b ?c)) → (?op (* ?a ?b) (* ?a ?c))  [distributivity]
;         — not applicable, no sum in second argument
;   rule: (* ?n (expt ?x ?k)) → (expt ?x (+ ?k 1))   — not applicable
; Result: (* 2 (expt x 2))

(explain-simplify (sqrt (* x x)) #:assuming [(real? x) (>= x 0)])
; Step 1: (sqrt (* x x))
;   rule: (sqrt (expt ?x 2)) → ?x   #:assuming (real? ?x) (>= ?x 0)
;   assumption (real? x): satisfied
;   assumption (>= x 0): satisfied
; Result: x

; Compact mode — just the rules that fired
(explain-simplify expr #:mode 'rules)
; (+ ?a ?a) → (* 2 ?a)
; (expt (sqrt ?x) 2) → ?x  [with assumptions]
```

`explain-simplify` is also how ruleset debugging works — `(explain-simplify
expr #:using my-ruleset)` shows exactly which rules in your custom set are
firing and in what order.

**LLM-integrated notebook:**

The `(curry llm)` module already handles Anthropic and OpenAI. The notebook
wires the live session into a conversation so you can ask questions grounded
in what you're actually running.

```scheme
; In a notebook code cell
(define g (schwarzschild-metric 1.0))   ; M = 1 solar mass
(define traj (geodesic g '(6 0 0 0) '(0 1 0 0) #:steps 1000))
(pl:line (map car traj) (map cadr traj))

; In a prose cell — asks about the plot you just produced
; "Why does the orbit precess? The initial conditions look circular."
```

The LLM receives: the current session environment (what's defined), the last
N code cells and their outputs, any PLplot figures as descriptions, and your
question. It answers in the context of the actual computation — not generic
physics, but *this geodesic*, *these initial conditions*, *this precession*.

It can also generate code: "show me what happens if I add a small angular
momentum perturbation" produces a new code cell you can run immediately.

The session context is opt-in and clearly marked — the LLM sees only what you
send it.

**Exploration sharing:**

Notebooks are plain text (Markdown + fenced Curry code blocks with metadata).
They can be:
- Committed to git (diff-friendly format)
- Published as static HTML with `curry notebook export`
- Shared as runnable `.curry-nb` files — recipient opens and runs them locally
- Annotated with `#:requires` metadata so `curry pkg install` fetches
  dependencies automatically before running

This is the orthopraxis layer: someone learns something, writes a notebook
that demonstrates it, shares it. Another person runs it, modifies it, learns
from the exploration rather than the explanation. No institution required.

**Effort:** 7–9 months combined (notebook + units + explain-simplify +
LLM integration + sharing).

---

## Decided against

Not everything below "not started" is simply un-gotten-to. Three things
were evaluated and explicitly ruled out, recorded in
`docs/thoughts/performance-chez-kaappi.md` (2026-07-16, surveying Chez
Scheme and the Kaappi implementation for lessons applicable to curry):

- **No NaN-boxing migration.** Kaappi's flonum-unboxing win comes from
  NaN-boxing `val_t`; curry gets the same win more cheaply by unboxing
  flonums at the compiler/IR level across proven-flonum expression chains,
  keeping the current 2-bit low-tag `val_t` representation and avoiding a
  rewrite of every module that touches values directly.
- **No register-VM rewrite.** Kaappi's register VM avoids push/pop
  dispatch traffic, but curry gets most of the same win from
  superinstructions on the existing stack VM (a fused self-tail-call
  opcode, fused global-call-and-lookup) at a fraction of the engineering
  cost of migrating `compiler.c` and `vm.c` to registers.
- **No CPS conversion for `call/cc`.** Both Kaappi and Chez were surveyed
  here (Kaappi rejected CPS outright — every call pays, two IR flavors to
  maintain; Chez's segmented-stack approach is the eventual endgame but
  only if capture cost ever shows up hot in profiling). The adopted
  strategy is Kaappi's: VM frame-stack copying for multi-shot `call/cc`,
  native direct-style + `setjmp` for the escape-only common case
  (`call/ec`, `guard`, `dynamic-wind` exits). This directly affects Phase
  6's green-threads sketch above (`(yield)` via LLVM coroutines) — that
  sketch predates this decision and the two haven't been reconciled;
  don't assume coroutine-based green threads slot in cleanly on top of
  the frame-copying continuation strategy without checking first.

**Package manager** (Phase 10) is deferred by decision pending the
comparative survey in `docs/pkg-design.md`, not because nobody's gotten to
it — see that phase's section above.

---

## Summary timeline

What's actually shipped, by version (supersedes the original
phase-to-version mapping below, which held through v1.4.0 and then
diverged — v1.5.0 shipped a Phase 6 item, generational GC, not the Phase 5
GR/QM library the original mapping promised for that slot):

| Version | Highlights | Status |
|---------|-----------|--------|
| ~~**v1.1.0**~~ | Condition system + restarts; FFI + zero-copy matrix/tensor | ✓ shipped |
| ~~**v1.1.1**~~ | SRFI compat: `(srfi s1 lists)`, `(srfi s27 random-bits)` | ✓ shipped |
| ~~**v1.2.0**~~ | MPFR + interval arithmetic; number theory | ✓ shipped |
| ~~**v1.2.1**~~ | Pollard rho fix; expanded test coverage | ✓ shipped |
| ~~**v1.2.5**~~ | Sexagesimal/Babylonian numbers: cuneiform reader/writer, Neugebauer notation | ✓ shipped |
| ~~**v1.3.0**~~ | Cheney semispace GC (`--gc semispace`) | ✓ shipped |
| ~~**v1.4.0**~~ | Extensible CAS complete (4a–4g: rules, algebra declarations, polynomial machinery + Groebner, equation solving, partial Risch, special functions, Laurent/Puiseux) | ✓ shipped |
| ~~**v1.5.0**~~ | Generational GC (experimental, opt-in) — *not* GR/QM as originally mapped to this slot | ✓ shipped |
| ~~**v1.6.0–1.6.3**~~ | GC instrumentation, benchmarking stack, JIT/GC correctness fixes | ✓ shipped |
| ~~**v1.7.0**~~ | Real error backtraces + machine-legible error codes; `(curry logic)` + `(curry sets)` (partial Phase 7) | ✓ shipped |
| ~~**v1.8.0**~~ | Interactive debugger (partial Phase 8); per-statement source lines | ✓ shipped |

Remaining phases, in the dependency order from the top of this document —
not pinned to specific version numbers, since the original numbering
broke after v1.4.0 and re-pinning invites the same drift:

| Phase | Name | Highlights | Estimated effort |
|---|---|---|---|
| 5 | Physics I | GR library, QM library, Clifford + gamma matrices | 4–5 mo |
| 6 (remainder) | Architecture | Green threads, pluggable scheduler, hot reload — GC portion already shipped, see above; heavier rewrite in progress on `gc-rewrite` branch | 5–7 mo |
| 7 (remainder) | Types + Sets | Slim CLOS, topology — pluggable-foundations portion already shipped differently, see above | 4–5 mo |
| 8 (remainder) | Tooling | Full introspection, sampling profiler, Tracy, SIMD tower — debugger portion already shipped, see above | 3–4 mo |
| 9 | Testing + I/O | Property-based testing, HDF5/NetCDF/FITS | 2–3 mo |
| 10 | Ecosystem | Package manager (deferred by decision); notebook (Qt6, mathematical rendering); units system; `explain-simplify`; LLM-integrated notebook; exploration sharing | 7–9 mo |

**Breaking-change note:** the *next* moving/precise GC milestone (on the
`gc-rewrite` branch, not the v1.5.0 generational GC already shipped) is
still expected to require a C-extension audit — `gc_pin`/`gc_unpin` for
off-stack Scheme pointers, same as originally described for "v2.0" above.
The FFI `with-pinned-*` macros (v1.1) already handle this automatically
for FFI callers; hand-written C modules will need a one-time look when
that milestone lands.

**Critical path:** v1.1 (condition system + FFI) was the original
bottleneck and has long since shipped. Everything from the GR library
onward — LAPACK eigensolvers, BLAS for tensors, HDF5 for scientific data,
GSL for special functions — routes through the FFI, which has been
available since v1.1.0.

**Pluggable interfaces:**

| Interface | Backends |
|---|---|
| GC (`gc_ops_t`) | Boehm (default), semispace (`--gc semispace`, v1.3.0), generational (`--gc generational`, v1.5.0, experimental) |
| Scheduler (`curry_sched_ops_t`) | not started (Phase 6 remainder) |
| Set foundations | `(curry sets)` + `(curry logic)`, pure Scheme (v1.7.0) — no C-level `curry_foundations_ops_t` vtable exists or is currently planned to be needed |
| Profiling backend | in-process call-count/wall-clock only (basic); Tracy/Perfetto not started (Phase 8 remainder) |

**Design intent:** This is an instrument for learning by doing — orthopraxis
rather than orthodoxy. The units system catches physical nonsense before the
mathematics does. `explain-simplify` shows the reasoning, not just the result.
The LLM-integrated notebook means you can ask "why did this geodesic diverge?"
with your actual computation in scope, not a generic textbook answer. Sharing
means knowledge moves horizontally. No institution required.
