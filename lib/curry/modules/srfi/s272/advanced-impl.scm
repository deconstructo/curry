;;; SRFI-272 advanced-tier implementation: everything from intermediate
;;; plus the full parameter set (pp-lines, pp-level-stub, ..., pp-tint),
;;; the style/hook registries (pretty-style/add-pp-style/lookup-pp-style,
;;; pretty-hook/add-pp-hook/lookup-pp-hook, the four hook constructors),
;;; and make-pprint-generator. The public shim at
;;; lib/curry/modules/srfi/272/advanced.scm just re-exports this.
;;; https://srfi.schemers.org/srfi-272/

(define-library (srfi s272 advanced-impl)
  (import (scheme base) (srfi s272 engine) (srfi s272 intermediate-impl)
          (srfi s158 generators-and-accumulators))
  (export
    pp pp* pprint pprint-shared pprint-simple pprint-file
    pp-width pp-graph pp-circle pp-radix pp-level pp-length
    pp-lines pp-level-stub pp-length-stub pp-lines-stub pp-newline
    pp-inline-width pp-miser-width pp-indent pp-tab pp-max-tab
    pp-code pp-brackets pp-pretty pp-color pp-decorate pp-emit pp-tint
    pp-styles pretty-style add-pp-style lookup-pp-style
    pp-hooks pretty-hook add-pp-hook lookup-pp-hook
    glst-pp-hook bvec-pp-hook atom-pp-hook rmac-pp-hook
    make-pprint-generator)
  (begin
    ;; A SRFI-158 generator that yields the pretty-printed lines of obj
    ;; one at a time, honoring the same keyword arguments as pp. Built
    ;; by rendering the whole thing up front (the engine's own layout
    ;; pass isn't incremental/lazy) and turning the resulting line list
    ;; into a list-based generator -- a real generator interface, even
    ;; though the work behind it happens eagerly on first pull.
    (define (make-pprint-generator obj . kvs)
      (%pp-with-kwargs kvs
        (lambda ()
          (list->generator (%pp-lines-list obj (%current-mode))))))))
