# Curry Scheme

Curry is an R7RS Scheme interpreter with a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices and tensors, an actor-model concurrency system, and a modular C extension interface.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

## Documents

### Language

- [Language reference](docs/language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](docs/symbolic.md) — CAS reference: variables, differentiation, integration, simplification, substitution, complex operators, Wirtinger calculus, auto-diff
- [Quantum superposition](docs/quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](docs/surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](docs/multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](docs/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages
- [MCP server](docs/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients; stdio and SSE transports
- [Raspberry Pi / embedded hardware](docs/RPI.md) — setup guide for Pi; GPIO, I2C, SPI, PWM with the `(curry rpi)` module

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
| Symbolic | CAS expression tree | `(symbolic x)` |

Arithmetic automatically promotes through the tower. `(+ 1/3 0.5)` → flonum. `(∂ (* x x) x)` → symbolic `(+ x x)`.

### CAS and auto-differentiation

| Procedure | Description |
|-----------|-------------|
| `(symbolic x y ...)` | Declare symbolic variables in scope |
| `(sym-var 'x)` | Create a symbolic variable object directly |
| `(sym-var? v)` / `(sym-expr? v)` / `(symbolic? v)` | Predicates |
| `(sym-var-name v)` | Variable name as string |
| `(∂ expr var)` | Symbolic differentiation (alias: `sym-diff`) |
| `(∫ expr var)` | Indefinite integration (alias: `integrate`) |
| `(∫ expr var a b)` | Definite integral from a to b |
| `(simplify expr)` | Algebraic simplification |
| `(substitute expr var val)` | Substitute and evaluate |
| `(conj expr)` / `(real-part expr)` / `(imag-part expr)` | Complex operators — symbolic-aware |
| `(wirtinger-d expr z)` | Wirtinger ∂/∂z (treats z and z̄ as independent) |
| `(wirtinger-dbar expr z)` | Wirtinger ∂/∂z̄ — zero iff expr is holomorphic |
| `(auto-diff f x)` | Numeric derivative at a point via dual-number ε |
| `(frac-diff expr α var)` | Caputo symbolic fractional derivative D^α |
| `(frac-int expr α var)` | Riemann-Liouville symbolic fractional integral I^α |
| `(quad-frac-diff f α x)` | Grünwald-Letnikov numerical D^α (for non-symbolic f) |
| `(quad-frac-int f α x)` | Numerical RL fractional integral |
| `(quad f a b)` | Gauss-Kronrod G7K15 adaptive numerical quadrature |
| `(sym->string expr)` / `(sym->infix expr)` | Infix string: `x^2 + 2*x + 1` |
| `(sym->latex expr)` | LaTeX string: `x^{2} + 2 x + 1` |

`∂` and `∫` are Unicode (U+2202, U+222B); ASCII aliases `sym-diff` and `integrate` are equivalent. All standard numeric operators lift automatically over symbolic values.

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
| [neo4j](docs/module-neo4j.md) | `(curry neo4j)` | Neo4j graph database client (Bolt 4.x/5.x, no libneo4j) | — |
| [image](docs/module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](docs/module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [qt6](docs/module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](docs/module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](docs/module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](docs/module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](docs/module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |
| [mqtt](docs/module-mqtt.md) | `(curry mqtt)` | MQTT client: publish, subscribe, QoS 0/1/2, TLS | `libpaho-mqtt-dev` |
| [ode](docs/module-ode.md) | `(curry ode)` | ODE solvers: Euler, RK4, Dormand-Prince RK45, Verlet | — |
| [mcp](docs/mcp-clients.md) | `(curry mcp)` | MCP server: expose Curry tools to AI clients via stdio or SSE | — |
| [profiling](docs/module-profiling.md) | `(curry profiling)` | Runtime call-count and wall-clock profiler for named closures and primitives | — |
| [rpi](docs/module-rpi.md) | `(curry rpi)` | GPIO, I2C, SPI, PWM for Raspberry Pi and Linux embedded boards *(Linux only)* | `libgpiod-dev` |

---

## Installation & Building

See [docs/INSTALL.md](docs/INSTALL.md) for Homebrew installation (macOS), building from source on Linux and macOS (including Qt6 and `.deb` packaging), and running the test suite.

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

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
