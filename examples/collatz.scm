; Single Collatz step
(define (collatz-step n)
  (if (even? n)
      (/ n 2)
      (+ (* 3 n) 1)))

; Full sequence down to 1
(define (collatz-seq n)
  (let loop ((n n) (acc (list n)))
    (if (= n 1)
        (reverse acc)
        (loop (collatz-step n)
              (cons (collatz-step n) acc)))))

; Stopping time only — no need to build the list if you don't want it
(define (collatz-steps n)
  (let loop ((n n) (count 0))
    (if (= n 1)
        count
        (loop (collatz-step n) (+ count 1)))))

; Test all n in [2, limit] — prints the worst offenders
(define (test-collatz start limit)
  (let loop ((n start) (worst-n 1) (worst-steps 0))
    (if (> n limit)
        (begin
          (display "Worst under ")
          (display limit)
          (display ": n=")
          (display worst-n)
          (display " at ")
          (display worst-steps)
          (display " steps")
          (newline))
        (let ((steps (collatz-steps n)))
          (when (= (modulo n 100000) 0)
            (display "checked up to ")
            (display n)
            (newline))
          (if (> steps worst-steps)
              (loop (+ n 1) n steps)
              (loop (+ n 1) worst-n worst-steps))))))

; Demo
(display (collatz-seq 27))
(newline)
(display "steps for 27: ")
(display (collatz-steps 27))
(newline)
