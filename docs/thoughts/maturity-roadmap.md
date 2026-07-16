# Maturity Gaps: What Chez and Kaappi Have That Curry Doesn't

*2026-07-16 — companion to [performance-chez-kaappi.md](performance-chez-kaappi.md), which
covers execution speed. This document covers everything else that makes an
implementation feel production-grade: debuggability, conformance confidence,
CI, tooling, and API stability.*

Curry's feature *breadth* already exceeds both reference points — no other
Scheme has a CAS, quantum values, Clifford algebra, and an LLM client built
in. The gap is not features, it's the scaffolding around them that lets
people trust and debug a large system. Findings below are verified against
`main`, not assumed.

---

## 1. What's already solid (don't relitigate)

- `CHANGELOG.md`, `VERSIONING.md`, semver git tags back to v1.4.2 — real
  release discipline.
- `docs/reference/` (44 files) and `docs/guides/` are genuinely thorough for
  the exotic subsystems (CAS, multivectors, surreals, quantum).
- An explicit, documented "R7RS deviations" section in `language.md` —
  Kaappi's `CONFORMANCE.md` does the same thing and it's the right pattern;
  curry already has it.
- Deep domain-specific test suites: `numeric_ext_tests.scm` (354 assertions),
  `sicm_tests.scm` (172), `symbolic_tests.scm` (111) — this is coverage Chez
  and Kaappi don't have because they don't have the subsystems.

## 2. The gaps, ranked by how often they'll bite someone

### 2.1 Error messages carry no location, ever (highest priority)

```
$ curry /tmp/errtest.scm      # a 3-line file, error on line 1, inside (g (f x))
Error: 𒀭 ḫiṭītu — lā qitnum:
  car: not a pair
```

No file, no line, no call stack. `condition-backtrace` — the one API that
could answer "where" — is a literal stub in `src/condition.c`:

```c
static val_t prim_condition_backtrace(int ac, val_t *av, void *ud)
    { (void)ac; (void)ud; (void)av; return V_NIL; /* full traces in v2.2 */ }
```

This is the single highest-leverage fix in this document. Every other item
here is something you hit occasionally; this is something you hit on every
debugging session, immediately, as the very first experience of a bug. Chez
and Kaappi both report source location and a call chain by default.

**What's needed:**
- Thread `source_line`/`source_file` through chunks (compiler already has
  line info for the reader — confirm it survives to `Chunk`) and stamp it on
  the active frame at raise time.
- Walk `VM_FRAMES` (the existing frame stack) at raise time and materialize
  it into the condition object — the frame stack already exists, this is
  "read it before unwinding," not new infrastructure.
- Print `file:line: in <proc>` chain by default on uncaught errors, matching
  the format most Scheme users expect from Racket/Guile/Chez.

### 2.2 No CI that builds or runs the test suite

`.github/workflows/` has exactly one file: CodeQL security scanning. Nothing
runs `cmake --build && ctest` on push or PR. This means:
- A broken build on `main` is only caught by whoever happens to build locally.
- `ctest`'s 36 suites (the ones this session ran manually) aren't gating
  merges.
- No cross-platform coverage (Linux vs macOS) beyond whatever the developer's
  own machine is.

**What's needed:** a `ci.yml` matrix (Ubuntu + macOS, Debug + Release) that
configures, builds, and runs `ctest --test-dir build`. This is the
prerequisite for the benchmark-regression CI already recommended in
performance-chez-kaappi.md §5 Tier 0 — that doc assumed a CI harness exists
to hang the benchmark job off of; it doesn't yet.

### 2.3 No editor/LSP tooling

Kaappi ships a bundled LSP server and a VS Code extension
(`vscode-kaappi`) with syntax highlighting. Curry has neither — no LSP, no
editor extension, no syntax-highlighting grammar published anywhere findable.
For a language with this much surface area (44 reference docs, Akkadian
syntax variants, a CAS), discoverability while typing matters more than
usual: users can't reasonably memorize 600+ procedure names across three
naming languages (English/Akkadian/cuneiform).

**What's needed, roughly in order of payoff:** a TextMate/Tree-sitter grammar
for syntax highlighting (cheap, immediate value, no server needed) → a
minimal LSP exposing hover-for-docstring and go-to-definition, sourced from
the same procedure registry `builtins_register()` already populates → the
existing `,help` / `(disassemble ...)` REPL introspection is a good backend
to reuse rather than duplicate.

### 2.4 No interactive debugger

Kaappi's `vm_debug.zig` gives breakpoints, step/next/continue, locals
inspection, and backtrace, driven from their bytecode VM. Curry's REPL has no
`,break`, `,step`, or `,locals` — `,gc` and `,env` are the extent of REPL
introspection commands found in `src/main.c`. Once §2.1's frame-walking
exists, a minimal stepper is a small addition on top: the VM already has an
explicit frame/register structure, which is the hard prerequisite Kaappi's
design also depends on (§4.4 of the performance doc notes this same
frame-stack-as-data-structure property is what makes their `call/cc` and
their debugger both possible).

### 2.5 Machine-legible diagnostics

Kaappi assigns every error a stable code (`error[KP####]`) and offers
`--diagnostics=json` for tooling. Curry's errors are prose strings — fine
for a human reading them once, unusable for a linter, an LSP, or an agent
parsing output programmatically (curry has an MCP module and an LLM client;
both would benefit from structured errors more than a typical Scheme would).
This is a smaller lift than it sounds: assign codes at the `scm_raise` call
sites incrementally, starting with the ~20 most common (`not a pair`, `not
bound`, `wrong number of arguments`, `not a procedure`, `division by zero`).

### 2.6 R7RS conformance isn't quantified

`docs/reference/language.md` documents deviations qualitatively but there's
no number anywhere — no "curry passes N/M of R7RS Appendix A identifiers"
the way Kaappi's `CONFORMANCE.md` states "1,391 pass, 0 fail" and gives
per-SRFI percentages (e.g. "SRFI 1: 95%, missing `unzip3`–`unzip5`"). Curry's
own `r7rs_tests.scm` is only 168 assertions — an order of magnitude below
Kaappi's suite, though curry's suite is hand-picked and Kaappi's may include
finer-grained per-procedure cases. Either way, nobody evaluating curry today
can answer "how R7RS-complete is this?" with a number, which matters for
credibility with anyone comparing implementations.

**What's needed:** expand `r7rs_tests.scm` toward Appendix A coverage (or
adopt a public R7RS test suite wholesale) and publish a `CONFORMANCE.md` with
per-SRFI percentages, mirroring the pattern that already exists for
"R7RS deviations."

### 2.7 No fuzzing

Neither the reader, the expander, nor the numeric tower (GMP-backed, lots of
promotion-boundary edge cases per CLAUDE.md's own review checklist —
"off-by-one errors in sexagesimal/numeric code") is fuzz-tested. Kaappi has a
`docs/dev/fuzzing.md` and a `fuzzing-feasibility.md`. Given curry's numeric
tower has more promotion boundaries than almost any other Scheme (fixnum →
bignum → rational → flonum → complex → quaternion → octonion → multivector →
surreal → symbolic), this is a strong candidate for `libFuzzer`/AFL harnesses
targeting `reader.c`, `numeric.c` promotion logic, and the sexagesimal
reader — exactly the code CLAUDE.md already flags for extra review scrutiny.

### 2.8 No package manager, still deferred

Recorded in memory as an open item (`project_pkg_design_deferred`) — you
already want a comparative survey before implementing. Restating it here
because it's a maturity axis both reference points score on: Kaappi ships
`thottam` with ~20 first-party libraries; Chez has an ecosystem via Racket/
Chicken-adjacent tooling and R6RS libraries. Curry's C module system
(`modules.c`) is solid for *built-in* modules but there's no story for
*user-published* Scheme libraries yet. Not re-litigating the design here —
flagging it as a maturity axis worth keeping on the roadmap, and noting that
Kaappi's answer (curated first-party monorepo-of-repos, §5 of the performance
doc) is worth weighting more now that there's a concrete example to compare
against CHICKEN/Akku/npm.

### 2.9 No CONTRIBUTING.md or issue templates

Minor, but zero-cost to fix: there's a `CODE_OF_CONDUCT.md`-shaped gap (no
`CONTRIBUTING.md`, no `.github/ISSUE_TEMPLATE/`). If curry ever wants outside
contributors, the entry cost right now is "read all of CLAUDE.md and infer
the workflow" — fine for one maintainer with an AI pair, a wall for anyone
else.

---

## 3. Recommended sequencing

Unlike the performance doc, most of these are independent and cheap — this
isn't a dependency chain, it's a priority-ordered backlog:

| Priority | Item | Effort | Why first/last |
|---|---|---|---|
| 1 | CI: build + ctest on push/PR | hours | Nothing else is trustworthy without this; blocks benchmark CI from the performance doc too |
| 2 | Error location + real `condition-backtrace` | days | Highest daily-impact fix; frame stack already exists, this is surfacing it |
| 3 | `CONFORMANCE.md` with real numbers | days | Cheap, high credibility payoff, no code changes required beyond running/counting |
| 4 | Syntax highlighting grammar | days | Cheap, immediate discoverability win, no server needed |
| 5 | Structured/coded diagnostics | 1–2 weeks, incremental | Pairs naturally with #2; do the top 20 error sites first |
| 6 | Minimal LSP (hover + go-to-def) | weeks | Wants #5's error codes and can reuse `,help`'s registry |
| 7 | REPL debugger (`,break`/`,step`/`,locals`) | weeks | Wants #2's frame-walking as foundation |
| 8 | Fuzzing harnesses (reader, numeric tower) | weeks, ongoing | High value given numeric-tower complexity; not blocking |
| 9 | CONTRIBUTING.md + issue templates | hour | Do whenever outside contribution becomes a goal |
| — | Package manager | deferred by design | Already tracked; survey first per existing decision |

---

## 4. One-line summary

Curry's *language* is more ambitious than Chez or Kaappi's; its *tooling
around the language* — CI, error locations, a debugger, conformance
numbers — is younger than either. The fastest path to "feels mature" is
items 1–4 above, all cheap, all independent, and all things a user notices
in their first ten minutes with curry.
