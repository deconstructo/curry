;;; Public shim for (srfi 272 colorize) -- see
;;; lib/curry/modules/srfi/s272/colorize-impl.scm.
(define-library (srfi 272 colorize)
  (import (srfi s272 colorize-impl))
  (export
    detect-sgr-tier sgr-support-tier sgr0 sgr1 sgr2 sgr3
    make-asb asb->sgr-string make-asb-palette asb-palette-ref default-asb-palette
    semantic-color-mapper? make-semantic-color-mapper default-semantic-color-mapper
    semantic-color->start-string semantic-color->end-string))
