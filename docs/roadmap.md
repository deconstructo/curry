# Curry — Implementation Roadmap

*Drafted 2026-06-06. Updated 2026-06-07 for v1.2.2. Source: cill_spec.pdf + design sessions.*

Curry is at v1.2.2. This document maps the path from here to a compiled Scheme
for scientific computing — the full cill specification. It is ordered by
dependency, not ambition; each phase unblocks the phases above it.

---

## Where we are now (v1.2.2)

| Capability | Status |
|---|---|
| R7RS base + numeric tower (fixnum → octonion → symbolic CAS) | ✓ complete |
| Bytecode compiler + VM | ✓ complete |
| LLVM ORC v2 JIT (tiered) | ✓ complete |
| Actors, STM, CSP channels | ✓ complete |
| Parallel map/reduce (Chase-Lev work-stealing) | ✓ complete |
| Qt6, PLplot visualisation | ✓ complete |
| Matrices, tensors (+ transpose/contract/einsum), spinors | ✓ complete |
| GC vtable abstraction + nursery bump-pointer | ✓ skeleton (Boehm backend) |
| Basic profiling (call counts, wall-clock) | ✓ basic |
| CAS rule engine (internal, not user-extensible) | ✓ partial |
| Polynomial ops (expand, collect, degree) | ✓ partial |
| **CL-style condition system with restarts** | ✓ **v1.1.0** |
| **General C FFI (libffi, zero-copy matrix/tensor)** | ✓ **v1.1.0** (`BUILD_FFI=ON`) |
| **SRFI compatibility layer (`surfage s1`, `surfage s27`)** | ✓ **v1.1.1** |
| **MPFR arbitrary-precision floats + interval arithmetic** | ✓ **v1.2.0** (`BUILD_MPFR=ON`) |
| **Number theory** (primality, factoring, modular arithmetic, combinatorics, CF) | ✓ **v1.2.0** |
| **FFI design guidance** (when to use FFI vs C module) | ✓ **v1.2.1** (docs) |
| Sexagesimal / Babylonian number system | ✗ (v1.2.5 — spec written) |
| Extensible CAS (`define-rule`, `define-algebra`, Groebner, Risch) | ✗ |
| Moving / generational GC | ✗ |
| Green threads | ✗ |
| Hot code reloading | ✗ |
| Slim CLOS | ✗ |
| Pluggable set theory foundations | ✗ |
| Topology | ✗ |
| GR / QM libraries | ✗ |
| Introspection (heap, compiler IR, actor debug) | ✗ |
| Sampling profiler, Tracy/Perfetto tracing | ✗ |
| Property-based testing | ✗ |
| Scientific I/O (HDF5, NetCDF, FITS) | ✗ |
| Package manager | ✗ |
| Notebook interface | ✗ |
| Units and dimensions system | ✗ |
| Step-by-step CAS (`explain-simplify`) | ✗ |
| LLM-integrated notebook | ✗ (LLM module exists; notebook does not yet) |
| Exploration sharing (`.curry-nb` format) | ✗ |

---

## Phase 1 — v1.1: Condition System + FFI ✓ done

*Shipped in v1.1.0 (2026-06-06). v1.1.1 added SRFI compatibility (`surfage s1
lists`, `surfage s27 random-bits`). The two features that unblock everything
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

## Phase 2.5 — v1.2.5: Babylonian/Sumerian Number System ← next

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

## Phase 3 — v1.3: Extensible CAS

The current CAS has rules hard-coded in C. This phase makes it user-extensible
with declared algebraic structure — essential for user-defined rings, groups,
and quantum operators.

```scheme
(define-algebra 'addition
  #:commutative? #t #:associative? #t #:identity 0)
(define-algebra 'clifford
  #:commutative? #f #:associative? #t
  #:relations gamma-anticommutator)
(define-algebra 'octonion-multiply
  #:commutative? #f #:associative? #f)  ; bracketing is structural

(define-rule (* 0 _)     → 0)
(define-rule (* 1 ?x)    → ?x)
(define-rule (+ ?x ?x)   → (* 2 ?x))

(define-ruleset trig-identities
  [(+ (expt (sin ?x) 2) (expt (cos ?x) 2)) → 1]
  [(sin (* 2 ?x)) → (* 2 (sin ?x) (cos ?x))])

(with-assumptions [(real? 'x) (> 'x 0)]
  (simplify (sqrt (* x x))))   ; → x (not |x|)

(assume! (real? 'x))
(can-assume? (> 'x 0))
```

Also: polynomial machinery (`poly-gcd`, `poly-factor`, `poly-roots`,
`poly-resultant`, `partial-fractions`, Groebner bases), equation solving
(`solve`, `solve-system`), Risch algorithm (partial — rational + log/exp tower),
special functions (`gamma`, `bessel-j`, `hermite`, `legendre`,
`spherical-harmonic`, elliptic integrals), integral transforms (Laplace,
Fourier, Z-transform), Laurent/Puiseux series.

**Effort:** 4–5 months.
**Unlocks:** User-defined algebraic structures, GR symbolic computation,
QM operator algebra.

---

## Phase 4 — v1.4: Pluggable GC — Semispace

The GC vtable (`gc_ops_t`) and per-thread nursery are already in `gc.h`. Object
headers already carry `fwd` forwarding fields. LLVM statepoints are already
emitted. The infrastructure is sitting there; this phase builds the first
moving collector on top of it.

**Semispace (Cheney) GC:**
- Two equal semi-spaces; allocation in from-space via bump pointer
- On exhaustion, copy live objects to to-space following statepoint stack maps
- Write barrier: Dijkstra (`if is_black(obj) && is_white(new_val): shade_grey(new_val)`)
- LLVM emits `llvm.gcwrite` at every pointer store; barrier reads that

The pluggable design means Boehm stays as the `--gc boehm` fallback for
debugging (conservative, no statepoints needed). Semispace is selected with
`--gc semispace` or a startup call.

Also: green thread coroutine frames as GC-managed heap objects; `gc-collect!`,
`gc-stats`, `gc-on-collection` hook.

**Effort:** 6–8 weeks.
**Version note:** C extension modules must use `gc_pin`/`gc_unpin` for pointers
stored off the stack once a moving GC is active. The FFI `with-pinned-*` macros
(Phase 1) handle this automatically. This is the only breaking change for
extension authors.

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

**Major version bump.** Two architectural changes that interact: the generational
GC needs to know about green thread stacks; the green thread scheduler is itself
a pluggable component.

**Generational GC:**
- Nursery (young gen) — bump-pointer, collected frequently
- Survivor spaces — objects surviving one nursery collection
- Old generation — tri-colour incremental marking, infrequently collected
- Large object space — tensors, matrices above threshold; bypasses nursery
- Dijkstra write barrier at every heap-to-heap pointer store (`llvm.gcwrite`)
- GC thread separate from mutator threads; incremental to bound pause times

**Green threads via LLVM coroutines:**
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

**Package manager** (`curry pkg`):
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

## Summary timeline

| Version | Name | Highlights | Status |
|---------|------|-----------|--------|
| ~~**v1.1.0**~~ | Foundation | Condition system + restarts; FFI + zero-copy matrix/tensor | ✓ shipped |
| ~~**v1.1.1**~~ | SRFI compat | `(surfage s1 lists)`, `(surfage s27 random-bits)` | ✓ shipped |
| ~~**v1.2.0**~~ | Precision | MPFR + interval arithmetic; number theory (185 new tests) | ✓ shipped |
| ~~**v1.2.1**~~ | Bug fixes | Pollard rho infinite-loop fix; expanded test coverage | ✓ shipped |
| **v1.2.5** ← | Babylon | Sexagesimal: cuneiform reader/writer, Neugebauer notation, `(curry sexagesimal)` | next |
| **v1.3** | Algebra | Extensible CAS, Groebner, Risch (partial), special functions | 4–5 mo |
| **v1.4** | Moving GC | Semispace collector, write barriers, GC stats | 6–8 wk |
| **v1.5** | Physics I | GR library, QM library, Clifford + gamma matrices | 4–5 mo |
| **v2.0** ⚠ | Architecture | Generational GC, green threads, pluggable scheduler, hot reload | 5–7 mo |
| **v2.1** | Types + Sets | Slim CLOS, pluggable set theory foundations, topology | 4–5 mo |
| **v2.2** | Tooling | Full introspection, sampling profiler, Tracy, SIMD tower | 3–4 mo |
| **v2.3** | Testing + I/O | Property-based testing, HDF5/NetCDF/FITS | 2–3 mo |
| **v3.0** | Ecosystem | Package manager; notebook (Qt6, mathematical rendering); units system; step-by-step CAS (`explain-simplify`); LLM-integrated notebook; exploration sharing | 7–9 mo |

**⚠ v2.0 is a breaking version** for C extension authors: moving GC requires
`gc_pin`/`gc_unpin` for off-stack Scheme pointers. The FFI `with-pinned-*`
macros (v1.1) handle this automatically for FFI callers. Hand-written C modules
need a one-time audit.

**Critical path:** v1.1 (condition system + FFI) was the bottleneck — it has
shipped. Everything from the GR library onward — LAPACK eigensolvers, BLAS for
tensors, HDF5 for scientific data, GSL for special functions — routes through
the FFI, which is now available.

**Pluggable interfaces shipped by this roadmap:**

| Interface | Backends |
|---|---|
| GC (`gc_ops_t`) | Boehm (now), semispace (v1.4), generational (v2.0) |
| Scheduler (`curry_sched_ops_t`) | work-stealing (v2.0), cooperative, priority |
| Set foundations (`curry_foundations_ops_t`) | naïve, ZFC (v2.1), constructive, HoTT (future) |
| Profiling backend | in-process (v2.2), Tracy, Perfetto |

**Total sequential estimate (single implementor):** 40–56 months for all ten
phases. In practice the mathematics, concurrency, and tooling tracks are
parallel. The critical path — v1.1 through v2.0 — is 18–24 months and
delivers a genuinely useful compiled Scheme for scientific computing before
the full ecosystem is complete.

**Design intent:** This is an instrument for learning by doing — orthopraxis
rather than orthodoxy. The units system catches physical nonsense before the
mathematics does. `explain-simplify` shows the reasoning, not just the result.
The LLM-integrated notebook means you can ask "why did this geodesic diverge?"
with your actual computation in scope, not a generic textbook answer. Sharing
means knowledge moves horizontally. No institution required.
