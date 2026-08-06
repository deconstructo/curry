;;; examples/mcp_okf.scm — Open Knowledge Format bundle as an MCP server
;;; Version: 1.0
;;;
;;; Loads an OKF bundle with (curry okf) and exposes it over MCP: every
;;; concept as its own browseable resource, plus tools for the structured
;;; queries an LLM would otherwise have to reimplement by reading every
;;; file in the bundle — find by type/tag, trust-tier summary, staleness,
;;; and the link graph (backlinks / broken links). This is the worked
;;; example from docs/thoughts/okf-module.md, made runnable.
;;;
;;; Resources: one per concept, "okf://concept/<id>" — the body only; use
;;; the get-concept tool for structured frontmatter access.
;;;
;;; Tools:
;;;   list-concepts    — every concept id, optionally filtered by type/tag
;;;   get-concept      — full frontmatter + body for one concept id
;;;   trust-summary    — concept counts per trust tier
;;;   stale-concepts   — concepts past their stale_after date
;;;   backlinks        — what links to a given concept id
;;;   broken-links     — links that don't resolve to any concept in the bundle
;;;
;;; Usage:
;;;   ./build/curry examples/mcp_okf.scm /path/to/bundle
;;;   ./build/curry examples/mcp_okf.scm   ; defaults to the bundled acme_retail sample
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-okf": {
;;;       "command": "/path/to/build/curry",
;;;       "args":    ["/path/to/examples/mcp_okf.scm", "/path/to/bundle"] } } }

(import (curry mcp) (curry okf))

;;; ---- Bundle ----

(define (script-dir)
  (let* ((self (car (command-line)))
         (slash (let loop ((i (- (string-length self) 1)))
                  (cond ((< i 0) #f)
                        ((char=? (string-ref self i) #\/) i)
                        (else (loop (- i 1)))))))
    (if slash (substring self 0 (+ slash 1)) "./")))

(define *bundle-root*
  (if (pair? (cdr (command-line)))
      (cadr (command-line))
      (string-append (script-dir) "../tests/fixtures/okf-real/acme_retail")))

(define *bundle* (okf-load-bundle *bundle-root*))
(define *graph*  (okf-bundle->graph *bundle*))
(define *back*   (okf-graph-backlinks *graph*))

;;; ---- Argument helpers (see docs/reference/module-mcp.md) ----

(define (arg args name) (cdr (assq name args)))
(define (arg? args name default)
  (let ((p (assq name args))) (if p (cdr p) default)))

;;; ---- Concept -> JSON-ish alist ----

(define (concept->alist c)
  `((id . ,(okf-concept-id c))
    (type . ,(or (okf-concept-type c) 'null))
    (title . ,(or (okf-concept-title c) 'null))
    (description . ,(or (okf-concept-description c) 'null))
    (tags . ,(okf-concept-tags c))
    (status . ,(okf-concept-status c))
    (stale_after . ,(or (okf-concept-stale-after c) 'null))
    (trust_tier . ,(symbol->string (okf-trust-tier c)))
    (stale . ,(okf-stale? c))
    (body . ,(okf-concept-body c))))

;;; ---- Resources: one per concept, so the LLM can fetch progressively ----

(for-each
  (lambda (c)
    (mcp-resource
      (string-append "okf://concept/" (okf-concept-id c))
      (string-append "OKF concept (" (or (okf-concept-type c) "untyped") "): "
                      (or (okf-concept-title c) (okf-concept-id c)))
      (lambda (uri) (mcp-text (okf-concept-body c)))))
  (okf-bundle-concepts *bundle*))

;;; ---- Tools ----

(mcp-tool "list-concepts" "List concept ids in the bundle, optionally filtered by type and/or tag"
  '((type . ((type . "string") (description . "OKF type, e.g. \"BigQuery Table\"") (default . "")))
    (tag  . ((type . "string") (description . "Tag to filter by") (default . ""))))
  (lambda (args)
    (let* ((type (arg? args 'type ""))
           (tag  (arg? args 'tag ""))
           (cs (okf-bundle-concepts *bundle*))
           (cs (if (string=? type "") cs (filter (lambda (c) (equal? (okf-concept-type c) type)) cs)))
           (cs (if (string=? tag "") cs (filter (lambda (c) (member tag (okf-concept-tags c))) cs))))
      (mcp-json (map okf-concept-id cs)))))

(mcp-tool "get-concept" "Fetch a concept's full frontmatter (as structured fields) and body by id"
  '((id . ((type . "string") (description . "Concept id, e.g. \"tables/orders\""))))
  (lambda (args)
    (let ((c (okf-bundle-ref *bundle* (arg args 'id))))
      (if c
          (mcp-json (concept->alist c))
          (mcp-text (string-append "no such concept: " (arg args 'id)))))))

(mcp-tool "trust-summary" "Count concepts per trust tier (unverified / machine-confirmed / human-reviewed)" '()
  (lambda (_)
    (mcp-json (map (lambda (tier)
                      (cons (symbol->string tier)
                            (length (okf-concepts-by-trust-tier *bundle* tier))))
                    '(unverified machine-confirmed human-reviewed)))))

(mcp-tool "stale-concepts" "List concepts whose stale_after date has passed" '()
  (lambda (_)
    (mcp-json (map (lambda (c)
                      `((id . ,(okf-concept-id c))
                        (stale_after . ,(okf-concept-stale-after c))
                        (trust_tier . ,(symbol->string (okf-trust-tier c)))))
                    (okf-concepts-stale *bundle*)))))

(mcp-tool "backlinks" "List concept ids that link to a given concept id"
  '((id . ((type . "string") (description . "Concept id to find backlinks for"))))
  (lambda (args)
    (mcp-json (hash-table-ref *back* (arg args 'id) '()))))

(mcp-tool "broken-links" "List every concept with links that don't resolve to a concept in this bundle" '()
  (lambda (_)
    (mcp-json (map (lambda (entry) `((id . ,(car entry)) (broken_targets . ,(cdr entry))))
                    (okf-bundle-broken-links *bundle*)))))

;;; ---- Serve ----

(mcp-serve "curry-okf" "1.0")
