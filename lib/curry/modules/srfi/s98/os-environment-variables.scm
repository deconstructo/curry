(define-library (srfi s98 os-environment-variables)
  (import (scheme base))
  (export get-environment-variable get-environment-variables)
  (begin
    ; Both procedures are already native builtins (src/builtins.c) matching
    ; SRFI-98/R7RS (scheme process-context) exactly; re-exported here only so
    ; portable code using the (srfi s98 ...) naming convention works
    ; unchanged.
    ))
