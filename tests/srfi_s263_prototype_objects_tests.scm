;;; srfi_s263_prototype_objects_tests.scm — (srfi s263 prototype-objects)
;;;
;;; SRFI-263: Self-inspired prototype/message-passing object system.

(import (srfi s263 prototype-objects))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;;; ---- Value slots: message-send syntax, getter/setter ----

(define pointobj (*the-root-object* 'derive))
(pointobj 'set-value-slot! 'x 'set-x! 10)
(pointobj 'set-value-slot! 'y 'set-y! 20)
(check "value slot getter" (pointobj 'x) 10)
(pointobj 'set-x! 50)
(check "value slot setter" (pointobj 'x) 50)

;; getter-only value slot (no setter given) -- there's no setter message
;; name to send, so any attempt to "set" it is just an unhandled message
(pointobj 'set-value-slot! 'readonly 99)
(check "getter-only value slot" (pointobj 'readonly) 99)
(check "getter-only value slot has no settable message"
       (guard (e (#t 'not-understood)) (pointobj 'set-readonly! 1))
       'not-understood)

;;; ---- Method slots ----

(define o (*the-root-object* 'derive))
(o 'set-value-slot! 'constant 'set-constant! 5)
(o 'set-method-slot! 'add
  (lambda (self resend summand) (+ summand (self 'constant))))
(check "method slot" (o 'add 10) 15)

;;; ---- define-object / define-method sugar ----

(define-object sugar-obj (*the-root-object*)
  (constant set-constant! 5)
  ((add self resend summand) (+ summand (self 'constant))))
(check "define-object value+method slots" (sugar-obj 'add 10) 15)

;;; ---- derive / inheritance / resend (no target-override) ----

(define-object base (*the-root-object*)
  ((greet self resend) "base"))
(define-object child (base)
  ((greet self resend) (string-append "child+" (resend #f))))
(check "inheritance + resend" (child 'greet) "child+base")

;;; ---- delete-slot! ----

(define-object deletable (*the-root-object*) (v 42))
(check "slot before delete" (deletable 'v) 42)
(deletable 'delete-slot! 'v)
(check "message-not-understood after delete-slot!"
       (guard (e (#t 'not-understood)) (deletable 'v))
       'not-understood)

;;; ---- copy: independent clone ----

(define orig (*the-root-object* 'derive))
(orig 'set-value-slot! 'v 'set-v! 1)
(define cp (orig 'copy))
(cp 'set-v! 99)
(check "copy is independent" (list (orig 'v) (cp 'v)) (list 1 99))

;;; ---- message-not-understood (default: raises) ----

(check "unhandled message raises by default"
       (guard (e (#t 'caught)) (o 'nonexistent-message))
       'caught)

;;; ---- message-not-understood is itself overridable ----

(define-object polite (*the-root-object*)
  ((message-not-understood self resend message args) 'shrug))
(check "message-not-understood overridable" (polite 'whatever) 'shrug)

;;; ---- Multiple inheritance: unambiguous message resolves ----

(define-object left (*the-root-object*)
  ((only-left self resend) 'L)
  ((shared self resend) 'left-shared))
(define-object right (*the-root-object*)
  ((shared self resend) 'right-shared))
(define-object diamond (left right))
(check "multiple inheritance, unambiguous message" (diamond 'only-left) 'L)

;;; ---- Multiple inheritance: ambiguous message raises ----

(check "ambiguous message raises"
       (guard (e (#t 'ambiguous-caught)) (diamond 'shared))
       'ambiguous-caught)

;;; ---- resend with an explicit target-override disambiguates ----

(define-method (diamond shared self resend) (resend right))
(check "resend target-override disambiguates" (diamond 'shared) 'right-shared)

;;; ---- Diamond convergence on a shared ancestor is NOT ambiguous ----
;;; (regression: this used to infinite-loop -- both parent branches of
;;; `diamond` converge on *the-root-object*'s own 'derive slot, which
;;; must be treated as one hit, not two.)

(check "diamond convergence on shared ancestor slot isn't ambiguous"
       (procedure? (diamond 'derive))
       #t)

;;; ---- Cyclic parent graphs don't hang or crash ----
;;; (regression: found by code review -- set-parent-slot! is public API,
;;; so nothing stops a cycle; unguarded recursion either stack-overflowed
;;; (native segfault) or, after the first fix, infinite-looped resending
;;; message-not-understood once the cycle severed the path to root.)

(define cyc-a (*the-root-object* 'derive))
(cyc-a 'set-parent-slot! 'parent #f cyc-a) ; self-cycle, also severs root
(check "self-cyclic parent: unhandled message raises (not hang/crash)"
       (guard (e (#t 'handled)) (cyc-a 'nonexistent-message))
       'handled)
(check "self-cyclic parent: root-severed fallback itself raises cleanly"
       (guard (e (#t 'handled)) ((cyc-a 'mirror) 'has-ancestor cyc-a))
       'handled)

(define cyc-x (*the-root-object* 'derive))
(define cyc-y (*the-root-object* 'derive))
(cyc-x 'set-parent-slot! 'buddy #f cyc-y)
(cyc-y 'set-parent-slot! 'buddy #f cyc-x) ; mutual cycle; root still reachable via each one's own `parent`
(check "mutual-cycle parent: unhandled message still raises cleanly"
       (guard (e (#t 'handled)) (cyc-x 'nonexistent-message))
       'handled)
(check "mutual-cycle parent: mirror has-ancestor terminates"
       ((cyc-x 'mirror) 'has-ancestor cyc-y)
       #t)
(check "mutual-cycle parent: full-ancestor-list terminates"
       (list? ((cyc-x 'mirror) 'full-ancestor-list))
       #t)

;;; ---- Mirror / reflection ----

(check "mirror has-ancestor (true)" ((child 'mirror) 'has-ancestor base) #t)
(check "mirror has-ancestor (false)" ((child 'mirror) 'has-ancestor polite) #f)
(check "mirror immediate-ancestor-list length" (length ((child 'mirror) 'immediate-ancestor-list)) 1)
(check "mirror full-ancestor-list reaches root"
       (if (memq *the-root-object* ((child 'mirror) 'full-ancestor-list)) #t #f)
       #t)
(check "mirror full-ancestor-list length" (length ((child 'mirror) 'full-ancestor-list)) 2)
(check "mirror slot? on a mirror-list entry"
       (slot? (car ((child 'mirror) 'immediate-slot-list)))
       #t)

;;; ---- slot reflection procedures ----

(let ((s (car ((sugar-obj 'mirror) 'immediate-slot-list))))
  (check "slot? predicate" (slot? s) #t)
  (check "slot-type is one of value/method/parent"
         (if (memq (slot-type s) '(value method parent)) #t #f)
         #t))

;;; ---- Summary ----

(newline)
(display "srfi-s263 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
