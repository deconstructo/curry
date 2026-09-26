;;; srfi_120_tests.scm — (srfi 120) Timer APIs: make-timer, timer?,
;;; timer-cancel!, timer-schedule!, timer-reschedule!, timer-task-remove!,
;;; timer-task-exists?, make-timer-delta, timer-delta?.
;;;
;;; Timers run their scheduler loop on a real background thread, so this
;;; suite is inherently timing-based -- every assertion below gives the
;;; scheduler thread a generous margin (tens of ms of slack around
;;; sub-100ms deadlines) via a small polling helper (%wait-until) rather
;;; than a single fixed sleep, to keep the suite reliable under load
;;; without being flaky on a fast, idle machine either.

(import (scheme base) (scheme write) (srfi 120) (srfi 18))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;; Polls `pred` (a zero-arg thunk) every 5ms until it returns non-#f or
;; `max-ms` elapses; returns the last value of `pred`.
(define (%wait-until pred max-ms)
  (let loop ((waited 0))
    (let ((v (pred)))
      (cond
        (v v)
        ((>= waited max-ms) #f)
        (else (thread-sleep! 0.005) (loop (+ waited 5)))))))

;;; ---- timer-delta ----

(check "make-timer-delta + timer-delta? true" (timer-delta? (make-timer-delta 5 'ms)) #t)
(check "timer-delta? false for plain integer" (timer-delta? 5) #f)
(check "make-timer-delta rejects unknown unit"
       (guard (e (#t 'caught)) (make-timer-delta 5 'fortnight))
       'caught)
(check "make-timer-delta accepts all required units"
       (map (lambda (u) (timer-delta? (make-timer-delta 1 u))) '(h m s ms us ns))
       '(#t #t #t #t #t #t))

;;; ---- timer? ----

(define t0 (make-timer))
(check "timer? true for a real timer" (timer? t0) #t)
(check "timer? false for a non-timer" (timer? 5) #f)
(timer-cancel! t0)

;;; ---- one-shot scheduling ----

(let* ((t (make-timer))
       (fired (list #f))
       (id (timer-schedule! t (lambda () (set-car! fired #t)) 30)))
  (check "one-shot task fires" (%wait-until (lambda () (car fired)) 500) #t)
  (check "one-shot task id no longer exists after firing"
         (%wait-until (lambda () (not (timer-task-exists? t id))) 200)
         #t)
  (timer-cancel! t))

;; timer-schedule! also accepts a timer-delta for `when`.
(let* ((t (make-timer))
       (fired (list #f)))
  (timer-schedule! t (lambda () (set-car! fired #t)) (make-timer-delta 30 'ms))
  (check "one-shot task fires via timer-delta `when`"
         (%wait-until (lambda () (car fired)) 500) #t)
  (timer-cancel! t))

;;; ---- periodic scheduling ----

(let* ((t (make-timer))
       (count 0)
       (id #f))
  (set! id (timer-schedule! t
             (lambda ()
               (set! count (+ count 1))
               (if (= count 3) (timer-task-remove! t id)))
             20 20))
  (check "periodic task fires (at least) 3 times, then is removed"
         (%wait-until (lambda () (and (>= count 3) (not (timer-task-exists? t id)))) 1000)
         #t)
  (let ((seen-at-stop count))
    (thread-sleep! 0.1)
    (check "removed periodic task does not fire again" count seen-at-stop))
  (timer-cancel! t))

;;; ---- timer-task-exists? / timer-task-remove! ----

(let* ((t (make-timer))
       (id (timer-schedule! t (lambda () #f) 10000)))
  (check "timer-task-exists? true for a pending task" (timer-task-exists? t id) #t)
  (check "timer-task-exists? false for an unknown id" (timer-task-exists? t 999999) #f)
  (check "timer-task-remove! returns #t and removes" (timer-task-remove! t id) #t)
  (check "task gone after removal" (timer-task-exists? t id) #f)
  (check "timer-task-remove! returns #f for an already-removed id" (timer-task-remove! t id) #f)
  (timer-cancel! t))

;;; ---- timer-reschedule! ----

(let* ((t (make-timer))
       (fired (list #f))
       (id (timer-schedule! t (lambda () #f) 10000)))
  (check "timer-reschedule! returns the same id" (timer-reschedule! t id 20) id)
  (timer-task-remove! t id)
  (check "timer-reschedule! errors on an unknown id"
         (guard (e (#t 'caught)) (timer-reschedule! t 424242 10))
         'caught)
  (timer-cancel! t))

;; Rescheduling a one-shot task after it has already executed is an
;; error (its id is no longer in the table -- see this library's own
;; header comment on why "already executed" and "not found" collapse
;; into the same check).
(let* ((t (make-timer))
       (id (timer-schedule! t (lambda () #f) 20)))
  (%wait-until (lambda () (not (timer-task-exists? t id))) 500)
  (check "timer-reschedule! errors on an already-executed one-shot task"
         (guard (e (#t 'caught)) (timer-reschedule! t id 10))
         'caught)
  (timer-cancel! t))

;; Setting period to 0 cancels the periodic aspect of a task.
(let* ((t (make-timer))
       (count 0)
       (id #f))
  (set! id (timer-schedule! t (lambda () (set! count (+ count 1))) 20 20))
  (%wait-until (lambda () (>= count 1)) 500)
  (timer-reschedule! t id 100000 0)
  (let ((seen-after-cancel count))
    (thread-sleep! 0.1)
    (check "period 0 stops further periodic firing" count seen-after-cancel))
  (timer-cancel! t))

;;; ---- error handling ----

;; With an error-handler: the handler is called, and the timer keeps running.
(let* ((caught (list #f))
       (t (make-timer (lambda (e) (set-car! caught (error-object-message e)))))
       (ran-after (list #f)))
  (timer-schedule! t (lambda () (error "task blew up")) 20)
  (check "error-handler is invoked with the condition"
         (%wait-until (lambda () (car caught)) 500)
         "task blew up")
  (timer-schedule! t (lambda () (set-car! ran-after #t)) 20)
  (check "timer keeps running after a handled error"
         (%wait-until (lambda () (car ran-after)) 500)
         #t)
  (timer-cancel! t))

;; Without an error-handler: the timer stops, and timer-cancel! re-raises
;; the preserved condition.
(let* ((t (make-timer)))
  (timer-schedule! t (lambda () (error "unhandled failure")) 20)
  ;; Poll until the timer has actually stopped -- i.e. until a probe
  ;; schedule attempt starts raising -- rather than a fixed sleep.
  (%wait-until
    (lambda () (guard (e (#t #t)) (timer-schedule! t (lambda () #f) 5) #f))
    500)
  (check "timer-schedule! on a stopped (errored) timer raises"
         (guard (e (#t 'caught)) (timer-schedule! t (lambda () #f) 5))
         'caught)
  (check "timer-cancel! re-raises the preserved condition"
         (guard (e (#t (error-object-message e))) (timer-cancel! t))
         "unhandled failure")
  ;; A second timer-cancel! on an already-cancelled timer must not
  ;; re-raise again (the condition was already delivered once).
  (check "second timer-cancel! is a harmless no-op" (timer-cancel! t) (if #f #f)))

;;; ---- cancelled timer rejects further scheduling ----

(let ((t (make-timer)))
  (timer-cancel! t)
  (check "timer-schedule! on a cancelled timer raises"
         (guard (e (#t 'caught)) (timer-schedule! t (lambda () #f) 10))
         'caught))

;;; ---- Summary ----

(newline)
(display "srfi-120 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
