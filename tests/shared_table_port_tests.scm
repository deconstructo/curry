;;; Hash tables, sets and string ports shared by several threads (#452, #453).
;;;
;;; map runs in parallel above 8 elements, so a lambda that writes one table or
;;; one port runs on several threads without the caller asking. A rehash
;;; replaced the arrays under a probing thread and the process never finished;
;;; a string port grew its capacity before its buffer, and another thread wrote
;;; past the old buffer. Results go through globals: a set! on a captured local
;;; is not seen by the spawner (#448).

(import (scheme base) (scheme write) (curry sync))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (range n) (let loop ((i (- n 1)) (a '())) (if (< i 0) a (loop (- i 1) (cons i a)))))
(define (count-missing h n)
  (let loop ((i 0) (bad 0))
    (if (= i n) bad
        (loop (+ i 1) (if (equal? (hash-table-ref h i (lambda () #f)) (* i i)) bad (+ bad 1))))))

;;; ---- a hash table written from a parallel map ----
(define h1 (make-hash-table))
(map (lambda (i) (hash-table-set! h1 i (* i i))) (range 20000))
(check "20000 parallel writes: size" (hash-table-size h1) 20000)
(check "20000 parallel writes: every value" (count-missing h1 20000) 0)

(define h2 (make-hash-table))
(for-each/par (lambda (i) (hash-table-set! h2 i (* i i))) (range 5000))
(check "for-each/par writes" (count-missing h2 5000) 0)

;;; The same keys written many times at once: one entry each.
(define h3 (make-hash-table))
(map (lambda (i) (hash-table-set! h3 (modulo i 50) (* (modulo i 50) (modulo i 50)))) (range 20000))
(check "repeated keys, size" (hash-table-size h3) 50)
(check "repeated keys, values" (count-missing h3 50) 0)

;;; Writes, deletes and reads together.
(define h4 (make-hash-table))
(map (lambda (i) (hash-table-set! h4 i i)) (range 4000))
(map (lambda (i)
       (cond ((even? i) (hash-table-delete! h4 i))
             (else (hash-table-set! h4 (+ i 100000) i))
             ))
     (range 4000))
(check "delete and add together: size" (hash-table-size h4) 4000)
(check "deleted keys are gone" (hash-table-exists? h4 10) #f)
(check "kept keys are there" (hash-table-ref h4 11 (lambda () #f)) 11)
(check "added keys are there" (hash-table-ref h4 100011 (lambda () #f)) 11)

;;; Listing a table while it is written: each listing is one consistent snapshot
;;; (no key twice, every pair is key . key), though two listings differ.
(define h5 (make-hash-table))
(define listing-ok #t)
(define (distinct? lst) (= (length lst) (set-size (list->set lst))))
(map (lambda (i)
       (hash-table-set! h5 i i)
       (if (= 0 (modulo i 200))
           (let ((ks (hash-table-keys h5)) (al (hash-table->alist h5)))
             (if (not (and (list? ks) (distinct? ks)
                           (list? al) (distinct? (map car al))
                           (not (memv #f (map (lambda (p) (= (car p) (cdr p))) al)))))
                 (set! listing-ok #f)))))
     (range 4000))
(check "listings taken during writes are consistent" listing-ok #t)
(check "and the table is whole afterwards" (hash-table-size h5) 4000)

;;; Equal-hash tables with structured keys.
(define h6 (make-hash-table))
(map (lambda (i) (hash-table-set! h6 (list i (number->string i)) i)) (range 3000))
(check "structured keys" (hash-table-ref h6 (list 2999 "2999") (lambda () #f)) 2999)
(check "structured keys: size" (hash-table-size h6) 3000)

;;; ---- actors writing one table ----
(define h7 (make-hash-table))
(define actors-done (make-semaphore 0))
(do ((a 0 (+ a 1))) ((= a 6))
  (let ((base (* a 1000)))
    (spawn (lambda ()
             (do ((i 0 (+ i 1))) ((= i 1000)) (hash-table-set! h7 (+ base i) i))
             (sem-post! actors-done)))))
(do ((a 0 (+ a 1))) ((= a 6)) (sem-wait! actors-done))
(check "six actors, one table" (hash-table-size h7) 6000)

;;; ---- sets ----
(define s1 (make-set))
(map (lambda (i) (set-add! s1 i)) (range 20000))
(check "set written in parallel: size" (set-size s1) 20000)
(check "set: members" (let loop ((i 0)) (cond ((= i 20000) #t) ((set-member? s1 i) (loop (+ i 1))) (else i))) #t)
(map (lambda (i) (if (even? i) (set-delete! s1 i))) (range 20000))
(check "set after parallel deletes" (set-size s1) 10000)
(check "set->list length" (length (set->list s1)) 10000)

;;; ---- a string port written from a parallel map ----
(define sp1 (open-output-string))
(map (lambda (i) (write-string "0123456789" sp1)) (range 4000))
(check "string port, write-string" (string-length (get-output-string sp1)) 40000)

(define sp2 (open-output-string))
(map (lambda (i) (write-char #\x sp2)) (range 20000))
(check "string port, write-char" (string-length (get-output-string sp2)) 20000)

(define sp3 (open-output-string))
(map (lambda (i) (write-char #\λ sp3)) (range 5000))
(check "string port, a multi-byte character is never split"
       (let ((s (get-output-string sp3)))
         (and (= (string-length s) 5000)
              (not (memv #f (map (lambda (c) (char=? c #\λ)) (string->list s))))))
       #t)

(define bp (open-output-bytevector))
(map (lambda (i) (write-u8 65 bp)) (range 20000))
(check "binary port, write-u8" (bytevector-length (get-output-bytevector bp)) 20000)

;;; Reading the port while it grows.
(define sp4 (open-output-string))
(define lens-ok #t)
(map (lambda (i)
       (write-string "abcd" sp4)
       (if (= 0 (modulo i 100))
           (let ((s (get-output-string sp4)))
             (if (not (= 0 (modulo (string-length s) 4))) (set! lens-ok #f)))))
     (range 4000))
(check "get-output-string during writes always sees whole writes" lens-ok #t)
(check "and the total is right" (string-length (get-output-string sp4)) 16000)

;;; Actors writing one port.
(define sp5 (open-output-string))
(define port-done (make-semaphore 0))
(do ((a 0 (+ a 1))) ((= a 6))
  (spawn (lambda ()
           (do ((i 0 (+ i 1))) ((= i 1000)) (write-string "ab" sp5))
           (sem-post! port-done))))
(do ((a 0 (+ a 1))) ((= a 6)) (sem-wait! port-done))
(check "six actors, one string port" (string-length (get-output-string sp5)) 12000)

;;; ---- a key too deep to hash: where it raises (the stack limit decides), the
;;; table must stay usable, not be left locked ----
(define deep (let loop ((i 0) (x '())) (if (= i 200000) x (loop (+ i 1) (list x)))))
(define hd (make-hash-table))
(hash-table-set! hd 1 'one)
(check "a very deeply nested key raises or is stored"
       (and (memq (guard (e (#t 'raised)) (hash-table-set! hd deep 'deep) 'stored) '(raised stored)) #t)
       #t)
(check "the table is still usable after the raise"
       (begin (hash-table-set! hd 2 'two) (list (hash-table-ref hd 1 (lambda () #f)) (hash-table-ref hd 2 (lambda () #f))))
       '(one two))
(check "also from another thread"
       (let ((r #f) (d (make-semaphore 0)))
         (spawn (lambda () (hash-table-set! hd 3 'three) (sem-post! d)))
         (sem-wait! d)
         (hash-table-ref hd 3 (lambda () #f)))
       'three)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
