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
    current-tts-backend current-tts-voice current-tts-rate current-tts-language
    ;; Extensibility for an optionally-compiled backend (e.g. (curry tts
    ;; piper), which only exists at all when curry was built with
    ;; -DBUILD_MODULE_PIPER=ON) -- see the "Backend registration" section
    ;; below for why this widens what used to be a deliberately closed,
    ;; fixed table.
    make-tts-backend tts-register-backend!)
  (begin

(define-condition tts-error (error) #:fields (backend))

;;; =========================================================================
;;; The backend protocol -- a record of closures, same shape (curry sql)'s
;;; own <sql-driver> uses and for the same reason: a handful of flat peer
;;; backends, no inheritance or dispatch machinery actually needed.
;;; =========================================================================

;; wait/stop/speaking? default to #f, meaning "this handle is an ordinary
;; (curry posix) process handle -- use process-wait/process-kill/
;; process-alive? directly", exactly the contract macos-say/espeak-ng's
;; own handles already documented before this widening (see
;; docs/reference/module-tts.md's own "async-handle lifecycle" section)
;; -- neither backend's own definition below needed to change at all.
;; Only a backend whose speak-async return value ISN'T a real OS process
;; (e.g. (curry tts piper): a background pthread doing native audio
;; output, not a subprocess -- see modules/piper/piper.c's own header
;; comment) needs to supply its own three procs, and %dispatch-handle-op
;; below picks between the two by asking process-handle? about the
;; handle itself, not by remembering which backend produced it -- so
;; tts-wait/tts-stop/tts-speaking? (which take just a handle, no backend
;; context -- see their own definitions further down) keep working
;; exactly as before with no change to THEIR signatures either.
(define-record-type <tts-backend>
  (%make-tts-backend available? speak-async save voices wait stop speaking? handle?)
  tts-backend?
  (available? tts-backend-available-proc)  ; (proc) -> boolean, PATH check only
  (speak-async tts-backend-speak-async)    ; (proc text voice rate) -> handle
  (save        tts-backend-save)           ; (proc text path voice rate) -> unspecified, blocks
  (voices      tts-backend-voices-proc)    ; (proc) -> list of (name . locale) pairs
  (wait        tts-backend-wait-proc)      ; (handle) -> unspecified, blocks -- or #f, see above
  (stop        tts-backend-stop-proc)      ; (handle) -> unspecified -- or #f
  (speaking?   tts-backend-speaking-proc)  ; (handle) -> boolean -- or #f
  (handle?     tts-backend-handle-proc))   ; (v) -> boolean, "is v one of MY handles" -- or #f;
                                            ; only consulted when v isn't a (curry posix)
                                            ; process-handle?, see %dispatch-handle-op below

;; Public constructor: wait/stop/speaking? are genuinely optional (a
;; trailing . rest, not the #!optional some other Schemes' define-record-
;; type constructors support -- unused anywhere else in this codebase),
;; defaulting to #f each -- see <tts-backend>'s own comment above for
;; what #f means here. A backend that supplies wait/stop/speaking? MUST
;; also supply handle? (its own speak-async's return value is by
;; definition not a process-handle?, so tts-wait/tts-stop/tts-speaking?
;; need a way to recognize it -- see %dispatch-handle-op).
(define (make-tts-backend available? speak-async save voices . rest)
  (%make-tts-backend available? speak-async save voices
                      (if (pair? rest) (car rest) #f)
                      (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) #f)
                      (if (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest))) (caddr rest) #f)
                      (if (and (pair? rest) (pair? (cdr rest)) (pair? (cddr rest)) (pair? (cdddr rest)))
                          (car (cdddr rest)) #f)))

(define %macos-backend
  (make-tts-backend macos-tts-available? macos-tts-speak-async macos-tts-save macos-tts-voices))

(define %espeak-backend
  (make-tts-backend espeak-tts-available? espeak-tts-speak-async espeak-tts-save espeak-tts-voices))

;; Was a fixed, small, known set with no registration mechanism -- widened
;; to an open registry (matching (curry conditions)'s own open condition-
;; type registry, which this file's original comment explicitly said
;; wasn't needed here) specifically because a backend like (curry tts
;; piper) may not even exist as an importable library at all, depending
;; on how curry was built (-DBUILD_MODULE_PIPER=OFF by default) -- unlike
;; macos-say/espeak-ng, which are plain Scheme + subprocess spawning and
;; always compile in regardless of platform, only ever varying in
;; runtime *availability*. (curry tts) itself can't unconditionally
;; (import (curry tts piper)) the way it does the other two, since that
;; import would fail outright (unbound library) on an ordinary build.
;; Instead, (curry tts piper) is its own separate library a caller who
;; wants piper support imports explicitly, and its own top-level (begin
;; ...) body registers itself here as a side effect of that import --
;; see that file's own header comment for the full rationale.
(define %backend-table
  (list (cons 'macos-say %macos-backend)
        (cons 'espeak-ng %espeak-backend)))

(define (tts-register-backend! sym backend)
  (set! %backend-table (cons (cons sym backend) %backend-table)))

(define (%lookup-backend sym)
  (let ((p (assq sym %backend-table)))
    (if p
        (cdr p)
        (condition-error 'tts-error (list (cons 'backend sym))
          (string-append "tts: unknown backend: " (symbol->string sym))))))

(define (tts-backends) (map car %backend-table))

;; Deliberately returns #f rather than raising for an unrecognized `sym`
;; -- unlike every other backend-taking procedure here, this is a pure
;; query ("is this a thing I could use right now"), and an unknown
;; symbol naturally answers "no" rather than being a usage error.
(define (tts-backend-available? sym)
  (let ((p (assq sym %backend-table)))
    (and p ((tts-backend-available-proc (cdr p))))))

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
;;; Voice/rate/language defaults -- same parameterize-or-permanent-set
;;; shape as current-tts-backend above. current-tts-voice and
;;; current-tts-rate are plain per-call defaults, consulted only when a
;;; call doesn't pass #:voice/#:rate itself. current-tts-language is a
;;; step removed: it never reaches the backend directly (say/espeak-ng
;;; both take a voice name, not a locale) -- instead %resolve-voice below
;;; uses it to pick the first backend voice whose locale (from
;;; tts-voices' own (name . locale) pairs) starts with the requested
;;; language, so a caller can set e.g. "fr" once instead of knowing the
;;; exact voice name a given backend/machine happens to expose. An
;;; explicit #:voice on the call, or a current-tts-voice default, always
;;; wins over current-tts-language.
;;; =========================================================================

(define current-tts-voice (make-parameter #f))
(define current-tts-rate (make-parameter #f))
(define current-tts-language (make-parameter #f))

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

;; (%string-prefix? prefix s) -- manual loop, same reasoning as
;; macos.scm's own %split-colon/%trim: no SRFI-13 string-prefix? in
;; scope here without pulling in a whole extra import for one call site.
(define (%string-prefix? prefix s)
  (let ((plen (string-length prefix)))
    (and (<= plen (string-length s))
         (string=? prefix (substring s 0 plen)))))

;; assoc (src/builtins.c) is fixed at 2 args, no R7RS-optional
;; comparator -- so this is a plain manual loop rather than
;; (assoc lang voices %string-prefix?).
(define (%find-voice-for-language lang voices)
  (cond
    ((null? voices) #f)
    ((%string-prefix? lang (cdar voices)) (caar voices))
    (else (%find-voice-for-language lang (cdr voices)))))

;; (%resolve-voice backend-sym backend voice) -- an explicit #:voice (or
;; current-tts-voice default) always wins and is validated the normal
;; way via %validate-voice; otherwise, if current-tts-language is set,
;; pick the first backend voice whose locale starts with it. Unmatched
;; language fails loudly, the same way an unknown #:voice does, rather
;; than silently falling back to the backend's own default voice. Only
;; one of the two branches ever calls tts-backend-voices-proc, so this
;; costs the same single extra voice-list subprocess as an explicit
;; #:voice does -- a name found via current-tts-language came straight
;; off that same list, so re-checking it through %validate-voice
;; (a second, redundant voice-list fetch) would be pure waste.
(define (%resolve-voice backend-sym backend voice)
  (if voice
      (%validate-voice backend-sym backend voice)
      (let ((lang (current-tts-language)))
        (and lang
             (or (%find-voice-for-language lang ((tts-backend-voices-proc backend)))
                 (condition-error 'tts-error (list (cons 'backend backend-sym))
                   (string-append "tts: no voice for backend " (symbol->string backend-sym)
                                  " matching language: " lang)))))))

;; #:rate gets the same discipline as #:voice: fail cleanly here rather
;; than handing a value neither `say -r`/`espeak-ng -s` (both expect a
;; plain positive integer, words per minute) nor number->string itself
;; can make sense of through to the subprocess.
(define (%validate-rate backend-sym rate)
  (when (and rate (not (and (integer? rate) (exact? rate) (positive? rate))))
    (condition-error 'tts-error (list (cons 'backend backend-sym))
      (string-append "tts: #:rate must be a positive exact integer, got: " (%rate->string rate))))
  rate)

(define (%rate->string rate)
  (let ((out (open-output-string)))
    (write rate out)
    (get-output-string out)))

;;; =========================================================================
;;; Public API
;;; =========================================================================

;; (tts-speak-async text . kwargs) -> process handle, non-blocking.
;; kwargs: #:voice name, #:rate words-per-minute, #:backend symbol --
;; each falls back to current-tts-voice/current-tts-rate/
;; current-tts-backend (and, absent an explicit or defaulted voice, to
;; current-tts-language) when omitted, so a caller who has set those up
;; front can call (tts-speak "...") with just the text.
(define (tts-speak-async text . kwargs)
  (let* ((backend-sym (%kwarg kwargs '#:backend (current-tts-backend)))
         (backend (%lookup-backend backend-sym))
         (voice (%resolve-voice backend-sym backend (%kwarg kwargs '#:voice (current-tts-voice))))
         (rate (%validate-rate backend-sym (%kwarg kwargs '#:rate (current-tts-rate)))))
    ((tts-backend-speak-async backend) text voice rate)))

;; (tts-speak text . kwargs) -- blocks until the utterance finishes.
;; Was calling process-wait directly here -- a leftover from before the
;; backend registry widened past a fixed process-handle-only world (see
;; %dispatch-handle-op above); that made every non-process backend's
;; handle (e.g. piper's, a background-thread handle, not a (curry
;; posix) process) fail tts-speak specifically with "not a process
;; handle", even though tts-wait itself was already correctly widened
;; to dispatch on the handle's actual type. Found via a live run
;; against the piper backend, not caught by tts_tests.scm since it
;; only exercises macos-say/espeak-ng (real process handles, so the
;; bug was invisible there).
(define (tts-speak text . kwargs)
  (tts-wait (apply tts-speak-async text kwargs))
  (values))

;; (tts-save text path . kwargs) -- render to `path`, no sound played.
(define (tts-save text path . kwargs)
  (let* ((backend-sym (%kwarg kwargs '#:backend (current-tts-backend)))
         (backend (%lookup-backend backend-sym))
         (voice (%resolve-voice backend-sym backend (%kwarg kwargs '#:voice (current-tts-voice))))
         (rate (%validate-rate backend-sym (%kwarg kwargs '#:rate (current-tts-rate)))))
    ((tts-backend-save backend) text path voice rate)))

;; (tts-voices . kwargs) -> list of (name . locale) pairs for the active
;; (or #:backend-forced) backend.
(define (tts-voices . kwargs)
  ((tts-backend-voices-proc (%lookup-backend (%kwarg kwargs '#:backend (current-tts-backend))))))

;; Async-handle lifecycle -- tts-speak-async's return value is an
;; ordinary (curry posix) process handle, so these are direct
;; pass-throughs; kept as tts-* names so callers never need to know
;; that's what it is (a future backend need not be subprocess-based).
;; Fast path first: an ordinary (curry posix) process handle (macos-say/
;; espeak-ng's own speak-async return value) is the overwhelmingly common
;; case, checked with zero registry lookup -- process-handle? is a plain
;; type predicate, not a search. Only when that's NOT what `h` is do we
;; walk %backend-table looking for the (exactly one, in practice) backend
;; whose own handle? proc recognizes it, and use THAT backend's wait/
;; stop/speaking? instead. An unrecognized handle falls through to
;; process-wait/process-kill/process-alive? anyway, which will raise its
;; own clear type error -- no silent no-op.
(define (%dispatch-handle-op h process-op backend-op-proc)
  (if (process-handle? h)
      (process-op h)
      (let loop ((table %backend-table))
        (cond
          ((null? table) (process-op h)) ; unrecognized -- let process-op's own error fire
          ((let ((handle-p (tts-backend-handle-proc (cdar table))))
             (and handle-p (handle-p h)))
           ((backend-op-proc (cdar table)) h))
          (else (loop (cdr table)))))))

(define (tts-wait h) (%dispatch-handle-op h process-wait tts-backend-wait-proc))
(define (tts-stop h) (%dispatch-handle-op h process-kill tts-backend-stop-proc))
(define (tts-speaking? h) (%dispatch-handle-op h process-alive? tts-backend-speaking-proc))

  )) ;; end begin, define-library
