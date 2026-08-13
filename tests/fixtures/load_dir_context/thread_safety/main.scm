;;; Regression fixture: load_dir_stack/load_dir_depth (runtime.c) must be
;;; per-thread (_Thread_local), not a plain process-wide static -- curry's
;;; actors each run in their own detached POSIX thread, and any of them
;;; can call (load ...) concurrently. A non-thread-local stack here was a
;;; genuine data race, verified via ThreadSanitizer during review: one
;;; actor's load_dir_release() free()ing a directory string could race a
;;; concurrent read of that same slot in another actor's scm_load(), a
;;; real use-after-free, and in practice let one actor's relative load
;;; resolve against a different (or freed) actor's directory context.
;;;
;;; This spawns 20 actors, half repeatedly (load ...)-ing dirA/inner.scm
;;; (which evaluates to 1), half dirB/inner.scm (which evaluates to 2),
;;; and has each actor verify on every single iteration that (load ...)
;;; returned the value belonging to *its own* assigned directory, not
;;; the other actor's -- the actual cross-thread confusion this bug
;;; caused. Deliberately avoids (define ...) inside the loaded files: a
;;; define would land in the shared GLOBAL_ENV (per prim_load), which is
;;; itself a separate, unrelated concurrent-write hazard this test isn't
;;; about -- using (load ...)'s own return value (the last form's value)
;;; keeps the test isolated to directory-context resolution only.
;;;
;;; Synchronization follows this codebase's own established pattern for
;;; main-thread <-> actor coordination (see actors_tests.scm): a
;;; (curry sync) semaphore posted once per actor, plus a shared mutable
;;; vector each actor writes its own slot of -- not send!/receive, which
;;; are for actor-to-actor mailbox messaging, not the main thread.

(import (curry sync))

(define n-actors 20)
(define iterations 30)
(define results (make-vector n-actors #f))
(define done (make-semaphore 0))

(define (spawn-loader idx dir expected)
  (spawn (lambda ()
    (guard (e (#t
               (display "actor ") (display idx) (display " raised: ") (display e) (newline)
               (vector-set! results idx #f)
               (sem-post! done)))
      (let loop ((n 0) (ok #t))
        (if (< n iterations)
            (loop (+ n 1) (and ok (equal? (load (string-append dir "/inner.scm")) expected)))
            (begin
              (vector-set! results idx ok)
              (sem-post! done))))))))

(let loop ((i 0))
  (when (< i n-actors)
    (if (even? i)
        (spawn-loader i "dirA" 1)
        (spawn-loader i "dirB" 2))
    (loop (+ i 1))))

(let loop ((n 0))
  (when (< n n-actors)
    (sem-wait! done)
    (loop (+ n 1))))

(define all-ok
  (let loop ((i 0))
    (cond
      ((= i n-actors) #t)
      ((not (vector-ref results i))
       (display "actor ") (display i) (display " got a wrong value at some iteration") (newline)
       #f)
      (else (loop (+ i 1))))))

(if all-ok
    (begin (display "PASS: 20 concurrent actors each resolved their own directory's (load ...) correctly, every iteration") (newline) (exit 0))
    (begin (display "FAIL: cross-thread directory-context confusion") (newline) (exit 1)))
