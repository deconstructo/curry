# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Configure with optional modules
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_CRYPTO=ON \
  -DBUILD_MODULE_LDAP=ON \
  -DBUILD_MODULE_STORAGE=ON \
  -DBUILD_MODULE_GRAPHQL=ON \
  -DBUILD_MODULE_IMAGE=ON \
  -DBUILD_MODULE_GIT=ON \
  -DBUILD_MODULE_PLPLOT=ON \
  -DBUILD_MODULE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"   # macOS only, for Qt6

# Build
cmake --build build -j$(nproc)                  # Linux
cmake --build build -j$(sysctl -n hw.logicalcpu) # macOS

# Run the REPL
./build/curry

# Run a script
./build/curry script.scm
```

## Dependencies

Required: `libgc` (Boehm GC), `libgmp`, pthreads, CMake ≥ 3.20, C11 compiler.

```bash
# Debian/Ubuntu
sudo apt install libgc-dev libgmp-dev cmake build-essential

# macOS (Homebrew)
brew install bdw-gc gmp cmake

# Optional modules — Linux
sudo apt install libssl-dev libsqlite3-dev libcurl4-openssl-dev libldap-dev \
                 libpng-dev libjpeg-dev libgit2-dev libgtk-4-dev libplplot-dev

# Optional modules — macOS
brew install openssl sqlite libgit2 libpng jpeg-turbo
# curl, ldap, and qt@6 also available via brew; curl/ldap are bundled with macOS
```

## Tests

```bash
cmake --build build && ctest --test-dir build -V

# Run only the C unit tests
./build/tests/curry_test

# Run only the Scheme R7RS tests
./build/curry tests/r7rs_tests.scm

# Run a specific test file
./build/curry tests/actors_tests.scm
./build/curry tests/numeric_ext_tests.scm

# Run a specific expression
./build/curry -e '(display (+ 1 2)) (newline)'
```

The four test suites are: `core` (C-level value/numeric/GC), `scheme_r7rs` (R7RS conformance), `numeric_ext` (Clifford algebra, symbolic CAS — differentiation/integration/Wirtinger/complex operators, surreal numbers, auto-diff, numeric tower exactness), and `actors` (concurrency primitives).

## CLI flags

```
./build/curry [options] [script.scm] [args...]
  -e EXPR    Evaluate expression and print result
  -l FILE    Load file before entering REPL
  -i         Force interactive REPL after loading scripts
  -v         Print version
```

Script arguments are bound to `command-line-args` in the global environment.

## REPL commands

Inside the REPL, comma-prefixed commands are available: `,quit`, `,help`, `,gc` (force GC), `,env` (list all global bindings). Readline history is saved to `~/.curry_history` (last 500 entries) when readline is present.

## Architecture

### Value representation (`src/value.h`)

Every Scheme value is a 64-bit `val_t` (`uintptr_t`) with a 2-bit tag in the low bits:
- `00` = heap pointer (GC-managed object, always 8-byte aligned)
- `01` = fixnum (62-bit signed integer, `vunfix(v)` / `vfix(n)`)
- `10` = character (Unicode codepoint in bits 8–31)
- `11` = immediate (`V_FALSE`, `V_TRUE`, `V_NIL`, `V_VOID`, `V_EOF`)

All heap objects begin with `Hdr { uint32_t type; uint32_t flags; }`. The type tag is an `ObjType` enum defined in `src/object.h`. Access via `as_pair(v)`, `as_str(v)`, etc.

### Memory management (`src/gc.h`)

Boehm GC (conservative, thread-safe). Allocation macros:
- `GC_NEW(T)` — struct with interior GC pointers
- `GC_NEW_ATOM(T)` — struct with no GC pointers (strings, numbers)
- `GC_NEW_FLEX(T, n)` — flexible-array struct (`T` must have `data[]` member)
- `GC_NEW_FLEX_ATOM(T, n)` — atomic flexible-array (bytevectors)

No explicit rooting needed on the stack. Call `gc_register_thread()` at the start of every new pthread.

### Numeric tower (`src/numeric.h`, `src/numeric.c`)

Hierarchy (auto-promotes upward):

```
fixnum → bignum (GMP mpz) → rational (GMP mpq) → flonum (double)
       → complex (val_t pair) → quaternion (4×double) → octonion (8×double)
       → multivector (Clifford Cl(p,q,r)) → surreal (Hahn series)
       → symbolic (CAS expression tree)
```

Overflow from fixnum goes to bignum automatically. When any arithmetic operand is symbolic the result is a symbolic expression rather than an error. Octonion multiplication uses the Graves/Cayley convention.

### Symbolic CAS (`src/symbolic.h`, `src/symbolic.c`)

`T_SYMVAR` and `T_SYMEXPR` extend the numeric tower. When any arithmetic operand is symbolic the result is a symbolic expression tree rather than an error.

**Variables and expressions**

```scheme
(sym-var 'x)               ; create symbolic variable
(symbolic x y)             ; bind x, y as symbolic unknowns in scope
(sym-var? v)               ; predicate
(sym-expr? v)              ; predicate
(symbolic? v)              ; true for both T_SYMVAR and T_SYMEXPR
(sym-var-name v)           ; recover the symbol name
(substitute expr var val)  ; replace var with val and simplify
(simplify expr)            ; algebraic simplification pass
```

**Differentiation** — `(∂ expr var)` where `var` is a sym-var:

Rules: linearity, product, quotient, power, chain rule through sin, cos, tan, exp, log, sqrt, abs, sinh, cosh, tanh, asin, acos, atan, asinh, acosh, atanh, cot, sec, csc. Unknown operators leave an unevaluated `(∂ expr var)` node.

**Integration** — `(∫ expr var)` or `(integrate expr var)`:

Returns the antiderivative (no constant of integration). Definite form: `(∫ expr var a b)` computes `F(b) − F(a)`. Works with all numeric tower types for bounds: fixnum, bignum, rational, flonum, complex.

Rules: linearity (sum/difference/neg/constant-multiple), power rule `x^n → x^(n+1)/(n+1)` (n ≠ −1), `x^−1 → ln|x|`, linear-substitution form for `(ax+b)^n`, sin, cos, tan, exp, ln, sqrt, sinh, cosh, tanh, cot, sec, csc, sec², csc², asin/acos/atan/asinh/acosh/atanh (IBP, linear arg). Unknown forms leave an unevaluated `(∫ expr var)` node.

**Complex operators** — symbolic-aware; return expression trees on sym-vars:

```scheme
(conj expr)        ; complex conjugate — also (conjugate expr)
(real-part expr)   ; Re(expr) — returns symbolic when expr is symbolic
(imag-part expr)   ; Im(expr) — returns symbolic when expr is symbolic
```

Simplification identities: `conj(conj(f)) = f`, `conj(real(f)) = real(f)`, `imag(real(f)) = 0`, `imag(conj(f)) = -imag(f)`, etc.

For a real variable `x`: `∂conj(f)/∂x = conj(∂f/∂x)`, `∫conj(f) dx = conj(∫f dx)` (same for real-part/imag-part).

**Wirtinger calculus** — treats `z` and `z̄ = conj(z)` as independent variables:

```scheme
(wirtinger-d    expr z)   ; ∂/∂z:  ∂z/∂z = 1,  ∂conj(z)/∂z = 0
(wirtinger-dbar expr z)   ; ∂/∂z̄: ∂z/∂z̄ = 0, ∂conj(z)/∂z̄ = 1
```

Key rules: `∂conj(f)/∂z = conj(∂f/∂z̄)`, `∂Re(f)/∂z = ½(∂f/∂z + conj(∂f/∂z̄))`, `∂Im(f)/∂z = (∂f/∂z − conj(∂f/∂z̄))/(2i)`. Arithmetic and holomorphic transcendentals follow the standard chain rule. A function is holomorphic iff `(wirtinger-dbar f z)` simplifies to 0.

**Polynomial / structural operations**:

```scheme
(expand expr)              ; distribute * over +; expand integer powers 2..16
(degree expr var)          ; polynomial degree in var (exact fixnum)
(leading-coeff expr var)   ; coefficient of highest-degree term (expands internally)
(collect expr var)         ; group like-degree terms, sorted by descending degree
```

`expand` fully distributes multiplications over sums and expands `(expt base n)` for integer n ∈ [2,16] by repeated distribution. `collect` calls `expand` internally and then groups terms into `(coeff * var^k)` buckets, combining coefficients of equal degree. Non-monomial sub-expressions (e.g. transcendentals of var) are left uncollected at the end of the sum.

**Auto-differentiation** via dual-number surreals: `(auto-diff f x)` evaluates `f(x + ε)` and extracts the ε coefficient = f′(x). Works for algebraic lambdas; C-level primitives (sin, cos, exp) do not propagate surreals.

Operator symbols (`SX_ADD`, `SX_MUL`, `SX_CONJ`, `SX_REAL`, `SX_IMAG`, `SX_INTEGRATE`, etc.) are interned at `symbolic_init()` time. `equal?` correctly compares symbolic expressions structurally and complex numbers by value.

### Surreal numbers (`src/surreal.h`, `src/surreal.c`)

Hahn-series representation: a sorted list of `(exponent, coefficient)` pairs. Constants `SUR_OMEGA` (ω, infinite) and `SUR_EPSILON` (ε = 1/ω, infinitesimal) are available after `surreal_init()`. Forward-mode auto-diff falls out naturally: `f(x + ε)` gives `f(x) + f'(x)·ε`.

### Multivectors / Clifford algebra (`src/multivec.h`, `src/multivec.c`)

`T_MULTIVEC` elements of Cl(p,q,r) with up to 8 basis vectors (2⁸ = 256 components). Blade indices are bitmaps. Supports geometric product (`mv_geom`), wedge (`mv_wedge`), left contraction, reverse, grade involution, dual, and grade projection. Useful algebras: Cl(3,0,0) 3D Euclidean, Cl(3,1,0) Minkowski, Cl(3,0,1) PGA, Cl(4,1,0) CGA.

### Quantum superposition (`src/quantum.h`, `src/quantum.c`)

`T_QUANTUM` represents `|ψ⟩ = Σ αᵢ|xᵢ⟩` with complex amplitudes. `(observe q)` collapses probabilistically; arithmetic maps over branches. `(superpose pairs)` builds from `(amplitude . value)` lists.

### Symbols (`src/symbol.h`, `src/symbol.c`)

All symbols are interned — pointer equality is identity. Pre-interned special-form symbols are declared via the X-macro `symbol_list.h` and available as globals (`S_DEFINE`, `S_LAMBDA`, etc.) after `sym_init()`.

### Evaluator (`src/eval.h`, `src/eval.c`)

Tree-walking interpreter with proper tail-call optimization via `goto tail` (iterative dispatch loop). All R7RS special forms are handled as cases in `eval()`. Function application always goes through the tail of the loop for closures, enabling TCO.

Exception handling uses `setjmp`/`longjmp` through the `ExnHandler` chain (`current_handler` thread-local). The `SCM_PROTECT(h, body, on_exn)` macro wraps a body with a handler frame. `call/cc` captures an escape continuation backed by a heap-allocated `jmp_buf`; upward escapes work, full first-class continuations are deferred.

Before dispatching special forms, `eval()` calls `akk_translate(op)` (`src/akkadian_eval.h`) to remap Akkadian/cuneiform synonyms to their canonical English symbols — so code can be written in Standard Babylonian Akkadian and it evaluates identically.

### Environments (`src/env.h`, `src/env.c`)

Linked list of `EnvFrame` structs (flat symbol/value arrays). `env_bind_args()` extends the closure's environment with parameter bindings on each call. `env_lookup()` raises an error on unbound variables. The global environment is `GLOBAL_ENV`.

### Module system (`src/modules.h`, `src/modules.c`)

Two kinds of modules:
1. **C extension `.so`** — exports `void curry_module_init(CurryVM *vm)`. Loaded with `dlopen`. Call `curry_define_fn` / `curry_define_val` to register bindings.
2. **Scheme `.sld` / `.scm`** — evaluated in a fresh environment; exports all top-level definitions.

Always-on modules (no build flag needed): `json`, `network`, `redis`, `regex`, `sync`, `vecdb`, `sqlite`.  
Optional modules (require `-DBUILD_MODULE_X=ON`): `crypto`, `ldap`, `storage`, `graphql`, `image`, `git`, `ui` (GTK4), `plplot`, `qt6`.

Module search order: `CURRY_MODULE_PATH` env var (colon-separated), then `lib/curry/modules/`. Module names map to paths, e.g. `(curry json)` → `curry/json.so`.

### Actor system (`src/actors.h`, `src/actors.c`)

Each actor (`T_ACTOR`) runs in a detached POSIX thread. Actors communicate exclusively via `actor_send` / `actor_receive` through per-actor `Mailbox` objects (mutex + condvar + ring buffer). The Boehm GC shared heap is thread-safe; actors do not need per-actor heaps. `current_actor` is thread-local.

Scheme primitives: `spawn`, `send!`, `receive`, `self`, `actor-alive?`.

### Sets and hash tables (`src/set.h`, `src/set.c`)

Open-addressing hash tables with 75% max load and tombstone deletion. Three comparator modes: `SET_CMP_EQ` (pointer), `SET_CMP_EQV`, `SET_CMP_EQUAL` (structural). Both `Set` and `Hashtable` use the same `val_hash` / `slot_matches` infrastructure.

### Initialization order

`gc_init() → sym_init() → num_init() → port_init() → env_init() → eval_init() → actors_init() → modules_init()`

`modules_init()` calls `builtins_register(GLOBAL_ENV)` to populate the top-level environment.

### Graphics / UI (`modules/qt6/qt6.cpp`)

The qt6 module has three API layers:

- **Layer 1** (`qt-*`): Raw Qt6 queries — painter/widget dimensions, `qt-gpu?` (OpenGL init check), `qt-process-events`.
- **Layer 2** (`gfx-*`): GPU-accelerated 2D graphics via `QPainter` on `QOpenGLWidget`. Primitives: clear, color, pen, shapes (rect, circle, ellipse, arc, pie, polygon), text, transforms (translate/rotate/scale), and batch drawing (`gfx-draw-points!`, `gfx-draw-lines!`, `gfx-fill-triangles!`). CPU software rendering falls back automatically via Mesa/llvmpipe when no GPU is present.
- **Layer 3** (natural names): Full UI framework — windows (`make-window`), canvas (`canvas-on-draw!`, `canvas-on-mouse!`), menus, toolbars, status bars, layout boxes (`make-vbox`, `make-hbox`), tabs, group boxes, and widgets (labels, buttons, checkboxes, sliders, dropdowns, radio groups, spin boxes, text inputs, progress bars, timers).

4D projection math is also included (pure C++, no Qt dependency): `project_4d_to_3d` uses perspective division on the w-axis. Scheme API: `make-4d-projector`, `project-4d`, `rotate-4d-xw`.

Cross-platform: Linux X11/Wayland (OpenGL), macOS (Metal via Qt), Windows (D3D/ANGLE).

macOS notes: modules build as `.so` bundles (`MODULE` type). The main binary uses `ENABLE_EXPORTS ON` (`-rdynamic` / `-Wl,-export_dynamic`). Module `.so` targets link with `-undefined dynamic_lookup` so `curry_*` symbols resolve from the main binary at `dlopen` time.

## Public embedding API (`include/curry.h`, `src/api.c`)

Thin wrappers around internal types for use from C extension modules. Key functions: `curry_define_fn`, `curry_define_val`, `curry_make_fixnum`, `curry_make_string`, `curry_make_pair`, `curry_call`, `curry_error`. These live in `curry_core` and resolve via `--export-dynamic`.

## Adding a new built-in procedure

In `src/builtins.c`, write a `PrimFn` with signature `val_t fn(int argc, val_t *argv, void *ud)` and register it with `DEF("name", fn, min_args, max_args)` inside `builtins_register()`.

## Adding a new C module

1. Create `modules/<name>/<name>.c` (or `.cpp`).
2. Implement `void curry_module_init(CurryVM *vm)` calling `curry_define_fn` / `curry_define_val`.
3. Add a `curry_c_module(<name>)` (or `curry_cxx_module`) call in `CMakeLists.txt` under the appropriate `option` guard.
4. Load from Scheme with `(import (curry <name>))`.

## MCP server module (`modules/mcp/mcp.c`)

Curry scripts can be offered as [Model Context Protocol](https://modelcontextprotocol.io/) servers, making
their tools callable by Claude Code and other MCP clients.  Two transports are supported:

- **stdio** — one client per process, JSON-RPC 2.0 over stdin/stdout.  Used by Claude Code's `mcpServers` config.
- **SSE** — HTTP + Server-Sent Events, multiple concurrent clients.  `GET /sse` opens a stream; `POST /message?sessionId=X` sends requests; responses arrive via the SSE stream.

### Scheme API

```scheme
(import (curry mcp))

; Register a tool.
; schema is an alist: ((param-name . ((type . "string") (description . "...") ...)) ...)
; Required params have no (default . ...) entry.  Optional ones do.
; handler receives an alist of (symbol . value) pairs — one per param.
(mcp-tool name description schema handler)

; Register a resource (static or dynamic URI).
; handler receives the URI string and must return (mcp-text ...) or (mcp-json ...).
(mcp-resource uri description handler)

; Return a plain-text result from a tool or resource handler.
(mcp-text string)

; Return a pre-serialised JSON result.
(mcp-json string)

; Emit a progress notification during a long tool call.
; Fraction = current/total.  message is an optional status string.
(mcp-notify-progress current total message)

; stdio transport — blocks reading JSON-RPC from stdin, writing to stdout.
; name and version are reported during the MCP initialize handshake.
(mcp-serve)                         ; defaults to name "curry-mcp"
(mcp-serve name version)

; SSE transport — HTTP server on port, blocks forever, supports many clients.
; Each client GETs /sse to open a stream, then POSTs to /message?sessionId=X.
(mcp-serve-sse port)
(mcp-serve-sse port name)
(mcp-serve-sse port name version)
```

### Schema format

Each parameter entry is an alist with these keys:

| Key | Required | Notes |
|-----|----------|-------|
| `type` | yes | `"string"`, `"number"`, `"integer"`, `"boolean"`, `"array"`, `"object"` |
| `description` | yes | Shown in tool descriptions |
| `default` | no | Makes the parameter optional (absent → required) |

Parameters without `default` are listed under `required` in the emitted JSON Schema.

### Handler argument alist

The handler lambda receives one argument: an alist of `(symbol . value)` pairs.
Values are decoded from the JSON call arguments:

- JSON string → Scheme string
- JSON number → Scheme number (exact integer if no decimal point, flonum otherwise)
- JSON boolean → `#t` / `#f`
- JSON array → Scheme list
- JSON null → `'()`

Use `(assq 'param-name args)` to retrieve values, or the idioms:

```scheme
(define (arg  args name)         (cdr (assq name args)))
(define (arg? args name default) (let ((p (assq name args))) (if p (cdr p) default)))
```

### Connecting from Claude Code

**stdio** (one client, spawned per session):

```json
{
  "mcpServers": {
    "my-server": {
      "command": "/path/to/build/curry",
      "args":    ["/path/to/examples/mcp_math.scm"]
    }
  }
}
```

**SSE** (persistent process, many clients):

```json
{
  "mcpServers": {
    "my-server": {
      "url": "http://localhost:8080/sse"
    }
  }
}
```

Start the SSE server with `(mcp-serve-sse 8080)` in the script.  A `[mcp] SSE server listening on port N` line is printed to stderr when ready.

### Example servers

| File | Description |
|------|-------------|
| `examples/mcp_server.scm` | Minimal demo: eval, factorial, stateful define, count-to with progress |
| `examples/mcp_math.scm` | Symbolic CAS: diff, simplify, substitute, evaluate, auto-diff, Taylor series |
| `examples/mcp_nbody.scm` | N-body gravity in D spatial dimensions (D may be non-integer) |

### Protocol notes

- **stdio** — newline-delimited JSON-RPC 2.0; one client only; dispatch is synchronous.
- **SSE** — one thread per HTTP connection; up to 32 concurrent sessions (compile-time `MAX_SESSIONS`); tool calls are serialised by a mutex (Scheme evaluator is not re-entrant); progress notifications are routed to the correct session's stream; keepalive comments sent every 15 s.
- Tool errors (caught exceptions) are returned as JSON-RPC error responses; the server continues running normally after an error.
- Global Scheme state persists across calls for the lifetime of the server process.
- `(self)` is not available from tool handlers (main thread is not an actor).

## Neo4j module (`modules/neo4j/neo4j.c`)

Fully implemented. Uses the **raw Bolt 4.x/5.x protocol** with PackStream binary encoding — no `libneo4j-client` dependency. Modelled on the Redis module.

Key points:
- Bolt version negotiated at connect time; proposes 5.4, 5.0, and the 4.0–4.4 range.
- Bolt 5.1+ authentication uses split HELLO / LOGON; earlier versions use combined HELLO.
- Queries use `RUN` + `PULL`; transactions use `BEGIN`/`COMMIT`/`ROLLBACK`.
- No external C dependencies beyond the standard socket API.
- Enable with `-DBUILD_MODULE_NEO4J=ON`; documented in `docs/module-neo4j.md`.

## Akkadian error messages

All runtime errors carry a Standard Babylonian Akkadian preamble (𒀭 ḫiṭītu — *great fault*) selected by keyword-matching the error string against `akkadian_table[]` in `src/akkadian.h`. Special-form names have Akkadian/cuneiform synonyms registered in `src/akkadian_names.h` and translated transparently in `eval()` — Akkadian code is valid Curry Scheme.
