;;; Public shim for (srfi 272 basic) -- see lib/curry/modules/srfi/s272/basic.scm.
(define-library (srfi 272 basic)
  (import (srfi s272 basic-impl))
  (export pp pp-width pp-graph pp-circle pprint pprint-shared pprint-simple))
