(define-library (srfi 78)
  (import (srfi s78 lightweight-testing))
  (export check check-set-mode! check-reset! check-passed? check-report
          %check-proc)) ; the `check` macro's expansion references this --
                        ; see s78/lightweight-testing.scm's own comment
