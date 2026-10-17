;;; SRFI-176: Version flag.
;;;
;;; https://srfi.schemers.org/srfi-176/ -- a standard `(version-alist)`
;;; procedure plus a `-V` command-line flag, both exposing implementation
;;; identity, build and feature information.
;;;
;;; Both are backed by one C function, curry_version_alist() in
;;; src/version_info.c. `(curry-version-alist)` returns its result to
;;; Scheme; `-V` (src/main.c) prints the same list in LOSE format (one
;;; property per line). The two therefore always agree.
;;;
;;; Properties (a property whose value is unknown is left out):
;;;   command website version languages
;;;   build.date build.platform build.configure
;;;   build.git.tag build.git.branch build.git.commit build.git.modified
;;;   scheme.id scheme.srfi scheme.features scheme.path
;;;   c.version c.compile c.link c.type-bits
;;;
;;; scheme.srfi is a literal list in src/version_info.c, not a scan of
;;; lib/curry/modules/srfi/ -- that way the list does not depend on where
;;; the installed modules are. tests/test_cli.sh compares the literal
;;; against the directory, so a new SRFI that is not added to it fails
;;; the test suite.
(define-library (srfi s176 version-flag)
  (import (scheme base))
  (export version-alist)
  (begin

    (define (version-alist)
      (curry-version-alist))))
