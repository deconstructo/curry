;;; Public shim for (srfi 272 intermediate) -- see
;;; lib/curry/modules/srfi/s272/intermediate-impl.scm.
(define-library (srfi 272 intermediate)
  (import (srfi s272 intermediate-impl))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file
    pp-width pp-graph pp-circle pp-radix pp-level pp-length pretty-style))
