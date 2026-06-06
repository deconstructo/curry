;;; examples/conditions_demo.scm — CL-style condition system demo
;;;
;;; Demonstrates the condition system for error recovery without
;;; discarding computation state.  This is the pattern that makes
;;; the condition system useful for long-running scientific computations.
;;;
;;; Run: ./build/curry examples/conditions_demo.scm

(import (curry conditions))

(display "=== Curry Condition System Demo ===") (newline) (newline)

;;; ─── 1. Basic condition signalling ──────────────────────────────────────────

(display "1. signal (non-unwinding) — handler runs but stack stays intact\n")

(define steps-taken 0)

(handler-bind
  (('gc-pressure (lambda (c)
      (display "   [handler] GC pressure: ")
      (display (condition-message c)) (newline))))
  (set! steps-taken (+ steps-taken 1))
  (signal (make-condition 'gc-pressure '() "heap at 80%"))
  (set! steps-taken (+ steps-taken 1))   ; still runs after signal
  (signal (make-condition 'gc-pressure '() "heap at 95%"))
  (set! steps-taken (+ steps-taken 1)))

(display "   steps-taken (should be 3): ") (display steps-taken) (newline)
(newline)

;;; ─── 2. Recovery with restarts ──────────────────────────────────────────────

(display "2. with-restarts + handler-bind — recover from bad matrix\n")

;;; Simulated matrix-inverse that signals on singular matrices
(define (safe-matrix-inverse M)
  (if (= (car M) 0)                          ; pretend det=0 means singular
      (condition-error 'singular-matrix
                       (list (cons 'matrix M))
                       "Matrix is singular")
      (/ 1.0 (car M))))                      ; pretend inverse = 1/det

(define result
  (with-restarts
    ((use-pseudoinverse "Use Moore-Penrose pseudoinverse"
       (begin (display "   [restart] using pseudoinverse") (newline)
              'pseudoinverse))
     (use-identity "Use identity matrix"
       (begin (display "   [restart] using identity") (newline)
              'identity))
     (return-zero "Return zero matrix"
       (begin (display "   [restart] returning zero") (newline)
              0.0)))
    (handler-bind
      (('singular-matrix (lambda (c)
          (display "   [handler] caught singular-matrix, invoking pseudoinverse\n")
          (invoke-restart 'use-pseudoinverse))))
      (safe-matrix-inverse '(0 1 2 3)))))    ; det=0, triggers restart

(display "   result: ") (display result) (newline)
(newline)

;;; ─── 3. handler-case — choose the right level of abstraction ───────────────

(display "3. handler-case — match by type hierarchy\n")

(define (try-operation name thunk)
  (display "   ") (display name) (display ": ")
  (display
    (handler-case
      (thunk)
      ((singular-matrix c)
       (string-append "singular matrix (" (condition-message c) ")"))
      ((math-error c)
       (string-append "math error (" (condition-message c) ")"))
      ((error c)
       (string-append "error (" (condition-message c) ")"))))
  (newline))

(try-operation "singular"
  (lambda () (condition-error 'singular-matrix '() "det=0")))
(try-operation "division"
  (lambda () (condition-error 'math-error '() "div/0")))
(try-operation "generic"
  (lambda () (error "something went wrong")))
(try-operation "success"
  (lambda () "all good"))
(newline)

;;; ─── 4. ignore-errors — swallow and inspect ─────────────────────────────────

(display "4. ignore-errors — attempt and recover without unwinding everything\n")

(define (attempt-parse str)
  (call-with-values
    (lambda ()
      (ignore-errors
        (if (string=? str "bad")
            (condition-error 'error '() (string-append "cannot parse: " str))
            (string-length str))))
    (lambda (result err)
      (if err
          (string-append "parse failed: " (condition-message err))
          (string-append "length = " (number->string result))))))

(display "   (attempt-parse \"hello\"): ") (display (attempt-parse "hello")) (newline)
(display "   (attempt-parse \"bad\"):   ") (display (attempt-parse "bad"))   (newline)
(newline)

;;; ─── 5. The scientific computing pattern ────────────────────────────────────

(display "5. Long-running simulation with recoverable errors\n")
(display "   (10 steps; step 5 and 8 hit a singular matrix)\n")

(define total-steps 0)
(define recovered-count 0)

(define (run-simulation n-steps)
  (let loop ((step 1) (state 1.0))
    (when (<= step n-steps)
      (let ((new-state
        (with-restarts
          ((use-last-good "Use last good state" state)
           (use-zero      "Use zero"            0.0))
          (handler-bind
            (('singular-matrix (lambda (c)
                (set! recovered-count (+ recovered-count 1))
                (invoke-restart 'use-last-good))))
            (if (member step '(5 8))
                (condition-error 'singular-matrix '() "step singular")
                (* state 1.1))))))
        (set! total-steps (+ total-steps 1))
        (loop (+ step 1) new-state)))))

(run-simulation 10)
(display "   total steps: ")    (display total-steps)     (newline)
(display "   recoveries: ")     (display recovered-count) (newline)
(display "   (should be 10 steps, 2 recoveries)") (newline)
(newline)

(display "Done.") (newline)
