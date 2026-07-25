;;; Basic R7RS conformance tests for Curry Scheme

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; Booleans
(check "not #f" (not #f) #t)
(check "not #t" (not #t) #f)
(check "boolean=?" (boolean=? #t #t) #t)

;;; Numbers
(check "exact->inexact" (inexact 1/3) (/ 1.0 3.0))
(check "floor" (floor 3.7) 3.0)
(check "ceiling" (ceiling 3.2) 4.0)
(check "round half-even" (round 2.5) 2.0)
(check "round 3.5" (round 3.5) 4.0)
(check "gcd" (gcd 32 -36) 4)
(check "lcm" (lcm 32 -36) 288)
(check "quotient" (quotient 13 4) 3)
(check "remainder" (remainder -13 4) -1)
(check "modulo" (modulo -13 4) 3)
(check "expt" (expt 2 10) 1024)
(check "sqrt exact" (sqrt 4) 2)
(check "number->string" (number->string 255 16) "ff")
(check "string->number" (string->number "ff" 16) 255)

;;; Arithmetic
(check "+" (+ 1 2 3) 6)
(check "*" (* 2 3 4) 24)
(check "-" (- 10 3 2) 5)
(check "/" (/ 10 2) 5)
(check "mixed exact/inexact" (exact? (* 2 3)) #t)

;;; Characters
(check "char->integer" (char->integer #\A) 65)
(check "integer->char" (integer->char 65) #\A)
(check "integer->char negative raises instead of a bogus codepoint"
       (guard (exn (#t 'raised)) (integer->char -1)) 'raised)
(check "integer->char beyond Unicode range raises"
       (guard (exn (#t 'raised)) (integer->char #x110000)) 'raised)
(check "integer->char surrogate raises"
       (guard (exn (#t 'raised)) (integer->char #xD800)) 'raised)
(check "char-upcase" (char-upcase #\a) #\A)
(check "char-alphabetic?" (char-alphabetic? #\a) #t)

;;; Unicode character classification and case mapping (beyond ASCII/Latin-1
;;; ctype.h range — see docs/reference issue on char-alphabetic? etc.
;;; misusing locale-dependent <ctype.h> for full Unicode codepoints)
(check "char-alphabetic? Greek"       (char-alphabetic? (integer->char 955)) #t)   ; λ
(check "char-alphabetic? Latin-1"     (char-alphabetic? #\é) #t)
(check "char-numeric? Arabic-Indic"   (char-numeric? (integer->char 1635)) #t)     ; ٣ (3)
(check "char-upcase Greek"            (char-upcase (integer->char 955)) (integer->char 923))   ; λ -> Λ
(check "char-downcase Greek"          (char-downcase (integer->char 923)) (integer->char 955))  ; Λ -> λ
(check "char-alphabetic? Cuneiform"   (char-alphabetic? (integer->char #x12000)) #t) ; 𒀀 (category Lo)
(check "char-foldcase Kelvin sign"    (char-foldcase (integer->char 8490)) #\k)      ; U+212A -> k
(check "char-upcase literal reads full codepoint"
       (char->integer #\λ) 955)

;;; Strings
(check "string-length" (string-length "hello") 5)
(check "string-ref" (string-ref "hello" 1) #\e)
(check "string-ref multibyte" (string-ref "héllo" 1) #\é)
(check "string-ref negative index raises"
       (guard (exn (#t 'raised)) (string-ref "hello" -1)) 'raised)
(check "string-ref out-of-range index raises"
       (guard (exn (#t 'raised)) (string-ref "hello" 5)) 'raised)
(check "string-ref empty string raises"
       (guard (exn (#t 'raised)) (string-ref "" 0)) 'raised)
(check "string-set! basic"
       (let ((s (string-copy "hello"))) (string-set! s 0 #\H) s) "Hello")
(check "string-set! multibyte"
       (let ((s (string-copy "héllo"))) (string-set! s 1 #\e) s) "hello")
(check "string-set! negative index raises"
       (guard (exn (#t 'raised))
         (let ((s (string-copy "hello"))) (string-set! s -1 #\z) 'no-error))
       'raised)
(check "string-set! out-of-range index raises"
       (guard (exn (#t 'raised))
         (let ((s (string-copy "hello"))) (string-set! s 5 #\z) 'no-error))
       'raised)
(check "string-set! out-of-range doesn't corrupt the string"
       (let ((s (string-copy "hello")))
         (guard (exn (#t #t)) (string-set! s 5 #\z))
         s)
       "hello")
(check "substring" (substring "hello" 1 3) "el")
(check "substring out-of-range end raises"
       (guard (exn (#t 'raised)) (substring "hello" 0 1000)) 'raised)
(check "substring negative start raises"
       (guard (exn (#t 'raised)) (substring "hello" -1 3)) 'raised)
(check "string->list" (string->list "abc") '(#\a #\b #\c))
(check "string->list out-of-range end raises"
       (guard (exn (#t 'raised)) (string->list "abc" 0 100)) 'raised)
(check "string-copy out-of-range raises instead of crashing"
       (guard (exn (#t 'raised)) (string-copy "abc" 1000 2)) 'raised)
(check "string-copy! negative at raises instead of corrupting"
       (guard (exn (#t 'raised))
         (let ((s (string-copy "hello"))) (string-copy! s -1 "X") 'no-error))
       'raised)
(check "string-copy! basic still works"
       (let ((s (string-copy "hello"))) (string-copy! s 1 "XY") s) "hXYlo")
(check "string-fill! negative start raises"
       (guard (exn (#t 'raised))
         (let ((s (string-copy "hello"))) (string-fill! s #\z -1) 'no-error))
       'raised)
(check "string-fill! basic still works"
       (let ((s (string-copy "hello"))) (string-fill! s #\z 1 3) s) "hzzlo")
(check "write-string out-of-range raises"
       (guard (exn (#t 'raised))
         (write-string "abc" (open-output-string) 0 100)) 'raised)
(check "string->utf8 out-of-range raises"
       (guard (exn (#t 'raised)) (string->utf8 "abc" 0 100)) 'raised)
(check "utf8->string out-of-range raises"
       (guard (exn (#t 'raised)) (utf8->string (string->utf8 "abc") 0 100)) 'raised)
(check "utf8->string basic still works"
       (utf8->string (string->utf8 "abc")) "abc")
(check "list->string" (list->string '(#\h #\i)) "hi")
(check "string->symbol" (string->symbol "foo") 'foo)
(check "symbol->string" (symbol->string 'bar) "bar")

;;; Pairs
(check "car" (car '(1 2 3)) 1)
(check "cdr" (cdr '(1 2 3)) '(2 3))
(check "cadr" (cadr '(1 2 3)) 2)
(check "list-tail" (list-tail '(a b c d) 2) '(c d))
(check "list-ref" (list-ref '(a b c) 1) 'b)
(check "assoc" (assoc 2 '((1 a) (2 b) (3 c))) '(2 b))
(check "member" (member 2 '(1 2 3)) '(2 3))
(check "list? proper" (list? '(1 2 3)) #t)
(check "list? empty" (list? '()) #t)
(check "list? dotted" (list? '(1 . 2)) #f)
(check "list? non-pair" (list? 5) #f)
(define circ (list 1 2 3))
(set-cdr! (cddr circ) circ)
(check "list? circular" (list? circ) #f)
(define circ1 (list 1))
(set-cdr! circ1 circ1)
(check "list? self-loop" (list? circ1) #f)
(check "length circular raises"
       (call/cc (lambda (k)
         (with-exception-handler
           (lambda (e) (k 'raised))
           (lambda () (length circ)))))
       'raised)

;;; Vectors
(define v (make-vector 3 0))
(vector-set! v 1 42)
(check "vector-ref" (vector-ref v 1) 42)
(check "vector-length" (vector-length v) 3)
(check "vector->list" (vector->list '#(1 2 3)) '(1 2 3))

;;; Control
(check "values" (call-with-values (lambda () (values 1 2)) +) 3)

;;; receive (R7RS sugar over call-with-values, compiled natively)
(check "receive proper formals"
  (receive (a b) (values 1 2) (+ a b)) 3)
(check "receive dotted formals"
  (receive (a . rest) (values 1 2 3) (list a rest)) '(1 (2 3)))
(check "receive rest-only formals"
  (receive all (values 1 2 3) all) '(1 2 3))
(check "receive single value"
  (receive (a) (values 42) a) 42)
(check "receive tail position"
  (let loop ((n 3) (acc 0))
    (if (= n 0) acc
        (receive (n1) (values (- n 1))
          (loop n1 (+ acc n))))) 6)

;;; apply/call with more than 64 arguments — regression coverage for a bug
;;; found in a full-codebase audit: apply()'s BcClosure/primitive/symbolic-
;;; function branches (runtime.c) and the tree-walker's own function-
;;; application dispatch (eval.c) all used a fixed 64-slot array. For
;;; runtime.c's apply(), called INDIRECTLY (as a value, not the literal
;;; `(apply f args)` the compiler special-cases into a safe OP_APPLY),
;;; the BcClosure/symbolic-function paths read past the end of that
;;; buffer — straight off the C stack — and used the garbage as real
;;; argument values; the primitive path silently truncated to 64 args
;;; with no error. The tree-walker's own dispatch was worse: it STOPPED
;;; EVALUATING argument expressions past the 64th, silently dropping
;;; their side effects too, with no error, reachable via -l/load or a
;;; library body (the only places the tree-walker still runs).
;;; `ap` (a variable, not the literal symbol `apply`) forces every call
;;; below through the INDIRECT path — the compiler only special-cases a
;;; literal `(apply f args)` call site into a safe, correctly-sized
;;; OP_APPLY; going through a variable reference is exactly the case that
;;; was vulnerable.
(define ap apply)
(define (variadic-length . xs) (length xs))
(define (range-from-1 n)
  (let loop ((i n) (acc '())) (if (= i 0) acc (loop (- i 1) (cons i acc)))))
(check ">64 args via indirect apply: correct count"
  (ap variadic-length (range-from-1 100))
  100)
(check ">64 args via indirect apply: every value preserved in order"
  (ap (lambda args args) (range-from-1 100))
  (range-from-1 100))
(check ">64 args to a variadic primitive via indirect apply"
  (ap + (range-from-1 100))
  5050)

;;; Let forms
(check "let" (let ((x 1) (y 2)) (+ x y)) 3)
(check "let*" (let* ((x 1) (y (+ x 1))) y) 2)
(check "letrec" (letrec ((even? (lambda (n) (if (= n 0) #t (odd? (- n 1)))))
                          (odd?  (lambda (n) (if (= n 0) #f (even? (- n 1))))))
                   (even? 10)) #t)
(check "named let" (let loop ((n 5) (acc 1))
                     (if (= n 0) acc (loop (- n 1) (* acc n)))) 120)

;;; Tail calls
(define (count-down n)
  (if (= n 0) 'done (count-down (- n 1))))
(check "tail recursion" (count-down 1000000) 'done)

;;; Variadic procedures and arity checking
;;; Regression coverage for a bug where the VM's OP_CALL/OP_TAIL_CALL never
;;; checked argument count: a fixed-arity closure called with the wrong
;;; number of args silently read uninitialized stack slots, and a variadic
;;; closure's rest parameter ended up holding the raw last argument instead
;;; of a proper list.
(define (rest-of a . rest) rest)
(check "variadic: rest param collects trailing args"
       (rest-of 1 2 3 4) '(2 3 4))
(check "variadic: rest param empty when no extra args"
       (rest-of 1) '())
(define (all-args . args) args)
(check "variadic: pure rest (no fixed params)"
       (all-args 1 2 3) '(1 2 3))
(check "variadic: pure rest with zero args"
       (all-args) '())
(check "variadic: apply with rest param"
       (apply rest-of '(1 2 3 4 5)) '(2 3 4 5))
(define (variadic-tail-sum n . acc)
  (if (= n 0) (if (null? acc) 0 (car acc))
      (variadic-tail-sum (- n 1) (+ n (if (null? acc) 0 (car acc))))))
(check "variadic: tail call re-collects rest each iteration"
       (variadic-tail-sum 100) 5050)

(define (exact-two x y) (+ x y))
(check "arity: exact match still works"
       (exact-two 1 2) 3)
(check "arity: too few arguments raises wrong-number-of-arguments"
       (guard (e (#t (error-object-code e))) (exact-two 1))
       'wrong-number-of-arguments)
(check "arity: too many arguments raises wrong-number-of-arguments"
       (guard (e (#t (error-object-code e))) (exact-two 1 2 3))
       'wrong-number-of-arguments)
(define (need-two-rest a b . rest) (list a b rest))
(check "arity: variadic too few required args raises"
       (guard (e (#t (error-object-code e))) (need-two-rest 1))
       'wrong-number-of-arguments)
(check "arity: variadic exact required args, empty rest"
       (need-two-rest 1 2) '(1 2 ()))

;;; Quasiquote
(let ((x 42))
  (check "quasiquote" `(a ,x c) '(a 42 c))
  (check "quasiquote splicing" `(a ,@(list 1 2) c) '(a 1 2 c)))

;;; do loop
(check "do" (let ((result '()))
              (do ((i 0 (+ i 1)))
                  ((= i 5) (reverse result))
                (set! result (cons i result))))
            '(0 1 2 3 4))

;;; Dynamic binding
(define p (make-parameter 10))
(check "parameter" (p) 10)
(parameterize ((p 99))
  (check "parameterize" (p) 99))
(check "parameterize restored" (p) 10)

;;; Exception handling
(check "guard" (guard (e (#t 'caught)) (error "oops")) 'caught)
(check "with-exception-handler"
       (with-exception-handler
         (lambda (e) 'handled)
         (lambda () (raise 'boom)))
       'handled)

;;; Sets
(define s (make-set))
(set-add! s 1)
(set-add! s 2)
(set-add! s 3)
(check "set-member?" (set-member? s 2) #t)
(check "set-size" (set-size s) 3)
(define s2 (list->set '(3 4 5)))
(check "set-intersection" (set-size (set-intersection s s2)) 1)

;;; Records
(define-record-type <person>
  (make-person name age)
  person?
  (name person-name)
  (age  person-age set-person-age!))

(define alice (make-person "Alice" 30))
(check "record?" (person? alice) #t)
(check "record-accessor" (person-name alice) "Alice")
(set-person-age! alice 31)
(check "record-mutator" (person-age alice) 31)

;;; define-record-type as an internal (local) definition — compiled natively,
;;; must stay local to the enclosing lambda rather than leaking to the
;;; global environment.
(define (make-local-record-counter)
  (define-record-type <ctr>
    (mk-ctr n)
    ctr?
    (n ctr-n set-ctr-n!))
  (define c (mk-ctr 0))
  (lambda ()
    (set-ctr-n! c (+ (ctr-n c) 1))
    (ctr-n c)))
(define counter1 (make-local-record-counter))
(check "local define-record-type: first call"  (counter1) 1)
(check "local define-record-type: second call" (counter1) 2)
(check "local define-record-type: ctor not leaked to global"
  (guard (e (#t 'unbound)) (mk-ctr 0) 'leaked)
  'unbound)

;;; %make-record-type: internal primitive define-record-type's codegen uses
;;; to reconstruct the RTD at runtime (see compile_define_record_type in
;;; compiler.c). It's an ordinary, discoverable global primitive, so direct
;;; misuse must raise a normal Scheme error rather than crash.
(check "%make-record-type with non-list fields raises cleanly, no crash"
  (guard (e (#t 'caught)) (%make-record-type 'foo 5))
  'caught)
(check "%make-record-type with improper list fields raises cleanly, no crash"
  (guard (e (#t 'caught)) (%make-record-type 'foo (cons 1 2)))
  'caught)

;;; Numeric: floor/ceiling/truncate/round on rationals (R7RS exact)
(check "floor rational"    (floor 13/4)     3)
(check "floor neg rational" (floor -13/4)  -4)
(check "ceiling rational"  (ceiling 13/4)   4)
(check "ceiling neg"       (ceiling -13/4) -3)
(check "truncate rational" (truncate 7/2)   3)
(check "truncate neg"      (truncate -7/2) -3)
(check "round half-even rational" (round 7/2) 4)
(check "round half-even rational 2" (round 5/2) 2)

;;; Numeric: floor-quotient, floor-remainder, floor/
(check "floor-quotient pos"  (floor-quotient  13  4)  3)
(check "floor-quotient neg"  (floor-quotient -13  4) -4)
(check "floor-remainder pos" (floor-remainder 13  4)  1)
(check "floor-remainder neg" (floor-remainder -13 4)  3)
(call-with-values (lambda () (floor/ 13 4))
  (lambda (q r) (check "floor/ quotient" q 3)
                (check "floor/ remainder" r 1)))

;;; Numeric: numerator / denominator
(check "numerator"   (numerator 3/4) 3)
(check "denominator" (denominator 3/4) 4)
(check "numerator exact int" (numerator 5) 5)
(check "denominator exact int" (denominator 5) 1)

;;; Numeric: min / max
(check "max" (max 1 3 2) 3)
(check "min" (min 3 1 2) 1)
(check "max inexact" (exact? (max 1 2.0)) #f)

;;; Numeric: atan 2-arg, log 2-arg
(check "atan 2-arg" (< (abs (- (atan 1.0 1.0) (/ (* 4 (atan 1.0)) 4))) 1e-10) #t)
(check "log base 2" (< (abs (- (log 8.0 2.0) 3.0)) 1e-10) #t)

;;; Numeric: nan? infinite? finite?
(check "nan?"      (nan? +nan.0)  #t)
(check "nan? no"   (nan? 1.0)     #f)
(check "infinite?" (infinite? +inf.0) #t)
(check "infinite? neg" (infinite? -inf.0) #t)
(check "finite?"   (finite? 1.0)  #t)
(check "finite? inf no" (finite? +inf.0) #f)

;;; Characters
(check "char-downcase"    (char-downcase #\A) #\a)
(check "char-upper-case?" (char-upper-case? #\A) #t)
(check "char-lower-case?" (char-lower-case? #\a) #t)
(check "char-numeric?"    (char-numeric? #\5) #t)
(check "char-whitespace?" (char-whitespace? #\space) #t)
(check "char=?"  (char=? #\a #\a) #t)
(check "char<?"  (char<? #\a #\b) #t)

;;; Strings
(check "string=?"       (string=? "abc" "abc") #t)
(check "string<?"       (string<? "abc" "abd") #t)
(check "string-contains" (string-contains "hello world" "world") 6)
(check "string-contains miss" (string-contains "hello" "xyz") #f)
(check "make-string"    (make-string 3 #\x) "xxx")
(check "make-string negative size raises instead of crashing"
       (guard (exn (#t 'raised)) (make-string -1)) 'raised)
(check "string"         (string #\h #\i) "hi")
(check "string-copy"    (string-copy "hello") "hello")

;;; Lists
(check "list-copy isolated" (let ((orig '(1 2 3)))
                              (let ((c (list-copy orig)))
                                (set-car! c 99)
                                (car orig)))
                            1)
(check "list-head"  (list-head '(a b c d) 2) '(a b))
(check "memq"  (memq 'b '(a b c)) '(b c))
(check "memv"  (memv 2 '(1 2 3)) '(2 3))
(check "assq"  (assq 'b '((a 1) (b 2) (c 3))) '(b 2))
(check "assv"  (assv 2 '((1 a) (2 b) (3 c))) '(2 b))
(check "list*" (list* 1 2 '(3 4)) '(1 2 3 4))

;;; Vectors
(define v2 (vector 10 20 30))
(check "vector literal" (vector-ref v2 1) 20)
(check "list->vector" (list->vector '(1 2 3)) '#(1 2 3))
(check "vector-copy" (vector-copy '#(1 2 3 4) 1 3) '#(2 3))
(let ((v3 (make-vector 3 0)))
  (vector-fill! v3 7 0 2)
  (check "vector-fill!" (vector->list v3) '(7 7 0)))
(check "vector-copy out-of-range raises instead of crashing"
       (guard (exn (#t 'raised)) (vector-copy (vector 1 2 3) 1000 2)) 'raised)
(check "vector->list out-of-range raises instead of leaking heap contents"
       (guard (exn (#t 'raised)) (vector->list (vector 1 2 3) 0 100)) 'raised)
(check "vector-fill! out-of-range raises instead of crashing"
       (guard (exn (#t 'raised)) (vector-fill! (vector 1 2 3) 99 0 100)) 'raised)
(check "vector-copy! negative at raises instead of crashing"
       (guard (exn (#t 'raised)) (vector-copy! (vector 1 2 3) -1 (vector 9 9))) 'raised)
(check "vector-copy! basic still works"
       (let ((v (vector 1 2 3))) (vector-copy! v 0 (vector 9 8)) v) '#(9 8 3))

;;; Bytevectors
(check "make-bytevector negative size raises instead of crashing"
       (guard (exn (#t 'raised)) (make-bytevector -1)) 'raised)
(define bv (make-bytevector 4 0))
(bytevector-u8-set! bv 2 255)
(check "bytevector-u8-ref"  (bytevector-u8-ref bv 2) 255)
(check "bytevector-u8-ref 0" (bytevector-u8-ref bv 0) 0)
(check "bytevector-length"  (bytevector-length bv) 4)
(check "bytevector?"  (bytevector? bv) #t)
(check "bytevector? no" (bytevector? "str") #f)
(check "bytevector-copy out-of-range raises instead of crashing"
       (guard (exn (#t 'raised)) (bytevector-copy (bytevector 1 2 3) 1000 2)) 'raised)
(check "bytevector-copy! negative at raises instead of crashing"
       (guard (exn (#t 'raised)) (bytevector-copy! (bytevector 1 2 3) -1 (bytevector 9 9))) 'raised)
(check "bytevector-copy! basic still works"
       (let ((b (bytevector 1 2 3))) (bytevector-copy! b 0 (bytevector 9 8))
            (list (bytevector-u8-ref b 0) (bytevector-u8-ref b 1) (bytevector-u8-ref b 2)))
       '(9 8 3))
(check "read-bytevector! out-of-range raises"
       (guard (exn (#t 'raised))
         (read-bytevector! (make-bytevector 4 0) (open-input-string "ab") 0 100))
       'raised)
(check "write-bytevector out-of-range raises"
       (guard (exn (#t 'raised)) (write-bytevector (bytevector 1 2 3) (open-output-string) 0 100))
       'raised)
(check "read-bytevector negative size raises instead of crashing"
       (guard (exn (#t 'raised)) (read-bytevector -1)) 'raised)
(check "read-bytevector basic still works"
       (bytevector-u8-ref (read-bytevector 2 (open-input-string "ab")) 0) 97)

;;; Predicates
(check "vector?"    (vector? '#(1 2)) #t)
(check "procedure?" (procedure? car) #t)
(check "port?"      (port? (open-input-string "")) #t)
(check "promise?"   (promise? (make-promise 42)) #t)
(check "set?"       (set? (make-set)) #t)
(check "hash-table?" (hash-table? (make-hash-table)) #t)

;;; apply / fold-left / for-each
(check "apply"     (apply + '(1 2 3 4)) 10)
(check "apply 2"   (apply string (list #\a #\b #\c)) "abc")
(check "fold-left" (fold-left + 0 '(1 2 3 4 5)) 15)
(check "for-each"
  (let ((acc '()))
    (for-each (lambda (x) (set! acc (cons (* x x) acc))) '(1 2 3))
    (reverse acc))
  '(1 4 9))

;;; String ports / I/O
(check "open-input-string read"
  (read (open-input-string "(+ 1 2)"))
  '(+ 1 2))
(check "read-char"
  (read-char (open-input-string "abc"))
  #\a)
(check "peek-char"
  (let ((p (open-input-string "abc")))
    (let ((c (peek-char p)))
      (list c (read-char p))))
  (list #\a #\a))
(check "read-line"
  (read-line (open-input-string "hello\nworld"))
  "hello")
(check "with-output-to-string"
  (with-output-to-string (lambda () (display 42) (display "!")))
  "42!")
(check "open-output-string"
  (let ((p (open-output-string)))
    (display "foo" p)
    (display "bar" p)
    (get-output-string p))
  "foobar")
(check "input-port?"  (input-port?  (open-input-string "")) #t)
(check "output-port?" (output-port? (open-output-string)) #t)

;;; Error objects
(define captured-error
  (guard (e (#t e)) (error "bad input" 42 "extra")))
(check "error-object?"         (error-object? captured-error) #t)
(check "error-message"         (error-message captured-error) "bad input")
(check "error-object-irritants" (error-object-irritants captured-error) '(42 "extra"))
(check "error-object? non-err" (error-object? 42) #f)
(check "raise-continuable"
  (with-exception-handler
    (lambda (e) (+ e 1))
    (lambda () (raise-continuable 41))
    )
  42)

;;; gensym
(check "gensym unique" (eq? (gensym) (gensym)) #f)
(check "gensym symbol?" (symbol? (gensym)) #t)

;;; Set operations (extended)
(define sa (list->set '(1 2 3 4)))
(define sb (list->set '(3 4 5 6)))
(check "set-union size"      (set-size (set-union sa sb)) 6)
(check "set-difference size" (set-size (set-difference sa sb)) 2)
(check "set-subset? yes"     (set-subset? (list->set '(1 2)) sa) #t)
(check "set-subset? no"      (set-subset? (list->set '(1 5)) sa) #f)
(define sc (make-set))
(set-add! sc 1) (set-add! sc 2) (set-add! sc 3)
(set-delete! sc 2)
(check "set-delete!" (set-member? sc 2) #f)
(check "set->list length" (length (set->list sc)) 2)

;;; Hash table (extended)
(define h (make-hash-table))
(hash-table-set! h "x" 10)
(hash-table-set! h "y" 20)
(check "hash-table-exists? yes" (hash-table-exists? h "x") #t)
(check "hash-table-exists? no"  (hash-table-exists? h "z") #f)
(check "hash-table-size" (hash-table-size h) 2)
(hash-table-delete! h "x")
(check "hash-table-delete!" (hash-table-exists? h "x") #f)
(check "hash-table-keys"   (length (hash-table-keys h)) 1)
(check "hash-table-values" (hash-table-values h) '(20))

;;; Force / promises
(define p-lazy (make-promise (+ 1 2)))
(check "force promise" (force p-lazy) 3)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
