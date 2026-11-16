;;; Loaded by tests/guard_reraise_tests.scm: (load ...) runs this through the
;;; tree-walking evaluator, not the bytecode compiler.
(define loaded-top-level
  (with-exception-handler (lambda (e) 42)
    (lambda () (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops))))))
(define (loaded-procedure)
  (with-exception-handler (lambda (e) 42)
    (lambda () (+ 1 (guard (x ((string? x) 'str)) (raise-continuable 'oops))))))
(define (loaded-arrow)
  (guard (x ((assq 'a x) => cdr)) (raise (list (cons 'a 42)))))
