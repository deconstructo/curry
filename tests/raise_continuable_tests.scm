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
       (call/cc (lambda (k)
         (with-exception-handler (lambda (e) (if (eq? e 'second) (k 1) 'again))
           (lambda () (raise-continuable 'first) (raise 'second)))))
       1)
(check "a handler that returns from that later non-continuable raise: a secondary exception"
       (guard (e ((error-object? e) (error-object-code e)))
         (with-exception-handler (lambda (e) 1)
           (lambda () (raise-continuable 'first) (raise 'second))))
       'handler-returned)
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
         (call/cc (lambda (k)
           (with-exception-handler
             (lambda (e) (if (eq? e 'cont) (guard (x (#t 'inner)) (raise 'q)) (k 'unwound)))
             (lambda () (raise-continuable 'cont) (raise 'second))))))
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
                  (call/cc (lambda (k)
                    (with-exception-handler
                      (lambda (e) (if (eq? e 'cont) (guard (x (#t 'inner)) (raise 'q)) (k (list 'weh-got e))))
                      (lambda () (list (raise-continuable 'cont) (raise 'second)))))))))
         (list (f) (f)))
       '((weh-got second) (weh-got second)))

;;; A handler that escapes through a continuation captured inside the thunk:
;;; the frame's own handler must be active again afterwards, and a raise that
;;; unwinds to it must find its VM state intact.
(check "escaping to a continuation inside the thunk reinstates the frame"
       (let ((k #f) (log '()))
         (let ((r (call/cc (lambda (out)
                  (with-exception-handler
                    (lambda (e)
                      (if (eq? e 'cont)
                          (k 'escaped)
                          (begin (set! log (cons (list 'weh e) log)) (out 'weh-value))))
                    (lambda ()
                      (let ((v (call/cc (lambda (kk) (set! k kk) (raise-continuable 'cont)))))
                        (list v (raise 'later)))))))))
           (list r log)))
       '(weh-value ((weh later))))
(check "the same with the frame one level deep inside another"
       (guard (outer (#t (list 'outer outer)))
         (call/cc (lambda (out)
           (with-exception-handler
             (lambda (e) 'outer-weh)
             (lambda ()
               (with-exception-handler
                 (lambda (e) (if (eq? e 'cont) (guard (g (#t 'inner)) (raise 'q)) (out 'inner-weh)))
                 (lambda () (list (raise-continuable 'cont) (raise 'z)))))))))
       'inner-weh)
(check "a nested frame's handler using guard, then a normal exit and a raise outside"
       (guard (outer (#t (list 'outer outer)))
         (list
           (with-exception-handler
             (lambda (e) 'outer-weh)
             (lambda ()
               (with-exception-handler
                 (lambda (e) (guard (g (#t 'inner)) (raise 'q)))
                 (lambda () (raise-continuable 'cont)))))
           (raise 'after)))
       '(outer after))
(check "leaving a with-exception-handler through a continuation does not leave its handler behind"
       (let* ((r1 (call/cc (lambda (k) (with-exception-handler (lambda (e) 0) (lambda () (k 1))))))
              (r2 (guard (e (#t (list 'caught e))) (raise 'x))))
         (list r1 r2))
       '(1 (caught x)))
(check "a handler that is not a procedure is an error at the raise"
       (guard (e (#t 'raised))
         (with-exception-handler #t (lambda () (raise-continuable 1))))
       'raised)

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

;;; raise (not continuable) still unwinds to the handler, which runs after the
;;; raise point is gone. If it returns, that is an error (#407); to carry on it
;;; has to escape.
(check "raise still unwinds to the handler, which can escape"
       (call/cc (lambda (k)
         (with-exception-handler (lambda (e) (k (list 'handled e)))
           (lambda () (+ 1 (raise 'oops))))))
       '(handled oops))
(check "an error from a primitive is still handled by unwinding"
       (call/cc (lambda (k)
         (with-exception-handler (lambda (e) (k 'handled)) (lambda () (car '())))))
       'handled)
(check "raise, handler returns: a secondary exception, not the handler's value"
       (guard (e ((error-object? e) (error-object-code e)))
         (with-exception-handler (lambda (e) (list 'handled e))
           (lambda () (+ 1 (raise 'oops)))))
       'handler-returned)
(check "an error from a primitive, handler returns: a secondary exception"
       (guard (e ((error-object? e) (error-object-code e)))
         (with-exception-handler (lambda (e) 'handled) (lambda () (car '()))))
       'handler-returned)

;;; In a loop, so the code is compiled (and native with the JIT).
(define (sum-with-handler n)
  (with-exception-handler (lambda (e) (* e 2))
    (lambda ()
      (let loop ((i 0) (acc 0))
        (if (= i n) acc (loop (+ i 1) (+ acc (raise-continuable i))))))))
(check "a loop of 300 continuable raises" (sum-with-handler 300) 89700)
(check "and again after it has been compiled" (sum-with-handler 300) 89700)

;;; C-level frames that only clean up and re-raise are looked through, as
;;; dynamic-wind's is: the handler runs in place and its value comes back to the
;;; raise point (#407; it was an error once a returning handler became one).
(import (curry sync) (curry conditions))
(define (wch thunk) (with-exception-handler (lambda (e) 10) thunk))
(check "through with-output-to-string"
       (wch (lambda () (let ((s (with-output-to-string (lambda () (display (raise-continuable 'x)))))) s)))
       "10")
(check "with-output-to-string restores the output port afterwards"
       (let ((before (current-output-port)))
         (wch (lambda () (with-output-to-string (lambda () (raise-continuable 'x)))))
         (eq? before (current-output-port)))
       #t)
(check "through call-with-port"
       (wch (lambda () (call-with-port (open-input-string "a") (lambda (p) (+ 1 (raise-continuable 'x))))))
       11)
(check "through handler-bind"
       (wch (lambda () (handler-bind ((error (lambda (c) 0))) (+ 1 (raise-continuable 'x)))))
       11)
(check "through with-mutex, which is unlocked afterwards"
       (let ((m (make-mutex)))
         (let ((r (wch (lambda () (with-mutex m (lambda () (+ 1 (raise-continuable 'x))))))))
           (list r (with-mutex m (lambda () 'unlocked)))))
       '(11 unlocked))
(check "through eval"
       (wch (lambda () (+ 1 (eval '(+ 1 (raise-continuable 'x)) (interaction-environment)))))
       12)
(check "through tree-walked parameterize"
       (let ((p (make-parameter 1)))
         (list (wch (lambda () (tree-eval (list 'parameterize (list (list p 5)) '(+ 1 (raise-continuable 'x))))))
               (p)))
       '(11 1))
(define rc-load-file
  (string-append (or (get-environment-variable "TMPDIR") "/tmp") "/raise_continuable_load_tmp.scm"))
(call-with-output-file rc-load-file (lambda (p) (write '(+ 1 (raise-continuable 'x)) p)))
(check "through load" (wch (lambda () (load rc-load-file))) 11)
(delete-file rc-load-file)

(check "map/seq over a long list: the handler's values come back to each raise point"
       (wch (lambda () (length (map/seq (lambda (i) (raise-continuable i)) (vector->list (make-vector 50 1))))))
       50)
(check "a plain raise through with-output-to-string still reaches the handler, which must escape"
       (call/cc (lambda (k)
         (with-exception-handler (lambda (e) (k (list 'handled e)))
           (lambda () (with-output-to-string (lambda () (raise 'boom)))))))
       '(handled boom))

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
