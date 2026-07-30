(define-library (srfi s215 log)
  (import (scheme base) (scheme write))
  (export
    send-log
    current-log-fields
    current-log-callback
    EMERGENCY ALERT CRITICAL ERROR WARNING NOTICE INFO DEBUG)
  (begin

    (define EMERGENCY 0)
    (define ALERT     1)
    (define CRITICAL  2)
    (define ERROR     3)
    (define WARNING   4)
    (define NOTICE    5)
    (define INFO      6)
    (define DEBUG     7)

    (define current-log-fields (make-parameter '()))

    ; The default callback buffers messages until the application installs
    ; its own callback, at which point the buffer replays into it and is
    ; cleared -- this also covers the case where the default is restored
    ; via parameterize, per the SRFI-215 spec.
    (define log-buffer '())
    (define log-buffer-max 100)

    (define (default-log-callback msg)
      (set! log-buffer (cons msg log-buffer))
      (when (> (length log-buffer) log-buffer-max)
        (set! log-buffer (reverse (cdr (reverse log-buffer))))))

    (define (log-callback-converter proc)
      (unless (eq? proc default-log-callback)
        (let ((buffered (reverse log-buffer)))
          (set! log-buffer '())
          (for-each proc buffered)))
      proc)

    (define current-log-callback
      (make-parameter default-log-callback log-callback-converter))

    (define (log-value->field v)
      (if (or (string? v) (bytevector? v) (exact-integer? v)
              (error-object? v) (condition? v))
          v
          (let ((out (open-output-string)))
            (write v out)
            (get-output-string out))))

    (define (send-log severity message . rest)
      (when (odd? (length rest))
        (error "send-log: odd number of key/value arguments" rest))
      (let loop ((kv rest))
        (unless (null? kv)
          (unless (symbol? (car kv))
            (error "send-log: key is not a symbol" (car kv)))
          (loop (cddr kv))))
      (let ((msg (append
                   (list (cons 'SEVERITY severity)
                         (cons 'MESSAGE message))
                   (let loop ((kv rest) (acc '()))
                     (if (null? kv)
                         (reverse acc)
                         (loop (cddr kv)
                               (cons (cons (car kv) (log-value->field (cadr kv))) acc))))
                   (current-log-fields))))
        ((current-log-callback) msg)))))
