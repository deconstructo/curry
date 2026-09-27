;;; Alternate-name alias for (srfi 272 basic) -- see lib/curry/modules/srfi/272/basic.scm.
(define-library (srfi srfi-272 basic)
  (import (srfi 272 basic))
  (export pp pp-width pp-graph pp-circle pprint pprint-shared pprint-simple))
