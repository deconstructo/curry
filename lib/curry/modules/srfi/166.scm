;;; (srfi 166): Monadic Formatting -- everything in (srfi 166 base),
;;; (srfi 166 pretty), (srfi 166 columnar), (srfi 166 unicode) and
;;; (srfi 166 color). https://srfi.schemers.org/srfi-166/
;;; The names that start with a percent sign are what the fn / with /
;;; with! macros expand into.
(define-library (srfi 166)
  (import (srfi 166 base) (srfi 166 pretty) (srfi 166 columnar)
          (srfi 166 unicode) (srfi 166 color))
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
    pretty-environment output-default pretty pretty-shared pretty-simply
    pretty-with-color columnar tabular wrapped wrapped/list wrapped/char
    justified from-file line-numbers terminal-aware string-terminal-width
    string-terminal-width/wide substring-terminal-width
    substring-terminal-width/wide substring-terminal-preserve upcased
    downcased as-red as-blue as-green as-cyan as-yellow as-magenta
    as-white as-black as-bold as-italic as-underline as-color
    as-true-color on-red on-blue on-green on-cyan on-yellow on-magenta
    on-white on-black on-color on-true-color))
