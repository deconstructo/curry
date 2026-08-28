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
    vector-unfold vector-unfold-right vector-unfold! vector-unfold-right!
    vector-reverse-copy vector-append-subvectors vector-map! vector-cumulate
    vector-skip vector-skip-right vector-partition
    reverse-vector->list reverse-list->vector)
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
        v))

    ;; In-place variants of vector-unfold/vector-unfold-right above --
    ;; write into an existing v's [start, end) instead of allocating a
    ;; new vector.
    (define (vector-unfold! f v start end . seeds)
      (let loop ((i start) (ss seeds))
        (when (< i end)
          (call-with-values (lambda () (apply f i ss))
            (lambda (val . next-ss) (vector-set! v i val) (loop (+ i 1) next-ss))))))

    (define (vector-unfold-right! f v start end . seeds)
      (let loop ((i (- end 1)) (ss seeds))
        (when (>= i start)
          (call-with-values (lambda () (apply f i ss))
            (lambda (val . next-ss) (vector-set! v i val) (loop (- i 1) next-ss))))))

    (define (vector-reverse-copy v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let* ((len (- end start)) (result (make-vector len)))
            (let loop ((i start) (j (- len 1)))
              (when (< i end)
                (vector-set! result j (vector-ref v i))
                (loop (+ i 1) (- j 1))))
            result))))

    ;; (vector-append-subvectors v1 start1 end1 v2 start2 end2 ...) --
    ;; like vector-append, but each argument is a (vector start end)
    ;; triple rather than a whole vector, avoiding an intermediate
    ;; vector-copy per input the naive (vector-append (vector-copy v1
    ;; start1 end1) ...) approach would need. Two passes: compute the
    ;; total length first so the result is allocated exactly once, then
    ;; fill each region via vector-copy!.
    (define (vector-append-subvectors . args)
      (define (triples lst)
        (if (null? lst) '() (cons (list (car lst) (cadr lst) (caddr lst)) (triples (cdddr lst)))))
      (let* ((ts (triples args))
             (total (fold-left (lambda (acc t) (+ acc (- (caddr t) (cadr t)))) 0 ts))
             (result (make-vector total)))
        (let loop ((ts ts) (pos 0))
          (if (null? ts)
              result
              (let* ((t (car ts)) (v (car t)) (s (cadr t)) (e (caddr t)))
                (vector-copy! result pos v s e)
                (loop (cdr ts) (+ pos (- e s))))))))

    ;; Destructive map into the FIRST vector -- like vector-map, but v's
    ;; own contents are overwritten instead of a new vector allocated.
    ;; Iterates only over the shortest vector's length when the argument
    ;; vectors have unequal lengths, per SRFI-133's own convention for
    ;; this procedure -- checked directly, this is NOT what curry's own
    ;; native (core, non-SRFI) vector-map does with mismatched lengths
    ;; (it raises instead), so don't assume the two agree on that case.
    (define (vector-map! f v . vs)
      (let ((len (apply min (vector-length v) (map vector-length vs))))
        (let loop ((i 0))
          (when (< i len)
            (vector-set! v i (apply f (vector-ref v i) (map (lambda (v2) (vector-ref v2 i)) vs)))
            (loop (+ i 1))))
        v))

    ;; Cumulative fold: result[0] = (f knil v[0]), result[i] = (f
    ;; result[i-1] v[i]) -- a running total/max/etc. as a new vector the
    ;; same length as v, not a single final accumulator the way
    ;; vector-fold's own single return value is.
    (define (vector-cumulate f knil v)
      (let* ((len (vector-length v)) (result (make-vector len)))
        (let loop ((i 0) (acc knil))
          (if (= i len)
              result
              (let ((acc2 (f acc (vector-ref v i))))
                (vector-set! result i acc2)
                (loop (+ i 1) acc2))))))

    ;; The complement of vector-index/vector-index-right: the index of
    ;; the first (resp. last) element pred does NOT match, rather than
    ;; does.
    (define (vector-skip pred v . range) (apply vector-index (lambda (x) (not (pred x))) v range))
    (define (vector-skip-right pred v . range)
      (apply vector-index-right (lambda (x) (not (pred x))) v range))

    ;; Returns two values: a new vector with every pred-satisfying
    ;; element first (stable, original relative order preserved), then
    ;; every non-satisfying element (also stable), and the count of
    ;; satisfying elements -- that count is also the boundary index
    ;; between the two groups in the result vector.
    (define (vector-partition pred v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let* ((len (- end start))
                 (result (make-vector len))
                 (count (vector-count pred v start end)))
            (let loop ((i start) (yes-idx 0) (no-idx count))
              (if (= i end)
                  (values result count)
                  (if (pred (vector-ref v i))
                      (begin (vector-set! result yes-idx (vector-ref v i))
                             (loop (+ i 1) (+ yes-idx 1) no-idx))
                      (begin (vector-set! result no-idx (vector-ref v i))
                             (loop (+ i 1) yes-idx (+ no-idx 1))))))))))

    ;; A list of v's elements in reverse order -- consing while walking
    ;; forward naturally produces the reverse, so this needs no separate
    ;; reverse step over vector->list's own result.
    (define (reverse-vector->list v . range)
      (call-with-values (lambda () (%range v range))
        (lambda (start end)
          (let loop ((i start) (acc '()))
            (if (= i end) acc (loop (+ i 1) (cons (vector-ref v i) acc)))))))

    ;; A vector containing lst's elements in reverse order -- placing
    ;; each list element from the END of the vector backward as we walk
    ;; the list forward achieves this in one pass, without building a
    ;; forward vector first and reversing it.
    (define (reverse-list->vector lst)
      (let* ((len (length lst)) (v (make-vector len)))
        (let loop ((l lst) (i (- len 1)))
          (if (null? l) v (begin (vector-set! v i (car l)) (loop (cdr l) (- i 1)))))))))
