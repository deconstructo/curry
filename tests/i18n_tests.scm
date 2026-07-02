;;; tests/i18n_tests.scm — Tests for the pluggable language pack system.

(define pass-count 0)
(define fail-count 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (set! pass-count (+ pass-count 1))
             (display "PASS: ") (display label) (newline))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (display expected) (newline)
             (display "  got:      ") (display got) (newline))))

(define (check-true label val)
  (if val
      (begin (set! pass-count (+ pass-count 1))
             (display "PASS: ") (display label) (newline))
      (begin (set! fail-count (+ fail-count 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected truthy, got: ") (display val) (newline))))

;;; ── Built-in Akkadian pack ──────────────────────────────────────────────────

(check "akkadian is active by default"
       (active-language) "akkadian")

(check-true "akkadian in registered-languages"
            (member "akkadian" (registered-languages)))

(check-true "language-info returns alist"
            (pair? (language-info "akkadian")))

(check "language-info id field"
       (let ((info (language-info "akkadian")))
         (cdr (assq 'id info)))
       "akkadian")

(check "language-info has mappings"
       (let* ((info (language-info "akkadian"))
              (mps  (cdr (assq 'mappings info))))
         (> (length mps) 100))
       #t)

(check "language-intro returns string"
       (string? (language-intro "akkadian"))
       #t)

;;; Akkadian translation works at eval time
(check "šakānum translates define"
       (begin (šakānum akk-test-val 99) akk-test-val)
       99)

(check "epēšum translates lambda"
       ((epēšum (x) (* x x)) 5)
       25)

;;; ── Register a test language ────────────────────────────────────────────────

(register-language!
  `((id           . "test-lang")
    (display-name . "Test Language")
    (intro        . "Test mode active!")
    (error-preamble . "TEST ERROR:")
    (mappings     .
      ((hola   define  "greet / name a thing")
       (mundo  display "show to the world")
       (suma   +       "add together")))))

(check-true "test-lang registered"
            (member "test-lang" (registered-languages)))

(check "language-info for test-lang"
       (let ((info (language-info "test-lang")))
         (cdr (assq 'display-name info)))
       "Test Language")

(check "language-intro for test-lang"
       (language-intro "test-lang")
       "Test mode active!")

;;; Activate and verify translation
(set-active-language! "test-lang")

(check "active-language after set"
       (active-language) "test-lang")

;;; Foreign names now resolve in env
(check "hola works as define"
       (begin (hola test-x 42) test-x)
       42)

(check "suma works as +"
       (suma 3 4)
       7)

;;; ── Switch back to Akkadian ─────────────────────────────────────────────────

(set-active-language! "akkadian")

(check "switched back to akkadian"
       (active-language) "akkadian")

;;; Akkadian names still work after switching back
(check "define still works after switch"
       (begin (define post-switch 77) post-switch)
       77)

;;; ── language-info returns #f for unknown pack ────────────────────────────────

(check "language-info unknown returns #f"
       (language-info "nonexistent-lang")
       #f)

(check "language-intro unknown returns #f"
       (language-intro "nonexistent-lang")
       #f)

;;; ── Deactivate (set to #f) ──────────────────────────────────────────────────

(set-active-language! #f)

(check "deactivated: active-language is #f"
       (active-language) #f)

;; Restore akkadian for any subsequent tests
(set-active-language! "akkadian")

;;; ── Summary ─────────────────────────────────────────────────────────────────

(newline)
(display pass-count) (display " passed, ")
(display fail-count) (display " failed")
(newline)

(when (> fail-count 0)
  (error "i18n tests failed" fail-count))
