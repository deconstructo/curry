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
- [MCP server](docs/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients; stdio and SSE transports

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
| [qt6](docs/module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](docs/module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](docs/module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](docs/module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](docs/module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |
| [mqtt](docs/module-mqtt.md) | `(curry mqtt)` | MQTT client: publish, subscribe, QoS 0/1/2, TLS | `libpaho-mqtt-dev` |
| [mcp](docs/mcp-clients.md) | `(curry mcp)` | MCP server: expose Curry tools to AI clients via stdio or SSE | — |

---

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

The formula lives at [`Formula/curry.rb`](Formula/curry.rb) in this repository, so you can also install directly from a local clone:

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

### 4. Create a .deb package

Build in Release mode, then run CPack from the build directory:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(nproc)
cd build-release && cpack -G DEB
```

This produces `curry-scheme_0.7.3_arm64.deb` (architecture auto-detected). Install it with:

```bash
sudo dpkg -i curry-scheme_0.7.3_arm64.deb
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
