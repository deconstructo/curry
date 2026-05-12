# Curry Scheme Language Reference

*v0.7.5 — 2026-05-12*

Curry is an R7RS Scheme interpreter with a numeric tower that extends through the hypercomplex numbers, a built-in computer algebra system, an actor-model concurrency layer, and a modular C extension interface. Error messages are rendered in Standard Babylonian Akkadian, as scribal tradition demands.

## Contents

- [Quick start](#quick-start)
- [What's in Curry](#whats-in-curry)
- [Module tiers](#module-tiers)
- [R7RS deviations](#r7rs-deviations)
- [Values and types](#values-and-types)
- [Special forms](#special-forms)
- [Numeric tower](#numeric-tower)
- [Symbolic CAS](#symbolic-cas)
- [Strings and characters](#strings-and-characters)
- [Lists and pairs](#lists-and-pairs)
- [Vectors](#vectors)
- [I/O](#input--output)
- [Continuations](#tail-calls-and-continuations)
- [Actor model](#actor-model)
- [Procedure tracing](#procedure-tracing)
- [Module search path](#module-search-path)
- [Three-language syntax](#three-language-syntax)
- [REPL commands](#repl-commands)
- [Environment variables](#environment-variables)

---

## Quick start

```bash
# Linux
cmake -B build && cmake --build build -j$(nproc)
# macOS
cmake -B build && cmake --build build -j$(sysctl -n hw.logicalcpu)

./build/curry                        # REPL
./build/curry script.scm             # run a file
./build/curry -e '(display (+ 1 2))' # evaluate expression
```

See [INSTALL.md](INSTALL.md) for full dependency and build instructions, including optional modules and the Homebrew formula.

---

## What's in Curry

Curry has three layers:

**Core language** — compiled directly into the interpreter. Always present, no import needed. Includes the full R7RS base library plus Curry's extensions: the extended numeric tower (through multivectors and symbolic expressions), the actor system, math constants (`e`, `pi`, `exact-pi`, `exact-e`), CAS procedures (`∂`, `∫`, `symbolic`, `simplify`, `substitute`, `expand`, `collect`, …), and tracing.

**Standard modules** — separate `.so` or `.scm` files that are always installed alongside the interpreter. Import with `(import (curry name))`. These are: `json`, `network`, `redis`, `regex`, `sync`, `sqlite`, `mcp`, `crypto`, `ldap`, `storage`, `graphql`, `image`, `git`, `ode`.

**Optional modules** — built only when the required external library is present at compile time (or when the Homebrew formula is installed with `--with-*`). These are: `qt6` (GUI and 2D graphics), `plplot` (scientific plotting), `symengine` (SymEngine CAS backend), `neo4j` (graph database), `mqtt` (messaging), `vecdb` (approximate nearest neighbours).

Module documentation lives in separate files; see [the module docs](#module-reference).

---

## Module tiers

| Tier | Members | Import |
|------|---------|--------|
| Core (always in scope) | numeric tower, CAS, actors, I/O, R7RS base | no import needed |
| Standard | `json` `network` `redis` `regex` `sync` `sqlite` `mcp` `crypto` `ldap` `storage` `graphql` `image` `git` `ode` | `(import (curry name))` |
| Optional (build flag / brew option) | `qt6` `plplot` `symengine` `neo4j` `mqtt` `vecdb` | `(import (curry name))` |

Trying to import a module that was not built raises an error. The Homebrew formula enables all standard modules and accepts `--with-qt6`, `--with-plplot`, `--with-symengine`, `--with-neo4j`, `--with-mqtt` at install time.

---

## R7RS deviations

Curry aims for R7RS compatibility. The following deviations are confirmed and not planned for resolution soon. Programs that rely on these R7RS guarantees need workarounds.

### 1. `call/cc` — escape continuations only

`call-with-current-continuation` captures and invokes escape continuations (jumping outward through live stack frames) correctly, including correct interaction with `dynamic-wind`. **Upward continuations** — capturing a continuation and invoking it after the original call has returned — are not supported and will behave unpredictably.

```scheme
; Works: escape
(call/cc (lambda (k) (k 42) (error "never")))  ; => 42

; Does NOT work: upward continuation
(define k #f)
(+ 1 (call/cc (lambda c (set! k c) 0)))        ; storing k and later (k 5) is unsupported
```

Delimited continuations (`shift`/`reset`) are on the roadmap but not yet implemented.

### 2. `floor`, `ceiling`, `truncate`, `round` — inexact input yields inexact result

R7RS requires that `(floor 1.5)` returns the exact integer `1`. Curry returns the inexact flonum `1.0`. For all four rounding procedures, when the input is inexact the output is also inexact.

```scheme
(floor   1.7)   ; => 1.0    (R7RS requires 1)
(ceiling 1.2)   ; => 2.0    (R7RS requires 2)
(truncate 1.9)  ; => 1.0    (R7RS requires 1)
(round  2.5)    ; => 2.0    (R7RS requires 2)

(floor 7/2)     ; => 3      (exact input → exact output — correct)
```

Workaround: `(inexact->exact (floor x))` to obtain an exact integer.

### 3. `string-set!` — not implemented

Mutable strings are not supported. `string-set!` is absent. Use `string-copy` plus `string-copy!` (which copies a range), or build a new string via `string-append` / `list->string`.

### 4. `error-object-message` — named `error-message`

R7RS specifies `error-object-message` and `error-object-irritants` as the accessors for error objects raised by `(error msg irritant ...)`. Curry uses `error-message` and `error-irritants` (without the `object-` infix).

```scheme
(guard (e (#t (error-message e)))           ; Curry
  (error "something went wrong" 42))        ; => "something went wrong"

; R7RS portable form (error-object-message e) will raise "unbound variable"
```

### 5. `raise-continuable` — identical to `raise`

R7RS requires that `raise-continuable` allows the exception handler to return a value, which becomes the result of the `raise-continuable` call. Curry's `raise-continuable` is currently identical to `raise` and does not pass control back to the raise site. Handlers that return a value from `raise-continuable` will see unexpected behaviour.

---

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
| Quaternion | `quaternion?` | `1+2i+3j+4k` or `(make-quaternion 1 2 3 4)` |
| Octonion | `octonion?` | `(make-octonion 1 2 3 4 5 6 7 8)` |
| Multivector | `multivector?` | `(make-mv 3 0 0)` |
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

---

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
(raise-continuable obj)        ; note: currently identical to raise — see R7RS deviations
```

### dynamic-wind

`dynamic-wind` guarantees cleanup code runs regardless of how control leaves a body — normal return, exception, or escape continuation:

```scheme
(dynamic-wind before thunk after)
```

`before` is called on entry, `after` on any exit. Both are called with no arguments; the return value of `after` is discarded.

```scheme
; Safe resource cleanup pattern
(dynamic-wind
  (lambda () (display "acquiring\n"))
  (lambda () (error "something went wrong"))
  (lambda () (display "always released\n")))
; => prints "acquiring", then "always released", then raises

; Interaction with call/cc: after runs on escape too
(define log '())
(call-with-current-continuation
  (lambda (k)
    (dynamic-wind
      (lambda () (set! log (cons 'in  log)))
      (lambda () (k 'escaped))
      (lambda () (set! log (cons 'out log))))))
log  ; => (out in)
```

`call-with-port` uses this pattern to close a port on any exit:

```scheme
(call-with-port (open-input-file "data.txt")
  (lambda (port)
    (read port)))   ; port is closed whether read succeeds or raises
```

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

---

## Numeric tower

Arithmetic promotes automatically through the tower:

```
fixnum → bignum → rational → flonum → complex
       → quaternion → octonion → multivector
       → surreal → symbolic
```

When any operand is symbolic, the result is a symbolic expression tree instead of an error.

```scheme
(+ 1/3 1/6)              ; => 1/2  (exact rational)
(sqrt 2)                 ; => 1.4142135623730951
(expt 2 100)             ; => 1267650600228229401496703205376 (bignum)
(magnitude 3+4i)         ; => 5.0
```

Math constants are in scope without any import:

| Name | Value |
|------|-------|
| `e` | 2.718281828459045 (flonum) |
| `pi` | 3.141592653589793 (flonum) |
| `exact-pi` | 100-digit rational approximation |
| `exact-e` | 100-digit rational approximation |

### Quaternions

Quaternions represent rotations in 3D. All standard arithmetic works.

Quaternion literals use the same `a+bi+cj+dk` notation as output. Any subset of terms is accepted, in order:

```scheme
1+2i+3j+4k          ; full quaternion
3j+4k               ; pure imaginary part
+k                  ; unit k
-2.5j
(make-quaternion w x y z)   ; constructor form
```

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

### Octonions

Octonions are a non-associative, non-commutative normed division algebra (dimension 8).

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

Multivectors are elements of Cl(p,q,r). They unify scalars, vectors, bivectors, spinors, and rotors.

```scheme
(define mv (make-mv 3 0 0))          ; zero element of Cl(3,0,0)
(mv-set! mv 0 1.0)                   ; set scalar part

(define e1  (mv-e 3 0 0 1))          ; unit basis vector e₁
(define e12 (mv-e 3 0 0 1 2))        ; unit bivector e₁∧e₂

(mv+ a b)                            ; addition
(mv* a b)                            ; geometric product
(mv-wedge a b)                       ; outer product a∧b
(mv-lcontract a b)                   ; left contraction a⌋b
(mv-grade mv k)                      ; grade-k projection
(mv-dual mv)
(mv-reverse mv)                      ; grade-reversal ã
(mv-norm mv)
(mv-ref mv blade)                    ; component by blade bitmap
(mv-signature mv)                    ; → (p q r)
```

See [multivec.md](multivec.md) for the full reference.

### Surreal numbers

The surreal type adds ω (the first infinite ordinal) and ε = 1/ω (the first infinitesimal).

```scheme
(+ omega 3)             ; => ω + 3
(< epsilon 1/1000000)   ; => #t
(> omega 1000000000)    ; => #t
(* omega epsilon)       ; => 1
(auto-diff (lambda (x) (* x x)) 5)  ; => 10  (exact derivative via ε)
```

See [surreal.md](surreal.md) for the full reference.

### CAS interaction with the upper numeric tower

The CAS (symbolic layer) treats numbers as constants. **Fixnum, bignum, rational, flonum, complex, quaternion, octonion, and surreal** values are all recognized as constants — arithmetic operations on them alongside symbolic variables produce symbolic expression trees.

However, there are gaps at the upper end of the tower:

**Quaternions and octonions**: basic arithmetic (`+`, `-`, `*`) works. Transcendental functions (`sin`, `cos`, `exp`, `log`, `sqrt`) applied to a quaternion or octonion in a symbolic context will crash (the CAS attempts to reduce them to a flonum via `num_to_double`, which falls through). Do not mix quaternions or octonions with CAS transcendentals.

**Surreals**: `+`, `-`, `*` work as CAS constants. `sqrt` is special-cased and works. `sin`, `cos`, `exp`, and `log` of a surreal will crash.

**Multivectors**: not recognized as numeric constants by the CAS. Mixing a multivector with any symbolic expression will crash. Multivectors must be kept in their own computational layer, separate from the CAS.

```scheme
; Safe: symbolic variable + quaternion constant via arithmetic
(symbolic x)
(+ x (make-quaternion 1 0 0 0))     ; works, builds symbolic ADD node

; Unsafe: transcendental of quaternion in CAS context
(sin (make-quaternion 1 0 0 0))     ; crashes — do not do this

; Unsafe: multivector in symbolic expression
(symbolic x)
(+ x (make-mv 3 0 0))              ; crashes — multivectors are opaque to CAS
```

---

## Symbolic CAS

Curry includes a built-in computer algebra system. Declare variables with `symbolic`; ordinary arithmetic on them builds expression trees.

```scheme
(symbolic x)
(+ x 1)                           ; => (+ x 1)
(∂ (* 1/2 m (expt v 2)) v)       ; => (* m v)
(substitute (expt x 2) x 3)       ; => 9
(expand (* (+ x 1) (+ x 2)))      ; => (+ (expt x 2) (* 3 x) 2)
(collect (+ (* 2 x) (* 3 x)) x)   ; => (+ (* 5 x))
```

See [symbolic.md](symbolic.md) for the full reference: differentiation, integration, Wirtinger calculus, complex operators, polynomial operations, output formatting, and Wirtinger calculus.

---

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

Characters follow Unicode; `char->integer` returns a Unicode codepoint. Note: `string-set!` is not implemented — see [R7RS deviations](#r7rs-deviations).

---

## Lists and pairs

```scheme
(cons 1 '(2 3))   ; (1 2 3)
(car '(1 2 3))    ; 1
(cdr '(1 2 3))    ; (2 3)
(cadr '(1 2 3))   ; 2

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

---

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

---

## Input / output

```scheme
; Ports
(open-input-file "path")
(open-output-file "path")
(call-with-port port proc)          ; closes port on any exit
(with-input-from-file "path" thunk)
(with-output-to-file "path" thunk)
(current-input-port)
(current-output-port)
(current-error-port)
(input-port-open? port)
(output-port-open? port)

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

---

## Tail calls and continuations

Curry guarantees proper tail calls in all R7RS tail positions. Escape continuations work:

```scheme
(call-with-current-continuation
  (lambda (k)
    (k 42)
    (error "never reached")))
```

Continuations interact correctly with `dynamic-wind`: invoking an escape continuation unwinds all `dynamic-wind` frames entered since the continuation was captured, calling each `after` thunk before the jump.

**Upward continuations are not supported** — see [R7RS deviations](#r7rs-deviations). Delimited continuations (`shift`/`reset`) are on the roadmap.

---

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

Actors communicate exclusively through message passing. There are no shared mutable variables (except the GC heap — use actors as the synchronization boundary). The `sync` module provides mutexes, condition variables, and semaphores for cases where shared state is required.

---

## Procedure tracing

`trace` and `untrace` wrap a named procedure with enter/exit instrumentation. Trace output goes to stderr.

```scheme
(define (fact n) (if (<= n 1) 1 (* n (fact (- n 1)))))

(trace 'fact)
(fact 4)
; [trace] --> (fact 4)
; [trace] --> (fact 3)
; [trace] --> (fact 2)
; [trace] --> (fact 1)
; [trace] <-- fact = 1
; [trace] <-- fact = 2
; [trace] <-- fact = 6
; [trace] <-- fact = 24

(untrace 'fact)
(traced? fact)      ; => #f
```

| Procedure | Description |
|-----------|-------------|
| `(trace 'name)` | Wrap the global binding of `name` with trace instrumentation |
| `(untrace 'name)` | Unwrap, restoring the original procedure |
| `(traced? x)` | `#t` if `x` is a traced-procedure wrapper |

`trace` and `untrace` take **quoted symbols**, not values.

---

## Module search path

Modules are located by mapping a name-list to a file path:

```
(curry json)     →  curry/json.so
(curry crypto)   →  curry/crypto.so
(mylib util)     →  mylib/util.scm
```

Search order:
1. Paths in `CURRY_MODULE_PATH` (colon-separated)
2. `{exe-dir}/mods/` (build tree)
3. `{exe-dir}/../lib/curry/modules/` (installed)
4. `lib/curry/modules/` (relative to working directory)

Each directory is tried for `.so`, `.dylib`, `.sld`, `.scm` in that order.

---

## Module reference

| Module | Tier | Description |
|--------|------|-------------|
| [`(curry json)`](module-json.md) | Standard | JSON parse and serialise |
| [`(curry network)`](module-network.md) | Standard | TCP/UDP sockets |
| [`(curry redis)`](module-redis.md) | Standard | Redis client (RESP2) |
| [`(curry regex)`](module-regex.md) | Standard | POSIX extended regular expressions |
| [`(curry sync)`](module-sync.md) | Standard | Mutex, condvar, semaphore |
| [`(curry sqlite)`](module-sqlite.md) | Standard | SQLite3 |
| [`(curry mcp)`](mcp-clients.md) | Standard | Model Context Protocol server |
| [`(curry crypto)`](module-crypto.md) | Standard | SHA-256, HMAC, base64 |
| [`(curry ldap)`](module-ldap.md) | Standard | LDAP directory access |
| [`(curry storage)`](module-storage.md) | Standard | S3, Azure Blob, Swift |
| [`(curry graphql)`](module-graphql.md) | Standard | GraphQL HTTP client |
| [`(curry image)`](module-image.md) | Standard | PNG/JPEG/GIF image I/O |
| [`(curry git)`](module-git.md) | Standard | Git repository access (libgit2) |
| [`(curry ode)`](module-ode.md) | Standard | ODE solvers (RK45, Euler) |
| [`(curry qt6)`](module-qt6.md) | Optional | Qt6 windowing, 2D graphics, widgets |
| [`(curry plplot)`](module-plplot.md) | Optional | PLplot scientific plotting |
| [`(curry mqtt)`](module-mqtt.md) | Optional | MQTT client with TLS |
| [`(curry neo4j)`](module-neo4j.md) | Optional | Neo4j graph database (Bolt) |
| [`(curry vecdb)`](module-vecdb.md) | Optional | Nearest-neighbour vector database |

---

## Three-language syntax

Curry supports three syntaxes for the same language, interchangeably:

1. **English (R7RS)** — standard Scheme: `define`, `lambda`, `if`, `+`, `-`, `map`, …
2. **Transliterated Akkadian** — Standard Babylonian Akkadian in Latin script: `šakānum`, `pārisum`, `šumma`, …
3. **Cuneiform** — the same words in Unicode cuneiform script: `𒋻`, `𒅁`, `𒋗`, …

All three resolve to the same internal operations and can be mixed freely within a single program.

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

```scheme
; English (R7RS)
(define (kinetic-energy m v)
  (* 1/2 m (* v v)))

; Transliterated Akkadian
(šakānum (kinetic-energy m v)
  (šapākum 1/2 m (šapākum v v)))

; Cuneiform
(𒋻 (kinetic-energy m v)
  (𒊺 1/2 m (𒊺 v v)))
```

### Error messages

All runtime errors carry a Standard Babylonian preamble:

```
𒀭 ḫiṭītu — šumu lā šakin:
  unbound variable: foo
```

The DINGIR sign (𒀭) is the divine determinative. `ḫiṭītu` means *fault* or *error*. The Akkadian phrase identifies the category; the English line gives the specific detail.

See [akkadian-reference.md](akkadian-reference.md) for the complete error phrase catalogue and full vocabulary.

---

## REPL commands

| Command | Effect |
|---------|--------|
| `,quit` or `,exit` | Exit |
| `,help` | Show commands |
| `,gc` | Force a GC cycle |
| `,env` | List all global bindings |

When built with `libreadline`, the REPL provides line editing (arrow keys, Ctrl-A/E, Ctrl-R incremental history search), persistent history in `~/.curry_history` (last 500 entries), and multi-line input (prompt changes to `... ` when an expression spans multiple lines).

Without readline the REPL uses basic `fgets`-based input.

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `CURRY_MODULE_PATH` | Colon-separated extra module search directories |
