# Changelog

### 0.8.4 — GPIO interrupts and Akkadian completeness

**GPIO interrupt support** (`(curry rpi)` module):

- `gpio-open` now accepts `'rising`, `'falling`, and `'both` as direction modes, configuring a line for libgpiod edge-event monitoring instead of plain input/output.
- **`(gpio-wait-edge handle [timeout-ms])`** — blocking wait for a GPIO edge using `poll()` on the libgpiod event fd. Returns `'rising`, `'falling`, or `#f` on timeout. Pass `-1` (default) to wait indefinitely. Designed to be wrapped in `spawn` for async use.
- **`(gpio-watch handle proc)`** — spawns a background C thread that calls `(proc edge timestamp-ns)` on each interrupt. The Scheme callback is kept alive as a Boehm GC root for the lifetime of the watcher. Returns a watcher handle.
- **`(gpio-unwatch watcher)`** — signals the watcher thread via a stop-pipe, joins it, removes the GC root, and frees the struct.
- **`(watcher? v)`** — predicate.

**Akkadian/cuneiform completeness** — ~69 new transliterated and cuneiform aliases added to cover all R7RS procedures introduced in v0.8.3 (plus several from v0.7.7 that were missing):

- Arithmetic: `square` (*mitḫartum*), `exact-integer?`, `truncate/`, `truncate-quotient`, `truncate-remainder`, `exact-integer-sqrt` (*ibum-kinattu*)
- I/O: binary port procedures (`read-u8`, `write-u8`, `peek-u8`, `u8-ready?`), `read-string`, `read-bytevector`, `write-bytevector`, file operations (`file-exists?`, `delete-file`, `call-with-input-file`, `call-with-output-file`, `with-input-from-file`, `with-output-to-file`)
- Strings: all ordering comparators (`string<=?` through `string-ci>=?`), `string-set!`, `string-copy!`, `string-for-each`, `string-fill!`, `string-foldcase`, `string->utf8`, `utf8->string`
- Bytevectors: complete suite (`make-bytevector`, `bytevector`, `bytevector-length`, `bytevector-u8-ref`, `bytevector-u8-set!`, `bytevector-copy`, `bytevector-copy!`, `bytevector-append`)
- Characters: all comparators (`char=?` through `char>=?`), all case-insensitive variants, `digit-value`, `char-foldcase`
- Vectors: `vector-append`, `vector-copy!`
- Process context: `get-environment-variable`, `get-environment-variables`, `emergency-exit`
- Time: `current-second`, `current-jiffy`, `jiffies-per-second`
- Error objects: `error-object-message`, `read-error?`, `file-error?`
- Lists: `make-list`

**Internal**: `src/builtins.c` split into `src/builtins.c` (R7RS standard procedures) and `src/builtins_curry.c` (CAS, vector calculus, quantum, surreal, quadrature extensions). `defprim()` made non-static for cross-file use.

---

### 0.8.3 — R7RS compliance gap-fill and RPi test suite

**R7RS compliance** — ~50 new procedures filling the remaining gaps in `(scheme base)`, `(scheme char)`, `(scheme file)`, `(scheme process-context)`, `(scheme time)`, and `(scheme write)`:

- **Arithmetic**: `square`, `exact-integer?`, `truncate/`, `truncate-quotient`, `truncate-remainder`, `exact-integer-sqrt`
- **Characters**: `char<=?`, `char>?`, `char>=?`, `char-ci=?`, `char-ci<?`, `char-ci>?`, `char-ci<=?`, `char-ci>=?`, `digit-value`
- **Strings**: `string<=?`, `string>?`, `string>=?`, `string-ci=?`, `string-ci<?`, `string-ci>?`, `string-ci<=?`, `string-ci>=?`, `string-upcase`, `string-downcase`, `string-set!`, `string-copy!`
- **Bytevectors**: `bytevector`, `bytevector-copy`, `bytevector-copy!`, `bytevector-append`
- **Vectors**: `vector-append`
- **I/O**: `flush-output-port`, `char-ready?`, `u8-ready?`, `read-u8`, `peek-u8`, `read-string`, `read-bytevector`, `read-bytevector!`, `write-u8`, `write-bytevector`, `write-simple`, `delete-file`, `call-with-input-file`, `call-with-output-file`, `with-input-from-file`, `with-output-to-file`, `file-exists?`
- **Process context**: `get-environment-variable`, `get-environment-variables`, `emergency-exit`
- **Time**: `current-second`, `current-jiffy`, `jiffies-per-second`
- **Error handling**: `error-object-message` alias (both names accepted), `read-error?`, `file-error?`; `scm_raise` now tags read/file errors correctly; `open-input-file` / `open-output-file` raise `file-error` instead of returning `#f`

**RPi test suite** (`tests/test_rpi.scm`): predicate and type-error tests always run; hardware sections (`gpio-open`, `i2c-open`, `spi-open`, `pwm-open`) skip gracefully when device nodes are absent — passes on CI and on Pi hardware alike.

---

### 0.8.2 — CAS Phase 7: assumptions + exotic limits

**Assumption flags on symbolic variables:**

`(sym-var 'x 'positive)` (and `'negative`, `'real`, `'integer`, `'nonzero`) stores a domain assumption in the variable's flag word. Assumptions unlock targeted simplification rules:

- **`(sym-var 'x 'positive)`** — `|x| → x`, `√(x²) → x`, `log(xⁿ) → n·log(x)`, `sign(x) → 1`
- **`(sym-var 'x 'negative)`** — `|x| → −x`, `sign(x) → −1`

```scheme
(define xp (sym-var 'x 'positive))
(abs xp)                          ; => xp
(simplify (sqrt (expt xp 2)))     ; => xp
(simplify (log (expt xp 3)))      ; => (* 3 (log x))
(sym-assumption? xp 'nonzero)     ; => #t  (implied by positive)
```

**`(sign x)`** — new sign function; evaluates numerically on constants and simplifies to `1`/`-1` with assumption flags. Output renders in both `sym->infix` and `sym->latex`.

**Exotic indeterminate limits:**

All four classical indeterminate forms now resolve:

```scheme
(symbolic x)
(limit (* x (log x)) x 0.0 'right)          ; => 0    (0·∞)
(limit (expt x x)       x 0.0 'right)       ; => 1    (0⁰)
(limit (expt x (/ 1 x)) x +inf.0)           ; => 1    (∞⁰)
(limit (expt (+ 1 (/ 1 x)) x) x +inf.0)    ; => e    (1^∞)
```

Algorithm: `0·∞` rewrites as a ratio and applies L'Hôpital; power forms rewrite `f^g` as `exp(g·log(f))`, take the limit of the exponent, then exponentiate. A new internal `sx_ratio_simplify` function cancels the L'Hôpital derivative quotient without interfering with the simplifier.

---

### 0.8.1 — CAS Phase 5: Taylor series

- **`(series f x a n)`** — truncated Taylor/Maclaurin series of `f` around point `a` to order `n`.  
  Computed by iterating `sx_diff` / `sx_substitute`; zero-coefficient terms are dropped.  
  Integer-valued flonum derivatives (e.g. `exp(0) = 1.0`) are coerced to fixnums before dividing by `k!`, so expansions around exact points yield **exact rational coefficients**: `1/2`, `1/6`, `1/24` …  
  Output is a plain symbolic ADD expression — composable with `simplify`, `substitute`, `∂`, `sym->infix`, `sym->latex`.

```scheme
(symbolic x)
(series (exp x) x 0 4)   ; (+ 1 x (* 1/2 x²) (* 1/6 x³) (* 1/24 x⁴))
(series (sin x) x 0 5)   ; (+ x (* -1/6 x³) (* 1/120 x⁵))
(sym->latex (series (cos x) x 0 4))
; 1 - \frac{1}{2} x^{2} + \frac{1}{24} x^{4}
```

---

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

### 0.7.9 — CAS Phase 4: limits, IBP integration, vector calculus; Raspberry Pi module

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
