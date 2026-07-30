;;; (srfi s18 multithreading)

(import (srfi s18 multithreading))

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

(define t (make-thread (lambda () (+ 1 2 3))))
(check "make-thread produces a thread object" (thread? t) #t)
(thread-start! t)
(check "thread-join! returns the thunk's result" (thread-join! t) 6)

; a thunk that raises must still mark the thread done, not deadlock
; thread-join! forever waiting on a thread that already exited
(define bad-t (thread-start! (make-thread (lambda () (error "thread failed")))))
(check "thread-join! returns rather than hanging when the thunk raised"
       (thread-join! bad-t)
       #f)

(define cv (make-condition-variable))
(check "make-condition-variable produces a condvar" (condition-variable? cv) #t)

(thread-sleep! 0.02)
(check "thread-sleep! returns" 'ok 'ok)

(check "thread-terminate! is unsupported and raises"
       (guard (e (#t 'caught)) (thread-terminate! t))
       'caught)

;; Several threads racing on a shared mutex-protected counter
(define counter 0)
(define counter-mx (make-mutex))
(define threads
  (map (lambda (i)
         (thread-start! (make-thread (lambda ()
           (mutex-lock! counter-mx)
           (set! counter (+ counter 1))
           (mutex-unlock! counter-mx)))))
       (list 1 2 3 4 5)))
(for-each thread-join! threads)
(check "five mutex-serialized increments land exactly once each" counter 5)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
