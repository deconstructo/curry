;;; Text-to-speech tests — (curry tts)
;;;
;;; Unlike (curry mariadb)/(curry postgres), neither backend here needs a
;;; "server" that might not be running -- `say` ships with macOS itself,
;;; and `espeak-ng`/`espeak` is a plain installed CLI tool on Linux, so
;;; this suite exercises real synthesis end to end wherever the relevant
;;; backend is actually available (checked via tts-backend-available?,
;;; never assumed), rather than being limited to import/error-path checks
;;; the way the SQL server-backed suites are.

(import (curry tts) (curry conditions))

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

(define (check-error label thunk)
  (if (guard (e (#t #t)) (thunk) #f)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (display " did not raise") (newline)
             (set! fail (+ fail 1)))))

(define (magic-bytes path n)
  (call-with-port (open-input-file path)
    (lambda (p)
      (let loop ((i 0) (acc '()))
        (if (= i n) (reverse acc) (loop (+ i 1) (cons (read-char p) acc)))))))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Backend registry (no subprocess spawned)
;;; ════════════════════════════════════════════════════════════

(check "tts-backends lists both registered backends" (tts-backends) '(macos-say espeak-ng))
(check "tts-backend-available? rejects an unknown backend symbol" (tts-backend-available? 'not-a-real-backend) #f)

;;; ════════════════════════════════════════════════════════════
;;; § 2  current-tts-backend — get/set/parameterize
;;; ════════════════════════════════════════════════════════════

(let ((original (current-tts-backend)))
  (check "current-tts-backend starts as one of the two registered backends"
    (if (memv (current-tts-backend) (tts-backends)) #t #f) #t)
  (current-tts-backend 'espeak-ng)
  (check "current-tts-backend set permanently" (current-tts-backend) 'espeak-ng)
  (parameterize ((current-tts-backend 'macos-say))
    (check "parameterize scopes an override" (current-tts-backend) 'macos-say))
  (check "current-tts-backend reverts after parameterize" (current-tts-backend) 'espeak-ng)
  (current-tts-backend original))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Error paths (no live backend needed)
;;; ════════════════════════════════════════════════════════════

(check-error "tts-speak raises on an unknown #:backend"
  (lambda () (tts-speak "hi" #:backend 'not-a-real-backend)))

(check-error "tts-voices raises on an unknown #:backend"
  (lambda () (tts-voices #:backend 'not-a-real-backend)))

(guard (e (#t
           (check "unknown-backend error is a 'tts-error" (condition-is-a? e 'tts-error) #t)
           (check "unknown-backend error's backend field is the requested symbol"
             (condition-field e 'backend) 'not-a-real-backend)))
  (tts-speak "hi" #:backend 'not-a-real-backend)
  (check "unreachable" #t #f))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Per-backend live checks — only when actually available
;;; ════════════════════════════════════════════════════════════

(define (exercise-backend sym ext)
  (if (not (tts-backend-available? sym))
      (begin
        (display "SKIP: ") (display sym) (display " not available on this machine") (newline))
      (begin
        (check-error (string-append "tts-speak raises on an unknown voice (" (symbol->string sym) ")")
          (lambda () (tts-speak "hi" #:backend sym #:voice "definitely-not-a-real-voice-xyz")))

        (let* ((voices (tts-voices #:backend sym))
               (path (string-append "/tmp/curry-tts-test-" (symbol->string sym) "." ext)))
          (check (string-append "tts-voices returns a non-empty list (" (symbol->string sym) ")")
            (> (length voices) 0) #t)

          (tts-save "hello from curry" path #:backend sym)
          (check (string-append "tts-save produces a non-empty file (" (symbol->string sym) ")")
            (> (call-with-port (open-input-file path) (lambda (p) (let loop ((n 0)) (if (eof-object? (read-char p)) n (loop (+ n 1)))))) 0)
            #t))

        (let ((h (tts-speak-async "a somewhat longer sentence, to leave time for a liveness check" #:backend sym)))
          (check (string-append "tts-speaking? is #t immediately after tts-speak-async (" (symbol->string sym) ")")
            (tts-speaking? h) #t)
          (tts-stop h)
          (tts-wait h)
          (check (string-append "tts-speaking? is #f after tts-stop+tts-wait (" (symbol->string sym) ")")
            (tts-speaking? h) #f)))))

(exercise-backend 'macos-say "aiff")
(exercise-backend 'espeak-ng "wav")

;;; macOS-specific: confirm the "AIFF" magic (real-file format check,
;;; matching graphviz_tests.scm's own PNG-magic-byte precedent) --
;;; skipped, not failed, when say isn't available (e.g. running on Linux).
(when (tts-backend-available? 'macos-say)
  (let ((path "/tmp/curry-tts-test-macos-say.aiff"))
    (check "macOS tts-save output has AIFF-C magic bytes (FORM)"
      (list-head (magic-bytes path 4) 4)
      (list #\F #\O #\R #\M))))

;;; espeak-ng-specific: confirm the "RIFF"/WAVE magic bytes.
(when (tts-backend-available? 'espeak-ng)
  (let ((path "/tmp/curry-tts-test-espeak-ng.wav"))
    (check "espeak-ng tts-save output has RIFF magic bytes"
      (list-head (magic-bytes path 4) 4)
      (list #\R #\I #\F #\F))))

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
