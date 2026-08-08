# `curry pkg` — Design Evaluation and Recommendation

> **Superseded** by
> [`docs/thoughts/package-management-design.md`](../thoughts/package-management-design.md),
> which inherits every conclusion below except §4 (C Extension Handling) —
> that one is deliberately reversed there in favor of an FFI-first native-
> capability default. Kept here as historical background/prior art, not
> deleted or moved.

## Context

Curry is a Scheme interpreter with a C core, optional C extension modules (compiled as `.so`), and a growing standard library of pure-Scheme modules. Its immediate user base is small; its ambition (SICM mechanics, hardware control, symbolic CAS) attracts users who care about correctness and reproducibility more than ecosystem breadth. The package manager needs to be simple enough for one person to operate initially and correct enough to not need redesigning when the ecosystem grows.

---

## 1. Registry Model

### The candidates

**Centralized upload registry** (PyPI, crates.io): Authors upload tarballs to a hosted service. The registry stores the artifacts directly. High availability — packages survive even if the author's GitHub disappears.

*Problems:* Requires server infrastructure to build and maintain. Costs money. Becomes a single point of failure and attack surface. For a solo-maintained language, this is a significant operational burden before a single package exists.

**Git-index pointing to external sources** (Julia General, Homebrew, Akku): A public git repository contains only metadata — name, version, URL, checksum. The actual source lives wherever the author hosts it (GitHub, elsewhere). Installing a package fetches from the source URL and verifies the checksum.

*Problems:* Packages can disappear if the author deletes their repo. The index PR workflow is slower than `cargo publish`. No canonical place to browse package contents.

**Pure URL installs, no central registry** (Go modules pre-proxy): Packages are addressed by URL directly. No registry at all.

*Problems:* No discoverability. No version resolution. This is the "just clone it yourself" approach dressed up slightly.

**CHICKEN eggs hybrid**: A git repository serves as both index and source — eggs are committed directly into the egg repository. No external URLs. Maximum availability; authors give up hosting control.

*Problems:* Doesn't scale well (the repository grows with every package version ever published). The CHICKEN egg repository has become unwieldy.

### Assessment

The git-index model is the right choice for Curry now and for the foreseeable future. The operational burden of a hosted registry is not justified until the ecosystem has dozens of active packages and contributors — at which point migrating to a centralized model is straightforward because the index format is just metadata. Julia made this choice from day one and it has scaled comfortably to thousands of packages. The checksum in the index is the integrity guarantee; the git history of the index is the audit log. The trust anchor is the index repository commit hash, which is cryptographically sound without requiring any server infrastructure.

The one real risk — packages disappearing because authors delete their repos — can be mitigated later with a mirroring service (Homebrew does this with `homebrew-bottles`). It is not a reason to build infrastructure prematurely.

---

## 2. Lock Files

### The candidates

**No lock files** (Quicklisp, Homebrew): Install whatever the current registry says is latest for each constraint. Fast, simple, zero cognitive overhead. Quicklisp sidesteps this entirely by releasing monthly "dists" — a coherent snapshot of compatible package versions — rather than resolving constraints at install time.

*Problems:* `curry pkg install` on a project three months later produces a different result than it did today. For physics simulations and control systems, where numerical results need to be reproducible across machines and time, this is unacceptable.

**Lock files as primary mechanism** (cargo, npm, Akku, modern pip with `pip-compile`): After resolution, write a `curry.lock` containing the exact version and checksum of every dependency in the transitive closure. Subsequent installs read the lock file rather than re-resolving.

*Problems:* Lock files need to be committed to source control and kept updated. This is unfamiliar workflow to users coming from languages that don't have them. Merge conflicts in lock files are annoying but manageable.

**Two-tier system** (Julia): `Project.toml` specifies direct dependencies with version constraints (what you author). `Manifest.toml` is the lock file (exact resolved versions of everything, auto-generated). You commit both. The `Manifest.toml` is optional — if absent, resolution happens fresh.

### Assessment

Lock files are mandatory. The Quicklisp/Homebrew model produces irreproducible builds, which is fine for installing applications but wrong for library development and scientific computing. A student running a SICM mechanics simulation a year from now should get the same numerical results as today. The Julia two-tier model is the cleanest design: `curry.pkg` is what you write, `curry.lock` is what the resolver writes. Both get committed. If you `curry pkg install` without a lock file present, one is generated. If one is present, it is honoured exactly.

The complexity cost is real but one-time — users learn it once and it becomes invisible.

---

## 3. Environments: Global vs. Per-Project

### The candidates

**Global install** (Quicklisp, Homebrew, early pip): Packages are installed to a single location — `~/.curry/packages/` or similar — shared across all projects on the machine. Simple mental model; no per-project setup.

*Problems:* Two projects depending on different versions of the same package cannot coexist. This is the classic dependency hell problem. Python suffered from it badly before `venv`; Node.js solved it (perhaps too aggressively) with local `node_modules`.

**Per-project environments** (Python venv, OPAM switches, Julia environments, npm local): Each project has its own package directory. Packages are not shared between projects. Isolation is complete.

*Problems:* Disk usage multiplies — `node_modules` becoming a gigabyte is the meme. Activation overhead (you must `activate` the environment or set `PATH`). First-time UX is worse.

**Hybrid with global cache** (cargo): Packages are compiled per-project, but the source and build artifacts are cached globally in `~/.cargo`. Projects get isolation without redundant downloads. This works because Rust recompiles everything from source.

**Named environments** (OPAM switches, conda): Global-like in feel but explicitly versioned. You create named environments and switch between them. Better for long-lived projects, worse for quick experiments.

### Assessment

Global install with a per-project lock file is the right starting point for Curry. Here is the reasoning: Curry's module system is flat (packages install `.so` and `.scm` files into a directory), it has no concept of project-scoped loading, and the user base right now is doing exploratory physics and control systems work — not maintaining ten concurrent projects with conflicting dependency graphs. A global `~/.curry/packages/` that the lock file pins to exact versions gives reproducibility without the ceremony of per-project environments.

The moment two installed packages genuinely conflict — meaning the same package at different versions is required — is the moment to add environment support. That moment is likely years away. Design for it in the lock file format (include an environment name field, default `"global"`) so migration is additive, not breaking.

---

## 4. C Extension Handling

### The candidates

**Pre-built binaries in the registry** (pip wheels, Homebrew bottles): The registry stores platform-specific compiled binaries. Install is fast; no compiler required on the user's machine.

*Problems:* You need a build farm to produce binaries for every platform. For a small ecosystem this is completely impractical. pip's wheel infrastructure is impressive and expensive to replicate.

**Source-only, user compiles** (cargo, CHICKEN eggs): Everything is compiled on the user's machine at install time. Requires a compiler and build tools, but this is already a prerequisite for Curry itself.

**Compile-to-C then compile** (CHICKEN): CHICKEN's compiler produces C, which is then compiled by the system C compiler. Portable but slow.

**CMake with cached artifacts** (cargo-style, adapted): Source is fetched, `cmake --build` runs in the package directory, the resulting `.so` is placed in `~/.curry/packages/<name>-<version>/`. Subsequent `pkg install` for the same version skips the build if the `.so` is already present and the checksum matches.

### Assessment

Source-only with CMake is the only viable option given Curry's existing build infrastructure. It's already how the built-in modules work. The user experience consequence is that installing a C extension package takes longer than a pure-Scheme one — this is expected and acceptable, analogous to `cargo build` for a crate with C dependencies. Pure-Scheme packages install in milliseconds; C extension packages take a few seconds. Document this distinction clearly in `curry.pkg` (a `(sources ...)` field signals a C extension; its absence means pure-Scheme).

Pre-built binaries can be added later as an optimization if a popular package is slow to build, using a simple convention: the index entry can include a `bottles` list of platform-tagged binary URLs alongside the source URL. The package manager tries bottles first, falls back to source. This is exactly Homebrew's model and it works well. Design the index format to accommodate it from day one even if no bottles exist yet.

---

## 5. Versioning

### The candidates

All serious modern package managers use semantic versioning (major.minor.patch). The debates are about constraint syntax and enforcement.

**Semver with caret constraints** (cargo, npm `^1.2.3`): Means "compatible with 1.2.3" — allows minor and patch updates, not major. Most useful in practice.

**Semver with tilde constraints** (`~1.2.3`): Allows only patch updates. More conservative.

**Exact pinning** (`=1.2.3`): Rarely right in a manifest; appropriate in a lock file.

**No constraints / latest** (`*`): Dangerous but common in early ecosystems.

**Cargo's strict semver enforcement**: A crate cannot publish a breaking change as a minor version without the resolver noticing. This is enforced socially (the community will complain) not technically.

**OPAM's constraint solver**: OPAM uses an external SAT solver (mccs or z3) for dependency resolution. Correct but heavy. Overkill until the ecosystem has hundreds of packages with real conflicts.

### Assessment

Semver with caret constraints as the default, exact pinning in lock files. The resolver can be a simple greedy algorithm to start — take the highest version satisfying all constraints, error on conflicts. A SAT solver is not needed until the ecosystem is large enough to have genuine diamond dependency problems, which at Curry's scale means never or years from now. Cargo didn't need a sophisticated resolver until it had tens of thousands of crates; it still uses a relatively simple backtracking algorithm rather than a full SAT solver.

---

## 6. Package Identity

### The candidates

**Name only** (npm, early pip): First to register a name owns it. Caused left-pad (a trivially small package that thousands of projects depended on; when the author removed it, half of npm broke). Caused malicious typosquatting at scale.

**Name + registry namespace** (crates.io): Names are first-come-first-served within a single global registry. Slightly better — at least there's one authoritative place — but still squattable.

**Name + author namespace** (GitHub packages, Swift Package Manager): Packages are addressed as `author/package-name`. No squatting; identity is tied to a controlled account.

**UUID** (Julia): Every package has a UUID generated at registration time. The human name is an alias. Two packages can have the same name (in different registries or forks) without conflict — the UUID disambiguates. No squatting possible. The tradeoff is that UUIDs are opaque and must be stored in `curry.pkg`.

**Git URL as identity** (Go modules): The package's canonical identity is its import path, which is a URL. No registry needed for identity; discoverability is the problem.

### Assessment

UUIDs, following Julia's model. This sounds heavyweight but it is genuinely the only approach that scales without a centralised registry gatekeeping names, and it prevents every squatting and left-pad scenario permanently. The UUID goes in `curry.pkg` once when you `curry pkg init` — the tooling generates it, you never type it by hand. After that it's invisible. Packages are referred to by their human name in manifests and lock files; the UUID is used internally by the resolver to distinguish packages with the same name from different registries or forks. If Curry ever has a second community registry, packages from both can coexist without conflict.

The counter-argument is that this is complexity nobody needs until there are multiple registries. The rebuttal: the UUID is four lines of code to generate and ten bytes in a file. The cost is negligible; the protection is permanent.

---

## 7. Manifest Format

### The candidates

**TOML** (`Cargo.toml`, `pyproject.toml`): Structured, human-readable, good tooling support, not Scheme.

**JSON** (`package.json`): Ubiquitous tooling; ugly to write by hand; not Scheme.

**S-expressions** (Akku, Guix, CHICKEN): Fits the language; readable by the interpreter itself with no extra parser; extensible.

**Custom DSL** (Homebrew Ruby formulae): Expressive but requires a runtime to evaluate.

### Assessment

S-expressions. This is not a close call. `curry.pkg` is read by a Curry script using the existing reader — no extra parser to write or maintain. It is consistent with the language's aesthetic. It is extensible (new fields are just new pairs in the list; old tooling ignores unknown fields gracefully). Akku, the most serious R7RS package manager, made this choice and it has caused no problems. The only users who will ever read a `curry.pkg` are Curry users; TOML's "broader tooling support" is irrelevant in this context.

---

## 8. Security

**Checksum verification** is non-negotiable. Every entry in the index includes the SHA-256 of the source tarball. The package manager verifies before extracting. This is what cargo, pip wheels, and Homebrew all do. A compromised upstream URL cannot silently inject malicious code.

**Index integrity** is handled by the git history of the index repository. The index is cloned/fetched, not downloaded as a flat file. A corrupted or hijacked index would require compromising the git repository, which has its own access controls and audit trail.

**Signing** (cargo's recently added `cargo vet`, npm's provenance attestations) is worth noting but not implementing now. The attack surface at Curry's scale is low. Add it when there are enough packages that a motivated attacker would bother.

---

## Summary Recommendation

| Question | Recommendation | Rationale |
|----------|---------------|-----------|
| Registry model | Git-index (Julia/Homebrew style) | No infrastructure to operate; migrates cleanly later |
| Lock files | Yes — `curry.lock`, auto-generated | Reproducibility for scientific/control use |
| Environments | Global install initially, environment name in lock file format | Correct for current scale; migration path built in |
| C extensions | Source-only via CMake; `bottles` field reserved in index | Matches existing build infrastructure |
| Versioning | Semver, caret constraints, greedy resolver | Sufficient until ecosystem is large |
| Package identity | UUID + human name (Julia model) | Permanent protection; negligible cost |
| Manifest format | S-expressions (`curry.pkg`) | Native to the language; no extra parser |
| Security | SHA-256 checksums in index; git history as audit log | Sufficient; signing deferred |

---

## What This Looks Like in Practice

A package author runs `curry pkg init`, gets a `curry.pkg` with a generated UUID:

```scheme
(package
  (name        pendulum-control)
  (uuid        "7f3a2b1c-4d8e-4f9a-b2c3-1a2b3c4d5e6f")
  (version     "0.1.0")
  (description "LQR controller synthesis for pendulum systems")
  (license     "MIT")
  (author      "Scáth <metanoia@gmail.com>")
  (homepage    "https://github.com/scath/curry-pendulum-control")
  (depends     (curry       ">=0.8.14")
               (curry-sicm  "^1.0"))
  (provides    (curry pendulum-control)))
```

A user runs `curry pkg install pendulum-control`. The package manager fetches the index, finds the entry, verifies the SHA-256, extracts to `~/.curry/packages/pendulum-control-0.1.0/`, and writes or updates `curry.lock`. Next time, the lock file is honoured exactly.

```scheme
(import (curry pendulum-control))
```

Works immediately. No `CURRY_MODULE_PATH` to set — `~/.curry/packages/` is prepended automatically at startup.

---

## One Thing to Get Right Before Everything Else

The index format. Once packages are published against it, changing the index schema breaks everything. Design it carefully — with the UUID field, the optional `bottles` list, and a `format-version` field — before writing a line of package manager code. Everything else can be iterated on; the index schema cannot.
