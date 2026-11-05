;;; Public shim for (srfi 166 columnar) -- see lib/curry/modules/srfi/s166/columnar-impl.scm.
(define-library (srfi 166 columnar)
  (import (srfi s166 columnar-impl))
  (export columnar tabular wrapped wrapped/list wrapped/char justified
          from-file line-numbers))
