;;; Tests for (srfi s215 log) -- SRFI 215: Central Log Exchange.
;;;
;;; Previously covered only by a "send-log is bound" smoke check
;;; (srfi_numbered_shims_tests.scm) -- this suite exercises the actual
;;; behavior: message shape, severity constants, current-log-fields
;;; merging, the value-conversion rules, the error paths, and the
;;; pre-install buffering/replay mechanism (the one genuinely subtle
;;; piece of this library: send-log calls made before any application
;;; callback is installed are buffered, up to 100 messages, and replayed
;;; in order into the first non-default callback installed afterward).
;;;
;;; Test ordering note: current-log-callback's buffer is a single
;;; module-level mutable list, shared across every send-log call in this
;;; process for the lifetime of the whole test file (not reset between
;;; check forms) -- but installing ANY non-default callback via
;;; parameterize replays and CLEARS the buffer as a side effect of
;;; parameter conversion (see log.scm's own log-callback-converter), so
;;; each section below that wraps its own send-log calls in a fresh
;;; (parameterize ((current-log-callback ...)) ...) block only ever
;;; observes messages sent within that same block -- no cross-section
;;; contamination, regardless of what ran before.

(import (srfi s215 log) (scheme base) (scheme write))

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

;;; ════════════════════════════════════════════════════════════
;;; § 1  severity constants -- exact values the spec assigns
;;; ════════════════════════════════════════════════════════════

(check "EMERGENCY" EMERGENCY 0)
(check "ALERT"     ALERT     1)
(check "CRITICAL"  CRITICAL  2)
(check "ERROR"     ERROR     3)
(check "WARNING"   WARNING   4)
(check "NOTICE"    NOTICE    5)
(check "INFO"      INFO      6)
(check "DEBUG"     DEBUG     7)

;;; ════════════════════════════════════════════════════════════
;;; § 2  message shape -- SEVERITY/MESSAGE keys, extra key/value pairs
;;; ════════════════════════════════════════════════════════════

(define captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log INFO "server started" 'PORT 8080))

(check "message includes SEVERITY"
  (cdr (assq 'SEVERITY captured)) INFO)
(check "message includes MESSAGE"
  (cdr (assq 'MESSAGE captured)) "server started")
(check "message includes extra key/value pairs"
  (cdr (assq 'PORT captured)) 8080)

;;; Multiple extra pairs, in order. Deliberately NOT just two assq
;;; lookups (assq finds a key regardless of its position in the alist,
;;; so that alone can't distinguish correct ordering from a swapped
;;; DEVICE/PERCENT bug -- found missing by independent code review of an
;;; earlier version of this check) -- (cddr captured) strips SEVERITY/
;;; MESSAGE and compares the exact remaining structure, since no
;;; current-log-fields are set in this section to append anything after.
(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log WARNING "disk low" 'DEVICE "sda1" 'PERCENT 95))
(check "multiple extra pairs appear in the exact order send-log was called with"
  (cddr captured)
  (list (cons 'DEVICE "sda1") (cons 'PERCENT 95)))

;;; ════════════════════════════════════════════════════════════
;;; § 3  current-log-fields -- appended after send-log's own pairs
;;; ════════════════════════════════════════════════════════════

(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg)))
               (current-log-fields '((HOST . "web1") (PID . 42))))
  (send-log ERROR "request failed" 'CODE 500))

(check "current-log-fields: HOST merged in"
  (cdr (assq 'HOST captured)) "web1")
(check "current-log-fields: PID merged in"
  (cdr (assq 'PID captured)) 42)
(check "current-log-fields: send-log's own pairs still present"
  (cdr (assq 'CODE captured)) 500)

;;; current-log-fields must not leak outside its own parameterize scope.
(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log INFO "outside the fields scope"))
(check "current-log-fields does not leak across parameterize scopes"
  (assq 'HOST captured) #f)

;;; ════════════════════════════════════════════════════════════
;;; § 4  value-conversion rules (log-value->field)
;;; ════════════════════════════════════════════════════════════

;;; string?/bytevector?/exact-integer? kept as-is (not stringified).
(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log DEBUG "types"
            'A-STRING "already a string"
            'A-BYTEVECTOR (bytevector 1 2 3)
            'AN-INTEGER 42))
(check "string value kept as-is"
  (cdr (assq 'A-STRING captured)) "already a string")
(check "bytevector value kept as-is (not converted to a string)"
  (bytevector? (cdr (assq 'A-BYTEVECTOR captured))) #t)
(check "exact-integer value kept as-is"
  (cdr (assq 'AN-INTEGER captured)) 42)

;;; error-object?/condition? kept as-is too.
(set! captured #f)
(define caught-error #f)
(guard (e (#t (set! caught-error e)))
  (error "a test error" 'irritant))
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log ERROR "an error occurred" 'THE-ERROR caught-error))
(check "error-object value kept as-is (not converted to a string)"
  (error-object? (cdr (assq 'THE-ERROR captured))) #t)

;;; Everything else (a symbol here) is converted to a string as if by write.
(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log DEBUG "a symbol value" 'SYM 'some-symbol))
(check "symbol value is converted to a written string"
  (cdr (assq 'SYM captured)) "some-symbol")

;;; A list value is also converted (write, not display) -- a string
;;; element inside it must come out quoted, distinguishing this from a
;;; plain display-based conversion.
(set! captured #f)
(parameterize ((current-log-callback (lambda (msg) (set! captured msg))))
  (send-log DEBUG "a list value" 'THE-LIST (list 1 "two" 'three)))
(check "list value is converted via write (quotes strings), not display"
  (cdr (assq 'THE-LIST captured)) "(1 \"two\" three)")

;;; ════════════════════════════════════════════════════════════
;;; § 5  error paths
;;; ════════════════════════════════════════════════════════════

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

(check "odd number of trailing key/value arguments raises"
  (raises? (lambda () (send-log INFO "msg" 'ONLY-A-KEY)))
  #t)
(check "a non-symbol key raises"
  (raises? (lambda () (send-log INFO "msg" "not-a-symbol" 'value)))
  #t)
(check "a well-formed call does not raise"
  (raises? (lambda ()
             (parameterize ((current-log-callback (lambda (msg) msg)))
               (send-log INFO "msg" 'KEY 'value))))
  #f)

;;; ════════════════════════════════════════════════════════════
;;; § 6  pre-install buffering and replay
;;; ════════════════════════════════════════════════════════════

;;; send-log calls made before any application callback is installed
;;; must not be silently dropped -- they're buffered, then replayed in
;;; order into the first non-default callback that gets installed.
(send-log INFO "buffered message one")
(send-log INFO "buffered message two")
(send-log INFO "buffered message three")

(define replayed '())
(parameterize ((current-log-callback (lambda (msg) (set! replayed (cons msg replayed)))))
  #f) ;; installing the callback alone triggers the replay
(set! replayed (reverse replayed))

(check "buffered messages were replayed (count)"
  (length replayed) 3)
(check "buffered messages replayed in original order (first)"
  (cdr (assq 'MESSAGE (car replayed))) "buffered message one")
(check "buffered messages replayed in original order (last)"
  (cdr (assq 'MESSAGE (list-ref replayed 2))) "buffered message three")

;;; The buffer must be empty after a replay -- a second, independent
;;; callback installation right afterward should receive nothing left
;;; over from the first.
(define second-replay '())
(parameterize ((current-log-callback (lambda (msg) (set! second-replay (cons msg second-replay)))))
  #f)
(check "buffer is cleared after a replay, not replayed twice"
  (length second-replay) 0)

;;; Restoring to the default callback (the normal parameterize exit
;;; path) must not itself trigger a replay attempt against the default
;;; buffering callback -- covered implicitly by every check above not
;;; raising or hanging, but assert explicitly that logging still works
;;; (buffers again) immediately afterward.
(send-log INFO "buffered after restoring the default")
(define third-replay '())
(parameterize ((current-log-callback (lambda (msg) (set! third-replay (cons msg third-replay)))))
  #f)
(check "buffering resumes correctly after the default callback is restored"
  (length third-replay) 1)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
