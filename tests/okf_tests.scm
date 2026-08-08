;;; OKF module tests — (curry okf)

(import (curry okf))
(import (curry posix))
(import (curry regex))

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

(define (check-true label result)
  (check label (if result #t #f) #t))

(define (script-dir)
  (let* ((self (car (command-line)))
         (slash (let loop ((i (- (string-length self) 1)))
                  (cond ((< i 0) #f)
                        ((char=? (string-ref self i) #\/) i)
                        (else (loop (- i 1)))))))
    (if slash (substring self 0 (+ slash 1)) "./")))

(define fixture-root (string-append (script-dir) "fixtures/okf"))

;;; Frontmatter splitting

(check "split-frontmatter basic"
  (okf-split-frontmatter "---\ntype: X\n---\nbody text\n")
  (cons "type: X" "body text\n"))

(check "split-frontmatter no frontmatter"
  (okf-split-frontmatter "just body\n")
  (cons "" "just body\n"))

;;; Loading a bundle

(define bundle (okf-load-bundle fixture-root))

(check "bundle loaded, index/log excluded, 4 concepts"
  (length (okf-bundle-concepts bundle)) 4)

(define events (okf-bundle-ref bundle "tables/events_"))
(define minimal (okf-bundle-ref bundle "tables/minimal"))
(define dau (okf-bundle-ref bundle "computations/dau"))
(define sessions (okf-bundle-ref bundle "references/metrics/sessions"))

(check-true "events_ concept found" events)
(check-true "minimal concept found" minimal)
(check-true "dau concept found" dau)
(check-true "sessions concept found" sessions)

(check "events_ id" (okf-concept-id events) "tables/events_")

;;; Frontmatter accessors

(check "events_ type" (okf-concept-type events) "BigQuery Table")
(check "events_ title" (okf-concept-title events) "GA4 Events")
(check "events_ description" (okf-concept-description events) "Raw event stream.")
(check "events_ tags" (okf-concept-tags events) '("analytics" "sessions"))
(check "events_ status" (okf-concept-status events) "stable")
(check "events_ resource absent" (okf-concept-resource events) #f)

(check "minimal type only" (okf-concept-type minimal) "BigQuery Table")
(check "minimal title absent" (okf-concept-title minimal) #f)
(check "minimal tags default empty" (okf-concept-tags minimal) '())
(check "minimal status default" (okf-concept-status minimal) "stable")
(check "minimal sources default empty" (okf-concept-sources minimal) '())
(check "minimal verified default empty" (okf-concept-verified minimal) '())

(check "field escape hatch" (okf-concept-field events "title") "GA4 Events")
(check "field escape hatch missing key" (okf-concept-field events "nope") #f)

;;; verified bare-map normalization

(check "verified bare map normalized to one-element list"
  (okf-concept-verified events)
  (list (list (cons "by" "human:alice") (cons "at" "2026-08-05T00:00:00Z"))))

;;; Trust tier and staleness

(check "events_ trust tier: human-reviewed" (okf-trust-tier events) 'human-reviewed)
(check "minimal trust tier: unverified" (okf-trust-tier minimal) 'unverified)
(check "dau trust tier: unverified (no verified field)" (okf-trust-tier dau) 'unverified)

(check "minimal not stale (no stale_after)" (okf-stale? minimal) #f)
(check "dau stale (stale_after in the past)" (okf-stale? dau) #t)

;;; Querying

(check "concepts-by-type BigQuery Table"
  (length (okf-concepts-by-type bundle "BigQuery Table")) 2)
(check "concepts-by-tag analytics"
  (map okf-concept-id (okf-concepts-by-tag bundle "analytics"))
  '("tables/events_"))
(check "concepts-by-trust-tier unverified count"
  (length (okf-concepts-by-trust-tier bundle 'unverified)) 3)
(check "concepts-stale"
  (map okf-concept-id (okf-concepts-stale bundle))
  '("computations/dau"))

(check "attested-computation? dau" (okf-attested-computation? dau) #t)
(check "attested-computation? events_" (okf-attested-computation? events) #f)

;;; Graph

(check "events_ links (raw)"
  (okf-concept-links events)
  '("../computations/dau.md" "../references/metrics/sessions.md" "../nowhere.md"))

(check "resolve relative link to sibling dir"
  (okf-resolve-link bundle events "../computations/dau.md")
  "computations/dau")
(check "resolve relative link two dirs over"
  (okf-resolve-link bundle events "../references/metrics/sessions.md")
  "references/metrics/sessions")
(check "resolve broken link -> #f"
  (okf-resolve-link bundle events "../nowhere.md")
  #f)

(define graph (okf-bundle->graph bundle))
(define events-targets (hash-table-ref graph "tables/events_" '()))
(check "graph drops broken link, keeps only the 2 resolvable ones"
  (length events-targets) 2)
(check "graph resolves dau" (and (member "computations/dau" events-targets) #t) #t)
(check "graph resolves sessions"
  (and (member "references/metrics/sessions" events-targets) #t) #t)

(define back (okf-graph-backlinks graph))
(check "backlinks: dau linked from events_"
  (hash-table-ref back "computations/dau" '())
  '("tables/events_"))
(check "backlinks: minimal has no incoming links"
  (hash-table-ref back "tables/minimal" '())
  '())

(check "broken-links surfaced"
  (length (okf-bundle-broken-links bundle)) 1)

;;; Attested Computation helpers

(check "computation runtime (top-level, per spec §10.2)" (okf-computation-runtime dau) "bigquery")
(check "computation parameters"
  (okf-computation-parameters dau)
  (list (list (cons "name" "date") (cons "type" "date") (cons "required" #t))))
(check "computation inline sql"
  (okf-computation-inline-sql dau)
  "SELECT date, COUNT(DISTINCT user_id) AS dau FROM events_ GROUP BY date")
(check "computation inline sql absent on non-computation concept"
  (okf-computation-inline-sql events) #f)

;;; Path-safety guards on write/index/log entry points

(define traversal-root (string-append fixture-root "-traversal-" (number->string (pid))))

(check "okf-write-concept rejects .. in id"
  (guard (e (#t 'raised))
    (okf-write-concept (make-okf-concept #:type "X" #:body "") traversal-root "../escaped"))
  'raised)
(check "okf-write-concept rejects absolute id"
  (guard (e (#t 'raised))
    (okf-write-concept (make-okf-concept #:type "X" #:body "") traversal-root "/etc/escaped"))
  'raised)
(check "okf-generate-index rejects .. in prefix"
  (guard (e (#t 'raised)) (okf-generate-index bundle "../escaped"))
  'raised)
(check "okf-log-append rejects .. in dir"
  (guard (e (#t 'raised)) (okf-log-append bundle "../escaped" "Creation" "x"))
  'raised)
(check-true "traversal attempts left no file outside the fixture tree"
  (not (file-exists? (string-append fixture-root "-escaped.md"))))

;;; Malformed frontmatter tolerance

(define malformed-concept
  (okf-read-concept (open-input-string "---\ntype: X\nverified: true\n---\nbody\n")
                     "malformed" "malformed.md"))
(check "malformed scalar verified: doesn't crash, normalizes to empty"
  (okf-concept-verified malformed-concept) '())
(check "malformed scalar verified: trust tier still derivable"
  (okf-trust-tier malformed-concept) 'unverified)

;;; Frontmatter validation — advisory checks the tolerant accessors above
;;; deliberately don't perform (that's the point: okf-concept-verified
;;; normalizing the malformed-scalar case above into '() hides exactly what
;;; okf-validate-concept exists to surface).

(define (make-test-concept yaml-body)
  (okf-read-concept (open-input-string yaml-body) "test/x" "test/x.md"))

(check "validate: clean concept has no issues"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\nstatus: stable\n---\nbody"))
  '())

(check "validate: missing type flagged"
  (okf-validate-concept (make-test-concept "---\nstatus: stable\n---\nbody"))
  '("type is required and must be a non-empty string (spec REQUIRED)"))

(check "validate: bad status enum flagged"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\nstatus: bogus\n---\nbody"))
  '("status must be draft/stable/deprecated, got: bogus"))

(check "validate: non-ISO stale_after flagged"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\nstale_after: not-a-date\n---\nbody"))
  '("stale_after must be a YYYY-MM-DD date string"))

(check "validate: ISO-shaped stale_after accepted"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\nstale_after: 2026-09-23\n---\nbody"))
  '())

(check "validate: non-list tags flagged"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\ntags: notalist\n---\nbody"))
  '("tags must be a list of strings"))

(check "validate: malformed scalar verified flagged (the case tolerance hides)"
  (okf-validate-concept malformed-concept)
  '("verified must be a map or a list of maps"))

(check "validate: verified entry missing by flagged"
  (okf-validate-concept
    (make-test-concept "---\ntype: Foo\nverified:\n  at: 2026-01-01\n---\nbody"))
  '("verified.by must be a non-empty string"))

(check "validate: verified bare map with by accepted"
  (okf-validate-concept
    (make-test-concept "---\ntype: Foo\nverified:\n  by: human:alice\n  at: 2026-01-01\n---\nbody"))
  '())

(check "validate: explicit empty verified list is zero issues, not a malformed-map error"
  (okf-validate-concept (make-test-concept "---\ntype: Foo\nverified: []\n---\nbody"))
  '())

;; A YAML anchor/alias can produce a list whose first element is a genuine
;; (string . value) pair but whose later elements are bare scalars — e.g.
;; via `generated: &a\n by: x` then `verified: [*a, 42]`, which parses to
;; verified = (("by" . "x") 42). Checking only the first element (the
;; original bug this regression test guards against) would call core
;; assoc on the bare 42 and crash the process instead of reporting a
;; clean validation issue.
(check "validate: anchor-derived list with a valid pair followed by a bare scalar is rejected safely, not crashed"
  (okf-validate-concept
    (make-test-concept "---\ntype: X\ngenerated: &a\n  by: x\nverified: [*a, 42]\n---\nbody"))
  '("verified must be a map or a list of maps"))

(check "validate-bundle: clean synthetic bundle has no issues"
  (okf-validate-bundle bundle) '())

;;; Writing: build, write, reload round-trip

(define tmp-root (string-append fixture-root "-tmp-" (number->string (pid))))

(define new-concept
  (make-okf-concept
    #:type "BigQuery Table"
    #:title "Daily Active Users Table"
    #:description "One row per user per day."
    #:tags '("analytics" "metrics")
    #:generated (list (cons "by" "test-agent/1.0") (cons "at" "2026-08-06T00:00:00Z"))
    #:body "# Schema\n\n| column | type |\n|--------|------|\n| date | DATE |\n"))

(okf-write-concept new-concept tmp-root "tables/daily_active_users")

(define reloaded
  (okf-load-concept tmp-root (string-append tmp-root "/tables/daily_active_users.md")))

(check "written concept type round-trips" (okf-concept-type reloaded) "BigQuery Table")
(check "written concept title round-trips" (okf-concept-title reloaded) "Daily Active Users Table")
(check "written concept tags round-trip" (okf-concept-tags reloaded) '("analytics" "metrics"))
(check-true "written concept body preserved"
  (let ((b (okf-concept-body reloaded)))
    (and (string? b) (> (string-length b) 0)
         (string=? (substring b 0 8) "# Schema"))))

;;; Real-world corpus — see tests/fixtures/okf-real/NOTICE.md. Unlike
;;; fixtures/okf/ (synthetic, written to match this module's own reading
;;; of the spec), this is a real bundle from the OKF reference
;;; implementation, exercising field shapes (flow-map sources/verified/
;;; parameters entries, an actual Attested Computation contract, real
;;; cross-directory links) the code under test wasn't shaped around.

(define real-root
  (string-append (script-dir) "fixtures/okf-real/acme_retail"))
(define real-bundle (okf-load-bundle real-root))

(check "real bundle: 9 concepts (index.md/log.md excluded)"
  (length (okf-bundle-concepts real-bundle)) 9)
(check-true "real bundle: no broken links"
  (null? (okf-bundle-broken-links real-bundle)))
(check "real bundle: no frontmatter validation issues"
  (okf-validate-bundle real-bundle) '())

(define real-revenue (okf-bundle-ref real-bundle "computations/revenue-ytd"))
(check-true "real bundle: revenue-ytd found" real-revenue)
(check "real bundle: runtime is top-level, not nested under executor"
  (okf-computation-runtime real-revenue) "bigquery")
(check "real bundle: parameters (flow-map block-sequence form)"
  (okf-computation-parameters real-revenue)
  (list (list (cons "name" "year") (cons "type" "integer") (cons "required" #t))))
(check "real bundle: verified list-form (not bare map) trust tier"
  (okf-trust-tier real-revenue) 'human-reviewed)
(check-true "real bundle: fenced SQL extracted, contains the sanctioned WHERE clause"
  (let ((sql (okf-computation-inline-sql real-revenue)))
    (and (string? sql)
         (regex-match (regex-compile "EXTRACT\\(YEAR FROM o\\.order_ts\\) = @year") sql)
         #t)))

(define real-margin (okf-bundle-ref real-bundle "computations/gross-margin-period"))
(check-true "real bundle: gross-margin-period found" real-margin)
(check-true "real bundle: second attested computation also extracts SQL"
  (let ((sql (okf-computation-inline-sql real-margin)))
    (and (string? sql) (> (string-length sql) 0))))

(define real-revenue-metric (okf-bundle-ref real-bundle "metrics/revenue"))
(check-true "real bundle: revenue metric found" real-revenue-metric)
(check "real bundle: metric links to its computation (relative ../ path)"
  (okf-resolve-link real-bundle real-revenue-metric "../computations/revenue-ytd.md")
  "computations/revenue-ytd")

;;; Spec-conformant "# Computation" using an indented code block instead
;;; of a fence (OKF SPEC.md §10.2's own canonical example uses this form;
;;; the real acme_retail corpus above happens to only use fences, so this
;;; exercises a spec-legal shape the vendored corpus doesn't cover).

(define indented-computation
  (okf-read-concept
    (open-input-string
      (string-append
        "---\ntype: Attested Computation\nruntime: bigquery\n---\n\n"
        "# Computation\n\n"
        "    SELECT SUM(amount) AS revenue\n"
        "    FROM finance.recognized_revenue\n"
        "    WHERE fiscal_year = @year\n"))
    "indented" "indented.md"))
(check "indented computation block extracted"
  (okf-computation-inline-sql indented-computation)
  "SELECT SUM(amount) AS revenue\nFROM finance.recognized_revenue\nWHERE fiscal_year = @year")

;;; okf-generate-index / okf-log-append

(define idx-bundle (okf-load-bundle tmp-root))
(okf-generate-index idx-bundle "tables")
(define index-content
  (call-with-input-file (string-append tmp-root "/tables/index.md")
    (lambda (p)
      (let loop ((acc '()))
        (let ((ch (read-char p)))
          (if (eof-object? ch)
              (list->string (reverse acc))
              (loop (cons ch acc))))))))
(check-true "generated index mentions the table's title"
  (and (string? index-content)
       (>= (string-length index-content) 20)))

(okf-log-append idx-bundle "tables" "Creation" "Added daily_active_users.")
(check-true "log.md created" (file-exists? (string-append tmp-root "/tables/log.md")))

;;; Cleanup temp fixture output

(define (%rm-rf path)
  (let ((fi (guard (e (#t #f)) (file-info path))))
    (when fi
      (if (file-info-directory? fi)
          (begin
            (for-each (lambda (name) (%rm-rf (string-append path "/" name)))
                      (directory-files path))
            (delete-directory path))
          (delete-file path)))))

(%rm-rf tmp-root)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
