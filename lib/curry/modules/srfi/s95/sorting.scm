;;; SRFI-95: Sorting and Merging.
;;;
;;; Predates and overlaps with SRFI-132 (already implemented as
;;; (srfi s132 sorting)): same merge-sort machinery, but 95's own
;;; argument order is (sequence less? [key]) rather than 132's
;;; (less? sequence), and 95 additionally accepts an optional per-element
;;; `key` extractor applied before comparison. Rather than a second sort
;;; implementation, this is a thin argument-reordering/key-wrapping shim
;;; over SRFI-132's real logic.
(define-library (srfi s95 sorting)
  (import (scheme base) (srfi s132 sorting))
  (export sorted? merge merge! sort sort!)
  (begin

    (define (%keyed less? key) (if key (lambda (a b) (less? (key a) (key b))) less?))
    (define (%key rest) (if (pair? rest) (car rest) #f))

    (define (sorted? sequence less? . rest)
      (let ((lt (%keyed less? (%key rest))))
        (cond
          ((list? sequence) (list-sorted? lt sequence))
          ((vector? sequence) (vector-sorted? lt sequence))
          (else (error "sorted?: not a list or vector" sequence)))))

    (define (merge list1 list2 less? . rest)
      (list-merge (%keyed less? (%key rest)) list1 list2))

    (define (merge! list1 list2 less? . rest)
      (list-merge! (%keyed less? (%key rest)) list1 list2))

    (define (sort sequence less? . rest)
      (let ((lt (%keyed less? (%key rest))))
        (cond
          ((list? sequence) (list-sort lt sequence))
          ((vector? sequence) (vector-sort lt sequence))
          (else (error "sort: not a list or vector" sequence)))))

    (define (sort! sequence less? . rest)
      (let ((lt (%keyed less? (%key rest))))
        (cond
          ((list? sequence) (list-sort! lt sequence))
          ((vector? sequence) (vector-sort! lt sequence))
          (else (error "sort!: not a list or vector" sequence)))))))
