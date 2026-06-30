# Changelog

### 1.6.3 — Fix GC crash when calling (gc) after (import (curry qt6))

Patch release fixing a `SIGSEGV` at address `0x8` in Boehm's `GC_mark_from`
that occurred whenever `(gc)` was called after importing the Qt6 module.

**Bug fix**
- **`gc_register_root` bad `GC_add_roots` for pointee header** (`src/gc.c`):
  A second `GC_add_roots` call registered the first 8 bytes of the
  pointed-to heap object as a Boehm root.  Those 8 bytes are the `Hdr
  {type, flags}` field — a small integer on little-endian ARM64 (e.g.
  `T_COMPLEX = 8`).  The check `cur & ~3u` also incorrectly passed for
  fixnums (fixnum 2 encodes as `9`, and `9 & ~3 = 8`), registering
  near-null addresses as root ranges.  When Boehm scanned those "roots" it
  read from address `0x8` and crashed.  The slot registration at line 286
  (`GC_add_roots(slot, slot+8)`) is sufficient on its own — removed the
  broken second call.
- **`JitClosure.fn` hidden from Boehm** (`src/llvm/jit.cpp`, `src/eval.c`,
  `src/vm.c`, `src/object.h`): the JIT function pointer is now stored as
  `GC_HIDE_POINTER(fn)` and revealed at all four call sites with
  `GC_REVEAL_POINTER`, preventing Boehm's conservative scanner from
  mistaking JIT code addresses for heap pointers.

---

### 1.6.2 — Generational GC + LLVM JIT correctness fix

Patch release fixing a use-after-free when running `--gc generational` with
the LLVM JIT backend (`--llvm` / `-DBUILD_LLVM=ON`).

**Bug fixes**
- **JIT statepoints inhibit minor GC** (`src/llvm/codegen.cpp`): every
  allocating call emitted as an `llvm.experimental.gc.statepoint` now
  brackets itself with `curry_gc_inhibit_minor_jit` / `curry_gc_resume_minor_jit`.
  Previously minor GC could fire mid-statepoint while live nursery pointers
  sat in JIT alloca slots invisible to the collector, producing stale
  references and potential use-after-free.
- **`ExnHandler` saves inhibit counter** (`src/eval.h`): the `SCM_PROTECT`
  macro now saves `gc_inhibit_count` at `setjmp` time and restores it on the
  `longjmp` path.  Without this, a Scheme exception propagating through a JIT
  frame would skip the resume call, permanently elevating the inhibit counter
  and preventing future minor collections.
- **C-linkage inhibit wrappers** (`src/gc.c`, `src/gc.h`):
  `gc_inhibit_minor_fn`, `gc_resume_minor_fn`, `gc_inhibit_save`, and
  `gc_inhibit_restore` are new plain-C functions exposing the
  `_Thread_local gc_inhibit_count` counter to C++ and JIT callers.
  The existing `gc_inhibit_minor()` / `gc_resume_minor()` inlines are
  guarded by `#ifndef __cplusplus` and were no-ops in C++ translation units.

---

### 1.6.1 — GC header C++ compatibility fix

Patch release fixing a build regression for users building the Qt6 module.

**Bug fixes**
- `src/gc.h`: wrap `#include <stdatomic.h>` and the `_Atomic` extern declarations
  in `#ifndef __cplusplus` so Qt6's C++ translation units no longer see conflicting
  `_Atomic` qualifiers from both `<stdatomic.h>` and `<atomic>` (20 compiler errors
  on Homebrew Qt6).
- MQTT retain test: switched to QoS 1 so the broker acknowledgement is awaited
  before the subscriber connects; previously QoS 0 fire-and-forget caused a race
  between publisher and subscriber.
- Pre-push hook: `|| true` guard prevents `set -euo pipefail` from killing the
  script when the formula SHA256 is a placeholder (grep finds no hex string);
  also extended check to catch any non-64-char value, not just the zero sentinel.

---

### 1.6.0 — GC performance instrumentation and real-time benchmarking

**GC statistics**
- New atomic counters in `src/gc.c`: `gc_stat_minor_count`, `gc_stat_major_count`,
  `gc_stat_minor_total_us`, `gc_stat_minor_max_us`.
- Pause ring buffer in `src/gc_gen.c`: last 256 minor GC pause times (µs) for
  computing p50/p95/p99 without sorting full history.
- Boehm major GC counted via `GC_set_on_collection_event` callback.
- New builtins `(gc-stats)` and `(gc-stats-reset!)` expose all counters and the
  pause ring as a Scheme alist.

**`(gc-stats)` alist**

```
((minor-count . N) (major-count . N) (minor-total-us . N) (minor-max-us . N)
 (pause-ring . #(u64 ...))   ; last 256 minor pause times in µs
 (heap-size-bytes . N) (free-bytes . N) (nursery-used . N))
```

**GC robustness (gc-perf work)**
- Lock-free `pinned_add`: atomic fast path; mutex only on resize.  Eliminates
  contention on the pinned slot list under concurrent minor GC + parallel map.
- Minor GC safepoint at VM `L_DISPATCH`: deferred minor GC fires between bytecode
  instructions rather than mid-instruction, preventing corruption of live C locals.
- Pinned slot nulling: after each scan the full `[0, pinned_count)` range is
  zeroed so Boehm can reclaim dead pinned objects between major collections.

**Real-time benchmarking stack**

`tools/bench-stack/` — one-command Docker Compose observability pipeline:
```
bench.scm → MQTT (mosquitto) → Telegraf → InfluxDB → Grafana (live dashboard)
```
Watch benchmark runs live at `http://localhost:3000` while `bench.scm` publishes
results.  Dashboard panels: mean time by benchmark, p99 over time, minor GC
count per run, max pause gauge, comparison table (boehm vs generational).

Three benchmark suites:
- `tests/bench.scm` — core throughput + GC-specific workloads (short-lived alloc,
  medium-lived promotion, large-object bypass, write-barrier mutation, mixed)
- `tests/bench_heavy.scm` — longer warm-up, actor ring, larger allocation budgets
- `tests/bench_torture.scm` — stress suite (1M alloc loop, deep recursion, etc.)

**New documentation**
- [`docs/reference/benchmarking.md`](docs/reference/benchmarking.md) — benchmark
  suite reference: build prereqs, quick start, all suites and panels, MQTT event
  schema, writing custom benchmarks
- [`docs/reference/profiling.md`](docs/reference/profiling.md) — profiler
  reference: `**eval-profiler**` levels, `(curry profiling)` API, `,profile` REPL
  command, timing workflows and limitations

---

### 1.5.0 — Generational GC

Two-generation Cheney garbage collector available as `--gc generational`.

**Architecture**
- Gen0 (nursery): shared 2 MB `mmap` region with mutex-protected bump pointer.
  All non-pinned objects are born here.
- Gen1 (tenured): 128 MB `mmap` region. Live nursery objects are promoted
  here via Cheney copy during minor collection.
- Minor collection: stop-the-world Cheney copy of the nursery into tenured
  space. Includes dirty-card scan (remembered set), tenured walk for
  `T_ENV`/`T_SET`/`T_HASHTABLE` (whose value arrays live in Boehm memory
  outside the card-table range), pinned-object scan, and ext-scanner pass.
- Major collection: full Cheney copy of both nursery and tenured space into
  a fresh tenured region, triggered when tenured exceeds 85% fill.
- Write barrier: `GC_WRITE_BARRIER(obj, field, val)` macro marks 512-byte
  cards dirty; under Boehm and semispace compiles to a single store.
  Instrumented at all mutable sites: `set-car!`, `set-cdr!`, `vector-set!`,
  `force`, `parameterize`, parameter-object calls, upvalue closing in the VM.
- Safepoints: polling (`gc_stop_world` flag). Threads yield at nursery
  exhaustion, actor receive/send!, and between work items in the parallel pool.
- Thread cooperation: `gc_gen_thread_park`/`unpark` let workers and the
  parallel-map dispatch thread opt out of STW while blocked on condvars.
- Parallel map/reduce: `par_scan_roots` ext-scanner covers `elems[]`,
  `results[]`, and per-chunk `WorkItem` val_t fields during dispatch.
- `WorkPool` and `WorkItem` pinned via Boehm so raw C pointers in the
  work-stealing deque stay valid across nursery resets.
- Symbolic CAS nodes (`SymVar`, `SymFn`, `SymExpr`) pinned; their val_t
  fields scanned during minor collection.
- Dynamic VM registry: no longer capped at 64; grows on demand, so programs
  spawning more than ~47 actors on a 16-core machine no longer silently drop
  VM roots (was a use-after-free).

**New CLI flags**
- `--gc generational` — select the two-generation backend
- `--gc-nursery-size N` — set nursery size (default `2M`; supports `K`/`M`/`G`)
- `--gc-tenured-size N` — set tenured region capacity (default `128M`)

**`(gc-stats)` generational alist**

Returns `((minor-collections . N) (major-collections . N) (nursery-bytes . N)
(nursery-used . N) (tenured-used . N) (tenured-capacity . N) (pinned-count . N))`.

**Known limitation**

The tree-walking eval/apply interpreter keeps intermediate `val_t` values as C
locals not tracked by any GC root. A minor GC firing during a deep numeric
computation corrupts those locals. Workaround: `--gc-nursery-size 16M`. A
precise or stack-mapping approach is the planned fix.

**SICM**: 167/167 pass under Boehm (default); 93/167 under `--gc generational`
with the default 2 MB nursery, 93/167 with `--gc-nursery-size 16M` (the C-stack
limitation affects the remaining 74 tests regardless of nursery size).

---

### 1.4.1 — Mandelbrot polish + Tetrabrot explorer

**New example: `examples/tetrabrot.scm`** — Bicomplex (ℂ²) Tetrabrot explorer

The bicomplex number system ℂ(i₁,i₂) extends ℂ to four real dimensions by
adding a second imaginary unit i₂ that commutes with i₁.  The Mandelbrot
iteration w → w² + c in this algebra lives in ℝ⁴ and is called the Tetrabrot.

The explorer lets you navigate any 2D cross-section of the four-dimensional
parameter space interactively:

- **Axis-pair dropdown** — six choices (x₀×x₁ … x₂×x₃); the (x₀,x₁) slice
  is the ordinary Mandelbrot set
- **Fixed-axis sliders** — sweep the two non-displayed ℝ⁴ components through
  the range −2 … 2 to move the slice through the solid
- Full pan, zoom-to-cursor, palette, iteration count, bookmarks, and PNG export
  (same feature set as the Mandelbrot explorer)
- GPU-accelerated bicomplex squaring shader — runs at full resolution even at
  high iteration counts

**Qt6 module — three new dropdown procedures**

`dropdown-add-item! dd label` — append a new item to an existing dropdown
without rebuilding it.  Used by the bookmark system to grow the list as the
user saves views.

`dropdown-clear! dd` — remove all items.

`dropdown-count dd` — return the number of items currently in the dropdown.

**Qt6 module — `canvas-save-png!` bug fixes**

Two HiDPI rendering bugs fixed in `canvas-save-png!` on Retina/HiDPI displays
(device pixel ratio ≥ 2):

- *Quarter-image bug* — the off-screen FBO was rendered into only the
  upper-right quadrant of the saved PNG.  Root cause: `gl-shader-draw!`
  computed the physical pixel size as `device->width() × dpr`, which is correct
  for a `QWidget` (logical width) but double-scales for `QOpenGLPaintDevice`
  (already physical width).  Fix: read the viewport Qt set in
  `beginNativePainting()` via `glGetIntegerv(GL_VIEWPORT)` instead.

- *Blurry HUD text* — QPainter-rendered text (coordinates, zoom level) was
  blurry in the saved PNG because the glyph cache rasterised at 1× density.
  Root cause: `setDevicePixelRatio(dpr)` moves the DPR scale into the
  projection matrix, leaving `state->matrix` as identity, so
  `pixelToDeviceTransformDensity()` returns 1 and glyphs are upscaled 2×.
  Fix: use `setWindow(0, 0, lw, lh)` / `setViewport(0, 0, pw, ph)` instead,
  which puts the DPR scale into `state->matrix` directly.  `gl-shader-draw!`
  and `gl-shader-draw-arrays!` now derive `u_dpr` from the larger of the
  device DPR and the painter world-transform scale so both the on-screen
  (`QWidget`) and off-screen (`QOpenGLPaintDevice`) paths give the same result.

**Bug fixes**

- `examples/mandelbrot.scm`: `cadddr` replaced with `list-ref` (Curry does not
  export `cadddr`)

---

### 1.4.0 — Extensible CAS

**User-extensible rewrite engine (Phase 4a)**
- `define-rule` / `define-ruleset` / `apply-rules` — pattern-matching rewrite rules with optional guards
- `list-rules`, `clear-rules` — inspect and reset rule sets

**Algebra declarations and assumptions (Phase 4b)**
- `define-algebra` — declare operator commutativity, associativity, identity, inverse
- `with-assumptions` — temporarily attach domain flags (`positive`, `real`, `integer`, `nonzero`, `quaternion`) to sym-vars
- `assume!` / `retract!` / `can-assume?` — permanent mutable assumptions

**Polynomial machinery (Phase 4c)**
- `poly-gcd`, `poly-resultant`, `poly-pseudo-remainder`, `poly-factor` (Yun squarefree + Kronecker)
- `poly-roots` (companion-matrix eigenvalues for degree ≤ 8)

**Equation solving (Phase 4d)**
- `solve` — univariate polynomial and simple transcendental equations
- `solve-system` — linear systems via Gaussian elimination

**Risch integration (Phase 4e)**
- Rational function integration (partial fractions over ℚ)
- Log-polynomial extension: integrates `f(x)·log(g(x))` forms

**Special functions (Phase 4f)**
- Orthogonal polynomials: `legendre`, `assoc-legendre`, `hermite`, `hermite-prob`, `chebyshev-t`, `chebyshev-u`, `laguerre`, `assoc-laguerre`
- `spherical-harmonic` — Y_l^m(θ,φ)
- Gamma family: `gamma` (exact integers/half-integers, symbolic), `log-gamma`, `digamma`, `beta`
- Bessel: `bessel-j`, `bessel-y`, `bessel-i`, `bessel-k` (Maclaurin series for small x)
- Elliptic integrals: `elliptic-k`, `elliptic-e`, `elliptic-f`, `elliptic-pi`

**Extended series (Phase 4g)**
- `laurent` — Laurent series around poles; pole order detected via guarded substitution
- `puiseux` — Puiseux (fractional-power) series via t-substitution

**Bug fixes**
- `sx_simplify`: exact `n/0` now raises; exact `0/0` returns unevaluated node; float `a/0.0` returns IEEE ±∞
- `sx_series`: guarded against unhandled exceptions when coefficient evaluation encounters poles
- `bessel-k`: replaced asymptotic-only seeds (up to 20% error at x=1) with Maclaurin series for x≤8

### 1.3.1 — Housekeeping

**Packaging**
- RPM CPack generator added alongside DEB; `cpack` now produces both
  `curry-scheme_*.deb` and `curry-scheme-*.rpm` from the same build.
- CMake project and CPack version now derived from `src/version.h` (single
  source of truth).

**CLI**
- `--gc BACKEND` was missing from `--help`; now listed alongside `--gc-max-heap`.

**Bug fixes**
- Mandelbrot explorer: left-drag pan and zoom-to-cursor y-axis were inverted;
  drag event name typo caused pan to not register.

**Housekeeping**
- `scripts/` directory merged into `tools/`; all references updated.
- LLVM "Enabled" banner text cleaned up.
- `docs/guides/INSTALL.md` documents both `.deb` and `.rpm` packaging with
  dependency tables for each format.

---

### 1.3.0 — Cheney Semispace GC

First moving garbage collector.  The Boehm conservative GC remains the default;
pass `--gc semispace` at startup to activate the Cheney backend.

**New GC backend: `--gc semispace`**

- Two 32 MB semispaces with bump-pointer allocation.  On exhaustion, a
  stop-the-world Cheney copy evacuates all live objects to to-space and swaps
  the spaces.
- Precise root set: VM value stack, `GLOBAL_ENV`, per-object pinned list
  (Actor/TVar/Channel/Mailbox/Continuation), module-registry external scanner.
- Type-specific scanners for all 54 `ObjType` values including raw C pointer
  fields (`Closure::env`, `BcClosure::chunk/upvals`, `EnvFrame::parent`,
  `Module::env`, `Record::rtd`, `Upvalue::next`).
- **Pinned types** (allocated in Boehm, never moved): `Symbol`, `Bignum`,
  `Rational`, `Mpfr`, `Port`, `Actor`, `Mailbox`, `TVar`, `Channel`,
  `Continuation`, `Primitive`.

**New Scheme procedures**

- **`(gc-collect!)`** — trigger an immediate collection cycle.
- **`(gc-stats)`** — return a GC statistics alist: `collections`,
  `bytes-allocated`, `bytes-survived`, `from-used`, `space-size`,
  `pinned-count` (semispace); `heap-size`, `free-bytes` (Boehm).
- **`(gc-on-collection proc)`** — register a zero-argument post-collection hook.

**C API additions**

- `gc_alloc_pinned` / `gc_alloc_raw_pinned` vtable entries and corresponding
  `CURRY_NEW_PINNED` macros — allocate typed or untyped objects in Boehm,
  bypassing the semispace nursery.
- `gc_ss_register_ext_scanner` — register an external root-scanner callback
  for C modules that hold `val_t` in non-GC-managed structs.
- `T_CHUNK` (53) and `T_UPVALUE` (54) added to `ObjType`; `Hdr` header
  prepended to `Chunk` and `Upvalue` structs.

**Documentation**

- `docs/reference/gc.md` — full GC reference: backends, Scheme API, C API,
  performance notes.

**Test results**

All 27 ctest suites pass.  Core language suites (r7rs, r6rs, numeric_ext,
actors, dynamic_wind, syntax_rules, akkadian, sexagesimal) all pass under
`--gc semispace`.  SICM ODE-integration tests: 93/167 under semispace — a
known stale-pointer residual deferred to Phase 6 (generational GC).

---

### 1.2.6 — Security patch

- **Fix stack buffer overflow in `number->string` with `'neugebauer`/`'cuneiform`**:
  `SexDigs.int_digs` and `SexDigs.frac_digs` were declared with 32 entries but
  `mpz_to_base60_digits` could write up to 64, corrupting the stack for integers
  ≥ 60³² (~2.68×10⁵⁷). Arrays expanded to 64 entries. (Reported by internal
  security review of v1.2.5.)

### 1.2.5 — Babylonian/Sexagesimal Number System

**Sexagesimal (base-60) I/O** — first-class Babylonian arithmetic, faithful to
Otto Neugebauer's 1935 transcription conventions.

- **`#s` reader prefix** — Neugebauer literal notation in source code.
  Commas separate integer sexagesimal places; semicolons mark the radix point.
  `#s1;30` → exact `3/2`, `#s1,0,0` → `3600`, `#s1;24,51,10` → `30547/21600`
  (the YBC 7289 approximation of √2, accurate to six decimal digits, c. 1800 BCE).
  The semicolon is **not** treated as a delimiter inside `#s` literals.

- **Cuneiform Unicode reader** — Babylonian cuneiform numerals are valid tokens:
  - 𒁹 (U+12079 ASH) = 1–9 repeated strokes
  - 𒌋 (U+1230B U) = tens
  - 𒑊 (U+1244A) = zero placeholder (SHAR2 tenu)
  - Adjacent glyphs within a sexagesimal group are written contiguously;
    groups are separated by spaces. `𒁹 𒌋𒁹` → `71`.

- **`(number->string n 'neugebauer)`** — format any exact or inexact number
  in Neugebauer notation. `(number->string 3600 'neugebauer)` → `"1,0,0"`.
  Optional `#:places k` keyword argument limits fractional sexagesimal digits
  for flonums: `(number->string (sqrt 2) 'neugebauer #:places 3)` → `"1;24,51,10"`.

- **`(number->string n 'cuneiform)`** — format as cuneiform glyph string.

- **`(string->number s 'neugebauer)`** / **`(string->number s 'cuneiform)`** —
  parse Neugebauer or cuneiform strings to exact Scheme numbers.

- **`(current-number-notation)`** / **`(current-number-notation sym)`** —
  global notation parameter (default `#f` = decimal). Setting to `'neugebauer`
  or `'cuneiform` makes `display` and `write` emit numbers in that notation.

- **`(curry sexagesimal)` module** — pure Scheme convenience library:
  `rational->sexagesimal`, `sexagesimal->rational`, `hms->seconds`,
  `seconds->hms`, `dms->degrees`, `degrees->dms`, `cuneiform->neugebauer`,
  `neugebauer->cuneiform`, `sex:ybc7289`.

- **`#:keyword` symbols are now self-evaluating** — fixed in both the
  tree-walking evaluator (`eval.c`) and the bytecode compiler (`compiler.c`).
  `#:foo` no longer triggers "unbound variable".

- **Full UTF-8 lookahead for file ports** — `port_peek_char` now returns a
  complete Unicode codepoint (not just the first byte) enabling multi-byte
  cuneiform glyph dispatch in the reader.

- **Test suite** — 76 assertions in `tests/sexagesimal_tests.scm` covering
  reader literals, cuneiform parsing, `number->string`, `string->number`,
  round-trips, `current-number-notation`, and the `(curry sexagesimal)` module.

### 1.2.2 — Qt6 GPU extensions; 4D cave explorer; release tooling

**Qt6 module — 13 new bindings**

- **Key-up events** (`window-on-key-up!`, `canvas-on-key!`, `canvas-on-key-up!`):
  fire on physical key release only; Qt auto-repeat fake releases are filtered.
  Enables held-key state tables for smooth WASD movement.
- **Mouse capture** (`canvas-grab-mouse!`, `canvas-release-mouse!`,
  `canvas-on-grab-move!`, `canvas-mouse-grabbed?`): cursor-lock + relative-delta
  mode for first-person camera. Cursor hidden and warped back to canvas centre
  after each event; `canvas-on-mouse!` move events suppressed while grabbed.
- **Mouse move semantics**: move events now emit `'drag` when a button is held,
  `'move` on hover — previously always `'move`.
- **Resize callback** (`canvas-on-resize!`): `(lambda (w h) …)` fires from
  `resizeEvent`; use to resize FBOs or rebuild projection matrices.
- **Timer with delta-t** (`make-timer/dt`, `timer/dt-start!`, `timer/dt-stop!`):
  callback receives elapsed ms since last tick via `QElapsedTimer` for
  frame-rate-independent physics.
- **Clipboard** (`clipboard-text`, `clipboard-set-text!`).
- **GPU vertex buffers** (`make-gl-buffer`, `gl-buffer-update!`,
  `gl-shader-draw-arrays!`): upload `f64vector` or Scheme vector to GPU; draw
  with per-vertex attribute VBOs. Primitives: `'points`, `'lines`,
  `'line-strip`, `'triangles`, `'triangle-strip`, `'triangle-fan`. 16-element
  Scheme vector maps to `mat4` uniform.
- **Framebuffer objects** (`make-gl-framebuffer`, `gl-framebuffer-texture`,
  `gl-framebuffer-resize!`, `gl-shader-to!`): offscreen render-to-texture for
  post-processing passes; `gl-shader-to!` restores Qt's default FBO after drawing.

**naturalmaze — `examples/naturalmaze/`**

Multi-file 4D cave explorer (`make run` from the directory). Each `.scm` can
be independently compiled to bytecode with `make`.

- 16×16×4 maze: DFS spanning tree + 28 % extra passages for open-world feel,
  2×2 clearing rooms, ~22 % W-passage density between dimension layers.
- GLSL 330 DDA raycaster with pitch (look up/down), procedural cave textures:
  rock fbm, moss by wall height, hanging vines, per-cell fungi spots with
  per-biome glow colour and sin-pulse animation.
- Floor: earthy dirt, scattered mushrooms (5 hash candidates per cell), puddle
  reflections, W-rift mandala runes (pulsing ring + 6-spoke pattern) on cells
  with 4th-dimension passages.
- Ceiling: stalactite noise, slowly breathing bioluminescent patches.
- Four biomes by W-slice: amber / cyan / violet / crimson fungi glow.
- Dimensional ripple overlay during W-transitions (chromatic tint toward
  destination biome proportional to visual-W fractional part).
- Mouse-grab look, smooth WASD with wall-sliding collision, Q/E step through
  the 4th dimension, scroll-wheel W-step, minimap HUD with W-rift indicators.

**Release tooling**

- `.claude/commands/release.md`: `/release <version>` slash command — 5-checkpoint
  end-to-end pipeline (preflight → bump commit → tag + SHA256 → formula → brew verify).
- `.claude/hooks/pre-push` (symlinked to `.git/hooks/pre-push`): blocks push if
  `src/version.h`, Formula URL tag, Formula `version` field, or most recent git
  tag disagree, or if the sha256 is still the zero placeholder.
- `tools/release-verify.sh`: post-release brew verification (uninstall, tap
  update, reinstall, `curry --version` assert, smoke test).

**Bug fixes**

- Formula/curry.rb `version` field was `"1.2.0"` while the URL and
  `src/version.h` both said `1.2.1`; corrected. The pre-push hook now catches
  this class of drift automatically.
- Curry VM: 3-argument `(< a b c)` inside a top-level recursive function called
  repeatedly from a named-let loop silently receives a non-numeric value after
  enough iterations (`to_mpz` error). Worked around in `naturalmaze/world.scm`
  by splitting into two 2-argument comparisons; VM bug documented for future fix.
- `(load "file.scc")` parses bytecode as Scheme source text and errors on the
  magic header; `naturalmaze/main.scm` updated to always load `.scm` source.

---

### 1.2.1 — Pollard rho bug fixes; expanded test coverage

**Bug fixes**

- **Pollard rho infinite loop** (`src/numtheory.c`): two bugs caused `factor`
  to loop forever on semiprimes whose prime factors exceed the trial-division
  limit (100 000): (1) `factor_into` modifies its `mpz_t n` argument (reduces
  to 1 when done), so the recursive call `factor_into(f, ...)` was destroying
  `f` before `mpz_divexact(n, n, f)` could use it — dividing by 1 instead of
  the actual factor; fixed by saving a copy before recursing. (2) The batch
  product accumulator `q` was never initialised to 1 — `mpz_inits` zeroes
  everything, so every batch GCD returned n; fixed by explicit `mpz_set_ui(q,
  1)` at init and on each batch boundary.

**Tests**

- `tests/numtheory_tests.scm` (111 → 120 assertions): large-semiprime factoring
  (`(* 100003 100019)`, `(* 999983 999979)`), `factor` of 2²⁰, flonum
  `continued-fraction` input, three additional `best-rational-approx` cases.
- `tests/mpfr_tests.scm` (34 → 65 assertions): standard arithmetic dispatch to
  MPFR, `inexact` promotion inside `with-precision`, `number->string` on MPFR,
  binary radix fix (`(number->string 255 2)` → `"11111111"`),
  `floor`/`ceiling`/`truncate` MPFR dispatch, `mpfr-erfc`/`mpfr-hypot`/
  `mpfr-fma`, precision propagation in binary ops.

---

### 1.2.0 — arbitrary-precision floats (MPFR) and number theory

**New features**

- **MPFR arbitrary-precision floats** (`-DBUILD_MPFR=ON`, `src/mpfr_num.c`).
  Adds `T_MPFR` to the numeric tower with `(mpfr x [prec])`, constants
  `(mpfr-pi)`, `(mpfr-e)`, `(mpfr-phi)`, `(mpfr-log2)`, `(mpfr-euler)`,
  `(mpfr-catalan)`, `(mpfr-apery)`, and MPFR-specific transcendentals
  (`mpfr-gamma`, `mpfr-zeta`, `mpfr-erf`, `mpfr-erfc`, `mpfr-j0`, `mpfr-j1`,
  `mpfr-hypot`, `mpfr-fma`, …).  Dynamic precision via `(with-precision N body
  ...)` / `(call-with-precision N thunk)` / `(current-precision)`.  Selectable
  rounding mode via `(mpfr-rounding-mode 'rndn|'rndz|'rndu|'rndd|'rnda)`.
  Standard tower transcendentals (`sin`, `cos`, `exp`, `log`, `sqrt`, …)
  dispatch to MPFR when given an MPFR operand.

- **Interval arithmetic** (with MPFR): `make-interval`, `interval`,
  `interval-lo`, `interval-hi`, `interval-midpoint`, `interval-width`,
  `interval-contains?`.  Endpoints use directed-rounded MPFR for certified
  bounds.

- **Number theory library** (`src/numtheory.c`, always built — GMP only).
  Primality and factoring (`prime?`, `next-prime`, `factor`, `prime-factors`),
  arithmetic functions (`totient`, `carmichael`, `mobius`, `divisors`,
  `divisor-count`, `divisor-sum`, `perfect?`/`abundant?`/`deficient?`, `omega`,
  `big-omega`), modular arithmetic (`mod-expt`, `mod-inverse`, `jacobi-symbol`,
  `kronecker-symbol`, `legendre-symbol`, `extended-gcd`, `chinese-remainder`),
  combinatorial sequences (`fibonacci`, `lucas`, `binomial`, `multinomial`,
  `catalan`, `bernoulli`, `euler-number`, `stirling1`, `stirling2`, `bell`,
  `partition-count`), continued fractions (`continued-fraction`, `convergents`,
  `best-rational-approx`), and number predicates (`squarefree?`,
  `perfect-power?`, `smooth?`).

**Bug fixes**

- `number->string` for radix 2 (and any radix other than 8, 10, 16) on
  fixnum inputs incorrectly emitted decimal digits using `%ld`; now routes
  through `mpz_get_str` so every radix is honoured.

See `docs/reference/numeric-precision.md`.

---

### 1.1.1 — SRFI compatibility layer (surfage)

**New features**

- **`(surfage s1 lists)`** — SRFI-1 list library: `iota`, `any`, `every`, `remove`, `delete`, `fold`, `append-map`, `filter-map`, `flat-map`, `take`, `drop`, `take-while`, `drop-while`, `count`, `partition`, `last-pair`, `first`–`fifth`, plus re-exports of all list procedures already in the global environment.
- **`(surfage s27 random-bits)`** — SRFI-27 random-number source API: re-exports Curry's built-in xoshiro256+ implementation (`default-random-source`, `make-random-source`, `random-source-randomize!`, `random-source-pseudo-randomize!`, `random-source->random-integer`, `random-source->random-real`, `random-integer`, `random-real`) under the portable `(surfage s27 random-bits)` name.

Code using `(import (surfage s1 lists))` or `(import (surfage s27 random-bits))` is now portable across Curry, Guile, Chicken, Chibi-Scheme, and other implementations that follow the surfage naming convention.

---

### 1.1.0 — CL condition system · C FFI · STM · channels · spinors · tensor ops

**New features**

- **CL-style condition system** (`src/condition.c`, `(import (curry conditions))`): Common
  Lisp-style non-unwinding error handling. `handler-bind` installs handlers that run with
  the full call stack intact; `with-restarts` establishes named recovery points;
  `invoke-restart` jumps to a recovery option without unwinding; `handler-case` is the
  unwinding fallback. A condition type hierarchy (BFS over parent lists) lets handlers match
  by type family. `signal` returns normally if no handler claims the condition; `condition-error`
  signals then raises. `ignore-errors` returns `(values result-or-#f condition-or-#f)`.
  Built-in types: `error`, `warning`, `math-error`, `singular-matrix`, `no-elementary-form`,
  `gc-pressure`, and others. See `docs/reference/language.md` and `examples/conditions_demo.scm`.

- **General C FFI** (`-DBUILD_FFI=ON`, `src/ffi.c`, `(import (curry ffi))`): Call any C
  function from Scheme via libffi. `define-foreign-library` loads a shared library;
  `define-foreign` declares a typed function binding; `with-pinned-matrix` / `with-pinned-tensor`
  pass matrix/tensor data pointers to C with zero copying (no marshal overhead for BLAS etc.).
  Type system handles `int`, `uint`, `long`, `size_t` (and Scheme-style `size-t`), `double`,
  `float`, `c-ptr`, `string`, `bool`, and `void`. Requires `libffi-dev` on Linux,
  automatically found via Homebrew on macOS.

- **STM + CSP channels** (ported from cill): TL2 software transactional memory
  (`atomically`, `make-tvar`, `tvar-read`, `tvar-write!`, `retry`, `or-else`, `select`);
  CSP buffered channels with synchronous rendezvous (`make-channel`, `channel-send!`,
  `channel-recv!`, `channel-close!`, non-blocking `%channel-try-*`). All primitives in
  global env; `(curry stm)` adds `or-else` and `select` macros. See
  `docs/reference/concurrency.md`.

- **Spinor type** (`T_SPINOR=45`, ported from cill): Weyl (left/right-handed), Dirac, and
  Majorana spinors with correct SL(2,C)/Lorentz transform law. `make-spinor`, `spinor-ref/set!`,
  `spinor+/-`, `spinor-scale`, `spinor-transform`, `spinor-conjugate`, `spinor-adjoint`
  (Dirac γ⁰), `spinor-inner` (Hermitian), `spinor->list`.

- **Tensor index-structure operations** (ported from cill): `tensor-transpose` (axis
  permutation), `tensor-contract` (generalised trace over two axes), `tensor-einsum`
  (Einstein summation notation — `"ij,jk->ik"` for matrix multiply, up to 8 tensors).

**Build changes**

- `BUILD_FFI=ON` CMake option; requires `libffi-dev` / Homebrew libffi.
- Banner and `-v` output now show `(LLVM Enabled)` and `(FFI)` capability tags.
- `src/curry_ffi.h` replaces `src/ffi.h` (renamed to avoid collision with `<ffi.h>`).

---

### 1.0.1 — macOS arm64 LLVM release-build fix; CMake cleanup

**Bug fixes**

- **macOS arm64 TLS ABI mismatch** (Release build with `BUILD_LLVM=ON`): C++ translation units (`jit.cpp`) referenced `extern thread_local gc_nursery` and `g_jit_call_depth`, causing the linker to look for C++ TLS wrapper symbols (`_ZTW10gc_nursery`, `_ZTW16g_jit_call_depth`) that the C compiler never emits — only `$tlv$init`-style symbols. Fixed by routing C++ callers through plain `extern "C"` functions: `gc_alloc_impl()` in `gc.c` and `jit_depth_push()`/`jit_depth_pop()` in `eval.c`. TLS variables and their inline accessors are now hidden from C++ via `#ifndef __cplusplus`. Linux is unaffected (ELF TLS is C/C++ ABI-compatible).

- **Deprecated CMake SQLite target**: `SQLite::SQLite3` → `SQLite3::SQLite3` (new canonical name in CMake's FindSQLite3 module).

- **CMake syntax warning**: missing whitespace between the crypto option description string and `ON` in `CMakeLists.txt`.

---

### 1.0.0 — LLVM ORC v2 JIT backend; work-stealing thread pool; Qt6 confirmed

**New features**

- **LLVM ORC v2 JIT backend** (`-DBUILD_LLVM=ON`): hot `BcClosure` objects are automatically compiled to native ARM64 / x86-64 after 50 calls. The compiled version replaces the bytecode interpreter transparently. `(jit-compile! proc)` force-compiles immediately; `(jit-compiled? proc)` and `(curry-llvm-available?)` let Scheme code detect and drive JIT compilation. Typical speedups: ~6× for recursive functions, ~14× for tight named-let loops.

- **Persistent work-stealing thread pool** (Chase-Lev deques): `map` and `reduce` now dispatch through a pool of `hw_concurrency` worker threads that are created once at startup and parked on a condvar between jobs. Eliminates the ~1 ms per-call thread-spawn overhead of the previous approach. `for-each/par` is an explicit opt-in for parallel side-effects; `for-each` remains always sequential.

- **Qt6 6.x confirmed** alongside LLVM JIT in the same binary (`build-release`). The build script (`/Users/yvain/src/b`) now passes both `$(brew --prefix llvm)` and `$(brew --prefix qt@6)` in `CMAKE_PREFIX_PATH`.

**Bug fixes**

- **Nested parallel-map deadlock**: worker threads calling `map` on lists longer than the parallel threshold would submit nested work items and then block on the pool condvar — with no remaining workers to service those items. Fixed by adding a `_Thread_local bool pool_is_worker` flag; `prim_map`, `prim_reduce`, and `prim_for_each_par` fall back to sequential execution when called from inside a worker thread.

- **Park-broadcast race condition** in `pool_submit`: `n_parked` was read without holding `park_mutex`, so a worker could increment the counter and call `cond_wait` between the load and the broadcast, missing the wake-up permanently. Fixed by acquiring `park_mutex` before checking `n_parked`.

- **`jit-compile!` upvalue crash**: force-compiling a closure with captured upvalues via `jit-compile!` produced "unbound variable" errors because `prim_jit_compile` did not call `jit_wrap_upvals`, unlike the auto-JIT path in `maybe_jit_bcc`. Fixed; `jit_wrap_upvals` is now non-static so `builtins_curry.c` can reach it.

- **Stale bytecode cache** after binary rebuilds: `SCC_FMT_VER` bumped from `0x01` to `0x02` to force recompilation of all cached `.scc` files when the binary changes.

- **PLplot batch-mode hang**: `plinit()` in the plplot module defaulted to `plspause(1)`, causing every `plot-end` call to wait indefinitely for user input even with the `svg` headless device. Fixed with `plspause(0)` before each `plinit()`.

- **Qt6 font warnings on macOS**: test harness used generic font names (`"Sans"`, `"Monospace"`) that do not exist on macOS. Replaced with `"Helvetica"` and `"Menlo"`.

---

### 0.8.18 — Release-build call/cc and MCP SSE fixes

**Bug fixes**

- **`call/cc` — clang ARM64 dead-store elimination**: `cont->result = value` before `longjmp()` was silently eliminated by clang (ARM64 `-O2`) because `longjmp` is declared `[[noreturn]]` and the store appeared dead. Added `__asm__ volatile("" ::: "memory")` barriers in both `eval()`'s TCO loop and `apply()` before each `longjmp` on continuation invocation. Both tree-walker (`eval_call_cc`) and bytecode VM (`prim_call_cc`) paths also now use `*(volatile val_t *)&cont->result` to force a memory reload after `longjmp`, since clang constant-folded the non-volatile read to `V_VOID` at setjmp time. These issues only manifested in release builds; debug builds (`-O0`) were unaffected.

- **`call/cc` — tree-walker optimizer frame instability**: `eval_call_cc()` was split from `eval()`'s giant goto-loop into a dedicated `__attribute__((noinline))` helper so the setjmp frame is stable and all variables accessed in the longjmp path land in callee-saved registers.

- **`guard` / R7RS exception tests**: `guard`'s expansion via `call/cc` depended on working continuation capture; the fixes above unblock `guard`, `with-exception-handler`, `raise`, `raise-continuable`, and `error-object?` tests in `r7rs_tests.scm`.

- **MCP SSE keepalive — Boehm GC signal interruption**: `sleep(15)` in `handle_sse_get`'s keepalive loop was cut short by Boehm GC's stop-the-world signal (EINTR), causing a premature keepalive to be sent — which the SSE isolation test detected as session-1 data leaking to session-2. Replaced with `nanosleep()` + EINTR retry so the full 15-second interval is always observed.

---

### 0.8.17 — Clang/Linux build fixes

**Build fixes**

- Release builds with upstream LLVM Clang on Linux now pass `-fno-omit-frame-pointer`. Clang at `-O2` omits frame pointers for register pressure, which prevents Boehm GC's conservative stack scanner from walking frames correctly and causes use-after-free segfaults. Apple Clang is unaffected (it always preserves frame pointers for Instruments compatibility).
- `prim_call_cc` (`call/cc`): the local `ret` variable is now `volatile` and the function is marked `__attribute__((noinline))`. Without these, Clang's optimizer cached `ret` in a caller-save register across the `setjmp`/`longjmp` boundary, returning garbage on continuation invocation. Again, Apple Clang's AArch64 calling convention masked this on macOS.

---

### 0.8.16 — Phase 11: multi-DOF Lagrangian + Hamiltonian mechanics

**Core C fixes**

- `dot-product` now accepts up/down tuples (previously only handled linked lists and silently returned 0 for tuples).
- `(partial i)`: tuple-valued slots in the local tuple (coordinate, velocity) are now replaced with nested sym-var tuples so that Lagrangians can call `dot-product`/`ref` on them symbolically. For an up-tuple slot, returns a down-tuple of partial derivatives (the covariant gradient).
- `sx_simplify` SX_DIV: common numeric coefficients in `(c1·A)/(c2·B)` are now cancelled — e.g. `(2·px)/(2·m) → px/m`. This fixes Hamiltonian output and Hamilton equation simplification.
- `num_mul`/`num_add`/`num_sub` and `sx_neg`/`sx_add`/`sx_sub`/`sx_mul`: tuple check now fires before symbolic check, enabling scalar×tuple distribution throughout the CAS layer.

**`(curry sicm)` additions**

- `L-free-particle-nd`, `L-harmonic-nd`: n-DOF isotropic Lagrangians using `dot-product`.
- `L-central-rectangular`: central force in 2D Cartesian coordinates.
- `L-Kepler-polar`: Kepler problem in polar coordinates `(up r θ)`.
- `momentum`: selector for slot 2 of a Hamiltonian state `(up t q p)`.
- `Lagrangian->Hamiltonian`: Legendre transform for diagonal mass matrices. Computes mass coefficients from `∂(∂L/∂qdot)/∂qdot`, then builds `H = Σ pᵢ²/(2mᵢ) + V(q)`.
- `Hamilton-equations`: returns `(up dH/dp, −dH/dq)` at a Hamiltonian state.
- `make-Hamiltonian`: direct `T*(p) + V(q)` Hamiltonian constructor.
- `Poisson-bracket`: `{f,g} = Σ (∂f/∂qᵢ · ∂g/∂pᵢ − ∂f/∂pᵢ · ∂g/∂qᵢ)`.
- `commutator`: `[A,B]f = A(Bf) − B(Af)`.

**Tests:** `sicm_tests.scm` expanded from 35 to 55 assertions (added §§16–21: 2D harmonic oscillator EOM, Kepler EOM, 1D/2D Hamiltonians, Hamilton equations, Poisson bracket identities including `{q,p}=1`).

**Internal simplifications**

- `numeric.c`: `tuple_binop` / `tuple_unop` helpers replace copy-pasted element-wise loops in `num_add`, `num_sub`, `num_neg`. `UNPACK_QUAT` macro replaces a 4-line quaternion-or-scalar extraction at five sites.
- `symbolic.c`: 18 transcendental one-liners (`sx_sqrt` … `sx_csc`) replaced by `SX_UNARY` / `SX_UNARY_NUM` macro table — adding a new transcendental now takes one line.

---

### 0.8.15 — SICM module fixes and test coverage

**`(curry sicm)` bug fixes**

- `Lagrangian->V` was listed in the module header as a supported procedure
  but was never implemented. It now correctly extracts the potential energy
  from a Lagrangian by evaluating L at zero velocity (`V = −L(t,q,0)`),
  which works because kinetic energy vanishes when nothing is moving.
- `Lagrangian->T` previously required the potential function V to be passed
  explicitly as a second argument. It now derives V from the Lagrangian
  itself (`T = L + V`) and takes only L.
- `literal-function*` used `iota`, which is not implemented in Curry.
  Replaced with an explicit loop.

**Test coverage**

- `tests/sicm_tests.scm` expanded from 21 to 35 assertions, covering the
  previously untested procedures: `literal-function*`, `Lagrangian->V`,
  `Lagrangian->T`, `make-Lagrangian`, `square` on `down` tuples,
  `Euler-Lagrange-operator` with free particle and gravity Lagrangians, and
  numeric `Lagrange-equations` verified against an exact analytic solution.

**Refactoring**

- `src/scc.c`: extracted shared `load_chunks_from_file` helper from the
  near-duplicate `read_scc` and `scc_load_direct` functions.

**Documentation**

- `docs/pkg-design.md` — design evaluation and recommendation for the
  `curry pkg` package manager (registry model, lock files, environments,
  C extension handling, versioning, package identity, manifest format,
  security).

---

### 0.8.14 — Akkadian/CLI test suites; SCC cache GC bug fix

**Test suites**

- `tests/akkadian_tests.scm` — 205 assertions covering every entry in
  `akkadian_names.h` in both transliterated Akkadian (e.g. `šakānum`) and
  cuneiform (e.g. `𒁹`) forms, for all AKK_SF special-form synonyms and
  AKK_PR procedure aliases.
- `tests/test_cli.sh` — 30 shell-level assertions for CLI features:
  shebang handling in `.scm` files, `-c` compile-to-`.scc`, `-c -o`
  custom output path, `-c -x` executable flag (shebang prepend + chmod),
  combined getopt flags (`-xc FILE`), magic-byte detection for
  extension-less `.scc` files, `-l` load-before-eval, script argument
  passing via `command-line-args`.
- Both suites are registered in `tests/CMakeLists.txt` and run via
  `ctest`.

**SCC cache GC bug fix**

- `read_scc` and `scc_load_direct` allocated the `Chunk**` pointer array
  with plain `malloc`.  Boehm GC does not scan non-GC heap memory for
  interior pointers, so the `Chunk` objects could be collected while the
  run loop was still executing them.  This manifested as a non-deterministic
  `unbound variable: <garbled>` error on the second run of any script that
  triggered a GC collection mid-loop (reproducibly hit by the 274-chunk
  akkadian test suite).  Fixed by using `GC_MALLOC` for both arrays.

**Documentation**

- `docs/vm.md` — new Bytecode Cache section: `.scc` format, constant-pool
  tags, cache validation, and the `GC_MALLOC` requirement.
- `docs/akkadian-reference.md` — bumped to v0.8.14; added test-coverage
  section.
- `CLAUDE.md` — expanded test suite table.

---

### 0.8.13 — `.scc` bytecode cache; Qt6 scroll/click fixes

**Bytecode cache (`.scc` files)**

- Compiled `Chunk` arrays are now cached alongside their source as
  `<script>.scc`, skipping recompilation on subsequent runs when the
  source file's mtime and size are unchanged.
- Two-tier lookup: source-adjacent `.scc` first; falls back to
  `~/.cache/curry/<mirrored-abs-path>.scc` when the source directory is
  not writable (system-installed scripts, read-only mounts, etc.).
- Cache is invalidated automatically on any content change or Curry
  version bump.  One chunk per top-level form preserves
  macro-expansion semantics across the file.
- `src/version.h` extracted so the version string is shared between
  `main.c` and `scc.c` without repetition.

**Qt6 / mandelbrot fixes**

- `setAttribute(WA_AcceptTouchEvents, false)` prevents macOS from
  swallowing trackpad scroll events as native gestures before they reach
  `wheelEvent`.
- `canvas->raise()` fixes click hit-testing on macOS where the native
  `NSScrollView` sits above `QOpenGLWidget` in the z-order.
- `timer-start!` is now idempotent — calling it on an already-running
  timer is a no-op, preventing duplicate ticks.
- `request-render!` no longer spawns actors directly; it sets a
  `*view-dirty*` flag and lets the 16 ms render timer gate actual spawns,
  preventing an O(n) thread explosion on rapid mouse-move events.

---

### 0.8.12 — `case` compiled natively by the bytecode compiler

**`case` special form in the bytecode compiler**

- `case` was handled by the tree-walking evaluator but not by the bytecode
  compiler.  Any script using `case` inside a compiled lambda would fail with
  `unbound variable: case`.
- Fixed by adding `compile_case` to `compiler.c`, which desugars `(case key
  clause...)` into `(let ((%%case-key%% key)) (cond ...))` at compile time and
  recurses into the existing `compile_let` / `compile_cond` paths.  This gives
  correct tail-call semantics for free: a `case` in tail position compiles its
  matching body in tail position.
- All three clause forms are supported:
  - `((datum...) expr...)` — eqv? match via `memv`, body compiled in sequence
  - `(else expr...)` — unconditional fallthrough
  - `((datum...) => proc)` — calls `(proc key)` on a match

---

### 0.8.11 — REPL ,vm command: GC heap stats and VM introspection

**New REPL command: `,vm`**

- Prints a snapshot of the Boehm GC heap and VM execution state:
  - `heap:` — bytes currently in use vs total heap committed to the process
  - `alloc:` — total lifetime bytes allocated (monotonically increasing)
  - `gc:` — number of GC collection cycles completed since startup
  - `stack:` — current VM value-stack depth vs the 4096-slot ceiling
  - `frames:` — current call-frame depth vs the 256-frame ceiling
- Useful for spotting memory growth, GC pressure, or unexpectedly deep
  recursion without reaching for an external profiler.
- Implemented via `GC_get_heap_size`, `GC_get_free_bytes`,
  `GC_get_total_bytes`, and `GC_gc_no` from the Boehm GC public API.
- `,help` updated to list `,vm` alongside the existing commands.

---

### 0.8.10 — GC root fix for VM struct; vm_push overflow check; Qt6 exception safety

**Critical: VM struct protected from Boehm GC collection**

- `_Thread_local VM *vm` is not scanned by Boehm GC's conservative collector
  (TLS is not in the stack, globals, or register set that GC scans).  When any
  allocation inside a primitive (e.g. `vector`) triggered a collection cycle,
  Boehm GC could determine the VM struct was unreachable, collect it, and reuse
  the memory — overwriting `vm->sp` with zero.  The subsequent `vm->sp -= argc + 1`
  wrapped to `0xffffffffffffffxx`, causing a SIGSEGV on the next stack write.
  Fixed by allocating the VM struct with `GC_MALLOC_UNCOLLECTABLE` so GC scans
  its interior for live `val_t` references but never frees it.  `vm_free` now
  calls `GC_FREE` explicitly.

**`vm_push` overflow check**

- The inline `vm_push` in `vm.h` (used by `apply_arr` and `apply` in `eval.c`)
  had no bounds check, unlike the `PUSH` macro inside `vm_run`.  Added a call to
  a new `vm_stack_overflow()` function (noreturn, raises a Scheme error) when
  `vm->sp` reaches the stack ceiling.

**Public API: `curry_is_error` / `curry_error_message`**

- Two new functions added to `include/curry.h` and implemented in `src/api.c`:
  - `curry_is_error(v)` — returns true if `v` is a Scheme error object (`T_ERROR`)
  - `curry_error_message(v)` — extracts the string message from an error object,
    or `NULL` if the message is not a string

**Qt6: VM state save/restore and exception reporting**

- When a Scheme exception fires inside `curry_apply()` from a Qt callback,
  `longjmp` bypasses the VM's normal `vm->sp` restoration, leaving the stack
  pointer corrupted for all subsequent callbacks.  `SCHEME_CALL` now saves
  `vm->sp`, `frame_count`, and `open_upvalues` via `curry_vm_state_save` before
  each callback and restores them in the exception handler.  The same save/restore
  is applied directly in `paintEvent`.
- New `qt6_print_exn(where, exn)` helper prints a human-readable error message
  (using the new `curry_error_message` API) to stderr whenever a Scheme exception
  is caught at a Qt boundary.

---

### 0.8.9 — VM bug-fixes: exactness, guard, macro expansion, profiling, MCP SSE

**Constant pool exactness preserved**

- `chunk_add_const` used `num_eq` to deduplicate constants, which ignores
  exactness — `num_eq(2, 2.0)` returns true, causing flonum `2.0` literals to be
  silently replaced by fixnum `2` in the constant pool.  Replaced with `scm_eqv`,
  which respects type: `(eqv? 2 2.0) = #f`.  Fixes the Redis test suite where
  `zscore` expected a flonum but received a fixnum.

**`T_BCCLOSURE` in the `ObjType` enum**

- `T_BCCLOSURE` was only a `#define` in `vm.h`, not a member of the `ObjType`
  enum in `object.h`.  The `vis_proc` macro did not include it, so C extensions
  (e.g. the MCP module) could not recognise VM-compiled closures as procedures.
  `T_BCCLOSURE = 41` is now in the enum and `vis_proc` checks for it.

**`guard` compiled natively**

- `guard` was delegated to the tree-walking evaluator, so bindings introduced by
  `let`/`define` in the surrounding VM frame were invisible to the guard body
  (looked up in `GLOBAL_ENV` and raised "unbound variable").  `guard` is now
  desugared at compile time into `call/cc + with-exception-handler + cond`,
  allowing it to capture local upvalues correctly.

**Macro expansion at compile time**

- The compiler now consults `GLOBAL_ENV` for syntax transformers before
  attempting to compile a call.  If the operator is a `syntax-rules` macro,
  the transformer is applied at compile time and the result compiled.  Expansion
  errors are wrapped in a `(raise ...)` form so they surface at runtime with
  full context.  Fixes `syntax_rules` test cases that called macros from
  compiled (VM) code.

**BcClosure profiling**

- Profiling hooks (level 1 call-count, level 2 timed) are now wired into
  `OP_CALL`, `OP_TAIL_CALL`, and `OP_RETURN` in `vm.c`, and into the
  `vis_bcclosure` branch of `apply()`/`apply_arr()` in `eval.c`.  `CallFrame`
  carries a `prof_start_ns` field for level-2 timing.

**MCP SSE threads: `vm_init()` on entry**

- Each SSE connection spawns a `conn_thread` (pthread).  `gc_register_thread()`
  was called but `vm_init()` was not, leaving the thread-local `vm == NULL`.
  Any `tools/call` request that invoked a BcClosure dereferenced `vm->sp` →
  SIGSEGV.  `vm_init()` is now called immediately after `gc_register_thread()`
  in `conn_thread`.

---

### 0.8.8 — VM as primary engine; call/cc, parameterize, quasiquote in compiler

**VM is now the primary script execution engine**

- `main.c` now loads scripts through `compiler_compile + vm_run` per top-level
  form instead of `scm_load` (the tree-walker).  REPL, `-e`, and file execution
  all route through the bytecode VM.

**Thread-local VM state**

- `VM *vm` changed from a process-global to `_Thread_local`; each thread must
  call `vm_init()` before using `vm_run`.  Actor threads now call `vm_init()`
  at startup, fixing a data-race SEGFAULT when actors used compiled closures.

**`call/cc` as a first-class builtin**

- `call-with-current-continuation` and `call/cc` are now registered C
  primitives (`prim_call_cc`) rather than tree-walker special forms.  They
  work correctly when called from compiled (VM) code.
- `prim_call_cc` saves and restores `vm->frame_count`, `vm->sp`, and
  `vm->open_upvalues` around `setjmp`/`longjmp` so that a longjmp escaping
  nested `vm_run` frames leaves the VM in a consistent state.  The same
  save/restore was added to `prim_with_exception_handler`.

**`parameterize` compiled natively**

- `parameterize` is removed from the eval-delegate list and compiled by a new
  `compile_parameterize()` function that desugars it at compile time to
  `let + dynamic-wind`.  Local variables referenced in the body (e.g. a
  continuation `k`) are now captured as upvalues rather than looked up in
  `GLOBAL_ENV`, fixing "unbound variable" errors when `parameterize` enclosed
  a `call/cc`-bound variable.

**`quasiquote` in the compiler**

- The compiler now handles `` ` `` / `quasiquote` directly: it calls `expand_qq()`
  from `eval.c` to expand the template into ordinary list-construction code
  and compiles the result.  `expand_qq` is now a public symbol declared in
  `eval.h`.

**Multiple-values fixes**

- `OP_VALUES N` now produces a proper `T_VALUES` object instead of a plain
  list, so `call-with-values` can distinguish a single-list return from
  multiple values.
- `OP_CALL_WITH_VALUES` and the new `prim_call_with_values` builtin both
  unpack `T_VALUES` objects and spread the values as separate arguments to
  the consumer.

**`OP_TAIL_CALL` entry-depth guard**

- The non-`BcClosure` path of `OP_TAIL_CALL` was missing the `entry_depth`
  check that detects when a nested `vm_run` call has completed.  This caused
  a SEGFAULT (executing garbage as opcodes) whenever a primitive was in tail
  position inside a nested call.  The check is now present on all return paths.

**New test suite**

- `tests/dynamic_wind_tests.scm` (16 tests) covering `make-parameter`,
  `parameterize` (normal and escape-via-`call/cc`), nested `parameterize`,
  converter callbacks, and `dynamic-wind` ordering.

---

### 0.8.7 — VM robustness and BcClosure interop

**VM safety**

- Value stack overflow (`PUSH` with > 4096 entries) now raises a proper Scheme
  error instead of silently writing past the stack array.
- Call-frame overflow (> 256 nested calls) now raises a Scheme error at both the
  `vm_run` entry point and the `OP_CALL` dispatch site; previously the check
  printed to stderr and returned `void` without unwinding.

**BcClosure interoperability**

- `procedure?` now returns `#t` for compiled (`BcClosure`) procedures.  Previously
  only tree-walker closures, primitives, continuations, and traced values were
  recognised.
- `apply_arr` (the cross-engine dispatch used by `apply`, `map`, `for-each`, etc.)
  now correctly handles `BcClosure` callees by pushing arguments onto the VM stack
  and delegating to `vm_run`.  Previously a compiled lambda passed to `map` would
  raise "not a procedure".

### 0.8.6 — Bytecode compiler and VM

Curry now executes via a **stack-based bytecode VM** instead of the
tree-walking interpreter.  All top-level evaluation — REPL, `-e`, file
load — goes through the new pipeline.

**Compiler** (`src/compiler.c`)

- Single-pass AST → `Chunk` bytecode compiler; each `lambda` produces one
  `Chunk` object with a constant pool, byte stream, and line table.
- Variable resolution at compile time: local → upvalue → global.  Upvalue
  capture marks enclosing locals and emits `[is_local, index]` capture
  descriptors after `OP_CLOSURE`.
- Lambda bodies pre-scanned for internal `(define …)` forms; pre-declared
  with a sentinel depth of `-1` giving **letrec\*** semantics.
- All scope-forming constructs (`let`, `let*`, `letrec`, `do`, named `let`)
  compiled as **lambda calls** rather than inlined scopes, preventing
  slot-index collisions when they appear as call arguments.
- Full special-form coverage: `quote`, `if`, `begin`, `define`, `set!`,
  `lambda`, `let`, `let*`, `letrec`, `letrec*`, named `let`, `and`, `or`,
  `cond` (including `=>`), `when`, `unless`, `do`, `values`, `apply`.
- Akkadian/cuneiform synonyms translated via `akk_translate()` before
  dispatch — Akkadian source compiles identically to its English equivalent.

**VM** (`src/vm.c`, `src/vm.h`)

- Flat value stack of `val_t` (`VM_STACK_MAX` = 4096); call stack of
  `CallFrame` (`VM_FRAMES_MAX` = 256).
- Calling convention: callee at `slots[-1]`, args at `slots[0..N-1]`.
  `OP_RETURN` replaces the callee+args window with the result.
- **Tail-call optimisation**: `OP_TAIL_CALL` reuses the current `CallFrame`
  for `BcClosure` callees — `memmove` args over slots, reset `ip`.
  Non-`BcClosure` callables tail-call via `apply_arr()`.
- **Upvalue protocol** (same as Lua 5): open upvalues point into the live
  stack; `vm_close_upvalues` copies them to `Upvalue.closed` on scope exit.
- `OP_CLOSE_UP A` closes the upvalue for `frame->slots[A]` without popping.
  `OP_SLIDE N` drops N locals below TOS in one step (scope-exit cleanup).
- `vm_reset()` sanitises stack state after a caught exception.
- Interoperability: primitives and tree-walker closures are dispatched
  via `apply_arr()`; both engines share `GLOBAL_ENV`.

**Opcode set** (`src/opcode.h`, `src/chunk.c`)

70 opcodes covering constants, locals, globals, upvalues, stack
manipulation, full numeric tower arithmetic, comparison, pairs/lists,
strings/chars, type predicates, vectors, control flow, calls, closures,
apply/values, exception handling, and I/O.  New opcodes: `OP_SLIDE`
(scope cleanup) and a corrected `OP_CLOSE_UP` (slot-addressed, non-popping).

**Bug fixes during development**

- `end_compiler` was emitting `OP_VOID` before `OP_RETURN`, causing all
  lambdas to return void.
- `OP_JUMP_FALSE` / `OP_JUMP_TRUE` always pop their condition; spurious
  `OP_POP` instructions after them in `compile_cond`, `compile_when`, and
  `compile_unless` were removed.
- `cond =>` clause now `DUP`s the test before `JUMP_FALSE` so the test
  value survives the pop for the `(proc test)` call.

**Documentation**

`docs/vm.md` — full architecture reference: calling convention, TCO,
upvalue open/closed protocol, compiler scope model, special-form
compilation strategies, complete opcode table, known limitations.

### 0.8.5 — Quaternion trig, non-commutative CAS, Akkadian expansion

**Quaternion numeric tower — transcendental functions**

All nine transcendental functions now handle quaternion arguments. Every `q = a + v̂·‖v‖` is embedded in the complex plane spanned by `{1, v̂}`, the complex formula is applied, and the result is reconstructed:

- `sin`, `cos`, `sinh`, `cosh`: direct closed-form in the {1, v̂} plane
- `tan`, `tanh`: routed through sin/cos
- `asin`, `acos`, `atan`: complex embedding via `z = a + ‖v‖·i`, apply formula, reconstruct
- `asinh`, `acosh`, `atanh`: extended condition covers quaternion alongside complex
- `exp`, `log`, `sqrt`: new quaternion branches using `quat_assemble()` helper
- `abs(quaternion)` now returns the Euclidean norm `√(a²+b²+c²+d²)` (previously returned the quaternion unchanged)
- `num_sub` and `num_div` (Hamilton right-division `a·conj(b)/‖b‖²`) were missing quaternion branches — added

Euler's identity `exp(πv̂) = −1` holds for any unit pure-imaginary quaternion `v̂`. The Pythagorean identity `sin²(q)+cos²(q) = 1` holds for all quaternions.

**Symbolic CAS — non-commutative products**

- `SYM_ASSUME_QUATERNION` flag on `sym-var`: `(sym-var 'q 'quaternion)` declares a quaternion-valued variable
- `SX_NCMUL` operator: an ordered, non-commutative product node. `sx_mul()` routes to `SX_NCMUL` whenever any operand is a concrete quaternion/octonion or a quaternion-flagged sym-var
- Real scalars (fixnum/flonum/bignum/rational) commute out as a leading coefficient; all other factors maintain left-to-right order
- Differentiation: ordered product rule — `∂(f₁·f₂·…·fₙ)/∂x = Σᵢ f₁·…·(∂fᵢ/∂x)·…·fₙ`
- `expand`: `expand_ncmul2()` recursively distributes NC products over sums, so `(q+p)²` yields four terms (`q²+qp+pq+p²`) rather than the commutative three
- Integer exponent expansion (`expt q n`) uses NC multiplication when the base is quaternion-flagged
- `num_is_zero`, `num_is_one`, `num_cmp` extended for quaternions so simplification rules fire correctly on quaternion coefficients

**Akkadian / cuneiform**

- `sym-assumption?` → `ṣimdat-la-idûm?` / `𒋻𒉡𒅆?` ("decree of the unknown?")
- Assumption keywords accepted in both English and Akkadian in `sym-var` and `sym-assumption?`: `ṣīrum`/real, `damqum`/positive, `lemnûm`/negative, `nikkassum`/integer, `la-ṣifrum`/nonzero, `rebûm`/quaternion

**Quaternion builtins — previously unregistered procedures now exposed**

- `quaternion-w`, `quaternion-x`, `quaternion-y`, `quaternion-z` — component accessors
- `quaternion-norm` — Euclidean norm `√(w²+x²+y²+z²)`
- `quaternion-conjugate` — `a−bi−cj−dk`
- `quaternion-normalize` — unit quaternion
- `quaternion-inverse` — `conj(q)/‖q‖²`
- `quaternion+` — variadic addition
- `quaternion*` — variadic Hamilton product
- `quaternion-rotate-vector` — rotate a 3-vector by a quaternion via `q·v·q⁻¹`
- `conj` generic now delegates to quaternion conjugate (previously fell through to no-op)
- `eqv?` and `equal?` now compare quaternions by component value, not pointer identity

**Symbolic CAS — additional simplifications**

- Like-term collection in sums: `(+ q q)` → `(nc* 2 q)`, `(+ (* 3 q) (* -3 q))` → `0`; works for commutative (`*`) and non-commutative (`nc*`) products alike
- NC product scalar folding: real-embedded quaternion `a+0i+0j+0k` folds into its real scalar part within `nc*`; a scalar of −1 folds into negation: `(* -1 q)` → `(- q)`
- NC integration factoring: leading and trailing constant quaternion factors are extracted around the integral of the variable-dependent middle block, preserving left-to-right order

**Tests**

300 assertions in `tests/numeric_ext_tests.scm`; new sections cover quaternion builtins (accessors, norm, conjugate, normalize, inverse, +, *, rotate-vector), corrected `conj`/`eqv?`/`equal?` behavior, and CAS simplifications (like-term collection, −1 folding, NC integration factoring).

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
