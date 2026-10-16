;;; Issue #292: the VM's call stack used to be a fixed 256 frames (and the
;;; value stack 4096 slots), so ordinary non-tail recursion deeper than 256
;;; calls raised stack-overflow. Both are now large virtual-memory
;;; reservations (VM_FRAMES_MAX = 1,048,576 frames).

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (len l) (if (null? l) 0 (+ 1 (len (cdr l)))))
(define (mk n acc) (if (= n 0) acc (mk (- n 1) (cons n acc))))
(define (my-map f l) (if (null? l) '() (cons (f (car l)) (my-map f (cdr l)))))

(check "recursion 300 deep (failed before #292)" (len (mk 300 '())) 300)
(check "recursion 100,000 deep" (len (mk 100000 '())) 100000)
(check "recursion 500,000 deep" (len (mk 500000 '())) 500000)
(check "non-tail map over 200,000 elements" (len (my-map (lambda (x) (* x x)) (mk 200000 '()))) 200000)

;; Values held only in deep VM stack slots must survive collections: the
;; stack is mmap memory that Boehm scans only through vm.c's root callback.
(define (build n)
  (if (= n 0) '()
      (let ((x (make-string 8 (integer->char (+ 65 (modulo n 26))))))
        (if (= 0 (modulo n 5000)) (begin (gc) (make-vector 1000 n)))
        (cons x (build (- n 1))))))
(define (all-ok? l n)
  (cond ((null? l) (= n 0))
        ((string=? (car l) (make-string 8 (integer->char (+ 65 (modulo n 26)))))
         (all-ok? (cdr l) (- n 1)))
        (else #f)))
(check "deep stack values survive forced collections" (all-ok? (build 200000) 200000) #t)

;; Past the limit: a catchable stack-overflow, then the VM keeps working.
(define (deep-sum n) (if (= n 0) 0 (+ 1 (deep-sum (- n 1)))))
(check "recursion past the frame limit raises stack-overflow"
       (guard (e (#t (error-object-code e))) (deep-sum 3000000))
       'stack-overflow)
(check "VM works normally after an overflow" (len (mk 1000 '())) 1000)
(check "deep recursion works again after an overflow" (len (mk 100000 '())) 100000)

;; The backtrace of a deep error keeps the outermost and innermost frames
;; plus one "... N more frames ..." entry, not one entry per frame.
(define (fail-deep n) (if (= n 0) (car 5) (+ 1 (fail-deep (- n 1)))))
(define bt (guard (e (#t (condition-backtrace e))) (fail-deep 100000)))
(check "deep backtrace is capped" (< (length bt) 100) #t)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(if (> fail 0) (exit 1) (exit 0))
