# Curry Scheme

Curry is an R7RS Scheme interpreter with a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices and tensors, an actor-model concurrency system, and a modular C extension interface.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

## Documents

### Language

- [Language reference](docs/language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](docs/symbolic.md) — CAS reference: variables, differentiation, integration, simplification, substitution, complex operators, Wirtinger calculus, auto-diff
- [Quantum superposition](docs/quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](docs/surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](docs/multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](docs/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages
- [MCP server](docs/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients; stdio and SSE transports
- [Raspberry Pi / embedded hardware](docs/RPI.md) — setup guide for Pi; GPIO, I2C, SPI, PWM with the `(curry rpi)` module

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

### Modules

| Module | Import | Description | Extra deps |
|--------|--------|-------------|------------|
| [json](docs/module-json.md) | `(curry json)` | JSON parse / stringify | — |
| [sqlite](docs/module-sqlite.md) | `(curry sqlite)` | SQLite3 database | `libsqlite3-dev` |
| [network](docs/module-network.md) | `(curry network)` | TCP / UDP sockets | — |
| [crypto](docs/module-crypto.md) | `(curry crypto)` | base64, MD5, SHA-256, HMAC | `libssl-dev` |
| [ldap](docs/module-ldap.md) | `(curry ldap)` | LDAP / LDAPS directory access | `libldap-dev` |
| [storage](docs/module-storage.md) | `(curry storage)` | S3, Swift, Azure Blob, GCS | `libcurl4-openssl-dev` |
| [graphql](docs/module-graphql.md) | `(curry graphql)` | GraphQL HTTP client | `libcurl4-openssl-dev` |
| [redis](docs/module-redis.md) | `(curry redis)` | Redis client (RESP2, no hiredis) | — |
| [neo4j](docs/module-neo4j.md) | `(curry neo4j)` | Neo4j graph database client (Bolt 4.x/5.x, no libneo4j) | — |
| [image](docs/module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](docs/module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [qt6](docs/module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](docs/module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](docs/module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](docs/module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](docs/module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |
| [mqtt](docs/module-mqtt.md) | `(curry mqtt)` | MQTT client: publish, subscribe, QoS 0/1/2, TLS | `libpaho-mqtt-dev` |
| [ode](docs/module-ode.md) | `(curry ode)` | ODE solvers: Euler, RK4, Dormand-Prince RK45, Verlet | — |
| [mcp](docs/mcp-clients.md) | `(curry mcp)` | MCP server: expose Curry tools to AI clients via stdio or SSE | — |
| [profiling](docs/module-profiling.md) | `(curry profiling)` | Runtime call-count and wall-clock profiler for named closures and primitives | — |
| [rpi](docs/module-rpi.md) | `(curry rpi)` | GPIO, I2C, SPI, PWM for Raspberry Pi and Linux embedded boards *(Linux only)* | `libgpiod-dev` |

---

## Installation & Building

See [docs/INSTALL.md](docs/INSTALL.md) for Homebrew installation (macOS), building from source on Linux and macOS (including Qt6 and `.deb` packaging), and running the test suite.

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

### 0.8.0 — Maxwell's equations: four interactive workbooks

Four interactive Qt6 demos — one per Maxwell equation — each paired with a
student guide that derives the physics, walks through the simulation, and
includes guided exercises.  All four use the built-in symbolic CAS to verify
the relevant identity live in the sidebar.

- **Faraday's Law** (`examples/faraday-explorer.scm`, `docs/faraday-explorer.md`)  
  Animated solenoid with time-varying B; induced E_φ computed from ∇×E = −∂B/∂t.
  Exact two-region solution (linear / 1/r), EMF saturation, Lenz's-law phase demo.
  CAS: verifies ∇×E + ∂B/∂t = 0 for a plane wave symbolically.

- **Ampère's Law** (`examples/ampere-explorer.scm`, `docs/ampere-explorer.md`)  
  Two modes toggled live: conduction current (wire) vs. displacement current
  (capacitor charging).  Demonstrates the 90° phase contrast between the two.
  CAS: verifies ∇×B − μ₀ε₀∂E/∂t = 0 for a plane wave.

- **Gauss's Law for E** (`examples/gauss-e-explorer.scm`, `docs/gauss-e-explorer.md`)  
  Uniformly-charged sphere; Gaussian surface draggable from centre to exterior.
  Shows flux saturation at r = R and the r³ / r² field profile.  Sign toggle.
  CAS: ∇·(r̂/3) = 1 = ρ/ε₀ (normalised units).

- **Gauss's Law for B** (`examples/gauss-b-explorer.scm`, `docs/gauss-b-explorer.md`)  
  2D magnetic dipole; positionable Gaussian surface demonstrates ∮B·n̂dl = 0
  when both poles are enclosed, and what a monopole *would* look like.
  CAS: proves ∇·(∇×A) = 0 identically for a concrete A = (0, xy, xyz).

---

### 0.7.9 — CAS Phase 4: limits, IBP integration, vector calculus

**Symbolic integration — new patterns:**
- **Integration by parts** for polynomial × trig/exp products: `∫x·sin(x)`, `∫x·cos(x)`, `∫x·exp(x)`, and iterated IBP for `∫x²·sin(x)` etc.
- **Polynomial × logarithm** (LIATE rule): `∫x^n·ln(x) = x^(n+1)·ln(x)/(n+1) − x^(n+1)/(n+1)²`
- **Trig power reductions** via half-angle: `∫sin²(f) = x/2 − sin(2f)/(4f′)`, `∫cos²(f) = x/2 + sin(2f)/(4f′)`
- **Quadratic denominator**: `∫c/(ax²+bx+d) = 2c/√Δ · atan((2ax+b)/√Δ)` when Δ=4ad−b²>0; handles completing-the-square automatically

**New `limit` procedure:**
- `(limit f x a)` — two-sided limit; `(limit f x a 'left/'right)` for one-sided
- Direct substitution, L'Hôpital for 0/0 and ∞/∞ (up to 5 applications), `finite/∞ = 0`
- Three-deep L'Hôpital works: `(limit (/ (- x (sin x)) (expt x 3)) x 0)` → `1/6`

**Vector calculus (Cartesian, N-dimensional):**
- `(grad f vars)` / `(gradient f vars)` — gradient of a scalar field
- `(divergence F vars)` — divergence of a vector field
- `(curl F vars)` — curl (3D)
- `(laplacian f vars)` / `(vec-laplacian F vars)` — scalar and vector Laplacian
- `(dot-product A B)` / `(cross-product A B)` — symbolic dot and cross products
- Identities verified symbolically: `div(curl F) = 0`, `curl(grad f) = (0 0 0)`
- Maxwell's equations verified for a plane wave in vacuum (see `docs/symbolic.md`)

**Simplifier improvements:**
- `a − a = 0` for any structurally-equal symbolic expressions
- `a + (−a) = 0` cancellation in the ADD simplifier

---

### 0.7.9 — Raspberry Pi hardware module

**New module `(curry rpi)`** — GPIO, I2C, SPI, and PWM for Raspberry Pi and
compatible Linux embedded boards (Orange Pi, Radxa, Armbian, etc.).  Linux
only; not supported on macOS.  Enable with `-DBUILD_MODULE_RPI=ON`.

- **GPIO** via `libgpiod` — the modern kernel character-device interface
  (`/dev/gpiochipN`).  Replaces the deprecated sysfs approach.
  `gpio-open`, `gpio-read`, `gpio-write`, `gpio-close`
- **I2C** via direct `ioctl` on `/dev/i2c-N` — no extra library beyond
  `libgpiod-dev`.  `i2c-open`, `i2c-read`, `i2c-write`, `i2c-close`
- **SPI** via direct `ioctl` on `/dev/spidevN.M` — full-duplex transfers as
  bytevectors.  `spi-open`, `spi-transfer`, `spi-close`
- **PWM** via sysfs `/sys/class/pwm` — nanosecond precision, works with
  `dtoverlay=pwm`.  `pwm-open`, `pwm-set!`, `pwm-enable!`, `pwm-disable!`, `pwm-close`
- All handles are opaque tagged pairs; predicate procedures (`gpio?`, `i2c?`,
  `spi?`, `pwm?`) provided for each type
- Setup guide with hardware examples at [docs/RPI.md](docs/RPI.md)
- Full API reference at [docs/module-rpi.md](docs/module-rpi.md)

---

### 0.7.8 — Profiling level-2 overhaul, raw builtins, solar system HUD

**Profiling level 2 — accurate wall-clock timing**:
- Level 2 now intercepts named closures *before* the `goto tail` optimisation so that a real return address exists and wall-clock time can be measured per call, not just counted. Previously, timing only covered the `apply()` path; now it covers every call to a named closure except self-tail-recursive ones
- Self-tail-recursive calls (where a closure calls itself as its own tail position) are exempted from the intercept — they fall through to the normal TCO path — to prevent unbounded stack growth in hot loops. They are still counted. Mutually recursive functions are fully timed
- The level-2 description in `docs/module-profiling.md` updated to document the trade-off

**Raw built-in procedures** (no import needed):
- `(profiling-report)` — equivalent to the module's `(profiler-report)`; returns the sorted `((name . (calls . ns)) ...)` alist
- `(profiling-reset)` — equivalent to `(profiler-reset)`; clears accumulated data
- Together with `(set! **eval-profiler** 2)`, these let scripts enable and query the profiler without importing `(curry profiling)`

**Solar system demo — live profiling HUD** (`examples/solar-system-qt6.scm`):
- New overlay displaying the top 12 hottest named closures by accumulated wall-clock time, heat-mapped from yellow (hottest) to grey, updated every animation frame
- Toggle with the **Profile HUD \[p\]** sidebar checkbox or by pressing **`p`**
- **Reset Profiler** button clears counters without restarting the simulation
- Demo enables level 2 at startup via `(set! **eval-profiler** 2)`

---

### 0.7.7 — R7RS compliance, fold fixes, extended Akkadian vocabulary, runtime profiler

**R7RS base-library completeness** — all missing procedures and special forms added:

- `let-values` / `let*-values` — destructuring bind over multiple return values
- `case =>` clause — apply a procedure to the matched key value
- `make-list k [fill]`
- `string-copy`, `string->list`, `vector->list` — optional `start`/`end` indices
- `string-for-each proc string [string ...]`
- `string-fill! string char [start [end]]`
- `string-foldcase` / `char-foldcase`
- `write-string string [port [start [end]]]`
- `string->utf8` / `utf8->string` with optional `start`/`end`
- `vector-copy! to at from [start [end]]`
- `vector-map proc vec [vec ...]`
- `vector-for-each proc vec [vec ...]`

All 12 test suites continue to pass (100%).

**`fold-left` / `fold-right` correctness fix**:
- `fold-left` argument order corrected from SRFI-1 `(proc element acc)` to R6RS `(proc acc element)`. The two conventions agree for commutative operations like `+` but differ for `cons`, `string-append`, and any order-sensitive reduction. `(fold-left string-append "0" '("1" "2" "3"))` now yields `"0123"` (was `"3210"`)
- `fold-right` added: `fold-right` was present in the Akkadian name table (`lapātum-imittam` / 𒇲𒌋) but was never registered as a builtin — calling it silently did nothing. Now registered and working: `(fold-right cons '() '(1 2 3))` → `(1 2 3)`

**Akkadian / cuneiform vocabulary extended**:
- Full numeric tower operations — quaternion, octonion, multivector, surreal, and CAS procedures all have Standard Babylonian Akkadian synonyms and cuneiform aliases
- Language reference and Akkadian reference updated to cover the complete vocabulary

**Profiling module** (`(curry profiling)`):
- `(profiler-start [level])` — enable profiling at level 1 (call counts), 2 (+ wall-clock timing via `apply()`), or 3 (+ primitive call counts). Updates the `**eval-profiler**` Scheme binding
- `(profiler-stop)` — set level to 0; accumulated data is preserved
- `(profiler-reset)` — clear all accumulated data
- `(profiler-level)` — return current level as a fixnum
- `(profiler-report)` — return an alist `((name . (calls . ns)) ...)` sorted by call count, descending
- TCO tail-calls are counted at all levels but not timed (no exit point on the `goto tail` path); apply-path calls are timed at level ≥ 2
- Instrumentation is always compiled into the core binary; when profiling is off, the hot-path check is a single integer compare with branch predictor predicting not-taken — effectively zero overhead
- `examples/profiling_mcp.scm` — MCP server wrapping the profiler as Claude Code tools

**Examples**:
- `examples/quantum_scenarios.scm` — three practical applications of the quantum superposition type: epistemic uncertainty modelled as a quantum value, arithmetic lifted over branches without collapsing, `(observe)` / `(quantum-states)` used for decision-making and distribution analysis

---

### 0.7.6 — Qt6 interactivity, Mandelbrot fixes, Neo4j documentation

**Qt6 module — new input events**:
- `(canvas-on-scroll! canvas proc)` — scroll wheel and trackpad two-finger scroll; callback receives `(dx dy x y mods)`. `dy > 0` = scroll up / zoom in. Pixel delta is used when available (trackpad), angle delta (wheel mouse) otherwise
- Mouse double-click now delivered as `'double-press` event type through the existing `canvas-on-mouse!` callback
- `(run-event-loop)` now calls `::exit(0)` after the Qt event loop returns, preventing a hang during Metal/OpenGL surface teardown on macOS

**Mandelbrot example — five correctness fixes**:
- **Concurrent read/write race**: introduced `*display-buf*` double-buffer; workers write to `*frame-buf*`, the coordinator atomically swaps to `*display-buf*` on completion — the paint thread never reads a buffer that's being written
- **Stale coordinator race**: each render captures its `*render-tag*` at spawn time; the coordinator only swaps the display buffer and clears `*rendering*` if the tag is still current, preventing a superseded render from clobbering in-progress state
- **Dead resize detection**: moved the canvas-resize check from `draw-frame` (where `*W*`/`*H*` were already updated) into the draw callback, using a `resized?` flag captured before the update
- **`timer-stop!` nil-guard**: `render-tick!` now guards `(when *render-timer* ...)` consistently
- **Scroll wheel and double-click zoom**: both now use the new Qt6 module events; zoom is cursor-centred

**Neo4j module**:
- `(curry neo4j)` is now documented: full reference at `docs/module-neo4j.md`, entry added to the module table
- The module was already fully implemented (Bolt 4.x/5.x, PackStream, transactions); this release makes it discoverable

### 0.7.5 — CAS expansion: transcendentals and polynomial operations

**Phase 1 — 12 new transcendental functions** (symbolic diff, integrate, Wirtinger, infix/LaTeX output, numeric evaluation):
- Hyperbolic: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`
- Inverse trig: `asin`, `acos`, `atan`
- Reciprocal trig: `cot`, `sec`, `csc`
- All carry full chain-rule differentiation, linear-argument integration (IBP for inverse trig), and holomorphic Wirtinger rules
- Complex numeric evaluation via logarithmic identities: `asin(z) = -i·ln(iz+√(1-z²))`, etc.

**Phase 2 — 4 new polynomial/structural operations**:
- `(expand expr)` — distribute `*` over `+`; expand integer powers 2..16
- `(degree expr var)` — polynomial degree in a variable (exact fixnum)
- `(leading-coeff expr var)` — coefficient of the highest-degree term
- `(collect expr var)` — combine like-degree terms; canonical descending form

**Bug fixes**:
- `num_sub` was missing the complex-number branch (pre-existing); mixed real/complex subtraction now works correctly
- `asin`/`acos`/`atan` were generated via a macro that omitted the symbolic dispatch check; applying them to symbolic variables no longer crashes

**Documentation**:
- Build and installation instructions extracted from `README.md` into `docs/INSTALL.md`

### 0.7.3.1 — ODE solver module

- Added `(curry ode)`: pure Scheme ODE solvers for initial-value problems `dy/dt = f(t, y)`
- **Euler** — first-order, fixed step
- **RK4** — classical fourth-order Runge-Kutta, fixed step; exact for polynomials of degree ≤ 4
- **RK45** — Dormand-Prince adaptive step (the algorithm behind MATLAB's `ode45` and SciPy's `RK45`); step size controlled automatically to meet a tolerance
- **Verlet** — velocity-Verlet symplectic integrator for Hamiltonian systems; conserves energy over long integrations where RK methods drift
- All methods accept scalar `y` (single ODE) or list `y` (system of ODEs)
- Works with the full numeric tower: exact rationals, complex numbers, and symbolic expressions
- All methods have `/steps` variants returning `((t . y) ...)` snapshots at every accepted step
- 30 tests covering all four methods against closed-form solutions

### 0.7.3 — MQTT client module

- Added `(curry mqtt)` module: full MQTT client using the Eclipse Paho C synchronous API (`libpaho-mqtt3cs`)
- Plain TCP and TLS connections: `mqtt-connect`, `mqtt-connect-tls`
- Publish (`mqtt-publish`) with QoS 0/1/2 and optional retain flag
- Subscribe / unsubscribe with per-topic QoS: `mqtt-subscribe`, `mqtt-unsubscribe`
- Blocking receive with timeout: `mqtt-receive` returns `(topic . payload)` or `#f`
- Incoming messages delivered via a native ring-buffer queue (mutex + condvar) — Paho callback thread never touches the Scheme/GC heap
- Test harness (`tests/test_mqtt.sh`) spins up an ephemeral Mosquitto broker (plain + TLS with a fresh self-signed cert); 14 tests cover pub/sub ordering, QoS 1, wildcard subscriptions, timeout, and TLS
- Redis TLS (`redis-connect-tls`) and Redis tests (40 tests via `tests/test_redis.sh`) added in this cycle

### 0.7.2 — MCP server module and packaging

- **Homebrew formula** (`Formula/curry.rb`) — install on macOS via `brew tap deconstructo/curry && brew install curry`; pre-builds sqlite, crypto, ldap, storage, image, and git modules automatically
- **Debian package** — `cpack -G DEB` produces `curry-scheme_0.7.2_<arch>.deb`; installs to standard system paths with correct `Depends` / `Recommends`
- Added `(curry mcp)` module: expose Curry procedures as [Model Context Protocol](https://modelcontextprotocol.io/) tools callable from Claude Code and other AI clients
- **stdio transport** — JSON-RPC 2.0 over stdin/stdout; one client per process, spawned by the MCP client (`mcp-serve`)
- **SSE transport** — persistent HTTP + Server-Sent Events server; multiple concurrent clients on one port (`mcp-serve-sse`)
- Progress notifications (`mcp-notify-progress`) for long-running tool calls
- Example servers: `mcp_server.scm` (eval, factorial, stateful define, progress demo), `mcp_math.scm` (CAS: differentiation, simplification, auto-diff, Taylor series), `mcp_nbody.scm` (N-body gravity in D dimensions)

### 0.1.7 — Matrix, tensor, and gravity simulator

- First-class `Matrix` and `Tensor` types with arithmetic, map, fold, and slicing
- `(curry math matrix)` and `(curry math tensor)` loadable Scheme modules
- `(curry gravity)` — continuous-dimension physics simulator: gravity and electromagnetism in non-integer spatial dimension D
- `syntax-rules` macro expander implemented; `parameterize` / `dynamic-wind` interaction fixed
- Qt6 module hardened against Scheme exceptions escaping across C++ stack frames

### 0.1.5 — Dynamic-wind, macOS support, new modules

- `dynamic-wind` implemented; `with-mutex` deadlock fixed; port finalizer added
- Full macOS build support (Apple Silicon and x86_64); `sem_init` portability fix
- `plplot`, `regex`, and `sync` modules added
- `trace` / `untrace` for tracing calls to global procedures
- Memory safety: use-after-free bugs, leaks, and symbol table data race fixed
- `tesseract.scm` demo with anaglyph stereoscopic 3D support

### 0.1.0 — Initial release

- R7RS Scheme interpreter with tree-walking evaluator and proper tail-call optimisation via `goto tail`
- Numeric tower: fixnum → bignum (GMP) → rational → flonum → complex → quaternion → octonion → multivector (Clifford Cl(p,q,r)) → surreal (Hahn series) → symbolic CAS
- Actor-model concurrency via pthreads: `spawn`, `send!`, `receive`
- Standard Babylonian Akkadian error messages with cuneiform preambles (𒀭 ḫiṭītu)
- Akkadian/cuneiform synonym evaluation — Curry source code can be written in Standard Babylonian Akkadian
- Modules: `json`, `network`, `redis`, `sqlite`, `crypto`, `ldap`, `storage`, `graphql`, `image`, `git`, `qt6`, `vecdb`
