# Curry Scheme

Curry is an R7RS Scheme implementation with practical R6RS compatibility, a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices, tensors, and spinors, a CL-style condition system with restarts, a general C FFI, STM and CSP channels alongside the actor-model concurrency system, a modular C extension interface, and a built-in LLM client that can talk to Claude, GPT-4o, Ollama, or any OpenAI-compatible endpoint — with multi-turn conversation, tool use, and a full agentic loop.

Source is compiled to bytecode and executed on a stack-based VM. When built with LLVM (`-DBUILD_LLVM=ON`), hot closures are automatically compiled to native machine code after 50 calls via an ORC v2 JIT backend. Compiled chunks are cached in `.scc` files (source-adjacent, or `~/.cache/curry/` for read-only paths) and reused on subsequent runs, invalidated automatically on source change or version bump. Use `-c file.scm` to pre-compile without running; `.scc` files can also be passed directly as the script argument.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

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
- [Akkadian / Cuneiform reference](docs/reference/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages

### Guides

- [Installation](docs/guides/INSTALL.md) — Homebrew (macOS), build from source, Qt6, test suite
- [LLM integration](docs/guides/guide-llm.md) — talking to Claude, OpenAI, and Ollama; tool use, agentic loops, structured output, MCP servers, CAS-backed AI
- [Raspberry Pi / embedded hardware](docs/guides/RPI.md) — setup guide for Pi; GPIO, I2C, SPI, PWM
- [MCP server](docs/guides/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients
- [macOS app bundler](docs/guides/make-macos-app.md) — bundle any Curry script as a `.app` with Qt frameworks embedded

### Extended numeric tower

| Level | Type | Entry point |
|-------|------|-------------|
| Fixnum | 62-bit signed integer | literals |
| Bignum | arbitrary precision integer | `(expt 2 200)` |
| Rational | exact ratio | `3/4`, `(/ 1 3)` |
| Flonum | IEEE 754 double | `1.5`, `+inf.0`, `+nan.0` |
| Complex | rectangular or polar | `(make-rectangular 3 4)` |
| Quaternion | 4-component hypercomplex | `(make-quaternion a b c d)` |
| Octonion | 8-component non-associative | `(make-octonion ...)` |
| Multivector | Clifford algebra Cl(p,q,r) | `(make-mv p q r)` |
| Surreal | Hahn series with ω and ε | `SUR_OMEGA`, `SUR_EPSILON` |
| Symbolic | CAS expression tree | `(symbolic x)` |

Arithmetic automatically promotes through the tower. `(+ 1/3 0.5)` → flonum. `(∂ (* x x) x)` → symbolic `(+ x x)`.

### CAS and auto-differentiation

| Procedure | Description |
|-----------|-------------|
| `(symbolic x y ...)` | Declare symbolic variables in scope |
| `(sym-var 'x)` | Create a symbolic variable object directly |
| `(sym-var? v)` / `(sym-expr? v)` / `(symbolic? v)` | Predicates |
| `(sym-var-name v)` | Variable name as string |
| `(∂ expr var)` | Symbolic differentiation (alias: `sym-diff`) |
| `(∫ expr var)` | Indefinite integration (alias: `integrate`) |
| `(∫ expr var a b)` | Definite integral from a to b |
| `(simplify expr)` | Algebraic simplification |
| `(substitute expr var val)` | Substitute and evaluate |
| `(conj expr)` / `(real-part expr)` / `(imag-part expr)` | Complex operators — symbolic-aware |
| `(wirtinger-d expr z)` | Wirtinger ∂/∂z (treats z and z̄ as independent) |
| `(wirtinger-dbar expr z)` | Wirtinger ∂/∂z̄ — zero iff expr is holomorphic |
| `(auto-diff f x)` | Numeric derivative at a point via dual-number ε |
| `(frac-diff expr α var)` | Caputo symbolic fractional derivative D^α |
| `(frac-int expr α var)` | Riemann-Liouville symbolic fractional integral I^α |
| `(quad-frac-diff f α x)` | Grünwald-Letnikov numerical D^α (for non-symbolic f) |
| `(quad-frac-int f α x)` | Numerical RL fractional integral |
| `(quad f a b)` | Gauss-Kronrod G7K15 adaptive numerical quadrature |
| `(sym->string expr)` / `(sym->infix expr)` | Infix string: `x^2 + 2*x + 1` |
| `(sym->latex expr)` | LaTeX string: `x^{2} + 2 x + 1` |

`∂` and `∫` are Unicode (U+2202, U+222B); ASCII aliases `sym-diff` and `integrate` are equivalent. All standard numeric operators lift automatically over symbolic values.

### Parallel map and reduce

`map` and `reduce` automatically parallelise over multiple CPU cores; sequential variants are always available.

| Procedure | Description |
|-----------|-------------|
| `(map f lst)` | Parallel when list exceeds threshold (default: 8 elements) |
| `(map/seq f lst ...)` | Always sequential; full multi-list R7RS `map` |
| `(reduce f identity lst)` | Parallel associative fold |
| `(reduce/seq f identity lst)` | Always sequential |
| `(for-each/par f lst)` | Parallel for-each — opt-in, order unspecified |
| `(map-parallel-threshold)` | Return current threshold |
| `(set-map-parallel-threshold! n)` | Set threshold |
| `(hardware-concurrency)` | Logical CPU count (= pool thread count) |

Backed by a **persistent work-stealing thread pool** (Chase-Lev deques, 4× oversubscription). No per-call thread-spawn overhead — the pool is created once at startup and lives for the process lifetime. Exceptions in worker threads propagate to the caller. `reduce` requires a true identity element; it is not used as a per-thread seed. `for-each` remains sequential; `for-each/par` is the explicit opt-in for independent side effects.

### Random numbers (SRFI-27)

The global source is seeded from `/dev/urandom` on first use (xoshiro256+).

| Procedure | Description |
|-----------|-------------|
| `(random-real)` | Uniform flonum in \[0, 1) |
| `(random-integer n)` | Uniform exact integer in \[0, n) |
| `(random-source-randomize! src)` | Re-seed from `/dev/urandom` |
| `default-random-source` | The global random source |

### Modules

| Module | Import | Description | Extra deps |
|--------|--------|-------------|------------|
| [json](docs/reference/module-json.md) | `(curry json)` | JSON parse / stringify | — |
| [sqlite](docs/reference/module-sqlite.md) | `(curry sqlite)` | SQLite3 database | `libsqlite3-dev` |
| [network](docs/reference/module-network.md) | `(curry network)` | TCP / UDP sockets | — |
| [crypto](docs/reference/module-crypto.md) | `(curry crypto)` | base64, MD5, SHA-256, HMAC | `libssl-dev` |
| [ldap](docs/reference/module-ldap.md) | `(curry ldap)` | LDAP / LDAPS directory access | `libldap-dev` |
| [http](docs/reference/module-http.md) | `(curry http)` | General-purpose HTTP client — any method, headers, body | `libcurl4-openssl-dev` |
| [llm](docs/reference/module-llm.md) | `(curry llm)` | LLM client: Claude, OpenAI, Ollama, any OpenAI-compat endpoint; tool use, agentic loop *(pure Scheme, no build step)* | `(curry http)` |
| [storage](docs/reference/module-storage.md) | `(curry storage)` | S3, Swift, Azure Blob, GCS | `libcurl4-openssl-dev` |
| [graphql](docs/reference/module-graphql.md) | `(curry graphql)` | GraphQL HTTP client | `libcurl4-openssl-dev` |
| [redis](docs/reference/module-redis.md) | `(curry redis)` | Redis client (RESP2, no hiredis) | — |
| [neo4j](docs/reference/module-neo4j.md) | `(curry neo4j)` | Neo4j graph database client (Bolt 4.x/5.x, no libneo4j) | — |
| [image](docs/reference/module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](docs/reference/module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [qt6](docs/reference/module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](docs/reference/module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](docs/reference/module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](docs/reference/module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](docs/reference/module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |
| [stm](docs/reference/concurrency.md) | `(curry stm)` | STM (`atomically`/`retry`/`or-else`), CSP channels, `select` macro | — |
| [conditions](docs/reference/language.md#condition-system-cl-style----import-curry-conditions) | `(curry conditions)` | CL condition system: `handler-bind`, `with-restarts`, `invoke-restart`, `handler-case` | — |
| [ffi](docs/reference/module-ffi.md) | `(curry ffi)` | General C FFI: `define-foreign`, zero-copy matrix/tensor passthrough | `libffi-dev` (`-DBUILD_FFI=ON`) |
| [mqtt](docs/reference/module-mqtt.md) | `(curry mqtt)` | MQTT client: publish, subscribe, QoS 0/1/2, TLS | `libpaho-mqtt-dev` |
| [ode](docs/reference/module-ode.md) | `(curry ode)` | ODE solvers: Euler, RK4, Dormand-Prince RK45, Verlet | — |
| [mcp](docs/guides/mcp-clients.md) | `(curry mcp)` | MCP server: expose Curry tools to AI clients via stdio or SSE | — |
| [profiling](docs/reference/module-profiling.md) | `(curry profiling)` | Runtime call-count and wall-clock profiler for named closures and primitives | — |
| [rpi](docs/reference/module-rpi.md) | `(curry rpi)` | GPIO, I2C, SPI, PWM for Raspberry Pi and Linux embedded boards *(Linux only)* | `libgpiod-dev` |
| [sicm](docs/reference/module-sicm.md) | `(curry sicm)` | Classical mechanics (SICM): Lagrangian, Hamiltonian, Poisson brackets | — |

### SRFI compatibility (`surfage`)

Portable SRFI libraries under the `(surfage sN name)` naming convention — compatible with Guile, Chicken, and Chibi-Scheme:

| Library | Import | Description |
|---------|--------|-------------|
| [s1 lists](docs/reference/module-surfage.md) | `(surfage s1 lists)` | SRFI-1: `iota`, `any`, `every`, `fold`, `take/drop`, `partition`, … |
| [s27 random-bits](docs/reference/module-surfage.md) | `(surfage s27 random-bits)` | SRFI-27: random sources, `random-integer`, `random-real` |

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

When built with `-DBUILD_LLVM=ON`, Curry adds a tiered native-compilation layer on top of the bytecode VM.

| Procedure | Description |
|-----------|-------------|
| `(curry-llvm-available?)` | `#t` if the JIT backend is compiled in |
| `(jit-compile! proc)` | Force-compile a bytecode closure to native code immediately |
| `(jit-compiled? proc)` | `#t` if the closure has been JIT-compiled |

**Auto-JIT**: any `BcClosure` called ≥ 50 times is automatically compiled to native ARM64 or x86-64 on the next call. The compiled version replaces the bytecode interpreter transparently — no source changes needed.

Typical speedups (Apple M-series, recursive and loop-heavy code):

| Benchmark | Bytecode | JIT | Speedup |
|-----------|----------|-----|---------|
| `(fib 25)` recursive | 54 ms | 9.2 ms | **5.9×** |
| `named-let` tight loop (10 000 iters) | 2.3 ms | 0.16 ms | **14×** |
| flonum arithmetic loop | 3.9 ms | 3.4 ms | 1.1× |

See [`examples/bench_jit.scm`](examples/bench_jit.scm) to run the comparison on your machine.

Build with JIT + Qt6 + all optional modules:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LLVM=ON \
  -DBUILD_MODULE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm);$(brew --prefix qt@6)"
cmake --build build-release -j$(sysctl -n hw.logicalcpu)
```

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

Supported types: `int`, `uint`, `long`, `size-t`, `double`, `float`, `c-ptr`, `string`, `bool`, `void`. Requires `libffi-dev` on Linux; found automatically via Homebrew on macOS.

See [`docs/reference/module-ffi.md`](docs/reference/module-ffi.md) for the full API.

---

## Installation & Building

See [docs/guides/INSTALL.md](docs/guides/INSTALL.md) for Homebrew installation (macOS), building from source on Linux and macOS (including Qt6 and `.deb` packaging), and running the test suite.

---

## Akkadian error messages

All runtime errors carry a Standard Babylonian preamble identifying the fault category. Selected phrases:

| Situation | Akkadian | Gloss |
|-----------|----------|-------|
| Unbound variable | *šumu lā šakin* | the name is not established |
| Wrong type (pair expected) | *lā qitnum* | not a small thing |
| Wrong type (number) | *lā nikkassum* | not a count |
| Wrong type (string) | *lā ṭupšarrum* | not a scribal tablet |
| Wrong type (procedure) | *lā pārisum* | not a resolver/judge |
| Division by zero | *ina ṣifri pašāṭum lā leqû* | cannot erase with the void |
| Module not found | *bīt ṭuppi lā ibašši* | the tablet house does not exist |
| File cannot be opened | *ṭuppu lā petûm* | the tablet cannot be opened |
| Actor dead | *ana erṣetim ittalak* | it has gone to the underworld |
| Unknown error | *ḫiṭītu rabîtum* | great fault |

Example:

```
𒀭 ḫiṭītu — lā nikkassum:
  +: not a number: "hello"
```

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
