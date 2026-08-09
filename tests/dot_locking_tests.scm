;;; Dot-locking module tests — (curry dot-locking)

(import (curry dot-locking) (curry posix) (curry sync) (srfi 18))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

(define (fresh-path n) (string-append "/tmp/curry-dot-locking-test-" (number->string n)))

;;; Basic obtain / release

(let ((f (fresh-path 1)))
  (check "obtain-dot-lock succeeds when unlocked" (obtain-dot-lock f) #t)
  (check "obtain-dot-lock fails when already locked (bounded retries)" (obtain-dot-lock f 0.05 2) #f)
  (check "release-dot-lock succeeds when locked" (release-dot-lock f) #t)
  (check "release-dot-lock fails when not locked" (release-dot-lock f) #f)
  (check "obtain-dot-lock succeeds again after release" (obtain-dot-lock f) #t)
  (release-dot-lock f))

;;; The lock file itself actually exists / is removed

(let ((f (fresh-path 2)))
  (obtain-dot-lock f)
  (check "lock file exists on disk while held" (file-info? (guard (e (#t #f)) (file-info (string-append f ".lock")))) #t)
  (release-dot-lock f)
  (check "lock file is gone after release" (guard (e (#t #f)) (file-info (string-append f ".lock")) #t) #f))

;;; No leftover temp files after a contested obtain

(define (filter pred lst)
  (cond ((null? lst) '()) ((pred (car lst)) (cons (car lst) (filter pred (cdr lst)))) (else (filter pred (cdr lst)))))
(define (%contains-lock-temp-suffix? name)
  (let loop ((i 0))
    (and (< (+ i 6) (string-length name))
         (or (string=? (substring name i (+ i 6)) ".lock.") (loop (+ i 1))))))

(let ((f (fresh-path 3)))
  (obtain-dot-lock f)
  (obtain-dot-lock f 0.05 2) ; fails, but shouldn't leave its own temp file behind
  (check "no leftover .lock.<host>.<pid>.* temp files after a failed obtain"
    (filter (lambda (name) (and (>= (string-length name) 5) (string=? (substring name 0 5) "curry")
                                 (%contains-lock-temp-suffix? name)))
            (directory-files "/tmp"))
    '())
  (release-dot-lock f))

;;; break-dot-lock

(let ((f (fresh-path 4)))
  (check "break-dot-lock returns #f when nothing to break" (break-dot-lock f) #f)
  (obtain-dot-lock f)
  (check "break-dot-lock returns #t and removes an existing lock" (break-dot-lock f) #t)
  (check "obtain-dot-lock succeeds after break-dot-lock" (obtain-dot-lock f) #t)
  (release-dot-lock f))

;;; Staleness

(let ((f (fresh-path 5)))
  (obtain-dot-lock f)
  (set-file-times (string-append f ".lock") (- (current-second) 1000) (- (current-second) 1000))
  (check "obtain-dot-lock returns 'broken after breaking a stale lock"
    (obtain-dot-lock f 0.05 3 500)
    'broken)
  (release-dot-lock f))

(let ((f (fresh-path 6)))
  (obtain-dot-lock f)
  (set-file-times (string-append f ".lock") (- (current-second) 10) (- (current-second) 10))
  (check "obtain-dot-lock does not break a lock younger than stale-time"
    (obtain-dot-lock f 0.05 2 500)
    #f)
  (release-dot-lock f))

(let ((f (fresh-path 7)))
  (obtain-dot-lock f)
  (set-file-times (string-append f ".lock") (- (current-second) 1000) (- (current-second) 1000))
  (check "obtain-dot-lock never considers a lock stale when stale-time is #f"
    (obtain-dot-lock f 0.05 2 #f)
    #f)
  (release-dot-lock f))

;;; Concurrency: see dot_locking_concurrency_tests.scm (a separate test
;;; file/ctest entry, deliberately not embedded here -- spawning many
;;; actors reliably reproduces its regression in a fresh process, but
;;; hung when run as this file's own 15th test in sequence, in a
;;; process that had already done substantial prior work; kept isolated
;;; rather than risk destabilizing this suite before that's understood).

;;; with-dot-lock* / with-dot-lock

(let ((f (fresh-path 8)))
  (check "with-dot-lock* returns the thunk's value" (with-dot-lock* f (lambda () (+ 1 2))) 3)
  (check "with-dot-lock* released the lock afterward" (obtain-dot-lock f) #t)
  (release-dot-lock f))

(let ((f (fresh-path 9)))
  (check "with-dot-lock (macro) returns the body's value" (with-dot-lock f (+ 1 2)) 3)
  (check "with-dot-lock released the lock afterward" (obtain-dot-lock f) #t)
  (release-dot-lock f))

(let ((f (fresh-path 10)))
  (check-error "with-dot-lock* propagates a non-local exit (an error) from thunk"
    (lambda () (with-dot-lock* f (lambda () (error "boom")))))
  (check "with-dot-lock* still released the lock after thunk raised" (obtain-dot-lock f) #t)
  (release-dot-lock f))

;; Note: there's no test for "with-dot-lock*/with-dot-lock raises when
;; the lock can't be obtained" -- their documented 2-arg signature
;; (file-name thunk) has no way to pass a bounded retry-number through
;; to the underlying obtain-dot-lock call, so with default settings
;; (retry-number #f) they retry forever rather than ever returning #f;
;; the (unless (obtain-dot-lock file-name) (error ...)) branch in
;; with-dot-lock*'s own implementation is consequently unreachable
;; under current defaults. Exercising it would mean hanging this test
;; suite forever, not a fast, deterministic check.

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(when (> fail 0) (exit 1))
