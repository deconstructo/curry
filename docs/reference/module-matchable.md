# Module: `(curry matchable)`

*unreleased*

Pattern matching, ported from [Alex Shinn's public-domain `match.scm`](https://synthcode.com/scheme/match.scm) — the portable, pure-`syntax-rules` implementation distributed as CHICKEN's `matchable` egg, Chibi's `(chibi match)`, Guile's `(ice-9 match)`, and others. Pure Scheme, no build step.

## Import

```scheme
(import (curry matchable))
```

## Supported patterns

| Pattern | Matches |
|---|---|
| a literal (number/string/boolean/char) | that exact value, via `equal?` |
| `'x` | that exact literal, via `equal?` |
| `_` | anything, binds nothing |
| `id` | anything; binds `id`. A *repeated* occurrence of the same identifier elsewhere in the pattern must `equal?` the first (non-linear patterns) |
| `(p1 p2 p3)` | a list of exactly that length |
| `(p1 . p2)` | a pair — `p1` against the car, `p2` against the cdr |
| `(p ...)` | zero or more repetitions of `p`, at the end of the list only (no trailing patterns after it — see Scope below); pattern variables inside `p` bind to a list of each match. `___` is an alias for `...` |
| `#(p1 p2 p3)` | a fixed-length vector |
| `(and p ...)` | all subpatterns match — `(and x pat)` binds `x` to the whole value while also requiring it to match `pat` |
| `(or p ...)` | at least one subpattern matches; identifiers bound in one branch are visible in the body only when that branch is the one that matched |
| `(not p)` | `p` does *not* match; binds nothing |
| `(? pred p ...)` | `pred` applied to the value is truthy, then the (optional) remaining `p ...` match as `and` |
| `(= proc p)` | `(proc value)` matches `p` |
| `(record pred? (accessor p) ...)` | `(pred? value)` holds, and `(accessor value)` matches `p` for each accessor/pattern pair |

## Scope

**Not ported**, deliberately:

- Trailing patterns after `...` (e.g. `(a b ... c d)`) and vector-level ellipsis (`#(p ...)`) — both need a named per-iteration binding whose scope must correctly nest across recursive re-entries of the same clause, exactly the class of bug this port hit repeatedly (see below); not worth the added risk for two rarely-used variants when plain `(p ...)` covers the common case. A pattern using trailing-after-ellipsis raises a clear error rather than silently mismatching.
- `**1`/`*..`/`=..` (repetition-count variants beyond plain `...`), `***` (tree-search patterns), `get!`/`set!` (accessor-binding patterns), quasiquote patterns, `match-letrec` (upstream's own comment calls this "challenge stage — unhygienic insertion"; too much risk for too little use).

**Adapted, not ported as-is:** upstream's `$`/`@`/`struct`/`object` record patterns lean on CHICKEN's generic `slot-ref`/`is-a?` record introspection, which has no R7RS equivalent — R7RS `define-record-type` gives you named accessor procedures per field and nothing more generic. `(record pred? (accessor p) ...)` names the accessors explicitly instead of pretending positional-by-record-type access is possible here.

## A note on porting a hygiene-dependent algorithm to curry

curry's `syntax-rules` does not alpha-rename template-introduced identifiers, so a name reused across nested/recursive expansions of the *same* clause can collide with *itself*: an inner recursive match's own binding shadows an outer one whose continuation is still pending, silently changing which value a later match sees. This is a different, subtler problem than the ordinary "don't collide with the caller's own pattern variable" concern — it showed up as a real, silent-wrong-answer bug during this port: `(match (list 1 (list 2 3) 4) ((a (b c) d) (list a b c d)))` returned `(1 2 3 3)` instead of `(1 2 3 4)` — `d` picked up `c`'s value — until every `(car v)`/`(cdr v)`/`(vector-ref v i)`/`(accessor v)` in the match/dispatch clauses was passed directly into the next call rather than first bound to a name. These are all cheap, pure re-reads, so nothing is lost by not naming them.

Three real curry core bugs were found and fixed while building this module (a `syntax-rules` ellipsis/dotted-tail template bug, a `syntax-rules` literal-vs-wildcard priority bug for `_`, and a reader bug parsing a dotted-pair tail immediately following the ellipsis identifier) — none specific to this module, all now covered by regression tests in `tests/syntax_rules_tests.scm` and this file.

## API

### `(match expr (pattern . body) ...)`

Matches `expr` against each `pattern` in turn; evaluates the corresponding `body` for the first that matches. Raises if none match.

```scheme
(match (list 1 2 3)
  ((a b c) (+ a b c)))
;; => 6
```

### `(match-lambda (pattern . body) ...)` / `(match-lambda* (pattern . body) ...)`

`match-lambda` makes a one-argument procedure matching that argument; `match-lambda*` makes a variadic procedure matching the whole argument list.

### `(match-let ((var value) ...) body ...)` / `(match-let* ((var value) ...) body ...)`

Like `let`/`let*`, but `var` can be a full pattern, not just an identifier.

```scheme
(match-let (((a b) (list 1 2))) (+ a b))
;; => 3
```

## Everything else this module exports

Every other exported name (`match-next`, `match-one`, `match-two`, ...) is internal machinery, not part of the public API — but it has to be exported anyway. curry's `syntax-rules` is not hygienic across `define-library` boundaries: `match`'s own expansion contains bare references to `match-next`, which expands into `match-one`, and so on, all resolved in the *importer's* environment. Leaving any of these unexported would make using `match` (not importing it — using it) fail with `unbound-variable` the first time some clause happened to reach that helper.

## See also

- [`docs/reference/writing-a-module.md`](writing-a-module.md) — the non-hygienic-macro-export gotcha this module is the most extreme example of in curry's own tree
