(define-library (srfi 215)
  (import (srfi s215 log))
  (export
    send-log current-log-fields current-log-callback EMERGENCY ALERT CRITICAL
    ERROR WARNING NOTICE INFO DEBUG))
