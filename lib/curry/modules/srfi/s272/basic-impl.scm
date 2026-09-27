;;; SRFI-272 basic-tier implementation: pp plus the pp-width/pp-graph/
;;; pp-circle parameters, and the three explicit-sharing-mode variants
;;; pprint/pprint-shared/pprint-simple (each hardwired to its own mode
;;; regardless of pp-graph/pp-circle, per the spec). The public shim at
;;; lib/curry/modules/srfi/272/basic.scm just re-exports this.
;;; https://srfi.schemers.org/srfi-272/

(define-library (srfi s272 basic-impl)
  (import (scheme base) (srfi s272 engine))
  (export pp pp-width pp-graph pp-circle pprint pprint-shared pprint-simple)
  (begin
    (define (%out maybe-port) (if (pair? maybe-port) (car maybe-port) (current-output-port)))

    (define (pp obj . maybe-port)
      (%pp-to-port obj (%out maybe-port) (%current-mode)))

    ;; pprint behaves as R7RS `write` -- only genuine cycles get datum
    ;; labels, ordinary (acyclic) sharing is printed as if freshly
    ;; consed each time.
    (define (pprint obj . maybe-port)
      (%pp-to-port obj (%out maybe-port) 'circle))

    ;; pprint-shared behaves as R7RS `write-shared` -- every object
    ;; occurring more than once (cyclic or not) gets a datum label.
    (define (pprint-shared obj . maybe-port)
      (%pp-to-port obj (%out maybe-port) 'graph))

    (define (pprint-simple obj . maybe-port)
      (%pp-to-port obj (%out maybe-port) 'simple))))
