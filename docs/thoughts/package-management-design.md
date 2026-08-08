# curry pkg — The Bīt Ṭuppi Design
## A Package Manager for Curry

*Public draft — 2026-08-08. Supersedes `docs/guides/pkg-design.md` (kept in
place as historical background — see that file for the original
candidates/assessment reasoning this document builds on and, in one place,
overturns). Feedback wanted: open a GitHub issue on the curry repository
tagged `pkg-design`.*

---

## 0. What this document is

This is a design proposal, not a committed spec. It's written to be read by
three audiences at once: curry's own maintainers deciding what to build,
prospective curry package authors who want to know what they're signing up
for, and — since curry's ambitions (SICM mechanics, hardware control,
symbolic CAS, a cuneiform-native numeric reader) sit at an unusual
intersection of "small embedded-Scheme runtime" and "wants a real package
ecosystem" — the wider Lisp/Scheme implementer community, whose experience
with CHICKEN eggs, Akku, Guix, and Quicklisp is exactly the prior art this
document leans on.

If you build package managers for a living, or have opinions about why
yours works the way it does, we want to hear where this gets it wrong. The
three places we're least confident are called out explicitly in §14. Open
an issue tagged `pkg-design` on the curry repository; that's the mechanism
for now, since none exists yet.

---

## 1. Context

A prior internal evaluation, `docs/guides/pkg-design.md`, already surveyed
CHICKEN/Akku/cargo/pip/npm/Julia/Quicklisp/OPAM/Guix and settled seven of
nine major questions: registry model, lock files, environments, versioning,
package identity, manifest format, and security. That document's reasoning
was sound and this one inherits it wholesale — §3 below restates each of
those seven conclusions as this document's own, so nothing here requires
also reading the original.

This document exists for two reasons. First, it revisits the one point
where we now disagree with the original evaluation: how a package reaches
outside pure Scheme to native (C/C++) capability. Second, it fills in
everything the original evaluation didn't cover at all — bundling tests and
reference documentation with a package, a local-development workflow,
targeted dependency updates, version retraction, optional dependencies, and
a path for porting existing CHICKEN eggs and SRFIs into curry's ecosystem.

One naming convention, used throughout and explained fully in §2: this
document talks about the registry, a package, and a native-capability
package using historical Akkadian terms — **bīt ṭuppi**, **iškāru**, and
**iškāru aḥûtu** — because curry's own error messages and special-form
aliases are already bilingual this way, and because (as §2 shows) the
historical terms turn out to map onto these concepts with unusual
precision, not as decoration. None of this is load-bearing in actual
syntax: the manifest file, the CLI, and every directory name stay plain
English. Akkadian is the document's vocabulary for talking about the
design, not the tool's vocabulary for talking to a user.

---

## 2. Naming and Vocabulary

Ancient Mesopotamian scribal culture had a real, working answer to "what do
you call a collection of collections," and it maps onto a package
ecosystem closely enough to be worth using rather than reinventing generic
terms that Python and npm have already made ambiguous (a "package" in
Python means two different things — a distributable unit and an import
namespace — and everyone just says "package" for both and disambiguates
from context; see the discussion this document grew out of for the full
comparison).

**bīt ṭuppi** — literally "house of tablets" (Sumerian *é-dub-ba-a*) — was
the native term covering school, scriptorium, and library at once.
Crucially, the most famous example, Ashurbanipal's library at Nineveh, was
not where its texts originated — it was an active *collection point*:
scribes were sent out to copy canonical compositions from Babylonia and
bring them in, and the whole thing was organized around catalogue tablets
indexing what was held. That is structurally identical to a git-index
registry (§3): it doesn't store the code, it aggregates pointers to where
each package's source actually lives. **bīt ṭuppi is this document's term
for the registry.**

**iškāru** — "series" — was the term for a canonical, ordered, named
composition spanning multiple tablets. Enūma Anu Enlil, the astrological
omen series, ran to 68–70 tablets under one iškāru name; tablets within it
were numbered by position and carried *catchlines* — the last line of
tablet N repeated as the first line of tablet N+1, letting a scribe verify
the sequence was intact and correctly ordered. **iškāru is this document's
term for an individual package**: a named, versioned, ordered unit, with
its own internal integrity story, exactly what a catchline is for.

**iškāru aḥûtu** — an attested cataloguing term. Library records from
Nineveh reference an *iškār aḥûti*, "series of extraneous material," to
distinguish supplementary astrological-omen tablets that circulated
alongside Enūma Anu Enlil from the canonical series proper. **This
document repurposes it for a package whose capability comes from outside
curry's own canon** — i.e., depends on a native system library. A plain
iškāru is self-contained; an iškāru aḥûtu reaches outside.

| Akkadian term | Concept | Concrete English artifact |
|---|---|---|
| bīt ṭuppi | the registry/index | the curry package index (a git repository) |
| iškāru | a package | a curry package, described by `curry.pkg` |
| iškāru aḥûtu | an FFI-backed native package | a `curry.pkg` with `(native ffi)` — no separate file extension or CLI form |

This is the last time this document explains the terms; from here on they're
used the way "package" and "registry" would be used in any other design
document, just with roots you can trace.

---

## 3. Prior Art vs. This Document

`docs/guides/pkg-design.md` already made these seven decisions with full
reasoning; they are not reopened here. This section restates the
conclusions in this document's vocabulary so it stands on its own.

**Registry model — the bīt ṭuppi is a git-index, not a hosting service.**
A public git repository holds metadata only: name, UUID, version, source
URL, checksum. Source code lives wherever its author hosts it. Rejected:
a centralized upload registry (real infrastructure and operational burden
before a single package exists), pure URL installs with no registry at all
(no discoverability, no version resolution), and CHICKEN's eggs-committed-
directly-into-the-index model (the index repository grows unboundedly with
every version ever published, and CHICKEN's own has become unwieldy as a
result). Julia's General registry proves the git-index model scales to
thousands of packages without server infrastructure.

**Lock files are mandatory, two-tier, Julia-shaped.** `curry.pkg` is what
an author writes — direct dependencies and constraints. `curry.lock` is
what the resolver writes — the exact resolved version and checksum of
every package in the transitive closure. Both are committed. Reproducible
resolution matters for curry specifically because its stated ambitions
(SICM mechanics, control systems, physics simulation) require numerical
results to be reproducible across machines and time — the Quicklisp/
Homebrew "always resolve to current latest" model is fine for installing
applications and wrong for this.

**Environments: global install now, with the escape hatch already in the
lock-file format.** Packages install to a single `~/.curry/packages/`,
shared across projects, pinned per-project by the lock file. True
per-project isolation (a project having its own package directory, the way
Python's venv or Julia's environments do) is deferred — not because it's
wrong, but because curry's module system is flat today and the user base
doing exploratory scientific work isn't yet running into real cross-project
version conflicts. The lock file format reserves an `environment` field
(default `"global"`) precisely so this migrates additively later rather
than breaking existing lock files.

**Versioning: semver, caret constraints by default, a simple greedy
resolver.** `^1.2.3`-style ranges in `curry.pkg`, exact pins in
`curry.lock`. No SAT solver (OPAM's approach) — that's warranted once an
ecosystem has hundreds of packages with genuine diamond-dependency
conflicts, which is years away at best for curry's scale, if it ever
happens at all.

**Package identity: UUID plus human name, Julia's model.** Every package
gets a UUID generated once, invisibly, at `curry pkg init` time. The human
name is what appears in manifests and on the command line; the UUID is
what the resolver actually keys on, so two packages with the same display
name — from different registries, or a fork — never collide, and no name
can ever be squatted the way `left-pad` was on npm.

**Manifest format: S-expressions, read by curry's own reader.** `curry.pkg`
needs no separate parser — it's just data curry already knows how to read.
This also happens to be Akku's choice, the most serious existing R7RS
package manager, and it has caused no problems there.

**Security: SHA-256 checksums are non-negotiable; signing is deferred.**
Every index entry carries the checksum of the source it points at; the
package manager verifies before extracting, so a compromised or hijacked
source URL can't silently inject anything. The git history of the index
itself is the audit log. Cryptographic signing (`cargo vet`, npm
provenance attestations) is real and worth having eventually, but not
before there's an ecosystem large enough that a motivated attacker would
bother.

---

## 4. The Native-Capability Decision Point

This is the one place this document reverses the prior evaluation, and per
this project's own standing rule about breaking changes, it gets the full
treatment: named candidates, their real problems, and a decisive
assessment — not a hedge.

### The problem

A curry package sometimes needs to reach outside pure Scheme to a native
C/C++ library. Curry itself already has two working mechanisms for this,
and they pull in different directions. Every C module curry ships today
(`qt6`, `sqlite`, `git`, and the rest) is a hand-written `.c`/`.cpp` file,
compiled by CMake at curry's own build time, `dlopen`'d at runtime, and
deliberately never linked against `curry_core` — linking would embed a
second copy of the symbol-intern table and break the pointer-identity
assumption curry's environment lookup relies on. The alternative,
`(curry ffi)`, lets pure Scheme code declare bindings against an arbitrary
system library and `dlopen` it at *runtime*, no compile step at all — and
`(curry hdf5)` already proves this works in production: a candidate list of
per-platform install locations, a `guard`-wrapped attempt at each, and a
clear `error` naming the right install command if none of them pan out.

### The candidates

**Source-compile via CMake at install time** (cargo, CHICKEN eggs; the
prior evaluation's own §4 pick): the package ships C/C++ source; installing
it runs `cmake --build` in the package directory and produces a `.so`.

*Problems:* every user needs a working C/C++ toolchain and CMake on their
machine (true, but already a curry-build prerequisite, so not a new
burden); install is slow and machine-specific; the package author now owns
a build-matrix problem across the platforms curry itself supports. None of
this is fatal — it's the price of admission for the class of library that
genuinely needs it (see below) — but it shouldn't be the *default* price
every native-capability package pays.

**FFI-first, runtime `dlopen`** (this document's proposed default,
following `(curry hdf5)`'s proven pattern exactly): the package is pure
Scheme, using `(curry ffi)`'s `define-foreign` against a system-installed
shared library it locates and `dlopen`s at `(import ...)` time.

*Problems:* `(curry ffi)` has real, confirmed structural limits — no
struct-by-value, no callbacks (no `ffi_closure`/trampoline support at all),
no variadic C functions, a fixed 64-argument ceiling. A callback-heavy
event-loop library, a struct-heavy API, or anything built around
`printf`-style variadic functions simply cannot be wrapped this way, full
stop. So FFI-first cannot be the *only* mechanism, only the default one.

**Pre-built binaries / bottles** (pip wheels, Homebrew bottles): already
reserved as a future `bottles` field in the prior evaluation's index
schema, restated rather than re-litigated here. Still not implemented;
still the right thing to add later if a popular package is slow to build
from source, exactly as Homebrew added bottling once its ecosystem
justified it.

### Assessment

**FFI-first is the new default for native capability; source-compile
survives as an explicit, named fallback tier — this is a deliberate
reversal of the prior evaluation's §4, made because the FFI path is not
hypothetical.** `(curry hdf5)` is real, shipping code, not a proposal: it
turns "does this package work" from a build-matrix question into a runtime
capability check ("is `libfoo.so` present"), the package stays pure Scheme
source with zero build step, and it is trivially compatible with the
git-index registry model — there's nothing to compile, cache, or verify
per-platform, just files. The source-compile tier doesn't go away; it's
still exactly right for the minority of libraries FFI structurally cannot
reach, and a package declares which tier it needs (§5) rather than the
tooling guessing.

This produces three tiers, only two of which get Akkadian names — minting
a fourth term for the compiled-source fallback would dilute a vocabulary
that's supposed to stay small enough to actually use:

1. **A plain iškāru** — pure Scheme, no native dependency at all.
2. **An iškāru aḥûtu** — pure Scheme using `(curry ffi)` against a
   runtime-`dlopen`'d system library. The new default for native
   capability.
3. **A compiled iškāru** (no separate term) — ships C/C++ source, built via
   CMake at install time, for the cases tier 2 structurally can't cover.

One consequence worth stating plainly rather than leaving implicit: the
package manager checksums an iškāru's *own* source, which is small and
lives in the tarball the index points at. It cannot and does not checksum
the system library an iškāru aḥûtu `dlopen`s against at runtime — that
library isn't part of the package, isn't fetched by the package manager,
and its integrity is the user's own system package manager's problem,
exactly the assumption `(curry hdf5)` already makes today about `libhdf5`.

**What this requires from curry itself:** nothing, for v1. `(curry ffi)`'s
limitations aren't a blocker, because tier 3 exists precisely to cover
them. If FFI-first adoption turns out to hit that ceiling often in
practice — real demand for callback support or struct marshaling — that's
a legitimate future investment in `src/ffi.c` (`ffi_closure` trampolines,
a struct-layout DSL), but it's not something this document is asking for
now.

---

## 5. Manifest Schema

The prior evaluation's sample manifest covered identity, versioning, and
dependencies well, but had no way to say "this package ships tests," "this
package ships its own documentation," or "this package needs native
capability." This section adds those fields.

**`tests`** — a path (relative to the package root) to bundled test
source, shipped inside the iškāru itself rather than living only in
curry's own tree. This is the direct analogue of curry's internal
`tests/X_tests.scm` convention, just packaged with the code it tests
instead of alongside a core module. `curry pkg test <name>` (§7) runs
whatever this field points at.

**`docs`** — a path to bundled reference documentation, shipped with the
package. This is deliberately not just a `homepage` URL: a `homepage` can
disappear or drift out of sync with the installed version; documentation
that travels with the code it documents can't.

**`native`** — one of `ffi`, `compiled`, or absent (meaning tier 1, plain
Scheme). This is how a package declares which of §4's three tiers it
needs; the installer uses it to decide whether `(import ...)` works
immediately after fetching the source, or whether a CMake build has to run
first.

**`depends-optional`** — weak/optional dependencies, modeled on Julia's
package extensions (added in Julia 1.9): a package can integrate with
another package *if* it happens to be present, without forcing every
consumer to install it. This maps directly onto something curry already
does at the core level — `(curry qt6)`, `(curry plplot)`, and the rest are
already optional, gated by CMake flags that may or may not be turned on in
a given build — so this field just extends a pattern curry's own module
system already lives by, down to the package layer.

A full manifest showing all of these together:

```scheme
(package
  (name        pendulum-control)
  (uuid        "7f3a2b1c-4d8e-4f9a-b2c3-1a2b3c4d5e6f")
  (version     "0.1.0")
  (description "LQR controller synthesis for pendulum systems")
  (license     "MIT")
  (author      "Scáth <metanoia@gmail.com>")
  (homepage    "https://github.com/scath/curry-pendulum-control")
  (native      ffi)
  (tests       "tests/pendulum-control-tests.scm")
  (docs        "docs/pendulum-control.md")
  (depends          (curry       ">=0.8.14")
                     (curry-sicm  "^1.0"))
  (depends-optional (curry-plplot "^0.3"))  ; renders control-loop plots if present
  (provides    (curry pendulum-control)))
```

---

## 6. Components

CHICKEN eggs distinguish `extension` (compiled), `library` (pure Scheme),
and `program` (a standalone executable) as different component types one
egg can bundle together. Given §4's decision, most of the reason that
model exists doesn't apply to curry: there's no longer a distinct
"compiled artifact" component type to track separately from a plain
library, because native capability is a manifest field (`native`) on an
otherwise ordinary package, not a different kind of thing.

**Assessment:** one package corresponds to one `provides` library name,
plus its bundled tests and docs (§5). No CHICKEN-style multi-component
model for v1 — the `native` field already captures the one distinction
that actually matters here (does this iškāru reach outside pure Scheme or
not), and adding a full components system on top would be tracking a
distinction curry's own design no longer needs. If a real need for a
standalone-program component ever shows up — a package that's meant to be
run, not imported — that's a reasonable future addition, but it isn't
designed here.

---

## 7. Tooling: The `curry pkg` Command Surface

`docs/roadmap.md`'s existing sketch already names five verbs:
`install`, `update`, `build`, `test`, `publish`. These stay as described —
`build` only does anything for a tier-3 compiled iškāru, `test` runs
whatever the manifest's `tests` field points at, `publish` submits to the
bīt ṭuppi. This section adds the verbs this document introduces.

**`search`** — query the bīt ṭuppi by name or description. Straightforward;
included here mainly because the user-facing verb list wasn't otherwise
complete without it.

**`develop`** — point an import at a local working directory instead of
the installed copy, so a package can be actively modified in place. This
is the single most important addition in this document, because it's the
direct answer to a workflow this whole design effort started from: making
it easy to handle changes to packages you're actively developing against,
not just packages you passively consume. It's the curry analogue of
Julia's `Pkg.develop(path=...)` and cargo's path dependencies.

There is a real, honestly-unresolved tension here: §3 settled on global
install with no per-project isolation, and `develop` needs *some* notion of
"this one import resolves somewhere else, for this session or this
project" — which a purely global model doesn't naturally give you. The
recommended v1 answer reuses a mechanism that already exists and already
works rather than inventing a new one: an environment-variable override in
the spirit of `CURRY_MODULE_PATH` (`src/modules.c` already searches that
variable's entries ahead of the default install locations), set for a
`curry pkg develop <name> --path <dir>` session so that name resolves to
the local checkout until the developer runs the equivalent of `curry pkg
free <name>`. This is the least battle-tested part of this document — flagged
explicitly, not papered over.

**`update -p <name>`** — update the resolution of exactly one dependency,
leaving the rest of `curry.lock` untouched, the way `cargo update -p foo`
and `Pkg.update("Foo")` do. This is a v1 requirement, not a nice-to-have:
when an upstream package changes and you need to react to it, you want a
minimal, reviewable diff to that one edge of the graph, not a full re-
resolve that touches everything and makes the actual change unreviewable.

**A registry-PR-submission helper** (folded into `publish`, or a separate
`curry pkg register`) — actually scaffolds and opens a pull request against
the bīt ṭuppi's index repository, rather than telling the author to go do
it by hand. Concretely: compute the source tarball's checksum, format the
index entry, create a branch, and run `gh pr create` with a templated
body. This is the local-CLI equivalent of what Julia's Registrator bot does
automatically on push; curry's version is a command a human runs
deliberately rather than a bot watching for tagged releases, at least
until there's enough traffic to justify automating that too. This needs
the `gh` CLI as an optional tool dependency, not a hard one — `publish`
should degrade to "here's the diff, open the PR yourself" if `gh` isn't
present.

---

## 8. Versioning and Registry Refinements

**Retraction, not deletion.** Cargo's `yank` marks a version as "don't
resolve to this for new work" without removing it from the index — anyone
who already locked that version keeps working exactly as before, while new
resolutions steer around it. This is a straightforward addition to the bīt
ṭuppi's index schema: a `retracted` field, boolean or a reason string, next
to each version entry. This is v1 scope, not deferred — it costs a schema
field, not new infrastructure, and it's the difference between "a bad
release breaks reproducibility for everyone downstream forever" and
"a bad release is marked and avoided going forward."

**Optional dependencies** are already covered in §5 — no repetition here.

**Targeted lockfile updates** are already covered in §7 — no repetition
here.

**Compat-range-bump automation** — Julia's CompatHelper bot watches for new
compatible releases and opens PRs bumping dependents' version ranges
automatically, because manually maintaining compat bounds across a
dependency graph doesn't scale past a handful of packages. This is
explicitly **future work**, not a v1 requirement: it's valuable once the
ecosystem is large enough that manual compat-bumping becomes real toil, and
curry's ecosystem isn't there yet.

This adds a fourth item to the prior evaluation's "one thing to get right
before everything else" — the index schema now needs the UUID field, the
`bottles` list, the `format-version` field, *and* the `retracted` field
designed in from the start. See §15.

---

## 9. Porting CHICKEN Eggs and SRFIs

This section is genuinely less settled than the rest of this document —
it's new design space nobody has pressure-tested yet, and it's presented
as a recommendation open to revision, not a decided conclusion.

### The candidates

**A manual porting guide plus manifest convention**: document how to wrap
an existing, portable-R7RS CHICKEN egg — or an unimplemented SRFI's
reference implementation — with a `curry.pkg` manifest around largely
unchanged source. For SRFIs specifically, mirror curry's own existing
convention of three parallel access paths to the same implementation
(`(srfi N)`, `(srfi srfi-N)`, `(srfi sN title-words)`) as the naming
template.

**Automated `.egg`-file parsing**: a tool that reads a CHICKEN egg's `.egg`
metadata file and auto-generates a `curry.pkg` manifest.

*Problems with the automated route:* a CHICKEN `.egg` file encodes
CHICKEN-specific concepts — the extension/library/program component split
§6 already showed mostly doesn't map onto curry's model, CHICKEN-specific
build steps, CHICKEN-specific dependency names. An auto-generator would
need to special-case or silently discard much of what it parses, for a
payoff — saving a human the ten minutes it takes to write a manifest by
hand — that doesn't justify the ongoing maintenance burden of keeping a
CHICKEN-format parser correct across CHICKEN's own format changes (the
CHICKEN 4→5 egg-format break is the cautionary tale here).

### Assessment

Ship the porting guide and the SRFI naming convention for v1. Defer
automated egg-file parsing as a named, later-roadmap item, not something
this document commits to building. A portable-R7RS egg or SRFI reference
implementation typically needs little more than a `curry.pkg` wrapped
around unchanged source; a guide showing exactly how to do that captures
most of the value the automated route would, at a fraction of the ongoing
cost.

---

## 10. What Must Change in curry Itself

Collected here rather than scattered, per the standing rule that a breaking
or core-touching change needs an explicit, resolved decision — not a
dangling question.

- **`modules_init()`'s search-directory list** (`src/modules.c`) needs a
  fifth entry, for wherever installed packages land (`~/.curry/packages/`
  or equivalent), so `(import ...)` finds them with no `CURRY_MODULE_PATH`
  fiddling required. Concrete, small, **in scope for v1**.
- **The module registry is append-only, with no unload or reload
  primitive** (`ModuleEntry`, `src/modules.c`). Consequence: this document
  does not attempt to make "upgrade a package without restarting curry"
  work. **Out of scope for v1**, stated plainly — upgrading a package
  means restarting the curry process that imported it, full stop, until
  core gains a reload story of its own.
- **`(curry ffi)`'s structural limits** (no struct-by-value, no callbacks,
  no variadics) are not a v1 blocker, because §4's tier 3 exists precisely
  to cover what tier 2 can't reach. Flagged as a plausible future
  investment in `src/ffi.c`, not requested now.
- **The Akkadian alias mechanism** (`lib/curry/modules/curry/private/
  lang-aliases.scm`) needs **no change at all** — it's a purely internal,
  opt-in convention for curry's own stdlib theming. A third-party package
  author may use it voluntarily for their own procedures; nothing about
  the package system depends on it or needs to touch it. Stated explicitly
  here so the question doesn't linger unanswered.
- **`develop` mode's tension with global install** (§7) is the one item on
  this list that isn't fully resolved — the recommended `CURRY_MODULE_PATH`-
  style override is a real answer, but it hasn't been built or tested, and
  is honestly flagged as such rather than presented as settled.

---

## 11. Roadmap

This document feeds `docs/roadmap.md`'s existing Phase 10; it isn't a
second roadmap and doesn't assign version numbers to itself — that's
`roadmap.md`'s job, done separately once this document is accepted.

**v1 scope:** the bīt ṭuppi git-index registry; `curry.pkg`/`curry.lock`;
tiers 1 and 2 (plain Scheme, FFI-backed) plus tier 3 (compiled fallback)
for native capability; the manifest fields in §5 (`tests`, `docs`,
`native`, `depends-optional`); the CLI surface in §7 (`install`, `update`,
`build`, `test`, `publish`, `search`, `develop`, `update -p`); retraction
in the index schema (§8); the `gh`-based publish/PR helper; the porting
guide for eggs and SRFIs (§9); and the one core change in §10 (the
search-directory entry).

**Explicitly deferred:** true per-project environments beyond the reserved
lock-file field; pre-built bottles; cryptographic signing; CompatHelper-
style automation; automated egg-file parsing; core FFI extension
(callbacks, struct marshaling); module hot-reload/unload.

---

## 12. Summary Recommendation

Only what this document adds or changes relative to the prior evaluation's
own summary table — its seven settled rows aren't repeated here.

| Question | Recommendation | Rationale |
|----------|---------------|-----------|
| Native-capability default | FFI-first (`(curry ffi)`, runtime `dlopen`); compiled-source as an explicit fallback tier | `(curry hdf5)` already proves this works; zero build step; compiled tier still covers what FFI structurally can't |
| Manifest additions | `tests`, `docs`, `native`, `depends-optional` fields | Tests and docs travel with the code; native capability is declared, not guessed; optional deps mirror curry's own optional-module reality |
| Components model | One package = one library + bundled tests/docs; no CHICKEN-style multi-component split | The one distinction that mattered (compiled vs. not) is now a manifest field, not a component type |
| Retraction | `retracted` field in the index schema | Bad releases get avoided going forward without breaking existing lock files |
| Targeted updates | `curry pkg update -p <name>` | Minimal, reviewable diffs when reacting to one upstream change |
| Local development | `curry pkg develop --path <dir>`, via a `CURRY_MODULE_PATH`-style override | Direct answer to "handling changes to packages you're developing against"; the one under-tested part of this design |
| Egg/SRFI porting | Manual guide + manifest convention; automated `.egg` parsing deferred | Covers most of the value at a fraction of the ongoing maintenance cost |
| Core changes | One `src/modules.c` search-path entry now; registry reload and FFI extension explicitly deferred | Small, scoped v1 footprint; everything else is additive later |

---

## 13. What This Looks Like in Practice

An iškāru aḥûtu — the FFI-backed tier — looking exactly like `(curry hdf5)`
does today, just published as a third-party package instead of shipped in
curry's own tree:

```scheme
;; curry.pkg
(package
  (name     mylib-sqlite-extra)
  (uuid     "3c9e1a20-...")
  (version  "0.2.0")
  (native   ffi)
  (tests    "tests/mylib-sqlite-extra-tests.scm")
  (docs     "docs/mylib-sqlite-extra.md")
  (depends  (curry ">=1.17"))
  (provides (curry mylib-sqlite-extra)))
```

```
$ curry pkg install mylib-sqlite-extra
Fetching from package index...
Verifying checksum... ok
mylib-sqlite-extra is pure Scheme (native: ffi) — no build step.
Installed to ~/.curry/packages/mylib-sqlite-extra-0.2.0/
```

```scheme
(import (curry mylib-sqlite-extra))
;; works immediately — the package's own define-foreign bindings
;; dlopen the system library the first time something calls into them,
;; exactly like (curry hdf5) does today.
```

Developing against a local checkout of the same package:

```
$ curry pkg develop mylib-sqlite-extra --path ../mylib-sqlite-extra
mylib-sqlite-extra now resolves to ../mylib-sqlite-extra for this session.
$ curry -e '(import (curry mylib-sqlite-extra)) ...'
;; picks up local edits immediately, no reinstall
$ curry pkg free mylib-sqlite-extra
mylib-sqlite-extra now resolves to the installed copy again.
```

Reacting to an upstream change with a minimal diff:

```
$ curry pkg update -p curry-sicm
Resolving curry-sicm only...
curry-sicm: 1.0.2 -> 1.1.0
curry.lock updated (1 package changed)
```

---

## 14. What's Settled, What's Open

**Settled, and not expected to move**: everything in §3 (inherited from the
prior evaluation) and the FFI-first native-capability decision in §4 — the
latter is a genuine reversal of prior guidance, made deliberately, with the
reasoning above, not lightly.

**Newer, and specifically wanted feedback on:**

1. **The FFI-first default itself.** If you've built or maintained an
   embedded-Scheme (or embedded-anything) FFI layer against real-world C
   libraries, does defaulting to runtime `dlopen` over compiled bindings
   hold up in practice, or does it just move the pain somewhere less
   visible?
2. **The `develop`-mode mechanism** (§7, §10) — is a `CURRY_MODULE_PATH`-
   style session override the right shape for local-development ergonomics
   under a global-install model, or does this quietly need real per-
   project environments sooner than §3 assumes?
3. **The egg/SRFI porting scope** (§9) — is a manual guide really enough,
   or does the value of automated porting kick in earlier than estimated
   here?

Feedback: open a GitHub issue on the curry repository tagged `pkg-design`.

---

## 15. One Thing to Get Right Before Everything Else, Again

The prior evaluation's closing line was the index schema — get the UUID
field, the `bottles` list, and a `format-version` field right before
writing a line of implementation code, because the schema is the one
artifact that can't be iterated on once packages exist against it. That's
still true, and this document adds one more field to that list: `retracted`
(§8), plus the `native` tier convention (§4, §5) needs to be baked into the
manifest format from the very first package anyone publishes. Everything
else in this document — the CLI surface, the porting guide, even the
`develop` workflow — can be built, shipped, and revised incrementally.
The index and manifest schemas cannot.
