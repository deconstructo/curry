;;; curry/conditions — CL-style condition system
;;;
;;; Primitives always in global env (no import needed):
;;;   condition?  condition-type  condition-fields  condition-message
;;;   condition-backtrace  condition-field  condition-is-a?
;;;   restart?    restart-name    restart-description
;;;   %make-condition  %condition-type-register!
;;;   %signal  %handler-bind-1  %with-restarts
;;;   %invoke-restart  %find-restart  %make-restart
;;;
;;; This module provides the high-level sugar on top of those primitives.

(define-library (curry conditions)
  (import (scheme base))
  (export
    define-condition
    make-condition make-condition*
    signal warn condition-error
    handler-bind %handler-bind-nest
    with-restarts
    invoke-restart find-restart
    handler-case
    ignore-errors)
  (begin

;;; ---- Root condition type hierarchy ----

(%condition-type-register! 'condition         '())
(%condition-type-register! 'error             '(condition))
(%condition-type-register! 'warning           '(condition))
(%condition-type-register! 'simple-error      '(error))
(%condition-type-register! 'simple-warning    '(warning))
(%condition-type-register! 'file-error        '(error))
(%condition-type-register! 'read-error        '(error))
(%condition-type-register! 'type-error        '(error))
(%condition-type-register! 'arity-error       '(error))
(%condition-type-register! 'unbound-variable  '(error))
(%condition-type-register! 'division-by-zero  '(error))
(%condition-type-register! 'math-error        '(error))
(%condition-type-register! 'singular-matrix   '(math-error))
(%condition-type-register! 'no-elementary-form '(math-error))
(%condition-type-register! 'gc-pressure       '(warning))

;;; ---- define-condition ----
;;;
;;; (define-condition name (parent ...) #:fields (field ...))
;;; (define-condition name (parent ...))
;;;
;;; Registers the type and the expected field names (for documentation).
;;; For construction use make-condition; for access use condition-field.

(define-syntax define-condition
  (syntax-rules ()
    ((_ name (parent ...) #:fields (field ...))
     (%condition-type-register! 'name '(parent ...)))
    ((_ name (parent ...))
     (%condition-type-register! 'name '(parent ...)))))

;;; ---- make-condition ----
;;;
;;; Two call forms:
;;;
;;;   (make-condition 'type)
;;;   (make-condition 'type fields-alist)
;;;   (make-condition 'type fields-alist message-string)
;;;
;;; Fields alist: ((field-name . value) ...)

(define (make-condition type-sym . rest)
  (let ((fields  (if (pair? rest) (car rest) '()))
        (message (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) #f)))
    (%make-condition type-sym fields message)))

;;; Convenience: build fields alist from a flat list of (sym val ...) pairs
;;; (make-condition* 'type 'field1 val1 'field2 val2 ...)
(define (make-condition* type-sym . kvs)
  (define (parse kvs acc)
    (if (null? kvs) (reverse acc)
        (parse (cddr kvs) (cons (cons (car kvs) (cadr kvs)) acc))))
  (%make-condition type-sym (parse kvs '()) #f))

;;; ---- signal / warn / condition-error ----

;;; (signal cond) — walk non-unwinding handlers; return normally if none claim it
(define (signal cond)
  (%signal cond))

;;; (warn type fields message) — signal only, never unwinds
(define (warn type-sym . rest)
  (signal (apply make-condition type-sym rest)))

;;; (condition-error type fields message) — signal first, then raise if unclaimed
;;; Note: use 'error for the standard R7RS error procedure; this is the CL form.
(define (condition-error type-sym . rest)
  (let ((c (apply make-condition type-sym rest)))
    (signal c)          ; handlers may invoke-restart and never return here
    (raise c)))         ; if signal returns, no handler claimed it — unwind

;;; ---- handler-bind ----
;;;
;;; (handler-bind
;;;   ((type-sym handler-proc)
;;;    ...)
;;;   body ...)
;;;
;;; Handlers are called WITHOUT unwinding the stack.  If a handler returns
;;; normally the next handler in the chain is tried.  If it calls
;;; invoke-restart, execution jumps to the matching with-restarts frame.

(define-syntax handler-bind
  (syntax-rules ()
    ((_ ((t h) ...) body ...)
     (%handler-bind-nest (list (cons t h) ...) (lambda () body ...)))))

(define (%handler-bind-nest bindings thunk)
  (if (null? bindings)
      (thunk)
      (%handler-bind-1
        (caar bindings)
        (cdar bindings)
        (lambda () (%handler-bind-nest (cdr bindings) thunk)))))

;;; ---- with-restarts ----
;;;
;;; (with-restarts
;;;   ((name "description" body ...)
;;;    ...)
;;;   form ...)
;;;
;;; Establishes named recovery points reachable via invoke-restart.
;;; Returns the result of form ... or of the chosen restart's body.

;;; Uses recursion rather than nested ... to avoid a curry syntax-rules
;;; limitation with nested ellipsis expansion.
(define-syntax with-restarts
  (syntax-rules ()
    ((_ () body ...)
     (begin body ...))
    ((_ ((rname rdesc rbody ...) rest ...) body ...)
     (%with-restarts
       (list (%make-restart 'rname rdesc (lambda () rbody ...)))
       (lambda () (with-restarts (rest ...) body ...))))))

;;; ---- invoke-restart / find-restart ----

(define (invoke-restart name)
  (%invoke-restart name))

(define (find-restart name)
  (%find-restart name))

;;; ---- handler-case ----
;;;
;;; (handler-case form
;;;   ((type var) body ...)
;;;   ...)
;;;
;;; Unwinding form: like guard but dispatches on condition type hierarchy.
;;; The var is bound to the condition object.

;;; Recursive expansion — one clause at a time, no nested ...
(define-syntax handler-case
  (syntax-rules ()
    ((_ form)
     form)
    ((_ form ((type var) body ...) rest ...)
     (guard (%hc-exn
             ((condition-is-a? %hc-exn 'type)
              (let ((var %hc-exn)) body ...))
             (#t (handler-case (raise %hc-exn) rest ...)))
       form))))

;;; ---- ignore-errors ----
;;;
;;; (ignore-errors body ...)
;;; → (values result #f)  on success
;;; → (values #f condition) on any error

(define-syntax ignore-errors
  (syntax-rules ()
    ((_ body ...)
     (guard (exn (#t (values #f exn)))
       (values (begin body ...) #f)))))

  )) ;; end begin, define-library
