;;; (curry tts) — cross-backend text-to-speech. Currently two backends:
;;; 'macos-say (built-in macOS `say`) and 'espeak-ng (Linux, via espeak-ng
;;; or the older espeak binary name). Both are plain CLI tools run through
;;; (curry posix)'s process-run/process-start -- no Objective-C, no
;;; framework linking, no new C module, and no shell-injection surface:
;;; process-run/process-start are backed by posix_spawn directly, never a
;;; shell, so text/voice/path arguments pass through as literal argv data.
;;;
;;; A caller can let the backend auto-select (current-tts-backend, seeded
;;; from (os-name) at import time) or force one explicitly via #:backend on
;;; any call -- see docs/reference/module-tts.md for the full API and the
;;; design rationale (a Scheme keyword-argument convention, #:foo, already
;;; established by (curry posix)'s own process-run/process-start).

(define-library (curry tts)
  (import (scheme base) (curry posix) (curry conditions) (curry tts macos) (curry tts espeak))
  (export
    tts-speak tts-speak-async tts-save
    tts-voices tts-backends tts-backend-available?
    tts-wait tts-stop tts-speaking?
    current-tts-backend)
  (begin

(define-condition tts-error (error) #:fields (backend))

;;; =========================================================================
;;; The backend protocol -- a record of closures, same shape (curry sql)'s
;;; own <sql-driver> uses and for the same reason: a handful of flat peer
;;; backends, no inheritance or dispatch machinery actually needed.
;;; =========================================================================

(define-record-type <tts-backend>
  (make-tts-backend available? speak-async save voices)
  tts-backend?
  (available? tts-backend-available-proc)  ; (proc) -> boolean, PATH check only
  (speak-async tts-backend-speak-async)    ; (proc text voice rate) -> process handle
  (save        tts-backend-save)           ; (proc text path voice rate) -> unspecified, blocks
  (voices      tts-backend-voices-proc))   ; (proc) -> list of (name . locale) pairs

(define %macos-backend
  (make-tts-backend macos-tts-available? macos-tts-speak-async macos-tts-save macos-tts-voices))

(define %espeak-backend
  (make-tts-backend espeak-tts-available? espeak-tts-speak-async espeak-tts-save espeak-tts-voices))

;; Fixed, small, known set -- doesn't need a registration mechanism the
;; way (curry conditions)'s open condition-type registry does.
(define %backend-table
  (list (cons 'macos-say %macos-backend)
        (cons 'espeak-ng %espeak-backend)))

(define (%lookup-backend sym)
  (let ((p (assq sym %backend-table)))
    (if p
        (cdr p)
        (condition-error 'tts-error (list (cons 'backend sym))
          (string-append "tts: unknown backend: " (symbol->string sym))))))

(define (tts-backends) (map car %backend-table))

(define (tts-backend-available? sym) ((tts-backend-available-proc (%lookup-backend sym))))

;;; =========================================================================
;;; Default backend -- auto-detected from (os-name) at import time,
;;; overridable permanently via (current-tts-backend 'sym) or scoped via
;;; parameterize -- make-parameter's own "called with one arg sets it"
;;; behavior (see src/builtins.c's prim_make_parameter) is exactly the
;;; same shape current-number-notation already uses, so this needs no
;;; wrapper procedure of its own.
;;; =========================================================================

(define (%default-backend)
  (if (string=? (os-name) "Darwin") 'macos-say 'espeak-ng))

(define current-tts-backend (make-parameter (%default-backend)))

;;; =========================================================================
;;; Keyword-argument scanning -- #:foo is a genuine self-evaluating
;;; reader token (Guile/Racket-style keyword symbol), the same convention
;;; (curry posix)'s own process-run/process-start use (find_kwarg in
;;; modules/posix/posix.c) -- this is that same idea at the Scheme level.
;;; =========================================================================

(define (%kwarg args keyword default)
  (let loop ((a args))
    (cond
      ((null? a) default)
      ((and (eq? (car a) keyword) (pair? (cdr a))) (cadr a))
      (else (loop (cdr a))))))

;;; =========================================================================
;;; Voice validation -- an unknown voice name fails cleanly here rather
;;; than being passed through to the subprocess blindly (see the design
;;; discussion in the plan: don't trust an arbitrary value, fail loudly).
;;; #f (no voice requested) always passes through untouched.
;;; =========================================================================

(define (%validate-voice backend-sym backend voice)
  (when (and voice (not (assoc voice ((tts-backend-voices-proc backend)))))
    (condition-error 'tts-error (list (cons 'backend backend-sym))
      (string-append "tts: unknown voice for backend " (symbol->string backend-sym) ": " voice)))
  voice)

;;; =========================================================================
;;; Public API
;;; =========================================================================

;; (tts-speak-async text . kwargs) -> process handle, non-blocking.
;; kwargs: #:voice name, #:rate words-per-minute, #:backend symbol.
(define (tts-speak-async text . kwargs)
  (let* ((backend-sym (%kwarg kwargs '#:backend (current-tts-backend)))
         (backend (%lookup-backend backend-sym))
         (voice (%validate-voice backend-sym backend (%kwarg kwargs '#:voice #f)))
         (rate (%kwarg kwargs '#:rate #f)))
    ((tts-backend-speak-async backend) text voice rate)))

;; (tts-speak text . kwargs) -- blocks until the utterance finishes.
(define (tts-speak text . kwargs)
  (process-wait (apply tts-speak-async text kwargs))
  (values))

;; (tts-save text path . kwargs) -- render to `path`, no sound played.
(define (tts-save text path . kwargs)
  (let* ((backend-sym (%kwarg kwargs '#:backend (current-tts-backend)))
         (backend (%lookup-backend backend-sym))
         (voice (%validate-voice backend-sym backend (%kwarg kwargs '#:voice #f)))
         (rate (%kwarg kwargs '#:rate #f)))
    ((tts-backend-save backend) text path voice rate)))

;; (tts-voices . kwargs) -> list of (name . locale) pairs for the active
;; (or #:backend-forced) backend.
(define (tts-voices . kwargs)
  ((tts-backend-voices-proc (%lookup-backend (%kwarg kwargs '#:backend (current-tts-backend))))))

;; Async-handle lifecycle -- tts-speak-async's return value is an
;; ordinary (curry posix) process handle, so these are direct
;; pass-throughs; kept as tts-* names so callers never need to know
;; that's what it is (a future backend need not be subprocess-based).
(define (tts-wait h) (process-wait h))
(define (tts-stop h) (process-kill h))
(define (tts-speaking? h) (process-alive? h))

  )) ;; end begin, define-library
