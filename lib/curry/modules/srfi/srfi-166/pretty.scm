;;; Alternate-name alias for (srfi 166 pretty) -- see lib/curry/modules/srfi/166/pretty.scm.
(define-library (srfi srfi-166 pretty)
  (import (srfi 166 pretty))
  (export
    pretty pretty-shared pretty-simply pretty-with-color))
