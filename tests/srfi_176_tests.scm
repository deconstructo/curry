;;; srfi_176_tests.scm — (srfi 176) Version flag: version-alist and the
;;; underlying curry-version builtin. The `-V` CLI flag itself (also
;;; part of this SRFI) is exercised separately by tests/test_cli.sh,
;;; since it's a process-exit-code/stdout-format check, not something
;;; meaningful to assert on from within a running Scheme program.

(import (scheme base) (srfi 176))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define (prop alist key) (assq key alist))
(define (prop-value alist key) (cadr (assq key alist)))

;;; R7RS has no `every` in (scheme base) -- tiny local version.
(define (every pred lst)
  (or (null? lst) (and (pred (car lst)) (every pred (cdr lst)))))

(define va (version-alist))

;;; ---- shape ----

(check "version-alist returns a list" (list? va) #t)
(check "version-alist returns a non-empty list" (> (length va) 0) #t)
(check "every entry is itself a non-empty list (key . values)"
       (every (lambda (e) (and (pair? e) (symbol? (car e)))) va) #t)

;;; ---- required-by-this-implementation properties ----

(check "command property present" (prop va 'command) '(command "curry"))
(check "command value is a string" (string? (prop-value va 'command)) #t)

(check "website property is a string" (string? (prop-value va 'website)) #t)

(check "version property matches curry-version" (prop-value va 'version) (curry-version))
(check "version is a string" (string? (prop-value va 'version)) #t)

(check "languages includes scheme" (and (memq 'scheme (cdr (prop va 'languages))) #t) #t)
(check "languages includes r7rs" (and (memq 'r7rs (cdr (prop va 'languages))) #t) #t)

(check "scheme.id is curry" (prop-value va 'scheme.id) 'curry)

;;; ---- scheme.srfi ----

(define srfi-list (cdr (prop va 'scheme.srfi)))
(check "scheme.srfi is a list of exact integers" (every (lambda (n) (and (integer? n) (exact? n))) srfi-list) #t)
(check "scheme.srfi is sorted ascending"
       (let loop ((lst srfi-list))
         (or (null? lst) (null? (cdr lst))
             (and (< (car lst) (cadr lst)) (loop (cdr lst)))))
       #t)
(check "scheme.srfi has no duplicates"
       (let loop ((lst srfi-list))
         (or (null? lst)
             (and (not (memv (car lst) (cdr lst))) (loop (cdr lst)))))
       #t)
(check "scheme.srfi includes 176 itself" (and (memv 176 srfi-list) #t) #t)
(check "scheme.srfi includes SRFI-1 (a real, long-shipped library)" (and (memv 1 srfi-list) #t) #t)
(check "scheme.srfi includes SRFI-0 (core cond-expand, no .sld shim)" (and (memv 0 srfi-list) #t) #t)
(check "scheme.srfi includes SRFI-61 (core cond, no .sld shim)" (and (memv 61 srfi-list) #t) #t)

;;; ---- scheme.features ----

(check "scheme.features matches (features) exactly"
       (cdr (prop va 'scheme.features)) (features))
(check "scheme.features includes r7rs" (and (memq 'r7rs (cdr (prop va 'scheme.features))) #t) #t)

;;; ---- build properties ----

(check "build.date is an RFC 3339 UTC timestamp"
       (let ((d (prop-value va 'build.date)))
         (and (= (string-length d) 20)
              (char=? (string-ref d 10) #\T)
              (char=? (string-ref d 19) #\Z)))
       #t)
(check "build.platform is a non-empty string"
       (> (string-length (prop-value va 'build.platform)) 0) #t)
(check "build.configure is a list of strings"
       (let ((l (cdr (prop va 'build.configure))))
         (and (pair? l) (every string? l)))
       #t)
(check "build.configure records the build type"
       (let loop ((l (cdr (prop va 'build.configure))))
         (cond ((null? l) #f)
               ((and (>= (string-length (car l)) 18)
                     (string=? (substring (car l) 0 18) "-DCMAKE_BUILD_TYPE")) #t)
               (else (loop (cdr l)))))
       #t)
;; Git properties are present only when built from a git checkout, so only
;; their shape is checked when they exist.
(check "build.git.commit, when present, is 40 hex digits"
       (let ((p (prop va 'build.git.commit)))
         (or (not p)
             (and (= (string-length (cadr p)) 40)
                  (every (lambda (c) (or (char-numeric? c)
                                         (memv c '(#\a #\b #\c #\d #\e #\f))))
                         (string->list (cadr p))))))
       #t)
(check "build.git.modified, when present, is a list of strings"
       (let ((p (prop va 'build.git.modified)))
         (or (not p) (every string? (cdr p))))
       #t)

;;; ---- scheme.path ----

(check "scheme.path, when present, is a list of strings"
       (let ((p (prop va 'scheme.path)))
         (or (not p) (every string? (cdr p))))
       #t)

;;; ---- C properties ----

(check "c.version is a non-empty string"
       (> (string-length (prop-value va 'c.version)) 0) #t)
(check "c.compile starts with a compiler command"
       (string? (cadr (prop va 'c.compile))) #t)
(check "c.link is a list of strings"
       (every string? (cdr (prop va 'c.link))) #t)
(define type-bits (cdr (prop va 'c.type-bits)))
(check "c.type-bits entries are (symbol integer)"
       (every (lambda (e) (and (list? e) (= (length e) 2)
                               (symbol? (car e)) (exact-integer? (cadr e))))
              type-bits)
       #t)
(check "c.type-bits: char is 8 bits" (cadr (assq 'char type-bits)) 8)
(check "c.type-bits: pointer matches 64-bit lp64 feature"
       (cadr (assq 'pointer type-bits))
       (if (memq 'lp64 (features)) 64 32))
(check "c.type-bits: int is 32 bits" (cadr (assq 'int type-bits)) 32)

;;; ---- (curry-version-alist) is what version-alist returns ----

(check "version-alist equals (curry-version-alist) apart from build.date"
       (let ((strip (lambda (l) (filter (lambda (e) (not (eq? (car e) 'build.date))) l))))
         (equal? (strip (version-alist)) (strip (curry-version-alist))))
       #t)

;;; ---- curry-version ----

(check "curry-version returns a string" (string? (curry-version)) #t)
(check "curry-version is non-empty" (> (string-length (curry-version)) 0) #t)

;;; ---- Summary ----

(newline)
(display "srfi-176 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
