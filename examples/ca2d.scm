#!/usr/bin/env curry
;;; Wolfram elementary cellular automaton — all 256 rules
;;;
;;; Usage:
;;;   ca.scm [options] [seed-string]
;;;
;;;   seed-string  Row of '#' (live) and ' ' (dead) cells.
;;;                Width is derived from the string length.
;;;                If omitted, a random row of --width cells is used.
;;;
;;; Examples:
;;;   curry ca.scm                          # rule 110, 80-wide random, 40 steps
;;;   curry ca.scm -r 30                    # rule 30 (chaos)
;;;   curry ca.scm -r 90                    # rule 90 (Sierpiński triangle)
;;;   curry ca.scm -r 184                   # rule 184 (traffic flow)
;;;   curry ca.scm -r 30 -w 120 -n 60       # wider run
;;;   curry ca.scm -r 110 "  #   # ##  #  " # explicit seed

(import (curry getopt))

(define specs
  (list
    (option 'rule   #\r "rule"   #t "110" "Wolfram rule number (0–255)")
    (option 'width  #\w "width"  #t "80"  "row width for random seed")
    (option 'steps  #\n "steps"  #t "40"  "number of generations to display")
    (option 'single #\s "single" #f #f    "start from a single live centre cell")
    (option 'help   #\h "help"   #f #f    "show this help")))

(define result (getopt (cdr command-line-args) specs))

(when (opt-get result 'help)
  (display (opt-usage "ca.scm" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e) (newline)) (opt-errors result))
  (display (opt-usage "ca.scm" specs))
  (exit 1))

(define rule-n  (string->number (opt-get result 'rule)))
(define steps   (string->number (opt-get result 'steps)))
(define width   (string->number (opt-get result 'width)))
(define single? (opt-get result 'single))
(define positional (opt-rest result))

(unless (and rule-n (exact? rule-n) (<= 0 rule-n 255))
  (display "error: rule must be an integer 0–255\n")
  (exit 1))

;; ---- Automaton --------------------------------------------------------------

(define (make-rule n)
  (lambda (l c r)
    (let ((bit (+ (* l 4) (* c 2) r)))
      (if (zero? (bitwise-and n (arithmetic-shift 1 bit))) 0 1))))

(define apply-rule (make-rule rule-n))

(define (next-gen row)
  (let* ((w   (vector-length row))
         (out (make-vector w 0)))
    (do ((i 0 (+ i 1))) ((= i w) out)
      (vector-set! out i
        (apply-rule
          (if (= i 0)       0 (vector-ref row (- i 1)))
          (vector-ref row i)
          (if (= i (- w 1)) 0 (vector-ref row (+ i 1))))))))

(define (display-row row)
  (let ((w (vector-length row)))
    (do ((i 0 (+ i 1))) ((= i w))
      (display (if (= (vector-ref row i) 1) "#" " ")))
    (newline)))

;; ---- Initial row ------------------------------------------------------------

(define (parse-seed str)
  (let* ((chars (string->list str))
         (v     (make-vector (length chars) 0)))
    (let loop ((i 0) (cs chars))
      (unless (null? cs)
        (when (char=? (car cs) #\#)
          (vector-set! v i 1))
        (loop (+ i 1) (cdr cs))))
    v))

(define (make-single-row w)
  (let ((v (make-vector w 0)))
    (vector-set! v (quotient w 2) 1)
    v))

;; Simple LCG seeded from current time
(define lcg-state
  (inexact->exact (floor (current-second))))

(define (lcg-next!)
  (set! lcg-state (remainder (+ (* lcg-state 1664525) 1013904223) 4294967296))
  lcg-state)

(define (make-random-row w)
  (let ((v (make-vector w 0)))
    (do ((i 0 (+ i 1))) ((= i w) v)
      (vector-set! v i (remainder (lcg-next!) 2)))))

(define initial-row
  (cond
    ((pair? positional) (parse-seed (car positional)))
    (single?            (make-single-row width))
    (else               (make-random-row width))))

;; ---- Run --------------------------------------------------------------------

(let loop ((row initial-row) (s 0))
  (display-row row)
  (when (< s steps)
    (loop (next-gen row) (+ s 1))))
