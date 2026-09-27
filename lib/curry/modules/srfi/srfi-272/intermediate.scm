;;; Alternate-name alias for (srfi 272 intermediate) -- see
;;; lib/curry/modules/srfi/272/intermediate.scm.
(define-library (srfi srfi-272 intermediate)
  (import (srfi 272 intermediate))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file
    pp-width pp-graph pp-circle pp-radix pp-level pp-length pretty-style))
