# Module: (curry codesets)

*unreleased*

[SRFI-238](https://srfi.schemers.org/srfi-238/) codesets: a unified interface for translating between numeric codes and symbolic mnemonics, plus human-readable messages, across `errno`, POSIX signals, and HTTP status codes. Pure system libc (`<errno.h>`, `<signal.h>`, `strerror`/`strsignal`); no external dependency, macOS/Linux portable. Built by default (`-DBUILD_MODULE_CODESETS=ON`).

A portable re-export under the SRFI's own naming convention is available as `(surfage s238 codesets)` — see [`module-surfage.md`](module-surfage.md).

## Import

```scheme
(import (curry codesets))
```

## Codesets

Three codesets are known: `'errno`, `'signal`, `'http-status`.

`errno` and `signal` symbol *names* and their numeric *values* come straight from this platform's own `<errno.h>`/`<signal.h>` macros at build time — the set of known codes and their spelling is fixed, but the actual number behind e.g. `ENOTSUP` can (and does) differ between macOS and Linux, exactly as it does in C. `http-status` is a static table of standard HTTP status codes (100–511) with no platform dependency, since it isn't a libc concept — its symbols are kebab-case (`'not-found`, `'internal-server-error`), not the numeric-code style of the other two.

## Procedures

### `(codeset? x)` → *boolean*

`#t` if `x` is one of the known codeset symbols (`'errno`, `'signal`, `'http-status`).

### `(codeset-symbols codeset)` → *list of symbols*

Every known symbol in `codeset`. Raises an error if `codeset` isn't recognized.

### `(codeset-symbol codeset code)` → *symbol* | `#f`

Converts a numeric `code` to its symbolic name, or passes an already-valid symbol through. Returns `#f` for an unrecognized code.

### `(codeset-number codeset code)` → *exact integer* | `#f`

Converts a symbolic `code` to its number, or passes an already-valid number through. Returns `#f` for an unrecognized code.

### `(codeset-message codeset code)` → *string* | `#f`

A human-readable message for `code` (symbol or number) — `strerror(3)` for `errno`, `strsignal(3)` for `signal`, the standard reason phrase for `http-status`. Returns `#f` if `code` isn't recognized.

```scheme
(import (curry codesets))

(codeset-symbol 'errno 2)              ; => ENOENT
(codeset-message 'errno 'ENOENT)       ; => "No such file or directory"

(codeset-symbol 'signal 9)             ; => SIGKILL
(codeset-message 'signal 'SIGSEGV)     ; => "Segmentation fault" (exact wording is platform-dependent)

(codeset-symbol 'http-status 404)      ; => not-found
(codeset-number 'http-status 'not-found) ; => 404
(codeset-message 'http-status 500)     ; => "Internal Server Error"

(codeset? 'errno)                      ; => #t
(codeset-symbol 'errno 999999)         ; => #f
```

## Notes

- `errno`/`signal` cover the POSIX.1-2008 base codes common to macOS and Linux, plus a handful of platform-conditional ones (`ENODATA`, `ENOSR`, `ENOSTR`, `ETIME`, `EOPNOTSUPP`, `EWOULDBLOCK`, `SIGPOLL`, `SIGIO`) included only when this platform's headers actually define them (and, for `EWOULDBLOCK`, only when it isn't just an alias of `EAGAIN`) — per the SRFI, `codeset-symbols` is expected to return "as many as are known," not necessarily every code that exists anywhere.
- This complements `(curry posix)`, whose errors already embed `strerror()` text but don't expose a structured errno symbol/number lookup on their own.
