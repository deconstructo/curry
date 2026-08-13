;;; Tests for (srfi s26 cut) / (srfi 26) -- cut/cute, the standard
;;; reference implementation verbatim. Doubles as a regression suite for
;;; the "Partial hygiene" fix in syntax_rules.c: cut/cute's own
;;; reference implementation is a recursive macro that accumulates a
;;; fresh `x` binding at each expansion step, which is exactly the shape
;;; that broke before that fix (every slot collapsed into whichever
;;; argument was passed last, since template-introduced identifiers were
;;; previously always emitted completely unchanged).

(import (srfi 26) (scheme write))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; ════════════════════════════════════════════════════════════
;;; § 1  cut -- basic slot substitution
;;; ════════════════════════════════════════════════════════════

(check "single slot" ((cut + 1 2 <>) 10) 13)
(check "no slots at all, still a zero-arg thunk" ((cut list)) '())

;;; The exact case that broke without the hygiene fix: multiple <> slots
;;; must each become their OWN distinct formal, not collapse into one.
(check "two slots stay distinct" ((cut list 1 <> 3 <>) "a" "b") '(1 "a" 3 "b"))
(check "three slots stay distinct" ((cut list <> <> <>) 'a 'b 'c) '(a b c))

;;; ════════════════════════════════════════════════════════════
;;; § 2  cut -- <...> rest-slot
;;; ════════════════════════════════════════════════════════════

(check "trailing <...> forwards extra args"
  ((cut list 1 <> <...>) 2 3 4 5) '(1 2 3 4 5))
(check "<...> alone forwards everything"
  ((cut list <...>) 1 2 3) '(1 2 3))

;;; ════════════════════════════════════════════════════════════
;;; § 3  cut vs cute -- evaluation timing of non-slot expressions
;;; ════════════════════════════════════════════════════════════

(define counter 0)
(define (next!) (set! counter (+ counter 1)) counter)

(set! counter 0)
(define cut-h (cut list (next!) <>))
(check "cut re-evaluates a non-slot expr on every call (1st)" (cut-h 'a) '(1 a))
(check "cut re-evaluates a non-slot expr on every call (2nd)" (cut-h 'b) '(2 b))

(set! counter 0)
(define cute-h (cute list (next!) <>))
(check "cute evaluates a non-slot expr once, at construction (1st)" (cute-h 'a) '(1 a))
(check "cute evaluates a non-slot expr once, reused (2nd)" (cute-h 'b) '(1 b))

;;; ════════════════════════════════════════════════════════════
;;; § 4  proc position is itself a non-slot expression
;;; ════════════════════════════════════════════════════════════

(define (adder n) (lambda (x y) (+ n x y)))
(check "the proc position can itself be an arbitrary expression"
  ((cut (adder 10) <> <>) 1 2) 13)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
