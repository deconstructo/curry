(define-library (srfi srfi-64)
  (import (srfi s64 testing))
  (export
    test-assert test-eqv test-eq test-equal test-approximate test-error
    test-read-eval-string test-begin test-end test-group
    test-group-with-cleanup test-skip test-expect-fail test-match-name
    test-match-nth test-match-any test-match-all test-runner?
    test-runner-current test-runner-get test-runner-create test-runner-null
    test-runner-simple test-runner-factory test-apply test-with-runner
    test-result-kind test-passed? test-result-ref test-result-set!
    test-result-remove test-result-clear test-result-alist
    test-runner-pass-count test-runner-fail-count test-runner-xpass-count
    test-runner-xfail-count test-runner-skip-count test-runner-test-name
    test-runner-group-path test-runner-group-stack test-runner-aux-value
    test-runner-aux-value! test-runner-reset test-runner-on-test-begin
    test-runner-on-test-begin! test-runner-on-test-end
    test-runner-on-test-end! test-runner-on-group-begin
    test-runner-on-group-begin! test-runner-on-group-end
    test-runner-on-group-end! test-runner-on-bad-count
    test-runner-on-bad-count! test-runner-on-bad-end-name
    test-runner-on-bad-end-name! test-runner-on-final test-runner-on-final!
    test-on-test-begin-simple test-on-test-end-simple
    test-on-group-begin-simple test-on-group-end-simple
    test-on-bad-count-simple test-on-bad-end-name-simple test-on-final-simple
    ; Internal helpers -- exported only because curry's syntax-rules macros
    ; resolve template identifiers in the use-site environment rather than
    ; the definition-site environment, so any procedure a public macro
    ; expands to (test-assert -> %run-assert, etc.) must be visible there
    ; too. Same reasoning as (srfi 64)'s own re-export of these.
    %run-assert %run-compare %run-approx %run-error %run-error-2 %run-group))
