# SRFI Compatibility: `(srfi sN …)`

Curry ships a set of pure-Scheme SRFI compatibility libraries under the `(srfi sN name)` namespace — a naming convention used by several Scheme implementations so that the same `(import …)` line works across them. The libraries delegate to Curry's built-ins wherever the procedure is already present; only missing names are defined in Scheme.

## Bare-number `(srfi N)` shims

The naming convention SRFI 97 itself specifies — and the one Chibi-Scheme and Gauche actually implement — is a bare-number library name with no descriptive component: `(import (srfi 1))`, not `(import (srfi s1 lists))`. For every library below, curry also ships a one-line shim `lib/curry/modules/srfi/N.scm` that just imports the `(srfi sN name)` library and re-exports everything it exports, so both spellings work:

```scheme
(import (srfi 1))          ; same bindings as...
(import (srfi s1 lists))   ; ...this
```

`(srfi N)` library names use an *exact non-negative integer* as the second component, which R7RS's library-name grammar explicitly permits alongside identifiers. Note for combining multiple `(srfi N)` imports in one program: several of these libraries deliberately share generic names on purpose (`(srfi 69)`/`(srfi 90)` and `(srfi 125)`/`(srfi 126)` both touch hash tables, `(srfi 113)` touches sets) — if two imported libraries export the *same* name with different behavior (e.g. `(srfi 69)`'s and `(srfi 125)`'s both-named `make-hash-table`), only one wins in that flat top-level scope, same as any Scheme's colliding-import behavior. `(srfi 125)`/`(srfi 126)` avoid this with each other (`hash-table-*` vs `hashtable-*`), so they're safe to combine with each other, just not with `(srfi 69)`/`(srfi 90)`.

## `(srfi srfi-N)` shims

[SRFI-261](https://srfi.schemers.org/srfi-261/) (finalized 2025-12-07) specifies `(srfi srfi-N)` as *the* portable library-name form going forward — distinct from both the bare `(srfi N)` shorthand above (which SRFI-261 also documents, as a restricted alternative) and curry's own descriptive `(srfi sN name)` naming, and silent on the latter. Curry ships a `(srfi srfi-N)` shim for every library below, identical in shape to the `(srfi N)` shims (same file, same one-line re-export, just one more library-name segment):

```scheme
(import (srfi srfi-1))     ; same bindings as...
(import (srfi 1))          ; ...this, and...
(import (srfi s1 lists))   ; ...this
```

The same import-collision caveat as `(srfi N)` above applies identically here (it's the same underlying `(srfi sN name)` library either way).

## Available `(srfi sN name)` libraries

| Library | SRFI | Description |
|---------|------|-------------|
| [`(srfi s1 lists)`](s1.md) | [SRFI-1](https://srfi.schemers.org/srfi-1/) | List library |
| [`(srfi s8 receive)`](s8.md) | [SRFI-8](https://srfi.schemers.org/srfi-8/) | `receive` multiple-values binding macro — shadows curry's own actor `receive` special form when imported |
| [`(srfi s14 char-sets)`](s14.md) | [SRFI-14](https://srfi.schemers.org/srfi-14/) | Character sets — construction, iteration, set algebra, and the standard predefined sets (`char-set:letter`, `char-set:whitespace`, etc), backed by a sorted-range representation over curry's full Unicode codepoint space |
| [`(srfi s18 multithreading)`](s18.md) | [SRFI-18](https://srfi.schemers.org/srfi-18/) | Thread/mutex/condition-variable naming over curry's actor `spawn` and `(curry sync)` |
| [`(srfi s19 time)`](s19.md) | [SRFI-19](https://srfi.schemers.org/srfi-19/) | Time/date objects, Julian Day conversions, `strftime`-style formatting — requires `-DBUILD_MODULE_POSIX=ON` (default) for `current-time` |
| [`(srfi s26 cut)`](s26.md) | [SRFI-26](https://srfi.schemers.org/srfi-26/) | `cut`/`cute` — partial application without writing `lambda` by hand; the reference implementation verbatim (its recursive-macro shape found and drove a real curry `syntax-rules` hygiene fix — see the doc page) |
| [`(srfi s27 random-bits)`](s27.md) | [SRFI-27](https://srfi.schemers.org/srfi-27/) | Random-number sources |
| [`(srfi s54 cat)`](s54.md) | [SRFI-54](https://srfi.schemers.org/srfi-54/) | `cat` — order-independent object-to-string formatting (width, padding, precision, radix, separators, pipes, converters) |
| [`(srfi s59 vicinity)`](s59.md) | [SRFI-59](https://srfi.schemers.org/srfi-59/) | Vicinity (directory-of-a-path) string utilities |
| [`(srfi s64 testing)`](s64.md) | [SRFI-64](https://srfi.schemers.org/srfi-64/) | Unit-testing framework — test cases/groups, skip/expect-fail specifiers, pluggable test runners; curry's own test suites use this |
| [`(srfi s69 hash-tables)`](s69-s90.md) | [SRFI-69](https://srfi.schemers.org/srfi-69/) | Basic hash tables — full API, wrapping curry's built-in hash table with corrected `hash-table-ref` semantics |
| [`(srfi s90 hash-tables)`](s69-s90.md) | [SRFI-90](https://srfi.schemers.org/srfi-90/srfi-90.html) | `make-table`, a keyword-argument hash-table constructor layered on `(srfi s69 hash-tables)` |
| [`(srfi s98 os-environment-variables)`](s98.md) | [SRFI-98](https://srfi.schemers.org/srfi-98/) | `get-environment-variable(s)` — thin re-export of native builtins |
| [`(srfi s111 boxes)`](s111.md) | [SRFI-111](https://srfi.schemers.org/srfi-111/) | Single-slot mutable box — `box`/`box?`/`unbox`/`set-box!` |
| [`(srfi s112 environment-inquiry)`](s112.md) | [SRFI-112](https://srfi.schemers.org/srfi-112/) | Implementation/OS/machine identity queries — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |
| [`(srfi s113 sets-and-bags)`](s113.md) | [SRFI-113](https://srfi.schemers.org/srfi-113/) | Sets — comparator-adapter wrapper over curry's native set; bags — pure-Scheme multiset on a comparator-adapted hash table |
| [`(srfi s125 hash-tables)`](s125-s126.md) | [SRFI-125](https://srfi.schemers.org/srfi-125/) | Comparator-keyed "intermediate" hash tables, layered on curry's native eq?/eqv?/equal? table |
| [`(srfi s126 hashtables)`](s125-s126.md) | [SRFI-126](https://srfi.schemers.org/srfi-126/) | R6RS-style `hashtable-*` naming, layered on `(srfi s125 hash-tables)` |
| [`(srfi s128 comparators)`](s128.md) | [SRFI-128](https://srfi.schemers.org/srfi-128/) | Comparator type, basic-type comparators, simplified compound pair/list/vector comparators, `default-comparator` |
| [`(srfi s132 sorting)`](s132-s133.md) | [SRFI-132](https://srfi.schemers.org/srfi-132/) | `list-sort`/`vector-sort`(`!`) — every variant is the same stable merge sort |
| [`(srfi s133 vectors)`](s132-s133.md) | [SRFI-133](https://srfi.schemers.org/srfi-133/) | Vector extras layered on curry's native vector ops: fold, index/count/any/every, binary-search, concatenate, unfold |
| [`(srfi s145 assume)`](s145.md) | [SRFI-145](https://srfi.schemers.org/srfi-145/) | `assume` runtime invariant declaration (always checked, never elided) |
| [`(srfi s158 generators-and-accumulators)`](s158.md) | [SRFI-158](https://srfi.schemers.org/srfi-158/) | Generator/accumulator combinators; `make-coroutine-generator` via a real thread |
| [`(srfi s170 posix)`](s170.md) | [SRFI-170](https://srfi.schemers.org/srfi-170/) | POSIX API (subset) — thin re-export of `(curry posix)`, requires `-DBUILD_MODULE_POSIX=ON` (default) |
| [`(srfi s174 posix-timespecs)`](s174.md) | [SRFI-174](https://srfi.schemers.org/srfi-174/) | Immutable `(seconds nanoseconds)` time-instant type |
| [`(srfi s194 random-data-samples)`](s194.md) | [SRFI-194](https://srfi.schemers.org/srfi-194/) | Random integer/real/boolean/char generators plus distribution samplers |
| [`(srfi s195 multiple-value-boxes)`](s195.md) | [SRFI-195](https://srfi.schemers.org/srfi-195/) | Multiple-value boxes — extends `(srfi s111 boxes)` with `box-arity`/`unbox-value`/`set-box-value!` |
| [`(srfi s209 enums)`](s209.md) | [SRFI-209](https://srfi.schemers.org/srfi-209/) | Enums and enum sets — typed, ordered symbolic constants with a name/ordinal/value, grouped into disjoint enum types; `define-enum`/`define-enumeration` sugar |
| [`(srfi s210 multiple-values)`](s210.md) | [SRFI-210](https://srfi.schemers.org/srfi-210/) | Multiple-values convenience syntax/procedures layered on `call-with-values`: `apply/mv`, `call/mv`, `bind/mv`, `case-receive`, `set!-values`, and more |
| [`(srfi s215 log)`](s215.md) | [SRFI-215](https://srfi.schemers.org/srfi-215/) | Central log exchange |
| [`(srfi s227 optional-arguments)`](s227.md) | [SRFI-227](https://srfi.schemers.org/srfi-227/) | `opt-lambda`, `let-optionals`/`let-optionals*`, `default-object`/`default-object?` — spelled with `#:optional`/`#:rest`, not `#!optional`/`#!rest` |
| [`(srfi s238 codesets)`](s238.md) | [SRFI-238](https://srfi.schemers.org/srfi-238/) | `errno`/`signal`/`http-status` code ⟷ symbol ⟷ message lookup — thin re-export of `(curry codesets)`, requires `-DBUILD_MODULE_CODESETS=ON` (default) |
| [`(srfi s252 property-testing)`](s252.md) | [SRFI-252](https://srfi.schemers.org/srfi-252/) | Property-based testing layered on `(srfi s64 testing)`: `test-property` and friends, plus a fixed suite of type/number generators |
| [`(srfi s263 prototype-objects)`](s263.md) | [SRFI-263](https://srfi.schemers.org/srfi-263/) | Self-inspired prototype/message-passing object system — complements, doesn't replace, `(curry oop)` |
| [`(srfi s279 inspect)`](s279.md) | [SRFI-279](https://srfi.schemers.org/srfi-279/) (draft) | `inspect-properties`/`inspect-describe` generic introspection protocol — curry-native implementation, not a port of the SRFI's own reference code (which depends on four other SRFIs curry doesn't have) |

## Portability note

Code written against any library above is compatible with Guile, Chicken (via the `srfi-egg`), Chibi-Scheme, and other implementations that follow the same `(srfi sN name)`/`(srfi N)` naming convention. The only two deviations are [`(srfi s27 random-bits)`](s27.md), which uses xoshiro256+ internally rather than the Mersenne Twister typically found in other implementations (statistically equivalent or better, and invisible from the API), and [`(srfi s227 optional-arguments)`](s227.md), which spells its markers `#:optional`/`#:rest` rather than `#!optional`/`#!rest` because curry's reader can't read the latter.
