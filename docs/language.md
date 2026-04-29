# Curry Scheme Language Reference

Curry is an R7RS Scheme interpreter with a numeric tower that extends through the hypercomplex numbers, an actor-model concurrency system, and a modular C extension interface. Error messages are rendered in Standard Babylonian Akkadian with cuneiform script, as scribal tradition demands.

## Quick start

```bash
cmake -B build && cmake --build build -j$(nproc)
./build/curry                        # REPL
./build/curry script.scm             # run a file
./build/curry -e '(display (+ 1 2))' # evaluate expression
```

## Values and types

Every value is a 64-bit word. Booleans, the empty list, `#!void`, and `#!eof` are immediate; all other values are heap pointers managed by Boehm GC (conservative, no explicit rooting required).

| Type | Predicate | Literal / Constructor |
|------|-----------|---------|
| Boolean | `boolean?` | `#t` `#f` |
| Fixnum | `fixnum?` | `42` `-7` |
| Bignum | `integer?` | `123456789012345678901234567890` |
| Rational | `rational?` | `3/4` |
| Flonum | `real?` | `3.14` `1e-9` |
| Complex | `complex?` | `1+2i` |
| Quaternion | `quaternion?` | `(make-quaternion 1 2 3 4)` |
| Octonion | `octonion?` | `(make-octonion 1 2 3 4 5 6 7 8)` |
| Surreal | `surreal?` | `omega`, `epsilon`, `(make-surreal ...)` |
| Symbolic variable | `sym-var?` | `(symbolic x)` then `x` |
| Symbolic expression | `sym-expr?` | `(+ x 1)` when x is symbolic |
| Quantum value | `quantum?` | `(quantum-uniform '(a b c))` |
| Pair | `pair?` | `(a . b)` |
| List | `list?` | `(1 2 3)` |
| Vector | `vector?` | `#(1 2 3)` |
| Bytevector | `bytevector?` | `#u8(1 2 3)` |
| String | `string?` | `"hello"` |
| Symbol | `symbol?` | `foo` |
| Character | `char?` | `#\a` `#\newline` |
| Procedure | `procedure?` | `(lambda (x) x)` |
| Port | `port?` | — |
| Actor | `actor?` | `(spawn thunk)` |

## Special forms

### Binding

```scheme
(define name expr)
(define (name params...) body...)
(define-values (a b c) expr)          ; multiple values

(let ((x 1) (y 2)) body...)
(let* ((x 1) (y (+ x 1))) body...)
(letrec ((f (lambda () (f)))) body...)
(letrec* ...)

(set! name expr)
```

### Lambda

```scheme
(lambda (a b c) body...)              ; fixed arity
(lambda (a b . rest) body...)         ; rest args
(lambda args body...)                 ; all args as list
```

### Conditionals

```scheme
(if test consequent)
(if test consequent alternate)
(cond (test expr...) ... (else expr...))
(case val ((lit...) expr...) ... (else expr...))
(when test body...)
(unless test body...)
(and expr...)
(or expr...)
```

### Sequencing

```scheme
(begin expr...)
(do ((var init step) ...) (test result...) body...)
```

### Quasiquote

```scheme
`(a ,b ,@c)     ; quasiquote / unquote / unquote-splicing
```

### Tail calls

All tail positions are optimised — proper tail recursion is guaranteed. Named `let` is idiomatic for loops:

```scheme
(let loop ((i 0) (acc '()))
  (if (= i 10)
      (reverse acc)
      (loop (+ i 1) (cons i acc))))
```

### Multiple values

```scheme
(values 1 2 3)
(call-with-values (lambda () (values 1 2)) +)   ; => 3
(define-values (q r) (floor/ 17 5))
```

### Exception handling

```scheme
(guard (exn
        ((string? (condition/report-string exn)) ...))
  body...)

(with-exception-handler
  (lambda (e) ...)
  thunk)

(error "message" irritant...)
(raise obj)
(raise-continuable obj)
```

Errors display with a Standard Babylonian Akkadian preamble:

```
𒀭 ḫiṭītu — šumu lā šakin:
  unbound variable: foo
```

The cuneiform sign 𒀭 (DINGIR) is the divine determinative; `ḫiṭītu` means *fault/error*. The Akkadian phrase identifies the error category — `šumu lā šakin` is "the name is not established", `lā qitnum` is "not a small thing [pair]", and so on.

### Modules and imports

```scheme
(import (scheme base))
(import (curry json))
(import (only (curry crypto) sha256-hex hmac-sha256))
(import (except (curry crypto) md5))
(import (rename (curry json) (json-parse parse-json)))
(import (prefix (curry network) net-))
```

### define-library

```scheme
(define-library (mylib util)
  (export helper transform)
  (import (scheme base))
  (begin
    (define (helper x) ...)
    (define (transform x) ...)))
```

## Surreal numbers

The surreal number type extends the real line with `omega` (ω, the first infinite ordinal) and `epsilon` (ε = 1/ω, the first positive infinitesimal). All arithmetic operators work on surreals. Surreals are totally ordered and slot into the numeric tower between rationals and flonums.

```scheme
(+ omega 3)             ; => ω + 3
(< epsilon 1/1000000)   ; => #t  (ε smaller than any positive real)
(> omega 1000000000)    ; => #t  (ω larger than any integer)
(* omega epsilon)       ; => 1   (ω · ω⁻¹ = 1)
(auto-diff (lambda (x) (* x x)) 5)  ; => 10  (exact derivative via ε)
```

See [surreal.md](surreal.md) for the full reference.

## Symbolic expressions

Curry includes a built-in computer algebra system. Declare variables with `symbolic`, then use ordinary arithmetic — expressions involving unresolved variables become symbolic expression trees instead of errors.

```scheme
(symbolic x)
(+ x 1)                           ; => (+ x 1)
(∂ (* 1/2 m (expt v 2)) v)       ; => (* m v)   (momentum!)
(substitute (expt x 2) x 3)       ; => 9
```

See [symbolic.md](symbolic.md) for the full reference.

## Quantum superposition

A quantum value is a normalised probability amplitude distribution over arbitrary Scheme values. Arithmetic maps over branches without collapsing; `observe` collapses probabilistically.

```scheme
(define coin (quantum-uniform '(heads tails)))
(observe coin)                     ; => heads or tails

(define q (quantum-uniform '(1 2 3)))
(* q 10)    ; => #|0.5774|10> + 0.5774|20> + 0.5774|30>|
```

See [quantum.md](quantum.md) for the full reference.

## Numeric tower

Arithmetic promotes automatically: fixnum → bignum → rational → flonum → complex → symbolic → quantum. The `exact→inexact` / `inexact→exact` procedures cross the exact/inexact boundary.

```scheme
(+ 1/3 1/6)              ; => 1/2  (exact rational)
(sqrt 2)                 ; => 1.4142135623730951
(expt 2 100)             ; => 1267650600228229401496703205376 (bignum)
(magnitude 3+4i)         ; => 5.0
```

### Quaternions

Quaternions represent rotations in 3D. All standard arithmetic works.

```scheme
(define q (make-quaternion w x y z))
(quaternion-w q)  (quaternion-x q)
(quaternion-y q)  (quaternion-z q)

(quaternion+ q1 q2)
(quaternion* q1 q2)      ; Hamilton product
(quaternion-conjugate q)
(quaternion-norm q)
(quaternion-normalize q)
(quaternion-inverse q)

; Rotate a 3-vector by a quaternion
(quaternion-rotate-vector q #(0 1 0))
```

Construct a rotation quaternion from an axis and angle:

```scheme
(define (axis-angle->quat ax ay az angle)
  (let* ((h (/ angle 2))
         (s (sin h)))
    (make-quaternion (cos h) (* ax s) (* ay s) (* az s))))
```

### Octonions

Octonions are a non-associative, non-commutative normed division algebra (dimension 8). They are not useful for anything. They are therefore essential.

```scheme
(define o (make-octonion e0 e1 e2 e3 e4 e5 e6 e7))
(octonion-ref o 3)       ; component e3
(octonion+ o1 o2)
(octonion* o1 o2)        ; Graves/Cayley multiplication table
(octonion-conjugate o)
(octonion-norm o)
(octonion-normalize o)
```

Note: `(octonion* (octonion* a b) c)` ≠ `(octonion* a (octonion* b c))` in general.

### Multivectors (Clifford algebra)

Multivectors are elements of the Clifford algebra Cl(p,q,r). They unify scalars, vectors, bivectors, spinors, and rotors. Quaternions are the even subalgebra of Cl(3,0,0).

```scheme
(define mv (make-mv 3 0 0))          ; zero element of Cl(3,0,0)
(mv-set! mv 0 1.0)                   ; set scalar part

(define e1  (mv-e 3 0 0 1))          ; unit basis vector e₁
(define e12 (mv-e 3 0 0 1 2))        ; unit bivector e₁∧e₂

(mv+ a b)                            ; addition
(mv- a b)                            ; subtraction
(mv* a b)                            ; geometric product
(mv-wedge a b)                       ; outer product a∧b
(mv-lcontract a b)                   ; left contraction a⌋b
(mv-scale mv 2.0)
(mv-reverse mv)                      ; grade-reversal ã
(mv-involute mv)                     ; grade involution â
(mv-conjugate mv)                    ; Clifford conjugate ā
(mv-dual mv)
(mv-grade mv k)                      ; grade-k projection
(mv-scalar mv)                       ; scalar part as number
(mv-norm mv)
(mv-normalize mv)
(mv-ref mv blade)                    ; component by blade bitmap or index list
(mv-signature mv)                    ; → (p q r)

(quaternion->mv q)                   ; embed quaternion in Cl(3,0,0)
(mv->quaternion mv)                  ; extract quaternion from Cl(3,0,0) even part
```

See [multivec.md](multivec.md) for the full reference, rotation examples, and notes on PGA and CGA.

## Strings and characters

```scheme
(string-length "hello")          ; 5
(string-ref "hello" 1)           ; #\e
(substring "hello" 1 3)          ; "el"
(string-append "foo" "bar")      ; "foobar"
(string->number "42")            ; 42
(number->string 255 16)          ; "ff"
(string-upcase "hello")          ; "HELLO"
(string-contains "foobar" "oba") ; 2  (or #f)
(string->list "abc")             ; (#\a #\b #\c)
(string-copy str)
(string->symbol "foo")
(symbol->string 'foo)
```

Characters follow Unicode; `char->integer` returns a Unicode codepoint.

## Lists and pairs

```scheme
(cons 1 '(2 3))   ; (1 2 3)
(car '(1 2 3))    ; 1
(cdr '(1 2 3))    ; (2 3)
(cadr '(1 2 3))   ; 2   (car of cdr)

(list 1 2 3)
(length '(1 2 3))
(append '(1 2) '(3 4))
(reverse '(1 2 3))
(map (lambda (x) (* x x)) '(1 2 3 4))
(filter odd? '(1 2 3 4 5))
(fold-left  + 0 '(1 2 3 4 5))
(fold-right cons '() '(1 2 3))
(for-each display '(1 2 3))
(assoc key alist)
(assq  key alist)
(member x lst)
(list-tail lst k)
(list-ref  lst k)
```

## Vectors

```scheme
(vector 1 2 3)
(make-vector 5 0)          ; #(0 0 0 0 0)
(vector-length v)
(vector-ref v 2)
(vector-set! v 2 99)
(vector->list v)
(list->vector '(1 2 3))
(vector-copy v start end)
(vector-fill! v val start end)
(vector-map f v)
(vector-for-each f v)
```

## Input / output

```scheme
; Ports
(open-input-file "path")
(open-output-file "path")
(call-with-port port proc)
(with-input-from-file "path" thunk)
(with-output-to-file "path" thunk)
(current-input-port)
(current-output-port)
(current-error-port)

; Reading
(read)
(read-char)
(peek-char)
(read-line)
(eof-object? v)

; Writing
(write obj)              ; machine-readable (strings quoted)
(display obj)            ; human-readable (strings unquoted)
(newline)
(write-char ch)
(write-string str)
(flush-output-port)
```

## Tail calls and continuations

Curry guarantees proper tail calls in all R7RS tail positions. Escape continuations work:

```scheme
(call-with-current-continuation
  (lambda (k)
    (k 42)
    (error "never reached")))
```

Full first-class continuations (upward-crossing) are an escape-only implementation. Delimited continuations (`shift`/`reset`) are on the roadmap.

## Actor model

Actors are lightweight concurrent processes. Each actor runs in its own thread sharing the GC heap.

```scheme
; Spawn an actor from a thunk
(define a (spawn (lambda ()
  (let loop ()
    (let ((msg (receive)))
      (display msg)
      (newline)
      (loop))))))

; Send a message (non-blocking)
(send! a "hello")
(send! a 42)

; Receive in the current actor (blocks until a message arrives)
(define msg (receive))

; Who am I?
(self)               ; => current actor handle

; Check if alive
(actor-alive? a)
```

Actors communicate exclusively through message passing. There are no shared mutable variables (except the GC heap — use actors as the synchronization boundary).

## Module search path

Modules are located by mapping a name-list to a file path:

```
(curry json)     →  curry/json.so   (C extension)
(curry crypto)   →  curry/crypto.so
(mylib util)     →  mylib/util.scm  (Scheme)
```

Search order:
1. Paths in `CURRY_MODULE_PATH` (colon-separated)
2. `{exe-dir}/mods/` (build tree — finds modules next to the binary)
3. `{exe-dir}/../lib/curry/modules/` (installed)
4. `lib/curry/modules/` (relative to working directory)

Each directory is tried for `.so`, `.dylib`, `.sld`, `.scm` in that order.

## Three-language syntax

Curry supports three syntaxes for the same language, interchangeably:

1. **English (R7RS)** — standard Scheme: `define`, `lambda`, `if`, `+`, `-`, `map`, …
2. **Transliterated Akkadian** — Standard Babylonian Akkadian in Latin script: `šakānum`, `pārisum`, `šumma`, `wašābum`, …
3. **Cuneiform** — the same words in Unicode cuneiform script: `𒋻`, `𒅁`, `𒋗`, `𒌝`, …

All three syntaxes resolve to the same internal operations. You can mix them freely within a single program, though this is not recommended for readability.

### Why

Error messages are rendered in Standard Babylonian Akkadian because scribal tradition demands it. Extending the language itself to accept Akkadian syntax follows naturally. The cuneiform script follows because the Babylonians did not write their language in Latin letters.

### Selected vocabulary

| English | Akkadian | Cuneiform | Meaning |
|---------|----------|-----------|---------|
| `define` | `šakānum` | `𒋻` | to place / establish |
| `lambda` | `pārisum` | `𒅁` | resolver, one who divides |
| `if` | `šumma` | `𒋗` | if |
| `+` | `wašābum` | `𒉡` | to add / dwell |
| `-` | `naṭālum` | `𒇲𒌑` | to subtract / look |
| `*` | `šapākum` | `𒊺` | to multiply / pour |
| `/` | `zâzum` | `𒌋` | to divide / share |
| `cons` | `rakābum` | `𒁀` | to join / ride |
| `car` | `rēšum` | `𒁹` | head / top |
| `cdr` | `šaplum` | `𒋗𒁀` | bottom / lower part |
| `list` | `ṭuppum` | `𒄿𒌝` | tablet (list of things) |
| `map` | `alākum` | `𒀪` | to go (over) / traverse |
| `display` | `šūbrûm` | `𒁹𒊮` | to show / make visible |
| `spawn` | `banûm` | `𒆳` | to create / beget |
| `send!` | `šapārum` | `𒀭𒋫` | to send (a message) |
| `receive` | `leqûm` | `𒂗` | to take / receive |
| `error` | `ḫiṭītum` | `𒄭` | fault / error |
| `symbolic` | `šiṭrum` | `𒌑𒋻` | writing / inscription |

The full vocabulary is in [akkadian-reference.md](akkadian-reference.md).

### Example — kinetic energy in three syntaxes

English (R7RS):
```scheme
(define (kinetic-energy m v)
  (* 1/2 m (* v v)))
```

Transliterated Akkadian:
```scheme
(šakānum (kinetic-energy m v)
  (šapākum 1/2 m (šapākum v v)))
```

Cuneiform:
```scheme
(𒋻 (kinetic-energy m v)
  (𒊺 1/2 m (𒊺 v v)))
```

### Error messages

All runtime errors carry a Standard Babylonian preamble:

```
𒀭 ḫiṭītu — šumu lā šakin:
  unbound variable: foo
```

The DINGIR sign (𒀭) is the divine determinative, written before the names of gods and important concepts. `ḫiṭītu` means *fault* or *error*. The Akkadian phrase identifies the category; the English line below it gives the specific detail.

See [akkadian-reference.md](akkadian-reference.md) for the complete error phrase catalogue and the full vocabulary of special forms and procedures.

## REPL commands

| Command | Effect |
|---------|--------|
| `,quit` or `,exit` | Exit |
| `,help` | Show commands |
| `,gc` | Force a GC cycle |

When built with `libreadline`, the REPL provides:
- Line editing (arrow keys, Ctrl-A/E, Ctrl-R incremental history search)
- Persistent history in `~/.curry_history` (last 500 entries)
- Multi-line input: the prompt changes to `... ` when an expression spans multiple lines

Without readline the REPL uses basic `fgets`-based input.

## Environment variables

| Variable | Effect |
|----------|--------|
| `CURRY_MODULE_PATH` | Colon-separated extra module search directories |
