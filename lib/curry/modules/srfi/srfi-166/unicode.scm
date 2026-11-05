;;; Alternate-name alias for (srfi 166 unicode) -- see lib/curry/modules/srfi/166/unicode.scm.
(define-library (srfi srfi-166 unicode)
  (import (srfi 166 unicode))
  (export
    terminal-aware string-terminal-width string-terminal-width/wide
    substring-terminal-width substring-terminal-width/wide
    substring-terminal-preserve upcased downcased))
