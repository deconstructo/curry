;;; Alternate-name alias for (srfi 166 columnar) -- see lib/curry/modules/srfi/166/columnar.scm.
(define-library (srfi srfi-166 columnar)
  (import (srfi 166 columnar))
  (export
    columnar tabular wrapped wrapped/list wrapped/char justified from-file
    line-numbers))
