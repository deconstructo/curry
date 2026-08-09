# Module: (curry dot-locking)

*unreleased*

Advisory file locking via the dot-locking scheme, pure Scheme, built on [`(curry posix)`](module-posix.md) and SRFI 18's `thread-sleep!`.

Ported from the API documented by the CHICKEN Scheme [`dot-locking` egg](https://wiki.call-cc.org/eggref/5/dot-locking) (algorithm originally by Olin Shivers, CHICKEN port by felix winkelmann, BSD-licensed) — this module reimplements the documented behavior from scratch against curry's own `(curry posix)` primitives, not a port of that egg's actual source.

## Import

```scheme
(import (curry dot-locking))
```

## How it works

The lock for a file named `file-name` is represented by a second file, `file-name.lock`. Obtaining the lock uses the classic "unique temp file, then `link(2)` it into place" technique: a process-and-call-uniquely-named temp file is created first, then `link(2)`'d to the lock path. `link(2)` fails if the target already exists, and — unlike `open(2)` with `O_EXCL`, historically unsafe on some NFS implementations — this check-and-create is atomic even across NFS clients, which is the entire reason this scheme exists instead of a plain exclusive-create. Whether or not the link succeeds, the temp file is deleted immediately afterward; only the lock file itself persists while the lock is held.

`(curry posix)` has no structured errno introspection (see that module's own "Deliberately not implemented" notes) — every failure surfaces as an ordinary condition carrying `strerror()` text. This module distinguishes "the lock is already held by someone else" (`create-hard-link` failing because the target already exists) from any other failure (a permission error, a full disk, ...) by matching that text, which is the only signal available; an unrecognized failure is re-raised rather than silently treated as "lock busy".

## API

### `(obtain-dot-lock file-name)`
### `(obtain-dot-lock file-name interval)`
### `(obtain-dot-lock file-name interval retry-number)`
### `(obtain-dot-lock file-name interval retry-number stale-time)`

Tries to obtain the lock for `file-name`. If the file is already locked, sleeps for `interval` seconds (default `1`, via `thread-sleep!`) before retrying. If the lock can't be obtained after `retry-number` attempts, returns `#f`; `retry-number` defaults to `#f`, an infinite number of retries.

If `stale-time` is non-`#f` (default `300`), it's the minimum age (in seconds, by the lock file's own mtime) a lock may have before it's considered stale; a stale lock is broken (deleted) and immediately retried. If obtaining the lock succeeds after breaking one this way, returns `'broken` rather than `#t`. Pass `stale-time` `#f` to never consider a lock stale.

It's possible for a lock to be broken here but for the immediate retry to still fail (another process won the race) — use `break-dot-lock` directly instead if that case needs handling specially.

```scheme
(import (curry dot-locking))
(obtain-dot-lock "/var/mail/alice")            ; #t, 'broken, or #f
(obtain-dot-lock "/var/mail/alice" 0.5 10)     ; 10 retries, 0.5s apart, then give up
(obtain-dot-lock "/var/mail/alice" 1 #f #f)    ; retry forever, never consider it stale
```

### `(release-dot-lock file-name)` → boolean

Releases the lock for `file-name`. `#t` on success, `#f` otherwise. Also usable to break the lock — `release-dot-lock` and `break-dot-lock` do the same thing.

### `(break-dot-lock file-name)` → boolean

Breaks the lock for `file-name` if one exists (`#t`), or `#f` if there was none to break. Breaking a lock does not imply a subsequent `obtain-dot-lock` will succeed, since another process may acquire it in between.

### `(with-dot-lock* file-name thunk)`

Obtains the lock, calls `(thunk)`, and releases the lock when `thunk` returns — or on a non-local exit (an escaping continuation invocation, or a raised condition), via `dynamic-wind`. Returns `thunk`'s own values on a normal return. Raises if the lock can't be obtained at all.

Uses `obtain-dot-lock`'s own defaults internally (there's no way to pass `interval`/`retry-number`/`stale-time` through this 2-argument form) — with the default `retry-number` of `#f`, this means `with-dot-lock*` blocks until the lock becomes available (or a stale one is broken) rather than ever giving up.

### `(with-dot-lock file-name body ...)`

Syntactic sugar for `(with-dot-lock* file-name (lambda () body ...))`.

```scheme
(import (curry dot-locking))
(with-dot-lock "/var/mail/alice"
  (append-message-to-mailbox! "/var/mail/alice" msg))
```

## See also

- [`module-posix.md`](module-posix.md) — `create-hard-link`, `file-info`, `machine-name`, `pid` this module is built on
