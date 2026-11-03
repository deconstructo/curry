;;; Regression test for a core bug: the composed c[ad]{2,4}r accessors
;;; (cadr, caar, cddr, caddr, cdddr, ...) used to call the raw, unchecked
;;; vcar/vcdr C macros at each step instead of validating pair-ness like
;;; car/cdr themselves do -- applying one past the end of a short list
;;; (e.g. (cddr '(1))) reached an intermediate '() or non-pair value and
;;; dereferenced it as if it were a heap Pair, crashing the whole process
;;; with a segfault instead of raising a catchable wrong-type-argument
;;; error. Found while implementing SRFI-272; fixed in src/builtins.c by
;;; routing every step through checked ccar_chk/ccdr_chk helpers.

(import (scheme base) (scheme write) (scheme cxr))

(define pass-count 0)
(define fail-count 0)

(define (check-error name thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL (expected error): ") (display name) (newline))))

(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display name)
             (display " expected=") (write expected)
             (display " actual=") (write actual) (newline))))

;; Every composed accessor, given input too short to satisfy it, must
;; raise a catchable error -- not crash the process.
(check-error "cadr on 1-elem list" (lambda () (cadr (list 1))))
(check-error "cddr on 1-elem list" (lambda () (cddr (list 1))))
(check-error "caar on nil"         (lambda () (caar (list))))
(check-error "cdar on nil"         (lambda () (cdar (list))))
(check-error "caddr on 2-elem list"  (lambda () (caddr (list 1 2))))
(check-error "cdddr on 2-elem list"  (lambda () (cdddr (list 1 2))))
(check-error "caaar on non-pair car" (lambda () (caaar (list 1 2 3))))
(check-error "cddar on non-pair car" (lambda () (cddar (list 1 2 3))))

;; And every composed accessor must still produce the right answer on
;; well-formed input of exactly the right shape.
(check "caar" 1 (caar (list (list 1 2) 3)))
(check "cadr" 2 (cadr (list 1 2 3)))
(check "cdar" '(2) (cdar (list (list 1 2) 3)))
(check "cddr" '(3) (cddr (list 1 2 3)))
(check "caaar" 1 (caaar (list (list (list 1)))))
(check "caadr" 2 (caadr (list 1 (list 2 3))))
(check "cadar" 2 (cadar (list (list 1 2) 3)))
(check "caddr" 3 (caddr (list 1 2 3)))
(check "cdaar" '(2) (cdaar (list (list (list 1 2)) 3)))
(check "cdadr" '(3) (cdadr (list 1 (list 2 3))))
(check "cddar" '() (cddar (list (list 1 2) 3)))
(check "cdddr" '(4) (cdddr (list 1 2 3 4)))

;; Four-level accessors (issue #345): they did not exist at all.  In a
;; full binary tree of depth 4 built so that taking car adds "a" and cdr
;; adds "d" to the FRONT of a leaf's label, (cXXXXr tree) is the leaf
;; labelled "XXXX" -- every accessor has to return its own name.
(define (cxr-tree suffix)
  (if (= (string-length suffix) 4)
      (string->symbol suffix)
      (cons (cxr-tree (string-append "a" suffix)) (cxr-tree (string-append "d" suffix)))))
(define tree4 (cxr-tree ""))
(for-each
 (lambda (name proc)
   (check name (string->symbol (substring name 1 5)) (proc tree4))
   (check-error (string-append name " on a 1-element list") (lambda () (proc (list 1)))))
 '("caaaar" "caaadr" "caadar" "caaddr" "cadaar" "cadadr" "caddar" "cadddr"
   "cdaaar" "cdaadr" "cdadar" "cdaddr" "cddaar" "cddadr" "cdddar" "cddddr")
 (list caaaar caaadr caadar caaddr cadaar cadadr caddar cadddr
       cdaaar cdaadr cdadar cdaddr cddaar cddadr cdddar cddddr))
(check "cadddr on a plain list" 4 (cadddr (list 1 2 3 4 5)))
(check "cddddr on a plain list" '(5) (cddddr (list 1 2 3 4 5)))

(display "cxxxr accessor tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
