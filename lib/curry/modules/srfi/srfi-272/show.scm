;;; Alternate-name alias for (srfi 272 show) -- see lib/curry/modules/srfi/272/show.scm.
(define-library (srfi srfi-272 show)
  (import (srfi 272 show))
  (export pprinted pprinted*
          pp-width pp-graph pp-circle pp-radix pp-level pp-length
          pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
          pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
          pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
          pp-styles pp-hooks))
