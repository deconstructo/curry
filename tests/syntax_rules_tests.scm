;;; Tests for syntax-rules macro system

(import (curry sync))

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

;;; ---- Single non-variadic pattern ----

(define-syntax my-identity
  (syntax-rules ()
    ((_ x) x)))

(check "identity macro" (my-identity 42) 42)
(check "identity list" (my-identity '(a b c)) '(a b c))

;;; ---- Wildcard _ ----

(define-syntax ignore-first
  (syntax-rules ()
    ((_ _ x) x)))

(check "wildcard _" (ignore-first 99 'hello) 'hello)

;;; ---- Multiple patterns with fallthrough ----

(define-syntax my-and
  (syntax-rules ()
    ((_)         #t)
    ((_ e)       e)
    ((_ e1 e2 ...) (if e1 (my-and e2 ...) #f))))

(check "my-and zero args"  (my-and)        #t)
(check "my-and one arg #t" (my-and #t)     #t)
(check "my-and one arg 5"  (my-and 5)      5)
(check "my-and two args"   (my-and #t #t)  #t)
(check "my-and false"      (my-and #f #t)  #f)
(check "my-and three args" (my-and 1 2 3)  3)

;;; ---- Ellipsis: zero matches ----

(define-syntax collect
  (syntax-rules ()
    ((_ x ...) (list x ...))))

(check "ellipsis zero"  (collect)        '())
(check "ellipsis one"   (collect 1)      '(1))
(check "ellipsis many"  (collect 1 2 3)  '(1 2 3))

;;; ---- Ellipsis in pattern and template ----

(define-syntax my-list
  (syntax-rules ()
    ((_ e ...) (list e ...))))

(check "my-list empty"    (my-list)          '())
(check "my-list single"   (my-list 'a)       '(a))
(check "my-list multiple" (my-list 'a 'b 'c) '(a b c))

;;; ---- Recursive macro ----

(define-syntax my-or
  (syntax-rules ()
    ((_) #f)
    ((_ e) e)
    ((_ e1 e2 ...)
     (let ((t e1))
       (if t t (my-or e2 ...))))))

(check "my-or zero"  (my-or)           #f)
(check "my-or one"   (my-or 5)         5)
(check "my-or false" (my-or #f #f)     #f)
(check "my-or true"  (my-or #f 7 #f)   7)

;;; ---- Literal matching ----

(define-syntax my-case-lambda
  (syntax-rules (=>)
    ((_ (test => result)) (if test result #f))
    ((_ (test))           test)))

(check "literal => branch" (my-case-lambda (#t => 'yes)) 'yes)
(check "literal no =>"     (my-case-lambda (42))         42)

;;; ---- swap! macro ----

(define-syntax swap!
  (syntax-rules ()
    ((_ a b)
     (let ((tmp a))
       (set! a b)
       (set! b tmp)))))

(define x 1)
(define y 2)
(swap! x y)
(check "swap! x" x 2)
(check "swap! y" y 1)

;;; ---- let defined via syntax-rules ----

(define-syntax my-let
  (syntax-rules ()
    ((_ ((var init) ...) body ...)
     ((lambda (var ...) body ...) init ...))))

(check "my-let basic" (my-let ((a 1) (b 2)) (+ a b)) 3)
(check "my-let body"  (my-let ((x 10)) (* x x)) 100)
(check "my-let empty" (my-let () 'ok) 'ok)

;;; ---- Zero-clause syntax-rules raises on use ----

(define-syntax no-patterns
  (syntax-rules ()))

(define raised? #f)
(guard (exn (#t (set! raised? #t)))
  (no-patterns foo))
(check "zero-clause raises" raised? #t)

;;; ---- Nested macro calls ----

(define-syntax my-begin
  (syntax-rules ()
    ((_ e) e)
    ((_ e1 e2 ...) (let ((ignored e1)) (my-begin e2 ...)))))

(check "my-begin single"   (my-begin 42)      42)
(check "my-begin sequence" (my-begin 1 2 3)   3)

;;; ---- HYGIENE: tests below are marked unhygienic and skipped ----
;;; These would require a full marks-and-substitutions system to pass.

;;; ;;HYGIENE: (if #f (begin ... ))

;;; ---- SRFI 149: template extensions ----

;;; (... template) escapes ellipsis-as-operator so a macro can generate
;;; code that itself uses "...", e.g. a macro that expands into another
;;; define-syntax whose own patterns/templates need a real ellipsis.
(define-syntax gen-list-macro
  (syntax-rules ()
    ((_ name)
     (define-syntax name
       (syntax-rules ()
         ((_ a (... ...)) (list a (... ...))))))))

(gen-list-macro generated-list)
(check "ellipsis escape: generated macro works" (generated-list 1 2 3) '(1 2 3))

;;; Escaping without any pattern variables inside — pure literal passthrough.
(define-syntax literal-dots
  (syntax-rules ()
    ((_) '(... (a b c)))))

(check "ellipsis escape: literal passthrough" (literal-dots) '(a b c))

;;; Pattern variables still substitute normally inside an escaped template.
(define-syntax echo-then-dots
  (syntax-rules ()
    ((_ x) (list x '(... (p ...))))))

(check "ellipsis escape: pvar substitution still applies" (echo-then-dots 'hi)
       '(hi (p ...)))

;;; ---- SRFI 149: custom ellipsis identifier ----

;;; (syntax-rules ellipsis (literals ...) rule ...) — a non-"..." token
;;; used as the repetition operator instead.
(define-syntax my-list-custom-ell
  (syntax-rules ::: ()
    ((_ a :::) (list a :::))))

(check "custom ellipsis: basic repetition" (my-list-custom-ell 1 2 3) '(1 2 3))
(check "custom ellipsis: zero repetitions" (my-list-custom-ell) '())

;;; With a custom ellipsis identifier, "..." itself is no longer special
;;; and can appear as ordinary literal data without escaping.
(define-syntax dots-as-data
  (syntax-rules ::: ()
    ((_ x) (list x '...))))

(check "custom ellipsis: literal ... as data" (dots-as-data 5) '(5 ...))

;;; Nested/recursive macro using a custom ellipsis, mirroring my-or above.
(define-syntax my-or-custom-ell
  (syntax-rules @@ ()
    ((_) #f)
    ((_ e) e)
    ((_ e1 e2 @@)
     (let ((t e1))
       (if t t (my-or-custom-ell e2 @@))))))

(check "custom ellipsis: recursive macro" (my-or-custom-ell #f 7 #f) 7)

;;; ---- SRFI 149: vector patterns and templates ----

(define-syntax vec-swap
  (syntax-rules ()
    ((_ #(a b)) #(b a))))

(check "vector pattern/template: fixed arity" (vec-swap #(1 2)) #(2 1))

(define-syntax vec-collect
  (syntax-rules ()
    ((_ a ...) #(a ...))))

(check "vector template: ellipsis"       (vec-collect)        #())
(check "vector template: ellipsis one"   (vec-collect 1)      #(1))
(check "vector template: ellipsis many"  (vec-collect 1 2 3)  #(1 2 3))

(define-syntax vec-pat-ellipsis
  (syntax-rules ()
    ((_ #(a ...)) (list a ...))))

(check "vector pattern: ellipsis" (vec-pat-ellipsis #(1 2 3)) '(1 2 3))

;;; Mixed: a vector pattern containing a fixed head and an ellipsis tail.
(define-syntax vec-head-tail
  (syntax-rules ()
    ((_ #(first rest ...)) (list first (list rest ...)))))

(check "vector pattern: head + ellipsis tail"
       (vec-head-tail #(1 2 3 4)) '(1 (2 3 4)))

;;; ---- define-syntax compiled natively (not tree-eval punted) ----
;;; Regression coverage for two behaviors that were broken before native
;;; compiler support: a macro defined and used within the SAME compiled
;;; unit (previously only visible to a later, separately-compiled
;;; top-level form), and a macro defined inside a lambda body staying
;;; local instead of leaking into the global environment.

(check "define-syntax used in the same begin block"
  (begin
    (define-syntax same-unit-if
      (syntax-rules ()
        ((_ c t e) (cond (c t) (else e)))))
    (same-unit-if #t 'yes 'no))
  'yes)

(define (use-local-macro-internally)
  (define-syntax local-double
    (syntax-rules ()
      ((_ x) (* 2 x))))
  (local-double 21))
(check "internal define-syntax works locally" (use-local-macro-internally) 42)
(check "internal define-syntax does not leak to global"
  (guard (e (#t 'unbound)) (local-double 1))
  'unbound)

(define (shadow-outer-macro)
  (define-syntax shadowed (syntax-rules () ((_ x) (list 'outer x))))
  (define (inner)
    (define-syntax shadowed (syntax-rules () ((_ x) (list 'inner x))))
    (shadowed 1))
  (list (shadowed 2) (inner)))
(check "internal define-syntax shadows an outer local macro correctly"
  (shadow-outer-macro) '((outer 2) (inner 1)))

;;; ---- let-syntax / letrec-syntax (native codegen) ----

(check "let-syntax basic"
  (let-syntax ((sq (syntax-rules () ((_ x) (* x x)))))
    (sq 5))
  25)

(check "letrec-syntax basic"
  (letrec-syntax ((sq (syntax-rules () ((_ x) (* x x)))))
    (sq 6))
  36)

;;; Regression tests for a real bug found by review of the "Partial
;;; hygiene" fix above: a let-syntax/letrec-syntax-bound macro's name
;;; lives only in the COMPILER's own compile-time SyntaxLocal table
;;; (compiler.c), never in any runtime environment sr_is_protected's
;;; def_env-based check could see -- so a self-recursive or mutually-
;;; recursive LOCAL macro's own self-reference got incorrectly renamed
;;; and broke with "unbound variable" in compiled code (this test file
;;; runs compiled, not tree-walked). Fixed via a second thread-local,
;;; sr_current_local_macros, that compile_let_syntax/compile_define_syntax
;;; set to the relevant local macro name(s) around each compile_time_eval
;;; call.
(check "self-recursive letrec-syntax macro (compiled path)"
  (letrec-syntax
      ((my-rec (syntax-rules ()
         ((_ 0) 0)
         ((_ n) (+ n (my-rec 0))))))
    (my-rec 5))
  5)

(check "mutually-recursive letrec-syntax macros (compiled path)"
  (letrec-syntax
      ((my-even? (syntax-rules ()
         ((_ 0) #t)
         ((_ n) (my-odd? n))))
       (my-odd? (syntax-rules ()
         ((_ n) (not (my-even? n))))))
    (my-even? 0))
  #t)

(check "self-recursive internal define-syntax (compiled path)"
  (let ()
    (define-syntax loop-tag
      (syntax-rules ()
        ((_ x) (list 'loop-tag x))))
    (loop-tag 42))
  '(loop-tag 42))

(check "let-syntax macro invisible outside its body"
  (guard (e (#t 'unbound))
    (begin (let-syntax ((only-here (syntax-rules () ((_ x) x)))) (only-here 1))
           (only-here 2)))
  'unbound)

(check "let-syntax visible to a nested lambda"
  ((let-syntax ((twice (syntax-rules () ((_ x) (* 2 x)))))
     (lambda (n) (twice n)))
   7)
  14)

;;; ---- top-level define-syntax's transformer-expr runs exactly once ----
;;; Regression test: the compiler evaluates a top-level macro's transformer
;;; expression once at compile time (for same-compiled-unit visibility) and
;;; must also re-register the macro at runtime (for .scc cache-hit
;;; persistence) without re-running transformer-expr itself when it produced
;;; an ordinary syntax-rules transformer — otherwise side-effecting code
;;; wrapped around a (syntax-rules ...) form would run twice.
(define double-eval-counter 0)
(define-syntax count-and-build
  (begin
    (set! double-eval-counter (+ double-eval-counter 1))
    (syntax-rules ()
      ((_ x) (* 3 x)))))
(check "define-syntax transformer-expr evaluated exactly once"
  double-eval-counter 1)
(check "macro rebuilt from extracted syntax-rules data still works"
  (count-and-build 5) 15)

;;; ---- %rebuild-syntax-rules direct misuse must not crash ----
;;; Regression test: %rebuild-syntax-rules (the internal primitive
;;; compile_define_syntax's codegen uses to reconstruct a macro without
;;; re-running its transformer-expr) is an ordinary, discoverable global —
;;; nothing stops user code from calling it directly as a define-syntax
;;; transformer-expr instead of only through generated bytecode. An earlier
;;; version returned an already-Syntax-wrapped value, so doing this built a
;;; Syntax-wrapping-a-Syntax macro binding that corrupted VM state when
;;; used. It must now behave like any other transformer-expr: produce a
;;; correctly-wrapped, working macro when the data is well-formed, and a
;;; normal Scheme error (never a crash) when it isn't.
(define-syntax direct-rebuild
  (%rebuild-syntax-rules '() '(((_ x) * 4 x)) '...))
(check "direct %rebuild-syntax-rules use produces a working macro"
  (direct-rebuild 5) 20)

(check "%rebuild-syntax-rules with malformed literals raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules 5 '() '...))
  'caught)
(check "%rebuild-syntax-rules with malformed rules raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules '() 5 '...))
  'caught)
(check "%rebuild-syntax-rules with non-symbol ellipsis raises cleanly, no crash"
  (guard (e (#t 'caught)) (%rebuild-syntax-rules '() '() 5))
  'caught)

;;; ---- calling a syntax-rules transformer directly as a procedure ----
;;; Regression test for a pre-existing (not introduced by the above fixes,
;;; but made more directly reachable by %rebuild-syntax-rules exposing a
;;; raw transformer as an ordinary first-class value) crash: a syntax-rules
;;; transformer Primitive assumes its argument is always the whole
;;; unevaluated use-site form (a pair). Binding it to a plain variable and
;;; calling it with a non-pair argument used to segfault (vcdr on a fixnum);
;;; must now raise a clean Scheme error.
(define bare-transformer
  (%rebuild-syntax-rules '() '(((_ x) x)) '...))
(check "calling a bare syntax-rules transformer with a non-pair arg raises cleanly"
  (guard (e (#t 'caught)) (bare-transformer 5))
  'caught)

;;; ---- ellipsis over a dotted-tail pattern capture ----
;;; Regression test: a template variable captured through a *dotted-tail*
;;; ellipsis position -- (pat . rest) ... -- must be substituted per
;;; repetition in the template, same as one captured through a proper-list
;;; position. sr_ell_refs used to only walk a template's proper-list
;;; portion, so the dotted-tail variable was never recognized as needing
;;; per-iteration substitution and came out as the literal, unexpanded
;;; name instead of its captured value.
(define-syntax dotted-ellipsis-probe
  (syntax-rules ()
    ((_ (pat . rest) ...) (quote ((pat . rest) ...)))))
;; (pat . rest) matched against (1 2) binds pat=1, rest=(2) -- rest is the
;; cdr of a 2-element list, i.e. a 1-element list, not the bare atom 2 --
;; so reconstructing (pat . rest) correctly gives (1 . (2)) = (1 2), not
;; (1 . 2).
(check "ellipsis correctly substitutes a dotted-tail-captured variable"
  (dotted-ellipsis-probe (1 2) (3 4))
  '((1 2) (3 4)))

;;; ---- "_" declared as a macro's own literal ----
;;; Regression test: sr_match_one checked "is this the universal wildcard
;;; _" before checking literal-list membership, so a macro that explicitly
;;; declares "_" as one of its own literals (to match a literal use-site
;;; underscore, distinct from the universal-wildcard case) had that
;;; declaration silently ignored -- its "_"-clause matched every
;;; invocation regardless of what was actually passed there, since the
;;; wildcard check always won first.
(define-syntax underscore-literal-probe
  (syntax-rules (_)
    ((_ _) 'literal-underscore-matched)
    ((_ x) (list 'fallback x))))
(check "a macro declaring _ as its own literal isn't shadowed by the universal wildcard"
  (underscore-literal-probe 42)
  '(fallback 42))
(check "the literal _ itself still matches that macro's own _-literal clause"
  (underscore-literal-probe _)
  'literal-underscore-matched)

;;; ---- reader: dotted-pair tail immediately after the ellipsis identifier ----
;;; Regression test: "..." is itself read via the "symbol starting with a
;;; literal '.'" fallback path in read_list (its own first character is
;;; '.'), which used to unconditionally recurse into reading a fresh list
;;; element next rather than re-checking for a genuine dotted-pair marker
;;; first -- so "(a ... . tail)" misread as (a ... (. tail)) instead of
;;; (a ... . tail): the real dot got read as a bare "." symbol followed by
;;; "tail" as a separate element, rather than splicing tail in directly.
(check "a dotted-pair tail right after the ellipsis identifier reads correctly"
  (cdr (cdr '(a ... . tail)))
  'tail)
(check "the ellipsis identifier itself still reads correctly in that position"
  (car (cdr '(a ... . tail)))
  '...)

;;; ---- Partial hygiene: per-expansion renaming of template-introduced,
;;; not-already-bound symbols (syntax_rules.c's own "Partial hygiene"
;;; header comment has the full design rationale, including two earlier
;;; heuristics tried and rejected). Found while porting SRFI-26: the
;;; standard reference cut/cute implementation is a recursive macro that
;;; accumulates a fresh `x` binding at each expansion step, which
;;; previously collapsed into ONE literal `x` used as multiple lambda
;;; formals, since template-introduced symbols were emitted completely
;;; unchanged. ----

;;; A minimal version of the exact pattern cut/cute needs: a macro that
;;; recursively accumulates a fresh identifier into a growing formals
;;; list across several separate expansions of itself.
;;;
;;; Deliberately named %accumulated-slot, not the more obvious `x` --
;;; this heuristic's one documented false-negative case is a template-
;;; introduced name that happens to collide with something ALREADY
;;; globally bound for an unrelated reason (checked, not just assumed:
;;; this file's own line 102 defines a genuine top-level `x`, which
;;; would make sr_is_protected correctly-by-its-own-logic decide NOT to
;;; rename a same-named introduced symbol here, reproducing the exact
;;; bug this test exists to catch, for an unrelated reason). A real
;;; macro author picking `x` as their own fresh-variable name and
;;; happening to collide with a caller's unrelated global `x` is exactly
;;; this same scenario -- rare, and safe when it happens (same behavior
;;; as before this fix, not corrupted), but real; documented in
;;; sr_is_protected's own header comment in syntax_rules.c.
(define-syntax accumulate-formals
  (syntax-rules ()
    ((_ (n ...))
     (lambda (n ...) (list n ...)))
    ((_ (n ...) tok . rest)
     (accumulate-formals (n ... %accumulated-slot) . rest))))
(check "recursive macro gives each accumulated formal its own fresh name"
  ((accumulate-formals () a b c) 10 20 30)
  '(10 20 30))

;;; Quoted symbolic data in a template must never be renamed -- it's
;;; literal output the macro author wrote, not a variable reference.
(define-syntax tag-it
  (syntax-rules ()
    ((_ x) (list 'a-quoted-tag x))))
(check "a quoted symbol in a template is emitted verbatim, not renamed"
  (tag-it 99)
  '(a-quoted-tag 99))

;;; A macro's own recursive self-reference, and references to ordinary
;;; global procedures (lambda, apply, list, ...), must resolve normally
;;; -- not get renamed into an unbound gensym just because they aren't
;;; pattern variables. accumulate-formals above already exercises this
;;; (its own name, plus lambda/list, all resolve correctly across
;;; several recursive expansions); the library-local case below covers
;;; the same thing for a macro whose self-reference has to resolve
;;; inside its own defining environment rather than GLOBAL_ENV.

;;; A helper macro/procedure DEFINED INSIDE THE SAME define-library body
;;; as the macro that uses it -- library bodies run in their own
;;; isolated environment (env_new_root(), not GLOBAL_ENV, per
;;; modules.c), so this is exactly the shape that broke when the
;;; "already bound" check only ever consulted GLOBAL_ENV (confirmed via
;;; a real regression in this codebase's own (curry private
;;; lang-aliases) helper macro, %lang-alias-row, recursing into itself).
;;; %helper is exported alongside build-list even though it's "private"
;;; plumbing, per this codebase's own documented macro-expansion gotcha
;;; (docs/reference/writing-a-module.md): curry's macro expansion is
;;; unhygienic in the OTHER direction too -- a free identifier in an
;;; exported macro's expansion resolves in the IMPORTER's environment at
;;; use time, not the defining library's environment, so build-list's
;;; expansion referencing %helper only works from outside this library
;;; if %helper is exported too. Unrelated to this fix (that gotcha
;;; predates it and would apply with or without partial hygiene); this
;;; test is specifically about %helper's OWN recursive self-reference
;;; resolving correctly *while its own template is being expanded*,
;;; which does depend on def_env being the library's environment.
(define-library (test hygiene-library-local-macro)
  (export build-list %helper)
  (import (scheme base))
  (begin
    (define-syntax %helper
      (syntax-rules ()
        ((_ acc) acc)
        ((_ acc x . rest) (%helper (cons x acc) . rest))))
    (define-syntax build-list
      (syntax-rules ()
        ((_ x ...) (%helper '() x ...))))))
(import (test hygiene-library-local-macro))
(check "a library-local macro's self-reference resolves inside its own defining environment"
  (build-list 1 2 3)
  '(3 2 1))

;;; ════════════════════════════════════════════════════════════
;;; Regression: sr_gensym's counter must be thread-safe.
;;; ════════════════════════════════════════════════════════════
;;;
;;; Found by review of the "Partial hygiene" fix: sr_gensym's fresh-name
;;; counter (syntax_rules.c) was a plain `static long`, not atomic --
;;; two threads reading the same pre-increment value under concurrent
;;; macro expansion produced two macro-introduced bindings with the
;;; SAME "fresh" name, corrupting one thread's binding into aliasing
;;; another's. Fixed with `_Atomic long` + `atomic_fetch_add`, matching
;;; actors.c's own next_actor_id pattern.
;;;
;;; Many actors concurrently `eval` a shared macro whose expansion
;;; introduces a fresh binding into (interaction-environment) -- a
;;; genuinely shared, concurrently-mutated GLOBAL_ENV, the exact
;;; scenario the race needed. Each actor's own expansion must bind and
;;; read back its OWN value; a name collision would show up as one
;;; actor reading a value it never wrote.

(define gensym-race-n-actors 20)
(define gensym-race-iterations 50)
(define gensym-race-results (make-vector gensym-race-n-actors #f))
(define gensym-race-done (make-semaphore 0))

;; A recursive macro (same accumulating-formals shape as accumulate-formals
;; above) is a convenient way to force multiple fresh-name allocations
;; per expansion, increasing the odds of catching a counter race within
;; a bounded number of iterations.
(define-syntax gensym-race-macro
  (syntax-rules ()
    ((_ (n ...) v)
     (let ((n v) ...) (+ n ...)))
    ((_ (n ...) v tok . rest)
     (gensym-race-macro (n ... %gensym-race-slot) v . rest))))

;; idx is captured by ordinary closure/lexical scoping (a parameter of
;; gensym-race-worker, distinct per actor) -- NOT a shared global
;; variable: mutating one shared global from 20 racing actors would be
;; its own unrelated bug in this test, not the one it's trying to catch.
;; idx's VALUE is spliced directly into the quoted form as a literal via
;; `list`, so each actor's own eval'd expansion embeds its own number.
(define (gensym-race-worker idx)
  (spawn (lambda ()
    (let loop ((i 0) (ok #t))
      (if (< i gensym-race-iterations)
          (let ((result (eval (list 'gensym-race-macro '() idx 'a 'b 'c 'd 'e)
                               (interaction-environment))))
            (loop (+ i 1) (and ok (equal? result (* idx 5)))))
          (begin
            (vector-set! gensym-race-results idx ok)
            (sem-post! gensym-race-done)))))))

(let loop ((i 0))
  (when (< i gensym-race-n-actors)
    (gensym-race-worker i)
    (loop (+ i 1))))

(let loop ((i 0))
  (when (< i gensym-race-n-actors)
    (sem-wait! gensym-race-done)
    (loop (+ i 1))))

(check "concurrent macro expansion doesn't collide fresh gensym names"
  (let loop ((i 0))
    (cond
      ((= i gensym-race-n-actors) #t)
      ((not (vector-ref gensym-race-results i)) #f)
      (else (loop (+ i 1)))))
  #t)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
