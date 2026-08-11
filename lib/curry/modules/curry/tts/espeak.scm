;;; (curry tts espeak) — Linux speech-synthesis backend for (curry tts),
;;; via espeak-ng (falling back to the older `espeak` binary name, which
;;; is what some distros still ship). Same rationale as (curry tts macos)'s
;;; own header comment: a plain argv-driven CLI tool run through (curry
;;; posix)'s process-run/process-start, no new C code, no shell-injection
;;; surface (posix_spawn directly, never /bin/sh).
;;;
;;; This module is written to satisfy (curry tts)'s <tts-backend> protocol
;;; (see lib/curry/modules/curry/tts.scm) — its own procedures below are
;;; plain, independently usable espeak-ng bindings, but their real purpose
;;; is being wired into (curry tts)'s backend table as 'espeak-ng.

(define-library (curry tts espeak)
  (import (scheme base) (curry posix) (curry conditions))
  (export
    espeak-tts-available? espeak-tts-speak-async espeak-tts-save espeak-tts-voices)
  (begin

(define-condition tts-error (error) #:fields (backend))

;; Same manual PATH-splitting/lookup as (curry tts macos)'s own %on-path?
;; -- duplicated rather than shared, matching (curry postgres)/(curry
;; mariadb)'s own precedent of small, self-contained sibling modules
;; each carrying their own tiny utilities instead of a shared private
;; module for a handful of lines.
(define (%split-colon s)
  (let loop ((start 0) (i 0) (acc '()))
    (cond
      ((= i (string-length s)) (reverse (cons (substring s start i) acc)))
      ((char=? (string-ref s i) #\:) (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
      (else (loop start (+ i 1) acc)))))

(define (any pred lst) (and (pair? lst) (or (pred (car lst)) (any pred (cdr lst)))))

(define (%on-path? name)
  (let ((path (get-environment-variable "PATH")))
    (and path
         (any (lambda (dir) (file-exists? (string-append dir "/" name)))
              (%split-colon path)))))

;; espeak-ng is the modern name; espeak is the historical one some
;; distros still ship exclusively. Resolved once per call rather than
;; cached, since the whole point of #:backend is letting a caller
;; re-check availability at any time without a stale answer.
(define (%resolve-executable)
  (cond ((%on-path? "espeak-ng") "espeak-ng")
        ((%on-path? "espeak") "espeak")
        (else #f)))

(define (espeak-tts-available?) (and (%resolve-executable) #t))

(define (%require-executable)
  (or (%resolve-executable)
      (condition-error 'tts-error (list (cons 'backend 'espeak-ng))
        "tts: neither espeak-ng nor espeak found on PATH")))

(define (%espeak-flags voice rate)
  (append (if voice (list "-v" voice) '())
          (if rate (list "-s" (number->string rate)) '())))

;; (espeak-tts-speak-async text voice rate) -> process handle. voice/rate
;; are #f or already-validated by (curry tts) against
;; espeak-tts-voices' own output before this is ever called.
(define (espeak-tts-speak-async text voice rate)
  (process-start (%require-executable) (append (%espeak-flags voice rate) (list text))))

;; (espeak-tts-save text path voice rate) -- blocks until the file is
;; written; raises 'tts-error on a non-zero exit.
(define (espeak-tts-save text path voice rate)
  (call-with-values
    (lambda () (process-run (%require-executable) (append (%espeak-flags voice rate) (list "-w" path text))))
    (lambda (exit-code stdout stderr)
      (unless (= exit-code 0)
        (condition-error 'tts-error (list (cons 'backend 'espeak-ng))
          (string-append "tts: espeak exited " (number->string exit-code) ": " stderr))))))

;; (espeak-tts-voices) -> list of (name . language) pairs. `--voices`
;; prints a header line then whitespace-separated columns:
;;   Pty Language       Age/Gender VoiceName          File   Other Languages
;;    2  en-us           --/M      English_(America)  ...
;; VoiceName never contains a space (multi-word names use underscores),
;; so a plain whitespace split is safe here -- unlike macOS's `say -v ?`,
;; which does need the regex-based locale-token approach.
(define (%whitespace-fields line)
  (let ((len (string-length line)))
    (let loop ((i 0) (start #f) (acc '()))
      (cond
        ((= i len) (reverse (if start (cons (substring line start i) acc) acc)))
        ((char-whitespace? (string-ref line i))
         (loop (+ i 1) #f (if start (cons (substring line start i) acc) acc)))
        (else (loop (+ i 1) (or start i) acc))))))

(define (espeak-tts-voices)
  (call-with-values
    (lambda () (process-run (%require-executable) (list "--voices")))
    (lambda (exit-code stdout stderr)
      (unless (= exit-code 0)
        (condition-error 'tts-error (list (cons 'backend 'espeak-ng))
          (string-append "tts: espeak --voices exited " (number->string exit-code) ": " stderr)))
      (let ((in (open-input-string stdout)))
        (read-line in) ;; discard the header line
        (let loop ((acc '()))
          (let ((line (read-line in)))
            (if (eof-object? line)
                (reverse acc)
                (let ((fields (%whitespace-fields line)))
                  (loop (if (>= (length fields) 4)
                            (cons (cons (list-ref fields 3) (list-ref fields 1)) acc)
                            acc))))))))))

  )) ;; end begin, define-library
