# Installation & Building

*v1.23.7 — 2026-09-09*

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
brew install deconstructo/curry/curry --with-neo4j     # Neo4j client
brew install deconstructo/curry/curry --with-graphql   # GraphQL HTTP client

# All of the above at once
brew install deconstructo/curry/curry \
  --with-qt6 --with-plplot --with-neo4j --with-graphql
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
# Required — note OpenSSL: the network module (on by default, TLS sockets)
# links it unconditionally, so CMake's top-level configure fails without it
# even if you don't touch any module flags.
sudo apt install libgc-dev libgmp-dev libssl-dev cmake build-essential

# Modules that are ON by default need no extra package beyond the above —
# json, network, redis, sqlite, regex, sync, mcp, lsp, profiling, posix,
# codesets, f64vector, typedvec, and neo4j all build against system
# libc/libssl/libsqlite3 alone. (vecdb is OFF by default but also needs
# no extra package when enabled — see the table below.)
sudo apt install libsqlite3-dev        # sqlite (on by default)

# Everything else optional — install only what you plan to enable:
sudo apt install libcurl4-openssl-dev  # http, storage, graphql (all on by default)
sudo apt install libpng-dev libjpeg-dev # image (on by default)
sudo apt install libgit2-dev           # git (on by default)
sudo apt install libplplot-dev         # plplot (on by default)
sudo apt install libpaho-mqtt-dev      # mqtt (on by default)
sudo apt install libldap-dev           # ldap (off by default)
sudo apt install libffi-dev            # general FFI, BUILD_FFI=ON (off by default)
sudo apt install llvm-18-dev           # LLVM JIT backend, BUILD_LLVM=ON (off by default) — see below
sudo apt install libgpiod-dev          # rpi (Linux only, off by default) — see docs/guides/RPI.md
# qt6: see docs/reference/module-qt6.md — needs a full Qt6 dev install (off by default)
# piper: no apt package yet — build libpiper from source first, see docs/reference/module-piper.md (off by default)
```

### 2. Build options at a glance

| Module (`-DBUILD_MODULE_...=`) | Default | Extra package needed |
|---|---|---|
| `F64VECTOR`, `TYPEDVEC`, `JSON`, `SQLITE`, `NETWORK`, `CRYPTO`, `STORAGE`, `HTTP`, `GRAPHQL`, `NEO4J`, `REDIS`, `IMAGE`, `GIT`, `PLPLOT`, `REGEX`, `SYNC`, `MCP`, `LSP`, `PROFILING`, `POSIX`, `CODESETS` | ON | see table above (most need nothing beyond required deps) |
| `MQTT` | ON | `libpaho-mqtt-dev` |
| `LDAP` | OFF | `libldap-dev` |
| `VECDB` | OFF | none (optional `usearch` for approximate search — see `docs/reference/module-vecdb.md`) |
| `QT6` | OFF | Qt6 dev packages — see `docs/reference/module-qt6.md` |
| `PIPER` | OFF | manual `libpiper`/`onnxruntime` build — see `docs/reference/module-piper.md` |
| `RPI` | OFF (no default value — must pass `=ON` explicitly), Linux only | `libgpiod-dev` — see `docs/guides/RPI.md` |

Plus two build-wide flags that aren't modules: `-DBUILD_FFI=ON` (general C FFI, `libffi-dev`) and `-DBUILD_LLVM=ON` (tiered native JIT backend, LLVM ≥ 15 — see `docs/reference/llvm-jit.md` and the LLVM dependency notes above).

### 3. Build

```bash
# Minimal build — every default-ON module above, using just the "Required" packages
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Full build — every module including the default-OFF ones (excluding piper/rpi,
# which need extra manual setup — see the table above)
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_LDAP=ON \
  -DBUILD_MODULE_VECDB=ON \
  -DBUILD_MODULE_QT6=ON \
  -DBUILD_FFI=ON \
  -DBUILD_LLVM=ON
cmake --build build -j$(nproc)
```

### 4. Run

```bash
./build/curry                          # REPL
./build/curry script.scm               # run a script
./build/curry -e '(display "𒀭") (newline)'
```

---

## Building on Linux (Fedora / RHEL)

Package names differ from Debian's (`gc-devel` vs. `libgc-dev`, etc.), but the CMake invocation itself is identical to the Debian/Ubuntu section above — only the package-manager step changes.

### 1. Install dependencies

```bash
# Required — OpenSSL too: the network module (on by default, TLS sockets)
# links it unconditionally, so CMake's top-level configure fails without it
# even if you don't touch any module flags.
sudo dnf install gc-devel gmp-devel openssl-devel cmake gcc gcc-c++ make pkgconfig

# sqlite (on by default)
sudo dnf install sqlite-devel

# Everything else optional — install only what you plan to enable:
sudo dnf install libcurl-devel                # http, storage, graphql (all on by default)
sudo dnf install libpng-devel libjpeg-turbo-devel # image (on by default)
sudo dnf install libgit2-devel                # git (on by default)
sudo dnf install plplot-devel                 # plplot (on by default)
sudo dnf install paho-c-devel                 # mqtt (on by default; package name may vary by release — build from https://github.com/eclipse/paho.mqtt.c if unavailable)
sudo dnf install openldap-devel                # ldap (off by default)
sudo dnf install libffi-devel                 # general FFI, BUILD_FFI=ON (off by default)
sudo dnf install llvm-devel llvm-static        # LLVM JIT backend, BUILD_LLVM=ON (off by default)
sudo dnf install libgpiod-devel               # rpi (Linux only, off by default) — see docs/guides/RPI.md
# qt6: sudo dnf install qt6-qtbase-devel qt6-qtbase-gui — see docs/reference/module-qt6.md (off by default)
# piper: no dnf package yet — build libpiper from source first, see docs/reference/module-piper.md (off by default)
```

See the module-flags table in the Debian/Ubuntu section above — it applies here too, just with `dnf` package names instead of `apt` ones.

### 2. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -j$(nproc)
```

### 3. Native arm64 verification via container

On an Apple Silicon Mac with Docker Desktop (or `podman`/the `container` CLI), a real Fedora build can be sanity-checked without a physical machine — this pulls and runs a native arm64 Fedora image (not cross-compilation) and is a useful pre-CI check since project CI only covers `ubuntu-latest`/`macos-latest`, not Fedora:

```bash
docker run --rm --platform linux/arm64 -v "$(pwd)":/src:ro fedora:latest bash -c '
  set -ex
  dnf install -y gc-devel gmp-devel openssl-devel cmake gcc gcc-c++ make pkgconfig \
    sqlite-devel libcurl-devel \
    libpng-devel libjpeg-turbo-devel libgit2-devel plplot-devel redis mosquitto git
  cp -r /src /work && cd /work
  cmake -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j$(nproc)
  ctest --test-dir build --output-on-failure -j$(nproc)
'
```

---

## Building on macOS

### 1. Install Xcode command-line tools

```bash
xcode-select --install
```

### 2. Install dependencies via Homebrew

```bash
# Required — OpenSSL too: the network module (on by default, TLS sockets)
# links it unconditionally, so CMake's top-level configure fails without it
# even if you don't touch any module flags.
brew install bdw-gc gmp openssl cmake

# Modules that are ON by default need no extra package beyond the above —
# json, network, redis, sqlite, regex, sync, mcp, lsp, profiling, posix,
# codesets, f64vector, typedvec, and neo4j all build against system
# libc/openssl/sqlite alone. (vecdb is OFF by default but also needs no
# extra package when enabled — see the table above.)
brew install sqlite       # sqlite (on by default)

# Everything else optional — install only what you plan to enable:
brew install libgit2            # git (on by default)
brew install libpng jpeg-turbo  # image (on by default)
brew install plplot             # plplot (on by default)
brew install libpaho-mqtt       # mqtt (on by default)
brew install openldap           # ldap (off by default)
brew install libffi             # general FFI, BUILD_FFI=ON (off by default)
brew install llvm               # LLVM JIT backend, BUILD_LLVM=ON (off by default) — see below
# curl is bundled with macOS — no extra install needed for http/storage/graphql
# qt6: see "Optional: Qt6 module on macOS" below (off by default)
# piper: no Homebrew formula yet — build libpiper from source first, see docs/reference/module-piper.md (off by default)
```

See the module-flags table in the Linux section above for which `-DBUILD_MODULE_...` flag maps to which package — it applies here too, just with Homebrew package names instead of apt ones.

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

> `brew --prefix qt@6` resolves to the umbrella `qt` formula, which doesn't actually ship `Qt6Config.cmake` (only its `qtbase` dependency does) — `CMakeLists.txt` falls back to `brew --prefix qtbase` automatically on macOS if the first `find_package(Qt6 …)` misses, so the command above still works. The module also bakes in Homebrew's Qt plugin directory at build time, so no `QT_QPA_PLATFORM_PLUGIN_PATH` env var is needed to run it.

### 5. Optional: LLVM JIT backend on macOS

```bash
brew install llvm
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_LLVM=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Homebrew's `llvm` formula is keg-only (not linked into `/usr/local` or `/opt/homebrew` by default), so `-DCMAKE_PREFIX_PATH` is needed the same way it is for Qt6. See `docs/reference/llvm-jit.md` for what the JIT backend actually does at runtime.

### Notes

- Modules build as `.so` bundles on both Linux and macOS; `(import (curry qt6))` works identically on both platforms.
- Apple Silicon and x86_64 are both supported natively.
- Apple Clang always preserves frame pointers (required for Instruments), so the Boehm GC stack-walk issue described below does not apply on macOS.

### Compiler notes (Linux)

GCC and upstream LLVM Clang are both supported. When building a Release build with Clang, the CMake configuration automatically adds `-fno-omit-frame-pointer`. This is required because Boehm GC's conservative stack scanner walks frame-pointer chains to find live heap objects; Clang at `-O2` omits them by default, which breaks the GC and causes segfaults. GCC is unaffected.

### 6. Create a .deb or .rpm package

CPack can produce both a Debian (`.deb`) and an RPM (`.rpm`) package from the same build. Build in Release mode first, then invoke CPack from the build directory:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(nproc)
cd build-release
```

**Debian/Ubuntu — build both formats at once:**

```bash
cpack                    # produces .deb and .rpm
```

**Build only one format:**

```bash
cpack -G DEB             # Debian/Ubuntu
cpack -G RPM             # Fedora/RHEL/openSUSE
```

This produces files named after the project version and host architecture, e.g.:

- `curry-scheme_1.3.0_amd64.deb`
- `curry-scheme-1.3.0-1.x86_64.rpm`

**Install:**

```bash
# Debian/Ubuntu
sudo dpkg -i curry-scheme_*.deb

# Fedora/RHEL
sudo rpm -i curry-scheme-*.rpm
# or
sudo dnf install ./curry-scheme-*.rpm   # resolves dependencies automatically
```

Both packages install to standard system paths:

| Path | Contents |
|------|----------|
| `/usr/bin/curry` | Interpreter |
| `/usr/lib/libcurry_core.a` | Embedding library |
| `/usr/include/curry/curry.h` | Public C API header |
| `/usr/lib/curry/modules/curry/` | Extension modules (`.so` + `.scm`) |
| `/usr/share/doc/curry/` | All documentation (Markdown + PDF) |

**Runtime dependencies:**

| Package type | Hard deps | Recommended | Suggested |
|---|---|---|---|
| `.deb` | `libgc2`, `libgmp10`, `libreadline8`, `libsqlite3-0` | OpenSSL, libcurl, libgit2, libpng, libjpeg, paho-c, libldap, libhdf5 | Qt6, PLplot |
| `.rpm` | `gc >= 8.0`, `gmp >= 6.0`, `readline >= 8.0`, `sqlite-libs` | openssl-libs, libcurl, libgit2, libpng, libjpeg-turbo, paho-c, openldap, hdf5 | qt6-qtbase, plplot |

Optional modules can be enabled at configure time before packaging — any module whose library is present will be built and included in the package automatically. `libhdf5` is the one exception: `(curry hdf5)` is pure Scheme + FFI and `dlopen`s it at runtime rather than linking at build time, so it's recommended regardless of build configuration — `(curry fits)` and `(curry netcdf)` need no extra library at all.

---

## Testing

```bash
cmake --build build && ctest --test-dir build -V
```

The test suite comprises multiple suites (500+ tests):

| Suite | File | What it covers |
|-------|------|----------------|
| `core` | `tests/test_core.c` | C-level: value representation, numeric tower, lists, strings, TCO, closures, continuations, sets, hash tables, records, exceptions |
| `scheme_r7rs` | `tests/r7rs_tests.scm` | R7RS conformance: all standard types, arithmetic, rounding, string/char ops, I/O ports, error objects, apply, fold, predicates, bytevectors, sets, hash tables |
| `numeric_ext` | `tests/numeric_ext_tests.scm` | Clifford algebra (Cl(3,0,0) basis blades, geometric product, wedge, grade, reverse, norms), symbolic CAS (∂, simplify, substitute), surreal numbers (ω, ε, arithmetic), auto-differentiation |
| `actors` | `tests/actors_tests.scm` | Actor spawn/alive?/send!, semaphore-coordinated result passing, mutex-protected shared state, condvar signal/wait |
| `dynamic_wind` | `tests/dynamic_wind_tests.scm` | `dynamic-wind`, `parameterize`, `make-parameter`, `call/cc` interaction, nested parameterize, converter callbacks |
