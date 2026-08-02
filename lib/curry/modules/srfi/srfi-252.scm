(define-library (srfi srfi-252)
  (import (srfi s252 property-testing))
  (export
    test-property test-property-expect-fail test-property-skip
    test-property-error test-property-error-type
    property-test-runner
    boolean-generator char-generator string-generator symbol-generator
    bytevector-generator
    integer-generator real-generator rational-generator complex-generator
    number-generator
    exact-integer-generator exact-real-generator exact-rational-generator
    exact-complex-generator exact-integer-complex-generator
    exact-number-generator
    inexact-integer-generator inexact-real-generator inexact-rational-generator
    inexact-complex-generator inexact-number-generator
    list-generator-of vector-generator-of pair-generator-of
    procedure-generator-of
    %test-property %test-property-expect-fail %test-property-error
    %test-property-skip))
