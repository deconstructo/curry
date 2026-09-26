(define-library (srfi srfi-120)
  (import (srfi s120 timer))
  (export
    make-timer timer? timer-cancel!
    timer-schedule! timer-reschedule! timer-task-remove! timer-task-exists?
    make-timer-delta timer-delta?))
