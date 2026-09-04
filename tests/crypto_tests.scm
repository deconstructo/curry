;;; crypto_tests.scm — (curry crypto) basic correctness + issue #161
;;; regression (unchecked bytevector-argument type confusion).
;;;
;;; No dedicated test file existed for this module before -- only
;;; akkadian_tests.scm's Akkadian-alias checks exercised it at all, and
;;; only incidentally. Added alongside the #161 fix since that fix is
;;; specifically about these functions rejecting non-bytevector
;;; arguments cleanly instead of crashing, which deserves its own
;;; regression coverage independent of the alias tests.

(import (scheme base) (curry crypto))

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

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

;;; ── Basic correctness (known-answer tests) ──────────────────────────────

(check "base64-encode known answer" (base64-encode (string->utf8 "hi")) "aGk=")
(check "md5-hex known answer (MD5(\"hi\") per RFC test vectors)"
  (md5-hex (string->utf8 "hi")) "49f68a5c8493ec2c0bf489821c21fc3b")
(check "sha1-hex known answer"
  (sha1-hex (string->utf8 "hi")) "c22b5f9178342609428d6f51b2c5af4c0bde6a42")
(check "sha256-hex known answer"
  (sha256-hex (string->utf8 "hi")) "8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4")
(check "hmac-sha256 produces a 32-byte bytevector"
  (bytevector-length (hmac-sha256 (string->utf8 "key") (string->utf8 "data"))) 32)

;;; ── Issue #161: unchecked bytevector-argument type confusion ────────────
;;;
;;; base64-encode/md5/sha1/sha256 (via a shared hash_via_evp helper)/
;;; hmac-sha256 all took their bytevector argument(s) straight from
;;; Scheme with no curry_is_bytevector check -- curry_bytevector_length/
;;; curry_bytevector_ref do an unchecked as_bytes() cast assuming their
;;; argument already IS a bytevector, the identical hazard #158 closed
;;; for socket handles. This was not merely a crash risk: passing a raw
;;; STRING (rather than the documented bytevector) didn't error under
;;; the old code, it silently computed a WRONG hash by misinterpreting
;;; the String object's own header layout as a Bytevector's -- confirmed
;;; (md5-hex "hi") returned a bogus result under the pre-fix code, not
;;; MD5("hi")'s real value. Every one of these must now reject a
;;; non-bytevector argument cleanly.
(check "base64-encode rejects a non-bytevector argument (was silent wrong output)"
  (raises? (lambda () (base64-encode 42))) #t)
(check "base64-encode rejects a raw string (documented as bytevector-only)"
  (raises? (lambda () (base64-encode "hi"))) #t)
(check "md5 rejects a non-bytevector argument (was a reproducible SIGSEGV)"
  (raises? (lambda () (md5 42))) #t)
(check "md5-hex rejects a raw string (was silently wrong, not just uncaught)"
  (raises? (lambda () (md5-hex "hi"))) #t)
(check "sha1 rejects a non-bytevector argument"
  (raises? (lambda () (sha1 42))) #t)
(check "sha256 rejects a non-bytevector argument"
  (raises? (lambda () (sha256 42))) #t)
(check "hmac-sha256 rejects a non-bytevector key"
  (raises? (lambda () (hmac-sha256 42 (string->utf8 "data")))) #t)
(check "hmac-sha256 rejects a non-bytevector data argument"
  (raises? (lambda () (hmac-sha256 (string->utf8 "key") 42))) #t)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
