;;; (curry tts macos) — macOS speech-synthesis backend for (curry tts), via
;;; the built-in `say` command-line tool. No Objective-C, no framework
;;; linking, no new C module: `say` is a plain argv-driven executable macOS
;;; ships with, and (curry posix)'s process-run/process-start already give a
;;; real, shell-free subprocess API (backed by posix_spawn) — text-to-speak,
;;; voice names, and file paths all pass through as literal argv elements,
;;; never through a shell, so there is no injection surface the way there
;;; would be building a "say ..." string for (system ...).
;;;
;;; This module is written to satisfy (curry tts)'s <tts-backend> protocol
;;; (see lib/curry/modules/curry/tts.scm) — its own procedures below are
;;; plain, independently usable `say` bindings, but their real purpose is
;;; being wired into (curry tts)'s backend table as 'macos-say.

(define-library (curry tts macos)
  (import (scheme base) (curry posix) (curry regex) (curry conditions))
  (export
    macos-tts-available? macos-tts-speak-async macos-tts-save macos-tts-voices)
  (begin

(define-condition tts-error (error) #:fields (backend))

;; (%on-path? name) -- true if `name` resolves to an executable file
;; somewhere on PATH, without spawning anything. Split PATH on ":" and
;; check each directory directly, the same manual-loop style (curry
;; postgres)/(curry mariadb) already use for their own small string
;; utilities rather than assuming a stdlib helper exists.
(define (%split-colon s)
  (let loop ((start 0) (i 0) (acc '()))
    (cond
      ((= i (string-length s)) (reverse (cons (substring s start i) acc)))
      ((char=? (string-ref s i) #\:) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
      (else (loop start (+ i 1) acc)))))

(define (%on-path? name)
  (let ((path (get-environment-variable "PATH")))
    (and path
         (any (lambda (dir) (file-exists? (string-append dir "/" name)))
              (%split-colon path)))))

;; `any` is SRFI-1, not a core builtin -- confirmed unbound outside an
;; explicit (srfi 1) import, which this library doesn't otherwise need
;; just for one call site.
(define (any pred lst) (and (pair? lst) (or (pred (car lst)) (any pred (cdr lst)))))

(define (macos-tts-available?) (and (%on-path? "say") #t))

(define (%say-flags voice rate)
  (append (if voice (list "-v" voice) '())
          (if rate (list "-r" (number->string rate)) '())))

;; (macos-tts-speak-async text voice rate) -> process handle. voice/rate
;; are #f or already-validated by (curry tts) against macos-tts-voices'
;; own output before this is ever called.
(define (macos-tts-speak-async text voice rate)
  (process-start "say" (append (%say-flags voice rate) (list text))))

;; (macos-tts-save text path voice rate) -- blocks until the file is
;; written; raises 'tts-error on a non-zero `say` exit.
(define (macos-tts-save text path voice rate)
  (call-with-values
    (lambda () (process-run "say" (append (%say-flags voice rate) (list "-o" path text))))
    (lambda (exit-code stdout stderr)
      (unless (= exit-code 0)
        (condition-error 'tts-error (list (cons 'backend 'macos-say))
          (string-append "tts: say exited " (number->string exit-code) ": " stderr))))))

;; (macos-tts-voices) -> list of (name . locale) pairs, e.g.
;; ("Moira" . "en_IE"). `say -v ?` prints one voice per line:
;;   Albert              en_US    # Hello! My name is Albert.
;; Locate the locale token (always exactly "xx_XX", two lowercase letters,
;; underscore, two uppercase letters) via regex rather than splitting on
;; whitespace -- some voice names contain internal spaces (e.g. localized
;; names with diacritics or parenthesized qualifiers), so "first whitespace
;; token = name" would be wrong; "everything before the locale token,
;; trimmed" is not.
(define %locale-rx (regex-compile "[a-z][a-z]_[A-Z][A-Z]"))

;; char-whitespace?, not just #\space -- matching (curry tts espeak)'s
;; own tokenizer, which already uses char-whitespace? for the same
;; reason: `say -v ?`'s column padding is space-padded in every sample
;; observed so far, but there's no guarantee that holds across every
;; locale/macOS build, and a stray tab would otherwise silently end up
;; inside the extracted voice name, breaking #:voice lookups.
(define (%trim s)
  (let* ((len (string-length s))
         (start (let loop ((i 0)) (if (and (< i len) (char-whitespace? (string-ref s i))) (loop (+ i 1)) i)))
         (end (let loop ((i len)) (if (and (> i start) (char-whitespace? (string-ref s (- i 1)))) (loop (- i 1)) i))))
    (substring s start end)))

(define (%parse-voice-line line)
  (let ((m (regex-match %locale-rx line)))
    (and m
         (let ((start (car (car m))) (end (cdr (car m))))
           (cons (%trim (substring line 0 start)) (substring line start end))))))

(define (macos-tts-voices)
  (call-with-values
    (lambda () (process-run "say" (list "-v" "?")))
    (lambda (exit-code stdout stderr)
      (unless (= exit-code 0)
        (condition-error 'tts-error (list (cons 'backend 'macos-say))
          (string-append "tts: say -v ? exited " (number->string exit-code) ": " stderr)))
      (let ((in (open-input-string stdout)))
        (let loop ((acc '()))
          (let ((line (read-line in)))
            (if (eof-object? line)
                (reverse acc)
                (let ((v (%parse-voice-line line)))
                  (loop (if v (cons v acc) acc))))))))))

  )) ;; end begin, define-library
