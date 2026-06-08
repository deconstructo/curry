# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commits

Commit after each meaningful task step, not just at the end. This keeps the history bisectable and lets either party revert to a known-good state if something goes wrong mid-task.

## Code review

After writing non-trivial C or Scheme code, spawn a fresh subagent (or use the `/code-review` skill) to review it before declaring the task done. The subagent should not share context with the writing session — the point is an independent read. Pay particular attention to: array bounds vs loop bounds, off-by-one errors in sexagesimal/numeric code, and cuneiform reader edge cases.

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
  -DBUILD_FFI=ON \
  -DBUILD_LLVM=ON \
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

# LLVM JIT backend — Linux (requires LLVM ≥ 15)
# Ubuntu 24.04 / Debian bookworm: LLVM 18 is in the main repo
sudo apt install llvm-18-dev
cmake -B build -DBUILD_LLVM=ON -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm

# Ubuntu 22.04 (jammy): default repo only has LLVM 14; add the LLVM apt repo first
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" \
  | sudo tee /etc/apt/sources.list.d/llvm.list
sudo apt update && sudo apt install llvm-18-dev
cmake -B build -DBUILD_LLVM=ON -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm

# Optional modules — macOS
brew install openssl sqlite libgit2 libpng jpeg-turbo
# curl, ldap, and qt@6 also available via brew; curl/ldap are bundled with macOS

# LLVM JIT backend — macOS
brew install llvm
cmake -B build -DBUILD_LLVM=ON -DCMAKE_PREFIX_PATH="$(brew --prefix llvm)"
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

The test suites registered in `ctest`:

| Name | Command | What it covers |
|------|---------|----------------|
| `core` | `curry_test` (C binary) | C-level value/numeric/GC primitives |
| `scheme_r7rs` | `r7rs_tests.scm` | R7RS conformance |
| `scheme_r6rs` | `r6rs_tests.scm` | R6RS compatibility: `library` form, `(rnrs)` imports, R6RS `define-record-type`, SRFI-27 random |
| `numeric_ext` | `numeric_ext_tests.scm` | Clifford algebra, symbolic CAS, surreal numbers, auto-diff, numeric tower |
| `actors` | `actors_tests.scm` | Concurrency primitives (spawn/send!/receive) |
| `dynamic_wind` | `dynamic_wind_tests.scm` | `dynamic-wind`, `call/cc` interactions |
| `syntax_rules` | `syntax_rules_tests.scm` | `define-syntax`/`syntax-rules` hygiene |
| `akkadian` | `akkadian_tests.scm` | Every Akkadian/cuneiform synonym (both transliterated and cuneiform forms) for all AKK_SF and AKK_PR entries in `akkadian_names.h` — 205 assertions |
| `sexagesimal` | `sexagesimal_tests.scm` | Babylonian base-60 I/O: `#s` reader, cuneiform Unicode reader, `number->string`/`string->number` with `'neugebauer`/`'cuneiform`, `current-number-notation`, `(curry sexagesimal)` module — 76 assertions |
| `cli` | `test_cli.sh` | CLI flags: shebang handling, `-c`/`-o`/`-x`, combined getopt, magic-byte detection for extension-less `.scc` files, `-l` load, script argument passing — 30 assertions |
| `ode`, `pde_numerical`, `d_operator`, `tuples`, `partial`, `trig`, `sicm` | individual `.scm` files | Numeric/physics modules |

## CLI flags

```
./build/curry [options] [script.scm|script.scc] [args...]
  -e EXPR    Evaluate expression and print result
  -l FILE    Load file before entering REPL
  -i         Force interactive REPL after loading scripts
  -c FILE    Compile FILE to .scc bytecode without executing it
  -o OUT     Output path for -c (default: FILE with .scc extension)
  -x         Make -c output executable (prepends shebang, sets +x)
  -v         Print version
```

Passing a `.scc` file as the positional argument runs it directly from bytecode without needing the original `.scm` source.

Scripts support shebang lines — `#!` is treated as a line comment, so `#!/usr/bin/env curry` works correctly. Make the file executable with `chmod +x script.scm` and invoke it directly.

Compiled `.scc` files also support direct execution: `curry -c script.scm` produces `script.scc` with a shebang prepended and the executable bit set, so `./script.scc` works immediately.

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

Boehm GC (conservative, thread-safe). Allocation macros: `GC_NEW(T)`, `GC_NEW_ATOM(T)`, `GC_NEW_FLEX(T, n)`, `GC_NEW_FLEX_ATOM(T, n)`. No explicit rooting needed on the stack. Call `gc_register_thread()` at the start of every new pthread.

### Numeric tower (`src/numeric.h`, `src/numeric.c`)

```
fixnum → bignum (GMP mpz) → rational (GMP mpq) → flonum (double)
       → complex → quaternion (4×double) → octonion (8×double)
       → multivector (Clifford Cl(p,q,r)) → surreal (Hahn series)
       → symbolic (CAS expression tree)
```

Overflow promotes automatically. When any operand is symbolic the result is a symbolic expression tree. See `docs/reference/symbolic.md` for the full CAS API.

### Sexagesimal / Babylonian base-60 (`src/numeric.c`, `src/reader.c`)

`#s1;30` → `3/2`; `#s1,0,0` → `3600`. Cuneiform Unicode tokens (`𒁹𒌋𒑊`) are also valid reader tokens. `number->string`/`string->number` accept `'neugebauer` and `'cuneiform` as a second argument. `current-number-notation` sets REPL display mode. The `(curry sexagesimal)` module (`lib/curry/modules/curry/sexagesimal.scm`) provides `rational->sexagesimal`, `hms->seconds`, `dms->degrees`, and notation-conversion helpers. See `docs/reference/module-sexagesimal.md`.

### Symbolic CAS (`src/symbolic.h`, `src/symbolic.c`)

`T_SYMVAR` / `T_SYMEXPR` extend the numeric tower. Key entry points: `sym-var`, `symbolic`, `substitute`, `simplify`, `∂`, `∫`, `limit`, `expand`, `collect`, `grad`, `divergence`, `curl`, `laplacian`, `wirtinger-d`, `wirtinger-dbar`. Assumptions (`'positive`, `'real`, `'quaternion`, etc.) are stored in `SymVar.hdr.flags` and guide simplification. See `docs/reference/symbolic.md`.

### Surreal numbers (`src/surreal.h`, `src/surreal.c`)

Hahn-series representation: sorted `(exponent, coefficient)` pairs. `SUR_OMEGA` (ω) and `SUR_EPSILON` (ε = 1/ω) available after `surreal_init()`. Forward-mode auto-diff: `f(x + ε)` gives `f(x) + f′(x)·ε`.

### Multivectors / Clifford algebra (`src/multivec.h`, `src/multivec.c`)

`T_MULTIVEC` elements of Cl(p,q,r) with up to 8 basis vectors (256 components). Blade indices are bitmaps. Operations: geometric product, wedge, left contraction, reverse, grade projection, dual.

### Quantum superposition (`src/quantum.h`, `src/quantum.c`)

`T_QUANTUM` represents `|ψ⟩ = Σ αᵢ|xᵢ⟩` with complex amplitudes. `(observe q)` collapses probabilistically; arithmetic maps over branches.

### Symbols (`src/symbol.h`, `src/symbol.c`)

All symbols are interned — pointer equality is identity. Pre-interned special-form symbols declared via `symbol_list.h`; available as globals (`S_DEFINE`, `S_LAMBDA`, …) after `sym_init()`.

### Evaluator (`src/eval.h`, `src/eval.c`)

Tree-walking interpreter with TCO via `goto tail`. Exception handling: `setjmp`/`longjmp` through `ExnHandler` chain; `SCM_PROTECT` macro wraps handler frames. `call/cc` gives upward-only escape continuations (full first-class continuations are deferred).

CL-style condition system (`src/condition.h`): `handler-bind` installs non-unwinding handlers; `with-restarts` / `invoke-restart` provide restarts. `with-restarts` snapshots VM state before the body and restores it before running a restart thunk.

C FFI (`src/ffi.c`, `BUILD_FFI=ON`): libffi-backed. Types `T_CPTR`, `T_FOREIGN_LIB`, `T_FOREIGN_FN`. `curry_ffi.h` (renamed from `ffi.h` to avoid collision with libffi's `<ffi.h>`).

Before dispatching special forms, `eval()` calls `akk_translate(op)` to remap Akkadian/cuneiform synonyms to their canonical English symbols.

### Environments (`src/env.h`, `src/env.c`)

Linked list of `EnvFrame` structs (flat symbol/value arrays). `env_lookup()` raises on unbound variables. Global environment is `GLOBAL_ENV`.

### Module system (`src/modules.h`, `src/modules.c`)

Two kinds: C extension `.so` (exports `curry_module_init`) and Scheme `.sld`/`.scm`. Always-on: `json`, `network`, `redis`, `regex`, `sync`, `vecdb`, `sqlite`. Optional (`-DBUILD_MODULE_X=ON`): `crypto`, `ldap`, `storage`, `http`, `graphql`, `image`, `git`, `ui`, `plplot`, `qt6`. Search order: `CURRY_MODULE_PATH`, then `lib/curry/modules/`.

### Actor system (`src/actors.h`, `src/actors.c`)

Each actor (`T_ACTOR`) runs in a detached POSIX thread communicating via per-actor `Mailbox` (mutex + condvar + ring buffer). Primitives: `spawn`, `send!`, `receive`, `self`, `actor-alive?`.

### Sets and hash tables (`src/set.h`, `src/set.c`)

Open-addressing hash tables, 75% max load, tombstone deletion. Comparator modes: `SET_CMP_EQ`, `SET_CMP_EQV`, `SET_CMP_EQUAL`.

### Initialization order

`gc_init() → sym_init() → num_init() → port_init() → env_init() → eval_init() → actors_init() → modules_init()`

### Graphics / UI (`modules/qt6/qt6.cpp`)

Three layers: `qt-*` (raw queries), `gfx-*` (2D GPU via QPainter/QOpenGLWidget), and Layer 3 (full UI: windows, canvas, menus, widgets, timers, dialogs). GLSL shader API: `make-gl-shader`, `gl-shader-draw!`, `make-gl-texture`, `make-gl-buffer`, `make-gl-framebuffer`. Key/mouse events, mouse capture (`canvas-grab-mouse!`), HiDPI, cross-platform (Linux/macOS/Windows). See `docs/reference/module-qt6.md`.

macOS: modules build as `.so` bundles with `-undefined dynamic_lookup`; main binary uses `ENABLE_EXPORTS ON`.

## Public embedding API (`include/curry.h`, `src/api.c`)

`curry_define_fn`, `curry_define_val`, `curry_make_fixnum`, `curry_make_string`, `curry_make_pair`, `curry_call`, `curry_error`. Live in `curry_core`, resolve via `--export-dynamic`.

## Parallel map, reduce, and for-each (`src/builtins_curry.c`, `src/workpool.c`)

`map` and `reduce` go parallel above `map_par_threshold` (default 8); `for-each/par` is the explicit parallel variant. Backed by a persistent Chase-Lev work-stealing thread pool (`hw_concurrency()` workers). Sequential variants: `map/seq`, `reduce/seq`. `(hardware-concurrency)` returns the pool size. `reduce` identity is applied once total, not per-chunk.

## Adding a new built-in procedure

In `src/builtins.c`, write `val_t fn(int argc, val_t *argv, void *ud)` and register with `DEF("name", fn, min_args, max_args)` inside `builtins_register()`.

## Adding a new C module

1. Create `modules/<name>/<name>.c` (or `.cpp`).
2. Implement `void curry_module_init(CurryVM *vm)`.
3. Add `curry_c_module(<name>)` in `CMakeLists.txt` under the appropriate `option` guard.
4. Load with `(import (curry <name>))`.

## MCP server module (`modules/mcp/mcp.c`)

Curry scripts serve as [Model Context Protocol](https://modelcontextprotocol.io/) servers. Two transports: **stdio** (one client, JSON-RPC over stdin/stdout) and **SSE** (HTTP + Server-Sent Events, up to 32 concurrent clients). Auth modes: `none`, `self-contained`, `introspect` (RFC 7662), `jwt` (RFC 7519). Entry points: `mcp-tool`, `mcp-resource`, `mcp-serve`, `mcp-serve-sse`. Examples in `examples/mcp_*.scm`. See `docs/reference/module-mcp.md`.

## Neo4j module (`modules/neo4j/neo4j.c`)

Raw Bolt 4.x/5.x protocol with PackStream encoding — no `libneo4j-client` dependency. Bolt version negotiated at connect time; Bolt 5.1+ uses split HELLO/LOGON. Enable with `-DBUILD_MODULE_NEO4J=ON`. See `docs/reference/module-neo4j.md`.

## R6RS compatibility

Supported: `library` form, `(rnrs)` sub-library aliases, R6RS `define-record-type` (`fields (mutable/immutable …)`), import filters (`only`, `except`, `rename`, `prefix`), `(for lib phase)` (phase stripped), SRFI-27 random numbers. Not supported: `syntax-case`, `identifier-syntax`, R6RS condition hierarchy, `.sps` phase semantics.

## R7RS compliance gaps

Not yet implemented:
- `open-input-bytevector`, `open-output-bytevector`, `get-output-bytevector` — bytevector ports (`port_get_output_bytevector` declared but not implemented in `src/port.c`)
- `write-shared` — needs pointer→label hash table for shared/cyclic datum labels
- `command-line` — currently `command-line-args` stores symbols; needs `scm_make_string` + alias
- `string-set!` / `string-copy!` — only correct when replacement char has same UTF-8 byte width (flat UTF-8 storage)
- Full first-class continuations — requires CPS or copying evaluator; current `setjmp`/`longjmp` supports upward-only escape

## Akkadian error messages

All runtime errors carry a Standard Babylonian Akkadian preamble (𒀭 ḫiṭītu — *great fault*) from `src/akkadian.h`. Special-form names have Akkadian/cuneiform synonyms in `src/akkadian_names.h`; `eval()` translates them transparently — Akkadian code is valid Curry Scheme.
