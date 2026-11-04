;;; Public shim for (srfi 166 pretty) -- see lib/curry/modules/srfi/s166/pretty-impl.scm.
(define-library (srfi 166 pretty)
  (import (srfi s166 pretty-impl))
  (export pretty pretty-shared pretty-simply pretty-with-color))
