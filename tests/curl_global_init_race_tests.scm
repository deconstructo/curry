;;; curl_global_init_race_tests.scm — issue #156 regression.
;;;
;;; modules/http/http.c, modules/graphql/graphql.c, and
;;; modules/storage/storage.c each called curl_global_init with no
;;; synchronization at all (http.c had a racy hand-rolled "static int
;;; curl_inited" check; graphql.c/storage.c had no guard whatsoever,
;;; calling it unconditionally on every client construction). libcurl's
;;; own documentation states curl_global_init is NOT safe to call
;;; concurrently from multiple threads. Curry actors are real OS threads
;;; with no global interpreter lock, so two actors both constructing
;;; their first client (http, GraphQL, Swift, or Azure) concurrently
;;; raced on this directly. Fixed with a pthread_once_t guard per module
;;; (shared between fn_swift_client/fn_azure_client in storage.c, since
;;; they call into the same underlying libcurl global state).
;;;
;;; graphql-client/swift-client/azure-client all call curl_global_init
;;; directly in their constructor with no network access needed to
;;; reach that call -- so the race for those three is exercised by
;;; concurrent CONSTRUCTION alone, no live network required. Only
;;; http-request's race (inside do_request, called per-request, not at
;;; construction) needs an actual request -- skips cleanly if there's no
;;; network access, matching this suite's own established convention
;;; (see network_tests.scm's TLS section).

(import (scheme base) (curry sync))
(import (curry http))
(import (curry graphql))
(import (curry storage))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (all-done? actors)
  (or (null? actors)
      (and (not (actor-alive? (car actors))) (all-done? (cdr actors)))))

(define (run-concurrently thunks)
  (let ((actors (map spawn thunks)))
    (let loop () (if (all-done? actors) 'done (loop)))))

;;; ════════════════════════════════════════════════════════════
;;; graphql-client / swift-client / azure-client: concurrent first
;;; construction races curl_global_init directly, no network needed.
;;; ════════════════════════════════════════════════════════════

(run-concurrently
  (list (lambda () (graphql-client "http://localhost:1/graphql"))
        (lambda () (graphql-client "http://localhost:1/graphql"))
        (lambda () (swift-client "http://localhost:1" "u" "p" "proj"))
        (lambda () (swift-client "http://localhost:1" "u" "p" "proj"))
        (lambda () (azure-client "account" "a2V5"))
        (lambda () (azure-client "account" "a2V5"))
        (lambda () (graphql-client "http://localhost:1/graphql"))
        (lambda () (swift-client "http://localhost:1" "u" "p" "proj"))))
(check "concurrent graphql-client/swift-client/azure-client construction completes without crash or hang"
  #t #t)

;;; ════════════════════════════════════════════════════════════
;;; http-request: concurrent first request races curl_global_init
;;; inside do_request. Skips cleanly if there's no network access.
;;; ════════════════════════════════════════════════════════════

(define (network-reachable?)
  (guard (e (#t #f))
    (let ((r (http-request "GET" "http://example.com")))
      (and (pair? r) #t))))

(if (not (network-reachable?))
    (begin (display "SKIP: no network access — skipping http-request concurrency test") (newline))
    (begin
      (run-concurrently
        (list (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))
              (lambda () (http-request "GET" "http://example.com"))))
      (check "concurrent first http-request calls across actors complete without crash or hang"
        #t #t)))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
