# Draft contribution to upstream SRFI-279

*Not yet submitted. Prepared 2026-08-15, ready to send if/when a maintainer contact confirms it's welcome.*

## What this is

A port of curry's own `(srfi 279)` implementation (`lib/curry/modules/srfi/s279/inspect.scm`) into the flat, `include`-able format the upstream [`scheme-requests-for-implementation/srfi-279`](https://github.com/scheme-requests-for-implementation/srfi-279) repository uses for per-implementation files (see `chibi.scm`/`guile.scm`/`kawa.scm` in that repo's own `srfi/` directory — full native implementations, not thin adapters over the reference `generic.scm`, so curry's own from-scratch approach fits the existing pattern).

- `curry.scm` — the port itself. Functionally equivalent to curry's own `inspect.scm`, minus the `define-library` wrapper and curry-specific header comments, plus an MIT license header matching the target repo's own convention (curry itself is GPL-3.0, but this is new glue code, not a copy of curry's core, so MIT here is legitimate and necessary for the target repo to accept it).
- `279.sld.diff` — the corresponding addition to `279.sld`'s `cond-expand`, wiring a new `curry` clause (curry's own `(features)` identifier) to the imports it needs and an `(include "curry.scm")`.

## Why not submitted yet

SRFI-279 is still in **draft** status. The upstream repo's own `README.org` says explicitly: *"If you'd like to participate in the discussion of this SRFI, or report issues with it, please join the SRFI-279 mailing list and send your message there."* GitHub Issues are disabled on that repo, and the existing per-implementation files (chibi/guile/kawa) all carry the SRFI author's own copyright — there's no established "other implementers submit a PR" precedent to point to, even though PRs are technically enabled. The socially/procedurally correct first step is introducing curry on the SRFI-279 mailing list and asking whether a `curry.scm` port would be welcome, not opening a cold PR.

## Verification performed

Built a temporary `define-library` locally, `include`-ing `curry.scm` with exactly the imports `279.sld.diff` proposes, and confirmed correct `inspect-properties`/`inspect-describe` output for: numbers, pairs, strings, vectors, typed numeric vectors (`u8vector` via `(srfi 4)` and `f64vector` via the separate `(curry f64vector)` module), bytevectors, hash tables (`srfi 69`), boxes (`srfi 111`), char-sets (`srfi 14`), records, record-types, and procedures. Two required imports (`(srfi 1)` for `last-pair`/`every`, `(curry f64vector)`) weren't obvious from curry's own module and were only found by actually running it against this exact include shape — see `279.sld.diff`'s own notes.

## Next step

Send an introduction to the SRFI-279 mailing list (linked from https://srfi.schemers.org/srfi-279/) describing curry and asking whether a `curry.scm` port in this shape is wanted — these two files are ready to attach/reference once that's confirmed.
