# Curry Features

*v1.23.3 — 2026-08-28*

A tour of what curry actually does

---

## Extended numeric tower

```
fixnum → bignum → rational → flonum → complex → quaternion → octonion
       → multivector → surreal → symbolic
```

Arithmetic automatically promotes through the tower as operands demand it —
`(+ 1/3 0.5)` produces a flonum, `(∂ (* x x) x)` produces the symbolic
expression `(+ x x)`, and a quaternion multiplied by a flonum promotes the
flonum side transparently. Nothing about ordinary arithmetic code needs to
know in advance which rung of the tower it's actually operating on.

- **Quaternions/octonions** — full hypercomplex arithmetic, useful for 3D/4D
  rotation math without a separate library.
- **Multivectors** (Clifford algebra `Cl(p,q,r)`) — geometric product, wedge,
  contraction, rotors, projective and conformal geometric algebra (PGA/CGA).
- **Surreal numbers** — Hahn-series representation with `ω` and `ε` as
  first-class exact infinitesimals, plus forward-mode automatic
  differentiation built on the same machinery.
- **Symbolic** — see the CAS section below; it's the top of the tower, not a
  separate subsystem bolted on the side.

See [`docs/reference/language.md`](docs/reference/language.md#values-and-types)
for the complete type/predicate/constructor table.

## Computer algebra system (CAS) and auto-differentiation

A real symbolic algebra layer, not a toy expression-printer: symbolic
differentiation and integration, algebraic simplification, substitution,
series expansion, and Wirtinger calculus (`∂`/`∂̄` for complex-valued
functions of a complex variable, needed for anything touching signal
processing or complex optimization). Also covers fractional calculus and
formats output as infix or LaTeX in addition to the default S-expression
form, so a derivation can go straight into a paper or a notebook.

User-extensible at the Scheme level as of v1.4: `define-rule`/`define-algebra`
let you declare a brand-new algebra (Grassmann/exterior algebra, tropical
algebra, a Lie algebra from structure constants, GF(p) finite-field
arithmetic) with a handful of rewrite rules and immediately get symbolic
simplification, differentiation, and series expansion over it for free — no
C required.

See [`docs/reference/symbolic.md`](docs/reference/symbolic.md) for the full
procedure reference.

## Quantum superposition values

`T_QUANTUM` represents `|ψ⟩ = Σ αᵢ|xᵢ⟩` — a genuine first-class value type
with complex amplitudes, not a simulation library layered on top of vectors.
Ordinary arithmetic maps over the branches; `(observe q)` collapses the
superposition probabilistically according to the amplitudes' squared
magnitudes. See [`docs/reference/quantum.md`](docs/reference/quantum.md).

## Parallel map and reduce

`map` and `reduce` automatically parallelize across CPU cores above a size
threshold (default 8 elements), backed by a persistent Chase-Lev
work-stealing thread pool sized to `(hardware-concurrency)` — no thread
pool to configure or manage yourself. Sequential variants (`map/seq`,
`reduce/seq`) are always available when ordering or side-effect sequencing
matters; `for-each/par` is the explicit parallel `for-each`. `reduce`'s
identity element is applied once total, not once per work-stealing chunk.
See [`docs/reference/parallel.md`](docs/reference/parallel.md).

## Random numbers (SRFI-27)

A single global source, seeded from `/dev/urandom` on first use
(xoshiro256+), with SRFI-27's full API: deterministic reseeding via
`random-source-pseudo-randomize!`, and state capture/restore via
`random-source-state-ref`/`random-source-state-set!` for reproducing an
exact draw sequence later. All access to the shared RNG state is
mutex-protected, since curry's actors are real OS threads that can call
`random-real`/`random-integer` concurrently. See
[`docs/reference/srfi/s27.md`](docs/reference/srfi/s27.md).

## Modules

Curry ships around 55 optional and always-on modules: SQL/NoSQL databases,
HTTP/GraphQL/MQTT/Neo4j clients, an LLM client (see below), image and
scientific-data I/O, a general C FFI, Qt6 GUI/graphics, POSIX bindings, and
concurrency primitives beyond the core actor model. Optional modules gate
behind a `-DBUILD_MODULE_X=ON` CMake flag (most default `ON`) and load with
`(import (curry X))`. Full list with import names, one-line descriptions,
and extra runtime dependencies:
[`docs/reference/modules.md`](docs/reference/modules.md).

## SRFI compatibility

46 portable `(srfi sN name)` libraries — the same naming convention Guile,
Chicken, and Chibi-Scheme use, so the same `(import ...)` line works across
implementations. Coverage runs from foundational list/vector/hash-table
libraries (SRFI-1, 125/126, 128, 132/133) through concurrency (SRFI-18),
time (SRFI-19), sets and comparators, to more recent additions like SRFI-215
(structured logging) and SRFI-227 (optional arguments). A full audit against
each SRFI's official specification (not just curry's own docs) is tracked
per-library, so gaps are documented rather than silently missing. Full
index: [`docs/reference/srfi/index.md`](docs/reference/srfi/index.md).

---

## LLM / AI integration

Curry can talk to any LLM out of the box. No API wrappers, no external
packages — just import and go.

```scheme
(import (curry llm))

; One-shot question (Ollama, local, no key needed)
(display (llm-ask (make-llm-client 'ollama "llama3.1") "What is a monad?"))

; Claude or OpenAI (reads key from env)
(display (llm-ask (make-llm-client 'claude) "Explain tail-call optimisation."))
```

Give the model **tools** it can actually call:

```scheme
(define conv (make-conversation (make-llm-client 'claude)))

(conv-system! conv "Use tools to answer accurately.")

(conv-tool! conv "sqrt"
  "Compute the square root of a number."
  '((n "number" "The number"))
  (lambda (args) (number->string (sqrt (cdr (assq 'n args))))))

; The model calls sqrt(144), gets "12.0", incorporates it into its reply
(display (conv-send! conv "What is the square root of 144? Use the sqrt tool."))
```

The library runs the full **agentic loop** automatically — send, detect
tool calls, execute lambdas, feed results back, repeat. Supports Anthropic's
native tool-use protocol and the OpenAI function-calling protocol; Ollama
and any OpenAI-compatible endpoint use the OpenAI path. Structured output,
multi-turn conversation state, and MCP server integration (curry scripts can
themselves *be* MCP servers, or call out to one) are all part of the same
module.

See [docs/guides/guide-llm.md](docs/guides/guide-llm.md) for ten
progressively interesting examples: database queries, parallel actor
pipelines, structured output, MCP servers backed by local models,
CAS-assisted maths tutors, and more.

---

## LLVM JIT backend

When built with `-DBUILD_LLVM=ON`, curry adds a tiered native-compilation
layer on top of the bytecode VM: any closure called ≥ 50 times is
automatically compiled to native ARM64/x86-64 machine code on the next call,
transparently — no source annotations, no separate compilation step, no
change to how you write or run a script. Typical speedups on recursive or
loop-heavy code range 1.1×–14× over the bytecode VM alone. See
[`docs/reference/llvm-jit.md`](docs/reference/llvm-jit.md) for the tier
thresholds, the full procedure list, and benchmark numbers.

## C FFI

When built with `-DBUILD_FFI=ON`, curry can call any C library directly
from Scheme with no glue code to write or generate.

```scheme
(import (curry ffi))

(define-foreign-library libm "libm.so")     ; Linux
; (define-foreign-library libm "libm.dylib") ; macOS

(define-foreign (c-sin  (x double)) → double #:from libm #:c-name "sin")
(define-foreign (c-sqrt (x double)) → double #:from libm #:c-name "sqrt")

(c-sin 1.5707963)   ; → 1.0
(c-sqrt 2.0)        ; → 1.41421...
```

**Zero-copy matrix passthrough** — pass a matrix or tensor's raw `double*`
directly to a C library (e.g. BLAS) with no copying:

```scheme
(with-pinned-matrix A pa
  (with-pinned-matrix B pb
    (with-pinned-matrix C pc
      (cblas-dgemm CblasRowMajor CblasNoTrans CblasNoTrans
                   rows cols inner 1.0 pa cols pb cols 0.0 pc cols))))
```

For structs deep enough that generic FFI primitives (`peek-bytes`, pinned
bytevectors, manual offset arithmetic) would be slower and harder to review
than a real C module — streaming synthesis APIs, complex callback
signatures — curry's own convention is to write a dedicated C module
instead (`(curry piper)` is the worked example: see
[`docs/reference/module-ffi.md`](docs/reference/module-ffi.md) for exactly
where that line is drawn).

Requires `libffi-dev` on Linux; found automatically via Homebrew on macOS.
See [`docs/reference/module-ffi.md`](docs/reference/module-ffi.md) for the
full API and type-mapping table.

## Garbage collector

Curry ships two GC backends, selectable at runtime: `--gc boehm` (default —
conservative, thread-safe, no configuration needed) or `--gc generational`
(experimental — a per-thread nursery aimed at lower pause times on
allocation-heavy workloads, opt-in while it matures). `(gc-stats)` and
`(gc-stats-reset!)` expose live counters and pause times from either
backend for your own instrumentation. See
[`docs/reference/gc.md`](docs/reference/gc.md) for the full backend/flag/
stats-field reference, [`docs/reference/benchmarking.md`](docs/reference/benchmarking.md)
for the real-time Grafana monitoring stack (Docker or Apple's native
container runtime), and [`docs/reference/profiling.md`](docs/reference/profiling.md)
for the interactive runtime profiler.

---

## Akkadian error messages

All runtime errors carry a Standard Babylonian Akkadian preamble
identifying the fault category, rendered in cuneiform script, as scribal
tradition demands:

```
𒀭 ḫiṭītu — lā nikkassum:
  +: not a number: "hello"
```

This isn't a cosmetic skin over English error text — special forms and
built-in procedures have genuine Akkadian and cuneiform synonyms throughout
the language (`eval()` transparently translates them), so code written
entirely in Akkadian is valid curry Scheme, not a token substitution layer
sitting on top of a different "real" language underneath. See
[`docs/reference/akkadian-reference.md`](docs/reference/akkadian-reference.md#error-messages)
for the full phrase table and vocabulary.
