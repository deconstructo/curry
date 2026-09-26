(define-library (srfi 273)
  (import (srfi s273 extensions))
  (export
    check-arg check-impl?
    lambda-checked case-lambda-checked define-checked
    define-check declare-checked define-values-checked
    %lc273-check-impl-marker
    %lc273-args %lc273-wrap %lc273-wrap-rest
    %lc273-normalize-preds %lc273-check-return-values
    %clc273-normalize %clc273-dispatch %clc273-try))
