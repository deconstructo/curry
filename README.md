# Curry Scheme

Curry is an R7RS Scheme interpreter with a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices and tensors, an actor-model concurrency system, and a modular C extension interface.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

## Documents

### Language

- [Language reference](docs/language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](docs/symbolic.md) — CAS reference: variables, differentiation, simplification, substitution
- [Quantum superposition](docs/quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](docs/surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](docs/multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](docs/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages

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
| Symbolic | CAS expression tree | `(sym-var 'x)` |

Arithmetic automatically promotes through the tower. `(+ 1/3 0.5)` → flonum. `(∂ (* x x) x)` → symbolic `(+ x x)`.

### CAS and auto-differentiation

| Procedure | Description |
|-----------|-------------|
| `(sym-var 'x)` | Declare a symbolic variable |
| `(∂ expr var)` | Symbolic differentiation |
| `(simplify expr)` | Algebraic simplification |
| `(substitute expr var val)` | Substitute and evaluate |
| `(auto-diff f x)` | Exact numeric derivative via dual numbers (ε) |

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
| [image](docs/module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](docs/module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [ui](docs/module-ui.md) | `(curry ui)` | GTK4 windows, menus, canvas, widgets | `libgtk-4-dev` |
| [qt6](docs/module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](docs/module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](docs/module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](docs/module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](docs/module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |

---

## Building on Linux (Debian / Ubuntu)

### 1. Install dependencies

```bash
# Required
sudo apt install libgc-dev libgmp-dev cmake build-essential

# Optional modules
sudo apt install libsqlite3-dev        # sqlite
sudo apt install libssl-dev            # crypto
sudo apt install libcurl4-openssl-dev  # storage, graphql
sudo apt install libldap-dev           # ldap
sudo apt install libpng-dev libjpeg-dev # image
sudo apt install libgit2-dev           # git
sudo apt install libgtk-4-dev          # ui (GTK4)
sudo apt install libplplot-dev         # plplot
```

### 2. Build

```bash
# Minimal build (core + always-on modules: json, network, redis, regex, sync, vecdb)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Full build with all optional modules
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_CRYPTO=ON \
  -DBUILD_MODULE_LDAP=ON \
  -DBUILD_MODULE_STORAGE=ON \
  -DBUILD_MODULE_GRAPHQL=ON \
  -DBUILD_MODULE_UI=ON \
  -DBUILD_MODULE_IMAGE=ON \
  -DBUILD_MODULE_GIT=ON \
  -DBUILD_MODULE_PLPLOT=ON \
  -DBUILD_MODULE_QT6=ON
cmake --build build -j$(nproc)
```

### 3. Run

```bash
./build/curry                          # REPL
./build/curry script.scm               # run a script
./build/curry -e '(display "𒀭") (newline)'
```

---

## Building on macOS

### 1. Install Xcode command-line tools

```bash
xcode-select --install
```

### 2. Install dependencies via Homebrew

```bash
# Required
brew install bdw-gc gmp cmake

# Optional modules
brew install openssl      # crypto
brew install sqlite       # sqlite
brew install libgit2      # git
brew install libpng jpeg-turbo  # image
brew install plplot             # plplot
# curl and ldap are bundled with macOS — no extra install needed
```

### 3. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.logicalcpu)
./build/curry
```

### 4. Optional: Qt6 module on macOS

```bash
brew install qt@6

# Qt6 from Homebrew is not on PATH by default — point CMake to it:
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

> Qt6 on macOS uses Metal for rendering. `QOpenGLWidget` is bridged through Apple's OpenGL compatibility layer; expect deprecation warnings at runtime but the module works correctly.

### Notes

- Modules build as `.so` bundles on both Linux and macOS; `(import (curry qt6))` works identically on both platforms.
- Apple Silicon and x86_64 are both supported natively.

---

## Testing

```bash
cmake --build build && ctest --test-dir build -V
```

The test suite comprises four suites (262 tests total):

| Suite | File | What it covers |
|-------|------|----------------|
| `core` | `tests/test_core.c` | C-level: value representation, numeric tower, lists, strings, TCO, closures, continuations, sets, hash tables, records, exceptions |
| `scheme_r7rs` | `tests/r7rs_tests.scm` | R7RS conformance: all standard types, arithmetic, rounding, string/char ops, I/O ports, error objects, apply, fold, predicates, bytevectors, sets, hash tables |
| `numeric_ext` | `tests/numeric_ext_tests.scm` | Clifford algebra (Cl(3,0,0) basis blades, geometric product, wedge, grade, reverse, norms), symbolic CAS (∂, simplify, substitute), surreal numbers (ω, ε, arithmetic), auto-differentiation |
| `actors` | `tests/actors_tests.scm` | Actor spawn/alive?/send!, semaphore-coordinated result passing, mutex-protected shared state, condvar signal/wait |

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
