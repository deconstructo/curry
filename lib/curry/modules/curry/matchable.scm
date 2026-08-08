;;; (curry matchable) — pattern matching, ported from Alex Shinn's public-domain
;;; match.scm (https://synthcode.com/scheme/match.scm — the portable, fully
;;; syntax-rules implementation distributed as CHICKEN's `matchable` egg,
;;; Chibi's `(chibi match)`, Guile's `(ice-9 match)`, and others).
;;;
;;; Supported pattern forms:
;;;   literal     self-evaluating values (numbers/strings/booleans/chars),
;;;               matched with equal?
;;;   'x          quoted literal, matched with equal?
;;;   _           wildcard, matches anything, binds nothing
;;;   id          variable — binds on first occurrence; a *repeated*
;;;               occurrence of the same identifier must equal? the first
;;;               (non-linear patterns)
;;;   (p1 p2 p3)  list of exactly that length
;;;   (p1 . p2)   pair — p1 against the car, p2 against the cdr
;;;   (p ...)     zero or more repetitions of p (an ellipsis, at the end of
;;;               the list only — no trailing patterns after it; see the
;;;               "not ported" note below for why). ___ is an alias for ...
;;;               (handy inside a syntax-rules template, where a literal
;;;               ... would be read as this macro's own ellipsis).
;;;   #(p1 p2 p3) fixed-length vector pattern
;;;   (and p ...) all subpatterns must match (e.g. (and x pat) binds x to
;;;               the whole value while also requiring it to match pat)
;;;   (or p ...)  at least one subpattern must match; identifiers bound in
;;;               one branch are visible (but only meaningfully bound) in
;;;               the body if that branch is the one that matched
;;;   (not p)     succeeds if p does NOT match; binds nothing
;;;   (? pred p ...)  applies pred to the value first, then matches the
;;;               (and p ...) of any remaining subpatterns
;;;   (= proc p)  applies proc to the value, matches the result against p
;;;   (record pred? (accessor p) ...)  matches if (pred? v) holds, then
;;;               matches (accessor v) against p for each accessor/pattern
;;;               pair — this replaces upstream's $/@/struct/object, which
;;;               all lean on record-type introspection R7RS doesn't expose
;;;               (CHICKEN's generic slot-ref/is-a? has no R7RS equivalent);
;;;               naming accessors explicitly is the honest R7RS-native
;;;               shape rather than pretending positional-by-record-type
;;;               access is possible here.
;;;
;;; Deliberately not ported:
;;;   trailing patterns after ... (e.g. (a b ... c d)), and vector-level
;;;               ellipsis (#(p ...)) — both need a named per-iteration
;;;               binding whose scope has to correctly nest across
;;;               recursive re-entries of the same clause, which is exactly
;;;               the class of bug this port hit repeatedly (see below) and
;;;               isn't worth the additional risk surface for two rarely-
;;;               used variants when plain (p ...) covers the common case
;;;   **1/*../=..  (repetition-count variants beyond plain ...)
;;;   ***          (tree-search patterns)
;;;   get!/set!    (accessor-binding patterns)
;;;   quasiquote patterns
;;;   match-letrec (upstream's own comment calls this "challenge stage —
;;;                unhygienic insertion"; too much risk for too little use)
;;;
;;; On porting a hygiene-dependent algorithm to curry's unhygienic macros:
;;; curry's syntax-rules does not alpha-rename template-introduced
;;; identifiers, so a name reused across nested/recursive expansions of the
;;; *same* clause (not a different clause — this is not the ordinary
;;; "avoid colliding with the caller's own pattern variable" case) can
;;; collide with *itself*: an inner recursive match's own binding shadows
;;; an outer one whose continuation is still pending, silently changing
;;; which value a later match sees. Upstream relies on every Scheme it
;;; targets giving each macro expansion fresh, non-colliding names for
;;; exactly this reason. The fix throughout this file is to never bind a
;;; value (car v)/(cdr v)/(vector-ref v i)/(accessor v) to a name before
;;; using it — pass the expression itself directly into the next match-one
;;; call instead. These are all cheap, pure re-reads, so nothing is lost
;;; by not naming them; the earlier, named-binding version of this file
;;; produced silently wrong bindings on patterns nested three or more
;;; levels deep (confirmed via (match (list 1 (list 2 3) 4)
;;; ((a (b c) d) (list a b c d))) returning (1 2 3 3) instead of
;;; (1 2 3 4) — "d" picked up "c"'s value — before this fix).
;;;
;;; API: match match-lambda match-lambda* match-let match-let*
;;;
;;; Everything else exported below (match-next, match-one, match-two, ...)
;;; looks like internal machinery and is — but it must still be exported.
;;; curry's syntax-rules is not hygienic across define-library boundaries
;;; either: match's own expansion contains bare references to match-next,
;;; which itself expands into match-one, and so on, all resolved in the
;;; *importer's* environment. Any of these left unexported would make
;;; using match (not importing it — using it) fail with unbound-variable
;;; the first time some clause happened to reach that helper.

(define-library (curry matchable)
  (import (scheme base))
  (export
    match match-lambda match-lambda* match-let match-let* match-let/aux match-named-let
    match-next match-one match-two
    match-check-ellipsis match-check-identifier
    match-drop-ids
    match-gen-or match-gen-or-step
    match-gen-ellipsis
    match-vector match-vector-step
    match-extract-vars match-extract-vars-step
    match-record-refs)
  (begin

;;; ── Portable identifier/ellipsis introspection ─────────────────────────
;;;
;;; Three tricks upstream's "else" (non-chibi/non-chicken) branch uses,
;;; each confirmed against curry's actual syntax-rules before porting the
;;; rest of this file: nesting a let-syntax whose *literals list* is built
;;; from already-known identifiers turns "is x already one of these ids"
;;; into an ordinary syntax-rules dispatch — if x is a declared literal,
;;; only a literal occurrence of x matches; otherwise x is an ordinary
;;; (always-matching) pattern variable of the nested macro.

(define-syntax match-check-ellipsis
  (syntax-rules ()
    ((_ (a . b) success-k failure-k) failure-k)
    ((_ #(a ...) success-k failure-k) failure-k)
    ((_ id success-k failure-k)
     (let-syntax ((ellipsis? (syntax-rules ()
                               ((ellipsis? (foo id) sk fk) sk)
                               ((ellipsis? other sk fk) fk))))
       (ellipsis? (a b c) success-k failure-k)))))

(define-syntax match-check-identifier
  (syntax-rules ()
    ((_ (x . y) success-k failure-k) failure-k)
    ((_ #(x ...) success-k failure-k) failure-k)
    ((_ x success-k failure-k)
     (let-syntax
         ((sym?
           (syntax-rules ()
             ((sym? x sk fk) sk)
             ((sym? y sk fk) fk))))
       (sym? abracadabra success-k failure-k)))))

;;; ── Utilities ────────────────────────────────────────────────────────────

(define-syntax match-drop-ids
  (syntax-rules ()
    ((_ expr ids ...) expr)))

;;; ── Core dispatch ────────────────────────────────────────────────────────
;;;
;;; match binds the expression once to v, then hands off to match-next,
;;; which tries each clause's pattern in turn via match-one, falling
;;; through to the next clause's failure continuation on a mismatch.

(define-syntax match
  (syntax-rules ()
    ((match (app ...) (pat . body) ...)
     (let ((%m-v (app ...)))
       (match-next %m-v ((app ...) (set! (app ...))) (pat . body) ...)))
    ((match #(vec ...) (pat . body) ...)
     (let ((%m-v #(vec ...)))
       (match-next %m-v (%m-v (set! %m-v)) (pat . body) ...)))
    ((match atom (pat . body) ...)
     (let ((%m-v atom))
       (match-next %m-v (atom (set! atom)) (pat . body) ...)))))

(define-syntax match-next
  (syntax-rules (=>)
    ((match-next v g+s)
     (error "match: no matching pattern"))
    ((match-next v g+s (pat (=> failure) . body) . rest)
     (let ((failure (lambda () (match-next v g+s . rest))))
       (match-one v pat g+s (match-drop-ids (begin . body)) (failure) ())))
    ((match-next v g+s (pat . body) . rest)
     (match-next v g+s (pat (=> %m-failure) . body) . rest))))

(define-syntax match-one
  (syntax-rules ()
    ((match-one v (p q . r) g+s sk fk i)
     (match-check-ellipsis
      q
      (match-extract-vars p (match-gen-ellipsis v p r g+s sk fk i) i ())
      (match-two v (p q . r) g+s sk fk i)))
    ((match-one . x)
     (match-two . x))))

(define-syntax match-two
  (syntax-rules (_ ___ quote ? record = and or not)
    ((match-two v () g+s (sk ...) fk i)
     (if (null? v) (sk ... i) fk))
    ((match-two v (quote p) g+s (sk ...) fk i)
     (if (equal? v 'p) (sk ... i) fk))
    ((match-two v (and) g+s (sk ...) fk i) (sk ... i))
    ((match-two v (and p q ...) g+s sk fk i)
     (match-one v p g+s (match-one v (and q ...) g+s sk fk) fk i))
    ((match-two v (or) g+s sk fk i) fk)
    ((match-two v (or p) . x)
     (match-one v p . x))
    ((match-two v (or p ...) g+s sk fk i)
     (match-extract-vars (or p ...) (match-gen-or v (p ...) g+s sk fk i) i ()))
    ((match-two v (not p) g+s (sk ...) fk i)
     (let ((%m-fk2 (lambda () (sk ... i))))
       (match-one v p g+s (match-drop-ids fk) (%m-fk2) i)))
    ((match-two v (? pred . p) g+s sk fk i)
     (if (pred v) (match-one v (and . p) g+s sk fk i) fk))
    ((match-two v (= proc p) . x)
     (let ((%m-w (proc v))) (match-one %m-w p . x)))
    ((match-two v (p ___ . r) g+s sk fk i)
     (match-extract-vars p (match-gen-ellipsis v p r g+s sk fk i) i ()))
    ((match-two v (p) g+s sk fk i)
     (if (and (pair? v) (null? (cdr v)))
         (match-one (car v) p ((car v) (set-car! v)) sk fk i)
         fk))
    ((match-two v (record pred? (acc p) ...) g+s sk fk i)
     (if (pred? v)
         (match-record-refs v ((acc . p) ...) g+s sk fk i)
         fk))
    ((match-two v (p . q) g+s sk fk i)
     (if (pair? v)
         (match-one (car v) p ((car v) (set-car! v))
                    (match-one (cdr v) q ((cdr v) (set-cdr! v)) sk fk)
                    fk
                    i)
         fk))
    ((match-two v #(p ...) g+s . x)
     (match-vector v 0 () (p ...) . x))
    ((match-two v _ g+s (sk ...) fk i) (sk ... i))
    ;; Not a pair/vector/special literal: a new symbol just binds; an
    ;; already-bound symbol (or any other literal) compares with equal? —
    ;; this is what makes repeated pattern variables non-linear.
    ((match-two v x g+s (sk ...) fk (id ...))
     (match-check-identifier
      x
      (let-syntax
          ((new-sym?
            (syntax-rules (id ...)
              ((new-sym? x sk2 fk2) sk2)
              ((new-sym? y sk2 fk2) fk2))))
        (new-sym? random-sym-to-match
                  (let ((x v)) (sk ... (id ... x)))
                  (if (equal? v x) (sk ... (id ...)) fk)))
      (if (equal? v x) (sk ... (id ...)) fk)))
    ))

;;; ── or ───────────────────────────────────────────────────────────────────

(define-syntax match-gen-or
  (syntax-rules ()
    ((_ v p g+s (sk ...) fk (i ...) ((id id-ls) ...))
     (let ((%m-sk2 (lambda (id ...) (sk ... (i ... id ...))))
           (id (if #f #f)) ...)
       (match-gen-or-step v p g+s (match-drop-ids (%m-sk2 id ...)) fk (i ...))))))

(define-syntax match-gen-or-step
  (syntax-rules ()
    ((_ v () g+s sk fk . x) fk)
    ((_ v (p) . x)
     (match-one v p . x))
    ((_ v (p . q) g+s sk fk i)
     (let ((%m-fk2 (lambda () (match-gen-or-step v q g+s sk fk i))))
       (match-one v p g+s sk (%m-fk2) i)))
    ))

;;; ── Variable extraction ──────────────────────────────────────────────────
;;;
;;; Walks a pattern collecting the new identifiers it binds, calling the
;;; continuation with them as ((orig-var tmp-name) ...). Needed wherever a
;;; sub-match happens in a loop (ellipsis, vector-ellipsis) or needs
;;; unified bindings across branches (or): those all need to know up front
;;; which identifiers to thread through, before generating the loop/branch
;;; code itself. Trimmed to the pattern forms this module actually
;;; supports (drops **1/*../=../***/@/object/get!/set!/quasiquote from the
;;; upstream literals list, adds record).

(define-syntax match-extract-vars
  (syntax-rules (_ ___ ? record = quote and or not)
    ((match-extract-vars (? pred . p) . x)
     (match-extract-vars p . x))
    ((match-extract-vars (record pred? (acc p) ...) . x)
     (match-extract-vars (p ...) . x))
    ((match-extract-vars (= proc p) . x)
     (match-extract-vars p . x))
    ((match-extract-vars (quote x) (k ...) i v)
     (k ... v))
    ((match-extract-vars (and . p) . x)
     (match-extract-vars p . x))
    ((match-extract-vars (or . p) . x)
     (match-extract-vars p . x))
    ((match-extract-vars (not . p) . x)
     (match-extract-vars p . x))
    ((match-extract-vars (p q . r) k i v)
     (match-check-ellipsis
      q
      (match-extract-vars (p . r) k i v)
      (match-extract-vars p (match-extract-vars-step (q . r) k i v) i ())))
    ((match-extract-vars (p . q) k i v)
     (match-extract-vars p (match-extract-vars-step q k i v) i ()))
    ((match-extract-vars #(p ...) . x)
     (match-extract-vars (p ...) . x))
    ((match-extract-vars _ (k ...) i v)   (k ... v))
    ((match-extract-vars ___ (k ...) i v) (k ... v))
    ((match-extract-vars p (k ...) (i ...) v)
     (let-syntax
         ((new-sym?
           (syntax-rules (i ...)
             ((new-sym? p sk fk) sk)
             ((new-sym? any sk fk) fk))))
       (new-sym? random-sym-to-match
                 (k ... ((p p-ls) . v))
                 (k ... v))))
    ))

(define-syntax match-extract-vars-step
  (syntax-rules ()
    ((_ p k i v ((v2 v2-ls) ...))
     (match-extract-vars p k (v2 ... . i) ((v2 v2-ls) ... . v)))
    ))

;;; ── Ellipsis (ellipsis) ────────────────────────────────────────────────────
;;;
;;; Scoped to the common case only: (p ...) with nothing after the ellipsis
;;; at this list level. Upstream additionally supports a fixed number of
;;; trailing patterns after the ellipsis (e.g. (a b ... c d)); porting that
;;; correctly needs the same care this file's other recursive clauses
;;; needed to avoid curry's unhygienic-macro name-collision class of bug,
;;; and isn't done here — a pattern using it raises a clear error instead
;;; of silently mismatching.
;;;
;;; No named per-iteration binding for the element just pulled off the
;;; list (car ls) — same fix as match-two's (p . q)/(p) clauses: a fresh
;;; let each loop iteration would still be fine here (its scope doesn't
;;; outlive the iteration), but there is no reason to reintroduce a named
;;; binding pattern that already bit us once in this file.

(define-syntax match-gen-ellipsis
  (syntax-rules ()
    ((_ v p () g+s (sk ...) fk i ((id id-ls) ...))
     (if (list? v)
         (let loop ((ls v) (id-ls '()) ...)
           (cond
             ((null? ls)
              (let ((id (reverse id-ls)) ...) (sk ... i)))
             ((pair? ls)
              (match-one (car ls) p ((car ls) (set-car! ls))
                         (match-drop-ids (loop (cdr ls) (cons id id-ls) ...))
                         fk i))
             (else fk)))
         fk))
    ((_ v p r g+s sk fk i vars)
     (error "match: trailing patterns after ... are not supported by (curry matchable)"))))

;;; ── Vectors ──────────────────────────────────────────────────────────────
;;;
;;; Fixed-length vector patterns only — #(p ...) (a vector-level ellipsis,
;;; matching a variable-length vector) is not supported, for the same
;;; reason trailing-after-ellipsis isn't: not worth the additional
;;; hygiene-risk surface for what upstream itself calls "just more of the
;;; same" as list ellipsis.

(define-syntax match-vector
  (syntax-rules ()
    ((_ v n ((pat index) ...) () sk fk i)
     (if (vector? v)
         (if (= (vector-length v) n)
             (match-vector-step v ((pat index) ...) sk fk i)
             fk)
         fk))
    ((_ v n (pats ...) (p . q) sk fk i)
     (match-vector v (+ n 1) (pats ... (p n)) q sk fk i))))

(define-syntax match-vector-step
  (syntax-rules ()
    ((_ v () (sk ...) fk i) (sk ... i))
    ((_ v ((pat index) . rest) sk fk i)
     (match-one (vector-ref v index) pat
                ((vector-ref v index) (vector-set! v index))
                (match-vector-step v rest sk fk)
                fk i))))

;;; ── Records ──────────────────────────────────────────────────────────────
;;;
;;; (record pred? (accessor p) ...) — see the module header comment for why
;;; this differs from upstream's $/@/struct/object.

(define-syntax match-record-refs
  (syntax-rules ()
    ((_ v ((acc . p) . q) g+s sk fk i)
     (match-one (acc v) p ((acc v) (error "match: record patterns are read-only"))
                (match-record-refs v q g+s sk fk) fk i))
    ((_ v () g+s (sk ...) fk i) (sk ... i))))

;;; ── Sugar ────────────────────────────────────────────────────────────────

(define-syntax match-lambda
  (syntax-rules ()
    ((_ (pattern . body) ...) (lambda (expr) (match expr (pattern . body) ...)))))

(define-syntax match-lambda*
  (syntax-rules ()
    ((_ (pattern . body) ...) (lambda expr (match expr (pattern . body) ...)))))

(define-syntax match-let
  (syntax-rules ()
    ((_ ((var value) ...) . body)
     (match-let/aux () () ((var value) ...) . body))
    ((_ loop ((var init) ...) . body)
     (match-named-let loop () ((var init) ...) . body))))

(define-syntax match-let/aux
  (syntax-rules ()
    ((_ ((var expr) ...) () () . body)
     (let ((var expr) ...) . body))
    ((_ ((var expr) ...) ((pat tmp) ...) () . body)
     (let ((var expr) ...)
       (match-let* ((pat tmp) ...)
         . body)))
    ((_ (v ...) (p ...) (((a . b) expr) . rest) . body)
     (match-let/aux (v ... (tmp expr)) (p ... ((a . b) tmp)) rest . body))
    ((_ (v ...) (p ...) ((#(a ...) expr) . rest) . body)
     (match-let/aux (v ... (tmp expr)) (p ... (#(a ...) tmp)) rest . body))
    ((_ (v ...) (p ...) ((a expr) . rest) . body)
     (match-let/aux (v ... (a expr)) (p ...) rest . body))))

(define-syntax match-named-let
  (syntax-rules ()
    ((_ loop ((pat expr var) ...) () . body)
     (let loop ((var expr) ...)
       (match-let ((pat var) ...)
         . body)))
    ((_ loop (v ...) ((pat expr) . rest) . body)
     (match-named-let loop (v ... (pat expr tmp)) rest . body))))

(define-syntax match-let*
  (syntax-rules ()
    ((_ () . body)
     (let () . body))
    ((_ ((pat expr) . rest) . body)
     (match expr (pat (match-let* rest . body))))))

  )) ;; end begin, define-library
