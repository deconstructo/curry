;;; SRFI-272 minimalist library: (srfi 272) -- just `pp`, with fixed
;;; internal defaults (79-column width, graph mode off, circle mode on,
;;; matching the engine's own parameter defaults) and no keyword args.
;;; https://srfi.schemers.org/srfi-272/

(define-library (srfi 272)
  (import (scheme base) (srfi s272 engine))
  (export pp)
  (begin
    (define (pp obj . maybe-port)
      (%pp-to-port obj (if (pair? maybe-port) (car maybe-port) (current-output-port)) (%current-mode)))))
