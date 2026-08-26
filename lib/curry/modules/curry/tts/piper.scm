;;; (curry tts piper) — Piper neural TTS backend for (curry tts), via the
;;; native (curry piper) module (modules/piper/piper.c, libpiper). Unlike
;;; (curry tts macos)/(curry tts espeak), which are always-compiled pure
;;; Scheme + subprocess spawning, this library only exists at all when
;;; curry was built with -DBUILD_MODULE_PIPER=ON (libpiper has no system
;;; package yet -- see docs/reference/module-piper.md for the build
;;; steps) -- so (curry tts) itself does NOT unconditionally import this
;;; the way it does macos/espeak. A caller who wants piper support
;;; imports (curry tts piper) explicitly; this library's own top-level
;;; (begin ...) body registers itself into (curry tts)'s backend table
;;; as a side effect of that import, via tts-register-backend! (see
;;; lib/curry/modules/curry/tts.scm's own comment on why that registry
;;; had to widen from closed to open for exactly this case).
;;;
;;; Two real differences from macos-say/espeak-ng, both consequences of
;;; piper being a neural model loaded from a file rather than a system
;;; command:
;;;
;;;   1. "voice" here means an .onnx model file's basename (e.g.
;;;      "en_US-lessac-medium"), discovered by listing (current-piper-
;;;      voice-dir) (default: current-directory, matching upstream
;;;      piper's own --data-dir default) for *.onnx files -- there is no
;;;      runtime "list installed voices" command the way espeak-ng has
;;;      --voices; download voices with piper's own `python3 -m
;;;      piper.download_voices <name>` (see docs/reference/module-
;;;      piper.md). Loaded synthesizers are cached by voice name
;;;      (%synth-cache below) so repeated tts-speak calls with the same
;;;      voice don't reload the ONNX model every time -- a real cost,
;;;      unlike spawning `say`/`espeak-ng` fresh each call.
;;;
;;;   2. "rate" (words per minute, the same unit macos-say/espeak-ng's
;;;      own #:rate already uses) has no native piper equivalent --
;;;      piper's own knob is length_scale, a speed MULTIPLIER (0.5 =
;;;      twice as fast, 2.0 = twice as slow, 1.0 = the model's own
;;;      natural/reference rate). %wpm->length-scale below converts using
;;;      a fixed reference-WPM constant (150, a commonly cited average
;;;      speaking rate) -- necessarily a heuristic, not a piper-native
;;;      concept, called out here so nobody mistakes the resulting speed
;;;      for an exact WPM the way it genuinely is for the other two
;;;      backends.
;;;
;;; speak-async here does NOT return a (curry posix) process handle --
;;; piper-speak-async (curry piper) spawns a background pthread inside
;;; the C module doing native audio output (CoreAudio/ALSA), not a
;;; subprocess (see modules/piper/piper.c's own header comment for why:
;;; direct-to-speaker streaming as chunks are synthesized, no temp file,
;;; no external player process). That's exactly the case (curry tts)'s
;;; own wait/stop/speaking? dispatch (%dispatch-handle-op, tts.scm) was
;;; widened to support -- piper-handle? is this backend's handle?
;;; predicate, piper-wait/piper-stop!/piper-alive? its wait/stop/
;;; speaking? procs, all supplied to make-tts-backend below.

(define-library (curry tts piper)
  (import (scheme base) (curry posix) (curry piper) (curry conditions) (curry tts))
  (export
    piper-tts-available? piper-tts-speak-async piper-tts-save piper-tts-voices
    current-piper-voice-dir current-piper-espeak-data-path)
  (begin

(define-condition tts-error (error) #:fields (backend))

;; Same manual string-suffix helper as espeak.scm/macos.scm's own small,
;; self-contained per-file utilities (no shared private module for one
;; call site each -- see (curry postgres)/(curry mariadb)'s precedent,
;; already cited in both of those files).
(define (%string-suffix? suffix s)
  (let ((slen (string-length suffix)) (n (string-length s)))
    (and (<= slen n) (string=? suffix (substring s (- n slen) n)))))

(define (%string-remove-suffix suffix s)
  (substring s 0 (- (string-length s) (string-length suffix))))

(define current-piper-voice-dir (make-parameter (current-directory)))

;; Auto-located once at import time: the first of these that exists wins,
;; else #f (passed to piper-create as NULL -- "not needed", per
;; modules/piper/piper.c's own comment on piper_create's third
;; argument). A caller with espeak-ng data somewhere unusual can
;; override via (current-piper-espeak-data-path "...") same as any other
;; parameter here.
(define (%find-espeak-data)
  (let loop ((dirs (list "/opt/homebrew/share/espeak-ng-data"
                         "/usr/local/share/espeak-ng-data"
                         "/usr/share/espeak-ng-data")))
    (cond
      ((null? dirs) #f)
      ((file-exists? (car dirs)) (car dirs))
      (else (loop (cdr dirs))))))

(define current-piper-espeak-data-path (make-parameter (%find-espeak-data)))

(define (piper-tts-available?)
  (and (file-exists? (current-piper-voice-dir))
       (any-onnx-file? (current-piper-voice-dir))))

(define (any-onnx-file? dir)
  (let loop ((files (directory-files dir)))
    (cond
      ((null? files) #f)
      ((%string-suffix? ".onnx" (car files)) #t)
      (else (loop (cdr files))))))

(define (piper-tts-voices)
  (let ((dir (current-piper-voice-dir)))
    (map (lambda (f) (cons (%string-remove-suffix ".onnx" f) #f)) ; no locale metadata available without parsing each .onnx.json
         (filter (lambda (f) (%string-suffix? ".onnx" f)) (directory-files dir)))))

;; filter isn't a core builtin bound outside (scheme base)'s own list
;; procedures in this codebase's minimal core -- confirmed present via
;; (scheme base) here, unlike espeak.scm's `any` (SRFI-1 only, so that
;; file defines its own); no local definition needed.

;; Cache key is (voice-dir . voice-name), NOT voice-name alone -- found
;; missing on a self-review pass: current-piper-voice-dir can change
;; between two calls (it's an ordinary parameter, same set-once-or-
;; parameterize idiom as current-tts-voice etc.), and two different
;; directories can each have their own, genuinely different,
;; "en_US-lessac-medium.onnx". Keying on the name alone would silently
;; keep returning the FIRST directory's cached synth after such a
;; change, ignoring the new directory entirely -- a real correctness
;; bug, not just a cosmetic one, since it would speak in the wrong
;; voice/model with no error at all.
(define %synth-cache '()) ; alist: (voice-dir . voice-name) -> piper-synth handle

(define (%voice-path voice ext)
  (string-append (current-piper-voice-dir) "/" voice ext))

;; Loads (and caches) the piper-synth for `voice`, raising 'tts-error
;; with a clear message if the .onnx file isn't where piper-tts-voices
;; itself would have found it -- validated by (curry tts)'s own
;; %validate-voice against piper-tts-voices' own output before this is
;; ever reached in the normal tts-speak/tts-save path, so this is a
;; defensive recheck (e.g. a voice file removed between listing and
;; use), not the primary validation.
(define (%get-synth voice)
  (let* ((key (cons (current-piper-voice-dir) voice))
         (cached (assoc key %synth-cache)))
    (if cached
        (cdr cached)
        (let ((model-path (%voice-path voice ".onnx")))
          (unless (file-exists? model-path)
            (condition-error 'tts-error (list (cons 'backend 'piper))
              (string-append "tts: no such piper voice: " voice
                             " (expected " model-path ")")))
          (let ((synth (piper-create model-path #f (current-piper-espeak-data-path))))
            (set! %synth-cache (cons (cons key synth) %synth-cache))
            synth)))))

;; A words-per-minute -> length_scale heuristic, not a piper-native
;; concept -- see this file's own header comment for why. #f (no rate
;; requested) means "use the model's own default length_scale", passed
;; through as #f all the way to piper-create's own default-options path
;; (modules/piper/piper.c's resolve_options).
(define %reference-wpm 150)
(define (%wpm->length-scale rate)
  (and rate (/ (exact->inexact %reference-wpm) rate)))

(define (piper-tts-speak-async text voice rate)
  (piper-speak-async (%get-synth (or voice (%first-voice-or-error))) text #f (%wpm->length-scale rate)))

(define (piper-tts-save text path voice rate)
  (piper-save (%get-synth (or voice (%first-voice-or-error))) text path #f (%wpm->length-scale rate)))

;; Unlike macos-say/espeak-ng (which have a real system default voice
;; when none is requested), piper has no notion of a default voice at
;; all -- SOME .onnx file must be named. Picks the first one
;; piper-tts-voices finds rather than silently failing with a confusing
;; "model-path required" error from piper-create; raises a clear
;; 'tts-error if the voice directory has none.
(define (%first-voice-or-error)
  (let ((voices (piper-tts-voices)))
    (if (null? voices)
        (condition-error 'tts-error (list (cons 'backend 'piper))
          (string-append "tts: no piper voices found in " (current-piper-voice-dir)
                         " -- download one with: python3 -m piper.download_voices <name>"))
        (car (car voices)))))

;; Self-registration -- see this file's own header comment for why
;; (curry tts) can't unconditionally import this the way it does macos/
;; espeak. Runs once, as a side effect of importing this library.
(tts-register-backend! 'piper
  (make-tts-backend piper-tts-available? piper-tts-speak-async piper-tts-save piper-tts-voices
                     piper-wait piper-stop! piper-alive? piper-handle?))

  )) ;; end begin, define-library
