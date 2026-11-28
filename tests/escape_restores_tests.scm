;;; A continuation escape puts back the state that atomically and the macro
;;; transformers keep (#433).
;;;
;;; stm_atomically restored current_tx for a return or a raise, not for an
;;; escape through a continuation: the aborted transaction stayed current, so
;;; its writes were visible outside any transaction, and the next atomically on
;;; the thread ran inside the stale transaction and never committed.

(import (curry sync))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

;; Value of a tvar as another thread sees it (committed state only). The
;; result goes through a global: a set! on a captured local does not reach the
;; spawning thread.
(define seen-result #f)
(define seen-done (make-semaphore 0))
(define (seen-by-actor tv)
  (spawn (lambda () (set! seen-result (tvar-read tv)) (sem-post! seen-done)))
  (sem-wait! seen-done)
  seen-result)

(define tv (make-tvar 0))
(check "escape out of atomically returns the continuation's value"
       (call/cc (lambda (k) (atomically (lambda () (tvar-write! tv 1) (k 'escaped)))))
       'escaped)
(check "the aborted transaction's write is not visible"
       (tvar-read tv) 0)
(check "a later atomically sees the old value"
       (atomically (lambda () (tvar-read tv))) 0)
(check "and commits"
       (begin (atomically (lambda () (tvar-write! tv 5))) (seen-by-actor tv)) 5)

;; Repeatedly, and from a nested call/cc.
(define tv2 (make-tvar 0))
(let loop ((i 0))
  (when (< i 20)
    (call/cc (lambda (k) (atomically (lambda () (tvar-write! tv2 (+ i 100)) (k #f)))))
    (loop (+ i 1))))
(check "twenty aborted transactions leave the value alone" (seen-by-actor tv2) 0)
(atomically (lambda () (tvar-write! tv2 7)))
(check "the next commit lands" (seen-by-actor tv2) 7)

;; A raise still restores it, as before.
(define tv3 (make-tvar 0))
(guard (e (#t #f)) (atomically (lambda () (tvar-write! tv3 1) (raise 'boom))))
(atomically (lambda () (tvar-write! tv3 2)))
(check "after a raise out of atomically" (seen-by-actor tv3) 2)

;; A nested atomically is part of the outer one; escaping from the inner one
;; aborts the whole transaction.
(define tv4 (make-tvar 0))
(call/cc (lambda (k)
  (atomically (lambda ()
    (tvar-write! tv4 1)
    (atomically (lambda () (tvar-write! tv4 2) (k #f)))))))
(check "an escape from a nested atomically aborts the outer one" (seen-by-actor tv4) 0)
(atomically (lambda () (tvar-write! tv4 9)))
(check "and a later one commits" (seen-by-actor tv4) 9)

;; The normal path is unchanged.
(check "atomically returns the thunk's value"
       (atomically (lambda () (tvar-write! tv 11) (tvar-read tv))) 11)
(check "and commits" (seen-by-actor tv) 11)

;;; An escape out of a macro transformer, then macros still work.
(define esc #f)
(define-syntax boom (lambda (stx) (esc 'escaped-from-transformer)))
(check "escape out of a procedural transformer"
       (call/cc (lambda (k) (set! esc k) (tree-eval '(boom)))) 'escaped-from-transformer)
(check "syntax-rules afterwards"
       (let ((y 10)) (let-syntax ((m (syntax-rules () ((_) y)))) (m))) 10)
(define-syntax boom2 (lambda (stx) (syntax-case stx () ((_) (esc 'escaped-2)))))
(check "escape out of a syntax-case transformer"
       (call/cc (lambda (k) (set! esc k) (tree-eval '(boom2)))) 'escaped-2)
(check "syntax-rules afterwards, tree-walked"
       (tree-eval '(let ((z 3)) (let-syntax ((m (syntax-rules () ((_) z)))) (m)))) 3)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
