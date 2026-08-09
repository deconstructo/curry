;;; (curry dot-locking) — advisory file locking via the dot-locking
;;; scheme, pure Scheme, built on (curry posix) + (srfi 18).
;;;
;;; Ported from the API documented by the CHICKEN Scheme "dot-locking"
;;; egg (https://wiki.call-cc.org/eggref/5/dot-locking; algorithm
;;; originally by Olin Shivers, CHICKEN port by felix winkelmann,
;;; BSD-licensed) — this module reimplements the documented behavior
;;; from scratch against curry's own (curry posix) primitives, not a
;;; port of that egg's actual source.
;;;
;;; The lock for a file named `file-name` is represented by a second
;;; file, `file-name.lock`. Obtaining the lock is done via the classic
;;; "unique temp file, then link(2) it into place" technique: a
;;; process-and-call-uniquely-named temp file is created first, then
;;; `link(2)`'d to the lock path. `link(2)` fails if the target already
;;; exists, and — unlike `open(2)` with `O_EXCL`, historically unsafe on
;;; some NFS implementations — this check-and-create is atomic even
;;; across NFS clients, which is the entire reason this scheme exists
;;; instead of a plain exclusive-create. Whether or not the link
;;; succeeds, the temp file is deleted immediately afterward; only the
;;; lock file itself (`file-name.lock`) persists while the lock is held.
;;;
;;; (curry posix) has no structured errno introspection (see that
;;; module's own "Deliberately not implemented" notes) — every failure
;;; surfaces as an ordinary condition carrying `strerror()` text. This
;;; module distinguishes "the lock is already held by someone else"
;;; (`create-hard-link` failing because the target already exists) from
;;; any other failure (a permission error, a full disk, ...) by matching
;;; that text, which is the only signal available; an unrecognized
;;; failure is re-raised rather than silently treated as "lock busy".

(define-library (curry dot-locking)
  (import (scheme base) (scheme file) (curry posix) (curry sync) (srfi 18))
  (export obtain-dot-lock release-dot-lock break-dot-lock with-dot-lock* with-dot-lock)
  (begin

(define (%lock-path file-name) (string-append file-name ".lock"))

(define (%string-contains? haystack needle)
  (let ((hlen (string-length haystack)) (nlen (string-length needle)))
    (let loop ((i 0))
      (cond ((> (+ i nlen) hlen) #f)
            ((string=? (substring haystack i (+ i nlen)) needle) #t)
            (else (loop (+ i 1)))))))

;; True iff `e` (a condition caught from a (curry posix) call) is the
;; "target already exists" failure specifically, as opposed to any other
;; reason the call could have failed.
(define (%already-exists-error? e)
  (and (error-object? e) (%string-contains? (error-object-message e) "File exists")))

;; curry actors are real OS threads sharing one address space (see
;; src/actors.c), so obtain-dot-lock can genuinely be called
;; concurrently, from multiple actors, within a single process — an
;; unsynchronized (set! %tmp-counter ...) is a real data race there,
;; not just a theoretical one: two actors could read the same counter
;; value, compute the identical tmp name, and then race each other's
;; call-with-output-file/delete-file/create-hard-link against that one
;; shared path, producing spurious ENOENT-style errors that have
;; nothing to do with the lock actually being held. %tmp-counter-mutex
;; makes the read-increment-write atomic across actors.
(define %tmp-counter 0)
(define %tmp-counter-mutex (make-mutex))

(define (%next-tmp-counter!)
  (with-mutex %tmp-counter-mutex
    (lambda () (set! %tmp-counter (+ %tmp-counter 1)) %tmp-counter)))

;; A name unique to this process and this call, in the same directory
;; as the lock itself (link(2) requires both paths be on the same
;; filesystem, and the target's own directory is guaranteed to be).
(define (%unique-tmp-name file-name)
  (string-append file-name ".lock." (machine-name) "." (number->string (pid)) "." (number->string (%next-tmp-counter!))))

;; One lock-acquisition attempt. Returns #t on success, #f if the lock
;; is already held (a normal, expected outcome — not an error); any
;; other failure is re-raised.
(define (%try-obtain! file-name)
  (let ((tmp (%unique-tmp-name file-name)))
    (call-with-output-file tmp (lambda (p) (display (pid) p)))
    (let ((acquired?
            (guard (e ((%already-exists-error? e) #f))
              (create-hard-link tmp (%lock-path file-name))
              #t)))
      (guard (e (#t #t)) (delete-file tmp)) ; always clean up our own temp file
      acquired?)))

;; (obtain-dot-lock file-name)
;; (obtain-dot-lock file-name interval)
;; (obtain-dot-lock file-name interval retry-number)
;; (obtain-dot-lock file-name interval retry-number stale-time)
;;
;; Tries to obtain the lock for file-name. If the file is already
;; locked, sleeps for `interval` seconds (default 1) before retrying.
;; If the lock cannot be obtained after `retry-number` attempts, returns
;; #f; `retry-number` defaults to #f, an infinite number of retries.
;;
;; If `stale-time` is non-#f (default 300), it's the minimum age (in
;; seconds, by the lock file's own mtime) a lock may have before it's
;; considered stale; a stale lock is broken (deleted) and immediately
;; retried. If obtaining the lock succeeds after breaking one this way,
;; returns 'broken rather than #t. Pass stale-time #f to never consider
;; a lock stale. It's possible for a lock to be broken here but for the
;; immediate retry to still fail (another process won the race) — use
;; break-dot-lock directly instead if that case needs handling specially.
(define (obtain-dot-lock file-name . opts)
  (let* ((interval     (if (pair? opts) (car opts) 1))
         (opts         (if (pair? opts) (cdr opts) '()))
         (retry-number (if (pair? opts) (car opts) #f))
         (opts         (if (pair? opts) (cdr opts) '()))
         (stale-time   (if (pair? opts) (car opts) 300)))
    (let loop ((retries-left retry-number) (broke-one? #f))
      (cond
        ((%try-obtain! file-name) (if broke-one? 'broken #t))
        ((and stale-time (%stale? file-name stale-time) (%try-break! file-name))
         (loop retries-left #t))
        ((and retry-number (<= retries-left 0)) #f)
        (else
         (thread-sleep! interval)
         (loop (and retry-number (- retries-left 1)) broke-one?))))))

(define (%stale? file-name stale-time)
  (guard (e (#t #f)) ; the lock may have been released between our failed
                      ; obtain attempt and this check -- treat that as
                      ; "not stale" (there's nothing to break) rather
                      ; than raising on the now-missing lock file
    (> (- (current-second) (file-info:mtime (file-info (%lock-path file-name)))) stale-time)))

(define (%try-break! file-name) (guard (e (#t #f)) (delete-file (%lock-path file-name)) #t))

;; (release-dot-lock file-name) -> #t on success, #f otherwise.
;; Also usable to break the lock for file-name (see break-dot-lock).
(define (release-dot-lock file-name) (%try-break! file-name))

;; (break-dot-lock file-name) -> breaks the lock for file-name if one
;; exists (#t), or #f if there was none to break. Breaking a lock does
;; not imply a subsequent obtain-dot-lock will succeed, since another
;; party may acquire it in between.
(define (break-dot-lock file-name) (%try-break! file-name))

;; (with-dot-lock* file-name thunk) -> obtains the lock, calls (thunk),
;; and releases the lock when thunk returns -- or on a non-local exit
;; (an escaping continuation invocation, or a raised condition), via
;; dynamic-wind. Returns thunk's own values on a normal return. Raises
;; if the lock can't be obtained at all.
(define (with-dot-lock* file-name thunk)
  (unless (obtain-dot-lock file-name)
    (error "with-dot-lock*: could not obtain lock" file-name))
  (dynamic-wind
    (lambda () #t)
    thunk
    (lambda () (release-dot-lock file-name))))

;; (with-dot-lock file-name body ...) -- syntactic sugar for
;; (with-dot-lock* file-name (lambda () body ...)).
(define-syntax with-dot-lock
  (syntax-rules ()
    ((_ file-name body ...) (with-dot-lock* file-name (lambda () body ...)))))

  )) ;; end begin, define-library
