(define-library (surfage s8 receive)
  (import (scheme base))
  (export receive)
  (begin

    ; SRFI-8's `receive` is a multiple-values binding macro, unrelated to
    ; curry's own `receive` special form (actor mailbox receive — see
    ; src/actors.c and the `(spawn)`/`(send!)`/`(receive)` primitives). The
    ; two share a name by coincidence between SRFI-8 and curry's actor model;
    ; importing this library shadows the actor form with the SRFI-8 macro
    ; for the rest of the importing program's top level, so don't import
    ; this into code that also uses actor mailboxes.
    (define-syntax receive
      (syntax-rules ()
        ((_ formals expression body ...)
         (call-with-values (lambda () expression)
                            (lambda formals body ...)))))))
