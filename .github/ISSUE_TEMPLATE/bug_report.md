---
name: Bug report
about: Something in curry doesn't behave the way the docs/spec say it should
title: ""
labels: bug
assignees: ""
---

## What happened

A clear description of the bug.

## Minimal repro

The smallest `.scm` snippet (or `curry -e '...'` one-liner) that reproduces
it. If it depends on an optional module (`-DBUILD_MODULE_X=ON`) or a
running service (Postgres, MariaDB, Redis, etc.), say so.

```scheme
;; paste here
```

## Expected vs. actual

- Expected:
- Actual:

## Environment

- Curry version (`curry -v`):
- OS/arch:
- Build flags used (if built from source), or "Homebrew" / "Homebrew --HEAD":
- GC backend, if relevant (`--gc boehm` / `--gc generational`):

## Anything else

Stack trace, `--timings` output, or anything else that might help — paste
as-is, don't summarize it away.
