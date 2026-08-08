;;; (curry schematic read) — line-based source/comment-block reader.
;;;
;;; Ported from Evan Hanson's BSD-licensed schematic
;;; (https://git.foldling.org/schematic/), specifically src/schematic/
;;; read.scm — see docs/reference/module-schematic.md for the full scope
;;; of the port and what changed. This piece is small and close to
;;; upstream's own shape: a single fold over a port's lines, splitting
;;; it into alternating (comment . code) "sections" wherever a run of
;;; line comments is immediately followed by a run of non-comment code,
;;; or vice versa.
;;;
;;; curry has no SRFI-13 string-trim/string-prefix?, so those are
;;; reimplemented locally here, same as upstream's own module does (it
;;; can't assume a host Scheme provides them either).

(define-library (curry schematic read)
  (import (scheme base) (scheme char))
  (export port-fold-source-sections)
  (begin

;; True iff `prefix` is a prefix of `str`.
(define (%string-prefix? prefix str)
  (let ((lp (string-length prefix)) (ls (string-length str)))
    (and (<= lp ls)
         (let loop ((i 0))
           (or (= i lp) (and (char=? (string-ref prefix i) (string-ref str i)) (loop (+ i 1))))))))

;; Trims every leading/trailing character in `chars` (a char or list of
;; chars) from `str`.
(define (%string-trim str chars)
  (let ((cs (if (pair? chars) chars (list chars))) (len (string-length str)))
    (let loop-start ((i 0))
      (if (and (< i len) (memv (string-ref str i) cs))
          (loop-start (+ i 1))
          (let loop-end ((j (- len 1)))
            (if (and (>= j i) (memv (string-ref str j) cs))
                (loop-end (- j 1))
                (substring str i (+ j 1))))))))

(define (%write-line str port) (display str port) (newline port))

(define (%chomp port) (%string-trim (get-output-string port) #\newline))

;; Builds a predicate testing whether a line starts with one of
;; `prefixes` (curry's own comment lines default to ";;" and ";;;", but
;; a caller may pass any list of line-comment prefix strings).
(define (%comment-predicate prefixes)
  (lambda (str)
    (let loop ((ps prefixes))
      (and (pair? ps) (or (%string-prefix? (car ps) str) (loop (cdr ps)))))))

;; Builds an accessor returning the documentation part of a comment
;; line: the text after a matched prefix and exactly one whitespace
;; character, or "" if the line is a bare/undocumented comment marker
;; (e.g. ";;;" on its own, or a prefix with no space after it — which
;; upstream treats as a non-documentation "section" comment, such as a
;; banner rule of semicolons).
(define (%comment-content-accessor prefixes)
  (lambda (str)
    (let ((len (string-length str)))
      (let loop ((ps prefixes))
        (if (null? ps)
            ""
            (let ((pl (string-length (car ps))))
              (cond
                ((= pl len) "")
                ((and (<= pl len) (%string-prefix? (car ps) str) (char-whitespace? (string-ref str pl)))
                 (substring str (+ pl 1) len))
                (else (loop (cdr ps))))))))))

;; Folds `kons` over every (comment . code) section read from `port`, in
;; order: `kons` is called as (kons comment-text code-text acc) once per
;; section, where a "section" is a maximal run of one kind (comment or
;; code) immediately followed by a run of the other kind. Both texts
;; have their comment-prefix markers stripped and leading/trailing blank
;; lines trimmed. `comment-prefixes` is a list of line-comment prefix
;; strings (curry's own convention: '(";;;" ";;")).
(define (port-fold-source-sections kons knil comment-prefixes port)
  (let ((comment? (%comment-predicate comment-prefixes))
        (comment-content (%comment-content-accessor comment-prefixes)))
    (let next-section ((docs (open-output-string)) (code (open-output-string)) (acc knil))
      (let next-line ((in-docs? #t))
        (let ((line (read-line port)))
          (cond
            ((eof-object? line) (kons (%chomp docs) (%chomp code) acc))
            (else
             (let ((trimmed (%string-trim line (list #\tab #\space))))
               (cond
                 ((not (comment? trimmed))
                  (%write-line line code)
                  (next-line #f))
                 (in-docs?
                  (%write-line (comment-content trimmed) docs)
                  (next-line #t))
                 (else
                  ;; A comment line following code: close out the
                  ;; current section and start a fresh one, seeding its
                  ;; comment buffer with this line.
                  (let ((docs-text (%chomp docs))
                        (code-text (%chomp code))
                        (docs* (open-output-string))
                        (code* (open-output-string)))
                    (%write-line (comment-content trimmed) docs*)
                    (next-section
                      docs* code*
                      (if (and (string=? "" docs-text) (string=? "" code-text))
                          acc
                          (kons docs-text code-text acc))))))))))))))

  )) ;; end begin, define-library
