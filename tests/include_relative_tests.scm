;;; Regression test for a real bug found while porting SRFI-279
;;; upstream: (include ...) inside a define-library declaration used to
;;; resolve its filename against the process's cwd unconditionally,
;;; rather than the directory of the file doing the including -- so a
;;; library whose own directory wasn't the cwd could never portably
;;; (include ...) a sibling file. Fixed in runtime.c's scm_load (a
;;; directory-context stack, load_push_dir/load_pop_dir) and mirrored in
;;; modules.c's load_scheme_module (a second, independent file-reading
;;; loop that (include ...) declarations can also be reached from).
;;;
;;; This only actually exercises the fix if ctest runs this script from
;;; a working directory other than tests/fixtures/include_relative/ --
;;; true by default (ctest's cwd is the build directory) — see this
;;; test's CMakeLists.txt registration, which deliberately does NOT set
;;; a WORKING_DIRECTORY override.

(import (test-include-relative))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(check "sibling file reached via a bare-filename (include ...) resolved relative to the .sld, not cwd"
  helper-loaded? #t)
(check "value from the included sibling file is visible"
  helper-value 42)

(display (string-append (number->string pass) " passed, " (number->string fail) " failed")) (newline)
(if (> fail 0) (exit 1) (exit 0))
