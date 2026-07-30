(define-library (srfi s133 vectors)
  (import (scheme base))
  (export
    ; already native / (scheme base)
    make-vector vector vector? vector-length vector-ref vector-set!
    vector->list list->vector vector-fill! vector-copy vector-copy!
    vector-append vector-map vector-for-each
    ; additions
    vector-empty? vector=
    vector-swap! reverse! vector-reverse! vector-reverse!*
    vector-index vector-index-right vector-count
    vector-any vector-every
    vector-fold vector-fold-right
    vector-binary-search vector-concatenate
    vector-unfold vector-unfold-right)
  (begin

    (define (vector-empty? v) (= (vector-length v) 0))

    (define (vector= elt=? . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((len (vector-length (car vs))))
            (and (every (lambda (v) (= (vector-length v) len)) (cdr vs))
                 (let loop ((i 0))
                   (or (= i len)
                       (and (every (lambda (v) (elt=? (vector-ref (car vs) i) (vector-ref v i))) (cdr vs))
                            (loop (+ i 1)))))))))

    (define (every pred lst)
      (or (null? lst) (and (pred (car lst)) (every pred (cdr lst)))))

    (define (vector-swap! v i j)
      (let ((tmp (vector-ref v i)))
        (vector-set! v i (vector-ref v j))
        (vector-set! v j tmp)))

    (define (vector-reverse! v . range)
      (let ((start (if (pair? range) (car range) 0))
            (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (if (< i j) (begin (vector-swap! v i j) (loop (+ i 1) (- j 1)))))
        v))

    (define (vector-reverse!* v . range) (apply vector-reverse! v range))

    (define (reverse! v) (vector-reverse! v))

    (define (%range v range)
      (values (if (pair? range) (car range) 0)
              (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v))))

    (define (vector-index pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start))
            (cond ((= i end) #f)
                  ((pred (vector-ref v i)) i)
                  (else (loop (+ i 1))))))))

    (define (vector-index-right pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i (- end 1)))
            (cond ((< i start) #f)
                  ((pred (vector-ref v i)) i)
                  (else (loop (- i 1))))))))

    (define (vector-count pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start) (n 0))
            (if (= i end) n (loop (+ i 1) (if (pred (vector-ref v i)) (+ n 1) n)))))))

    (define (vector-any pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start))
            (and (< i end) (or (pred (vector-ref v i)) (loop (+ i 1))))))))

    (define (vector-every pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start))
            (or (>= i end) (and (pred (vector-ref v i)) (loop (+ i 1))))))))

    (define (vector-fold kons knil v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start) (acc knil))
            (if (= i end) acc (loop (+ i 1) (kons acc (vector-ref v i))))))))

    (define (vector-fold-right kons knil v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i (- end 1)) (acc knil))
            (if (< i start) acc (loop (- i 1) (kons acc (vector-ref v i))))))))

    ;; requires v sorted ascending by `less?` over [start, end)
    (define (vector-binary-search v key less? . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((lo start) (hi end))
            (if (>= lo hi)
                #f
                (let ((mid (quotient (+ lo hi) 2)))
                  (let ((x (vector-ref v mid)))
                    (cond ((less? x key) (loop (+ mid 1) hi))
                          ((less? key x) (loop lo mid))
                          (else mid)))))))))

    (define (vector-concatenate vs) (apply vector-append vs))

    (define (vector-unfold f len . seeds)
      (let ((v (make-vector len)))
        (let loop ((i 0) (ss seeds))
          (if (< i len)
              (call-with-values (lambda () (apply f i ss))
                (lambda (val . next-ss) (vector-set! v i val) (loop (+ i 1) next-ss)))))
        v))

    (define (vector-unfold-right f len . seeds)
      (let ((v (make-vector len)))
        (let loop ((i (- len 1)) (ss seeds))
          (if (>= i 0)
              (call-with-values (lambda () (apply f i ss))
                (lambda (val . next-ss) (vector-set! v i val) (loop (- i 1) next-ss)))))
        v))))
