# Using the object system

`(curry oop)` gives Curry Slim CLOS: classes, generic functions, and multiple
dispatch. This guide shows three things it's particularly good for that don't
show up in a plain API reference — extending the numeric tower, driving a Qt6
UI with dispatch instead of a `cond` ladder, and pairing objects with actors
for encapsulated concurrent state. For the full API, see
[`(curry oop)`](../reference/module-oop.md).

Every example below is runnable as shown; a few need `(import (curry qt6))`
which requires the optional Qt6 build (`-DBUILD_MODULE_QT6=ON`).

---

## 1. Extending the numeric tower

The motivating case for multiple dispatch in Curry: teach `+`/`*`/`simplify`
about a new algebraic type without touching a line of C, and without your new
type paying any tax on ordinary numbers.

```scheme
(import (curry oop))

(define-class <rational2> ()
  (num #:init 1 #:accessor r-num)
  (den #:init 1 #:accessor r-den))

(define (r-simplify n d)
  (let ((g (gcd n d)))
    (make <rational2> #:num (/ n g) #:den (/ d g))))

(define-method + ((a <rational2>) (b <rational2>))
  (r-simplify (+ (* (r-num a) (r-den b)) (* (r-num b) (r-den a)))
              (* (r-den a) (r-den b))))

(define-method * ((a <rational2>) (b <rational2>))
  (r-simplify (* (r-num a) (r-num b)) (* (r-den a) (r-den b))))

(define-generic describe (x))
(define-method describe ((x <rational2>))
  (string-append (number->string (r-num x)) "/" (number->string (r-den x))))

(define r1 (make <rational2> #:num 1 #:den 3))
(define r2 (make <rational2> #:num 1 #:den 6))

(display (describe (+ r1 r2)))   (newline)   ; => 1/2
(display (+ 1 2))                (newline)   ; => 3
```

The first `define-method` on `+` *promotes* the global `+` binding into a
generic function whose fallback is the original C primitive. Every later call
to plain `+` on numbers still runs that same primitive — `(+ 1 2)` above never
constructs an argument-class tuple or touches the dispatch cache at all
(Layer 3 in [the performance section](../reference/module-oop.md#performance)
proves this specifically, not just asserts it). You only pay for dispatch on
the calls that could actually need it.

The same pattern extends CAS operators:

```scheme
(define-method simplify ((x <rational2>)) x)   ; already in lowest terms
```

Real `(curry sicm)`/CAS code follows exactly this shape for `<manifold>`,
`<coordinate-system>`, or a user-defined algebraic structure — see
`docs/thoughts/oop.md`'s "Integration with the numeric tower" section for the
design intent this fulfills.

---

## 2. Driving a Qt6 UI with generic dispatch

Qt6 event callbacks (`canvas-on-draw!`, `canvas-on-mouse!`, ...) are plain
Scheme procedures — nothing Qt-specific is needed to make them dispatch on
object type instead of branching on a tag by hand.

```scheme
(import (curry oop))
(import (curry qt6))

(define-class <shape> ()
  (color #:init "black" #:accessor shape-color))
(define-class <circle> (<shape>)
  (radius #:init 10 #:accessor circle-radius))
(define-class <square> (<shape>)
  (side #:init 10 #:accessor square-side))

(define-generic draw-shape! (painter shape x y))

(define-method draw-shape! ((painter <object>) (s <circle>) (x <number>) (y <number>))
  (gfx-set-color! painter 0.2 0.4 0.9 1.0)
  (gfx-fill-ellipse! painter x y (circle-radius s) (circle-radius s)))

(define-method draw-shape! ((painter <object>) (s <square>) (x <number>) (y <number>))
  (gfx-set-color! painter 0.9 0.3 0.2 1.0)
  (gfx-fill-rect! painter x y (square-side s) (square-side s)))
```

`painter` is specialized `<object>` — every shape method accepts any painter,
so this isn't a case where the `<object>`-disables-the-fast-path rule costs
anything (there's no fallback to protect here; `draw-shape!` was never a
builtin).

Adding a new shape kind later — a `<triangle>`, a `<polygon>` — means adding
one more `define-method`, not finding and editing a `(cond ((circle? s) ...)
((square? s) ...))` chain wherever shapes are drawn, hit-tested, and
serialized.

**Headless, testable without a display** (what CI/this guide's own
verification runs — useful for generating images server-side, or just trying
this without opening a window):

```scheme
(define shapes (list (make <circle> #:radius 20) (make <square> #:side 30)))

(call-with-painter 200 200
  (lambda (painter)
    (let loop ((ss shapes) (x 10))
      (unless (null? ss)
        (draw-shape! painter (car ss) x 10)
        (loop (cdr ss) (+ x 60))))))
```

**In a real window**, the callback is identical — only how you obtain
`painter` changes:

```scheme
(define win (make-window "Shapes" 400 300))
(canvas-on-draw! (window-canvas win)
  (lambda (painter w h)
    (let loop ((ss shapes) (x 20))
      (unless (null? ss)
        (draw-shape! painter (car ss) x 50)
        (loop (cdr ss) (+ x 60))))))
```

The same idea works for `canvas-on-mouse!` dispatching on which `<widget>`
subclass was hit, or a `<tool>` hierarchy (`<pen-tool>`, `<eraser-tool>`)
where `(on-drag! current-tool canvas x y)` is one generic call regardless of
which tool is active. See [`(curry qt6)`](../reference/module-qt6.md) for the
full widget/painter API.

---

## 3. Objects owned by actors

`(curry oop)` instances are plain data — they don't run on their own thread
the way `object-system.md`'s (unbuilt) "actors all the way down" design
imagines. But pairing an ordinary class with an actor gives you exactly that
shape when you want it: one actor per instance, the actor's mailbox as the
only way in, and dispatch on the message inside.

```scheme
(import (curry oop))

(define-class <account> ()
  (id      #:init 0 #:accessor account-id)
  (balance #:init 0 #:mutable))

(define-generic deposit!    (acct amount))
(define-generic withdraw!   (acct amount))
(define-generic balance-of  (acct))

(define-method deposit! ((acct <account>) (amount <number>))
  (slot-set! acct 'balance (+ (slot-ref acct 'balance) amount))
  (slot-ref acct 'balance))

(define-method withdraw! ((acct <account>) (amount <number>))
  (if (> amount (slot-ref acct 'balance))
      (error "insufficient funds" (account-id acct))
      (begin
        (slot-set! acct 'balance (- (slot-ref acct 'balance) amount))
        (slot-ref acct 'balance))))

(define-method balance-of ((acct <account>)) (slot-ref acct 'balance))
```

The account instance's slot is declared `#:mutable`, which would ordinarily
mean "no synchronization guarantee" — but wrapping it in an actor makes the
mutation safe anyway, for a reason that has nothing to do with `(curry oop)`
itself: only one thread (the actor's) ever runs a method against this
particular instance, because the mailbox is the only way to reach it.

```scheme
;; One actor per account. The actor thread is the only code that ever
;; touches this account's slots — the mailbox is the serialization point,
;; not a lock.
(define (spawn-account id initial)
  (spawn (lambda ()
    (let ((acct (make <account> #:id id #:balance initial)))
      (let loop ()
        (let ((msg (receive)))
          (case (car msg)
            ((deposit)  (send! (caddr msg) (deposit! acct (cadr msg))))
            ((withdraw) (send! (caddr msg)
                          (guard (e (#t 'insufficient-funds))
                            (withdraw! acct (cadr msg)))))
            ((balance)  (send! (cadr msg) (balance-of acct))))
          (loop))))))) 

(define acct (spawn-account 1 100))
```

`receive` is curry's plain actor-mailbox primitive, not a pattern-matching
form — dispatch on the message shape with `case`/`cond` as shown; a real
`msg` here is just `(deposit 50 <reply-actor>)` or similar.

Note that the top-level script itself has no mailbox — `(self)` outside any
`spawn`'d thread isn't a usable actor identity. To observe a result from
top-level code (as opposed to another actor), spawn a small "client" actor
and synchronize back with `(curry sync)`:

```scheme
(import (curry sync))
(define done (make-semaphore 0))
(define results '())

(spawn (lambda ()
  (send! acct (list 'deposit 50 (self)))
  (set! results (cons (receive) results))
  (send! acct (list 'withdraw 500 (self)))
  (set! results (cons (receive) results))
  (send! acct (list 'balance (self)))
  (set! results (cons (receive) results))
  (sem-post! done)))

(sem-wait! done)
(display (reverse results))
(newline)
; => (150 insufficient-funds 150)
```

This pattern — a class for the data and methods, an actor loop for
serialization — generalizes to anything that needs both a clean object
interface and safe concurrent access: a connection pool, a game entity, a
simulation cell in a larger actor mesh. See the
[actor model reference](../reference/language.md#actor-model) and
[concurrency guide](../reference/concurrency.md) for `spawn`/`send!`/`receive`
and the other concurrency primitives (channels, STM) this composes with.

---

## See also

- [`(curry oop)` reference](../reference/module-oop.md) — the full API
- `docs/thoughts/oop.md` — design rationale and alternatives considered
- `docs/thoughts/object-system.md` — a more radical, unbuilt alternative
  design ("actors all the way down") that this guide's §3 approximates by
  hand rather than baking into the object model itself
- [`(curry qt6)` reference](../reference/module-qt6.md)
- [Concurrency reference](../reference/concurrency.md)
