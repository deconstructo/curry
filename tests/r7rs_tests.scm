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
;; Regression: strtol("", &end, radix) consumes nothing (end == s) and
;; returns 0 with errno untouched, indistinguishable from a genuine "0" by
;; the errno/end-of-string check alone -- (string->number "") returned 0
;; instead of #f (R7RS: the empty string is never a valid number literal).
(check "string->number empty string returns #f, not 0" (string->number "") #f)
(check "string->number empty numerator/denominator returns #f" (string->number "3/") #f)

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
;; Regression: string-ref used a separate, buggy byte-counting loop that
;; stopped the instant it saw enough lead bytes, without skipping the rest
;; of a multi-byte character's continuation bytes first — correct only
;; when every character before the target index happens to be 1 byte
;; (ASCII), or the target index is 0 or 1. Indexing *past* a 3-byte CJK
;; character (not just past a single 2-byte accented Latin one, which
;; happened to still work) exposed it: it landed mid-sequence and decoded
;; garbage instead of the intended character. Fixed by reusing the
;; already-correct utf8_char_offset() helper substring/string-copy rely on.
(check "string-ref past one multibyte char (3-byte CJK)" (string-ref "日本語" 1) #\本)
(check "string-ref past two multibyte chars (3-byte CJK)" (string-ref "日本語" 2) #\語)
(check "string-ref past a 2-byte char then more text" (string-ref "café latte" 5) #\l)
;; Regression: decoding a string's UTF-8 byte data assumed every lead byte
;; encountered had all of its continuation bytes actually present in the
;; buffer. utf8->string used to do a raw byte copy with no validation, so a
;; bytevector ending mid-multi-byte-sequence produced a string whose last
;; "character" was a truncated lead byte with no continuation bytes after
;; it — string-ref had to raise on that rather than read past the string's
;; allocation decoding continuation bytes that were never there. utf8->string
;; itself now validates (see below) and rejects this input before a
;; malformed String can even be constructed this way, but string-ref's own
;; defensive bounds check is kept as-is (belt and suspenders against any
;; other route to a malformed string), and this still exercises it
;; transitively -- the guard here catches utf8->string's own raise now,
;; one layer earlier than before, and the assertion still holds.
(check "string-ref on a string ending in a truncated UTF-8 sequence raises"
       (guard (exn (#t 'raised)) (string-ref (utf8->string (bytevector 32 32 32 240)) 3))
       'raised)
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
;; string->vector / vector->string (R7RS 6.7) -- were entirely missing
;; from curry (issue #46); (srfi 279)'s inspect.scm had its own local
;; list->vector/list->string-composed workaround noting the gap.
(check "string->vector" (string->vector "abc") (vector #\a #\b #\c))
(check "string->vector multibyte" (string->vector "héllo")
       (vector #\h #\é #\l #\l #\o))
(check "string->vector with start/end" (string->vector "hello" 1 3)
       (vector #\e #\l))
(check "string->vector empty string" (string->vector "") (vector))
(check "string->vector out-of-range end raises"
       (guard (exn (#t 'raised)) (string->vector "abc" 0 100)) 'raised)
(check "vector->string" (vector->string (vector #\a #\b #\c)) "abc")
(check "vector->string with start/end" (vector->string (vector #\a #\b #\c) 1) "bc")
(check "vector->string empty vector" (vector->string (vector)) "")
(check "vector->string raises on a non-character element"
       (guard (exn (#t 'raised)) (vector->string (vector 1 2 3))) 'raised)
(check "vector->string out-of-range end raises"
       (guard (exn (#t 'raised)) (vector->string (vector #\a #\b) 0 100)) 'raised)
(check "string->vector / vector->string round-trip"
       (vector->string (string->vector "curry")) "curry")
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
(check "write-string preserves multi-byte UTF-8 to a string port"
       (let ((out (open-output-string)))
         (write-string "Acme \x2014; test" out)
         (get-output-string out))
       "Acme \x2014; test")
(check "write preserves multi-byte UTF-8 in a quoted string to a string port"
       (let ((out (open-output-string)))
         (write "Acme \x2014; test" out)
         (get-output-string out))
       "\"Acme \x2014; test\"")
(check "string->utf8 out-of-range raises"
       (guard (exn (#t 'raised)) (string->utf8 "abc" 0 100)) 'raised)
(check "utf8->string out-of-range raises"
       (guard (exn (#t 'raised)) (utf8->string (string->utf8 "abc") 0 100)) 'raised)
;; Regression: utf8->string did a raw byte copy with no UTF-8 validation at
;; all -- any bytevector, however malformed, silently became a String.
(check "utf8->string raises on a truncated multi-byte sequence"
       (guard (exn (#t 'raised)) (utf8->string (bytevector 32 32 32 240))) 'raised)
(check "utf8->string raises on a stray continuation byte"
       (guard (exn (#t 'raised)) (utf8->string (bytevector 128))) 'raised)
(check "utf8->string raises on an overlong 2-byte encoding of NUL"
       (guard (exn (#t 'raised)) (utf8->string (bytevector 192 128))) 'raised)
(check "utf8->string raises on an encoded UTF-16 surrogate half"
       (guard (exn (#t 'raised)) (utf8->string (bytevector 237 160 128))) 'raised)
(check "utf8->string raises on a lead byte past the 4-byte range"
       (guard (exn (#t 'raised)) (utf8->string (bytevector 255))) 'raised)
(check "utf8->string still accepts well-formed multi-byte UTF-8"
       (utf8->string (string->utf8 "héllo 日本語")) "héllo 日本語")
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

;; write/display must terminate on circular structure (R7RS 6.13.3), using
;; #n=/#n# datum labels -- regression for a real hang confirmed live.
(check "write on circular pair terminates with datum labels"
       (let ((out (open-output-string)))
         (write circ out)
         (get-output-string out))
       "#0=(1 2 3 . #0#)")
(check "display on circular pair terminates with datum labels"
       (let ((out (open-output-string)))
         (display circ out)
         (get-output-string out))
       "#0=(1 2 3 . #0#)")
(check "write on self-referential single pair terminates"
       (let ((out (open-output-string)))
         (write circ1 out)
         (get-output-string out))
       "#0=(1 . #0#)")
(define circ-vec (vector 1 2 3))
(vector-set! circ-vec 2 circ-vec)
(check "write on circular vector terminates with datum labels"
       (let ((out (open-output-string)))
         (write circ-vec out)
         (get-output-string out))
       "#0=#(1 2 #0#)")
;; Regression: the cycle-safe write/display machinery's first pass
;; (ws_count_refs) originally recursed on both car AND cdr for every
;; cons cell, so an ordinary long flat (non-circular, non-shared) list
;; overflowed the C stack once write()/display() started routing through
;; it unconditionally -- found by independent security review, reproduced
;; at ~150-200k elements. Fixed by making the cdr walk iterative (only
;; car recursion remains, bounded by nesting depth, not list length).
(let loop ((i 0) (acc '()))
  (if (= i 300000)
      (let ((out (open-output-string)))
        (write acc out)
        (check "write on a long flat list doesn't stack-overflow"
               (string? (get-output-string out)) #t))
      (loop (+ i 1) (cons i acc))))

;;; Vectors
(define v (make-vector 3 0))
(vector-set! v 1 42)
(check "vector-ref" (vector-ref v 1) 42)
(check "vector-length" (vector-length v) 3)
(check "vector->list" (vector->list '#(1 2 3)) '(1 2 3))

;;; Control
(check "values" (call-with-values (lambda () (values 1 2)) +) 3)

;;; call-with-values with a zero-value producer — regression coverage for
;;; a bug found in a full-codebase audit: vm.c's OP_VALUES special-cased
;;; n == 0 by pushing V_VOID instead of a genuine zero-count Values
;;; object (unlike eval.c's tree-walker equivalent, which already built a
;;; real empty Values object). call-with-values's consumer decides how
;;; many arguments to apply by checking vis_values(produced); V_VOID
;;; isn't distinguishable from an ordinary single void-returning value,
;;; so it was applied as ONE argument, and a zero-argument consumer
;;; failed with "too many arguments (got 1, need 0)". Fixed by letting
;;; n == 0 fall through to the same code path n >= 2 already used.
(check "call-with-values: zero-value producer, zero-arg consumer"
  (call-with-values (lambda () (values)) (lambda () 'ok)) 'ok)
(check "call-with-values: zero-value producer, rest-arg consumer sees an empty list"
  (call-with-values (lambda () (values)) list) '())

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

;;; define-values / defined? — compiled natively as of this session (both
;;; previously had ZERO compiler codegen, tree-walker-only; ordinary
;;; compiled code couldn't use either at all).
(define-values (dv-a dv-b) (values 1 2))
(check "define-values: top-level, two vars" (list dv-a dv-b) '(1 2))
(define-values (dv-single) 42)
(check "define-values: single var, single (non-Values) value" dv-single 42)
(define-values (dv-extra1 dv-extra2) (values 10 20 30))
(check "define-values: extra produced values are discarded"
  (list dv-extra1 dv-extra2) '(10 20))
(check "define-values: internal define inside a lambda body"
  (let ()
    (define-values (p q) (values 3 4))
    (+ p q))
  7)
(check "define-values: zero formals, expr evaluated for effect only"
  (begin (define-values () (values)) 'ok) 'ok)
(check "define-values: too few produced values raises"
  (guard (e (#t 'caught))
    (define-values (x y z) (values 1 2))
    'not-reached)
  'caught)

(define dv-global-defined 1)
(check "defined?: bound global" (defined? dv-global-defined) #t)
(check "defined?: unbound global" (defined? dv-totally-unbound-name-xyz) #f)
(check "defined?: local parameter"
  ((lambda (x) (defined? x)) 5) #t)
(check "defined?: upvalue captured from an enclosing lambda"
  ((lambda (x) ((lambda () (defined? x)))) 5) #t)

;;; let-values / let*-values — regression coverage for a bug found in a
;;; full-codebase audit: src/compiler.c had zero handling for
;;; S_LET_VALUES/S_LET_STAR_VALUES, so both special forms only worked in
;;; the tree-walker (eval.c), not compiled/`-e` execution or .scc scripts.
;;; Fixed by desugaring both to nested call-with-values/lambda forms at
;;; compile time (compile_let_values/compile_let_star_values).
(check "let-values proper formals"
  (let-values (((a b) (values 1 2))) (+ a b)) 3)
(check "let-values multiple bindings"
  (let-values (((a b) (values 1 2)) ((c) (values 3))) (list a b c)) '(1 2 3))
(check "let-values dotted formals"
  (let-values (((a . rest) (values 1 2 3))) (list a rest)) '(1 (2 3)))
(check "let-values rest-only formals"
  (let-values ((all (values 1 2 3))) all) '(1 2 3))
(check "let-values is parallel: a producer cannot see an earlier binding"
  (let ((a 100)) (let-values (((a) (values 1)) ((b) (values a))) (list a b))) '(1 100))

;;; call-with-values / receive / let-values / let*-values in tail
;;; position now get genuine TCO — regression coverage for a bug found
;;; while fixing the above: prim_call_with_values (builtins.c) invokes
;;; its consumer via a real nested C call, which is fine for a one-shot
;;; call but meant a self-recursive loop tail-calling through any of
;;; these forms accumulated one such nested call PER ITERATION, hitting
;;; curry's call-stack limit instead of looping forever. Fixed with a new
;;; OP_TAIL_CALL_WITH_VALUES bytecode op (emitted only when the compiler
;;; sees a literal 2-argument (call-with-values producer consumer) — the
;;; same unconditional syntactic special-casing convention already used
;;; for apply/values — in tail position) that reuses the current call
;;; frame for a BcClosure consumer exactly the way plain OP_TAIL_CALL
;;; does, instead of going through prim_call_with_values at all.
(check "call-with-values in tail position: deep recursion doesn't overflow"
  (let loop ((n 200000) (acc 0))
    (if (= n 0) acc
        (call-with-values (lambda () (values (- n 1)))
          (lambda (n1) (loop n1 (+ acc 1)))))) 200000)
(check "receive in tail position: deep recursion doesn't overflow"
  (let loop ((n 200000) (acc 0))
    (if (= n 0) acc
        (receive (n1) (values (- n 1))
          (loop n1 (+ acc 1))))) 200000)
(check "let-values in tail position: deep recursion doesn't overflow"
  (let loop ((n 200000) (acc 0))
    (if (= n 0) acc
        (let-values (((n1) (values (- n 1))))
          (loop n1 (+ acc 1))))) 200000)
(check "let*-values in tail position: deep recursion doesn't overflow"
  (let loop ((n 200000) (acc 0))
    (if (= n 0) acc
        (let*-values (((n1) (values (- n 1))))
          (loop n1 (+ acc 1))))) 200000)
(check "call-with-values still correct as a non-tail subexpression"
  (+ 1 (call-with-values (lambda () (values 10 20)) +)) 31)
; Genuinely in tail position this time (the sole/last expression of an
; immediately-invoked lambda's body, not an argument expression to
; `check` itself, which compile_call always compiles tail=false) --
; exercises OP_TAIL_CALL_WITH_VALUES's non-BcClosure branch specifically.
(check "call-with-values in tail position with a non-BcClosure consumer (a primitive)"
  ((lambda () (call-with-values (lambda () (values 1 2 3)) +))) 6)
(check "let*-values proper formals"
  (let*-values (((a b) (values 1 2)) ((c) (values (+ a b)))) (list a b c)) '(1 2 3))
(check "let*-values is sequential: a later producer sees an earlier binding"
  (let*-values (((a) (values 1)) ((b) (values (+ a 1)))) (list a b)) '(1 2))
(check "let*-values no bindings"
  (let*-values () 42) 42)
(check "let-values no bindings"
  (let-values () 42) 42)

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

;;; Regression coverage for issue #120: a named let's own init expressions
;;; were compiled/evaluated in a scope where the loop's own name was
;;; ALREADY a valid local (bound to a void placeholder, later the real
;;; closure) instead of R7RS's mandated "inits run in the ENCLOSING
;;; scope, before the loop's own binding exists at all". Two independent
;;; bytecode-compilation paths had this bug (compiler_classic.c's
;;; compile_let and the Tier 2.1 IR pipeline's own separate ir_emit.c
;;; IR_NAMED_LET case, including ITS OWN separate MAX_LOCALS-overflow
;;; fallback that builds a real, standalone closure the way compile_let
;;; always did) -- all three fixed identically: compile each init value
;;; via the true enclosing compiler, never the loop's own.
(check "named let: own name is not visible during its own init exprs"
       (guard (e (#t 'unbound)) (let loop ((i (procedure? loop))) i))
       'unbound)
(check "named let: init expr correctly sees an outer binding of the same name"
       (let ((count (lambda (x) (* x 10))))
         (let count ((i (count 5))) i))
       50)
;;; Same check again, but with enough loop variables (MAX_LOCALS is 256)
;;; to force ir_emit.c's separate MAX_LOCALS-overflow "build a real,
;;; standalone closure" fallback path, which has its own from-scratch
;;; copy of the same logic (and, before the fix, the same bug) as the
;;; common "splice the loop directly into the caller's frame" fast path
;;; exercised by the checks above. Deliberately NOT built via `eval`/
;;; `load` at runtime -- both route through eval.c's tree-walking
;;; evaluator (prim_eval calls eval() directly; scm_load's own read-loop
;;; does too), a third, wholly separate named-let implementation from
;;; the two bytecode-compilation paths (compiler_classic.c, ir_emit.c)
;;; this test exists to cover, so a form constructed and handed to
;;; either at runtime would silently test nothing relevant. Must appear
;;; as literal top-level source text in this script file, compiled the
;;; same way every other top-level form in it is (main.c's own
;;; compiler_compile + vm_run), machine-generated once rather than
;;; hand-typed.
(check "named let, enough loop vars to force the overflow fallback path: own name not visible"
       (guard (e (#t 'unbound))
         (let loop ((v0 0) (v1 0) (v2 0) (v3 0) (v4 0) (v5 0) (v6 0) (v7 0) (v8 0) (v9 0) (v10 0) (v11 0) (v12 0) (v13 0) (v14 0) (v15 0) (v16 0) (v17 0) (v18 0) (v19 0) (v20 0) (v21 0) (v22 0) (v23 0) (v24 0) (v25 0) (v26 0) (v27 0) (v28 0) (v29 0) (v30 0) (v31 0) (v32 0) (v33 0) (v34 0) (v35 0) (v36 0) (v37 0) (v38 0) (v39 0) (v40 0) (v41 0) (v42 0) (v43 0) (v44 0) (v45 0) (v46 0) (v47 0) (v48 0) (v49 0) (v50 0) (v51 0) (v52 0) (v53 0) (v54 0) (v55 0) (v56 0) (v57 0) (v58 0) (v59 0) (v60 0) (v61 0) (v62 0) (v63 0) (v64 0) (v65 0) (v66 0) (v67 0) (v68 0) (v69 0) (v70 0) (v71 0) (v72 0) (v73 0) (v74 0) (v75 0) (v76 0) (v77 0) (v78 0) (v79 0) (v80 0) (v81 0) (v82 0) (v83 0) (v84 0) (v85 0) (v86 0) (v87 0) (v88 0) (v89 0) (v90 0) (v91 0) (v92 0) (v93 0) (v94 0) (v95 0) (v96 0) (v97 0) (v98 0) (v99 0) (v100 0) (v101 0) (v102 0) (v103 0) (v104 0) (v105 0) (v106 0) (v107 0) (v108 0) (v109 0) (v110 0) (v111 0) (v112 0) (v113 0) (v114 0) (v115 0) (v116 0) (v117 0) (v118 0) (v119 0) (v120 0) (v121 0) (v122 0) (v123 0) (v124 0) (v125 0) (v126 0) (v127 0) (v128 0) (v129 0) (v130 0) (v131 0) (v132 0) (v133 0) (v134 0) (v135 0) (v136 0) (v137 0) (v138 0) (v139 0) (v140 0) (v141 0) (v142 0) (v143 0) (v144 0) (v145 0) (v146 0) (v147 0) (v148 0) (v149 0) (v150 0) (v151 0) (v152 0) (v153 0) (v154 0) (v155 0) (v156 0) (v157 0) (v158 0) (v159 0) (v160 0) (v161 0) (v162 0) (v163 0) (v164 0) (v165 0) (v166 0) (v167 0) (v168 0) (v169 0) (v170 0) (v171 0) (v172 0) (v173 0) (v174 0) (v175 0) (v176 0) (v177 0) (v178 0) (v179 0) (v180 0) (v181 0) (v182 0) (v183 0) (v184 0) (v185 0) (v186 0) (v187 0) (v188 0) (v189 0) (v190 0) (v191 0) (v192 0) (v193 0) (v194 0) (v195 0) (v196 0) (v197 0) (v198 0) (v199 0) (v200 0) (v201 0) (v202 0) (v203 0) (v204 0) (v205 0) (v206 0) (v207 0) (v208 0) (v209 0) (v210 0) (v211 0) (v212 0) (v213 0) (v214 0) (v215 0) (v216 0) (v217 0) (v218 0) (v219 0) (v220 0) (v221 0) (v222 0) (v223 0) (v224 0) (v225 0) (v226 0) (v227 0) (v228 0) (v229 0) (v230 0) (v231 0) (v232 0) (v233 0) (v234 0) (v235 0) (v236 0) (v237 0) (v238 0) (v239 0) (v240 0) (v241 0) (v242 0) (v243 0) (v244 0) (v245 0) (v246 0) (v247 0) (v248 0) (v249 0) (v250 0) (v251 0) (v252 0) (v253 (procedure? loop))) v253))
       'unbound)

;;; Regression coverage for issue #123, found during independent security
;;; review of the #120 fix above: the IR pipeline's "splice the named let
;;; directly into the caller's own frame" fast path captures the loop's
;;; own name as an OPEN upvalue (for the inner recursive lambda's self-
;;; calls), but its non-tail-call exit discarded that stack slot via
;;; OP_SLIDE without ever closing the upvalue first. An escaped closure
;;; holding that upvalue kept pointing at the now-stale, soon-to-be-
;;; reused stack address, silently aliasing whatever LATER local ended up
;;; there instead -- readable and (via set!) WRITABLE, from inside a
;;; closure that already escaped the named let entirely. The #120 fix
;;; widened OP_SLIDE's own operand from a fixed 1 to argc+1, which is
;;; what turned this from "clobber the named let's own about-to-be-
;;; discarded result" into "clobber whichever enclosing local happens to
;;; sit argc+1 slots up, chosen by how many loop variables the source
;;; declares" -- i.e. made the aliasing target attacker-influenceable
;;; rather than a fixed, always-safe-to-discard value. Fixed by emitting
;;; OP_CLOSE_UP before the OP_SLIDE.
(check "named let: an escaped closure over the loop's own name does not alias a later local"
       (let ()
         (define esc #f)
         (define (f)
           (let* ((x (let loop ((i 0) (j 1))
                       (set! esc (lambda (v) (set! loop v)))
                       'done))
                  (a 111) (b 222) (c 333) (d 444))
             (esc 'clobbered)
             (list x a b c d)))
         (f))
       (list 'done 111 222 333 444))

;;; Regression coverage for issue #124: a let/let*/letrec/do/let-syntax/
;;; guard binding or clause with the wrong shape (not a pair, or a pair
;;; with no init/body) was destructured via unchecked vcar/vcdr chains
;;; across THREE independent implementations of this logic (the Tier 2.1
;;; IR pipeline in ir_lower.c/ir_emit.c, the classic compiler in
;;; compiler_classic.c, and the tree-walking evaluator in eval.c) --
;;; SIGSEGVing the whole process instead of raising a catchable error.
;;; A malformed binding with a missing init is a plausible typo, so this
;;; was reachable from ordinary source. Fixed by validating every
;;; binding/clause up front with the same idiom already used for the
;;; enclosing form's own arity (require_min_args in the two compiler
;;; paths; a small equivalent helper in eval.c, which has no access to
;;; the compiler-internal header that idiom lives in).
(define (jit124-malformed-raises? thunk)
  (guard (e (#t #t)) (thunk) #f))
(check "let: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(let ((a)) 1) (interaction-environment))))
       #t)
(check "named let: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(let loop ((a)) 1) (interaction-environment))))
       #t)
(check "let*: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(let* ((a)) 1) (interaction-environment))))
       #t)
(check "letrec: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(letrec ((a)) 1) (interaction-environment))))
       #t)
(check "letrec*: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(letrec* ((a)) 1) (interaction-environment))))
       #t)
(check "do: malformed var-spec raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(do ((a)) (#t) 1) (interaction-environment))))
       #t)
(check "let-syntax: malformed binding raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(let-syntax ((m)) 1) (interaction-environment))))
       #t)
(check "guard: empty clause raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(guard (e ()) (raise 'oops)) (interaction-environment))))
       #t)
(check "guard: bare single-element clause (R7RS-valid, no body) still works"
       (eval '(guard (e (#t)) (raise 5)) (interaction-environment))
       #t)
;; `eval` here exercises eval.c's OWN tree-walking implementation of each
;; form, a THIRD independent copy of this logic distinct from both the
;; classic bytecode compiler (compiler_classic.c) and the Tier 2.1 IR
;; pipeline (ir_lower.c/ir_emit.c) -- each of those two has its own
;; identical bug, independently confirmed fixed via `curry -e` (which
;; compiles rather than tree-walks) during development, but not
;; exercised by this .scm file: a malformed top-level form would crash
;; during the COMPILE phase of loading this very test script, before any
;; runtime `guard` could ever catch it. do/let-syntax/guard specifically
;; have NO IR lowering at all (SF_DO/SF_LET_SYNTAX/SF_GUARD route
;; straight to compiler_classic.c's own compile_do/compile_let_syntax/
;; compile_guard), so for those three forms this compiler-side fix is
;; the ONLY live path outside eval.c's own tree-walker -- but is
;; likewise only checkable via a real subprocess compile, not from
;; inside this file.
;; Confirms the fix didn't regress ordinary usage of any of these forms.
(check "let/let*/letrec/do/let-syntax/guard still work correctly"
       (list (let ((a 1) (b 2)) (+ a b))
             (let* ((a 1) (b (+ a 1))) b)
             (letrec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5))
             (do ((i 0 (+ i 1)) (s 0 (+ s i))) ((= i 5) s))
             (let-syntax ((m (syntax-rules () ((_ x) (+ x 1))))) (m 5))
             (guard (e (#t 'caught)) (raise 'oops)))
       (list 3 2 120 10 6 'caught))

;;; Issue #125 (ir_emit/ir_emit_inline_call's unbounded per-binding C
;;; recursion in a flat let* chain, Tier 2.1 IR pipeline) is regression-
;;; tested in tests/test_cli.sh, not here: `eval` exercises eval.c's own
;;; tree-walking let* -- a plain while loop over bindings, not per-
;;; binding C recursion -- so it can't exercise ir_emit.c's bug at all;
;;; and a malformed/huge top-level form must be tested via a real `curry`
;;; subprocess compile regardless, for the same reason #124's do/
;;; let-syntax/guard checks are in test_cli.sh rather than here.

;;; Additional eval.c-only gaps found by independent code review of the
;;; #124 fix above: the compiler paths already rejected these malformed
;;; forms cleanly, but eval.c's own tree-walker still SIGSEGVed on them
;;; (require_binding_shape only validates an individual BINDING's shape,
;;; not that the enclosing form has enough top-level pieces to destructure
;;; in the first place -- e.g. `(let* ((a 1)))` has a well-formed binding
;;; but no body, and the old code unconditionally called vcdr on that nil
;;; body while checking for more forms to evaluate).
(check "let*: missing body doesn't crash (returns void)"
       (jit124-malformed-raises? (lambda () (eval '(let* ((a 1))) (interaction-environment)) #f))
       #f)
(check "letrec: missing body doesn't crash (returns void)"
       (jit124-malformed-raises? (lambda () (eval '(letrec ((a 1))) (interaction-environment)) #f))
       #f)
(check "named let: missing bindings/body raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(let loop) (interaction-environment))))
       #t)
(check "do: missing test clause raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(do ((i 0 (+ i 1)))) (interaction-environment))))
       #t)
(check "do: non-pair test clause raises instead of crashing"
       (jit124-malformed-raises? (lambda () (eval '(do ((i 0)) ()) (interaction-environment))))
       #t)
(check "do: dotted step-spec doesn't dereference the improper tail"
       ;; (i 0 . 5) -- vcddr(spec) is 5, a non-pair. Matches
       ;; compiler_classic.c's own do step-expr check (vis_pair(vcdr(vcdr
       ;; (spec)))): treated as "no step expression" rather than crashing.
       ;; This makes i never advance, hence the outer call/cc escape
       ;; instead of actually looping.
       (call-with-current-continuation
        (lambda (k)
          (guard (e (#t (k 'raised)))
            (eval '(do ((i 0 . 5) (n 0 (+ n 1)))
                       ((or (= i 1) (> n 2)) 'done))
                  (interaction-environment)))))
       'done)
;; letrec* naming its own error correctly (ir_lower_letrec, shared by
;; letrec/letrec*, was found by independent code review to hardcode
;; "letrec" in the raised message even for a letrec* input) can't be
;; tested from inside this file at all: `(letrec* ((a)) 1)` at the top
;; level is a COMPILE-time error for the whole enclosing top-level form,
;; including any guard meant to catch it -- the same eval-vs-compile
;; distinction #124/#125's other compiler-path checks are subject to
;; (see the comment above). Tested via a real subprocess in test_cli.sh
;; instead, grepping stderr for the exact form name.

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

;;; record-type-constructor/-predicate/-accessors/-mutators: the actual
;;; procedure objects define-record-type's own codegen creates, stashed
;;; back onto the RTD (SRFI-279's rtd-properties wants these -- see
;;; record_type.c/builtins.c's own comments on why the RTD doesn't have
;;; them populated until after each binding is compiled/evaluated).
(define person-rtd (record-rtd alice))
(check "record-type-constructor is the actual constructor"
  (person-name ((record-type-constructor person-rtd) "Bob" 40)) "Bob")
(check "record-type-predicate is the actual predicate"
  ((record-type-predicate person-rtd) alice) #t)
(check "record-type-predicate rejects a non-instance"
  ((record-type-predicate person-rtd) 42) #f)
(check "record-type-accessors: field order matches record-type-field-names"
  (map (lambda (acc) (acc alice)) (record-type-accessors person-rtd))
  (list "Alice" 31))
(check "record-type-mutators: immutable field's mutator slot is #f"
  (car (record-type-mutators person-rtd)) #f)
(check "record-type-mutators: mutable field's mutator slot actually mutates"
  (let ((bob (make-person "Bob" 20)))
    ((cadr (record-type-mutators person-rtd)) bob 21)
    (person-age bob))
  21)

;;; bignum?/multivector?: two type predicates genuinely missing from
;;; curry's core before (srfi 253) -- every other extended-tower/object
;;; predicate (quaternion?, octonion?, complex?, rational?, surreal?,
;;; symbolic?, quantum?, matrix?, tensor?, spinor?, actor?) already
;;; existed; added alongside (srfi 253) so it can be used meaningfully
;;; across curry's whole type system, not just R7RS's base types.
(check "bignum?: #f for a fixnum" (bignum? 5) #f)
(check "bignum?: #t past fixnum range" (bignum? (expt 2 100)) #t)
(check "multivector?: #f for a plain number" (multivector? 5) #f)
(check "multivector?: #t for a real multivector" (multivector? (make-mv 3 0 0)) #t)

;;; %rtd-set-constructor!/-predicate!/-accessor!/-mutator!: intended for
;;; internal use only by define-record-type's own codegen, but ordinary
;;; DEF'd globals like every other primitive, so directly callable by
;;; any script. Independent security review found and reproduced a real
;;; segfault: no vis_rtd check on the first argument, so vunptr blindly
;;; reinterpreted an arbitrary value's raw bits as a RecordType*. Also
;;; found a non-fixnum field-index argument silently aliasing into a
;;; valid slot via vunfix's raw bit-shift (no vis_fixnum check).
(check "%rtd-set-accessor! on a non-rtd raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rtd-set-accessor! 5 0 (lambda (x) x)))
  'caught)
(check "%rtd-set-predicate! on #f raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rtd-set-predicate! #f 'x))
  'caught)
(check "%rtd-set-accessor! on a string raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rtd-set-accessor! "hello" 0 (lambda (x) x)))
  'caught)
(check "%rtd-set-accessor! with a non-fixnum index raises, doesn't alias into a valid slot"
  (guard (e (#t 'caught)) (%rtd-set-accessor! person-rtd (integer->char 0) (lambda (r) 'hijacked)))
  'caught)
(check "record-type-accessors unaffected by the rejected hijack attempt above"
  (person-name alice) "Alice")

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

;;; record-type-* through a LOCALLY-defined record type: exercises the
;;; add_local/scope_depth > 0 branch of compile_define_record_type
;;; (the rtd_ref gensym gets its own reserved local slot instead of a
;;; global binding), a different codegen path from the top-level
;;; <person> record above -- must stash the same way.
(define (make-local-rtd-probe)
  (define-record-type <box2>
    (mk-box2 v)
    box2?
    (v box2-v set-box2-v!))
  (record-rtd (mk-box2 0)))
(define box2-rtd (make-local-rtd-probe))
(check "local define-record-type: record-type-constructor works"
  ((car (record-type-accessors box2-rtd)) ((record-type-constructor box2-rtd) 99)) 99)
(check "local define-record-type: record-type-predicate works"
  ((record-type-predicate box2-rtd) ((record-type-constructor box2-rtd) 0)) #t)
(check "local define-record-type: record-type-predicate rejects other types"
  ((record-type-predicate box2-rtd) 42) #f)
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

;; Regression for GH issue #88: floor-quotient/floor-remainder/floor/
;; used to silently accept a non-integer argument and return a
;; plausible-but-wrong result instead of raising, unlike
;; quotient/remainder/truncate-quotient/truncate-remainder, which
;; already correctly rejected one via numeric.c's to_mpz().
(check "floor-quotient rejects a rational argument"
       (guard (exn (#t 'raised)) (floor-quotient 15/2 2)) 'raised)
(check "floor-remainder rejects an inexact argument"
       (guard (exn (#t 'raised)) (floor-remainder 7.5 2)) 'raised)
(check "floor/ rejects a rational argument"
       (guard (exn (#t 'raised))
         (call-with-values (lambda () (floor/ 15/2 2)) (lambda (q r) 'no-error))) 'raised)
;; floor and truncate agree when both operands are positive, so this
;; checks the fix didn't disturb the bignum path at all, via a value
;; already known correct (quotient) rather than a hand-derived one.
(check "floor-quotient still works on bignums after the fix"
       (floor-quotient (expt 2 100) 7) (quotient (expt 2 100) 7))

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
;; Regression: read-line on a real (non-string) port used to bypass the
;; one-codepoint peek-char lookahead entirely, silently dropping
;; whatever character a preceding peek-char had already buffered, and
;; leaving that lookahead stale forever afterward (peek-char would then
;; keep reporting the same stale character indefinitely, never reaching
;; eof-object? even long after the real stream was exhausted). Only
;; reproduces on a real FILE*-backed port -- open-input-string's
;; peek-char restores its position immediately rather than caching a
;; persistent lookahead, so it never hit this path.
(let ((path "/tmp/curry-portline-regression-test.txt"))
  (call-with-output-file path (lambda (p) (display "line1\nline2\n" p)))
  (check "peek-char then read-line on a file port doesn't drop the peeked character"
    (call-with-input-file path
      (lambda (p) (let ((c (peek-char p))) (list c (read-line p)))))
    (list #\l "line1"))
  (check "peek-char after read-line correctly reaches eof, doesn't stick to a stale lookahead"
    (call-with-input-file path
      (lambda (p)
        (peek-char p) (read-line p)   ; line1
        (peek-char p) (read-line p)   ; line2
        (eof-object? (peek-char p))))
    #t))
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

;;; delay-force chains -- GitHub issue #51: force only flattened one
;;; level of delay-force indirection, leaving a chain 3+ levels deep as
;;; an unforced inner promise instead of its final value.
(define dfp3 (delay 42))
(define dfp2 (delay-force dfp3))
(define dfp1 (delay-force dfp2))
(check "delay-force chain, 3 levels" (force dfp1) 42)

(define dfp6 (delay 100))
(define dfp5 (delay-force dfp6))
(define dfp4 (delay-force dfp5))
(define dfp3b (delay-force dfp4))
(define dfp2b (delay-force dfp3b))
(define dfp1b (delay-force dfp2b))
(check "delay-force chain, 6 levels" (force dfp1b) 100)
(check "delay-force chain memoized on second force" (force dfp1b) 100)

;;; mixed plain delay / delay-force chain
(define dfq2 (delay 7))
(define dfq1 (delay-force dfq2))
(check "mixed delay/delay-force chain" (force dfq1) 7)

;;; the classic delay-force use case: a recursive stream filter, which
;;; chains delay-force arbitrarily deep as it skips non-matching elements
(define (df-stream-filter pred s)
  (delay-force
    (if (null? (force s))
        (delay '())
        (let ((h (car (force s))) (t (cdr (force s))))
          (if (pred h)
              (delay (cons h (df-stream-filter pred t)))
              (df-stream-filter pred t))))))
(define (df-stream-from n) (delay (cons n (df-stream-from (+ n 1)))))
(define (df-stream-take s n)
  (if (= n 0) '()
      (cons (car (force s)) (df-stream-take (cdr (force s)) (- n 1)))))
(check "delay-force recursive stream filter"
  (df-stream-take (df-stream-filter even? (df-stream-from 1)) 5)
  '(2 4 6 8 10))

;;; Macro introspection: macro?, macroexpand-1, macroexpand
;;; (introspection uplift work, docs/thoughts/introspection-uplift-plan.md)
(define-syntax intro-my-when
  (syntax-rules ()
    ((_ c b ...) (if c (begin b ...) (if #f #f)))))
(check "macro?: a real macro" (macro? 'intro-my-when) #t)
(check "macro?: an ordinary procedure" (macro? 'car) #f)
(check "macro?: not even bound" (macro? 'totally-unbound-name-xyz) #f)
(check "macroexpand-1: expands exactly one step"
       (macroexpand-1 '(intro-my-when #t (display 1) (display 2)))
       '(if #t (begin (display 1) (display 2)) (if #f #f)))
(check "macroexpand-1: non-macro-use form returned unchanged"
       (macroexpand-1 '(+ 1 2)) '(+ 1 2))
(check "macroexpand-1: non-pair returned unchanged" (macroexpand-1 42) 42)
;; A macro whose expansion is itself another macro use -- macroexpand
;; (unlike macroexpand-1) keeps going until the head is no longer a macro.
(define-syntax intro-double-when
  (syntax-rules ()
    ((_ c b ...) (intro-my-when c b ...))))
(check "macroexpand: fully expands the outermost form"
       (macroexpand '(intro-double-when #t 'ok))
       '(if #t (begin 'ok) (if #f #f)))

;; Regression: a self-referential macro (its own expansion is another use
;; of itself) used to hang macroexpand forever -- each expansion step
;; conses a genuinely fresh list, so a bare `next == expr` fixpoint check
;; never detects it. Found by independent review. Fixed with a hard
;; expansion-step cap that raises a catchable error instead of looping.
(define-syntax intro-loopy
  (syntax-rules () ((_ x) (intro-loopy x))))
(check "macroexpand: self-referential macro raises instead of hanging"
       (guard (e (#t 'raised)) (macroexpand '(intro-loopy 1)))
       'raised)

;;; Object introspection: type-of (introspection uplift work)
(check "type-of: fixnum" (type-of 42) 'fixnum)
(check "type-of: pair" (type-of (cons 1 2)) 'pair)
(check "type-of: string" (type-of "hi") 'string)
(check "type-of: symbol" (type-of 'sym) 'symbol)
(check "type-of: char" (type-of #\a) 'char)
(check "type-of: boolean" (type-of #t) 'boolean)
(check "type-of: null" (type-of '()) 'null)
(check "type-of: vector" (type-of (vector 1 2)) 'vector)
(check "type-of: a compiled closure" (type-of (lambda (x) x)) 'closure)
(check "type-of: a primitive" (type-of car) 'primitive)
(check "type-of: flonum" (type-of 1.5) 'flonum)

;;; Issue #127: cond/case clause destructuring was unchecked in both
;;; compile_cond/compile_case (compiler_classic.c) and eval.c's own
;;; S_COND/S_CASE handlers -- a clause that wasn't itself a pair (e.g.
;;; `(cond ())`, `(cond 1)`, `(case 1 1)`) SIGSEGVed instead of raising.
;;; Found via independent security review of the #124/#125 fix, which
;;; used the identical validate-before-destructure idiom for the
;;; let/letrec/do family but missed this sibling form entirely.
(define (jit127-malformed-raises? thunk)
  (guard (e (#t #t)) (thunk) #f))
(check "cond: empty clause raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(cond ()) (interaction-environment))))
       #t)
(check "cond: non-pair clause raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(cond 1) (interaction-environment))))
       #t)
(check "case: empty clause raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case 1 ()) (interaction-environment))))
       #t)
(check "case: non-pair clause raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case 1 1) (interaction-environment))))
       #t)
(check "case: missing key raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case) (interaction-environment))))
       #t)
;; A second round of independent review of the fix just above found
;; more crashes it didn't cover -- an improper (non-nil, non-pair)
;; clause body, and an `=>` arrow clause with no receiver expression --
;; in both cond and case, including case's own `else` clause (which had
;; its own separate copy of the same logic, missing the same checks its
;; sibling matched-datum branch already had).
(check "cond: improper clause tail raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(cond (1 . 2)) (interaction-environment))))
       #t)
(check "cond: improper else-clause tail raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(cond (else . 2)) (interaction-environment))))
       #t)
(check "cond: arrow clause with no receiver raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(cond (1 =>)) (interaction-environment))))
       #t)
(check "case: improper clause tail raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case 1 ((1) . 2)) (interaction-environment))))
       #t)
(check "case: else clause with no body doesn't crash (returns void)"
       (jit127-malformed-raises? (lambda () (eval '(case 1 (else)) (interaction-environment)) #f))
       #f)
(check "case: else-arrow clause with no receiver raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case 1 (else =>)) (interaction-environment))))
       #t)
(check "case: matched-arrow clause with no receiver raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(case 1 ((1) =>)) (interaction-environment))))
       #t)
;; The compiler-path halves of #127 (compile_cond/compile_case) can only
;; be tested via a real subprocess compile, same reasoning as #124's own
;; do/let-syntax/guard checks -- see tests/test_cli.sh.
(check "cond/case still work correctly"
       (list (cond (#f 1) (#t 2))
             (cond ((assv 1 (list (cons 1 'a))) => cdr) (else 'no))
             (case 2 ((1) 'one) ((2) 'two) (else 'other))
             (case 5 ((1 2) 'a) (else 'b)))
       (list 2 'a 'two 'b))

;;; Issue #128: several more eval.c-only unchecked-destructure gaps in
;;; the same class as #124/#127, found by independent code review.
;;; These are tree-walker-only bugs -- the corresponding compiler paths
;;; already validate correctly for the same malformed input.
(check "let-values: non-pair binding raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(let-values (1) 1) (interaction-environment))))
       #t)
(check "let-values: malformed binding raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(let-values ((a)) 1) (interaction-environment))))
       #t)
(check "let*-values: malformed binding raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(let*-values ((a)) 1) (interaction-environment))))
       #t)
(check "parameterize: non-pair binding raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(parameterize (1) 1) (interaction-environment))))
       #t)
(check "parameterize: malformed binding raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(parameterize ((car)) 1) (interaction-environment))))
       #t)
(check "parameterize: non-parameter value raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(parameterize ((car 5)) 1) (interaction-environment))))
       #t)
;; A second round of independent review of the fix just above found
;; `(let-values)` / `(let*-values)` / `(parameterize)` -- no operand
;; list at all, not merely a malformed one -- still crashed: the
;; per-binding checks above only fire once a bindings LIST is already
;; being iterated. Also confirms `(parameterize ())` (a valid empty
;; bindings list with no body) matches the compiled path's own
;; leniency here (returns void) rather than introducing a stricter,
;; tree-walker-only rejection for the same input.
(check "let-values: missing bindings list raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(let-values) (interaction-environment))))
       #t)
(check "let*-values: missing bindings list raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(let*-values) (interaction-environment))))
       #t)
(check "parameterize: missing bindings list raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(parameterize) (interaction-environment))))
       #t)
(check "parameterize: empty bindings and no body doesn't crash (returns void)"
       (jit127-malformed-raises? (lambda () (eval '(parameterize ()) (interaction-environment)) #f))
       #f)
(check "when: missing test raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(when) (interaction-environment))))
       #t)
(check "when: missing body doesn't crash (returns void)"
       (jit127-malformed-raises? (lambda () (eval '(when #t) (interaction-environment)) #f))
       #f)
(check "unless: missing test raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(unless) (interaction-environment))))
       #t)
(check "define-syntax: missing name/transformer raises instead of crashing"
       (jit127-malformed-raises? (lambda () (eval '(define-syntax) (interaction-environment))))
       #t)
(check "let-values/let*-values/parameterize/when/unless still work correctly"
       (list (let-values (((a b) (values 1 2))) (+ a b))
             (let*-values (((a) (values 1)) ((b) (values (+ a 1)))) b)
             (let* ((p (make-parameter 10)))
               (parameterize ((p 20)) (p)))
             (when #t 'yes)
             (unless #f 'also-yes))
       (list 3 2 20 'yes 'also-yes))

;;; Issue #129: the reader itself (src/reader.c) had no recursion-depth
;;; or list-length limit at all -- read_list/read_list_tail recurse
;;; once per list element with no bound, and read_datum recurses once
;;; per prefix reader macro (quote/quasiquote/unquote) with no bound
;;; either. A sufficiently large flat list, deeply nested parens, or a
;;; long quote-chain all SIGSEGVed at READ time, before any compiler or
;;; eval() guard (#125) is ever reached. Fixed by sharing the same
;;; check_c_stack_depth guard #125 added, called from read_datum,
;;; read_list, and read_list_tail. Exercised here via `read` on a
;;; string port -- the reader is a single shared implementation (unlike
;;; eval() vs. the compilers), so this genuinely exercises the same
;;; code a top-level script parse would.
;; 1000000, not the ~15000-60000 that was enough to crash the pre-fix
;; binary: same lesson as #125's own test_cli.sh threshold (see that
;; file's comment), compounded by BOTH known sources of variance found
;; there -- the guard fires at a FRACTION of the real per-thread stack
;; limit, and (a) CTest's test-runner process launches this script
;; under a much larger default stack (64MB) than an interactive shell
;; (8MB), while (b) a Release build's optimizer shrinks read_list/
;; read_list_tail/read_datum's own per-recursion-level stack frame
;; enough that a given recursion depth needs even more levels to reach
;; the same threshold. Confirmed empirically: Release-build + 64MB
;; stack (the worst combination actually seen, on CI) needed >600000
;; for the tightest of the three vectors (nested parens). 1000000 is
;; comfortably past all three under that combination, confirmed fast
;; (well under a second) even at this size.
(define (jit129-flat-list-source n)
  (let ((out (open-output-string)))
    (write-char #\( out)
    (do ((i 0 (+ i 1))) ((= i n))
      (write-char #\1 out) (write-char #\space out))
    (write-char #\) out)
    (get-output-string out)))
(check "reader: oversized flat list raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (read (open-input-string (jit129-flat-list-source 1000000))))
       'caught)
(check "reader: deeply nested parens raise a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (read (open-input-string
                (string-append (make-string 1000000 #\() "1" (make-string 1000000 #\))))))
       'caught)
(check "reader: long quote-chain raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (read (open-input-string (string-append (make-string 1000000 #\') "1"))))
       'caught)
(check "reader: ordinary lists/nesting/quoting still read correctly"
       (read (open-input-string "(1 2 (3 (4 5)) 'x)"))
       '(1 2 (3 (4 5)) (quote x)))

;;; Issue #132: independent security review of the #127/#128 fix found
;;; the identical unchecked-`rest`-destructure crash across a much
;;; wider set of eval.c special forms than any single prior issue
;;; tracked -- if/lambda/define/set!/define-values/quasiquote/
;;; call-with-values/call-cc/guard/import(#:keyword) all SIGSEGVed on a
;;; too-short argument list. Each of these already had a compiled-path
;;; equivalent (compiler_classic.c/ir_lower.c) that validated correctly
;;; for the same input -- only the tree-walker was missing the check.
(define (jit132-malformed-raises? thunk)
  (guard (e (#t #t)) (thunk) #f))
(check "if: missing arguments raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(if) (interaction-environment))))
       #t)
(check "if: missing consequent raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(if #t) (interaction-environment))))
       #t)
(check "lambda: missing params raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(lambda) (interaction-environment))))
       #t)
(check "define: missing name/expr raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define) (interaction-environment))))
       #t)
(check "set!: missing arguments raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(set!) (interaction-environment))))
       #t)
(check "set!: missing value raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(set! x) (interaction-environment))))
       #t)
(check "define-values: missing arguments raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define-values) (interaction-environment))))
       #t)
(check "define-values: missing expr raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define-values (a)) (interaction-environment))))
       #t)
(check "quasiquote: missing operand raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(quasiquote) (interaction-environment))))
       #t)
(check "call-with-values: missing arguments raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(call-with-values) (interaction-environment))))
       #t)
(check "call-with-values: missing consumer raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(call-with-values (lambda () 1)) (interaction-environment))))
       #t)
(check "call/cc: missing operand raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(call/cc) (interaction-environment))))
       #t)
(check "guard: missing var-clauses list raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(guard) (interaction-environment))))
       #t)
(check "guard: empty var-clauses list raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(guard ()) (interaction-environment))))
       #t)
(check "guard: missing body doesn't crash (returns void)"
       (jit132-malformed-raises? (lambda () (eval '(guard (e)) (interaction-environment)) #f))
       #f)
(check "import: #:keyword with no argument raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(import (curry crypto) #:prefix) (interaction-environment))))
       #t)
(check "if/lambda/define/set!/define-values/quasiquote/call-with-values/call-cc/guard still work correctly"
       (list (if #t 1) (if #f 1 2) ((lambda () 5))
             (let () (define x 10) x)
             (let ((y 1)) (set! y 2) y)
             (let-values (((a) (values 1))) a)
             `(1 ,(+ 1 1))
             (call-with-values (lambda () (values 1 2)) +)
             (guard (e (#t 'caught)) (raise 'oops))
             (call/cc (lambda (k) (k 42))))
       (list 1 2 5 10 2 1 '(1 2) 3 'caught 42))

;;; A second round of independent code/security review of the #132 fix
;;; found more crashes it didn't cover: `(let)`/`(let*)`/`(letrec)`/
;;; `(letrec*)`/`(let-syntax)`/`(letrec-syntax)` -- no bindings/body at
;;; all, missed since the first pass only checked forms with a fixed
;;; small number of required arguments, not this "at least the
;;; bindings position must exist" family -- plus a whole separate class
;;; the first pass didn't consider: an IMPROPER (non-nil, non-pair)
;;; body, e.g. `(let () . 5)`, reaches vcdr(body) on a non-pair in the
;;; body-sequencing loop nearly every form in this file shares. Also
;;; found: `(set! 1 2)` was a type-confusion bug, not just a crash --
;;; sym_cstr(sym) read a non-Symbol object's memory at the wrong
;;; struct-field offset.
(check "let: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let) (interaction-environment))))
       #t)
(check "let*: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let*) (interaction-environment))))
       #t)
(check "letrec: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(letrec) (interaction-environment))))
       #t)
(check "letrec*: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(letrec*) (interaction-environment))))
       #t)
(check "let-syntax: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let-syntax) (interaction-environment))))
       #t)
(check "letrec-syntax: missing bindings/body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(letrec-syntax) (interaction-environment))))
       #t)
(check "let-syntax: empty bindings and no body doesn't crash (returns void)"
       (jit132-malformed-raises? (lambda () (eval '(let-syntax ()) (interaction-environment)) #f))
       #f)
(check "when: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(when #t . 5) (interaction-environment))))
       #t)
(check "unless: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(unless #f . 5) (interaction-environment))))
       #t)
(check "let: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let () . 5) (interaction-environment))))
       #t)
(check "let*: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let* () . 5) (interaction-environment))))
       #t)
(check "letrec: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(letrec () . 5) (interaction-environment))))
       #t)
(check "let-values: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let-values () . 5) (interaction-environment))))
       #t)
(check "let-syntax: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let-syntax () . 5) (interaction-environment))))
       #t)
(check "lambda: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '((lambda () . 5)) (interaction-environment))))
       #t)
(check "named let: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(let loop () . 5) (interaction-environment))))
       #t)
(check "delay: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(delay . 5) (interaction-environment))))
       #t)
(check "delay-force: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(delay-force . 5) (interaction-environment))))
       #t)
(check "guard: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(guard (e) . 5) (interaction-environment))))
       #t)
(check "guard: else clause with no body doesn't crash (returns void)"
       (jit132-malformed-raises? (lambda () (eval '(guard (e (else)) (raise 1)) (interaction-environment)) #f))
       #f)
(check "guard: else clause with improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(guard (e (else . 2)) (raise 1)) (interaction-environment))))
       #t)
(check "guard: matched-test clause with improper body doesn't crash (falls back to test value)"
       (eval '(guard (e (#t . 2)) (raise 1)) (interaction-environment))
       #t)
(check "parameterize: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(parameterize () . 5) (interaction-environment))))
       #t)
(check "do: improper result-clause tail raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(do () (#t . 2)) (interaction-environment))))
       #t)
(check "begin: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(begin . 5) (interaction-environment))))
       #t)
(check "set!: non-symbol name raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(set! 1 2) (interaction-environment))))
       #t)
;; A third round of independent review found two more sites in the
;; same class: define's symbol-name branch had its own separate
;; ternary for the missing-value case that require_body_shape's sweep
;; hadn't reached (only the lambda-sugar branch got it), and
;; with-assumptions delegates to eval_body (runtime.c) -- the shared
;; closure-application trampoline -- which had never been guarded
;; against an improper body at all.
(check "define: improper tail (symbol-name branch) raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define x . 5) (interaction-environment))))
       #t)
(check "with-assumptions: improper body raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(with-assumptions () . 5) (interaction-environment))))
       #t)
(check "let/let*/letrec/let-syntax/when/unless/guard/do/begin still work correctly with ordinary bodies"
       (list (let () 1 2 3) (let* () 4) (letrec () 5) (let-syntax () 6)
             (when #t 'yes) (unless #f 'also-yes)
             (do () (#t 'done))
             (begin 1 2 3)
             (let ((x 1)) (set! x 2) x)
             (guard (e (else 'e)) (raise 1))
             (guard (e (#t)) (raise 1)))
       (list 3 4 5 6 'yes 'also-yes 'done 3 2 'e #t))

;;; Issue #133: the printer (write/display, src/port.c) has the same
;;; unbounded-recursion class #129 fixed for the reader, but for
;;; runtime-constructed data rather than source text: ws_count_refs/
;;; ws_write (the write/display-shared machinery) and plain scm_write
;;; (write-simple, and the debugger's own value printing) all recurse
;;; once per level of CAR nesting with no bound. A deeply car-nested
;;; list built at runtime (never going through the reader, so #129's
;;; own fix doesn't cover it) previously SIGSEGVed on write/display/
;;; write-simple. Fixed by sharing check_c_stack_depth (the same guard
;;; #125/#129 already share) at all three functions' own entries.
;; 1000000, not a smaller depth: same lesson as #125/#129's own test
;; thresholds -- the guard fires at a fraction of the real per-thread
;; stack limit, and a Release build's optimizer shrinks ws_write's own
;; per-recursion-level stack frame enough that 500000 (already
;; confirmed reliable under Debug builds and under write-simple's
;; simpler, unshared scm_write path at any build type) stopped
;; reaching the guard's threshold under Release+64MB-stack specifically
;; -- confirmed empirically that 800000 was the minimum reliable depth
;; there; 1000000 gives comfortable margin, confirmed fast (well under
;; a second) even at this size.
(define (jit133-build-deep n x)
  (if (= n 0) x (jit133-build-deep (- n 1) (list x))))
(check "write: deeply car-nested runtime list raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (write (jit133-build-deep 1000000 1) (open-output-string)))
       'caught)
(check "write-simple: deeply car-nested runtime list raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (write-simple (jit133-build-deep 1000000 1) (open-output-string)))
       'caught)
(check "display: deeply car-nested runtime list raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (display (jit133-build-deep 1000000 1) (open-output-string)))
       'caught)
(check "write/write-simple/display: ordinary depth still works correctly"
       (let ((out (open-output-string)))
         (write (jit133-build-deep 50 1) out)
         (string-length (get-output-string out)))
       101)

;;; Issue #134 (found via independent security review of the fix itself):
;;; scm_equal/val_hash (src/set.c) have the same unbounded-recursion
;;; class, but reachable from ordinary Scheme data with no symbolic
;;; module involved at all -- a deeply nested list compared with
;;; `equal?` (or hashed) SIGSEGVed. Unlike the symbolic-CAS construction
;;; functions, building a list this way costs O(depth), not O(depth^2)
;;; (no re-simplification pass), so a large N here stays fast without
;;; needing to constrain the process's own stack via ulimit the way
;;; test_cli.sh's own #134 test does.
(check "equal?: deeply nested list raises a catchable stack-overflow, not a SIGSEGV"
       (guard (e (#t 'caught))
         (equal? (jit133-build-deep 3000000 1) (jit133-build-deep 3000000 1)))
       'caught)
(check "equal?: ordinary depth still works correctly"
       (equal? (jit133-build-deep 50 1) (jit133-build-deep 50 1))
       #t)

;;; Issue #134 (found via independent security review of the fix
;;; itself): num_mul's own separate inline tuple-distribution loop
;;; (src/numeric.c) -- unlike num_add/num_sub/num_neg, which all route
;;; through the already-guarded tuple_binop/tuple_unop -- recursed into
;;; a nested (up-wrapped) tuple with no bound of its own. `up`/`down`
;;; build nesting in O(1) per level with no simplification pass, so
;;; this (like the equal? case above) stays fast at a large depth
;;; without needing test_cli.sh's ulimit constraint. Also confirms
;;; sx_depends_on (used by ∫/limit/series/laplace/fourier/etc as their
;;; first structural check) is now guarded too -- it was the first
;;; thing independent review found still crashing after the initial
;;; #134 fix landed.
(define (jit134-build-up n x)
  (if (= n 0) x (jit134-build-up (- n 1) (up x))))
(check "integrate: deeply nested up-tuple raises a catchable stack-overflow, not a SIGSEGV"
       (let ((x (sym-var 'x)))
         (guard (e (#t 'caught)) (∫ (jit134-build-up 3000000 x) x)))
       'caught)
(check "tuple arithmetic (+ - * on up/down) still works correctly"
       (let ((v (up 1 2 3)))
         (list (- v) (* 2 v) (+ v v) (* (down 1 1 1) v)))
       (list (up -1 -2 -3) (up 2 4 6) (up 2 4 6) 6))

;;; Issue #135: define-record-type and syntax-rules crash on malformed
;;; input on BOTH the compiled and tree-walked paths, unlike every
;;; other issue in this series (#124-#132) -- record_type_build_spec
;;; (src/record_type.c) and sr_compile_fn (src/syntax_rules.c) are each
;;; a single function shared by compiler.c's native codegen and eval.c's
;;; own S_DEFINE_RECORD_TYPE case, so a fix here closes both paths at
;;; once and `eval` genuinely exercises the same code either path would
;;; use, unlike #124-#132's own eval-only tests. Also exercised directly
;;; (not just via eval) in tests/test_cli.sh, confirming the compiled
;;; path specifically.
(check "define-record-type: missing ctor/pred raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define-record-type x) (interaction-environment))))
       #t)
(check "define-record-type: name itself a list raises instead of crashing"
       (jit132-malformed-raises? (lambda () (eval '(define-record-type (x)) (interaction-environment))))
       #t)
(check "define-record-type: non-pair ctor-form raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type point x point? (x px)) (interaction-environment))))
       #t)
(check "define-record-type: non-pair field-spec raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type point (mk-point x) point? y) (interaction-environment))))
       #t)
(check "syntax-rules: ellipsis identifier with nothing after it raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-syntax m (syntax-rules x)) (interaction-environment))))
       #t)
(check "syntax-rules: empty rule raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-syntax m (syntax-rules () ())) (interaction-environment))))
       #t)
(check "define-record-type/syntax-rules still work correctly"
       (eval '(begin
                (define-record-type point (mk-point x y) point? (x point-x) (y point-y set-point-y!))
                (define p (mk-point 1 2))
                (define-syntax my-if (syntax-rules () ((_ c t e) (cond (c t) (else e)))))
                (list (point? p) (point-x p) (point-y p) (my-if #t 'yes 'no)))
             (interaction-environment))
       (list #t 1 2 'yes))

;;; A second round of independent code/security review of the #135 fix
;;; found the R6RS branch of record_type_build_spec had NO validation
;;; at all (the commit's own claim that it was "already defensive" was
;;; wrong), and that a malformed syntax-rules PATTERN (as opposed to a
;;; malformed RULE, already fixed) passed definition-time validation
;;; but crashed sr_transformer_fn's own vcdr(pat) the first time the
;;; macro was actually used -- so a malformed macro could be defined/
;;; loaded successfully and only detonate for whoever later called it.
(check "define-record-type: R6RS field-spec too short raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type x (fields (mutable))) (interaction-environment))))
       #t)
(check "define-record-type: R6RS non-symbol field name raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type x (fields 5)) (interaction-environment))))
       #t)
(check "define-record-type: non-symbol name raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type 5 (fields a)) (interaction-environment))))
       #t)
(check "define-record-type: name itself a pair raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(define-record-type (x) (fields a)) (interaction-environment))))
       #t)
(check "syntax-rules: non-pair pattern raises instead of crashing (at definition, not first use)"
       (jit132-malformed-raises?
        (lambda () (eval '(begin (define-syntax m (syntax-rules () (x 1))) (m)) (interaction-environment))))
       #t)
(check "syntax-rules: fixnum pattern raises instead of crashing"
       (jit132-malformed-raises?
        (lambda () (eval '(begin (define-syntax m (syntax-rules () (5 1))) (m)) (interaction-environment))))
       #t)
(check "%rebuild-syntax-rules: non-pair pattern raises instead of crashing"
       (jit132-malformed-raises?
        (lambda ()
          (eval '(begin
                   (define-syntax m (%rebuild-syntax-rules '() '((x . y)) '...))
                   (m))
                (interaction-environment))))
       #t)
(check "define-record-type R6RS/syntax-rules with valid patterns still work correctly"
       (eval '(begin
                (define-record-type point2 (fields (mutable x) (immutable y) z))
                (define p2 (make-point2 1 2 3))
                (define-syntax my-or (syntax-rules ()
                                       ((_) #f) ((_ a) a)
                                       ((_ a b ...) (let ((t a)) (if t t (my-or b ...))))))
                (list (point2? p2) (point2-x p2) (point2-y p2) (point2-z p2) (my-or #f #f 5)))
             (interaction-environment))
       (list #t 1 2 3 5))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
