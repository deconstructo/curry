# Installation & Building

## Install via Homebrew (macOS)

The fastest way to get Curry on macOS — no manual dependency wrangling required:

```bash
brew tap deconstructo/curry https://github.com/deconstructo/curry
brew install curry
```

This installs the `curry` binary with the following modules pre-built: json, network, redis, regex, sync, mcp, sqlite, crypto, ldap, storage (S3/GCS/Azure), image (PNG/JPEG), and git.

Optional modules can be enabled at install time with `--with-*` flags:

```bash
brew install deconstructo/curry/curry --with-qt6       # Qt6 GUI + canvas
brew install deconstructo/curry/curry --with-plplot    # scientific plotting
brew install deconstructo/curry/curry --with-symengine # SymEngine CAS backend
brew install deconstructo/curry/curry --with-neo4j     # Neo4j client
brew install deconstructo/curry/curry --with-graphql   # GraphQL HTTP client

# All of the above at once
brew install deconstructo/curry/curry \
  --with-qt6 --with-plplot --with-symengine --with-neo4j --with-graphql
```

To install the latest development build straight from the `main` branch:

```bash
brew install --HEAD deconstructo/curry/curry
brew install --HEAD deconstructo/curry/curry --with-qt6  # HEAD + Qt6
```

The formula lives at [`Formula/curry.rb`](../Formula/curry.rb) in this repository, so you can also install directly from a local clone:

```bash
brew install --formula Formula/curry.rb
```

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
  -DBUILD_MODULE_IMAGE=ON \
  -DBUILD_MODULE_GIT=ON \
  -DBUILD_MODULE_PLPLOT=ON \
  -DBUILD_MODULE_MQTT=ON \  
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

### 5. Create a .deb package

Build in Release mode, then run CPack from the build directory:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(nproc)
cd build-release && cpack -G DEB
```

This produces `curry-scheme_0.7.5_arm64.deb` (architecture auto-detected). Install it with:

```bash
sudo dpkg -i curry-scheme_0.7.5_arm64.deb
```

The package installs to standard system paths:

| Path | Contents |
|------|----------|
| `/usr/bin/curry` | Interpreter |
| `/usr/lib/libcurry_core.a` | Embedding library |
| `/usr/include/curry/curry.h` | Public C API header |
| `/usr/lib/curry/modules/curry/` | Extension modules (`.so` + `.scm`) |
| `/usr/share/doc/curry/` | All documentation (Markdown + PDF) |

Runtime dependencies (`libgc`, `libgmp`) are declared as `Depends`; the optional module libraries (SQLite, OpenSSL, libcurl, etc.) appear as `Recommends`.

Optional modules can be enabled at configure time before packaging — any module whose library is present will be built and included in the package automatically.

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
