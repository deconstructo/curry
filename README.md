# Curry Scheme

Curry is an R7RS Scheme implementation with practical R6RS compatibility, a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices, tensors, and spinors, a CL-style condition system with restarts, a general C FFI, STM and CSP channels alongside the actor-model concurrency system, a modular C extension interface, and a built-in LLM client that can talk to Claude, GPT-4o, Ollama, or any OpenAI-compatible endpoint — with multi-turn conversation, tool use, and a full agentic loop.

Source is compiled to bytecode and executed on a stack-based VM. When built with LLVM (`-DBUILD_LLVM=ON`), hot closures are automatically compiled to native machine code after 50 calls via an ORC v2 JIT backend. Compiled chunks are cached in `.scc` files (source-adjacent, or `~/.cache/curry/` for read-only paths) and reused on subsequent runs, invalidated automatically on source change or version bump. Use `-c file.scm` to pre-compile without running; `.scc` files can also be passed directly as the script argument.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

Built around pluggable logics, an open-ended numeric tower, and no single "correct" answer treated as bedrock.

## AI and development

Yes, I do use Claude for development. My apology is this:

* I can code, but I am slow - very slow. I make use of Claude to develop ideas quickly. For me, what is more interesting and important is exploring the ideas that I incorporate into Curry, than actually writing the code itself. 

* I treat Claude more as a dialogue partner than as just a simple tool. I discuss ideas that I have. I discuss things that I find in papers or in other lisp or scheme implementations and work through what goes in, what remains on the table, and what is pulled out and/or replaced. 

* One of the freedoms of this mode of development is that I see that if even a rather large change is required to get to a better place - in terms of stability, performance, flexibility or features - then I am _happy_ to make that change! Evidence are the changes from a tree-walking interpreter to VM to JIT compiler, or the work that I've done (and backed out) on improving garbage collection.

* AI may do the coding and a lot of the documentation, but I assume all responsibility for bugs, errors, omissions, poor architecture, poor code and poor documentation.

## Get involved!

I'd love to work with others on this, dare I say rather unique language. I'd also like to be useful for others. So Feel free to drop issues in or PRs.

## Documents

Documentation is split into two directories:

- **[`docs/reference/`](docs/reference/)** — language specification, numeric tower, CAS, module APIs
- **[`docs/guides/`](docs/guides/)** — installation, tutorials, how-tos, worked examples

### Language reference

- [Language reference](docs/reference/language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](docs/reference/symbolic.md) — CAS reference: variables, differentiation, integration, simplification, substitution, complex operators, Wirtinger calculus, auto-diff
- [Quantum superposition](docs/reference/quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](docs/reference/surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](docs/reference/multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](docs/reference/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages, plus the runtime error-message phrase table
- [Error codes](docs/reference/error-codes.md) — stable machine-legible `error-object-code`/`condition-code` registry for tooling
- [Module index](docs/reference/modules.md) — the full list of `(curry ...)` modules, import names, and extra build dependencies
- [SRFI compatibility](docs/reference/srfi/index.md) — the 38 portable `(srfi sN name)` libraries, one page each
- [Parallel map/reduce](docs/reference/parallel.md) — the work-stealing thread pool behind `map`/`reduce`
- [LLVM JIT backend](docs/reference/llvm-jit.md) — auto-JIT, benchmark numbers, build flags
- [Garbage collector reference](docs/reference/gc.md) — the two GC backends and `(gc-stats)` fields

### Guides

- [Installation](docs/guides/INSTALL.md) — Homebrew (macOS), build from source, Qt6, test suite
- [LLM integration](docs/guides/guide-llm.md) — talking to Claude, OpenAI, and Ollama; tool use, agentic loops, structured output, MCP servers, CAS-backed AI
- [Raspberry Pi / embedded hardware](docs/guides/RPI.md) — setup guide for Pi; GPIO, I2C, SPI, PWM
- [MCP server](docs/guides/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients
- [macOS app bundler](docs/guides/make-macos-app.md) — bundle any Curry script as a `.app` with Qt frameworks embedded
- [Monitoring guide](docs/guides/guide-monitoring.md) — using `(gc-stats)`, running the Grafana stack (Docker and Apple Containers), customizing dashboards, publishing custom metrics
- [Text-to-speech with Piper](docs/guides/tts-piper.md) — building `libpiper` and curry against it on macOS/Linux, speaking/saving audio via `(curry tts)`, sourcing and adding voice models
- [Simulating cell biochemistry with Gillespie](docs/guides/gillespie-cells.md) — stochastic reaction networks, composable temperature/pH/nutrient-sensitive rate laws, multi-cell simulation via actors
- [Benchmarking reference](docs/reference/benchmarking.md) — bench suites, MQTT event schema, all GC stat fields
- [Profiling reference](docs/reference/profiling.md) — `**eval-profiler**`, `(curry profiling)` API, `,profile` REPL command, timing workflows

### Extended numeric tower

Fixnum → bignum → rational → flonum → complex → quaternion → octonion → multivector → surreal → symbolic. Arithmetic automatically promotes through the tower: `(+ 1/3 0.5)` → flonum, `(∂ (* x x) x)` → symbolic `(+ x x)`. See [`docs/reference/language.md`](docs/reference/language.md#values-and-types) for the full type/predicate/constructor table.

### CAS and auto-differentiation

Symbolic differentiation/integration, simplification, Wirtinger calculus, fractional calculus, and infix/LaTeX output — see [`docs/reference/symbolic.md`](docs/reference/symbolic.md) for the full quick-reference table.

### Parallel map and reduce

`map` and `reduce` automatically parallelise over multiple CPU cores on a persistent work-stealing thread pool; sequential variants are always available. See [`docs/reference/parallel.md`](docs/reference/parallel.md).

### Random numbers (SRFI-27)

The global source is seeded from `/dev/urandom` on first use (xoshiro256+). See [`docs/reference/srfi/s27.md`](docs/reference/srfi/s27.md).

### Modules

Curry ships ~55 optional and always-on modules — databases, HTTP/GraphQL/MQTT clients, an LLM client, image/scientific-data I/O, FFI, concurrency primitives, and more. Full list with import names, descriptions, and extra build dependencies: [`docs/reference/modules.md`](docs/reference/modules.md).

### SRFI compatibility (`srfi`)

38 portable SRFI libraries under the `(srfi sN name)` naming convention — compatible with Guile, Chicken, and Chibi-Scheme. Full index: [`docs/reference/srfi/index.md`](docs/reference/srfi/index.md).

---

## LLM / AI integration

Curry can talk to any LLM out of the box. No API wrappers, no external packages — just import and go.

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

The library runs the full **agentic loop** automatically — send, detect tool calls, execute lambdas, feed results back, repeat. Supports Anthropic's native tool-use protocol and the OpenAI function-calling protocol; Ollama and any OpenAI-compatible endpoint use the OpenAI path.

See [docs/guides/guide-llm.md](docs/guides/guide-llm.md) for ten progressively interesting examples: database queries, parallel actor pipelines, structured output, MCP servers backed by local models, CAS-assisted maths tutors, and more.

---

### LLVM JIT backend

When built with `-DBUILD_LLVM=ON`, Curry adds a tiered native-compilation layer on top of the bytecode VM: any closure called ≥ 50 times is automatically compiled to native ARM64/x86-64 on the next call, transparently — no source changes needed. Typical speedups on recursive/loop-heavy code range 1.1×-14×. See [`docs/reference/llvm-jit.md`](docs/reference/llvm-jit.md) for the procedure list, benchmark numbers, and build command.

---

### C FFI

When built with `-DBUILD_FFI=ON`, Curry can call any C library directly from Scheme with no glue code.

```scheme
(import (curry ffi))

(define-foreign-library libm "libm.so")     ; Linux
; (define-foreign-library libm "libm.dylib") ; macOS

(define-foreign (c-sin  (x double)) → double #:from libm #:c-name "sin")
(define-foreign (c-sqrt (x double)) → double #:from libm #:c-name "sqrt")

(c-sin 1.5707963)   ; → 1.0
(c-sqrt 2.0)        ; → 1.41421...
```

**Zero-copy matrix passthrough** — pass a matrix or tensor's raw `double*` directly to C (e.g. BLAS) with no copying:

```scheme
(with-pinned-matrix A pa
  (with-pinned-matrix B pb
    (with-pinned-matrix C pc
      (cblas-dgemm CblasRowMajor CblasNoTrans CblasNoTrans
                   rows cols inner 1.0 pa cols pb cols 0.0 pc cols))))
```

Requires `libffi-dev` on Linux; found automatically via Homebrew on macOS. See [`docs/reference/module-ffi.md`](docs/reference/module-ffi.md) for the full API and type-mapping table.

---

### Garbage collector

Curry ships two GC backends, selectable at runtime with `--gc boehm` (default, conservative, no configuration) or `--gc generational` (experimental per-thread nursery, lower pause times for allocation-heavy workloads). `(gc-stats)`/`(gc-stats-reset!)` expose live counters and pause times. See [`docs/reference/gc.md`](docs/reference/gc.md) for the full backend/flag/stats-field reference, [docs/reference/benchmarking.md](docs/reference/benchmarking.md) for the real-time Grafana monitoring stack, and [docs/reference/profiling.md](docs/reference/profiling.md) for the runtime profiler.

---

## Installation & Building

See [docs/guides/INSTALL.md](docs/guides/INSTALL.md) for Homebrew installation (macOS), building from source on Linux and macOS (including Qt6 and `.deb`/`.rpm` packaging), and running the test suite.

---

## Akkadian error messages

All runtime errors carry a Standard Babylonian preamble identifying the fault category:

```
𒀭 ḫiṭītu — lā nikkassum:
  +: not a number: "hello"
```

See [`docs/reference/akkadian-reference.md`](docs/reference/akkadian-reference.md#error-messages) for the full phrase table.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
