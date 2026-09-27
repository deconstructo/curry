;;; srfi_189_tests.scm — (srfi 189) Maybe and Either: Just/Nothing and
;;; Right/Left container types, their accessors, monadic operations,
;;; protocol conversions, map/fold/unfold, syntax forms, and trivalent
;;; logic.

(import (scheme base) (srfi 189))

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

;;; ---- basic predicates ----

(check "just? true for a Just" (just? (just 1)) #t)
(check "just? false for Nothing" (just? (nothing)) #f)
(check "nothing? true for Nothing" (nothing? (nothing)) #t)
(check "maybe? true for a Just" (maybe? (just 1)) #t)
(check "maybe? true for Nothing" (maybe? (nothing)) #t)
(check "maybe? false for a non-Maybe" (maybe? 5) #f)
(check "right? true for a Right" (right? (right 1)) #t)
(check "left? true for a Left" (left? (left 1)) #t)
(check "either? true for a Left" (either? (left 1)) #t)
(check "either? false for a non-Either" (either? 5) #f)

;;; ---- constructors ----

(check "just with zero payload values is still a Just, distinct from Nothing"
       (just? (just)) #t)
(check "list->just wraps a list as the payload" (maybe->list (list->just '(1 2 3))) '(1 2 3))
(check "list->left wraps a list as the payload" (either->list (list->left '(a b))) '())
(check "list->right wraps a list as the payload" (either->list (list->right '(1 2))) '(1 2))

;;; ---- Maybe/Either conversions ----

(check "maybe->either on a Just gives a Right with the same payload"
       (either->list (maybe->either (just 1 2) 'err)) '(1 2))
(check "maybe->either on Nothing gives a Left of the given objects"
       (left? (maybe->either (nothing) 'err)) #t)
(check "either->maybe on a Right gives a Just with the same payload"
       (maybe->list (either->maybe (right 1 2))) '(1 2))
(check "either->maybe on a Left gives Nothing" (nothing? (either->maybe (left 1))) #t)
(check "either-swap Right becomes Left with the same payload"
       (either->list (either-swap (right 1 2))) '())
(check "either-swap Left becomes Right with the same payload"
       (either->list (either-swap (left 1 2))) '(1 2))

;;; ---- equality ----

(check "maybe= true for two equal Justs" (maybe= equal? (just 1 2) (just 1 2)) #t)
(check "maybe= false for differing payloads" (maybe= equal? (just 1) (just 2)) #f)
(check "maybe= true for two Nothings" (maybe= equal? (nothing) (nothing)) #t)
(check "maybe= false for Just vs Nothing" (maybe= equal? (just 1) (nothing)) #f)
(check "maybe= over more than two arguments" (maybe= equal? (just 1) (just 1) (just 1)) #t)
(check "either= true for two equal Rights" (either= equal? (right 1) (right 1)) #t)
(check "either= false for Left vs Right" (either= equal? (left 1) (right 1)) #f)

;;; ---- accessors ----

(check "maybe-ref on a Just calls success on the payload"
       (maybe-ref (just 1 2) (lambda () 'fail) list) '(1 2))
(check "maybe-ref on Nothing calls failure with no arguments"
       (maybe-ref (nothing) (lambda () 'fail)) 'fail)
(check "maybe-ref's default success is values" (maybe-ref (just 5) (lambda () 'fail)) 5)
(check "either-ref on a Right calls success on the payload"
       (either-ref (right 1) (lambda (e) (list 'err e))) 1)
(check "either-ref on a Left calls failure ON THE PAYLOAD (not zero-arg)"
       (either-ref (left 'boom) (lambda (e) (list 'err e))) '(err boom))
(check "maybe-ref/default returns the payload for a Just"
       (maybe-ref/default (just 1) 'default) 1)
(check "maybe-ref/default returns the given default for Nothing"
       (maybe-ref/default (nothing) 'default) 'default)
(check "either-ref/default returns the given default for a Left"
       (either-ref/default (left 1) 'default) 'default)

;;; ---- monadic operations ----

(check "maybe-join unwraps a Just of exactly one Maybe"
       (maybe-ref (maybe-join (just (just 5))) (lambda () 'none)) 5)
(check "maybe-join on a Just of Nothing gives Nothing"
       (nothing? (maybe-join (just (nothing)))) #t)
(check "maybe-join raises on a Just of a non-Maybe payload"
       (guard (e (#t 'caught)) (maybe-join (just 5))) 'caught)

(define half (lambda (x) (if (even? x) (just (/ x 2)) (nothing))))
(check "maybe-compose chains monadic functions, short-circuiting on Nothing"
       (maybe-ref (maybe-bind (just 8) half half) (lambda () 'none)) 2)
(check "maybe-bind short-circuits when the chain hits an odd (Nothing) result"
       (nothing? (maybe-bind (just 6) half half)) #t)
(check "maybe-bind on Nothing is Nothing without calling anything"
       (nothing? (maybe-bind (nothing) half)) #t)

;;; ---- sequence-flavored operations ----

(check "maybe-length is 1 for a Just regardless of payload count" (maybe-length (just 1 2 3)) 1)
(check "maybe-length is 0 for Nothing" (maybe-length (nothing)) 0)
(check "either-length is 1 for a Right" (either-length (right 1)) 1)
(check "either-length is 0 for a Left" (either-length (left 1)) 0)

(check "maybe-filter keeps a Just satisfying the predicate"
       (maybe-ref (maybe-filter even? (just 4)) (lambda () 'none)) 4)
(check "maybe-filter turns a non-matching Just into Nothing"
       (nothing? (maybe-filter even? (just 3))) #t)
(check "maybe-remove keeps a Just failing the predicate"
       (maybe-ref (maybe-remove even? (just 3)) (lambda () 'none)) 3)
(check "either-filter keeps a matching Right" (either-ref (either-filter even? (right 4) 'bad) (lambda (e) e)) 4)
(check "either-filter turns a non-matching Right into the given Left"
       (either-ref (either-filter even? (right 3) 'bad) (lambda (e) e)) 'bad)

;;; ---- maybe-sequence / either-sequence ----

(check "maybe-sequence with the default aggregator wraps each single value in a list"
       (maybe-ref (maybe-sequence (list (just 1) (just 2) (just 3)) map) (lambda () 'none))
       '((1) (2) (3)))
(check "maybe-sequence with a custom aggregator flattens single-valued payloads"
       (maybe-ref (maybe-sequence (list (just 1) (just 2) (just 3)) map (lambda (x) x)) (lambda () 'none))
       '(1 2 3))
(check "maybe-sequence short-circuits to the first Nothing encountered"
       (nothing? (maybe-sequence (list (just 1) (nothing) (just 3)) map)) #t)
(check "either-sequence with a custom aggregator flattens single-valued payloads"
       (either-ref (either-sequence (list (right 1) (right 2)) map (lambda (x) x)) (lambda (e) 'bad)) '(1 2))
(check "either-sequence short-circuits to the first Left encountered"
       (left? (either-sequence (list (right 1) (left 'bad) (right 3)) map (lambda (x) x))) #t)

;;; ---- protocol conversions ----

(check "maybe->list on a Just" (maybe->list (just 1 2)) '(1 2))
(check "maybe->list on Nothing" (maybe->list (nothing)) '())
(check "list->maybe on a non-empty list" (nothing? (list->maybe '(1 2))) #f)
(check "list->maybe on the empty list gives Nothing" (nothing? (list->maybe '())) #t)

(check "maybe->truth on a Just returns its first value" (maybe->truth (just 5)) 5)
(check "maybe->truth on Nothing returns #f" (maybe->truth (nothing)) #f)
(check "truth->maybe on #f gives Nothing" (nothing? (truth->maybe #f)) #t)
(check "truth->maybe on a truthy value gives a Just of it" (maybe->truth (truth->maybe 5)) 5)

(check "maybe->list-truth on a Just returns the payload list" (maybe->list-truth (just 1 2)) '(1 2))
(check "maybe->list-truth on Nothing returns #f" (maybe->list-truth (nothing)) #f)
(check "list-truth->maybe on #f gives Nothing" (nothing? (list-truth->maybe #f)) #t)
(check "list-truth->maybe on a list gives a Just of it" (maybe->list-truth (list-truth->maybe '(1 2))) '(1 2))

(check "maybe->generation on Nothing gives the eof-object" (eof-object? (maybe->generation (nothing))) #t)
(check "maybe->generation on a Just gives its value" (maybe->generation (just 5)) 5)
(check "generation->maybe on eof-object gives Nothing" (nothing? (generation->maybe (eof-object))) #t)
(check "generation->maybe on any other value gives a Just" (maybe->truth (generation->maybe 5)) 5)

(check "maybe->values on a Just returns its payload as multiple values"
       (call-with-values (lambda () (maybe->values (just 1 2))) list) '(1 2))
(check "maybe->values on Nothing returns zero values"
       (call-with-values (lambda () (maybe->values (nothing))) list) '())
(check "values->maybe wraps a producer's multiple values into a Just"
       (maybe-ref (values->maybe (lambda () (values 1 2))) (lambda () 'none) list) '(1 2))
(check "values->maybe on a zero-value producer gives Nothing"
       (nothing? (values->maybe (lambda () (values)))) #t)

(check "maybe->two-values on a Just gives (value #t)"
       (call-with-values (lambda () (maybe->two-values (just 5))) list) '(5 #t))
(check "maybe->two-values on Nothing gives (#f #f)"
       (call-with-values (lambda () (maybe->two-values (nothing))) list) '(#f #f))
(check "two-values->maybe on (v #t) gives a Just of v"
       (maybe-ref (two-values->maybe (lambda () (values 5 #t))) (lambda () 'none)) 5)
(check "two-values->maybe on (#f #f) gives Nothing"
       (nothing? (two-values->maybe (lambda () (values #f #f)))) #t)

;;; ---- exception->either / either-guard ----

(check "exception->either wraps a matched exception as a Left"
       (left? (exception->either (lambda (e) #t) (lambda () (error "boom")))) #t)
(check "exception->either wraps a successful thunk's result as a Right"
       (either-ref (exception->either (lambda (e) #t) (lambda () 42)) (lambda (e) 'bad)) 42)
(check "exception->either re-raises when the predicate doesn't match"
       (guard (e (#t 'reraised)) (exception->either (lambda (e) #f) (lambda () (error "boom"))))
       'reraised)
(check "either-guard is exception->either with an inline body"
       (left? (either-guard (lambda (e) #t) (error "x"))) #t)

;;; ---- map / for-each / fold ----

(check "maybe-map transforms a Just's payload" (maybe-ref (maybe-map (lambda (x) (* x 10)) (just 5)) (lambda () 'none)) 50)
(check "maybe-map on Nothing is a no-op" (nothing? (maybe-map (lambda (x) (* x 10)) (nothing))) #t)
(let ((seen #f))
  (maybe-for-each (lambda (x) (set! seen x)) (just 7))
  (check "maybe-for-each calls proc on a Just's payload" seen 7))
(let ((seen #f))
  (maybe-for-each (lambda (x) (set! seen x)) (nothing))
  (check "maybe-for-each is a no-op for Nothing" seen #f))
(check "maybe-fold reduces a Just's payload with kons" (maybe-fold + 0 (just 5)) 5)
(check "maybe-fold returns nil for Nothing" (maybe-fold + 0 (nothing)) 0)

;;; ---- maybe-unfold / either-unfold ----
;;; The spec's own two-call-only design (see maybe-either.scm's own
;;; header comment): stop? is called at most twice, never looped.

(check "maybe-unfold returns Nothing when stop? is immediately true"
       (nothing? (maybe-unfold (lambda (n) #t) (lambda (n) n) (lambda (n) n) 0)) #t)
(check "maybe-unfold raises if stop? is still false after one successor step"
       (guard (e (#t 'caught)) (maybe-unfold (lambda (n) #f) (lambda (n) n) (lambda (n) (+ n 1)) 0))
       'caught)
(check "maybe-unfold applies mapper to the successor's result when stop? then succeeds"
       (maybe-ref (maybe-unfold (lambda (n) (= n 1)) (lambda (n) (* n 100)) (lambda (n) 1) 0) (lambda () 'err))
       100)
(check "either-unfold returns a Left of seeds when stop? is immediately true"
       (either-ref (either-unfold (lambda (n) #t) (lambda (n) n) (lambda (n) n) 42) (lambda (e) e))
       42)

;;; ---- syntax ----

(check "maybe-if on a Just evaluates just-expr" (maybe-if (just 5) 'yes 'no) 'yes)
(check "maybe-if on Nothing evaluates nothing-expr" (maybe-if (nothing) 'yes 'no) 'no)
(check "maybe-if raises on a non-Maybe" (guard (e (#t 'caught)) (maybe-if 5 'yes 'no)) 'caught)

(check "maybe-and short-circuits on Nothing" (nothing? (maybe-and (just 1) (nothing) (just 2))) #t)
(check "maybe-and returns the last Just when nothing short-circuits"
       (maybe-ref (maybe-and (just 1) (just 2)) (lambda () 'none)) 2)
(check "either-and short-circuits on Left" (left? (either-and (right 1) (left 'x) (right 2))) #t)

(check "maybe-or short-circuits on the first Just" (maybe-ref (maybe-or (nothing) (just 2) (just 3)) (lambda () 'none)) 2)
(check "maybe-or returns the last Nothing when everything is Nothing" (nothing? (maybe-or (nothing) (nothing))) #t)
(check "either-or short-circuits on the first Right" (either-ref (either-or (left 'x) (right 2)) (lambda (e) 'bad)) 2)

(check "maybe-let* binds and threads values through successive claws"
       (maybe-ref (maybe-let* ((x (just 1)) (y (just 2))) (just (+ x y))) (lambda () 'none)) 3)
(check "maybe-let* short-circuits on a Nothing claw"
       (nothing? (maybe-let* ((x (just 1)) (y (nothing))) (just (+ x y)))) #t)
(check "maybe-let* supports a (expr)-shaped claw for its side-effecting short-circuit only"
       (maybe-ref (maybe-let* (((just 'ignored)) (x (just 5))) (just x)) (lambda () 'none)) 5)
(check "maybe-let* supports a bare-identifier claw"
       (let ((m (just 9))) (maybe-ref (maybe-let* (m) (just m)) (lambda () 'none))) 9)
(check "either-let* binds and threads values, short-circuiting on Left"
       (left? (either-let* ((x (right 1)) (y (left 'bad))) (right (+ x y)))) #t)

(check "maybe-let*-values binds multiple payload values via a formals list"
       (maybe-ref (maybe-let*-values (((a b) (just 1 2))) (just (+ a b))) (lambda () 'none)) 3)
(check "either-let*-values binds multiple payload values via a formals list"
       (either-ref (either-let*-values (((a b) (right 1 2))) (right (+ a b))) (lambda (e) 'bad)) 3)

;;; ---- trivalent logic ----

(check "tri-not flips a Just #t to Just #f" (maybe-ref (tri-not (just #t)) (lambda () 'none)) #f)
(check "tri-not flips a Just #f to Just #t" (maybe-ref (tri-not (just #f)) (lambda () 'none)) #t)
(check "tri-not on Nothing is Nothing" (nothing? (tri-not (nothing))) #t)

(check "tri=? true when all Justs share the same value" (maybe-ref (tri=? (just #t) (just #t)) (lambda () 'none)) #t)
(check "tri=? false when values differ" (maybe-ref (tri=? (just #t) (just #f)) (lambda () 'none)) #f)
(check "tri=? true when all Nothing" (maybe-ref (tri=? (nothing) (nothing)) (lambda () 'none)) #t)
(check "tri=? false when Just and Nothing are mixed" (maybe-ref (tri=? (just #t) (nothing)) (lambda () 'none)) #f)

(check "tri-and returns Just #t when everything is true"
       (maybe-ref (tri-and (just #t) (just #t)) (lambda () 'none)) #t)
(check "tri-and returns the first Nothing encountered" (nothing? (tri-and (just #t) (nothing))) #t)
(check "tri-and returns the first false Just encountered"
       (maybe-ref (tri-and (just #t) (just #f) (nothing)) (lambda () 'none)) #f)

(check "tri-or returns the first true Just encountered"
       (maybe-ref (tri-or (just #f) (just #t)) (lambda () 'none)) #t)
(check "tri-or returns Just #f when everything is false"
       (maybe-ref (tri-or (just #f) (just #f)) (lambda () 'none)) #f)
(check "tri-or returns the first Nothing if nothing true precedes it"
       (nothing? (tri-or (just #f) (nothing) (just #t))) #t)

(check "tri-merge returns the first Just regardless of its value"
       (maybe-ref (tri-merge (nothing) (just #f) (just #t)) (lambda () 'none)) #f)
(check "tri-merge returns Nothing when everything is Nothing"
       (nothing? (tri-merge (nothing) (nothing))) #t)

;;; ---- Summary ----

(newline)
(display "srfi-189 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
