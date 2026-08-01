# SRFI Compatibility: `(srfi sN …)`

Curry ships a set of pure-Scheme SRFI compatibility libraries under the `(srfi sN name)` namespace — a naming convention used by several Scheme implementations so that the same `(import …)` line works across them.

**Full documentation has moved to [`srfi/index.md`](srfi/index.md)**, which lists all 24 supported SRFIs with a link to each one's own reference page under [`docs/reference/srfi/`](srfi/).

```scheme
(import (srfi s1 lists))   ; or, equivalently:
(import (srfi 1))
```

See [`srfi/index.md`](srfi/index.md) for the full availability table, the bare-number `(srfi N)` shim convention, and the portability note.
