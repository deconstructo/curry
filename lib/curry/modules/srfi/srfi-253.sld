(define-library (srfi srfi-253)
  (import (srfi s253 data-checking))
  (export
    check-arg values-checked check-case
    lambda-checked case-lambda-checked
    define-checked define-record-type-checked
    %values-checked %lambda-checked %clc-dispatch %clc-try
    %drtc-build-raw %drtc-finish %drtc-ctor-checks %drtc-check-each %drtc-mod-check))
