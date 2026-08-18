# Maturity Gaps: What Chez and Kaappi Have That Curry Doesn't

**Document version 2 — 2026-08-18** (v1 was 2026-07-16). Verified against
curry v1.21.0 / `main`.

*2026-07-16, updated 2026-08-18 — companion to
[performance-chez-kaappi.md](performance-chez-kaappi.md), which covers
execution speed. This document covers everything else that makes an
implementation feel production-grade: debuggability, conformance confidence,
CI, tooling, and API stability.*

Curry's feature *breadth* already exceeds both reference points — no other
Scheme has a CAS, quantum values, Clifford algebra, and an LLM client built
in. The gap is not features, it's the scaffolding around them that lets
people trust and debug a large system. Findings below are verified against
`main`, not assumed.

**2026-08-18 update**: the July 16 version of this document is significantly
stale — five of its nine gaps (CI, error location/backtrace, LSP, syntax
highlighting, interactive debugger, CONTRIBUTING.md) have since shipped. This
revision re-verifies every item against current `main` and re-ranks what's
actually left.

---

## 1. What's already solid (don't relitigate)

- `CHANGELOG.md`, `VERSIONING.md`, semver git tags back to v1.4.2 — real
  release discipline.
- `docs/reference/` (44+ files) and `docs/guides/` are genuinely thorough for
  the exotic subsystems (CAS, multivectors, surreals, quantum).
- An explicit, documented "R7RS deviations" section in `language.md` —
  Kaappi's `CONFORMANCE.md` does the same thing and it's the right pattern;
  curry already has it.
- Deep domain-specific test suites: `numeric_ext_tests.scm` (354 assertions),
  `sicm_tests.scm` (172), `symbolic_tests.scm` (111) — this is coverage Chez
  and Kaappi don't have because they don't have the subsystems.
- **CI that builds and tests on every push/PR** (`.github/workflows/ci.yml`):
  a real `ubuntu-latest`/`macos-latest` × `Debug`/`Release` matrix running
  `cmake --build && ctest`, plus a separate `benchmark.yml` and `codeql.yml`.
  This was gap #1 in the July version — now done and was the prerequisite
  everything else below builds trust on.
- **Errors carry file:line and a call chain by default**:
  ```
  $ curry /tmp/errtest.scm
  Error [wrong-type-argument]: 𒀭 ḫiṭītu — lā qitnum:
    car: not a pair
    at f (/tmp/errtest.scm:1)
    at <toplevel> (/tmp/errtest.scm:1)
  ```
  `condition-backtrace` is no longer a stub — it returns the real captured
  frame stack off the error object. This was the July version's
  "single highest-leverage fix"; it shipped. One residual gap: the comment
  at the implementation site notes user-signalled `Condition` objects
  (`make-condition`, not a raised error) "don't capture frames yet" — worth
  closing for parity, but the common case (an actual error) is fully fixed.
- **A real interactive debugger**: gdb-style, breakpoints by function name
  or `file:line` (`,break` in the REPL, `-b` on the CLI, `(breakpoint)` in
  source), step/next/finish/continue, `bt`, `locals` (including captured
  upvalues), `p <expr>` — see `docs/reference/debugger.md`. This directly
  matches what the July version flagged Kaappi as having and curry lacking.
- **An LSP server and editor tooling**: `(curry lsp)` (`modules/lsp/lsp.c`)
  implements `textDocument/hover` and `textDocument/completion` over
  Content-Length-framed stdio, with reader-driven diagnostics and a
  nesting-depth crash guard (27 assertions in `tests/test_lsp.sh`). Syntax
  highlighting exists for both Vim (`editors/vim/`: syntax, ftdetect,
  ftplugin) and VS Code (`editors/vscode/`: a real extension —
  `package.json`, `language-configuration.json`, a `.tmLanguage.json`
  grammar), not just a bare grammar file. Not yet done: go-to-definition
  (LSP has hover+completion only) and publishing the VS Code extension
  somewhere findable (Marketplace/Open VSX) — see §2.3 below.
- **`CONTRIBUTING.md` and GitHub issue templates** (`.github/ISSUE_TEMPLATE/
  bug_report.md`, `feature_request.md`, `config.yml`) both exist.

## 2. The gaps, ranked by how often they'll bite someone

### 2.1 Structured/coded diagnostics are ~17% done (highest priority now)

`docs/reference/error-codes.md` documents a real mechanism (`scm_raise_code`
stamps a stable symbol like `wrong-type-argument` onto an error object,
readable via `(error-object-code e)`/`(condition-code e)`, shown by the
REPL/script printer as `Error [wrong-type-argument]: ...`) — this is not
vaporware, it works today exactly as documented. But coverage is thin:
**107 of 633 total raise sites use a code (~17%)** — the doc's own words,
"most of them" (raised without a code) is accurate. `(error-object-code e)`
returns `#f` for the ~83% majority.

This is now the highest-leverage remaining item for the same reason error
location was in July: it's something every programmatic consumer of curry's
errors (a future LSP diagnostic, the MCP module, the LLM client, any linter)
hits immediately, and the mechanism to fix it already exists — this is
"add codes to more call sites," not new infrastructure.

**What's needed:** work through `src/*.c`'s remaining ~526 uncoded
`scm_raise(...)` sites, starting with the highest-traffic ones (type errors
in `builtins.c`, arity errors, unbound-variable) — same incremental
approach `error-codes.md` itself already recommends.

### 2.2 R7RS conformance still isn't quantified

Unchanged from July: `docs/reference/language.md` documents deviations
qualitatively but there's still no `CONFORMANCE.md` and no published number
anywhere ("curry passes N/M of R7RS Appendix A", per-SRFI percentages).
`tests/r7rs_tests.scm` has grown from 168 to **314 assertions** since July,
real progress, but still an order of magnitude below what a dedicated
conformance suite would cover, and growing the test file isn't the same as
publishing a number anyone can cite.

**What's needed:** unchanged — expand toward Appendix A coverage (or adopt
a public R7RS test suite) and publish a `CONFORMANCE.md` with per-SRFI
percentages, mirroring the "R7RS deviations" pattern that already works
well in `language.md`.

### 2.3 LSP go-to-definition + published editor extension

Partially addressed since July (see §1) — hover and completion work, syntax
highlighting exists for Vim and VS Code. What's left: `textDocument/
definition` isn't implemented (only hover/completion are wired into the LSP
dispatch), and the VS Code extension isn't confirmed published anywhere
(Marketplace/Open VSX) — it exists as source in `editors/vscode/` but a user
would need to build/side-load it today, not install it.

**What's needed:** add a `textDocument/definition` handler (same
reader-driven approach hover already uses, resolving a symbol to its
`define`'s source location instead of its docstring) and publish the VS
Code extension.

### 2.4 No fuzzing

Unchanged from July, still zero: no `libFuzzer`/AFL harness anywhere in the
tree. Neither the reader, the expander, nor the numeric tower (GMP-backed,
lots of promotion-boundary edge cases per CLAUDE.md's own review checklist —
"off-by-one errors in sexagesimal/numeric code") is fuzz-tested. Given
curry's numeric tower has more promotion boundaries than almost any other
Scheme (fixnum → bignum → rational → flonum → complex → quaternion →
octonion → multivector → surreal → symbolic), this remains a strong
candidate for harnesses targeting `reader.c`, `numeric.c` promotion logic,
and the sexagesimal reader — exactly the code CLAUDE.md already flags for
extra review scrutiny.

**What's needed:** unchanged — `libFuzzer`/AFL harnesses for the three areas
above, plus a `docs/dev/fuzzing.md` documenting how to run them, matching
Kaappi's own pattern.

### 2.5 condition-backtrace gap for user-signalled Condition objects

New, narrower item split out of what used to be §2.1: a raised *error*
captures a full backtrace (see §1), but a user-signalled `Condition` object
(via `make-condition`, the CL-style condition system, not an R7RS `error`)
does not yet. Small, well-scoped fix now that the frame-capture
infrastructure for the error case already exists and works.

**What's needed:** capture the same frame stack at `(signal ...)`/condition
construction time that error-raising already does, and wire it into
`condition-backtrace` for that path too.

### 2.6 No package manager, still deferred

Unchanged from July: recorded in memory as an open item
(`project_pkg_design_deferred`) — a comparative survey was wanted before
implementing, and the survey (`docs/thoughts/package-management-design.md`,
which supersedes the older `docs/guides/pkg-design.md`) is done, but no
implementation exists yet. Restating it here because it's a maturity axis
both reference points score on: Kaappi ships `thottam` with ~20 first-party
libraries; Chez has an ecosystem via Racket/Chicken-adjacent tooling and
R6RS libraries. Curry's C module system (`modules.c`) is solid for
*built-in* modules but there's still no story for *user-published* Scheme
libraries. Not re-litigating the design here — flagging it as a maturity
axis worth keeping on the roadmap.

---

## 3. Recommended sequencing

Most of what was sequenced in July is now done. What remains is a shorter,
mostly-independent backlog:

| Priority | Item | Effort | Why first/last |
|---|---|---|---|
| 1 | Structured diagnostics: widen code coverage past ~17% | 1–2 weeks, incremental | Mechanism already exists and is documented; highest-leverage remaining item, same reasoning error-location had in July |
| 2 | `CONFORMANCE.md` with real numbers | days | Cheap, high credibility payoff, no code changes required beyond running/counting |
| 3 | LSP go-to-definition + publish the VS Code extension | days–1 week | Small addition on an already-working LSP; publishing is zero-code, pure distribution |
| 4 | `condition-backtrace` for user-signalled Conditions | days | Small, well-scoped, closes the one residual gap in an otherwise-shipped fix |
| 5 | Fuzzing harnesses (reader, numeric tower) | weeks, ongoing | High value given numeric-tower complexity; not blocking |
| — | Package manager | deferred by design | Survey done (`docs/thoughts/package-management-design.md`); implementation not started |

**Done since July** (no longer on this list): CI matrix, error location +
real `condition-backtrace` for errors, interactive debugger, LSP
(hover/completion), syntax highlighting (Vim + VS Code), CONTRIBUTING.md +
issue templates.

---

## 4. One-line summary

The July gap list front-loaded the items that mattered most for a user's
first ten minutes with curry — CI, error location, a debugger, editor
support — and all of those shipped. What's left skews toward *credibility
with someone comparing implementations* (a published conformance number,
codes on most errors, fuzzing) rather than day-one friction, which is a
meaningfully more mature place to be than five weeks ago.
