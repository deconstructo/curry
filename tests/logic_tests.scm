;;; logic_tests.scm — (curry logic) pluggable non-classical logic framework
;;;
;;; Tests: classical, belnap-four, fuzzy-logic, intuitionistic-logic,
;;;        probabilistic-logic, defeasible-logic, and the knowledge-base API.

(import (curry logic))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define-syntax check-approx
  (syntax-rules ()
    ((_ label expr expected eps)
     (let ((got expr))
       (if (< (abs (- got expected)) eps)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected ≈ ") (write expected) (newline)
             (display "  got:       ") (write got) (newline)))))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Logic record API
;;; ══════════════════════════════════════════════════════════════════════════

(check "logic? belnap"       (logic? belnap-four)          #t)
(check "logic? classical"    (logic? classical-logic)       #t)
(check "logic? fuzzy"        (logic? fuzzy-logic)           #t)
(check "logic? intuit"       (logic? intuitionistic-logic)  #t)
(check "logic? prob"         (logic? probabilistic-logic)   #t)
(check "logic? defeasible"   (logic? defeasible-logic)      #t)
(check "logic? non-logic"    (logic? 42)                    #f)
(check "logic-name belnap"   (logic-name belnap-four)       'belnap-four)

;;; ══════════════════════════════════════════════════════════════════════════
;;; Classical logic baseline
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic classical-logic
  (check "classical ⊓ T T"  (l-and #t #t) #t)
  (check "classical ⊓ T F"  (l-and #t #f) #f)
  (check "classical ⊔ F F"  (l-or  #f #f) #f)
  (check "classical ⊔ F T"  (l-or  #f #t) #t)
  (check "classical ∼ T"    (l-not #t) #f)
  (check "classical ∼ F"    (l-not #f) #t)
  (check "classical holds T" (l-holds? #t) #t)
  (check "classical holds F" (l-holds? #f) #f)
  (check "classical top"     (l-top) #t)
  (check "classical bot"     (l-bot) #f)
  ;; LEM holds in classical logic
  (check "classical LEM"    (l-or #t (l-not #t)) #t)
  (check "classical LEM F"  (l-or #f (l-not #f)) #t))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Belnap FOUR — paraconsistent
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic belnap-four
  ;; AND truth table (bilattice glb in truth ordering)
  (check "belnap T∧T" (l-and 'T 'T) 'T)
  (check "belnap T∧F" (l-and 'T 'F) 'F)
  (check "belnap T∧N" (l-and 'T 'N) 'N)
  (check "belnap T∧B" (l-and 'T 'B) 'B)
  (check "belnap N∧N" (l-and 'N 'N) 'N)
  (check "belnap N∧F" (l-and 'N 'F) 'F)
  (check "belnap N∧B" (l-and 'N 'B) 'F)  ; N,B incomparable → glb = F
  (check "belnap B∧B" (l-and 'B 'B) 'B)
  (check "belnap F∧B" (l-and 'F 'B) 'F)

  ;; OR truth table (bilattice lub in truth ordering)
  (check "belnap T∨T" (l-or 'T 'T) 'T)
  (check "belnap F∨F" (l-or 'F 'F) 'F)
  (check "belnap F∨T" (l-or 'F 'T) 'T)
  (check "belnap N∨N" (l-or 'N 'N) 'N)
  (check "belnap N∨F" (l-or 'N 'F) 'N)
  (check "belnap N∨T" (l-or 'N 'T) 'T)
  (check "belnap N∨B" (l-or 'N 'B) 'T)  ; N,B incomparable → lub = T
  (check "belnap B∨B" (l-or 'B 'B) 'B)
  (check "belnap B∨F" (l-or 'B 'F) 'B)

  ;; NOT
  (check "belnap ¬T" (l-not 'T) 'F)
  (check "belnap ¬F" (l-not 'F) 'T)
  (check "belnap ¬N" (l-not 'N) 'N)
  (check "belnap ¬B" (l-not 'B) 'B)  ; contradiction is self-complementary!

  ;; Entailment: T and B are designated (contain true information)
  (check "belnap holds T" (l-holds? 'T) #t)
  (check "belnap holds B" (l-holds? 'B) #t)  ; B is "true" (also false — that's ok)
  (check "belnap holds F" (l-holds? 'F) #f)
  (check "belnap holds N" (l-holds? 'N) #f)

  ;; The key property: ¬B = B, so B is both true AND false (designated AND its ¬ is designated)
  (check "belnap B both-true"  (l-holds? 'B)        #t)
  (check "belnap B both-false" (l-holds? (l-not 'B)) #t)

  ;; de Morgan holds
  (check "belnap de Morgan 1" (l-not (l-and 'T 'F)) (l-or (l-not 'T) (l-not 'F)))
  (check "belnap de Morgan 2" (l-not (l-or 'T 'B))  (l-and (l-not 'T) (l-not 'B))))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Fuzzy logic
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic fuzzy-logic
  (check-approx "fuzzy AND"    (l-and 0.7 0.3) 0.3 1e-9)
  (check-approx "fuzzy OR"     (l-or  0.7 0.3) 0.7 1e-9)
  (check-approx "fuzzy NOT"    (l-not 0.7)     0.3 1e-9)
  (check-approx "fuzzy NOT 0"  (l-not 0.0)     1.0 1e-9)
  (check-approx "fuzzy NOT 1"  (l-not 1.0)     0.0 1e-9)

  (check "fuzzy holds 0.9"  (l-holds? 0.9) #t)
  (check "fuzzy holds 0.5"  (l-holds? 0.5) #f)   ; strictly > 0.5
  (check "fuzzy holds 0.1"  (l-holds? 0.1) #f)

  ;; Identity: 0 is bottom, 1 is top
  (check-approx "fuzzy AND top" (l-and 1.0 0.7) 0.7 1e-9)
  (check-approx "fuzzy OR bot"  (l-or  0.0 0.7) 0.7 1e-9)

  ;; Adjustable threshold
  (let ((strict-fuzzy (make-fuzzy-logic 0.8)))
    (check "strict fuzzy holds 0.9" ((logic-entails? strict-fuzzy) 0.9) #t)
    (check "strict fuzzy holds 0.7" ((logic-entails? strict-fuzzy) 0.7) #f)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Intuitionistic logic
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic intuitionistic-logic
  ;; Basic connectives
  (check "intuit proved∧proved"   (l-and 'proved 'proved) 'proved)
  (check "intuit proved∧open"     (l-and 'proved 'open)   'open)
  (check "intuit proved∧refuted"  (l-and 'proved 'refuted) 'refuted)
  (check "intuit refuted∨proved"  (l-or  'refuted 'proved) 'proved)
  (check "intuit open∨refuted"    (l-or  'open 'refuted)   'open)

  ;; The critical subversion: ¬¬open ≠ proved
  (check "intuit ¬proved"  (l-not 'proved) 'refuted)
  (check "intuit ¬refuted" (l-not 'refuted) 'proved)
  (check "intuit ¬open"    (l-not 'open)   'open)    ; NOT(open) = open!
  (check "intuit ¬¬open"   (l-not (l-not 'open)) 'open)  ; double negation fails
  (check "intuit ¬¬proved" (l-not (l-not 'proved)) 'proved) ; but holds for proved

  ;; LEM fails for 'open
  (check "intuit LEM open fails"
    (l-or 'open (l-not 'open))   ; open ∨ ¬open = open ∨ open = open ≠ proved
    'open)

  ;; LEM holds for proved/refuted (classical fragment)
  (check "intuit LEM proved"
    (l-or 'proved (l-not 'proved))  ; proved ∨ refuted = proved
    'proved)

  ;; Heyting implication
  (check "intuit proved→refuted" (l-implies 'proved 'refuted) 'refuted)
  (check "intuit proved→proved"  (l-implies 'proved 'proved)  'proved)
  (check "intuit open→refuted"   (l-implies 'open 'refuted)   'refuted)
  (check "intuit open→proved"    (l-implies 'open 'proved)    'proved)
  (check "intuit refuted→proved" (l-implies 'refuted 'proved) 'proved) ; ex falso
  (check "intuit refuted→refuted"(l-implies 'refuted 'refuted)'proved) ; ex falso

  ;; Entailment
  (check "intuit holds proved"  (l-holds? 'proved)  #t)
  (check "intuit holds open"    (l-holds? 'open)    #f)
  (check "intuit holds refuted" (l-holds? 'refuted) #f))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Probabilistic logic
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic probabilistic-logic
  ;; Joint probability (independent)
  (check-approx "prob AND 0.5 0.5" (l-and 0.5 0.5) 0.25 1e-9)
  (check-approx "prob AND 1.0 0.7" (l-and 1.0 0.7) 0.7  1e-9)
  (check-approx "prob AND 0.0 0.7" (l-and 0.0 0.7) 0.0  1e-9)

  ;; Disjunction via inclusion-exclusion
  (check-approx "prob OR 0.5 0.5"  (l-or  0.5 0.5) 0.75 1e-9)
  (check-approx "prob OR 0.0 0.7"  (l-or  0.0 0.7) 0.7  1e-9)
  (check-approx "prob OR 1.0 0.3"  (l-or  1.0 0.3) 1.0  1e-9)

  ;; Complement
  (check-approx "prob NOT 0.7"     (l-not 0.7) 0.3 1e-9)
  (check-approx "prob NOT 0.0"     (l-not 0.0) 1.0 1e-9)

  ;; Entailment
  (check "prob holds 0.9"  (l-holds? 0.9) #t)
  (check "prob holds 0.5"  (l-holds? 0.5) #f)
  (check "prob holds 0.1"  (l-holds? 0.1) #f))

;;; Bayesian combination (Naive Bayes)
(let ((logic probabilistic-logic))
  ;; Prior 0.6, evidence bumps to 0.9 — posterior should be high
  (let ((post ((logic-combine logic) 0.6 0.9)))
    (check "prob Bayes combine" (> post 0.8) #t))
  ;; Prior 0.6, conflicting evidence 0.1 — posterior drops
  (let ((post ((logic-combine logic) 0.6 0.1)))
    (check "prob Bayes conflict" (< post 0.5) #t)))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Defeasible logic
;;; ══════════════════════════════════════════════════════════════════════════

(with-logic defeasible-logic
  ;; Basic connectives (linear ordering)
  (check "defeat AND strict-true weak-false"  (l-and 'strict-true 'weak-false) 'weak-false)
  (check "defeat AND strict-true strict-true" (l-and 'strict-true 'strict-true) 'strict-true)
  (check "defeat OR strict-false weak-true"   (l-or  'strict-false 'weak-true) 'weak-true)
  (check "defeat NOT strict-true"    (l-not 'strict-true)  'strict-false)
  (check "defeat NOT weak-true"      (l-not 'weak-true)    'weak-false)
  (check "defeat NOT unknown"        (l-not 'unknown)      'unknown)
  (check "defeat NOT weak-false"     (l-not 'weak-false)   'weak-true)
  (check "defeat NOT strict-false"   (l-not 'strict-false) 'strict-true)

  ;; Entailment
  (check "defeat holds strict-true"  (l-holds? 'strict-true)  #t)
  (check "defeat holds weak-true"    (l-holds? 'weak-true)    #t)
  (check "defeat holds unknown"      (l-holds? 'unknown)      #f)
  (check "defeat holds weak-false"   (l-holds? 'weak-false)   #f)
  (check "defeat holds strict-false" (l-holds? 'strict-false) #f))

;; Combine: defeat semantics
(let ((combine (logic-combine defeasible-logic)))
  ;; Strict beats weak
  (check "defeat combine: strict-true beats weak-false" (combine 'weak-false 'strict-true) 'strict-true)
  (check "defeat combine: strict-false beats weak-true" (combine 'weak-true 'strict-false) 'strict-false)
  ;; Same direction: idempotent
  (check "defeat combine: weak-true+weak-true"   (combine 'weak-true 'weak-true)   'weak-true)
  ;; Opposite same level: cancel to unknown
  (check "defeat combine: weak-true+weak-false"  (combine 'weak-true 'weak-false)  'unknown)
  ;; Unknown is identity
  (check "defeat combine: unknown+weak-true"     (combine 'unknown 'weak-true)     'weak-true)
  ;; Strict contradiction → unknown
  (check "defeat combine: strict-true+strict-false" (combine 'strict-true 'strict-false) 'unknown))

;;; ══════════════════════════════════════════════════════════════════════════
;;; Knowledge base API
;;; ══════════════════════════════════════════════════════════════════════════

;;; Classical KB: basic operations
(let ((kb (make-kb classical-logic)))
  (check "kb? classical"      (kb? kb) #t)
  (check "kb bottom (absent)" (kb-query kb 'P) #f)
  (kb-assert! kb 'P #t)
  (check "kb-query after assert" (kb-query kb 'P) #t)
  (check "kb-true? #t"           (kb-true? kb 'P) #t)
  (check "kb-false? #t"          (kb-false? kb 'P) #f)
  (kb-retract! kb 'P)
  (check "kb-query after retract" (kb-query kb 'P) #f)
  (check "kb consistent"          (kb-consistent? kb) #t))

;;; Belnap KB: THE KEY TEST — contradictions don't explode
(let ((kb (make-kb belnap-four)))
  (kb-assert! kb 'sky-is-blue 'T)    ; Source A: the sky is blue
  (kb-assert! kb 'sky-is-blue 'F)    ; Source B: the sky is NOT blue (contradiction!)
  (check "belnap kb: combined to B"   (kb-query kb 'sky-is-blue) 'B)
  (check "belnap kb: true? = #t"      (kb-true? kb 'sky-is-blue)  #t)  ; it IS true
  (check "belnap kb: false? = #t"     (kb-false? kb 'sky-is-blue) #t)  ; also false
  (check "belnap kb: inconsistent"    (kb-consistent? kb) #f)
  ;; Contradiction is CONTAINED — other facts are unaffected
  (kb-assert! kb 'grass-is-green 'T)
  (check "belnap kb: unaffected fact true"  (kb-true? kb 'grass-is-green) #t)
  (check "belnap kb: unaffected fact false" (kb-false? kb 'grass-is-green) #f)
  ;; Contradictions report exactly which propositions are contradicted
  (let ((contradictions (kb-contradictions kb)))
    (check "belnap kb: contradiction list length" (length contradictions) 1)
    (check "belnap kb: contradiction is sky"      (car contradictions) 'sky-is-blue)))

;;; Intuitionistic KB: can't prove things by double negation
(let ((kb (make-kb intuitionistic-logic)))
  (kb-assert! kb 'goldbach-conjecture 'open)   ; we don't know
  (check "intuit kb: open is not proved"  (kb-true? kb 'goldbach-conjecture) #f)
  (check "intuit kb: ¬¬open ≠ proved"
    (kb-true?
      (let ((kb2 (make-kb intuitionistic-logic)))
        (kb-assert! kb2 'x ((logic-complement intuitionistic-logic)
                             ((logic-complement intuitionistic-logic) 'open)))
        kb2)
      'x)
    #f))

;;; Intuitionistic KB: combine is an INFORMATION join (regression for old bug
;;; where %intuit-combine = %intuit-join would silently swallow 'refuted because
;;; open > refuted in the truth ordering, making combine('open,'refuted) = 'open).
(let ((kb (make-kb intuitionistic-logic)))
  ;; Start from absence (bottom = 'open), assert 'refuted — should stick
  (kb-assert! kb 'riemann-hypothesis 'refuted)  ; we've "disproved" it
  (check "intuit kb: refuted sticks" (kb-query kb 'riemann-hypothesis) 'refuted)
  (check "intuit kb: refuted is false" (kb-false? kb 'riemann-hypothesis) #t)
  (check "intuit kb: refuted is not true" (kb-true? kb 'riemann-hypothesis) #f))

(let ((kb (make-kb intuitionistic-logic)))
  ;; Proof dominates refutation (proofs are monotone)
  (kb-assert! kb 'P 'refuted)
  (kb-assert! kb 'P 'proved)
  (check "intuit kb: proof dominates refutation" (kb-query kb 'P) 'proved)
  (check "intuit kb: P is true after proof"      (kb-true? kb 'P) #t))

(let ((kb (make-kb intuitionistic-logic)))
  ;; combine('open,'open) = 'open (identity of info-join is 'open)
  (kb-assert! kb 'Q 'open)
  (check "intuit kb: open+open stays open" (kb-query kb 'Q) 'open)
  (check "intuit kb: open is not proved"   (kb-true? kb 'Q) #f))

;;; make-logic: unknown kwargs raise an error
(let ((caught #f))
  (guard (exn (#t (set! caught #t)))
    (make-logic 'test #f #t
      (lambda (a b) a) (lambda (a b) b) (lambda (a) a)
      'typo: values))
  (check "make-logic: unknown kwarg raises" caught #t))

;;; make-logic: missing value for kwarg raises an error
(let ((caught #f))
  (guard (exn (#t (set! caught #t)))
    (make-logic 'test #f #t
      (lambda (a b) a) (lambda (a b) b) (lambda (a) a)
      'implies:))    ; key without following value
  (check "make-logic: missing kwarg value raises" caught #t))

;;; Defeasible KB: classic penguin test
(let ((kb (make-kb defeasible-logic)))
  ;; General rule: birds fly (weak default)
  (kb-add-rule! kb 'tweety-flies
    (lambda (kb)
      (if (kb-true? kb 'tweety-is-bird)
          'weak-true
          'unknown)))
  ;; Specific rule: penguins don't fly (strict override)
  (kb-add-rule! kb 'tweety-flies
    (lambda (kb)
      (if (kb-true? kb 'tweety-is-penguin)
          'strict-false    ; penguins definitely don't fly
          'unknown)))
  ;; Assert Tweety is a bird and a penguin
  (kb-assert! kb 'tweety-is-bird    'strict-true)
  (kb-assert! kb 'tweety-is-penguin 'strict-true)
  (kb-close! kb)
  ;; The specific rule should defeat the general one
  (check "defeasible: penguin defeats bird-flies"
    (kb-false? kb 'tweety-flies) #t))

;;; Forward chaining with kb-close!
(let ((kb (make-kb belnap-four)))
  (kb-assert! kb 'A 'T)
  (kb-add-rule! kb 'B
    (lambda (kb) (if (kb-true? kb 'A) 'T 'N)))
  (kb-add-rule! kb 'C
    (lambda (kb) (if (kb-true? kb 'B) 'T 'N)))
  (kb-close! kb)
  (check "chaining A→B→C" (kb-query kb 'C) 'T))

;;; ══════════════════════════════════════════════════════════════════════════
;;; with-logic: switching logics at runtime
;;; ══════════════════════════════════════════════════════════════════════════

;; The same computation under two different logics
(define (contradiction-outcome logic a-true a-false)
  (with-logic logic
    (l-and a-true a-false)))

;; In classical: T∧F = F
(check "with-logic classical contradiction"
  (contradiction-outcome classical-logic #t #f) #f)

;; In Belnap: T∧F = F (but the KB would hold both, not just the AND of them)
(check "with-logic belnap T∧F"
  (contradiction-outcome belnap-four 'T 'F) 'F)

;; In fuzzy: 0.9 AND 0.1 = 0.1 (min)
(check-approx "with-logic fuzzy"
  (contradiction-outcome fuzzy-logic 0.9 0.1) 0.1 1e-9)

;;; ══════════════════════════════════════════════════════════════════════════
;;; Summary
;;; ══════════════════════════════════════════════════════════════════════════

(newline)
(display "Logic tests: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(when (> fail 0) (error "test failures" fail))
