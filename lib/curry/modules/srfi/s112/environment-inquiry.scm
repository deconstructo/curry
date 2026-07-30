(define-library (srfi s112 environment-inquiry)
  (import (curry posix) (scheme base))
  (export
    implementation-name implementation-version
    cpu-architecture machine-name os-name os-version)
  (begin
    ; All six procedures are already named per the SRFI-112 spec and
    ; delegate directly to the C-level (curry posix) module (uname(2)/
    ; gethostname(2)) — re-exported here only so portable code using the
    ; (srfi s112 environment-inquiry) naming convention works unchanged.
    ; Requires curry built with -DBUILD_MODULE_POSIX=ON (the default).
    ))
