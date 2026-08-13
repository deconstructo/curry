;; Fixture for cond_expand_tests.scm's sibling test,
;; include_relative_tests.scm: a define-library whose own (include ...)
;; declaration names a sibling file by bare filename, the same layout
;; SRFI libraries commonly use (and specifically the layout the upstream
;; SRFI-279 reference repo's 279.sld uses for its per-implementation
;; chibi.scm/kawa.scm/guile.scm/generic.scm files). This only proves
;; anything as long as the test that imports it runs from a working
;; directory that ISN'T this one -- see include_relative_tests.scm and
;; its CMakeLists.txt registration (no WORKING_DIRECTORY override, so
;; ctest's default build-dir cwd applies).
(define-library (test-include-relative)
  (export helper-loaded? helper-value)
  (include "helper.scm"))
