(define-library (srfi s238 codesets)
  (import (curry codesets) (scheme base))
  (export
    codeset? codeset-symbols codeset-symbol codeset-number codeset-message)
  (begin
    ; All five procedures are already named per the SRFI-238 spec and
    ; delegate directly to the C-level (curry codesets) module — re-exported
    ; here only so portable code using the (srfi s238 codesets) naming
    ; convention works unchanged. Requires curry built with
    ; -DBUILD_MODULE_CODESETS=ON (the default).
    ))
