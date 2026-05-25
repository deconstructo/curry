#!/usr/bin/env curry
;;; Rule 110 elementary cellular automaton
;;;
;;; Usage:
;;;   rule110.scm [options] [seed-string]
;;;
;;;   seed-string  Row of '#' (live) and ' ' (dead) cells.
;;;                Width is derived from the string length.
;;;                If omitted, a random row of --width cells is used.
;;;
;;; Examples:
;;;   curry rule110.scm                        # 80-wide random, 40 steps
;;;   curry rule110.scm -w 120 -n 60           # wider random run
;;;   curry rule110.scm "  #   # ##  #  "      # explicit seed

(import (curry getopt))

(define specs
  (list
    (option 'width  #\w "width"  #t "80"  "row width for random seed")
    (option 'steps  #\n "steps"  #t "40"  "number of generations to display")
    (option 'help   #\h "help"   #f #f    "show this help")))

(define result (getopt (cdr command-line-args) specs))

(when (opt-get result 'help)
  (display (opt-usage "rule110.scm" specs))
  (exit 0))

(unless (opt-ok? result)
  (for-each (lambda (e) (display e) (newline)) (opt-errors result))
  (display (opt-usage "rule110.scm" specs))
  (exit 1))

(define steps  (string->number (opt-get result 'steps)))
(define width  (string->number (opt-get result 'width)))
(define positional (opt-rest result))

;; ---- Rule 110 ---------------------------------------------------------------

(define rule110-table #(0 1 1 1 0 1 1 0))

(define (rule110 l c r)
  (vector-ref rule110-table (+ (* l 4) (* c 2) r)))

(define (next-gen row)
  (let* ((w   (vector-length row))
         (out (make-vector w 0)))
    (do ((i 0 (+ i 1))) ((= i w) out)
      (vector-set! out i
        (rule110 (if (= i 0)       0 (vector-ref row (- i 1)))
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

;; Simple LCG seeded from current time
(define lcg-state
  (inexact->exact (floor (current-second))))

(define (lcg-next!)
  (set! lcg-state (remainder (+ (* lcg-state 1664525) 1013904223) 4294967296))
  lcg-state)

(define (random-bit)
  (remainder (lcg-next!) 2))

(define (make-random-row w)
  (let ((v (make-vector w 0)))
    (do ((i 0 (+ i 1))) ((= i w) v)
      (vector-set! v i (random-bit)))))

(define initial-row
  (if (pair? positional)
      (parse-seed (car positional))
      (make-random-row width)))

;; ---- Run --------------------------------------------------------------------

(let loop ((row initial-row) (s 0))
  (display-row row)
  (when (< s steps)
    (loop (next-gen row) (+ s 1))))
