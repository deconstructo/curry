;;; Public shim for (srfi 272 measure) -- see
;;; lib/curry/modules/srfi/s272/measure-impl.scm.
(define-library (srfi 272 measure)
  (import (srfi s272 measure-impl))
  (export char-width-procedure))
