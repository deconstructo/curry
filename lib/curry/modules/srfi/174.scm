(define-library (srfi 174)
  (import (srfi s174 posix-timespecs))
  (export
    timespec timespec? timespec-seconds timespec-nanoseconds
    inexact->timespec timespec->inexact timespec=? timespec<? timespec-hash))
