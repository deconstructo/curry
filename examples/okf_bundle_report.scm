;;; examples/okf_bundle_report.scm — plain-text health report for an OKF bundle
;;; Version: 1.0
;;;
;;; No MCP, no server, just (curry okf) doing what it's for: load a bundle,
;;; ask it questions, print the answers. Good first thing to run against a
;;; bundle you don't know yet — a text-only "what's in here, what's stale,
;;; what's broken" summary.
;;;
;;; Usage:
;;;   ./build/curry examples/okf_bundle_report.scm /path/to/bundle
;;;   ./build/curry examples/okf_bundle_report.scm   ; defaults to the bundled acme_retail sample

(import (curry okf))

(define (script-dir)
  (let* ((self (car (command-line)))
         (slash (let loop ((i (- (string-length self) 1)))
                  (cond ((< i 0) #f)
                        ((char=? (string-ref self i) #\/) i)
                        (else (loop (- i 1)))))))
    (if slash (substring self 0 (+ slash 1)) "./")))

(define root
  (if (pair? (cdr (command-line)))
      (cadr (command-line))
      (string-append (script-dir) "../tests/fixtures/okf-real/acme_retail")))

(define bundle (okf-load-bundle root))
(define concepts (okf-bundle-concepts bundle))
(define graph (okf-bundle->graph bundle))
(define back (okf-graph-backlinks graph))

(define (heading s)
  (newline) (display s) (newline)
  (display (make-string (string-length s) #\=)) (newline))

(define (count-by key-fn items)
  (let ((counts (make-hash-table)))
    (for-each
      (lambda (item)
        (let ((k (key-fn item)))
          (hash-table-set! counts k (+ 1 (hash-table-ref counts k 0)))))
      items)
    (hash-table->alist counts)))

;;; ---- Overview ----

(heading (string-append "OKF bundle: " root))
(display "concepts: ") (display (length concepts)) (newline)

;;; ---- By type ----

(heading "By type")
(for-each
  (lambda (p) (display "  ") (display (cdr p)) (display "  ") (display (car p)) (newline))
  (count-by (lambda (c) (or (okf-concept-type c) "(untyped)")) concepts))

;;; ---- Trust tiers ----

(heading "Trust tiers")
(for-each
  (lambda (tier)
    (display "  ") (display (length (okf-concepts-by-trust-tier bundle tier)))
    (display "  ") (display tier) (newline))
  '(unverified machine-confirmed human-reviewed))

;;; ---- Stale concepts ----

(heading "Stale concepts")
(let ((stale (okf-concepts-stale bundle)))
  (if (null? stale)
      (display "  (none)\n")
      (for-each
        (lambda (c)
          (display "  ") (display (okf-concept-id c))
          (display " (stale_after ") (display (okf-concept-stale-after c)) (display ")")
          (newline))
        stale)))

;;; ---- Broken links ----

(heading "Broken links")
(let ((broken (okf-bundle-broken-links bundle)))
  (if (null? broken)
      (display "  (none)\n")
      (for-each
        (lambda (entry)
          (display "  ") (display (car entry)) (display " -> ")
          (display (cdr entry)) (newline))
        broken)))

;;; ---- Attested Computations ----

(heading "Attested Computations")
(for-each
  (lambda (c)
    (display "  ") (display (okf-concept-id c))
    (display "  runtime=") (display (or (okf-computation-runtime c) "?"))
    (display "  linked-from=") (display (length (hash-table-ref back (okf-concept-id c) '())))
    (newline))
  (filter okf-attested-computation? concepts))

;;; ---- Most-linked-to concepts (top 5) ----

(heading "Most-linked-to concepts")

;; No sort primitive assumed available in core — simple selection of the
;; top 5 by repeated max-extraction instead of depending on one.
(let loop ((remaining
             (filter (lambda (p) (> (cdr p) 0))
                     (map (lambda (c) (cons (okf-concept-id c) (length (hash-table-ref back (okf-concept-id c) '()))))
                          concepts)))
           (shown 0))
  (if (or (null? remaining) (>= shown 5))
      (when (= shown 0) (display "  (nothing is linked to)\n"))
      (let ((best (let find ((lst (cdr remaining)) (best (car remaining)))
                    (if (null? lst) best
                        (find (cdr lst) (if (> (cdar lst) (cdr best)) (car lst) best))))))
        (display "  ") (display (car best)) (display "  <- ") (display (cdr best)) (display " links")
        (newline)
        (loop (filter (lambda (p) (not (equal? (car p) (car best)))) remaining) (+ shown 1)))))
