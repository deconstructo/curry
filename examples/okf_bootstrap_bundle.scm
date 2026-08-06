;;; examples/okf_bootstrap_bundle.scm — build an OKF bundle from scratch
;;; Version: 1.0
;;;
;;; The producer side of (curry okf): mint concepts with make-okf-concept,
;;; write them with okf-write-concept, regenerate an index, and append a
;;; log entry — the same shape the reference agent's BQ pass uses (write
;;; one concept per real thing you discovered, then let index/log
;;; generation catch up), just with hand-written data instead of a live
;;; BigQuery connection.
;;;
;;; Builds a tiny two-table bundle plus one Metric that links to a table,
;;; regenerates both directories' index.md, and appends a bundle-root log
;;; entry — then reloads the bundle it just wrote and prints a report, to
;;; show the round trip actually works.
;;;
;;; Usage:
;;;   ./build/curry examples/okf_bootstrap_bundle.scm /tmp/my-bundle

(import (curry okf))
(import (curry posix))

(define root
  (if (pair? (cdr (command-line)))
      (cadr (command-line))
      (string-append "/tmp/okf-bootstrap-demo-" (number->string (pid)))))

(display "Writing a new bundle at: ") (display root) (newline)

;;; ---- Two tables, discovered independently (as if from schema introspection) ----

(okf-write-concept
  (make-okf-concept
    #:type "BigQuery Table"
    #:title "Customer Orders"
    #:description "One row per completed customer order."
    #:tags '("sales" "orders")
    #:generated (list (cons "by" "okf_bootstrap_bundle.scm/1.0")
                       (cons "at" "2026-08-06T00:00:00Z"))
    #:body (string-append
             "# Schema\n\n"
             "| column | type | description |\n"
             "|--------|------|-------------|\n"
             "| order_id | STRING | Globally unique order id |\n"
             "| customer_id | STRING | Foreign key into [customers](/tables/customers.md) |\n"
             "| total_usd | NUMERIC | Order total in USD |\n"))
  root "tables/orders")

(okf-write-concept
  (make-okf-concept
    #:type "BigQuery Table"
    #:title "Customers"
    #:description "One row per known customer."
    #:tags '("sales" "customers")
    #:generated (list (cons "by" "okf_bootstrap_bundle.scm/1.0")
                       (cons "at" "2026-08-06T00:00:00Z"))
    #:body (string-append
             "# Schema\n\n"
             "| column | type | description |\n"
             "|--------|------|-------------|\n"
             "| customer_id | STRING | Globally unique customer id |\n"
             "| region | STRING | Sales region |\n"))
  root "tables/customers")

;;; ---- A Metric that links back to a table it's derived from ----

(okf-write-concept
  (make-okf-concept
    #:type "Metric"
    #:title "Orders per Customer"
    #:description "Average number of orders placed per customer."
    #:tags '("sales" "engagement")
    #:generated (list (cons "by" "okf_bootstrap_bundle.scm/1.0")
                       (cons "at" "2026-08-06T00:00:00Z"))
    #:body (string-append
             "# Definition\n\n"
             "COUNT(*) from [orders](../tables/orders.md) divided by "
             "COUNT(DISTINCT customer_id) from [customers](../tables/customers.md).\n"))
  root "metrics/orders-per-customer")

;;; ---- Index + log ----

(okf-generate-index (okf-load-bundle root) "tables")
(okf-generate-index (okf-load-bundle root) "metrics")
(okf-generate-index (okf-load-bundle root) "")

(okf-log-append (okf-load-bundle root) "" "Creation"
  "Bootstrapped a 3-concept demo bundle (2 tables, 1 metric) via okf_bootstrap_bundle.scm.")

;;; ---- Reload what was just written and report on it ----

(newline) (display "Reloading the bundle just written...") (newline)
(define bundle (okf-load-bundle root))
(display "concepts: ") (display (length (okf-bundle-concepts bundle))) (newline)
(display "broken links: ") (display (length (okf-bundle-broken-links bundle))) (newline)

(for-each
  (lambda (c)
    (display "  ") (display (okf-concept-id c))
    (display "  (") (display (okf-concept-type c)) (display ")")
    (newline))
  (okf-bundle-concepts bundle))

(newline)
(display "tables/index.md:") (newline)
(display (call-with-input-file (string-append root "/tables/index.md")
           (lambda (p)
             (let loop ((acc '()))
               (let ((ch (read-char p)))
                 (if (eof-object? ch) (list->string (reverse acc)) (loop (cons ch acc))))))))
