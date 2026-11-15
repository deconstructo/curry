;;; raise-continuable returns the handler's value to the raise point (#377).
;;;
;;; with-exception-handler unwinds to its own frame before it calls the handler,
;;; so the raise point was gone, and (raise-continuable obj) behaved like raise:
;;; the handler's value became the value of the whole with-exception-handler
;;; call, and the code after the raise was skipped.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;;; The example from R7RS 6.11.
(check "R7RS example: the handler's value is returned to the raise point"
       (let ((out '()))
         (let ((v (with-exception-handler
                    (lambda (con)
                      (set! out (cons (if (string? con) con 'a-warning) out))
                      42)
                    (lambda () (+ (raise-continuable "should be a number") 23)))))
           (list v out)))
       '(65 ("should be a number")))

(check "the issue's example" 
       (with-exception-handler (lambda (e) 10) (lambda () (+ 1 (raise-continuable 0)))) 11)
(check "a handler computing from the raised object"
       (with-exception-handler (lambda (e) (* e 2)) (lambda () (+ 1 (raise-continuable 20)))) 41)
(check "two raises in one expression"
       (with-exception-handler (lambda (e) (+ e 1)) (lambda () (+ 1 (raise-continuable 5) (raise-continuable 6)))) 14)
(check "the code after the raise runs"
       (let ((seen '()))
         (with-exception-handler (lambda (e) 0)
           (lambda () (raise-continuable 'x) (set! seen (cons 'after seen))))
         seen)
       '(after))

;;; The two spellings of with-exception-handler: the special form (compiled)
;;; and the procedure reached as a value (interpreted).
(define weh with-exception-handler)
(check "with-exception-handler as a value"
       (weh (lambda (e) 10) (lambda () (+ 1 (raise-continuable 0)))) 11)

;;; Nested handlers: the handler runs with the outer handler installed.
(check "a handler that raises continuably reaches the outer handler"
       (with-exception-handler
         (lambda (e) 1)
         (lambda ()
           (with-exception-handler
             (lambda (e) (+ 10 (raise-continuable e)))
             (lambda () (+ 100 (raise-continuable 5))))))
       111)
(check "the outer handler's value is the inner raise's value"
       (with-exception-handler
         (lambda (e) 'outer)
         (lambda ()
           (with-exception-handler
             (lambda (e) (raise-continuable (list e)))
             (lambda () (raise-continuable 3)))))
       'outer)

;;; A handler that does not return.
(check "a handler that raises a non-continuable exception reaches the guard outside"
       (guard (e (#t (list 'outer e)))
         (with-exception-handler (lambda (e) (raise (list e)))
           (lambda () (raise-continuable 7))))
       '(outer (7)))
(check "a handler that signals an error reaches the guard outside"
       (guard (e ((error-object? e) (list 'error (error-object-message e))))
         (with-exception-handler (lambda (e) (error "boom" e))
           (lambda () (raise-continuable 9))))
       '(error "boom"))
(check "a guard around the raise catches it as before"
       (guard (e (#t (list 'g e))) (raise-continuable 5)) '(g 5))
(check "a guard clause that does not match passes it on"
       (guard (outer (#t (list 'outer outer)))
         (guard (inner ((string? inner) 'string)) (raise-continuable 5)))
       '(outer 5))

;;; The handler stack is intact afterwards.
(check "a guard inside the thunk still catches a later error"
       (with-exception-handler (lambda (e) 1)
         (lambda () (raise-continuable 'x) (guard (e (#t 'caught)) (car '()))))
       'caught)
(check "a later non-continuable raise is handled by the same handler, by unwinding"
       (guard (e (#t (list 'guard e)))
         (with-exception-handler (lambda (e) 1)
           (lambda () (raise-continuable 'first) (raise 'second))))
       1)
(check "the outer guard is reached by a raise from a handler after earlier returns"
       (guard (e (#t (list 'late e)))
         (with-exception-handler (lambda (e) (if (eq? e 'again) (raise 'boom) 1))
           (lambda () (raise-continuable 'one) (raise-continuable 'two) (raise-continuable 'again))))
       '(late boom))

;;; A handler that uses guard or with-exception-handler itself must not disturb
;;; the frame it was called from (this crashed the process while the VM
;;; handler stack entry of that frame was overwritten).
(check "a handler using guard, then a plain raise handled by the same frame"
       (guard (e (#t (list 'outer e)))
         (with-exception-handler
           (lambda (e) (if (eq? e 'cont) (guard (x (#t 'inner)) (raise 'q)) 'unwound))
           (lambda () (raise-continuable 'cont) (raise 'second))))
       'unwound)
(check "a handler using guard returns its value to the raise point"
       (with-exception-handler
         (lambda (e) (guard (x (#t (list 'inner x))) (raise e)))
         (lambda () (list 'got (raise-continuable 'v))))
       '(got (inner v)))
(check "a handler using with-exception-handler itself"
       (with-exception-handler
         (lambda (e) (with-exception-handler (lambda (x) (+ x 100)) (lambda () (raise-continuable e))))
         (lambda () (+ 1 (raise-continuable 5))))
       106)
(check "the same frame handles a later raise after a nested handler ran"
       (let ((f (lambda ()
                  (with-exception-handler
                    (lambda (e) (if (eq? e 'cont) (guard (x (#t 'inner)) (raise 'q)) (list 'weh-got e)))
                    (lambda () (list (raise-continuable 'cont) (raise 'second)))))))
         (list (f) (f)))
       '((weh-got second) (weh-got second)))

;;; dynamic-wind: the handler runs inside the dynamic extent of the raise.
(check "the handler runs between the before and after thunks"
       (let ((log '()))
         (define (note x) (set! log (cons x log)))
         (with-exception-handler (lambda (e) (note 'handler) 0)
           (lambda ()
             (dynamic-wind (lambda () (note 'before))
                           (lambda () (raise-continuable 'x))
                           (lambda () (note 'after)))))
         (reverse log))
       '(before handler after))

;;; raise (not continuable) is unchanged: the handler's value is the value of
;;; the with-exception-handler call.
(check "raise still unwinds to the handler"
       (with-exception-handler (lambda (e) (list 'handled e))
         (lambda () (+ 1 (raise 'oops))))
       '(handled oops))
(check "an error from a primitive is still handled by unwinding"
       (with-exception-handler (lambda (e) 'handled) (lambda () (car '()))) 'handled)

;;; In a loop, so the code is compiled (and native with the JIT).
(define (sum-with-handler n)
  (with-exception-handler (lambda (e) (* e 2))
    (lambda ()
      (let loop ((i 0) (acc 0))
        (if (= i n) acc (loop (+ i 1) (+ acc (raise-continuable i))))))))
(check "a loop of 300 continuable raises" (sum-with-handler 300) 89700)
(check "and again after it has been compiled" (sum-with-handler 300) 89700)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
