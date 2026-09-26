;;; SRFI-120: Timer APIs.
;;;
;;; https://srfi.schemers.org/srfi-120/ -- a portable interface to
;;; scheduled/periodic background tasks, for implementations (like
;;; curry) that have real threads but no built-in timer facility.
;;;
;;; Built directly on (curry sync)'s mutex/condition-variable (NOT
;;; SRFI-18's own re-exported wrappers around the same primitives --
;;; see the note on thread creation below) and (srfi s69 hash-tables)
;;; for the task table; the scheduler itself is one dedicated thread
;;; per timer, sleeping via cond-wait-timeout! (a real, interruptible
;;; timed wait -- not a poll loop) until either its own deadline
;;; arrives or a mutation (schedule/reschedule/remove/cancel) signals
;;; it to re-check sooner. All scheduling arithmetic is done in exact
;;; milliseconds against current-jiffy (CLOCK_MONOTONIC-backed, see
;;; src/builtins.c) rather than wall-clock time, so a system clock
;;; adjustment can't cause a task to fire early/late or a periodic
;;; task's cadence to drift.
;;;
;;; Thread creation: curry's actor system (spawn) is the only real-
;;; thread primitive; SRFI-18's own thread-start! is a thin wrapper
;;; over it (see lib/curry/modules/srfi/s18/multithreading.scm's own
;;; header comment). This library spawns its scheduler thread directly
;;; via spawn rather than going through SRFI-18, to avoid depending on
;;; (srfi 18) at all for a library that only actually needs "a
;;; background thread," not SRFI-18's own thread-object API surface
;;; (join, specific-value slots, etc.) -- (curry sync)'s mutex/condvar
;;; already give everything the scheduler itself needs.
;;;
;;; Task ids are plain incrementing fixnums, one counter per timer
;;; (the SRFI only requires "a readable datum, typically an integer" --
;;; unique per timer, not globally).
;;;
;;; Error handling (make-timer's optional error-handler, and the
;;; preserved-error timer-cancel! re-raises): a task's thunk runs
;;; wrapped in a guard, never as an unprotected call -- an uncaught
;;; Scheme condition escaping the scheduler thread's own top-level
;;; would otherwise silently kill that thread with no way for the rest
;;; of the program to ever find out. With no error-handler, the SRFI
;;; says the timer "stops" and the error is "preserved" for
;;; timer-cancel! to re-raise; this implementation stops the timer by
;;; setting its own cancelled flag (so the scheduler loop exits on its
;;; next iteration, same as an explicit timer-cancel!) rather than
;;; adding a second, different "halted" state -- from the outside,
;;; a timer that errored-and-stopped is indistinguishable from one a
;;; caller could have cancelled at that exact moment, which matches
;;; the SRFI's own description closely enough that a separate state
;;; would only add complexity without changing observable behavior
;;; (timer-schedule!/-reschedule!/-task-remove! all already need to
;;; treat "already stopped" as an error regardless of *why* it
;;; stopped).
;;;
;;; "Tasks can cancel/reschedule other tasks but not themselves" vs.
;;; "self-rescheduled tasks continue current execution then reschedule"
;;; (SRFI text, describing periodic tasks) reads as mildly contradictory
;;; taken together -- resolved here the same way as this codebase's own
;;; SRFI-273 implementation resolves a similarly-worded corner: a
;;; periodic task's own id is re-inserted into the task table (with its
;;; NEXT occurrence's timing already computed) immediately before that
;;; occurrence's thunk runs, not after -- so the id remains valid and
;;; timer-reschedule!/timer-task-remove! calls against it (whether from
;;; that task's own thunk or another task) always affect the upcoming
;;; occurrence, never the one currently executing, which always runs to
;;; completion regardless. A ONE-SHOT task's id is removed before its
;;; thunk runs and never reinserted, so "task already executed" (one of
;;; the SRFI's own named error conditions for timer-reschedule!) falls
;;; naturally out of an ordinary "id not found" check -- no separate
;;; "already ran" bookkeeping needed.

(define-library (srfi s120 timer)
  (import (scheme base))
  (import (curry sync))
  (import (srfi s69 hash-tables))
  (export
    make-timer timer? timer-cancel!
    timer-schedule! timer-reschedule! timer-task-remove! timer-task-exists?
    make-timer-delta timer-delta?
    ;; Internal helpers an exported macro's own expansion reaches --
    ;; N/A here (no exported macros, only procedures/records), kept
    ;; unexported deliberately.
    )
  (begin

    ;; ── Timer delta objects ──────────────────────────────────────────────────

    (define-record-type <timer-delta>
      (%make-timer-delta n unit)
      timer-delta?
      (n timer-delta-n)
      (unit timer-delta-unit))

    ;; Validated at construction time, not lazily when first used to
    ;; schedule something -- an unknown unit should fail at the call
    ;; site that actually got it wrong, not silently propagate into a
    ;; later timer-schedule! call with a confusing error origin.
    (define (make-timer-delta n unit)
      (case unit
        ((h m s ms us ns) (%make-timer-delta n unit))
        (else (error "make-timer-delta: unit must be one of h m s ms us ns" unit))))

    (define (%timer-delta->ms d)
      (let ((n (timer-delta-n d)) (u (timer-delta-unit d)))
        (case u
          ((h)  (* n 3600000))
          ((m)  (* n 60000))
          ((s)  (* n 1000))
          ((ms) n)
          ((us) (/ n 1000))
          ((ns) (/ n 1000000)))))

    ;; Accepts either a timer-delta or a non-negative integer (already
    ;; milliseconds, per the SRFI text) and normalizes to a single
    ;; exact millisecond quantity for the scheduler's own internal math.
    ;; The non-negative/exactness check is applied uniformly to the
    ;; FINAL ms value, not just to a bare integer `x` -- a timer-delta
    ;; with a negative or inexact `n` would otherwise sail through
    ;; unchecked (a real gap found by review: make-timer-delta's own
    ;; header comment claims validation happens "at construction time",
    ;; but only ever checked `unit`, never `n`).
    (define (%->ms x who)
      (let ((ms (if (timer-delta? x) (%timer-delta->ms x) x)))
        (if (and (real? ms) (exact? ms) (>= ms 0))
            ms
            (error (string-append who ": when/period must be a non-negative "
                                   "exact integer (milliseconds) or a timer-delta "
                                   "with a non-negative exact n")
                   x))))

    ;; A period of 0 means "not periodic" (SRFI text: "set period to
    ;; zero to cancel [a periodic task]") -- normalizes an already-
    ;; converted ms period value to #f so the scheduler's own `period`
    ;; check (a plain truthiness test) treats it as one-shot rather
    ;; than reinserting the task forever at an unchanged deadline.
    (define (%normalize-period period-ms)
      (if (and period-ms (> period-ms 0)) period-ms #f))

    ;; ── Timer objects ────────────────────────────────────────────────────────

    (define-record-type <timer>
      (%make-timer-record mutex condvar tasks error-handler)
      timer?
      (mutex %timer-mutex)
      (condvar %timer-condvar)
      (tasks %timer-tasks)
      (error-handler %timer-error-handler)
      ;; Mutable fields, all protected by %timer-mutex -- every access
      ;; to any of these (reads included) happens with the mutex held,
      ;; enforced by convention (this file's own helpers below), not by
      ;; the record system itself.
      (next-id %timer-next-id %timer-next-id-set!)
      (preserved-error %timer-preserved-error %timer-preserved-error-set!)
      ;; cancelled? means "should stop / has been told to stop" -- set
      ;; the instant a stop is requested (explicitly, or by an
      ;; unhandled task error) so nothing else can race ahead of it.
      ;; stopped? means "the scheduler thread has actually finished its
      ;; loop and performed its own final mutex-unlock!" -- only once
      ;; stopped? is true is it safe for timer-cancel! to destroy the
      ;; underlying mutex/condvar (see timer-cancel! below).
      (cancelled? %timer-cancelled? %timer-cancelled-set!)
      (stopped? %timer-stopped? %timer-stopped-set!)
      ;; destroyed? means "a timer-cancel! call has already read/
      ;; cleared preserved-error and destroyed the mutex/condvar" --
      ;; DELIBERATELY separate from stopped?: the scheduler thread can
      ;; reach stopped?=#t entirely on its own (e.g. it auto-stopped
      ;; itself after an unhandled task error) before anyone has ever
      ;; called timer-cancel! at all. Short-circuiting timer-cancel! on
      ;; stopped? rather than destroyed? would (and, found by testing
      ;; during development, once actually did) let the FIRST
      ;; timer-cancel! call silently no-op and skip delivering a
      ;; preserved error whenever the scheduler happened to already be
      ;; fully stopped by the time it was called.
      (destroyed? %timer-destroyed? %timer-destroyed-set!))

    ;; Monotonic "now", in exact milliseconds -- current-jiffy is
    ;; CLOCK_MONOTONIC-backed (src/builtins.c), so this is immune to
    ;; wall-clock adjustments, unlike (current-second)/SRFI-19 time.
    (define (%now-ms)
      (/ (* (current-jiffy) 1000) (jiffies-per-second)))

    ;; A task: id, thunk, its absolute deadline (ms, %now-ms scale),
    ;; and its period in ms or #f for a one-shot task.
    (define-record-type <timer-task>
      (%make-task id thunk when-abs period)
      %timer-task?
      (id %task-id)
      (thunk %task-thunk)
      (when-abs %task-when-abs %task-when-abs-set!)
      (period %task-period %task-period-set!))

    ;; Earliest when-abs among all currently-scheduled tasks, or #f if
    ;; the table is empty. Caller must hold the timer's mutex.
    (define (%earliest-deadline tmr)
      (let ((tasks (hash-table-values (%timer-tasks tmr))))
        (if (null? tasks)
            #f
            (let loop ((ts (cdr tasks)) (best (%task-when-abs (car tasks))))
              (if (null? ts)
                  best
                  (loop (cdr ts) (min best (%task-when-abs (car ts)))))))))

    ;; Removes and returns every task whose deadline has arrived
    ;; (when-abs <= now). Periodic tasks are immediately reinserted
    ;; under the SAME id with their next occurrence's deadline (see
    ;; this file's header comment on why this happens before, not
    ;; after, running the current occurrence's thunk); one-shot tasks
    ;; are simply removed. Caller must hold the timer's mutex.
    (define (%pop-due-tasks! tmr now)
      (let ((due '()))
        (hash-table-walk (%timer-tasks tmr)
          (lambda (id task)
            (when (<= (%task-when-abs task) now)
              (set! due (cons task due))
              (hash-table-delete! (%timer-tasks tmr) id)
              (when (%task-period task)
                (hash-table-set! (%timer-tasks tmr) id
                  (%make-task id (%task-thunk task)
                              (+ (%task-when-abs task) (%task-period task))
                              (%task-period task)))))))
        due))

    ;; Runs every due task's thunk (with the timer's mutex NOT held --
    ;; critical: a thunk may itself call timer-schedule!/-reschedule!/
    ;; -task-remove!/-cancel! on this same timer, which would deadlock
    ;; if this thread already held the mutex). Returns the first
    ;; unhandled condition encountered (after the error-handler, if
    ;; any, already had a chance to handle it), or #f if every task's
    ;; thunk ran without an unhandled error.
    ;;
    ;; The call to `handler` is itself wrapped in its own guard: if a
    ;; user-supplied error-handler raises, that escapes as an ordinary
    ;; unhandled condition (stopping the timer) instead of propagating
    ;; straight out of this whole function -- which would otherwise
    ;; kill the scheduler thread outright with no `cancelled?`/
    ;; `stopped?` bookkeeping ever happening, i.e. exactly the silent,
    ;; undetectable failure this file's header comment says the outer
    ;; guard exists to prevent in the first place.
    (define (%run-due-tasks! tmr due)
      (let ((handler (%timer-error-handler tmr)))
        (let loop ((ts due))
          (if (null? ts)
              #f
              (let ((unhandled
                      (guard (e (#t
                                 (if handler
                                     (guard (e2 (#t e2)) (handler e) #f)
                                     e)))
                        ((%task-thunk (car ts)))
                        #f)))
                (if unhandled unhandled (loop (cdr ts))))))))

    ;; The scheduler thread's own top-level loop. Runs until the timer
    ;; is cancelled (explicitly, or implicitly by an unhandled task
    ;; error with no error-handler -- see this file's header comment).
    ;; On exit, sets stopped? and broadcasts before its own final
    ;; mutex-unlock! -- timer-cancel! waits on exactly this signal
    ;; before it's safe to destroy the mutex/condvar (see below).
    (define (%timer-loop! tmr)
      (let ((mx (%timer-mutex tmr)) (cv (%timer-condvar tmr)))
        (mutex-lock! mx)
        (let loop ()
          (if (%timer-cancelled? tmr)
              (begin
                (%timer-stopped-set! tmr #t)
                (cond-broadcast! cv)
                (mutex-unlock! mx))
              (let* ((now (%now-ms))
                     (due (%pop-due-tasks! tmr now)))
                (if (null? due)
                    (begin
                      (let ((next (%earliest-deadline tmr)))
                        (if next
                            ;; cond-wait-timeout!'s C side only accepts a
                            ;; fixnum or a flonum for the timeout -- the ms
                            ;; arithmetic above is exact and can yield a
                            ;; non-fixnum rational (e.g. a sub-millisecond
                            ;; timer-delta unit), so it must be floated here.
                            (cond-wait-timeout! cv mx (exact->inexact (max 0 (/ (- next now) 1000))))
                            (cond-wait! cv mx)))
                      (loop))
                    (begin
                      (mutex-unlock! mx)
                      (let ((unhandled (%run-due-tasks! tmr due)))
                        (mutex-lock! mx)
                        ;; Only record the error if an explicit
                        ;; timer-cancel! didn't already run (and win)
                        ;; while the mutex was released above -- without
                        ;; this guard, a cancel that raced a task's own
                        ;; unhandled error could return cleanly while
                        ;; this branch went on to silently overwrite
                        ;; preserved-error a moment later, orphaning the
                        ;; condition (a real race found by review: the
                        ;; caller who legitimately called timer-cancel!
                        ;; never sees it, and nothing else ever will
                        ;; either, since a preserved error is delivered
                        ;; by the NEXT timer-cancel! call, and there is
                        ;; no reason for the original caller to make
                        ;; one). An explicit cancel always wins.
                        (when (and unhandled (not (%timer-cancelled? tmr)))
                          (%timer-preserved-error-set! tmr unhandled)
                          (%timer-cancelled-set! tmr #t)))
                      (loop))))))))

    ;; ── Public API ───────────────────────────────────────────────────────────

    (define (make-timer . maybe-handler)
      (let ((tmr (%make-timer-record
                   (make-mutex) (make-condvar)
                   (make-hash-table) (if (null? maybe-handler) #f (car maybe-handler)))))
        (%timer-next-id-set! tmr 0)
        (%timer-preserved-error-set! tmr #f)
        (%timer-cancelled-set! tmr #f)
        (%timer-stopped-set! tmr #f)
        (%timer-destroyed-set! tmr #f)
        (spawn (lambda () (%timer-loop! tmr)))
        tmr))

    ;; Stops the timer and BLOCKS until the scheduler thread has
    ;; actually finished its loop (see %timer-loop!'s own comment) --
    ;; not just requested to stop -- so that by the time this call
    ;; returns, no task will ever run again and it's safe to release
    ;; the underlying OS mutex/condvar. A preserved error is delivered
    ;; (raised) at most once: once read here, it's cleared, so a
    ;; SECOND, SEQUENTIAL timer-cancel! call from the same thread on an
    ;; already-stopped timer is a harmless no-op (the %timer-destroyed?
    ;; check up front never touches the -- by then destroyed -- mutex
    ;; at all). Calling timer-cancel! concurrently from multiple
    ;; threads on the very same timer is NOT supported -- the same
    ;; restriction (curry sync)'s own mutex-destroy!/condvar-destroy!
    ;; already have (a mutex/condvar must not be destroyed while any
    ;; thread might still be blocked trying to acquire it) -- ordinary
    ;; single-owner usage (one thread creates and later cancels a given
    ;; timer) is unaffected.
    (define (timer-cancel! tmr)
      (if (%timer-destroyed? tmr)
          (if #f #f)
          (begin
            (mutex-lock! (%timer-mutex tmr))
            (%timer-cancelled-set! tmr #t)
            (cond-signal! (%timer-condvar tmr))
            (let wait ()
              (unless (%timer-stopped? tmr)
                (cond-wait! (%timer-condvar tmr) (%timer-mutex tmr))
                (wait)))
            (let ((err (%timer-preserved-error tmr)))
              (when err (%timer-preserved-error-set! tmr #f))
              (%timer-destroyed-set! tmr #t)
              (mutex-unlock! (%timer-mutex tmr))
              (mutex-destroy! (%timer-mutex tmr))
              (condvar-destroy! (%timer-condvar tmr))
              (when err (raise err))))))

    (define (timer-schedule! tmr thunk when . maybe-period)
      (let ((period (if (null? maybe-period) #f (car maybe-period))))
        (mutex-lock! (%timer-mutex tmr))
        (if (%timer-cancelled? tmr)
            (begin (mutex-unlock! (%timer-mutex tmr))
                   (error "timer-schedule!: timer is cancelled" tmr))
            (let* ((id (%timer-next-id tmr))
                   (when-ms (%->ms when "timer-schedule!"))
                   (period-ms (and period (%normalize-period (%->ms period "timer-schedule!")))))
              (%timer-next-id-set! tmr (+ id 1))
              (hash-table-set! (%timer-tasks tmr) id
                (%make-task id thunk (+ (%now-ms) when-ms) period-ms))
              (cond-signal! (%timer-condvar tmr))
              (mutex-unlock! (%timer-mutex tmr))
              id))))

    (define (timer-reschedule! tmr id when . maybe-period)
      (mutex-lock! (%timer-mutex tmr))
      (let ((task (hash-table-ref/default (%timer-tasks tmr) id #f)))
        (if (not task)
            (begin (mutex-unlock! (%timer-mutex tmr))
                   (error "timer-reschedule!: id not associated with timer, "
                          "or its task already executed" tmr id))
            (let* ((when-ms (%->ms when "timer-reschedule!"))
                   (period-given? (not (null? maybe-period)))
                   (period-ms (and period-given? (%->ms (car maybe-period) "timer-reschedule!"))))
              ;; "Set period to 0 to cancel [a] periodic task" (SRFI
              ;; text) -- 0 here means "no longer periodic", i.e. the
              ;; task becomes one-shot at the newly-given `when`, not
              ;; literally "repeat every 0ms".
              (%task-when-abs-set! task (+ (%now-ms) when-ms))
              (when period-given?
                (%task-period-set! task (%normalize-period period-ms)))
              (cond-signal! (%timer-condvar tmr))
              (mutex-unlock! (%timer-mutex tmr))
              id))))

    (define (timer-task-remove! tmr id)
      (mutex-lock! (%timer-mutex tmr))
      (let ((existed? (hash-table-exists? (%timer-tasks tmr) id)))
        (when existed? (hash-table-delete! (%timer-tasks tmr) id))
        (mutex-unlock! (%timer-mutex tmr))
        existed?))

    (define (timer-task-exists? tmr id)
      (mutex-lock! (%timer-mutex tmr))
      (let ((existed? (hash-table-exists? (%timer-tasks tmr) id)))
        (mutex-unlock! (%timer-mutex tmr))
        existed?))

  )) ;; end begin, define-library
