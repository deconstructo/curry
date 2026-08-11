# Contributing to Curry

Outside contributions are welcome! Help me build a quirky but useful scheme
that allows you to do strange things that you may not be able to do with
other schemes!

## Before you start

For anything bigger than a small, obvious fix, open an issue first (or
comment on an existing one) describing what you want to change and why.
This avoids wasted work on a patch that doesn't fit the project's direction
— curry has a deliberately opinionated design (pluggable logics, an
open-ended numeric tower, no single "correct" answer treated as bedrock —
see the [README](README.md)'s intro for the short version), and some things
that look like gaps are intentional.

Good first contributions: fixing a documented bug, adding a missing SRFI
procedure, expanding test coverage, improving a `docs/reference/` page that's
out of date relative to the code, adding an Akkadian/cuneiform alias for an
existing procedure.

Bigger contributions (a new module, a language-level feature, a GC/VM
change) should start as a design discussion in an issue before any code is
written.

## Building

```bash
# Configure (Debug — use this while developing)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)                  # Linux
cmake --build build -j$(sysctl -n hw.logicalcpu) # macOS

# Run the REPL
./build/curry
```

Required dependencies: `libgc` (Boehm GC), `libgmp`, pthreads, CMake ≥ 3.20,
a C11 compiler. See [`docs/guides/INSTALL.md`](docs/guides/INSTALL.md) for
the full dependency list, optional module flags (`-DBUILD_MODULE_*`), and
platform-specific instructions (Homebrew on macOS, apt on Debian/Ubuntu).

If your change touches a module gated behind a `-DBUILD_MODULE_X=ON` flag or
`-DBUILD_FFI=ON`, configure with that flag on before building — see
[`CLAUDE.md`](CLAUDE.md#build) for the full configure line covering every
optional module.

## Testing

```bash
cmake --build build && ctest --test-dir build -V
```

Run this before opening a PR — CI runs it too, but catching a break locally
is faster for everyone. To run a single suite while iterating:

```bash
./build/curry_test                          # C unit tests
./build/curry tests/r7rs_tests.scm           # one Scheme suite
```

See the table in [`CLAUDE.md`](CLAUDE.md#tests) for what each registered
`ctest` suite covers. If you add a new `.scm` test file, register it in
`CMakeLists.txt` so `ctest` actually runs it.

**Add tests for what you change.** A bug fix should include a test that
would have failed before the fix; a new procedure should include at least
the basic success/error-path cases in the relevant `tests/*.scm` file.

## Code style

- **C**: match the surrounding file's style rather than introducing a new
  one. No compiler warnings on a clean build.
- **Scheme**: default to *no comments* — well-named identifiers should carry
  the meaning. Only comment a genuinely non-obvious *why* (a hidden
  constraint, a workaround for a specific upstream bug, something that would
  surprise a careful reader) — never restate *what* the code does. Look at
  any recently-touched file in `lib/curry/modules/curry/` for the house
  style before writing a new module.
- Every pure-Scheme `(curry X)` module should be a `define-library` form —
  see [`docs/reference/writing-a-module.md`](docs/reference/writing-a-module.md)
  for the pattern and its one real gotcha (non-hygienic macro exports across
  library boundaries).
- Don't add abstractions, config flags, or generality beyond what the
  current change actually needs.

## Review focus for C or Scheme changes

Two things get scrutinized especially closely in this codebase, because
they've bitten it before:

- **Array bounds vs. loop bounds**, and **off-by-one errors in numeric or
  struct-offset code** — the numeric tower has a lot of promotion-boundary
  edge cases (fixnum → bignum → rational → flonum → complex → quaternion →
  octonion → multivector → surreal → symbolic), and a few modules read raw C
  struct fields at hand-computed byte offsets via FFI (no struct binding) —
  get the offset arithmetic checked against the actual header, not just
  historical documentation.

- **Cuneiform / sexagesimal reader edge cases** — base-60 I/O and the
  cuneiform Unicode reader are easy places to get an edge case wrong.

If your PR touches either area, say so in the PR description and expect
(or ask for) a close read of that specific code, not just a skim.

## Commit messages

Recent history uses a loose `type(scope): summary` convention —
`feat(mariadb): ...`, `fix(postgres): ...`, `docs(rpi): ...`,
`chore(formula): ...` — with a body explaining *why*, not a restatement of
the diff. Match it. Squash noisy work-in-progress commits before opening a
PR; keep the history bisectable, not a play-by-play.

## Opening a PR

- Target `main`.
- Make sure `ctest --test-dir build` passes.
- Update the relevant `docs/reference/` or `docs/guides/` page in the same
  PR if your change adds or changes user-visible behavior — a feature
  without a doc update isn't done.
- If you added a new `(curry X)` module, add it to
  [`docs/reference/modules.md`](docs/reference/modules.md)'s index.
- Describe *why* in the PR description, not just *what* — the same standard
  the commit messages are held to.

## License

Curry is licensed under GPL-3.0-only (see [`LICENSE`](LICENSE)). By
submitting a contribution, you agree it's licensed under the same terms.

## Questions

Open a [discussion](https://github.com/deconstructo/curry/discussions) or an
issue with the `question` label if you're not sure whether something's a bug,
a missing feature, or intentional.
