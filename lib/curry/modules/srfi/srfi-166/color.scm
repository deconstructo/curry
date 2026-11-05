;;; Alternate-name alias for (srfi 166 color) -- see lib/curry/modules/srfi/166/color.scm.
(define-library (srfi srfi-166 color)
  (import (srfi 166 color))
  (export
    as-red as-blue as-green as-cyan as-yellow as-magenta as-white as-black
    as-bold as-italic as-underline as-color as-true-color on-red on-blue
    on-green on-cyan on-yellow on-magenta on-white on-black on-color
    on-true-color))
