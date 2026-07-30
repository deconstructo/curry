(define-library (srfi s59 vicinity)
  (import (scheme base))
  (export
    program-vicinity library-vicinity implementation-vicinity
    user-vicinity home-vicinity
    in-vicinity sub-vicinity make-vicinity pathname->vicinity ->vicinity
    vicinity:suffix? ->namestring)
  (begin

    ; Unix-only (curry's own build targets Linux/macOS): the directory
    ; separator is always #\/. SRFI-59 predates R7RS module search paths;
    ; library-vicinity and implementation-vicinity are best-effort here —
    ; CURRY_MODULE_PATH's first entry and curry's install prefix aren't
    ; introspectable from Scheme, so both fall back to program-vicinity.

    (define (vicinity:suffix? ch) (eqv? ch #\/))

    (define (pathname->vicinity path)
      (let loop ((i (- (string-length path) 1)))
        (cond ((< i 0) "")
              ((vicinity:suffix? (string-ref path i)) (substring path 0 (+ i 1)))
              (else (loop (- i 1))))))

    (define ->vicinity pathname->vicinity)

    (define (make-vicinity path)
      (if (and (> (string-length path) 0) (vicinity:suffix? (string-ref path (- (string-length path) 1))))
          path
          (string-append path "/")))

    (define (->namestring vicinity) vicinity)

    (define (in-vicinity vic name) (string-append vic name))

    (define (sub-vicinity vic name) (make-vicinity (string-append vic name)))

    (define (program-vicinity)
      (if (and (pair? (command-line)) (> (string-length (car (command-line))) 0))
          (pathname->vicinity (car (command-line)))
          "./"))

    (define (library-vicinity) (program-vicinity))
    (define (implementation-vicinity) (program-vicinity))

    (define (home-vicinity)
      (let ((h (get-environment-variable "HOME")))
        (if h (make-vicinity h) #f)))

    (define (user-vicinity) "")))
