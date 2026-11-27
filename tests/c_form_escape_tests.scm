;;; #428: a continuation escape that leaves a C-level with-... form runs that
;;; form's cleanup, as an exception already did.
(import (scheme base) (scheme write) (scheme file) (scheme load) (curry sync) (curry conditions))

(define pass 0)
(define fail 0)
(define (check name got want)
  (if (equal? got want)
      (set! pass (+ pass 1))
      (begin (set! fail (+ fail 1))
             (display "FAIL: ") (display name) (display " got ") (write got)
             (display " want ") (write want) (newline))))

;; ---- with-output-to-string ----
(define out0 (current-output-port))
(check "wots: value" (call/cc (lambda (k) (with-output-to-string (lambda () (k 'out))))) 'out)
(check "wots: stdout restored" (eq? out0 (current-output-port)) #t)
(check "wots: normal" (with-output-to-string (lambda () (display "x"))) "x")

;; and through a guard that catches in place
(check "wots: guard raise-continuable"
       (guard (e (#t 'g)) (with-output-to-string (lambda () (raise-continuable 'oops))))
       'g)
(check "wots: stdout restored after guard" (eq? out0 (current-output-port)) #t)

;; ---- with-mutex ----
(define m (make-mutex))
(check "mutex: value" (call/cc (lambda (k) (with-mutex m (lambda () (k 'out))))) 'out)
(check "mutex: unlocked after escape" (mutex-trylock! m) #t)
(mutex-unlock! m)
(check "mutex: normal" (with-mutex m (lambda () 'v)) 'v)
(check "mutex: unlocked after normal" (mutex-trylock! m) #t)
(mutex-unlock! m)
(check "mutex: raise" (guard (e (#t 'r)) (with-mutex m (lambda () (raise 'x)))) 'r)
(check "mutex: unlocked after raise" (mutex-trylock! m) #t)
(mutex-unlock! m)

;; ---- with-output-to-file / with-input-from-file ----
(define f "c_form_escape_tmp.txt")
(call/cc (lambda (k) (with-output-to-file f (lambda () (display "abc") (k 'out)))))
(check "to-file: stdout restored" (eq? out0 (current-output-port)) #t)
(check "to-file: closed and flushed"
       (call-with-input-file f (lambda (p) (read-line p)))
       "abc")
(define in0 (current-input-port))
(call/cc (lambda (k) (with-input-from-file f (lambda () (k 'out)))))
(check "from-file: stdin restored" (eq? in0 (current-input-port)) #t)
(delete-file f)

;; ---- call-with-input-file / call-with-output-file ----
(define g "c_form_escape_tmp2.txt")
(call/cc (lambda (k) (call-with-output-file g (lambda (port) (write-string "xyz" port) (k 'out)))))
(check "call-with-output-file: flushed after escape"
       (call-with-input-file g (lambda (port) (read-line port))) "xyz")
(define cwf-port #f)
(call/cc (lambda (k) (call-with-input-file g (lambda (port) (set! cwf-port port) (k 'out)))))
(check "call-with-input-file: closed after escape" (input-port-open? cwf-port) #f)
(check "call-with-output-file: closed after raise"
       (guard (e (#t (output-port-open? cwf-port))) 
         (call-with-output-file g (lambda (port) (set! cwf-port port) (raise 'x))))
       #f)
(check "call-with-output-file: closed after raise (port)" (output-port-open? cwf-port) #f)
(check "call-with-input-file: normal"
       (call-with-input-file g (lambda (port) 'ok)) 'ok)
(delete-file g)

;; ---- call-with-port ----
(define p (open-input-string "hello"))
(call/cc (lambda (k) (call-with-port p (lambda (port) (k 'out)))))
(check "call-with-port: closed after escape" (input-port-open? p) #f)

;; ---- with-assumptions ----
(define x (sym-var 'x))
(call/cc (lambda (k) (with-assumptions ((x positive)) (k 'out))))
(check "with-assumptions: restored after escape" (can-assume? x 'positive) #f)

;; ---- handler-bind ----
(define hb-log '())
(call/cc (lambda (k)
  (handler-bind (('gc-pressure (lambda (c) (set! hb-log (cons 'stale hb-log)))))
    (k 'out))))
(signal (make-condition 'gc-pressure '() "later"))
(check "handler-bind: unbound after escape" hb-log '())

;; ---- with-restarts ----
(call/cc (lambda (k) (with-restarts ((use-zero "zero" 0)) (k 'out))))
(check "with-restarts: restart gone after escape"
       (guard (e (#t 'no-restart)) (invoke-restart 'use-zero) 'found)
       'no-restart)
(check "with-restarts: still works"
       (with-restarts ((use-one "one" 1)) (invoke-restart 'use-one))
       1)

;; ---- call-with-precision (only with MPFR) ----
(define has-prec (guard (e (#t #f)) (call-with-precision 100 (lambda () #t))))
(when has-prec
  (let ((p0 (current-precision)))
    (call/cc (lambda (k) (call-with-precision 200 (lambda () (k 'out)))))
    (check "precision: restored after escape" (current-precision) p0)))

;; ---- normal and exception paths, and an escape that leaves only the inner form ----
(define nlog '())
(handler-bind (('gc-pressure (lambda (c) (set! nlog (cons 'outer nlog)))))
  (call/cc (lambda (k)
    (handler-bind (('gc-pressure (lambda (c) (set! nlog (cons 'inner nlog)))))
      (k 'out))))
  (signal (make-condition 'gc-pressure '() "x")))
(check "handler-bind: inner escape leaves outer bound" nlog '(outer))
(set! nlog '())
(handler-bind (('gc-pressure (lambda (c) (set! nlog (cons 'h nlog)))))
  'normal)
(signal (make-condition 'gc-pressure '() "x"))
(check "handler-bind: unbound after normal exit" nlog '())
(guard (e (#t 'r)) (handler-bind (('gc-pressure (lambda (c) 1))) (raise 'boom)))
(signal (make-condition 'gc-pressure '() "x"))
(check "handler-bind: unbound after raise" nlog '())
(check "with-assumptions: normal exit"
       (begin (with-assumptions ((x positive)) 1) (can-assume? x 'positive)) #f)
(check "with-assumptions: raise"
       (begin (guard (e (#t 1)) (with-assumptions ((x positive)) (raise 'a))) (can-assume? x 'positive)) #f)
(define p2 (open-input-string "q"))
(call-with-port p2 (lambda (port) 1))
(check "call-with-port: closed after normal exit" (input-port-open? p2) #f)
(define p3 (open-input-string "q"))
(guard (e (#t 1)) (call-with-port p3 (lambda (port) (raise 'a))))
(check "call-with-port: closed after raise" (input-port-open? p3) #f)

;; ---- nested: both forms clean up, innermost first ----
(call/cc (lambda (k)
  (with-mutex m (lambda ()
    (with-output-to-string (lambda () (k 'out)))))))
(check "nested: stdout restored" (eq? out0 (current-output-port)) #t)
(check "nested: mutex unlocked" (mutex-trylock! m) #t)
(mutex-unlock! m)

;; ---- inside a dynamic-wind: before/after still pair up ----
(define wlog '())
(call/cc (lambda (k)
  (dynamic-wind
    (lambda () (set! wlog (cons 'in wlog)))
    (lambda () (with-output-to-string (lambda () (k 'out))))
    (lambda () (set! wlog (cons 'out wlog))))))
(check "dynamic-wind around a C form" (reverse wlog) '(in out))
(check "dynamic-wind: stdout restored" (eq? out0 (current-output-port)) #t)

;; ---- a raise from the cleanup context still reaches outer handlers ----
(check "after thunk raise through a C form"
       (guard (o (#t (list 'outer o)))
         (call/cc (lambda (k)
           (with-output-to-string
             (lambda ()
               (dynamic-wind (lambda () #f)
                             (lambda () (k 'out))
                             (lambda () (raise 'from-after))))))))
       '(outer from-after))
(check "stdout restored after that" (eq? out0 (current-output-port)) #t)

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(if (> fail 0) (exit 1))
