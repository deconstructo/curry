;;; Regression fixture: a (load ...) that raises must not leave stale
;;; directory-context state behind that corrupts a *later, unrelated*
;;; (load ...) once the error is caught and execution continues in the
;;; same process. Was broken before scm_load/load_scheme_module wrapped
;;; their read/eval loops in SCM_PROTECT to release back to a saved
;;; load_dir_mark() on the exceptional path (runtime.c), not just the
;;; normal-exit path.

(guard (e (#t (display "caught the deliberate error, continuing") (newline)))
  (load "broken.scm"))

(load "ok.scm")

(if (equal? ok-value 7)
    (begin (display "PASS: unrelated load after a caught error still resolves correctly") (newline) (exit 0))
    (begin (display "FAIL: got ") (write ok-value) (newline) (exit 1)))
