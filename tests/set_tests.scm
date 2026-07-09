;;; set_tests.scm — tests for the extended set API

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define (set->sorted-list s)
  (list-sort < (set->list s)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Existing ops (regression)
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (make-set)))
  (set-add! s 1) (set-add! s 2) (set-add! s 3)
  (check "set-member? present"  (set-member? s 2) #t)
  (check "set-member? absent"   (set-member? s 9) #f)
  (check "set-size"             (set-size s) 3)
  (check "set-subset?"
    (set-subset? (list->set '(1 2)) s) #t)
  (check "set-subset? false"
    (set-subset? (list->set '(1 5)) s) #f))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set=? and set-empty?
;;; ══════════════════════════════════════════════════════════════════════════

(check "set=? equal"     (set=? (list->set '(1 2 3)) (list->set '(3 1 2))) #t)
(check "set=? not equal" (set=? (list->set '(1 2 3)) (list->set '(1 2)))   #f)
(check "set=? empty"     (set=? (make-set) (make-set)) #t)

(check "set-empty? empty"     (set-empty? (make-set)) #t)
(check "set-empty? non-empty" (set-empty? (list->set '(1))) #f)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-copy
;;; ══════════════════════════════════════════════════════════════════════════

(let* ((s  (list->set '(1 2 3)))
       (s2 (set-copy s)))
  (check "set-copy: equal contents" (set=? s s2) #t)
  (set-add! s2 99)
  (check "set-copy: mutation independence" (set-member? s 99) #f))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-adjoin / set-adjoin!
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (list->set '(1 2))))
  (let ((s2 (set-adjoin s 3 4 5)))
    (check "set-adjoin: new set has all"   (set=? s2 (list->set '(1 2 3 4 5))) #t)
    (check "set-adjoin: original unchanged" (set=? s (list->set '(1 2))) #t)))

(let ((s (list->set '(1 2))))
  (set-adjoin! s 3 4)
  (check "set-adjoin!: mutates in place" (set=? s (list->set '(1 2 3 4))) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-delete (non-destructive)
;;; ══════════════════════════════════════════════════════════════════════════

(let* ((s  (list->set '(1 2 3)))
       (s2 (set-delete s 2)))
  (check "set-delete: removed"    (set=? s2 (list->set '(1 3))) #t)
  (check "set-delete: original unchanged" (set-member? s 2) #t))

(let* ((s  (list->set '(1 2 3)))
       (s2 (set-delete s 99)))       ; delete absent element
  (check "set-delete: absent is no-op" (set=? s2 s) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-symmetric-difference
;;; ══════════════════════════════════════════════════════════════════════════

(check "set-sym-diff basic"
  (set=? (set-symmetric-difference (list->set '(1 2 3)) (list->set '(2 3 4)))
         (list->set '(1 4)))
  #t)
(check "set-sym-diff disjoint"
  (set=? (set-symmetric-difference (list->set '(1 2)) (list->set '(3 4)))
         (list->set '(1 2 3 4)))
  #t)
(check "set-sym-diff equal"
  (set-empty? (set-symmetric-difference (list->set '(1 2)) (list->set '(1 2))))
  #t)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-for-each
;;; ══════════════════════════════════════════════════════════════════════════

(let ((acc '()))
  (set-for-each (lambda (x) (set! acc (cons x acc))) (list->set '(1 2 3)))
  (check "set-for-each: visits all" (set=? (list->set acc) (list->set '(1 2 3))) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-map
;;; ══════════════════════════════════════════════════════════════════════════

(check "set-map: square"
  (set=? (set-map (lambda (x) (* x x)) (list->set '(1 2 3)))
         (list->set '(1 4 9)))
  #t)

(check "set-map: collapse (1 -1 → same after abs)"
  (set-size (set-map abs (list->set '(1 -1 2 -2))))
  2)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-filter / set-filter!
;;; ══════════════════════════════════════════════════════════════════════════

(let ((s (list->set '(1 2 3 4 5 6))))
  (check "set-filter: evens"
    (set=? (set-filter even? s) (list->set '(2 4 6))) #t)
  (check "set-filter: original unchanged"
    (set-size s) 6))

(let ((s (list->set '(1 2 3 4 5 6))))
  (set-filter! odd? s)
  (check "set-filter!: odds remain" (set=? s (list->set '(1 3 5))) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-fold
;;; ══════════════════════════════════════════════════════════════════════════

(check "set-fold: sum"
  (set-fold + 0 (list->set '(1 2 3 4 5)))
  15)

(check "set-fold: string accumulation"
  (set-size
    (list->set
      (let ((acc '()))
        (set-fold (lambda (a e) (set! acc (cons e acc)) a) #f (list->set '(a b c)))
        acc)))
  3)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-any? / set-every?
;;; ══════════════════════════════════════════════════════════════════════════

(check "set-any? true"  (set-any? even? (list->set '(1 2 3))) #t)
(check "set-any? false" (set-any? even? (list->set '(1 3 5))) #f)
(check "set-any? empty" (set-any? even? (make-set))           #f)

(check "set-every? true"  (set-every? odd?  (list->set '(1 3 5))) #t)
(check "set-every? false" (set-every? even? (list->set '(1 2 3))) #f)
(check "set-every? empty" (set-every? even? (make-set))           #t)

;;; Short-circuit: set-any? with a side-effectful pred
(let ((calls 0))
  (set-any? (lambda (x) (set! calls (+ calls 1)) (= x 1)) (list->set '(1 2 3 4 5)))
  (check "set-any? short-circuits" (< calls 5) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-count
;;; ══════════════════════════════════════════════════════════════════════════

(check "set-count: evens"  (set-count even? (list->set '(1 2 3 4 5 6))) 3)
(check "set-count: none"   (set-count even? (list->set '(1 3 5)))       0)
(check "set-count: all"    (set-count odd?  (list->set '(1 3 5)))       3)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-find
;;; ══════════════════════════════════════════════════════════════════════════

(let ((result (set-find even? (list->set '(1 2 3 4)))))
  (check "set-find: found is even" (even? result) #t))

(check "set-find: not found → #f"      (set-find even? (list->set '(1 3 5)))     #f)
(check "set-find: not found → default" (set-find even? (list->set '(1 3 5)) 'missing) 'missing)

;;; ══════════════════════════════════════════════════════════════════════════
;;; set-union / set-intersection / set-difference (regression)
;;; ══════════════════════════════════════════════════════════════════════════

(let ((a (list->set '(1 2 3))) (b (list->set '(2 3 4))))
  (check "set-union"        (set=? (set-union a b)        (list->set '(1 2 3 4))) #t)
  (check "set-intersection" (set=? (set-intersection a b) (list->set '(2 3)))     #t)
  (check "set-difference"   (set=? (set-difference a b)   (list->set '(1)))       #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Summary
;;; ══════════════════════════════════════════════════════════════════════════

(newline)
(display "Set tests: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(when (> fail 0) (error "test failures" fail))
