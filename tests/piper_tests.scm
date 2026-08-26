;;; Tests for (curry piper) / (curry tts piper) -- only registered in
;;; ctest when curry was built with -DBUILD_MODULE_PIPER=ON (see
;;; tests/CMakeLists.txt), since libpiper has no system package yet and
;;; can't be assumed present the way espeak-ng/say can.
;;;
;;; Live-synthesis checks (tts-save/tts-speak-async round trips) are
;;; further gated on (tts-backend-available? 'piper) -- true only once a
;;; real .onnx voice model has been downloaded into (current-piper-voice-
;;; dir) (see docs/reference/module-piper.md), which even a machine with
;;; BUILD_MODULE_PIPER=ON won't necessarily have -- skipped, not failed,
;;; the same convention tests/tts_tests.scm's own exercise-backend
;;; already uses for macos-say/espeak-ng's own real-tool availability.

(import (curry tts) (curry tts piper) (curry piper) (curry conditions) (curry posix))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " -- got ") (write got)
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
;;; § 1  Registration -- importing (curry tts piper) adds 'piper to the
;;; (curry tts) backend table (see tts.scm's own registry-widening
;;; comment for why this couldn't be baked in unconditionally there).
;;; ════════════════════════════════════════════════════════════

(check "importing (curry tts piper) registers 'piper into (tts-backends)"
  (if (memq 'piper (tts-backends)) #t #f) #t)

(check "tts-backend-available? 'piper does not raise regardless of voice availability"
  (guard (e (#t 'raised)) (tts-backend-available? 'piper))
  (tts-backend-available? 'piper)) ;; comparing the value against itself just proves no exception fired

;;; ════════════════════════════════════════════════════════════
;;; § 2  piper-create/piper-synth?/piper-version -- pure API-shape
;;; checks, no voice model needed.
;;; ════════════════════════════════════════════════════════════

(check "piper-synth? is false for a non-synth value" (piper-synth? 42) #f)
(check "piper-handle? is false for a non-handle value" (piper-handle? 42) #f)
(check "piper-version returns a string" (string? (piper-version)) #t)

(check-error "piper-create raises on a nonexistent model path"
  (lambda () (piper-create "/definitely/not/a/real/path.onnx" #f #f)))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Live round trip -- only when a real voice model is present.
;;; ════════════════════════════════════════════════════════════

(if (not (tts-backend-available? 'piper))
    (begin
      (display "SKIP: piper backend not available (no .onnx voice model found in ")
      (display (current-piper-voice-dir))
      (display ") -- download one with: python3 -m piper.download_voices <name>")
      (newline))
    (begin
      (check-error "tts-speak raises on an unknown voice (piper)"
        (lambda () (tts-speak "hi" #:backend 'piper #:voice "definitely-not-a-real-voice-xyz")))

      (let* ((voices (tts-voices #:backend 'piper))
             (path "/tmp/curry-tts-test-piper.wav"))
        (check "tts-voices returns a non-empty list (piper)"
          (> (length voices) 0) #t)

        (tts-save "hello from curry piper" path #:backend 'piper)
        (check "tts-save produces a WAV file with RIFF magic bytes (piper)"
          (list-head (magic-bytes path 4) 4)
          (list #\R #\I #\F #\F)))

      ;; Direct-to-speaker playback -- speaking?/stop/wait dispatch
      ;; through (curry tts)'s own %dispatch-handle-op (tts.scm), since
      ;; piper-speak-async's handle is NOT a (curry posix) process-
      ;; handle?, unlike macos-say/espeak-ng's own. This is the real
      ;; regression coverage for that widened dispatch mechanism, not
      ;; just piper-specific behavior.
      (let ((h (tts-speak-async "a somewhat longer sentence, to leave time for a liveness check"
                                 #:backend 'piper)))
        (check "tts-speaking? is #t immediately after tts-speak-async (piper)"
          (tts-speaking? h) #t)
        (check "the handle is NOT a (curry posix) process-handle? (piper uses a native thread, not a subprocess)"
          (process-handle? h) #f)
        (tts-stop h)
        (tts-wait h)
        (check "tts-speaking? is #f after tts-stop+tts-wait (piper)"
          (tts-speaking? h) #f))))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
