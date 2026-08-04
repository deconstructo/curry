;;; (curry logic) — Pluggable non-classical logic framework
;;;
;;; A logic is a first-class value: a record holding the algebraic structure
;;; of a truth domain. Swap logics in and out with `with-logic`. Build
;;; knowledge bases that accumulate evidence without exploding on contradiction.
;;;
;;; Built-in logics:
;;;   classical-logic       — Boolean {#f, #t}; explosion on contradiction
;;;   belnap-four           — {N, F, T, B}; paraconsistent; no explosion
;;;   fuzzy-logic           — [0.0, 1.0]; Łukasiewicz/Gödel min/max
;;;   intuitionistic-logic  — {refuted, open, proved}; ¬¬P ≠ P; no LEM
;;;   probabilistic-logic   — [0.0, 1.0]; Bayesian joint/disjunction
;;;   defeasible-logic      — {strict-false…strict-true}; defeats by priority
;;;
;;; NOTE on `bottom` vs truth-ordering minimum:
;;;   `bottom` means "no information yet" — the seed value for kb-query on an
;;;   absent proposition.  For logics with a bilattice structure (Belnap, intuit.)
;;;   this is the information-ordering minimum, NOT the truth-ordering minimum.
;;;   e.g., intuitionistic bottom = 'open (no evidence), not 'refuted (evidence
;;;   of falsity).  The two orderings are independent.
;;;
;;; Example:
;;;   (import (curry logic))
;;;
;;;   (let ((kb (make-kb belnap-four)))
;;;     (kb-assert! kb 'P 'T)   ; source A says P is true
;;;     (kb-assert! kb 'P 'F)   ; source B says P is false
;;;     (kb-true? kb 'P)        => #t   -- it IS true (partially)
;;;     (kb-false? kb 'P)       => #t   -- AND false  (partially)
;;;     (kb-consistent? kb))    => #f   -- contradiction detected, not exploded
;;;
;;; Reference: "The Anarchist's Curry Cookbook", Chapter: The Laws Are Not
;;;            What They Seem.

(define-library (curry logic)
  (import (scheme base))
  (export
    logic? logic-name logic-bottom logic-top logic-meet logic-join
    logic-complement logic-implies logic-entails? logic-combine logic-display-tv
    make-logic
    current-logic with-logic
    l-and l-or l-not l-implies l-holds? l-top l-bot
    ⊓ ⊔ ∼ ⊃ ⊨ ⊤ ⊥
    kb? kb-logic kb-facts kb-rules
    make-kb kb-assert! kb-retract! kb-query kb-true? kb-false?
    kb-contradictions kb-consistent? kb-propositions kb-add-rule! kb-close!
    classical-logic
    belnap-four
    make-fuzzy-logic fuzzy-logic fuzzy-logic/product
    intuitionistic-logic
    probabilistic-logic
    defeasible-logic)
  (begin

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logic system record
;;; ══════════════════════════════════════════════════════════════════════════

(define-record-type <logic>
  (%make-logic name bottom top meet join complement implies entails? combine display-tv)
  logic?
  (name        logic-name)
  (bottom      logic-bottom)      ;; "no information" / absent-proposition sentinel
  (top         logic-top)         ;; most true
  (meet        logic-meet)        ;; (meet a b) → AND
  (join        logic-join)        ;; (join a b) → OR
  (complement  logic-complement)  ;; (complement a) → NOT
  (implies     logic-implies)     ;; (implies a b) → IMPLIES
  (entails?    logic-entails?)    ;; (entails? v) → bool: is v "true enough"?
  (combine     logic-combine)     ;; (combine old new) → merged evidence
  (display-tv  logic-display-tv)) ;; (display-tv v port) → void

;;; Valid keyword arguments for make-logic.
(define %logic-kwarg-keys '(implies: entails?: combine: display:))

;;; (make-logic name bottom top meet join complement . kwargs)
;;;
;;; Required positional: name (symbol), bottom, top, meet, join, complement.
;;; Optional keyword args as alternating symbol/value pairs:
;;;   'implies:   proc   — (proc a b) → implication (default: ¬a∨b)
;;;   'entails?:  proc   — (proc v) → bool (default: equal? v top)
;;;   'combine:   proc   — (proc old new) → merged truth value (default: join)
;;;   'display:   proc   — (proc v port) → void (default: write)
(define (make-logic name bottom top meet join complement . kwargs)
  (define (kw key default)
    (let loop ((kws kwargs))
      (cond ((null? kws) default)
            ((eq? (car kws) key) (cadr kws))
            (else (loop (cddr kws))))))
  ;; Validate kwargs: each key must be known and must have a following value.
  (let loop ((kws kwargs))
    (unless (null? kws)
      (let ((k (car kws)))
        (unless (pair? (cdr kws))
          (error "make-logic: keyword argument missing value" k))
        (unless (memq k %logic-kwarg-keys)
          (error "make-logic: unknown keyword argument" k))
        (loop (cddr kws)))))
  (%make-logic
    name bottom top meet join complement
    (kw 'implies:  (lambda (a b) (join (complement a) b)))
    (kw 'entails?: (lambda (v) (equal? v top)))
    (kw 'combine:  join)
    (kw 'display:  (lambda (v p) (write v p)))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Dynamic logic parameter and with-logic
;;; ══════════════════════════════════════════════════════════════════════════

(define current-logic (make-parameter #f))

(define-syntax with-logic
  (syntax-rules ()
    ((_ logic body ...)
     (parameterize ((current-logic logic))
       body ...))))

(define (%require-logic who)
  (or (current-logic)
      (error (string-append who ": no current-logic — wrap in (with-logic ...)"))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Connectives — dispatch through current-logic
;;; ══════════════════════════════════════════════════════════════════════════

(define (l-and a b)     ((logic-meet     (%require-logic "l-and")) a b))
(define (l-or  a b)     ((logic-join     (%require-logic "l-or"))  a b))
(define (l-not a)       ((logic-complement (%require-logic "l-not")) a))
(define (l-implies a b) ((logic-implies  (%require-logic "l-implies")) a b))
(define (l-holds? v)    ((logic-entails? (%require-logic "l-holds?")) v))
(define (l-top)         (logic-top    (%require-logic "l-top")))
(define (l-bot)         (logic-bottom (%require-logic "l-bot")))

;;; Unicode aliases
(define ⊓ l-and)
(define ⊔ l-or)
(define ∼ l-not)
(define ⊃ l-implies)
(define ⊨ l-holds?)
(define ⊤ l-top)
(define ⊥ l-bot)

;;; ══════════════════════════════════════════════════════════════════════════
;;; Knowledge base
;;; ══════════════════════════════════════════════════════════════════════════

(define-record-type <kb>
  (%make-kb logic facts rules)
  kb?
  (logic kb-logic)
  (facts kb-facts)                     ;; hash-table: proposition → truth-value
  (rules kb-rules kb-set-rules!))      ;; list of (prop . (kb → truth-value))

(define (make-kb logic)
  (%make-kb logic (make-hash-table) '()))

;;; Assert a truth value for a proposition.
;;; The new value is combined with any existing value using logic-combine.
;;; Returns the resulting (merged) truth value.
(define (kb-assert! kb prop val)
  (let* ((logic  (kb-logic kb))
         (facts  (kb-facts kb))
         (old    (hash-table-ref facts prop (logic-bottom logic)))
         (merged ((logic-combine logic) old val)))
    (hash-table-set! facts prop merged)
    merged))

;;; Remove a proposition from the kb entirely.
(define (kb-retract! kb prop)
  (hash-table-delete! (kb-facts kb) prop))

;;; Return the current truth value for prop (bottom if absent).
(define (kb-query kb prop)
  (hash-table-ref (kb-facts kb) prop (logic-bottom (kb-logic kb))))

;;; Is prop "true" (its truth value is designated)?
(define (kb-true? kb prop)
  ((logic-entails? (kb-logic kb)) (kb-query kb prop)))

;;; Is prop "false" (the complement of its value is designated)?
;;; In paraconsistent logics kb-true? and kb-false? can BOTH return #t.
(define (kb-false? kb prop)
  ((logic-entails? (kb-logic kb))
   ((logic-complement (kb-logic kb)) (kb-query kb prop))))

;;; Return a list of propositions where both P and ¬P are designated.
;;; Short-circuits: stops at the first contradiction only if you use kb-consistent?.
(define (kb-contradictions kb)
  (let* ((logic (kb-logic kb))
         (ent   (logic-entails? logic))
         (neg   (logic-complement logic)))
    (let loop ((pairs (hash-table->alist (kb-facts kb))) (acc '()))
      (if (null? pairs)
          acc
          (let ((val (cdar pairs)))
            (loop (cdr pairs)
                  (if (and (ent val) (ent (neg val)))
                      (cons (caar pairs) acc)
                      acc)))))))

;;; Short-circuits on first contradiction found.
(define (kb-consistent? kb)
  (let* ((logic (kb-logic kb))
         (ent   (logic-entails? logic))
         (neg   (logic-complement logic)))
    (let loop ((pairs (hash-table->alist (kb-facts kb))))
      (cond
        ((null? pairs) #t)
        ((let ((v (cdar pairs))) (and (ent v) (ent (neg v)))) #f)
        (else (loop (cdr pairs)))))))

;;; List all propositions with known (non-bottom) truth values.
(define (kb-propositions kb)
  (let ((bot (logic-bottom (kb-logic kb))))
    (map car
         (filter (lambda (pair) (not (equal? (cdr pair) bot)))
                 (hash-table->alist (kb-facts kb))))))

;;; Add a forward-chaining rule: (proc kb) → truth-value, combined into prop.
(define (kb-add-rule! kb prop proc)
  (kb-set-rules! kb (cons (cons prop proc) (kb-rules kb))))

;;; Run forward-chaining rules to a fixed point.
;;; Errors if no fixed point is reached within max-iter iterations (default 1000).
(define (kb-close! kb . args)
  (let ((max-iter (if (pair? args) (car args) 1000)))
    (let loop ((i 0))
      (when (> i max-iter)
        (error "kb-close!: no fixed point reached after" max-iter "iterations — combine may be non-monotone"))
      (let ((changed #f))
        (for-each
          (lambda (rule)
            (let* ((prop   (car rule))
                   (before (kb-query kb prop))
                   (after  (kb-assert! kb prop ((cdr rule) kb))))
              (unless (equal? before after)
                (set! changed #t))))
          (kb-rules kb))
        (when changed (loop (+ i 1)))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Classical logic (baseline sanity)
;;; ══════════════════════════════════════════════════════════════════════════

(define classical-logic
  (make-logic 'classical-logic
    #f #t
    (lambda (a b) (and a b))
    (lambda (a b) (or  a b))
    (lambda (a)   (not a))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Belnap FOUR (paraconsistent)
;;;
;;; Truth values: N (neither), F (false), T (true), B (both)
;;;
;;; Two independent orderings form a bilattice:
;;;   Truth ordering:  F ≤ {N, B} ≤ T   (N and B are incomparable)
;;;   Info  ordering:  N ≤ {F, T} ≤ B   (F and T are incomparable)
;;;
;;; Logical connectives (∧, ∨, ¬) are the meet/join in the TRUTH ordering.
;;; kb-assert! uses the INFORMATION ordering join to accumulate evidence.
;;;
;;; Designated (entailed) values: T and B — both contain some true information.
;;; This is why contradictions don't explode: B just means "we have contradictory
;;; evidence"; it doesn't make every other proposition true.
;;; ══════════════════════════════════════════════════════════════════════════

(define (%belnap-meet a b)
  ;; glb in truth ordering; F ≤ {N,B} ≤ T; N,B incomparable → glb(N,B)=F
  (case a
    ((N) (case b ((N) 'N) ((F) 'F) ((T) 'N) ((B) 'F)))
    ((F) 'F)
    ((T) (case b ((N) 'N) ((F) 'F) ((T) 'T) ((B) 'B)))
    ((B) (case b ((N) 'F) ((F) 'F) ((T) 'B) ((B) 'B)))))

(define (%belnap-join a b)
  ;; lub in truth ordering; lub(N,B)=T
  (case a
    ((N) (case b ((N) 'N) ((F) 'N) ((T) 'T) ((B) 'T)))
    ((F) (case b ((N) 'N) ((F) 'F) ((T) 'T) ((B) 'B)))
    ((T) 'T)
    ((B) (case b ((N) 'T) ((F) 'B) ((T) 'T) ((B) 'B)))))

(define (%belnap-not a)
  ;; ¬N=N, ¬F=T, ¬T=F, ¬B=B (automorphism of truth ordering)
  (case a ((N) 'N) ((F) 'T) ((T) 'F) ((B) 'B)))

(define (%belnap-entails? v)
  ;; Designated = contains true information; T and B qualify
  (or (eq? v 'T) (eq? v 'B)))

(define (%belnap-combine a b)
  ;; lub in INFORMATION ordering: accumulate evidence without losing any
  ;; N (no info) is the identity; F+T = B (contradictory info)
  (case a
    ((N) b)
    ((F) (case b ((N) 'F) ((F) 'F) ((T) 'B) ((B) 'B)))
    ((T) (case b ((N) 'T) ((F) 'B) ((T) 'T) ((B) 'B)))
    ((B) 'B)))

(define belnap-four
  (make-logic 'belnap-four
    'N 'T
    %belnap-meet %belnap-join %belnap-not
    'entails?:  %belnap-entails?
    'combine:   %belnap-combine
    'display:   (lambda (v p)
                  (case v
                    ((N) (display "#<neither>" p))
                    ((F) (display "#<false>"   p))
                    ((T) (display "#<true>"    p))
                    ((B) (display "#<both>"    p))
                    (else (write v p))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Shared display helper for [0,1] logics
;;; ══════════════════════════════════════════════════════════════════════════

(define (%display-pct prefix v p)
  (display prefix p)
  (display (inexact->exact (round (* v 100))) p)
  (display "%" p))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Fuzzy logic (Łukasiewicz / Gödel)
;;;
;;; Truth values: inexact reals in [0.0, 1.0]
;;; AND = min, OR = max, NOT = 1 - x (Gödel t-norm variant)
;;; Entailment: v > threshold (default 0.5)
;;; Combine: max (strongest evidence wins)
;;; ══════════════════════════════════════════════════════════════════════════

(define %fuzzy-meet (lambda (a b) (min a b)))
(define %fuzzy-join (lambda (a b) (max a b)))
(define %fuzzy-not  (lambda (a)   (- 1.0 a)))

(define (make-fuzzy-logic . args)
  ;; Optional first arg: entailment threshold (default 0.5)
  (let ((threshold (if (pair? args) (car args) 0.5)))
    (make-logic 'fuzzy-logic
      0.0 1.0
      %fuzzy-meet %fuzzy-join %fuzzy-not
      'entails?: (lambda (v) (> v threshold))
      'combine:  %fuzzy-join
      'display:  (lambda (v p) (%display-pct "" v p)))))

(define fuzzy-logic (make-fuzzy-logic))

;;; Łukasiewicz strong conjunction (product) variant
(define fuzzy-logic/product
  (make-logic 'fuzzy-logic/product
    0.0 1.0
    (lambda (a b) (* a b))
    (lambda (a b) (- (+ a b) (* a b)))
    %fuzzy-not
    'entails?: (lambda (v) (> v 0.5))
    'combine:  %fuzzy-join))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Intuitionistic logic (constructive / Heyting)
;;;
;;; Truth values: 'refuted, 'open, 'proved
;;; Linear ordering: refuted < open < proved
;;;
;;; The key subversion: NOT(open) = open, so NOT(NOT(open)) = open ≠ proved.
;;; The Law of Excluded Middle (P ∨ ¬P = proved) FAILS for 'open propositions.
;;; You don't get to say "either P is true or it isn't" — you have to PROVE it.
;;;
;;; Implication uses Heyting implication, not material conditional:
;;;   proved → b = b
;;;   open   → refuted = refuted; open → other = proved
;;;   refuted → b = proved (ex falso — if your premise is false, anything follows)
;;;
;;; bottom = 'open: absent propositions have no evidence yet (open question),
;;; not refuted.  combine uses an information join where 'open is the identity
;;; and proof dominates refutation (proofs are monotone in constructive math).
;;; ══════════════════════════════════════════════════════════════════════════

(define (%intuit-meet a b)
  ;; min in {refuted < open < proved}: least upper bound that's ≤ both
  (case a
    ((proved)  b)
    ((refuted) 'refuted)
    ((open)    (if (eq? b 'refuted) 'refuted 'open))))

(define (%intuit-join a b)
  ;; max in {refuted < open < proved}
  (case a
    ((proved)  'proved)
    ((refuted) b)
    ((open)    (if (eq? b 'proved) 'proved 'open))))

(define (%intuit-not a)
  (case a ((proved) 'refuted) ((refuted) 'proved) ((open) 'open)))

(define (%intuit-implies a b)
  ;; Heyting implication: largest c such that (meet a c) ≤ b
  (case a
    ((proved)  b)
    ((open)    (if (eq? b 'refuted) 'refuted 'proved))
    ((refuted) 'proved)))    ; ex falso quodlibet

(define (%intuit-entails? v) (eq? v 'proved))

(define (%intuit-combine a b)
  ;; Information join: 'open is the identity (no evidence yet).
  ;; Any specific evidence supersedes open.  Proof dominates refutation
  ;; because proofs are monotone in constructive mathematics.
  (cond ((eq? a 'open)                           b)
        ((eq? b 'open)                           a)
        ((or (eq? a 'proved) (eq? b 'proved))   'proved)
        (else                                    'refuted)))

(define intuitionistic-logic
  (make-logic 'intuitionistic-logic
    'open 'proved
    %intuit-meet %intuit-join %intuit-not
    'implies:  %intuit-implies
    'entails?: %intuit-entails?
    'combine:  %intuit-combine))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Probabilistic logic (Bayesian)
;;;
;;; Truth values: inexact reals in [0.0, 1.0] — probabilities
;;;
;;; Connectives assume INDEPENDENCE between propositions:
;;;   P(A∧B) = P(A) · P(B)
;;;   P(A∨B) = P(A) + P(B) - P(A)·P(B)
;;;   P(¬A)  = 1 - P(A)
;;;
;;; Combine: Naive Bayes log-odds update, renormalized:
;;;   posterior = a·b / (a·b + (1-a)·(1-b))
;;;
;;; Edge case: when a=1,b=0 or a=0,b=1 (contradictory certainties),
;;; total=0 and the result is undefined; we return 0.5 (maximum uncertainty).
;;; ══════════════════════════════════════════════════════════════════════════

(define (%bayes-combine a b)
  (let* ((joint     (* a b))
         (joint-neg (* (- 1.0 a) (- 1.0 b)))
         (total     (+ joint joint-neg)))
    (if (< total 1e-15)
        0.5      ; contradictory certainties: a=1,b=0 or a=0,b=1 → undefined → 0.5
        (/ joint total))))

(define probabilistic-logic
  (make-logic 'probabilistic-logic
    0.0 1.0
    (lambda (a b) (* a b))
    (lambda (a b) (- (+ a b) (* a b)))
    %fuzzy-not    ; same as fuzzy: P(¬A) = 1 - P(A)
    'entails?:  (lambda (v) (> v 0.5))
    'combine:   %bayes-combine
    'display:   (lambda (v p) (%display-pct "P=" v p))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Defeasible logic (priority-weighted / "unless defeated")
;;;
;;; Truth values (ordered by strength):
;;;   'strict-false < 'weak-false < 'unknown < 'weak-true < 'strict-true
;;;
;;; Strict facts are indefeasible — they cannot be overridden.
;;; Weak facts are defeasible — they hold "by default" unless a stronger
;;; fact defeats them.
;;;
;;; Combine semantics:
;;;   - Strict beats weak in the same direction
;;;   - Same strength, opposite direction → 'unknown (cancelled)
;;;   - Strict-true + strict-false → 'unknown (irresolvable contradiction)
;;;
;;; bottom = 'unknown: absent propositions have no evidence either way.
;;; ══════════════════════════════════════════════════════════════════════════

(define (%defeat-level v)
  ;; O(1) dispatch vs assq walk
  (case v
    ((strict-false) 0) ((weak-false) 1) ((unknown) 2)
    ((weak-true)    3) ((strict-true) 4)
    (else (error "defeasible-logic: not a truth value" v))))

(define (%defeat-meet a b)
  (if (<= (%defeat-level a) (%defeat-level b)) a b))

(define (%defeat-join a b)
  (if (>= (%defeat-level a) (%defeat-level b)) a b))

(define (%defeat-not a)
  (case a
    ((strict-true)  'strict-false)
    ((weak-true)    'weak-false)
    ((unknown)      'unknown)
    ((weak-false)   'weak-true)
    ((strict-false) 'strict-true)))

(define (%defeat-entails? v)
  (or (eq? v 'weak-true) (eq? v 'strict-true)))

(define (%defeat-combine a b)
  (cond
    ((eq? a b)              a)           ; idempotent
    ((eq? a 'unknown)       b)           ; unknown is identity
    ((eq? b 'unknown)       a)
    ;; Strict always beats weak; two stricts in opposition → unknown
    ((eq? a 'strict-true)   (if (eq? b 'strict-false) 'unknown 'strict-true))
    ((eq? a 'strict-false)  (if (eq? b 'strict-true)  'unknown 'strict-false))
    ((eq? b 'strict-true)   'strict-true)
    ((eq? b 'strict-false)  'strict-false)
    ;; Remaining: both are weak, in opposite directions → cancel
    (else                   'unknown)))

(define defeasible-logic
  (make-logic 'defeasible-logic
    'unknown 'strict-true
    %defeat-meet %defeat-join %defeat-not
    'entails?:  %defeat-entails?
    'combine:   %defeat-combine))

  )) ;; end begin, define-library
