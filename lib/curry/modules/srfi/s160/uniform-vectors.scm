(define-library (srfi s160 uniform-vectors)
  (import (scheme base) (srfi s4 uniform-vectors) (srfi s128 comparators)
          (srfi s1 lists))
  (export
    make-u8vector u8vector u8vector? u8vector-length u8vector-ref
    u8vector-set! u8vector->list list->u8vector u8vector-copy
    u8vector-append u8vector-copy! u8vector-fill! u8vector-empty? u8vector=
    u8vector-swap! u8vector-reverse! u8vector-reverse-copy u8vector-map
    u8vector-map! u8vector-for-each u8vector-count u8vector-index
    u8vector-index-right u8vector-skip u8vector-skip-right u8vector-any
    u8vector-every u8vector-filter u8vector-remove u8vector-partition
    u8vector-fold u8vector-fold-right u8vector-concatenate u8vector-unfold
    u8vector-unfold-right u8vector-comparator u8vector->generator
    make-u8vector-generator make-s8vector s8vector s8vector?
    s8vector-length s8vector-ref s8vector-set! s8vector->list
    list->s8vector s8vector-copy s8vector-append s8vector-copy!
    s8vector-fill! s8vector-empty? s8vector= s8vector-swap!
    s8vector-reverse! s8vector-reverse-copy s8vector-map s8vector-map!
    s8vector-for-each s8vector-count s8vector-index s8vector-index-right
    s8vector-skip s8vector-skip-right s8vector-any s8vector-every
    s8vector-filter s8vector-remove s8vector-partition s8vector-fold
    s8vector-fold-right s8vector-concatenate s8vector-unfold
    s8vector-unfold-right s8vector-comparator s8vector->generator
    make-s8vector-generator make-u16vector u16vector u16vector?
    u16vector-length u16vector-ref u16vector-set! u16vector->list
    list->u16vector u16vector-copy u16vector-append u16vector-copy!
    u16vector-fill! u16vector-empty? u16vector= u16vector-swap!
    u16vector-reverse! u16vector-reverse-copy u16vector-map u16vector-map!
    u16vector-for-each u16vector-count u16vector-index
    u16vector-index-right u16vector-skip u16vector-skip-right u16vector-any
    u16vector-every u16vector-filter u16vector-remove u16vector-partition
    u16vector-fold u16vector-fold-right u16vector-concatenate
    u16vector-unfold u16vector-unfold-right u16vector-comparator
    u16vector->generator make-u16vector-generator make-s16vector s16vector
    s16vector? s16vector-length s16vector-ref s16vector-set!
    s16vector->list list->s16vector s16vector-copy s16vector-append
    s16vector-copy! s16vector-fill! s16vector-empty? s16vector=
    s16vector-swap! s16vector-reverse! s16vector-reverse-copy s16vector-map
    s16vector-map! s16vector-for-each s16vector-count s16vector-index
    s16vector-index-right s16vector-skip s16vector-skip-right s16vector-any
    s16vector-every s16vector-filter s16vector-remove s16vector-partition
    s16vector-fold s16vector-fold-right s16vector-concatenate
    s16vector-unfold s16vector-unfold-right s16vector-comparator
    s16vector->generator make-s16vector-generator make-u32vector u32vector
    u32vector? u32vector-length u32vector-ref u32vector-set!
    u32vector->list list->u32vector u32vector-copy u32vector-append
    u32vector-copy! u32vector-fill! u32vector-empty? u32vector=
    u32vector-swap! u32vector-reverse! u32vector-reverse-copy u32vector-map
    u32vector-map! u32vector-for-each u32vector-count u32vector-index
    u32vector-index-right u32vector-skip u32vector-skip-right u32vector-any
    u32vector-every u32vector-filter u32vector-remove u32vector-partition
    u32vector-fold u32vector-fold-right u32vector-concatenate
    u32vector-unfold u32vector-unfold-right u32vector-comparator
    u32vector->generator make-u32vector-generator make-s32vector s32vector
    s32vector? s32vector-length s32vector-ref s32vector-set!
    s32vector->list list->s32vector s32vector-copy s32vector-append
    s32vector-copy! s32vector-fill! s32vector-empty? s32vector=
    s32vector-swap! s32vector-reverse! s32vector-reverse-copy s32vector-map
    s32vector-map! s32vector-for-each s32vector-count s32vector-index
    s32vector-index-right s32vector-skip s32vector-skip-right s32vector-any
    s32vector-every s32vector-filter s32vector-remove s32vector-partition
    s32vector-fold s32vector-fold-right s32vector-concatenate
    s32vector-unfold s32vector-unfold-right s32vector-comparator
    s32vector->generator make-s32vector-generator make-u64vector u64vector
    u64vector? u64vector-length u64vector-ref u64vector-set!
    u64vector->list list->u64vector u64vector-copy u64vector-append
    u64vector-copy! u64vector-fill! u64vector-empty? u64vector=
    u64vector-swap! u64vector-reverse! u64vector-reverse-copy u64vector-map
    u64vector-map! u64vector-for-each u64vector-count u64vector-index
    u64vector-index-right u64vector-skip u64vector-skip-right u64vector-any
    u64vector-every u64vector-filter u64vector-remove u64vector-partition
    u64vector-fold u64vector-fold-right u64vector-concatenate
    u64vector-unfold u64vector-unfold-right u64vector-comparator
    u64vector->generator make-u64vector-generator make-s64vector s64vector
    s64vector? s64vector-length s64vector-ref s64vector-set!
    s64vector->list list->s64vector s64vector-copy s64vector-append
    s64vector-copy! s64vector-fill! s64vector-empty? s64vector=
    s64vector-swap! s64vector-reverse! s64vector-reverse-copy s64vector-map
    s64vector-map! s64vector-for-each s64vector-count s64vector-index
    s64vector-index-right s64vector-skip s64vector-skip-right s64vector-any
    s64vector-every s64vector-filter s64vector-remove s64vector-partition
    s64vector-fold s64vector-fold-right s64vector-concatenate
    s64vector-unfold s64vector-unfold-right s64vector-comparator
    s64vector->generator make-s64vector-generator make-f64vector f64vector
    f64vector? f64vector-length f64vector-ref f64vector-set!
    f64vector->list list->f64vector f64vector-copy f64vector-append
    f64vector-fill! f64vector-empty? f64vector= f64vector-swap!
    f64vector-reverse! f64vector-reverse-copy f64vector-map f64vector-map!
    f64vector-for-each f64vector-count f64vector-index
    f64vector-index-right f64vector-skip f64vector-skip-right f64vector-any
    f64vector-every f64vector-filter f64vector-remove f64vector-partition
    f64vector-fold f64vector-fold-right f64vector-concatenate
    f64vector-unfold f64vector-unfold-right f64vector-comparator
    f64vector->generator make-f64vector-generator comparator? =? <? >? <=?
    >=?)
  (begin

    ;; ---- u8vector: extended ops (SRFI-160-style) ----

    (define (u8vector-empty? v) (= (u8vector-length v) 0))

    (define (u8vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (u8vector-length a) (u8vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (u8vector-length a))
                               (and (= (u8vector-ref a i) (u8vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (u8vector-swap! v i j)
      (let ((tmp (u8vector-ref v i)))
        (u8vector-set! v i (u8vector-ref v j))
        (u8vector-set! v j tmp)))

    (define (u8vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u8vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (u8vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (u8vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u8vector-length v)))
             (n (- end start))
             (out (make-u8vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u8vector-set! out i (u8vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (u8vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs)))
             (out (make-u8vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u8vector-set! out i (apply f (map (lambda (vv) (u8vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (u8vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (u8vector-set! v1 i (apply f (map (lambda (vv) (u8vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (u8vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (u8vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (u8vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (u8vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (u8vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (u8vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (u8vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (u8vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (u8vector-skip pred v1 . vs) (apply u8vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (u8vector-skip-right pred v1 . vs) (apply u8vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (u8vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (u8vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (u8vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (u8vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (u8vector-filter pred v)
      (list->u8vector (filter pred (u8vector->list v))))

    (define (u8vector-remove pred v)
      (list->u8vector (remove pred (u8vector->list v))))

    (define (u8vector-partition pred v)
      (let-values (((yes no) (partition pred (u8vector->list v))))
        (values (list->u8vector yes) (list->u8vector no))))

    (define (u8vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (u8vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from u8vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (u8vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u8vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (u8vector-ref vv i)) vecs) (list acc))))))))

    (define (u8vector-concatenate vs) (apply u8vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (u8vector-unfold f length . seeds)
      (let ((out (make-u8vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u8vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (u8vector-unfold-right f length . seeds)
      (let ((out (make-u8vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u8vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define u8vector-comparator
      (make-comparator
        u8vector?
        u8vector=
        (lambda (a b)
          (let ((la (u8vector-length a)) (lb (u8vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (u8vector-ref a i) (u8vector-ref b i)) (loop (+ i 1)))
                        (else (< (u8vector-ref a i) (u8vector-ref b i))))))))
        #f))

    (define (u8vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u8vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (u8vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-u8vector-generator v . range) (apply u8vector->generator v range))

    ;; ---- s8vector: extended ops (SRFI-160-style) ----

    (define (s8vector-empty? v) (= (s8vector-length v) 0))

    (define (s8vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (s8vector-length a) (s8vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (s8vector-length a))
                               (and (= (s8vector-ref a i) (s8vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (s8vector-swap! v i j)
      (let ((tmp (s8vector-ref v i)))
        (s8vector-set! v i (s8vector-ref v j))
        (s8vector-set! v j tmp)))

    (define (s8vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s8vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (s8vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (s8vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s8vector-length v)))
             (n (- end start))
             (out (make-s8vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s8vector-set! out i (s8vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (s8vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs)))
             (out (make-s8vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s8vector-set! out i (apply f (map (lambda (vv) (s8vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (s8vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (s8vector-set! v1 i (apply f (map (lambda (vv) (s8vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (s8vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (s8vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (s8vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (s8vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (s8vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (s8vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (s8vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (s8vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (s8vector-skip pred v1 . vs) (apply s8vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (s8vector-skip-right pred v1 . vs) (apply s8vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (s8vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (s8vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (s8vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (s8vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (s8vector-filter pred v)
      (list->s8vector (filter pred (s8vector->list v))))

    (define (s8vector-remove pred v)
      (list->s8vector (remove pred (s8vector->list v))))

    (define (s8vector-partition pred v)
      (let-values (((yes no) (partition pred (s8vector->list v))))
        (values (list->s8vector yes) (list->s8vector no))))

    (define (s8vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (s8vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from s8vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (s8vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s8vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (s8vector-ref vv i)) vecs) (list acc))))))))

    (define (s8vector-concatenate vs) (apply s8vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (s8vector-unfold f length . seeds)
      (let ((out (make-s8vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s8vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (s8vector-unfold-right f length . seeds)
      (let ((out (make-s8vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s8vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define s8vector-comparator
      (make-comparator
        s8vector?
        s8vector=
        (lambda (a b)
          (let ((la (s8vector-length a)) (lb (s8vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (s8vector-ref a i) (s8vector-ref b i)) (loop (+ i 1)))
                        (else (< (s8vector-ref a i) (s8vector-ref b i))))))))
        #f))

    (define (s8vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s8vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (s8vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-s8vector-generator v . range) (apply s8vector->generator v range))

    ;; ---- u16vector: extended ops (SRFI-160-style) ----

    (define (u16vector-empty? v) (= (u16vector-length v) 0))

    (define (u16vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (u16vector-length a) (u16vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (u16vector-length a))
                               (and (= (u16vector-ref a i) (u16vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (u16vector-swap! v i j)
      (let ((tmp (u16vector-ref v i)))
        (u16vector-set! v i (u16vector-ref v j))
        (u16vector-set! v j tmp)))

    (define (u16vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u16vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (u16vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (u16vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u16vector-length v)))
             (n (- end start))
             (out (make-u16vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u16vector-set! out i (u16vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (u16vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs)))
             (out (make-u16vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u16vector-set! out i (apply f (map (lambda (vv) (u16vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (u16vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (u16vector-set! v1 i (apply f (map (lambda (vv) (u16vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (u16vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (u16vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (u16vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (u16vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (u16vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (u16vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (u16vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (u16vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (u16vector-skip pred v1 . vs) (apply u16vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (u16vector-skip-right pred v1 . vs) (apply u16vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (u16vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (u16vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (u16vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (u16vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (u16vector-filter pred v)
      (list->u16vector (filter pred (u16vector->list v))))

    (define (u16vector-remove pred v)
      (list->u16vector (remove pred (u16vector->list v))))

    (define (u16vector-partition pred v)
      (let-values (((yes no) (partition pred (u16vector->list v))))
        (values (list->u16vector yes) (list->u16vector no))))

    (define (u16vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (u16vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from u16vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (u16vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u16vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (u16vector-ref vv i)) vecs) (list acc))))))))

    (define (u16vector-concatenate vs) (apply u16vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (u16vector-unfold f length . seeds)
      (let ((out (make-u16vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u16vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (u16vector-unfold-right f length . seeds)
      (let ((out (make-u16vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u16vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define u16vector-comparator
      (make-comparator
        u16vector?
        u16vector=
        (lambda (a b)
          (let ((la (u16vector-length a)) (lb (u16vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (u16vector-ref a i) (u16vector-ref b i)) (loop (+ i 1)))
                        (else (< (u16vector-ref a i) (u16vector-ref b i))))))))
        #f))

    (define (u16vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u16vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (u16vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-u16vector-generator v . range) (apply u16vector->generator v range))

    ;; ---- s16vector: extended ops (SRFI-160-style) ----

    (define (s16vector-empty? v) (= (s16vector-length v) 0))

    (define (s16vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (s16vector-length a) (s16vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (s16vector-length a))
                               (and (= (s16vector-ref a i) (s16vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (s16vector-swap! v i j)
      (let ((tmp (s16vector-ref v i)))
        (s16vector-set! v i (s16vector-ref v j))
        (s16vector-set! v j tmp)))

    (define (s16vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s16vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (s16vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (s16vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s16vector-length v)))
             (n (- end start))
             (out (make-s16vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s16vector-set! out i (s16vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (s16vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs)))
             (out (make-s16vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s16vector-set! out i (apply f (map (lambda (vv) (s16vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (s16vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (s16vector-set! v1 i (apply f (map (lambda (vv) (s16vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (s16vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (s16vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (s16vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (s16vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (s16vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (s16vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (s16vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (s16vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (s16vector-skip pred v1 . vs) (apply s16vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (s16vector-skip-right pred v1 . vs) (apply s16vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (s16vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (s16vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (s16vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (s16vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (s16vector-filter pred v)
      (list->s16vector (filter pred (s16vector->list v))))

    (define (s16vector-remove pred v)
      (list->s16vector (remove pred (s16vector->list v))))

    (define (s16vector-partition pred v)
      (let-values (((yes no) (partition pred (s16vector->list v))))
        (values (list->s16vector yes) (list->s16vector no))))

    (define (s16vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (s16vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from s16vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (s16vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s16vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (s16vector-ref vv i)) vecs) (list acc))))))))

    (define (s16vector-concatenate vs) (apply s16vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (s16vector-unfold f length . seeds)
      (let ((out (make-s16vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s16vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (s16vector-unfold-right f length . seeds)
      (let ((out (make-s16vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s16vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define s16vector-comparator
      (make-comparator
        s16vector?
        s16vector=
        (lambda (a b)
          (let ((la (s16vector-length a)) (lb (s16vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (s16vector-ref a i) (s16vector-ref b i)) (loop (+ i 1)))
                        (else (< (s16vector-ref a i) (s16vector-ref b i))))))))
        #f))

    (define (s16vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s16vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (s16vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-s16vector-generator v . range) (apply s16vector->generator v range))

    ;; ---- u32vector: extended ops (SRFI-160-style) ----

    (define (u32vector-empty? v) (= (u32vector-length v) 0))

    (define (u32vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (u32vector-length a) (u32vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (u32vector-length a))
                               (and (= (u32vector-ref a i) (u32vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (u32vector-swap! v i j)
      (let ((tmp (u32vector-ref v i)))
        (u32vector-set! v i (u32vector-ref v j))
        (u32vector-set! v j tmp)))

    (define (u32vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u32vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (u32vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (u32vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u32vector-length v)))
             (n (- end start))
             (out (make-u32vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u32vector-set! out i (u32vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (u32vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs)))
             (out (make-u32vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u32vector-set! out i (apply f (map (lambda (vv) (u32vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (u32vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (u32vector-set! v1 i (apply f (map (lambda (vv) (u32vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (u32vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (u32vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (u32vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (u32vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (u32vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (u32vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (u32vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (u32vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (u32vector-skip pred v1 . vs) (apply u32vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (u32vector-skip-right pred v1 . vs) (apply u32vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (u32vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (u32vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (u32vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (u32vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (u32vector-filter pred v)
      (list->u32vector (filter pred (u32vector->list v))))

    (define (u32vector-remove pred v)
      (list->u32vector (remove pred (u32vector->list v))))

    (define (u32vector-partition pred v)
      (let-values (((yes no) (partition pred (u32vector->list v))))
        (values (list->u32vector yes) (list->u32vector no))))

    (define (u32vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (u32vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from u32vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (u32vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u32vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (u32vector-ref vv i)) vecs) (list acc))))))))

    (define (u32vector-concatenate vs) (apply u32vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (u32vector-unfold f length . seeds)
      (let ((out (make-u32vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u32vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (u32vector-unfold-right f length . seeds)
      (let ((out (make-u32vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u32vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define u32vector-comparator
      (make-comparator
        u32vector?
        u32vector=
        (lambda (a b)
          (let ((la (u32vector-length a)) (lb (u32vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (u32vector-ref a i) (u32vector-ref b i)) (loop (+ i 1)))
                        (else (< (u32vector-ref a i) (u32vector-ref b i))))))))
        #f))

    (define (u32vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u32vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (u32vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-u32vector-generator v . range) (apply u32vector->generator v range))

    ;; ---- s32vector: extended ops (SRFI-160-style) ----

    (define (s32vector-empty? v) (= (s32vector-length v) 0))

    (define (s32vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (s32vector-length a) (s32vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (s32vector-length a))
                               (and (= (s32vector-ref a i) (s32vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (s32vector-swap! v i j)
      (let ((tmp (s32vector-ref v i)))
        (s32vector-set! v i (s32vector-ref v j))
        (s32vector-set! v j tmp)))

    (define (s32vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s32vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (s32vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (s32vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s32vector-length v)))
             (n (- end start))
             (out (make-s32vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s32vector-set! out i (s32vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (s32vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs)))
             (out (make-s32vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s32vector-set! out i (apply f (map (lambda (vv) (s32vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (s32vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (s32vector-set! v1 i (apply f (map (lambda (vv) (s32vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (s32vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (s32vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (s32vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (s32vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (s32vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (s32vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (s32vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (s32vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (s32vector-skip pred v1 . vs) (apply s32vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (s32vector-skip-right pred v1 . vs) (apply s32vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (s32vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (s32vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (s32vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (s32vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (s32vector-filter pred v)
      (list->s32vector (filter pred (s32vector->list v))))

    (define (s32vector-remove pred v)
      (list->s32vector (remove pred (s32vector->list v))))

    (define (s32vector-partition pred v)
      (let-values (((yes no) (partition pred (s32vector->list v))))
        (values (list->s32vector yes) (list->s32vector no))))

    (define (s32vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (s32vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from s32vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (s32vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s32vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (s32vector-ref vv i)) vecs) (list acc))))))))

    (define (s32vector-concatenate vs) (apply s32vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (s32vector-unfold f length . seeds)
      (let ((out (make-s32vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s32vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (s32vector-unfold-right f length . seeds)
      (let ((out (make-s32vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s32vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define s32vector-comparator
      (make-comparator
        s32vector?
        s32vector=
        (lambda (a b)
          (let ((la (s32vector-length a)) (lb (s32vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (s32vector-ref a i) (s32vector-ref b i)) (loop (+ i 1)))
                        (else (< (s32vector-ref a i) (s32vector-ref b i))))))))
        #f))

    (define (s32vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s32vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (s32vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-s32vector-generator v . range) (apply s32vector->generator v range))

    ;; ---- u64vector: extended ops (SRFI-160-style) ----

    (define (u64vector-empty? v) (= (u64vector-length v) 0))

    (define (u64vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (u64vector-length a) (u64vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (u64vector-length a))
                               (and (= (u64vector-ref a i) (u64vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (u64vector-swap! v i j)
      (let ((tmp (u64vector-ref v i)))
        (u64vector-set! v i (u64vector-ref v j))
        (u64vector-set! v j tmp)))

    (define (u64vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u64vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (u64vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (u64vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u64vector-length v)))
             (n (- end start))
             (out (make-u64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u64vector-set! out i (u64vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (u64vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs)))
             (out (make-u64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (u64vector-set! out i (apply f (map (lambda (vv) (u64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (u64vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (u64vector-set! v1 i (apply f (map (lambda (vv) (u64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (u64vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (u64vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (u64vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (u64vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (u64vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (u64vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (u64vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (u64vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (u64vector-skip pred v1 . vs) (apply u64vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (u64vector-skip-right pred v1 . vs) (apply u64vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (u64vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (u64vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (u64vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (u64vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (u64vector-filter pred v)
      (list->u64vector (filter pred (u64vector->list v))))

    (define (u64vector-remove pred v)
      (list->u64vector (remove pred (u64vector->list v))))

    (define (u64vector-partition pred v)
      (let-values (((yes no) (partition pred (u64vector->list v))))
        (values (list->u64vector yes) (list->u64vector no))))

    (define (u64vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (u64vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from u64vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (u64vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map u64vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (u64vector-ref vv i)) vecs) (list acc))))))))

    (define (u64vector-concatenate vs) (apply u64vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (u64vector-unfold f length . seeds)
      (let ((out (make-u64vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (u64vector-unfold-right f length . seeds)
      (let ((out (make-u64vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (u64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define u64vector-comparator
      (make-comparator
        u64vector?
        u64vector=
        (lambda (a b)
          (let ((la (u64vector-length a)) (lb (u64vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (u64vector-ref a i) (u64vector-ref b i)) (loop (+ i 1)))
                        (else (< (u64vector-ref a i) (u64vector-ref b i))))))))
        #f))

    (define (u64vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (u64vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (u64vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-u64vector-generator v . range) (apply u64vector->generator v range))

    ;; ---- s64vector: extended ops (SRFI-160-style) ----

    (define (s64vector-empty? v) (= (s64vector-length v) 0))

    (define (s64vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (s64vector-length a) (s64vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (s64vector-length a))
                               (and (= (s64vector-ref a i) (s64vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (s64vector-swap! v i j)
      (let ((tmp (s64vector-ref v i)))
        (s64vector-set! v i (s64vector-ref v j))
        (s64vector-set! v j tmp)))

    (define (s64vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s64vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (s64vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (s64vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s64vector-length v)))
             (n (- end start))
             (out (make-s64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s64vector-set! out i (s64vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (s64vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs)))
             (out (make-s64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (s64vector-set! out i (apply f (map (lambda (vv) (s64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (s64vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (s64vector-set! v1 i (apply f (map (lambda (vv) (s64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (s64vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (s64vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (s64vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (s64vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (s64vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (s64vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (s64vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (s64vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (s64vector-skip pred v1 . vs) (apply s64vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (s64vector-skip-right pred v1 . vs) (apply s64vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (s64vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (s64vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (s64vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (s64vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (s64vector-filter pred v)
      (list->s64vector (filter pred (s64vector->list v))))

    (define (s64vector-remove pred v)
      (list->s64vector (remove pred (s64vector->list v))))

    (define (s64vector-partition pred v)
      (let-values (((yes no) (partition pred (s64vector->list v))))
        (values (list->s64vector yes) (list->s64vector no))))

    (define (s64vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (s64vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from s64vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (s64vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map s64vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (s64vector-ref vv i)) vecs) (list acc))))))))

    (define (s64vector-concatenate vs) (apply s64vector-append vs))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (s64vector-unfold f length . seeds)
      (let ((out (make-s64vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (s64vector-unfold-right f length . seeds)
      (let ((out (make-s64vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (s64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define s64vector-comparator
      (make-comparator
        s64vector?
        s64vector=
        (lambda (a b)
          (let ((la (s64vector-length a)) (lb (s64vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (s64vector-ref a i) (s64vector-ref b i)) (loop (+ i 1)))
                        (else (< (s64vector-ref a i) (s64vector-ref b i))))))))
        #f))

    (define (s64vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (s64vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (s64vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-s64vector-generator v . range) (apply s64vector->generator v range))

    ;; ---- f64vector: extended ops (SRFI-160-style) ----

    (define (f64vector-empty? v) (= (f64vector-length v) 0))

    (define (f64vector= . vs)
      (or (null? vs) (null? (cdr vs))
          (let ((a (car vs)))
            (let loop ((rest (cdr vs)))
              (or (null? rest)
                  (let ((b (car rest)))
                    (and (= (f64vector-length a) (f64vector-length b))
                         (let eq-loop ((i 0))
                           (or (= i (f64vector-length a))
                               (and (= (f64vector-ref a i) (f64vector-ref b i))
                                    (eq-loop (+ i 1)))))
                         (loop (cdr rest)))))))))

    (define (f64vector-swap! v i j)
      (let ((tmp (f64vector-ref v i)))
        (f64vector-set! v i (f64vector-ref v j))
        (f64vector-set! v j tmp)))

    (define (f64vector-reverse! v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (f64vector-length v))))
        (let loop ((i start) (j (- end 1)))
          (when (< i j) (f64vector-swap! v i j) (loop (+ i 1) (- j 1))))))

    (define (f64vector-reverse-copy v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (f64vector-length v)))
             (n (- end start))
             (out (make-f64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (f64vector-set! out i (f64vector-ref v (- end 1 i)))
            (loop (+ i 1))))
        out))

    (define (f64vector-map f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs)))
             (out (make-f64vector n)))
        (let loop ((i 0))
          (when (< i n)
            (f64vector-set! out i (apply f (map (lambda (vv) (f64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        out))

    (define (f64vector-map! f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (f64vector-set! v1 i (apply f (map (lambda (vv) (f64vector-ref vv i)) vecs)))
            (loop (+ i 1))))
        v1))

    (define (f64vector-for-each f v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0))
          (when (< i n)
            (apply f (map (lambda (vv) (f64vector-ref vv i)) vecs))
            (loop (+ i 1))))))

    (define (f64vector-count pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0) (acc 0))
          (if (= i n) acc
              (loop (+ i 1) (if (apply pred (map (lambda (vv) (f64vector-ref vv i)) vecs)) (+ acc 1) acc))))))

    (define (f64vector-index pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0))
          (cond ((= i n) #f)
                ((apply pred (map (lambda (vv) (f64vector-ref vv i)) vecs)) i)
                (else (loop (+ i 1)))))))

    (define (f64vector-index-right pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i (- n 1)))
          (cond ((< i 0) #f)
                ((apply pred (map (lambda (vv) (f64vector-ref vv i)) vecs)) i)
                (else (loop (- i 1)))))))

    (define (f64vector-skip pred v1 . vs) (apply f64vector-index (lambda args (not (apply pred args))) v1 vs))
    (define (f64vector-skip-right pred v1 . vs) (apply f64vector-index-right (lambda args (not (apply pred args))) v1 vs))

    (define (f64vector-any pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0))
          (and (< i n)
               (or (apply pred (map (lambda (vv) (f64vector-ref vv i)) vecs))
                   (loop (+ i 1)))))))

    (define (f64vector-every pred v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0) (last #t))
          (if (= i n) last
              (let ((r (apply pred (map (lambda (vv) (f64vector-ref vv i)) vecs))))
                (and r (loop (+ i 1) r)))))))

    (define (f64vector-filter pred v)
      (list->f64vector (filter pred (f64vector->list v))))

    (define (f64vector-remove pred v)
      (list->f64vector (remove pred (f64vector->list v))))

    (define (f64vector-partition pred v)
      (let-values (((yes no) (partition pred (f64vector->list v))))
        (values (list->f64vector yes) (list->f64vector no))))

    (define (f64vector-fold kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i 0) (acc knil))
          (if (= i n) acc
              (loop (+ i 1) (apply kons acc (map (lambda (vv) (f64vector-ref vv i)) vecs)))))))

    ;; Note the argument order differs from f64vector-fold above: kons
    ;; here receives the current elements FIRST and the accumulator LAST
    ;; (e.g. (kons e1 e2 ... acc)), matching SRFI-133/SRFI-160's own
    ;; fold-right convention -- not the same order as fold's (kons acc
    ;; e1 e2 ...), which matters for a non-commutative kons like cons.
    (define (f64vector-fold-right kons knil v1 . vs)
      (let* ((vecs (cons v1 vs))
             (n (apply min (map f64vector-length vecs))))
        (let loop ((i (- n 1)) (acc knil))
          (if (< i 0) acc
              (loop (- i 1) (apply kons (append (map (lambda (vv) (f64vector-ref vv i)) vecs) (list acc))))))))

    (define (f64vector-concatenate vs)
      ;; (curry f64vector)'s f64vector-append is a fixed 2-arg procedure
      ;; (unlike the other 8 kinds' N-ary append), so concatenating N
      ;; vectors folds pairwise instead of apply-ing. The single-element
      ;; case is special-cased to copy rather than fold-left-ing over an
      ;; empty rest list, which would just return (car vs) unchanged --
      ;; aliasing the input instead of producing a fresh vector, unlike
      ;; every other kind's -concatenate (found by independent review).
      (cond ((null? vs) (f64vector))
            ((null? (cdr vs)) (f64vector-copy (car vs)))
            (else (fold-left f64vector-append (car vs) (cdr vs)))))

    ;; call-with-values's receiver used to be called with (loop ...) as
    ;; its own tail call -- curry's core VM doesn't fully TCO that shape
    ;; inside a define-library body (a separate, pre-existing core bug,
    ;; found here by independent security review: SIGSEGV via C stack
    ;; overflow past a few thousand elements). Worked around by routing
    ;; the multiple return values through `list` as call-with-values's
    ;; receiver (an ordinary, non-tail call) and doing the actual loop
    ;; recursion as a separate, genuinely tail call afterward.
    (define (f64vector-unfold f length . seeds)
      (let ((out (make-f64vector length))
            (ss seeds))
        (let loop ((i 0))
          (when (< i length)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (f64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (+ i 1)))))
        out))

    (define (f64vector-unfold-right f length . seeds)
      (let ((out (make-f64vector length))
            (ss seeds))
        (let loop ((i (- length 1)))
          (when (>= i 0)
            (let ((results (call-with-values (lambda () (apply f i ss)) list)))
              (f64vector-set! out i (car results))
              (set! ss (cdr results))
              (loop (- i 1)))))
        out))

    (define f64vector-comparator
      (make-comparator
        f64vector?
        f64vector=
        (lambda (a b)
          (let ((la (f64vector-length a)) (lb (f64vector-length b)))
            (if (not (= la lb)) (< la lb)
                (let loop ((i 0))
                  (cond ((= i la) #f)
                        ((= (f64vector-ref a i) (f64vector-ref b i)) (loop (+ i 1)))
                        (else (< (f64vector-ref a i) (f64vector-ref b i))))))))
        #f))

    (define (f64vector->generator v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (f64vector-length v)))
             (i start))
        (lambda ()
          (if (>= i end) (eof-object)
              (let ((x (f64vector-ref v i))) (set! i (+ i 1)) x)))))

    (define (make-f64vector-generator v . range) (apply f64vector->generator v range))
))
