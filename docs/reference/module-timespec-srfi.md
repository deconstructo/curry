# `(srfi s174 posix-timespecs)`

*unreleased*

[SRFI-174](https://srfi.schemers.org/srfi-174/) POSIX timespecs: a small, immutable `(seconds nanoseconds)` time-instant type shared across SRFIs that need one (e.g. [SRFI-19](https://srfi.schemers.org/srfi-19/) time/date, if implemented later), so they don't each define their own incompatible variant. Pure Scheme, no C.

## Import

```scheme
(import (srfi s174 posix-timespecs))
```

## Procedures

### `(timespec seconds nanoseconds)` → *timespec*

`seconds` must be an exact integer (may be negative — an instant before some epoch); `nanoseconds` must be an exact integer in `[0, 10⁹)`. Raises an error otherwise, rather than silently normalizing an out-of-range value.

### `(timespec? x)` → *boolean*
### `(timespec-seconds ts)` → *exact integer*
### `(timespec-nanoseconds ts)` → *exact integer*, in `[0, 10⁹)`

### `(inexact->timespec x)` → *timespec*

Converts a flonum number of seconds to a timespec. Floors toward negative infinity, so a negative input still produces a valid (non-negative) nanoseconds component: `(inexact->timespec -1.25)` gives seconds `-2`, nanoseconds `750000000` (i.e. `-2 + 0.75 = -1.25`), not seconds `-1` with a negative nanoseconds field.

### `(timespec->inexact ts)` → *flonum*

`seconds + nanoseconds/10⁹`. Lossy for values that don't round-trip exactly through IEEE double precision, same caveat as any exact-to-inexact conversion.

### `(timespec=? a b)` → *boolean*
### `(timespec<? a b)` → *boolean*

Compares seconds first, then nanoseconds.

### `(timespec-hash ts)` → *non-negative exact integer*

Equal timespecs always hash equally; the specific values aren't meant to match any other implementation's.

```scheme
(import (srfi s174 posix-timespecs))

(define t (timespec 1721000000 500000000))
(timespec-seconds t)        ; => 1721000000
(timespec->inexact t)       ; => 1721000000.5

(timespec<? (timespec 5 0) (timespec 5 1))  ; => #t

(let ((t (inexact->timespec -1.25)))
  (list (timespec-seconds t) (timespec-nanoseconds t)))  ; => (-2 750000000)
```
