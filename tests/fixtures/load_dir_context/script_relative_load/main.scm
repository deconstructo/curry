;;; Regression fixture: a top-level script (run as curry's positional
;;; script argument, not via -l or (import ...)) whose own (load ...) of
;;; a sibling file must resolve relative to *this script's* directory,
;;; not ctest's cwd (the build directory — see this test's CMakeLists.txt
;;; registration, deliberately no WORKING_DIRECTORY override matching
;;; this fixture's own directory). Was broken before main.c's
;;; positional-script-argument path started marking/pushing its own
;;; directory (runtime.c's load_dir_mark/load_push_dir).

(load "subdir/inner.scm")

(if (equal? inner-value 99)
    (begin (display "PASS: script-relative (load ...) resolved correctly") (newline) (exit 0))
    (begin (display "FAIL: got ") (write inner-value) (newline) (exit 1)))
