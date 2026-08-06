# Module index

The full list of `(curry ...)` modules. Optional modules that need an external library are gated by a `-DBUILD_MODULE_X=ON` CMake flag (most default `ON`); see each module's own doc page for its exact flag and any extra runtime dependency.

| Module | Import | Description | Extra deps |
|--------|--------|-------------|------------|
| [json](module-json.md) | `(curry json)` | JSON parse / stringify | — |
| [yaml](module-yaml.md) | `(curry yaml)` | YAML parse / stringify: anchors/aliases, merge keys, multi-doc streams *(pure Scheme, no build step)* | — |
| [toml](module-toml.md) | `(curry toml)` | TOML 1.0 parse / stringify: tables, arrays of tables, inline tables, all string/datetime forms *(pure Scheme, no build step)* | — |
| [okf](module-okf.md) | `(curry okf)` | Open Knowledge Format v0.2 bundle reader/query/writer: trust tiers, staleness, link graph, Attested Computations *(pure Scheme, no build step)* | — |
| [sqlite](module-sqlite.md) | `(curry sqlite)` | SQLite3 database | `libsqlite3-dev` |
| [network](module-network.md) | `(curry network)` | TCP / UDP sockets | — |
| [crypto](module-crypto.md) | `(curry crypto)` | base64, MD5, SHA-256, HMAC | `libssl-dev` |
| [ldap](module-ldap.md) | `(curry ldap)` | LDAP / LDAPS directory access | `libldap-dev` |
| [http](module-http.md) | `(curry http)` | General-purpose HTTP client — any method, headers, body | `libcurl4-openssl-dev` |
| [llm](module-llm.md) | `(curry llm)` | LLM client: Claude, OpenAI, Ollama, any OpenAI-compat endpoint; tool use, agentic loop *(pure Scheme, no build step)* | `(curry http)` |
| [storage](module-storage.md) | `(curry storage)` | S3, Swift, Azure Blob, GCS | `libcurl4-openssl-dev` |
| [graphql](module-graphql.md) | `(curry graphql)` | GraphQL HTTP client | `libcurl4-openssl-dev` |
| [redis](module-redis.md) | `(curry redis)` | Redis client (RESP2, no hiredis) | — |
| [neo4j](module-neo4j.md) | `(curry neo4j)` | Neo4j graph database client (Bolt 4.x/5.x, no libneo4j) | — |
| [image](module-image.md) | `(curry image)` | PNG / JPEG / GIF load, save, edit | `libpng-dev libjpeg-dev` |
| [git](module-git.md) | `(curry git)` | Git repository access | `libgit2-dev` |
| [qt6](module-qt6.md) | `(curry qt6)` | Qt6 windows, canvas, widgets, 4D math | Qt6 |
| [plplot](module-plplot.md) | `(curry plplot)` | Scientific 2D/3D plotting | `libplplot-dev` |
| [vecdb](module-vecdb.md) | `(curry vecdb)` | Vector nearest-neighbour search | — |
| [regex](module-regex.md) | `(curry regex)` | POSIX extended regular expressions | — |
| [sync](module-sync.md) | `(curry sync)` | Mutex, condition variable, semaphore | — |
| [stm](concurrency.md) | `(curry stm)` | STM (`atomically`/`retry`/`or-else`), CSP channels, `select` macro | — |
| [conditions](language.md#condition-system-cl-style----import-curry-conditions) | `(curry conditions)` | CL condition system: `handler-bind`, `with-restarts`, `invoke-restart`, `handler-case` | — |
| [ffi](module-ffi.md) | `(curry ffi)` | General C FFI: `define-foreign`, zero-copy matrix/tensor passthrough | `libffi-dev` (`-DBUILD_FFI=ON`) |
| [mqtt](module-mqtt.md) | `(curry mqtt)` | MQTT client: publish, subscribe, QoS 0/1/2, TLS | `libpaho-mqtt-dev` |
| [ode](module-ode.md) | `(curry ode)` | ODE solvers: Euler, RK4, Dormand-Prince RK45, Verlet | — |
| [mcp](../guides/mcp-clients.md) | `(curry mcp)` | MCP server: expose Curry tools to AI clients via stdio or SSE | — |
| [lsp](module-lsp.md) | `(curry lsp)` | Language Server Protocol server: diagnostics, hover, completion for any LSP editor | — |
| [profiling](profiling.md) | `(curry profiling)` | Runtime call-count and wall-clock profiler for named closures and primitives | — |
| [rpi](module-rpi.md) | `(curry rpi)` | GPIO, I2C, SPI, PWM for Raspberry Pi and Linux embedded boards *(Linux only)* | `libgpiod-dev` |
| [sicm](module-sicm.md) | `(curry sicm)` | Classical mechanics (SICM): Lagrangian, Hamiltonian, Poisson brackets | — |
| [fits](module-fits.md) | `(curry fits)` | FITS scientific image read/write *(pure Scheme, no build step)* | — |
| [netcdf](module-netcdf.md) | `(curry netcdf)` | NetCDF classic format reader *(pure Scheme, no build step)* | — |
| [hdf5](module-hdf5.md) | `(curry hdf5)` | HDF5 dataset/attribute read/write via FFI *(pure Scheme + FFI, no build step)* | `libhdf5` installed at runtime (`-DBUILD_FFI=ON`) |
| [posix](module-posix.md) | `(curry posix)` | Filesystem/process POSIX bindings (SRFI-170 subset): `file-info`, directories, symlinks, uid/gid, `umask`, users/groups | — |
| [codesets](module-codesets.md) | `(curry codesets)` | SRFI-238 codesets: `errno`/`signal`/`http-status` symbol ⟷ number ⟷ message lookup | — |
| [aviation-weather](module-aviation-weather.md) | `(curry aviation-weather)` | METAR / TAF / ATIS aviation weather report parsing *(pure Scheme, no build step)* | — |
| [base64](module-base64.md) | `(curry base64)` | RFC 4648 base64 encode/decode: string, bytevector, and streaming port forms *(pure Scheme, no build step)* | — |
| [naips](module-naips.md) | `(curry naips)` | Airservices Australia NAIPS briefing-service client (loc/area/met/notam briefing) *(pure Scheme, no build step)* | `(curry http)` |
| [babylonian-astronomy](module-babylonian-astronomy.md) | `(curry babylonian-astronomy)` | Babylonian mathematical astronomy: System-A zigzag function, synodic month/Saros eclipse cycle, civil calendar month names *(pure Scheme, no build step)* | — |

## See also

- [`srfi/index.md`](srfi/index.md) — portable `(srfi sN name)` compatibility libraries
- [`writing-a-module.md`](writing-a-module.md) — how to wrap a pure-Scheme `(curry X)` module in `define-library`, including the macro-export gotcha
