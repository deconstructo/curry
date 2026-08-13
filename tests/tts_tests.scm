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

;;; ════════════════════════════════════════════════════════════
;;; § 5  current-tts-voice / current-tts-rate / current-tts-language
;;; ════════════════════════════════════════════════════════════

(let ((original (current-tts-voice)))
  (check "current-tts-voice starts unset" (current-tts-voice) #f)
  (current-tts-voice "some-voice")
  (check "current-tts-voice set permanently" (current-tts-voice) "some-voice")
  (parameterize ((current-tts-voice "other-voice"))
    (check "parameterize scopes an override (voice)" (current-tts-voice) "other-voice"))
  (check "current-tts-voice reverts after parameterize" (current-tts-voice) "some-voice")
  (current-tts-voice original))

(let ((original (current-tts-rate)))
  (check "current-tts-rate starts unset" (current-tts-rate) #f)
  (current-tts-rate 200)
  (check "current-tts-rate set permanently" (current-tts-rate) 200)
  (current-tts-rate original))

(let ((original (current-tts-language)))
  (check "current-tts-language starts unset" (current-tts-language) #f)
  (current-tts-language "en")
  (check "current-tts-language set permanently" (current-tts-language) "en")
  (current-tts-language original))

(check-error "current-tts-language raises when no voice matches"
  (lambda ()
    (parameterize ((current-tts-language "zz-definitely-not-a-real-language"))
      (tts-speak "hi" #:backend (car (tts-backends))))))

;; An explicit #:voice must win over current-tts-language even when the
;; language would resolve to a different, valid voice -- the whole point
;; of %resolve-voice's (if voice ...) check coming before the language
;; branch. Exercised against a real backend since it needs a real voice
;; name to assert against.
(define (exercise-voice-beats-language sym)
  (when (tts-backend-available? sym)
    (let* ((voices (tts-voices #:backend sym))
           (explicit (car (car voices))))
      (parameterize ((current-tts-language "zz-definitely-not-a-real-language"))
        ;; explicit #:voice bypasses language resolution entirely, so an
        ;; unmatched current-tts-language must NOT raise here.
        (let ((h (tts-speak-async "hi" #:backend sym #:voice explicit)))
          (check (string-append "explicit #:voice overrides an unmatched current-tts-language (" (symbol->string sym) ")")
            (tts-speaking? h) #t)
          (tts-stop h)
          (tts-wait h))))))

(exercise-voice-beats-language 'macos-say)
(exercise-voice-beats-language 'espeak-ng)

;; current-tts-language lets tts-speak-async run with only the text
;; argument, once backend/voice/language defaults are all set up front —
;; the exact scenario this feature exists for.
(define (exercise-language-default sym)
  (when (tts-backend-available? sym)
    (parameterize ((current-tts-backend sym)
                   (current-tts-voice #f)
                   (current-tts-language "en"))
      (let ((h (tts-speak-async "hello")))
        (check (string-append "tts-speak-async needs only text once backend/language are set (" (symbol->string sym) ")")
          (tts-speaking? h) #t)
        (tts-stop h)
        (tts-wait h)))))

(exercise-language-default 'macos-say)
(exercise-language-default 'espeak-ng)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
