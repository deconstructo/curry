;;; Public shim for (srfi 166 unicode) -- see lib/curry/modules/srfi/s166/unicode-impl.scm.
(define-library (srfi 166 unicode)
  (import (srfi s166 unicode-impl))
  (export terminal-aware
          string-terminal-width string-terminal-width/wide
          substring-terminal-width substring-terminal-width/wide
          substring-terminal-preserve
          upcased downcased))
