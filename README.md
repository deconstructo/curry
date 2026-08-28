# Curry Scheme

Curry is an R7RS Scheme implementation with practical R6RS compatibility, a numeric tower extending through the hypercomplex numbers into Clifford algebra, a built-in computer algebra system, quantum superposition values, first-class matrices, tensors, and spinors, a CL-style condition system with restarts, a general C FFI, STM and CSP channels alongside the actor-model concurrency system, a modular C extension interface, and a built-in LLM client that can talk to Claude, GPT-4o, Ollama, or any OpenAI-compatible endpoint — with multi-turn conversation, tool use, and a full agentic loop.

Source is compiled to bytecode and executed on a stack-based VM. When built with LLVM (`-DBUILD_LLVM=ON`), hot closures are automatically compiled to native machine code after 50 calls via an ORC v2 JIT backend. Compiled chunks are cached in `.scc` files (source-adjacent, or `~/.cache/curry/` for read-only paths) and reused on subsequent runs, invalidated automatically on source change or version bump. Use `-c file.scm` to pre-compile without running; `.scc` files can also be passed directly as the script argument.

Error messages are rendered in Standard Babylonian Akkadian with cuneiform script (𒀭 ḫiṭītu — *great fault*), as scribal tradition demands.

Built around pluggable logics, an open-ended numeric tower, and no single "correct" answer treated as bedrock.

## Documents

Documentation is split into two directories:

- **[`docs/reference/`](docs/reference/)** — language specification, numeric tower, CAS, module APIs
- **[`docs/guides/`](docs/guides/)** — installation, tutorials, how-tos, worked examples

### Language reference

- [Language reference](docs/reference/language.md) — syntax, types, special forms, numeric tower, symbolic math, quantum values, Akkadian syntax, actors, module system
- [Symbolic expressions](docs/reference/symbolic.md) — CAS reference: variables, differentiation, integration, simplification, substitution, complex operators, Wirtinger calculus, auto-diff
- [Quantum superposition](docs/reference/quantum.md) — quantum value type: construction, observation, arithmetic
- [Surreal numbers](docs/reference/surreal.md) — Hahn-series surreals: ω, ε, exact infinitesimals, auto-diff
- [Multivectors](docs/reference/multivec.md) — Clifford algebra Cl(p,q,r): geometric product, rotors, PGA, CGA
- [Akkadian / Cuneiform reference](docs/reference/akkadian-reference.md) — complete vocabulary of special forms and procedures in all three languages, plus the runtime error-message phrase table
- [Error codes](docs/reference/error-codes.md) — stable machine-legible `error-object-code`/`condition-code` registry for tooling
- [Module index](docs/reference/modules.md) — the full list of `(curry ...)` modules, import names, and extra build dependencies
- [SRFI compatibility](docs/reference/srfi/index.md) — the 39 portable `(srfi sN name)` libraries, one page each
- [Parallel map/reduce](docs/reference/parallel.md) — the work-stealing thread pool behind `map`/`reduce`
- [LLVM JIT backend](docs/reference/llvm-jit.md) — auto-JIT, benchmark numbers, build flags
- [Garbage collector reference](docs/reference/gc.md) — the two GC backends and `(gc-stats)` fields

### Guides

- [Installation](docs/guides/INSTALL.md) — Homebrew (macOS), build from source, Qt6, test suite
- [LLM integration](docs/guides/guide-llm.md) — talking to Claude, OpenAI, and Ollama; tool use, agentic loops, structured output, MCP servers, CAS-backed AI
- [Raspberry Pi / embedded hardware](docs/guides/RPI.md) — setup guide for Pi; GPIO, I2C, SPI, PWM
- [MCP server](docs/guides/mcp-clients.md) — expose Curry procedures as Model Context Protocol tools callable from Claude Code and other AI clients
- [macOS app bundler](docs/guides/make-macos-app.md) — bundle any Curry script as a `.app` with Qt frameworks embedded
- [Monitoring guide](docs/guides/guide-monitoring.md) — using `(gc-stats)`, running the Grafana stack (Docker and Apple Containers), customizing dashboards, publishing custom metrics
- [Text-to-speech with Piper](docs/guides/tts-piper.md) — building `libpiper` and curry against it on macOS/Linux, speaking/saving audio via `(curry tts)`, sourcing and adding voice models
- [Simulating cell biochemistry with Gillespie](docs/guides/gillespie-cells.md) — stochastic reaction networks, composable temperature/pH/nutrient-sensitive rate laws, multi-cell simulation via actors
- [Benchmarking reference](docs/reference/benchmarking.md) — bench suites, MQTT event schema, all GC stat fields
- [Profiling reference](docs/reference/profiling.md) — `**eval-profiler**`, `(curry profiling)` API, `,profile` REPL command, timing workflows

## Features

The numeric tower, CAS, quantum values, parallel map/reduce, modules, SRFI
compatibility, LLM/AI integration, the LLVM JIT backend, C FFI, the garbage
collector, and Akkadian error messages all live in
**[FEATURES.md](FEATURES.md)**.

---

## Installation & Building

See [docs/guides/INSTALL.md](docs/guides/INSTALL.md) for Homebrew installation (macOS), building from source on Linux and macOS (including Qt6 and `.deb`/`.rpm` packaging), and running the test suite.

---

## AI and development: my philosophy

Yes, I do use Claude for development. My apology is this:

* I can code, but I am slow - very slow. I make use of Claude to develop ideas quickly. For me, what is more interesting and important is exploring the ideas that I incorporate into Curry, than actually writing the code itself. 

* I treat Claude more as a dialogue partner than as just a simple tool. I discuss ideas that I have. I discuss things that I find in papers or in other lisp or scheme implementations and work through what goes in, what remains on the table, and what is pulled out and/or replaced. 

* One of the freedoms of this mode of development is that I see that if even a rather large change is required to get to a better place - in terms of stability, performance, flexibility or features - then I am _happy_ to make that change! Evidence are the changes from a tree-walking interpreter to VM to JIT compiler, or the work that I've done (and backed out) on improving garbage collection.

* AI may do the coding and a lot of the documentation, but **I am responsible** for bugs, errors, omissions in the code and in the documentation. If there's something wrong, I will do my best to fix it.

## Get involved!

I'd love to work with others on this, dare I say rather unique language. I'd also like to be useful for others. So Feel free to drop issues in or PRs.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
