;;; (curry llm)'s internal JSON encoder `j` builds request bodies and tool
;;; results. It is not exported, so this reads its definition out of the
;;; library source and evaluates it on its own.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (library-body forms)
  ;; (define-library (curry llm) ... (begin body ...)) -> body forms
  (let loop ((decls (cddr (car forms))))
    (cond ((null? decls) '())
          ((and (pair? (car decls)) (eq? (caar decls) 'begin)) (cdar decls))
          (else (loop (cdr decls))))))

(define (read-all path)
  (call-with-input-file path
    (lambda (in)
      (let loop ((acc '()))
        (let ((f (read in)))
          (if (eof-object? f) (reverse acc) (loop (cons f acc))))))))

(define llm-path
  (let ((arg (and (pair? (command-line)) (pair? (cdr (command-line))) (cadr (command-line)))))
    (or arg "lib/curry/modules/curry/llm.scm")))

(define j-form
  (let loop ((forms (library-body (read-all llm-path))))
    (cond ((null? forms) (error "no (define (j val) ...) found in" llm-path))
          ((and (pair? (car forms)) (eq? (caar forms) 'define)
                (pair? (cadar forms)) (eq? (car (cadar forms)) 'j))
           (car forms))
          (else (loop (cdr forms))))))

(eval j-form (interaction-environment))

(check "integer" (j 42) "42")
(check "float keeps all digits" (j 0.1) "0.1")
(check "integer-valued float" (j 1000.0) "1000.0")
(check "exact ratio is written as a float" (j (/ 1 3)) "0.3333333333333333")
(check "ratio too large for a double is null" (j (/ (expt 10 400) 3)) "null")
(check "negative ratio too large for a double is null" (j (- (/ (expt 10 400) 3))) "null")
(check "tiny ratio is a valid float" (j (/ 1 (expt 10 400))) "0.0")
(check "+inf.0 is null" (j +inf.0) "null")
(check "-inf.0 is null" (j -inf.0) "null")
(check "+nan.0 is null" (j +nan.0) "null")
(check "complex number is null" (j (make-rectangular 1 2)) "null")
(check "numbers inside an array and an object"
       (j (vector 1 +inf.0 (/ 1 4) '(("x" . +nan.0) ("y" . 2.5))))
       "[1,null,0.25,{\"x\":null,\"y\":2.5}]")
(check "string and boolean unchanged" (j (vector "a\"b" #t #f)) "[\"a\\\"b\",true,false]")

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
