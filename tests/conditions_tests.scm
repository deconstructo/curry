;;; tests/conditions_tests.scm — CL-style condition system tests
;;;
;;; Run: ./build/curry tests/conditions_tests.scm
;;;
;;; Covers: condition creation and hierarchy, signal (non-unwinding),
;;; handler-bind + invoke-restart, with-restarts, handler-case,
;;; ignore-errors, multi-level hierarchy matching, nested with-restarts.

(import (curry conditions))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ desc expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display desc) (newline)
             (display "  expected: ") (display expected) (newline)
             (display "  got:      ") (display got) (newline)))))))

;;; ---- Condition creation ----

(define c1 (make-condition 'math-error
              (list (cons 'expr '(/ 1 0)))
              "division by zero"))

(check "condition?"            (condition? c1)                   #t)
(check "condition-type"        (condition-type c1)               'math-error)
(check "condition-message"     (condition-message c1)            "division by zero")
(check "condition-field"       (condition-field c1 'expr)        '(/ 1 0))
(check "non-condition"         (condition? 42)                   #f)

;;; ---- Hierarchy ----

(define c2 (make-condition 'singular-matrix '() "singular"))

(check "is-a? direct"          (condition-is-a? c2 'singular-matrix) #t)
(check "is-a? parent"          (condition-is-a? c2 'math-error)      #t)
(check "is-a? grandparent"     (condition-is-a? c2 'error)           #t)
(check "is-a? root"            (condition-is-a? c2 'condition)       #t)
(check "is-a? unrelated"       (condition-is-a? c2 'warning)         #f)

;;; ---- T_ERROR objects also match 'error ----

(check "error-obj is-a? error"
  (guard (e (#t (condition-is-a? e 'error)))
    (error "boom"))
  #t)

;;; ---- signal (non-unwinding) ----

(define log '())
(handler-bind
  (('gc-pressure (lambda (c)
      (set! log (cons 'pressure log)))))
  (signal (make-condition 'gc-pressure '() "heap"))
  (set! log (cons 'after-signal log)))

(check "signal: handler called"      (member 'pressure log)     '(pressure))
(check "signal: execution continues" (member 'after-signal log) '(after-signal pressure))

;;; ---- handler-bind: multiple handlers, type matching ----

(define hb-log '())
(handler-bind
  (('warning    (lambda (c) (set! hb-log (cons 'warning-h  hb-log))))
   ('gc-pressure (lambda (c) (set! hb-log (cons 'pressure-h hb-log)))))
  (signal (make-condition 'gc-pressure '() "full")))

; gc-pressure is-a warning, so BOTH handlers fire
(check "handler-bind: specific handler fired" (member 'pressure-h hb-log) '(pressure-h))
(check "handler-bind: parent fires too"       (member 'warning-h  hb-log) '(warning-h pressure-h))

; unrelated type handler should NOT fire
(define hb-log2 '())
(handler-bind
  (('math-error (lambda (c) (set! hb-log2 (cons 'math-h hb-log2)))))
  (signal (make-condition 'gc-pressure '() "full")))
(check "handler-bind: unrelated type not fired" hb-log2 '())

;;; ---- with-restarts + invoke-restart ----

(define r1
  (with-restarts ((use-zero "Return zero" 0)
                  (use-one  "Return one"  1))
    (invoke-restart 'use-zero)))
(check "invoke-restart use-zero" r1 0)

(define r2
  (with-restarts ((use-zero "Return zero" 0)
                  (use-one  "Return one"  1))
    (invoke-restart 'use-one)))
(check "invoke-restart use-one" r2 1)

;;; handler-bind + with-restarts (the CL recovery pattern)

(define recovered
  (with-restarts
    ((use-pseudoinverse "Use pseudoinverse" 'pseudo)
     (use-identity      "Use identity"      'id))
    (handler-bind
      (('math-error (lambda (c)
          (invoke-restart 'use-pseudoinverse))))
      (condition-error 'math-error '() "singular matrix"))))
(check "handler-bind + invoke-restart" recovered 'pseudo)

;;; handler not invoked if type doesn't match

(define hb-fired #f)
(handler-bind
  (('warning (lambda (c) (set! hb-fired #t))))
  (with-restarts ((use-zero "zero" 0))
    (handler-bind
      (('math-error (lambda (c) (invoke-restart 'use-zero))))
      (condition-error 'math-error '() "oops"))))
(check "warning handler not fired for math-error" hb-fired #f)

;;; ---- handler-case ----

(define hc1
  (handler-case
    (condition-error 'math-error '() "bad math")
    ((math-error c) (string-append "caught: " (condition-message c)))))
(check "handler-case: direct match"
  hc1 "caught: bad math")

(define hc2
  (handler-case
    (condition-error 'singular-matrix '() "M")
    ((singular-matrix c) 'singular)
    ((math-error      c) 'math)
    ((error           c) 'error-level)))
(check "handler-case: most specific wins" hc2 'singular)

(define hc3
  (handler-case
    (condition-error 'singular-matrix '() "M")
    ((math-error c) 'math)      ; parent type — should catch
    ((error      c) 'error-level)))
(check "handler-case: parent type catches child" hc3 'math)

(define hc4
  (handler-case
    42                          ; no exception raised
    ((math-error c) 'never)))
(check "handler-case: no exception = normal return" hc4 42)

;;; ---- ignore-errors ----
;;; (define-values not yet in curry — use call-with-values)

(define v1 #f) (define e1 #f)
(call-with-values
  (lambda () (ignore-errors 99))
  (lambda (v e) (set! v1 v) (set! e1 e)))
(check "ignore-errors: success value"     v1  99)
(check "ignore-errors: success no-error"  e1  #f)

(define v2 #f) (define e2 #f)
(call-with-values
  (lambda () (ignore-errors (condition-error 'math-error '() "oops")))
  (lambda (v e) (set! v2 v) (set! e2 e)))
(check "ignore-errors: error value"     v2 #f)
(check "ignore-errors: error is cond?"  (condition? e2) #t)

;;; ---- nested with-restarts ----

(define nested
  (with-restarts ((outer "outer" 'outer-val))
    (with-restarts ((inner "inner" 'inner-val))
      (invoke-restart 'outer))))     ; invoke outer from inner context
(check "nested with-restarts: invoke outer from inner" nested 'outer-val)

;;; ---- find-restart ----

(define fr-result #f)
(with-restarts ((find-me "find me" 99))
  (set! fr-result (find-restart 'find-me)))
(check "find-restart: found inside frame"  (restart? fr-result) #t)
(check "find-restart: correct name"        (restart-name fr-result) 'find-me)
(check "find-restart: outside frame = #f"  (find-restart 'find-me) #f)

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(when (> fail 0) (error "condition tests failed"))
