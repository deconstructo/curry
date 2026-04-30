# Curry Scheme Documentation

Curry is an R7RS Scheme interpreter with a numeric tower extending through the hypercomplex numbers, a built-in computer algebra system, quantum superposition values, an actor-model concurrency system, and a modular C extension interface.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands. Special forms and procedures are also available under their Akkadian names and in Unicode cuneiform.

## Documents

### Language

- [Language reference](language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](symbolic.md) — CAS reference: variables, differentiation, simplification, substitution
- [Quantum superposition](quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages

### Extended numeric tower

| Feature | Entry point | Description |
|---------|-------------|-------------|
| Symbolic CAS | `(symbolic x y z)` | Declare variables; arithmetic over them builds expression trees |
| Differentiation | `(∂ expr var)` | Symbolic differentiation with simplification |
| Substitution | `(substitute expr var val)` | Replace variable, evaluate, simplify |
| Quantum values | `(quantum-uniform lst)` | Uniform superposition over a list |
| Quantum values | `(superpose alist)` | Explicit amplitude superposition |
| Quantum collapse | `(observe q)` | Probabilistic collapse to one branch |
| Surreal numbers | `omega`, `epsilon` | First infinite and infinitesimal surreals |
| Auto-diff | `(auto-diff f x)` | Exact numeric derivative via ε |
| Multivectors | `(make-mv p q r)` | Clifford algebra Cl(p,q,r): rotors, spinors, PGA motors |

### Modules

| Module | Import | Description | Requires |
|--------|--------|-------------|----------|
| [json](module-json.md) | `(curry json)` | JSON parse / stringify | — |
| [sqlite](module-sqlite.md) | `(curry sqlite)` | SQLite3 database | `libsqlite3-dev` |
| [network](module-network.md) | `(curry network)` | TCP / UDP sockets | — |
| [crypto](module-crypto.md) | `(curry crypto)` | base64, MD5, SHA-256, HMAC | `libssl-dev` |
| [ldap](module-ldap.md) | `(curry ldap)` | LDAP / LDAPS directory access | `libldap-dev` |
| [ui](module-ui.md) | `(curry ui)` | GTK4 windows, canvas, widgets | `libgtk-4-dev` |
| [storage](module-storage.md) | `(curry storage)` | S3, Swift, Azure Blob | `libcurl4-openssl-dev` |
| [graphql](module-graphql.md) | `(curry graphql)` | GraphQL client | `libcurl4-openssl-dev` |
| [redis](module-redis.md) | `(curry redis)` | Redis client (RESP2, no hiredis) | — |
| [image](module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [vecdb](module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [qt6](module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | `qt6-base-dev` |
| [plplot](module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [regex](module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |

## Quick install (Debian/Ubuntu)

```bash
# Core
sudo apt install libgc-dev libgmp-dev cmake build-essential

# Modules
sudo apt install libsqlite3-dev libssl-dev libldap-dev \
                 libgtk-4-dev libcurl4-openssl-dev \
                 libpng-dev libjpeg-dev libgit2-dev \
                 libplplot-dev
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_MODULE_UI=ON \
  -DBUILD_MODULE_STORAGE=ON \
  -DBUILD_MODULE_GRAPHQL=ON \
  -DBUILD_MODULE_VECDB=ON
cmake --build build -j$(nproc)

./build/curry                          # REPL
./build/curry script.scm               # run a script
./build/curry -e '(display "𒀭") (newline)'
ctest --test-dir build -V              # run tests
```

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
