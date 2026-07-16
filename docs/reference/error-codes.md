# Error Codes

Every error object carries a `message` (human-readable, Akkadian preamble
and all) and, at a curated set of call sites, a stable machine-legible
`code` — a symbol like `wrong-type-argument` that does not change across
releases even if the message text is reworded. Codes are for tooling: a
linter, an LSP, the MCP server, or the LLM client parsing curry's own
output can match on the code instead of scraping prose.

```scheme
(guard (e (#t (display (error-object-code e)) (newline)))
  (car 5))
;; => wrong-type-argument
```

`(condition-code e)` is the equivalent accessor for the CL-style condition
system and behaves identically for error objects; it returns `#f` for
user-signalled `Condition` objects (`make-condition`), which don't carry a
code. `(error-object-code e)` likewise returns `#f` for errors raised
without a code, which is most of them — see below.

The REPL/script error printer shows the code when present:

```
$ curry -e '(car 5)'
Error [wrong-type-argument]: 𒀭 ḫiṭītu — lā qitnum:
  car: not a pair
  at <toplevel> (<expr>:1)
```

## Coverage

Curry raises errors from roughly 540 call sites across the C source. Only
the ones most frequently hit in practice are coded — this is deliberately
incremental, not an attempt to code every site. An uncoded error still has
a full message and backtrace; it just returns `#f` from
`error-object-code`. Expanding coverage is a matter of swapping `scm_raise`
for `scm_raise_code` at additional sites (see `src/eval.h`) — no other
plumbing is required.

## Registry

| Code | Meaning | Representative sites |
|------|---------|----------------------|
| `wrong-type-argument` | An argument had the wrong type for the operation | `car`, `cdr`, `set-car!`, `set-cdr!`, `vector-ref`, `vector-set!`, `string-ref` |
| `wrong-number-of-arguments` | A call passed too few or too many arguments | primitive procedure calls (arity-checked at the `vis_prim` dispatch in `eval.c`/`env.c`) |
| `unbound-variable` | A variable reference or `set!` named a symbol with no binding | global/local lookup (`env.c`), `set!` (`eval.c`), `OP_LOAD_GLOBAL` (`vm.c`) |
| `not-a-procedure` | The operator position of a call was not callable | `eval.c` apply paths |
| `division-by-zero` | Exact division, `quotient`, `remainder`, or `modulo` by zero | `numeric.c` — see note below |
| `index-out-of-range` | A numeric index was outside a sequence's valid bounds | `vector-ref`, `vector-set!`, `substring` |
| `stack-overflow` | The VM's value stack, call-frame stack, or exception-handler stack was exhausted | `vm.c` |

Codes are pre-interned symbols (`EC_WRONG_TYPE_ARGUMENT`, etc.), declared
in `src/symbol_list.h` next to the existing R7RS error-kind symbols
(`S_ERROR`, `S_FILE_ERROR`, `S_READ_ERROR`) and available as C globals
after `sym_init()`, following the same pattern as special-form symbols
like `S_LAMBDA`.

### `division-by-zero` was also a crash fix

Before this registry existed, `(/ 1 0)`, `(quotient 5 0)`, `(remainder 5
0)`, and `(modulo 5 0)` did not raise a catchable Scheme condition at
all — they crashed the process with `SIGFPE`, because the fixnum path did
a raw C `/`/`%` and the exact-rational path called GMP's `mpq_div` with a
zero divisor, both undefined behavior on a zero denominator. Adding the
`division-by-zero` code required adding the missing zero checks first;
the two changes shipped together. If you're auditing for similar bugs,
the pattern to search for is a raw `/` or `%` on `intptr_t` values, or a
GMP `mpq_div`/`mpz_tdiv_q`/`mpz_tdiv_r` call, with no preceding
zero-divisor check.

## GC note

`ErrorObj.code` is a `val_t` like every other field and must be traced by
every moving GC backend. Curry has four: Boehm (conservative — traces
automatically, no code needed) plus three precise/moving backends
(`gc_gen.c`, `gc_generational.c`, `gc_semispace.c`) that each need an
explicit `case T_ERROR` evacuation line. If you add a field to `ErrorObj`,
grep for `e->backtrace` in those three files and add your field the same
way — this is exactly the "not GC-traced" bug class documented in
`docs/thoughts/maturity-roadmap.md`.
