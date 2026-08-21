# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commits

Commit after each meaningful task step, not just at the end. This keeps the history bisectable and lets either party revert to a known-good state if something goes wrong mid-task.

## Code review

After writing non-trivial C or Scheme code, spawn a fresh subagent (or use the `/code-review` skill) to review it before declaring the task done. The subagent should not share context with the writing session — the point is an independent read. Pay particular attention to: array bounds vs loop bounds, off-by-one errors in sexagesimal/numeric code, and cuneiform reader edge cases.

## Security review

After writing non-trivial C or Scheme code, spawn a fresh subagent (or use the `/security-review` skill) to review it before declaring the task done. The subagent should not share context with the writing session — the point is an independent read. Pay particular attention to: array bounds vs loop bounds, off-by-one errors in sexagesimal/numeric code, and cuneiform reader edge cases.

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
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"   # macOS only, for Qt6 (see note below)



# Build
cmake --build build -j$(nproc)                  # Linux
cmake --build build -j$(sysctl -n hw.logicalcpu) # macOS

# Run the REPL
./build/curry

# Run a script
./build/curry script.scm
```

**Qt6 on macOS, note:** `brew --prefix qt@6` resolves to Homebrew's umbrella `qt`
formula, whose `lib/cmake/Qt6/` does *not* actually contain `Qt6Config.cmake`
(only `qtbase`, a dependency of `qt`, ships it) — passing that prefix alone used
to make `find_package(Qt6 …)` silently fail and skip the module with just a
`WARNING`, not an error, so it's easy to not notice. `CMakeLists.txt` now
falls back to `brew --prefix qtbase` automatically on macOS when the initial
`find_package` misses, so the command above works either way; pass
`-DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"` directly if you want to skip
the fallback. Once found, the qt6 module also bakes in Homebrew's Qt plugin
directory at build time (see `CURRY_QT6_PLUGIN_DIR` in `modules/qt6/qt6.cpp`),
so `(import (curry qt6))` no longer aborts with *"Could not find the Qt
platform plugin cocoa"* — no `QT_QPA_PLATFORM_PLUGIN_PATH` env var needed.

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

**When working on the compiler, VM, or anything else that changes codegen, always run `curry` against `.scm` test files with `--clear-cache`** (or delete stale `.scc` files first — `find tests -name '*.scc' -delete`). The transparent `.scc` cache (see below) is keyed on source *content* hash, not compiler version: a `.scc` file compiled by yesterday's binary is still a cache HIT today if the `.scm` source hasn't changed, silently serving old bytecode instead of re-running your changes through the compiler. `ctest`'s Scheme-based suites (`scheme_r7rs`, `scheme_r6rs`, `syntax_rules`, every `srfi_*`/module test, etc. — everything except the pure-C `core` target) invoke `curry` on checked-in `.scm` files this same way and are just as exposed: a stale `.scc` left over from before your change can make `ctest` report a pass that never actually exercised the new code. Confirmed concretely during Tier 2.1/2.2 IR development: `ctest` reported 99/99 passing against `.scc` files compiled the day before a live-dispatch refactor landed (`curry --timings tests/r7rs_tests.scm` showed `cache HIT`) — the suite wasn't lying, but it also wasn't testing the refactor. Clearing all `.scc` files and rerunning was the only way to get a genuine result.

```bash
cmake --build build && ctest --test-dir build -V

# Run only the C unit tests
./build/tests/curry_test

# Run only the Scheme R7RS tests -- --clear-cache forces a fresh
# compile instead of a possible stale cache hit (see above)
./build/curry --clear-cache tests/r7rs_tests.scm

# Run a specific test file
./build/curry --clear-cache tests/actors_tests.scm
./build/curry --clear-cache tests/numeric_ext_tests.scm

# Run a specific expression (not cached -- -e is never subject to the
# .scc cache, which only applies to a positional script-file argument)
./build/curry -e '(display (+ 1 2)) (newline)'
```

The test suites registered in `ctest`:

| Name | Command | What it covers |
|------|---------|----------------|
| `core` | `curry_test` (C binary) | C-level value/numeric/GC primitives |
| `scheme_r7rs` | `r7rs_tests.scm` | R7RS conformance |
| `scheme_r6rs` | `r6rs_tests.scm` | R6RS compatibility: `library` form, `(rnrs)` imports, R6RS `define-record-type`, SRFI-27 random |
| `cond_expand` | `cond_expand_tests.scm` | `(features)` and `cond-expand`: feature identifiers, `and`/`or`/`not`/`library` requirements, `else`, no-match error, `define-library`-declaration position (incl. nested `cond-expand`) |
| `srfi_s279_inspect` | `srfi_s279_inspect_tests.scm` | `(srfi 279)` In(tro)spection Protocol (`inspect-properties`/`inspect-describe`) across numbers/booleans/pairs/symbols/chars/strings/vectors/bytevectors/errors/hash-tables/boxes/sets/bags/records, plus the `record?`/`record-rtd`/`record-type-name`/`record-type-field-names` primitives it's built on |
| `include_relative` | `include_relative_tests.scm` (+ `fixtures/include_relative/`) | `(include ...)` inside a `define-library` declaration resolves relative to the *including file's own directory*, not the process's cwd — regression test for a real bug found while porting SRFI-279 upstream (a library whose own directory wasn't cwd couldn't portably `(include "sibling.scm")`); runs deliberately from `build/tests`, not the fixture directory |
| `define_library_stack_guard` | `define_library_stack_guard_tests.scm` | Non-tail recursion inside a `define-library` body (tree-walked via `eval()`, not the compiled VM path) raises a catchable `stack-overflow` condition instead of SIGSEGV-ing the whole process — `eval()`'s own stack-depth guard (`src/eval.c`, per-thread cached stack base vs. current stack pointer, checked once per real C-level entry, not per `goto tail` iteration) |
| `load_dir_context_script_relative` | `fixtures/load_dir_context/script_relative_load/main.scm` | A top-level script's own `(load "relative/path.scm")` resolves against *the script's own directory* (main.c's positional-script-argument path now marks it), not ctest's cwd |
| `load_dir_context_exception_safety` | `fixtures/load_dir_context/exception_safety/main.scm` | A `(load ...)` that raises, caught by `guard`, doesn't leave stale directory-context state behind that corrupts a later unrelated `(load ...)` — `scm_load`/`load_scheme_module` release back to a saved mark on the exceptional path (`SCM_PROTECT`), not just normal exit |
| `load_dir_context_thread_safety` | `fixtures/load_dir_context/thread_safety/main.scm` | The directory-context stack is `_Thread_local` and a spawned actor inherits its spawning thread's stack (`load_dir_snapshot`/`load_dir_adopt_snapshot`) — 20 concurrent actors, each verified correct on every one of 30 iterations |
| `syntax_rules` | `syntax_rules_tests.scm` | `syntax-rules`/`define-syntax`/`let-syntax`/`letrec-syntax`, plus the "Partial hygiene" per-expansion renaming of template-introduced symbols in `src/syntax_rules.c` (recursive macros that accumulate fresh bindings, quoted symbolic data staying literal, a library-local macro's self-reference resolving in its own defining environment, not `GLOBAL_ENV`) |
| `srfi_s26_cut` | `srfi_s26_cut_tests.scm` | `(srfi 26)` `cut`/`cute` — the reference implementation verbatim; doubles as the motivating regression suite for the `syntax-rules` hygiene fix above |
| `srfi_s14_char_sets` | `srfi_s14_char_sets_tests.scm` | `(srfi 14)` char-sets — construction, iteration, set algebra, comparisons, and the standard predefined sets (full-Unicode letter/digit/whitespace/upper/lower-case backed by curry's own classification tables; ASCII-only punctuation/symbol/control/etc) |
| `numeric_ext` | `numeric_ext_tests.scm` | Clifford algebra, symbolic CAS, surreal numbers, auto-diff, numeric tower |
| `actors` | `actors_tests.scm` | Concurrency primitives (spawn/send!/receive) |
| `dynamic_wind` | `dynamic_wind_tests.scm` | `dynamic-wind`, `call/cc` interactions |
| `akkadian` | `akkadian_tests.scm` | Every Akkadian/cuneiform synonym (both transliterated and cuneiform forms) for all AKK_SF/AKK_PR entries in `akkadian_names.h` plus every per-library synonym declared via `lib/curry/modules/curry/private/lang-aliases.scm` across the SRFI libraries — 1736 assertions |
| `sexagesimal` | `sexagesimal_tests.scm` | Babylonian base-60 I/O: `#s` reader, cuneiform Unicode reader (now covering the whole numeric tower: rational/flonum fractions, complex/quaternion/octonion/multivector, surreal, symbolic), `number->string`/`string->number` with `'neugebauer`/`'cuneiform`, `current-number-notation`, `(curry sexagesimal)` module — 110 assertions |
| `babylonian_astronomy` | `babylonian_astronomy_tests.scm` | `(curry babylonian-astronomy)`: zigzag function, synodic-month/Saros eclipse-cycle constants, civil calendar month names, Akkadian aliases — 26 assertions |
| `cli` | `test_cli.sh` | CLI flags: shebang handling, `-c`/`-o`/`-x`, combined getopt, magic-byte detection for extension-less `.scc` files, `-l` load, script argument passing — 30 assertions |
| `lsp` | `test_lsp.sh` | `(curry lsp)` over real Content-Length-framed stdio: `initialize` capabilities, reader-driven diagnostics (raise + clear on `didChange`), hover (special forms/builtins/Akkadian synonyms), completion (static table + structurally-collected local bindings), the nesting-depth crash guard (including the `#\"`/`#\;` character-literal bypass), `didClose`, unknown-method errors — 27 assertions |
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
  -b SPEC    Set a debugger breakpoint before running (function name
             or file:line; repeatable)
  -v         Print version
  --timings  Print a read/expand/compile/execute pipeline timing report
             to stderr on exit (plus a cache HIT/MISS line for script runs)
  --gc BACKEND          GC backend: boehm (default) or generational (experimental)
  --gc-max-heap N       Limit GC heap (suffixes K/M/G; 0 = unlimited)
  --gc-nursery-size N   Per-thread nursery size (requires --gc generational)
```

Passing a `.scc` file as the positional argument runs it directly from bytecode without needing the original `.scm` source.

Scripts support shebang lines — `#!` is treated as a line comment, so `#!/usr/bin/env curry` works correctly. Make the file executable with `chmod +x script.scm` and invoke it directly.

Compiled `.scc` files also support direct execution: `curry -c script.scm` produces `script.scc` with a shebang prepended and the executable bit set, so `./script.scc` works immediately.

### Transparent `.scc` cache

Running `curry script.scm` directly (no `-c` needed) auto-compiles and writes a `.scc` cache next to the source (or under `~/.cache/curry/` if that directory isn't writable), then reuses it on the next run. Cache validity is keyed on a content hash (FNV-1a 64 over the full source, `src_hash()` in `src/scc.c`) rather than mtime/size, so it can't be fooled by `git checkout`/`cp -p`/editors that touch mtime without changing content. Non-regular-file sources (e.g. bash process substitution, `curry <(...)`) are never cached — hashing would consume a one-shot stream before the real compile pass could read it, so `src_hash()` refuses anything that isn't `S_ISREG` via a `stat()` check up front. Run with `--timings` to see `cache: HIT`/`MISS` for a given script run.

Script arguments are available as `(command-line)` (R7RS thunk returning a list of strings) and as `command-line-args` (legacy variable, same list).

## REPL commands

Inside the REPL, comma-prefixed commands are available: `,quit`, `,help`, `,gc` (force GC), `,env` (list all global bindings), `,vm` (VM/heap stats), `,profile`, and the debugger commands `,break <fn|file:line>`, `,unbreak <n>`, `,breaks`, `,debug <expr>`. Readline history is saved to `~/.curry_history` (last 500 entries) when readline is present.

## Interactive debugger

gdb-style debugger for VM-compiled code: breakpoints by function name or file:line (`,break` in the REPL, `-b SPEC` on the CLI, `(breakpoint)` in source), step/next/finish/continue, `bt`, `locals` (named, including captured upvalues), `p <expr>`. One flag check per dispatch when idle; JIT tier bypassed while armed; tree-walker code (`load`, `tree-eval`) is invisible to it; main thread only. `.scc` v3 persists all debug metadata so cached script runs debug identically. See `docs/reference/debugger.md`.

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

`#s1;30` → `3/2`; `#s1,0,0` → `3600`. Cuneiform Unicode tokens (`𒁹𒌋𒑊`) are also valid reader tokens. `number->string`/`string->number` accept `'neugebauer` and `'cuneiform` as a second argument; `'cuneiform` covers the whole numeric tower (rational/flonum fractions via a `·` radix separator, complex via a 𒄿 imaginary-unit marker, quaternion/octonion/multivector via a 𒂊 basis-blade marker, surreal/symbolic writer-only) — see `sex_parse_cuneiform_extended` in `src/numeric.c`. `current-number-notation` sets REPL display mode. The `(curry sexagesimal)` module (`lib/curry/modules/curry/sexagesimal.scm`) provides `rational->sexagesimal`, `hms->seconds`, `dms->degrees`, and notation-conversion helpers. See `docs/reference/module-sexagesimal.md`. `(curry babylonian-astronomy)` (`lib/curry/modules/curry/babylonian-astronomy.scm`) builds on this for actual Babylonian mathematical-astronomy techniques (zigzag function, synodic-month/Saros eclipse cycle, civil calendar) — see `docs/reference/module-babylonian-astronomy.md`.

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

Before dispatching special forms, `eval()` calls `lang_translate(op)` to remap Akkadian/cuneiform synonyms to their canonical English symbols. `lang_translate`/`lang_pr_lookup` (`src/lang_registry.h`) are the language-agnostic entry points into a pluggable registry of "language packs" (`src/akkadian_lang.c` is Akkadian's own pack, built from `src/akkadian_names.h`); adding another language means writing a new names header + pack file and registering it in `lang_registry_init()`, not touching `eval.c`/`compiler.c`/`builtins.c`/`modules.c`.

### Environments (`src/env.h`, `src/env.c`)

Linked list of `EnvFrame` structs (flat symbol/value arrays). `env_lookup()` raises on unbound variables. Global environment is `GLOBAL_ENV`.

### Module system (`src/modules.h`, `src/modules.c`)

Two kinds: C extension `.so` (exports `curry_module_init`) and Scheme `.sld`/`.scm`. Always-on: `json`, `network`, `redis`, `regex`, `sync`, `vecdb`, `sqlite`. Optional (`-DBUILD_MODULE_X=ON`, most default ON): `crypto`, `ldap`, `storage`, `http`, `graphql`, `image`, `git`, `ui`, `plplot`, `qt6`, `posix` (SRFI-170 filesystem/process bindings + SRFI-112 environment inquiry), `codesets` (SRFI-238 errno/signal/http-status lookup). Search order: `CURRY_MODULE_PATH`, then `lib/curry/modules/`.

Every pure-Scheme `(curry X)` module should be wrapped in `(define-library (curry X) (import ...) (export ...) (begin ...))`, matching the SRFI libraries' existing convention. A `define-library` body runs in a fresh environment with **no parent** (`env_new_root()` in `src/env.c`) — nothing is visible except what's explicitly imported, not even core builtins, though `(scheme base)`/`(scheme write)`/`(scheme inexact)`/etc. all alias the same flat `GLOBAL_ENV`, so importing `(scheme base)` alone normally reaches the whole core builtin surface. The one non-obvious gotcha: curry's `syntax-rules` is not hygienic across `define-library` boundaries, so if an **exported macro's expansion** references a helper procedure/macro/value not itself exported, importers get an `unbound-variable` error the first time they *use* the macro (not at import time) — trace every `define-syntax`'s expansion and export everything it transitively reaches. See [`docs/reference/writing-a-module.md`](docs/reference/writing-a-module.md) for the full pattern and worked examples.

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

Raw Bolt 4.x/5.x protocol with PackStream encoding — no `libneo4j-client` dependency. Bolt version negotiated at connect time; Bolt 5.1+ uses split HELLO/LOGON. Enable with `-DBUILD_MODULE_NEO4J=ON`. `neo4j-connect-tls` wraps the socket in TLS (OpenSSL, auto-detected at configure time) before the handshake, for encrypted connections; plain `neo4j-connect` sends credentials in cleartext. See `docs/reference/module-neo4j.md`.

## R6RS compatibility

Supported: `library` form, `(rnrs)` sub-library aliases, R6RS `define-record-type` (`fields (mutable/immutable …)`), import filters (`only`, `except`, `rename`, `prefix`), `(for lib phase)` (phase stripped), SRFI-27 random numbers. Not supported: `syntax-case`, `identifier-syntax`, R6RS condition hierarchy, `.sps` phase semantics.

## R7RS compliance gaps

Not yet implemented:
- Full first-class continuations — requires CPS or copying evaluator; current `setjmp`/`longjmp` supports upward-only escape

## Akkadian error messages

All runtime errors carry a Standard Babylonian Akkadian preamble (𒀭 ḫiṭītu — *great fault*) from `src/akkadian.h`. Special-form names have Akkadian/cuneiform synonyms in `src/akkadian_names.h`; `eval()` translates them transparently — Akkadian code is valid Curry Scheme.

Two mechanisms distribute foreign-language procedure aliases, chosen by where the name is defined:
- **Names bound in `GLOBAL_ENV` at `builtins_register()` time** (R7RS/R6RS core, and C `.so` modules like `crypto`/`json`/`mcp`/`posix`/`codesets`) get their aliases from `src/akkadian_names.h`'s `AKK_PR`/`AKK_SF` entries, compiled into the `src/lang_registry.c` pack and applied by `builtins.c`'s startup loop (core) or `modules.c`'s `import_binding()` at import time (C modules — those aliases only come to exist once the module is imported, since the module's own env doesn't exist before then).
- **Names defined inside a pure-Scheme library** (every SRFI, every `(curry oop)`/`(curry sets)`/etc.) get their aliases declared in that same `.scm` file via `lib/curry/modules/curry/private/lang-aliases.scm`'s `define-name-aliases`/`define-syntax-aliases` macros — see that file's header comment for the pattern, including a reader gotcha (never list two cuneiform forms back-to-back in an `export` clause; they merge into one symbol on read).

A C header entry never covers a pure-Scheme library's own procedures, and a Scheme alias declaration never applies to a C-module or core binding — pick the one matching where the name is actually defined.
