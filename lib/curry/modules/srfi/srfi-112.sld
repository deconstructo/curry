(define-library (srfi srfi-112)
  (import (srfi s112 environment-inquiry))
  (export
    implementation-name implementation-version cpu-architecture machine-name
    os-name os-version))
