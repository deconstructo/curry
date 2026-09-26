;;; SRFI-176: Version flag.
;;;
;;; https://srfi.schemers.org/srfi-176/ -- a standard `(version-alist)`
;;; procedure plus a `-V` command-line flag, both exposing the same kind
;;; of implementation-identity/build/feature information in a portable,
;;; line-oriented S-expression format (LOSE) that's easy to parse from
;;; Scheme, C, or a shell script.
;;;
;;; `-V` itself is implemented directly in src/main.c (print_version_
;;; alist_lose), deliberately independent of booting the Scheme runtime
;;; -- matching how curry's pre-existing `-v`/`-h` flags already short-
;;; circuit before any VM/module initialization, rather than requiring
;;; a full interpreter boot just to print version info to a shell
;;; script. This library's own `version-alist` is the richer, from-
;;; within-a-running-program counterpart: it can (and does) include
;;; properties that genuinely need the module system and core builtins
;;; already initialized -- `scheme.features` is literally just curry's
;;; own `(features)` builtin, and `scheme.srfi` enumerates every SRFI
;;; this codebase ships. The two deliberately overlap on the handful of
;;; static identity properties (command/website/version/languages/
;;; scheme.id) rather than one calling the other, so -V stays as fast
;;; and dependency-free as -v/-h; if you change one of those five,
;;; change both.
;;;
;;; scheme.srfi is a plain, hand-maintained sorted literal (not computed
;;; by scanning lib/curry/modules/srfi/ at import time, e.g. via
;;; directory-files from (curry posix)) -- deliberately, to keep this
;;; library dependency-free of any optional C module (posix is default-
;;; ON but not guaranteed present) and immune to install-layout
;;; assumptions about where the running binary's own module directory
;;; actually lives. Regenerate by running, from the repo root:
;;;   ls lib/curry/modules/srfi/*.sld | xargs -n1 basename | \
;;;     sed 's/\.sld$//' | grep -E '^[0-9]+$' | sort -n
;;; then manually re-adding 0 and 61 (SRFI-0/SRFI-61 are hardcoded
;;; compiler/evaluator special forms -- cond-expand/cond's extended
;;; arrow clause -- with no .sld shim to enumerate; see s0.md/s61.md).

(define-library (srfi s176 version-flag)
  (import (scheme base))
  (export version-alist)
  (begin

    (define %scheme-srfi
      '(0 1 4 8 9 14 18 19 26 27 31 41 45 54 59 61 64 69 78 90 95 98
        106 111 112 113 120 125 126 128 132 133 141 145 158 160 170 174
        176 194 195 209 210 212 215 225 227 238 252 253 263 273 279))

    (define (version-alist)
      (list
        (list 'command "curry")
        (list 'website "https://github.com/deconstructo/curry")
        (list 'version (curry-version))
        (list 'languages 'scheme 'r7rs)
        (list 'scheme.id 'curry)
        (cons 'scheme.srfi %scheme-srfi)
        (cons 'scheme.features (features))))))
