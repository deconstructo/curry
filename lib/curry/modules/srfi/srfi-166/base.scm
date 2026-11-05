;;; Alternate-name alias for (srfi 166 base) -- see lib/curry/modules/srfi/166/base.scm.
(define-library (srfi srfi-166 base)
  (import (srfi 166 base))
  (export
    show each each-in-list fn with with! forked call-with-output
    make-state-variable nothing %fn %fn-collect %with %with! displayed
    written written-shared written-simply escaped maybe-escaped numeric
    numeric/comma numeric/si numeric/fitted nl fl space-to tab-to joined
    joined/prefix joined/suffix joined/last joined/dot joined/range padded
    padded/right padded/both trimmed trimmed/right trimmed/both
    trimmed/lazy fitted fitted/right fitted/both port row col width output
    writer string-width substring/width substring/preserve pad-char
    ellipsis radix precision decimal-sep decimal-align sign-rule
    comma-rule comma-sep word-separator? ambiguous-is-wide?
    pretty-environment output-default))
