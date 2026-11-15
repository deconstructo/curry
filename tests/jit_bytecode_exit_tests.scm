;;; The main thread finishes while four actors are still running compiled
;;; code. The process must exit normally: the JIT's code must not be freed
;;; under the running threads. Passes by exit status (a crash is 139).
(import (curry sync))
(define (spin n acc) (if (= n 0) acc (spin (- n 1) (+ acc 1))))
(for-each (lambda (id) (spawn (lambda () (spin 300000000 0)))) '(1 2 3 4))
(define (wait n) (if (= n 0) 0 (wait (- n 1))))
(wait 3000000)
(display "done") (newline)
