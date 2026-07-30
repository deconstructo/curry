(define-library (srfi 227)
  (import (srfi s227 optional-arguments))
  (export
    opt-lambda let-optionals let-optionals* default-object default-object?
    %opt-bind %opt-bind-optional))
