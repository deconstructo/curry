# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Configure with optional modules
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_QT6=ON \
  -DBUILD_MODULE_SYMENGINE=ON \
  -DBUILD_MODULE_NEO4J=OFF \
  -DBUILD_MODULE_VECDB=ON

# Build
cmake --build build -j$(nproc)

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

# macOS (Homebrew — native Apple Silicon or x86_64)
brew install bdw-gc gmp cmake

# Optional modules — Linux
sudo apt install libsqlite3-dev        # sqlite
sudo apt install libcurl4-openssl-dev  # storage, graphql
sudo apt install libldap-dev           # ldap
sudo apt install libpng-dev libjpeg-dev # image
sudo apt install libgit2-dev           # git

# Optional modules — macOS
brew install openssl sqlite libgit2 libpng jpeg-turbo
# curl and ldap are bundled with macOS (no extra install needed)
```

## Building on macOS

Install Xcode command-line tools first:
```bash
xcode-select --install
```

Install required dependencies:
```bash
brew install bdw-gc gmp cmake
```

Configure and build (same as Linux):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.logicalcpu)
./build/curry
```

### Qt6 module on macOS

```bash
brew install qt@6

# Qt6 from Homebrew is not on PATH by default — tell CMake where it is:
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Qt6 on macOS uses Metal for rendering. `QOpenGLWidget` is bridged through
Apple's OpenGL compatibility layer; expect deprecation warnings at runtime
(`libGL error: ...`) but the module works correctly.

### Notes

- Modules are built as `.so` bundles (CMake `MODULE` type) on both Linux and
  macOS, so `(import (curry qt6))` works identically on both platforms.
- Symbol export uses `ENABLE_EXPORTS ON` which maps to `-rdynamic` on Linux
  and `-Wl,-export_dynamic` on macOS — no manual linker flags needed.
- Boehm GC tracks dlopen'd images via `_dyld_register_func_for_add_image` on
  macOS, so GC roots in module data segments (including `s_proc_roots` in the
  qt6 module) are scanned correctly.

## Tests

```bash
cmake --build build && ctest --test-dir build -V

# Run only the C unit tests
./build/tests/curry_test

# Run only the Scheme R7RS tests
./build/curry tests/r7rs_tests.scm

# Run a specific expression
./build/curry -e '(display (+ 1 2)) (newline)'
```

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

Hierarchy: `fixnum → bignum (GMP mpz) → rational (GMP mpq) → flonum (double) → complex (val_t pair) → quaternion (4×double) → octonion (8×double)`.

Arithmetic functions (`num_add`, `num_mul`, etc.) auto-promote between levels. Overflow from fixnum arithmetic goes to bignum automatically. The octonion multiplication table uses the Graves/Cayley convention.

### Symbols (`src/symbol.h`, `src/symbol.c`)

All symbols are interned — the same name always maps to the same heap address, so `eq?` on symbols is pointer comparison. Pre-interned special-form symbols are declared via the X-macro `symbol_list.h` and are available as globals (`S_DEFINE`, `S_LAMBDA`, etc.) after `sym_init()`.

### Evaluator (`src/eval.h`, `src/eval.c`)

Tree-walking interpreter with proper tail-call optimization via `goto tail` (iterative dispatch loop). All R7RS special forms are handled as cases in `eval()`. Function application always goes through the tail of the loop for closures, enabling TCO.

Exception handling uses `setjmp`/`longjmp` through the `ExnHandler` chain (`current_handler` thread-local). The `SCM_PROTECT(h, body, on_exn)` macro wraps a body with a handler frame. `call/cc` captures an escape continuation backed by a heap-allocated `jmp_buf`; upward escapes work, full first-class continuations are deferred.

### Environments (`src/env.h`, `src/env.c`)

Linked list of `EnvFrame` structs (flat symbol/value arrays). `env_bind_args()` extends the closure's environment with parameter bindings on each call. `env_lookup()` raises an error on unbound variables. The global environment is `GLOBAL_ENV`.

### Module system (`src/modules.h`, `src/modules.c`)

Two kinds of modules:
1. **C extension `.so`** — exports `void curry_module_init(CurryVM *vm)`. Loaded with `dlopen`. Call `curry_define_fn` / `curry_define_val` to register bindings.
2. **Scheme `.sld` / `.scm`** — evaluated in a fresh environment; exports all top-level definitions.

Module search order: `CURRY_MODULE_PATH` env var (colon-separated), then `lib/curry/modules/`. Module names are lists of symbols mapped to filesystem paths, e.g. `(curry json)` → `curry/json.so`.

### Actor system (`src/actors.h`, `src/actors.c`)

Each actor (`T_ACTOR`) runs in a detached POSIX thread. Actors communicate exclusively via `actor_send` / `actor_receive` through per-actor `Mailbox` objects (mutex + condvar + ring buffer). The Boehm GC shared heap is thread-safe; actors do not need per-actor heaps. `current_actor` is thread-local.

Scheme primitives: `spawn`, `send!`, `receive`, `self`, `actor-alive?`.

### Sets and hash tables (`src/set.h`, `src/set.c`)

Open-addressing hash tables with 75% max load and tombstone deletion. Three comparator modes: `SET_CMP_EQ` (pointer), `SET_CMP_EQV`, `SET_CMP_EQUAL` (structural). Both `Set` and `Hashtable` use the same `val_hash` / `slot_matches` infrastructure.

### Adding a new built-in procedure

In `src/builtins.c`, write a `PrimFn` with signature `val_t fn(int argc, val_t *argv, void *ud)` and register it with `DEF("name", fn, min_args, max_args)` inside `builtins_register()`.

### Adding a new C module

1. Create `modules/<name>/<name>.c` (or `.cpp`).
2. Implement `void curry_module_init(CurryVM *vm)` calling `curry_define_fn` / `curry_define_val`.
3. Add a `curry_c_module(<name>)` (or `curry_cxx_module`) call in `CMakeLists.txt` under the appropriate `option` guard.
4. Load from Scheme with `(import (curry <name>))`.

### Graphics / GPU (`modules/qt6/qt6.cpp`)

The qt6 module layers: 2D canvas (QPainter), 3D scene (Qt3D / QRhi), and 4D projection math (implemented in pure C++, no Qt dependency). 4D→3D projection uses perspective division on the w-axis (`project_4d_to_3d`). 3D rendering uses Qt6's `QRhi` abstraction: Metal on macOS, Vulkan on Linux, D3D11 on Windows. GPU compute is exposed via `gpu-buffer-make` / `gpu-dispatch` backed by QRhi compute shaders.

### Initialization order

`gc_init() → sym_init() → num_init() → port_init() → env_init() → eval_init() → actors_init() → modules_init()`

`modules_init()` calls `builtins_register(GLOBAL_ENV)` to populate the top-level environment.

### Add readline support
