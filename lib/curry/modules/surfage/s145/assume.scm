(define-library (surfage s145 assume)
  (import (scheme base))
  (export assume assume-type)
  (begin

    ; SRFI-145: (assume expr message ...) declares that expr is assumed true.
    ; A conforming implementation may elide the check entirely in production
    ; builds; curry always checks it and raises on violation, since there is
    ; no separate "production mode" build flag to key an elision on. This
    ; is unrelated to curry's own `assume!`/`can-assume?` (src/builtins_curry.c),
    ; which record algebraic assumptions on symbolic CAS variables, not
    ; runtime boolean invariants.
    (define-syntax assume
      (syntax-rules ()
        ((_ expr)
         (if (not expr)
             (error "assumption violated" 'expr)
             (if #f #f)))
        ((_ expr message ...)
         (if (not expr)
             (error "assumption violated" 'expr message ...)
             (if #f #f)))))

    (define (assume-type obj pred . message)
      (if (not (pred obj))
          (apply error "assumption violated: wrong type" obj message)
          obj))))
