;; SRFI-9: Defining Record Types.
;;
;; curry's core `define-record-type` special form already implements the
;; R7RS form, which is a strict superset of SRFI-9's own -- SRFI-9 requires
;; exactly one accessor per field (no mutators, no field omitted from the
;; constructor), while R7RS additionally allows mutators and constructors
;; that only take a subset of fields. Every valid SRFI-9 form is therefore
;; already valid curry syntax; this shim just makes the name importable
;; under SRFI-9's own library path.
(define-library (srfi s9 records)
  (import (scheme base))
  (export define-record-type))
