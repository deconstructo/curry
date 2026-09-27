;;; Alternate-name alias for (srfi 272 advanced) -- see
;;; lib/curry/modules/srfi/272/advanced.scm.
(define-library (srfi srfi-272 advanced)
  (import (srfi 272 advanced))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file
    pp-width pp-graph pp-circle pp-radix pp-level pp-length
    pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
    pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
    pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
    pp-styles pretty-style add-pp-style lookup-pp-style
    pp-hooks pretty-hook add-pp-hook lookup-pp-hook
    glst-pp-hook bvec-pp-hook atom-pp-hook rmac-pp-hook
    make-pprint-generator))
