;;; (curry schematic wiki) — svnwiki generator for commented Scheme source.
;;;
;;; Ported from Evan Hanson's BSD-licensed schematic
;;; (https://git.foldling.org/schematic/), specifically the
;;; schematic-wiki.scm script — see docs/reference/module-schematic.md.
;;; Generates svnwiki documentation fragments suitable for the CHICKEN
;;; wiki (http://wiki.call-cc.org/edit-help): builds on (curry schematic
;;; extract)'s s-expressive specifications, wrapping "documentation"
;;; types (procedure, syntax, constant, parameter, record, string, type)
;;; in svnwiki `<tag>form</tag>` markup and anything else (e.g.
;;; 'declaration) as a plain " type form" line.
;;;
;;; Any limitations of (curry schematic extract) apply equally here —
;;; this is upstream's own caveat, restated in
;;; docs/reference/module-schematic.md.

(define-library (curry schematic wiki)
  (import (scheme base) (scheme read) (scheme write) (curry schematic extract))
  (export scheme->wiki)
  (begin

(define %wiki-tags '(constant parameter procedure record string syntax type))

(define (%write-wiki-tag type form out)
  (display "<" out) (display type out) (display ">" out)
  (write form out)
  (display "</" out) (display type out) (display ">\n" out))

(define (%write-wiki-pre type form out)
  (display " " out) (display type out) (display " " out)
  (write form out) (display "\n" out))

(define (%write-wiki-doc doc out)
  (display "\n" out) (display doc out) (display "\n\n" out))

;; Converts the Scheme source on `input` to svnwiki fragments on
;; `output`. `types` and `comment-prefixes` are passed straight through
;; to extract-definitions (types: #f to describe definitions by their
;; own form, #t to use ": name type" declarations instead).
(define (scheme->wiki input output . opts)
  (let* ((types (if (pair? opts) (car opts) #f))
         (opts (if (pair? opts) (cdr opts) '()))
         (comment-prefixes (if (pair? opts) (car opts) (list ";;;" ";;"))))
    (let ((specs-out (open-output-string)))
      (extract-definitions input specs-out types comment-prefixes)
      (let ((specs-in (open-input-string (get-output-string specs-out))))
        (let loop ((spec (read specs-in)))
          (unless (eof-object? spec)
            (for-each
              (lambda (tag)
                (let ((type (car tag)))
                  (if (memq type %wiki-tags)
                      (%write-wiki-tag type (cdr tag) output)
                      (%write-wiki-pre type (cdr tag) output))))
              (cdr spec))
            (%write-wiki-doc (car spec) output)
            (loop (read specs-in))))))))

  )) ;; end begin, define-library
