# The Laws Are Not What They Seem
## A Rogue's Gallery of Truth

*From The Anarchist's Curry Cookbook*

---

You have been lied to about logic.

Not maliciously — the lie is one of those respectable, load-bearing lies that everyone
agrees to maintain because the alternative requires admitting that the foundations are
weirder than anyone told you in school. The lie is this:

> *A statement is either true or false. One or the other. Pick one.*

This is classical logic — the Aristotelian heritage, the Boolean bedrock, the substrate
on which every `if` statement you have ever written is built. And it is *fine* for most
things, the way a flat-earth model is fine for most things. You don't need general
relativity to navigate the city. But the moment you zoom out — the moment your data
comes from multiple conflicting sources, the moment you're reasoning about things that
might or might not be proven, the moment reality refuses to cooperate with your binary
predilections — classical logic detonates.

It detonates spectacularly. It has a principle called **ex contradictione quodlibet**
(*ECQ*), or more colloquially, **the principle of explosion**. It says: from a
contradiction, anything follows. If your database contains both "the sky is blue" and
"the sky is not blue", then under classical logic you can prove that the moon is made of
cheese, that 1 = 2, and that your mortgage is paid off. The whole system becomes useless.

This is not a bug that well-designed systems avoid. This is the *intended behavior* of
classical logic. It's a choice — and it turns out there are other choices.

Curry ships with a pluggable logic framework. You can swap the truth domain under any
computation. You can build knowledge bases that accumulate evidence from contradictory
sources without catching fire. You can reason constructively, probabilistically,
defeasibly, or across degrees of truth. And — this being Curry — logics are first-class
values you can pass around, compose, and manipulate at runtime.

Let's meet the inmates.

```scheme
(import (curry logic))
```

---

## The Engine: Logics as First-Class Values

Before the inmates, the cage. A **logic** in Curry is a record holding:

- A **truth domain** — the set of possible truth values (two booleans, four symbols,
  the interval [0,1], whatever you need)
- A **bottom** and **top** — the least and most "true" values
- **Meet** and **join** — AND and OR under this domain's algebra
- A **complement** — NOT
- An **implication** — IMPLIES (defaults to ¬a∨b, but you can override)
- An **entailment predicate** — "is this value true enough to assert?"
- A **combine** operation — how to merge two pieces of evidence about the same fact

```scheme
;; Make a custom logic
(define my-logic
  (make-logic 'my-logic
    bottom-value
    top-value
    meet-fn       ; (λ (a b) …)
    join-fn       ; (λ (a b) …)
    complement-fn ; (λ (a) …)
    ;; optional overrides:
    'implies:   my-implies-fn
    'entails?:  my-entails-fn
    'combine:   my-combine-fn))
```

You activate a logic with `with-logic`, which installs it as a dynamic parameter:

```scheme
(with-logic belnap-four
  (l-and 'T 'B))     ;; => 'B   (AND in Belnap's four-valued logic)
```

The connectives `l-and`, `l-or`, `l-not`, `l-implies`, `l-holds?`, `l-top`, `l-bot`
all dispatch through the current logic. Unicode aliases `⊓ ⊔ ∼ ⊃ ⊨ ⊤ ⊥` are also
available if you're feeling like yourself.

---

## Inmate 1: Belnap's FOUR (Paraconsistent)

Nuel Belnap (1977) looked at the explosion principle and said: *no*. He proposed a
four-valued logic where contradictions are **contained** rather than catastrophic.

The four truth values represent what a database might tell you:

| Symbol | Meaning              | What the database says |
|--------|----------------------|------------------------|
| `N`    | **Neither** (no info)| Nothing — silent        |
| `F`    | **False**            | P is false              |
| `T`    | **True**             | P is true               |
| `B`    | **Both**             | P is true AND false (!)  |

`B` is not a glitch. `B` is the database being honest about contradictory evidence. The
key insight: `B` is **designated** — it counts as "true" — because it contains true
information. But its negation (¬B = B) is *also* designated. So you can have a
proposition that is both true and false simultaneously, and the rest of your knowledge
base remains uncontaminated.

The connectives are defined via a **bilattice**: two independent orderings with
independent meet/join operations.

- **Truth ordering**: F ≤ {N, B} ≤ T *(how true is it?)*
- **Information ordering**: N ≤ {F, T} ≤ B *(how much do we know?)*

Logical connectives (∧, ∨, ¬) operate on the **truth ordering**.
Evidence combination operates on the **information ordering**.

```scheme
(with-logic belnap-four
  ;; Standard tautologies still hold
  (l-and 'T 'T)         ;; => 'T
  (l-or  'F 'T)         ;; => 'T
  (l-not 'T)            ;; => 'F

  ;; The weird bit: N and B are incomparable in the truth ordering
  (l-and 'N 'B)         ;; => 'F  (greatest lower bound of incomparable elements)
  (l-or  'N 'B)         ;; => 'T  (least upper bound of incomparable elements)

  ;; ¬B = B — contradiction is self-complementary
  (l-not 'B)            ;; => 'B

  ;; B is designated (true) AND its negation is designated (false)
  (l-holds? 'B)         ;; => #t
  (l-holds? (l-not 'B)) ;; => #t   ← both! and nothing explodes
)
```

### Use case 1: Databases that disagree

You're merging two customer databases. They disagree about whether Alice has opted in to
marketing emails. In classical logic this is a crisis. In Belnap it's just a fact you
can query:

```scheme
(define db (make-kb belnap-four))

;; Source A (from the CRM) says Alice has opted in
(kb-assert! db 'alice-opted-in 'T)

;; Source B (from the email platform) says she hasn't
(kb-assert! db 'alice-opted-in 'F)

;; What do we know?
(kb-query db 'alice-opted-in)    ;; => 'B   (both)
(kb-true? db 'alice-opted-in)    ;; => #t   (we have some true evidence)
(kb-false? db 'alice-opted-in)   ;; => #t   (also some false evidence)
(kb-consistent? db)              ;; => #f   (contradiction detected)
(kb-contradictions db)           ;; => '(alice-opted-in)
```

You detected the contradiction. You didn't crash. You know exactly *which* facts are
contradicted. You can query the sources, escalate to a human, log it, whatever — without
the entire system going sideways.

Meanwhile, other facts in the database are completely unaffected:

```scheme
(kb-assert! db 'alice-account-id "ACC-1234")
;; ... assert a thousand more facts ...

;; These all work fine despite the contradiction above
(kb-query db 'alice-account-id)   ;; => "ACC-1234"
;; The contradiction is contained. It is not a wormhole to nonsense.
```

### Use case 2: Legal reasoning

Article 17 of the Terms of Service says users *may* copy content for personal use.
Section 4.2 of the Copyright Policy says they *may not* copy content. Both are in force.
This is not unusual in law — contradictions in legal corpora are extremely common.

```scheme
(define legal-kb (make-kb belnap-four))

;; Article 17: copying for personal use is permitted
(kb-assert! legal-kb 'copying-permitted 'T)

;; Section 4.2: copying is not permitted
(kb-assert! legal-kb 'copying-permitted 'F)

;; The system recognizes the conflict and flags it for a lawyer,
;; rather than silently deciding one way or generating nonsense
(kb-query legal-kb 'copying-permitted)  ;; => 'B  (genuinely contested)
(kb-contradictions legal-kb)            ;; => '(copying-permitted)
```

### Use case 3: Distributed consensus (without coordinator)

Three nodes disagree on whether a transaction committed. Don't wait for a quorum — just
track the evidence:

```scheme
(define consensus (make-kb belnap-four))

(kb-assert! consensus 'tx-42-committed 'T)  ; node A says committed
(kb-assert! consensus 'tx-42-committed 'T)  ; node B says committed (adds to T)
(kb-assert! consensus 'tx-42-committed 'F)  ; node C says rolled back

;; T + T = T (same evidence, no new info)
;; T + F = B (contradictory evidence — needs reconciliation)
(kb-query consensus 'tx-42-committed)  ;; => 'B  (partition detected)
```

---

## Inmate 2: Fuzzy Logic (Łukasiewicz / Gödel)

Aristotle's bivalence says truth is binary. Lotfi Zadeh (1965) looked at the sentence
"this water is hot" and pointed out that this is obviously wrong. Truth comes in degrees.

Fuzzy logic uses truth values in **[0, 1]**. The connectives are:

- AND = min (you're only as sure as your weakest link)
- OR = max (you're as sure as your strongest link)
- NOT = 1 − x (complement)

```scheme
(with-logic fuzzy-logic
  (l-and 0.9 0.3)    ;; => 0.3   hot AND bright = min(hot, bright)
  (l-or  0.2 0.8)    ;; => 0.8
  (l-not 0.7)        ;; => 0.3
  (l-holds? 0.6)     ;; => #t   (> 0.5 threshold)
  (l-holds? 0.4)     ;; => #f
)
```

### Use case: Medical triage

A symptom scorer combining multiple signals, each with a degree of confidence:

```scheme
(define (triage patient-data)
  (let ((kb (make-kb fuzzy-logic)))
    ;; Assert symptom degrees from diagnostic tests
    (kb-assert! kb 'fever           (patient-data 'temp-score))
    (kb-assert! kb 'respiratory     (patient-data 'breath-score))
    (kb-assert! kb 'inflammation    (patient-data 'crp-score))

    ;; Forward-chaining rule: high-acuity if fever AND respiratory AND inflammation
    (kb-add-rule! kb 'high-acuity
      (lambda (kb)
        (with-logic fuzzy-logic
          (l-and (kb-query kb 'fever)
                 (l-and (kb-query kb 'respiratory)
                        (kb-query kb 'inflammation))))))

    (kb-close! kb)
    (kb-query kb 'high-acuity)))

;; Patient with high fever (0.9), moderate breathing difficulty (0.6),
;; but low inflammation (0.2):
(triage (lambda (k) (case k ((temp-score) 0.9) ((breath-score) 0.6) ((crp-score) 0.2))))
;; => 0.2   min(0.9, 0.6, 0.2) — the inflammation is the limiting factor
```

The score says 0.2 — probably not high acuity. But if the CRP comes back higher, the
score rises automatically because we're tracking a *degree*, not a binary flag.

### Use case: Threshold-adjustable confidence

```scheme
;; Default: entailment threshold = 0.5
;; For security-critical decisions, raise the bar
(define strict-logic (make-fuzzy-logic 0.9))

(with-logic strict-logic
  (l-holds? 0.85)   ;; => #f   not good enough for high-security mode
  (l-holds? 0.95))  ;; => #t
```

---

## Inmate 3: Intuitionistic Logic (Constructive / Heyting)

Classical logic has the **Law of Excluded Middle** (LEM): P ∨ ¬P is always true.
Either P is true or its negation is — there's no third option.

Intuitionistic logic says: *not so fast*. A proposition is true only if you can
**prove** it. "True-by-default-because-its-negation-isn't-refuted" doesn't count.

The truth domain has three values:

| Symbol     | Meaning                            |
|------------|-------------------------------------|
| `'proved`  | There exists a constructive proof   |
| `'open`    | Neither proved nor refuted (yet)    |
| `'refuted` | There exists a constructive refutation |

The critical properties:

```scheme
(with-logic intuitionistic-logic
  ;; ¬open = open  (the negation of an unproven statement is also unproven)
  (l-not 'open)           ;; => 'open

  ;; Therefore: ¬¬open ≠ proved  (double negation elimination fails!)
  (l-not (l-not 'open))   ;; => 'open   ← NOT 'proved

  ;; LEM fails for 'open
  (l-or 'open (l-not 'open))  ;; => 'open   ← NOT 'proved
                               ;;    you can't have P ∨ ¬P unless you have one

  ;; But double negation holds for things that ARE proved
  (l-not (l-not 'proved))      ;; => 'proved ✓

  ;; Heyting implication: proved → refuted = refuted
  (l-implies 'proved 'refuted)  ;; => 'refuted
  ;; open → refuted = refuted (if you assume open is true, refuted follows)
  (l-implies 'open 'refuted)    ;; => 'refuted
  ;; refuted → anything = proved (ex falso: from a false premise, anything)
  (l-implies 'refuted 'open)    ;; => 'proved
)
```

### Use case: Mathematical software that tracks proof status

You're building a theorem assistant. Propositions have proof status:

```scheme
(define math-kb (make-kb intuitionistic-logic))

;; Riemann Hypothesis: open problem
(kb-assert! math-kb 'riemann-hypothesis 'open)

;; Fermat's Last Theorem: proved (by Wiles, 1995)
(kb-assert! math-kb 'fermats-last-theorem 'proved)

;; A corollary requiring both
(kb-add-rule! math-kb 'corollary-of-both
  (lambda (kb)
    (with-logic intuitionistic-logic
      (l-and (kb-query kb 'riemann-hypothesis)
             (kb-query kb 'fermats-last-theorem)))))

(kb-close! math-kb)

;; The corollary's status is only as good as its weakest ingredient
(kb-query math-kb 'corollary-of-both)   ;; => 'open
```

### Use case: "Prove it" mode for access control

```scheme
;; Classical security: if we can't prove you're denied, you're permitted
;; Intuitionistic security: if we can't prove you're permitted, you're not

(define (intuit-access-control permissions)
  (with-logic intuitionistic-logic
    ;; LEM doesn't hold: absence of permission ≠ proven denial
    ;; You must have POSITIVE PROOF of permission, not just absence of denial
    (l-holds? (kb-query permissions 'user-may-access-secret))))

;; Under classical logic: (not (not permission)) = permission
;; Under intuitionistic: (not (not unknown-permission)) ≠ permission
;; This closes the "assume permitted unless explicitly denied" vulnerability
```

---

## Inmate 4: Probabilistic Logic (Bayesian)

Truth values are probabilities in [0, 1]. The connectives follow probability calculus,
**assuming independence**:

- P(A ∧ B) = P(A) · P(B)
- P(A ∨ B) = P(A) + P(B) − P(A)·P(B)
- P(¬A) = 1 − P(A)

Evidence combines via **Naive Bayes**: given a prior P(H) and a likelihood from new
evidence, the posterior is computed from log-odds:

```scheme
(with-logic probabilistic-logic
  (l-and 0.8 0.7)      ;; => 0.56   joint probability
  (l-or  0.3 0.6)      ;; => 0.72   inclusion-exclusion
  (l-not 0.7)          ;; => 0.3
  (l-holds? 0.6)       ;; => #t   P > 0.5
)
```

### Use case: Spam filter

```scheme
(define (score-message message)
  (let ((kb (make-kb probabilistic-logic)))
    ;; Prior: base rate of spam in inbox
    (kb-assert! kb 'is-spam 0.15)

    ;; Each feature updates the probability via Bayesian combination
    (when (contains-word? message "WINNER")
      (kb-assert! kb 'is-spam 0.95))  ; very spammy word
    (when (contains-word? message "invoice")
      (kb-assert! kb 'is-spam 0.4))   ; ambiguous — could be legit
    (when (from-known-sender? message)
      (kb-assert! kb 'is-spam 0.02))  ; strongly not-spam

    ;; kb-assert! with probabilistic-logic uses Bayesian combination
    ;; each new assertion updates the posterior, not just the latest value
    (kb-query kb 'is-spam)))

;; An invoice from a known sender: prior 0.15 → 0.4 (invoice) → Bayes(known-sender)
;; The known-sender evidence dominates — probability drops well below 0.5
```

### Use case: Sensor fusion

Three sensors independently detect motion. Each has its own false-positive rate:

```scheme
(define (motion-detected? readings)
  ;; readings: list of (sensor-id . probability)
  (let ((kb (make-kb probabilistic-logic)))
    (kb-assert! kb 'motion 0.1)   ; base rate: 10% chance of motion at any moment
    (for-each
      (lambda (r)
        (kb-assert! kb 'motion (cdr r)))
      readings)
    (kb-true? kb 'motion)))  ; P > 0.5

;; Three sensors all say 0.7 confident:
;; Bayesian combination: 0.1 → 0.7 → 0.7 → 0.7 ≈ 0.9+ after three updates
(motion-detected? '((sensor-1 . 0.7) (sensor-2 . 0.7) (sensor-3 . 0.7)))
;; => #t
```

---

## Inmate 5: Defeasible Logic (Unless Defeated)

Defeasible logic is for the real world, where general rules have exceptions, exceptions
have exceptions, and the most *specific* rule wins.

Truth values are ordered by **strength**:

```
strict-false  <  weak-false  <  unknown  <  weak-true  <  strict-true
```

- **Strict** facts are indefeasible — they cannot be overridden.
- **Weak** facts hold "by default" — unless a stronger fact defeats them.

The famous test case: *Tweety is a bird. Birds fly. Tweety is a penguin. Penguins
don't fly. Does Tweety fly?*

Classical logic explodes (you have ∀x. Bird(x)→Fly(x) and ¬Fly(tweety)).
Defeasible logic handles it gracefully: the specific rule (penguin) defeats the general
rule (bird):

```scheme
(define kb (make-kb defeasible-logic))

;; General rule: birds fly (weak default)
(kb-add-rule! kb 'tweety-flies
  (lambda (kb)
    (if (kb-true? kb 'tweety-is-bird)
        'weak-true    ; birds fly, by default
        'unknown)))

;; Specific exception: penguins don't fly (strict override)
(kb-add-rule! kb 'tweety-flies
  (lambda (kb)
    (if (kb-true? kb 'tweety-is-penguin)
        'strict-false ; penguins definitely don't fly
        'unknown)))

(kb-assert! kb 'tweety-is-bird    'strict-true)
(kb-assert! kb 'tweety-is-penguin 'strict-true)
(kb-close! kb)

;; strict-false defeats weak-true
(kb-true?  kb 'tweety-flies)  ;; => #f
(kb-false? kb 'tweety-flies)  ;; => #t
```

### Use case: Policy inheritance

A user is in group Admin. Group Admin has access to everything — but a specific policy
revokes their access to the financial module:

```scheme
(define policy (make-kb defeasible-logic))

;; Admin group gets broad access by default (weak)
(kb-add-rule! policy 'alice-financial-access
  (lambda (kb)
    (if (kb-true? kb 'alice-is-admin)
        'weak-true
        'unknown)))

;; But financial module has a specific exclusion list (strict)
(kb-add-rule! policy 'alice-financial-access
  (lambda (kb)
    (if (kb-true? kb 'alice-on-exclusion-list)
        'strict-false
        'unknown)))

(kb-assert! policy 'alice-is-admin          'strict-true)
(kb-assert! policy 'alice-on-exclusion-list 'strict-true)
(kb-close! policy)

(kb-true? policy 'alice-financial-access)  ;; => #f
;; strict-false (exclusion list) beats weak-true (admin default)
```

---

## The Runtime Logic Switch

Because logics are first-class values, you can select them at runtime — including based
on user configuration, context, or the nature of the data you're processing:

```scheme
(define (choose-logic data-provenance)
  (case data-provenance
    ((single-source-trusted) classical-logic)
    ((multi-source-conflicting) belnap-four)
    ((sensor-readings) probabilistic-logic)
    ((policy-hierarchy) defeasible-logic)
    ((mathematical-claims) intuitionistic-logic)
    (else fuzzy-logic)))

(define (analyze-claim kb-data claim provenance)
  (let* ((logic (choose-logic provenance))
         (kb    (make-kb logic)))
    (for-each (lambda (datum) (kb-assert! kb (car datum) (cdr datum))) kb-data)
    (kb-close! kb)
    (list
      'true:   (kb-true? kb claim)
      'false:  (kb-false? kb claim)
      'value:  (kb-query kb claim)
      'contradictions: (kb-contradictions kb))))
```

---

## Writing Your Own Logic

The framework is built to be extended. Here's a sketch of three-valued **Łukasiewicz
logic** (which differs from the fuzzy logic above in being discrete):

```scheme
;; Truth values: 'false, 'indeterminate, 'true
;; The original three-valued logic (Łukasiewicz, 1920)
;; Key: the middle value is "possible" — true in some possible worlds

(define lukasiewicz-three
  (make-logic 'lukasiewicz-three
    'false 'true
    (lambda (a b)                              ; AND = min
      (let ((ord '(false indeterminate true)))
        (if (<= (list-index ord a) (list-index ord b)) a b)))
    (lambda (a b)                              ; OR = max
      (let ((ord '(false indeterminate true)))
        (if (>= (list-index ord a) (list-index ord b)) a b)))
    (lambda (a)                                ; NOT: false↔true, indet↔indet
      (case a ((false) 'true) ((true) 'false) ((indeterminate) 'indeterminate)))
    'implies:
    (lambda (a b)                              ; Łukasiewicz implication
      ;; 0→b=1 (anything follows from false), 1→b=b, 1/2→1=1, 1/2→1/2=1, 1/2→0=1/2
      (case a
        ((false) 'true)
        ((true) b)
        ((indeterminate)
         (case b
           ((true) 'true)
           ((indeterminate) 'true)
           ((false) 'indeterminate)))))
    'entails?: (lambda (v) (eq? v 'true))))
```

Or go entirely abstract — if your truth domain is a Heyting algebra, a bilattice, a
probability space, or anything else with well-defined meet/join/complement operations,
you can plug it in. The knowledge base and inference engine don't care about the domain.
They only call the procedures you provide.

---

## Why Scáth Added This

Because "true or false" is a choice that classical logic made in the 4th century BCE,
and it's been coasting on institutional inertia ever since.

Because every database with a merge function, every AI system with conflicting training
data, every legal system with contradictory statutes, every engineering system with
noisy sensors, every human institution with conflicting records is *already* operating in
non-classical truth domains. They're just lying about it — mapping `B` to `T` or `F`
arbitrarily, throwing exceptions, or refusing to acknowledge the contradiction.

Because a language that already has surreal numbers, Clifford algebras, Babylonian error
messages, and quantum superpositions as first-class values really has no business
pretending truth is binary.

Because Belnap was right in 1977 and barely anyone noticed.

Because you should be able to write:

```scheme
(with-logic belnap-four
  (kb-assert! kb 'the-map-matches-the-territory 'F)
  (kb-assert! kb 'the-map-matches-the-territory 'T)
  (display "contradiction detected: ")
  (display (kb-contradictions kb))
  (display " — but the rest of the knowledge base still works fine."))
```

And have it *mean something*, rather than watching the system catch fire.

---

## Quick Reference

```scheme
(import (curry logic))

;; Built-in logics
classical-logic        ;; {#f, #t}; standard Boolean
belnap-four            ;; {N, F, T, B}; paraconsistent
fuzzy-logic            ;; [0.0, 1.0]; Gödel min/max
(make-fuzzy-logic 0.8) ;; fuzzy with custom entailment threshold
fuzzy-logic/product    ;; [0.0, 1.0]; Łukasiewicz product t-norm
intuitionistic-logic   ;; {refuted, open, proved}; constructive
probabilistic-logic    ;; [0.0, 1.0]; Bayesian joint/disjunction
defeasible-logic       ;; {strict-false…strict-true}; priority defeat

;; Dynamic dispatch (uses current-logic)
(with-logic L body …)  ;; install L as current logic in body
(l-and a b)            ;; AND;  alias ⊓
(l-or  a b)            ;; OR;   alias ⊔
(l-not a)              ;; NOT;  alias ∼
(l-implies a b)        ;; →;    alias ⊃
(l-holds? v)           ;; is v designated?  alias ⊨
(l-top)                ;; most-true value;  alias ⊤
(l-bot)                ;; least-true value; alias ⊥

;; Knowledge base
(make-kb logic)                    ;; create a KB
(kb-assert! kb prop val)           ;; add evidence (combines with existing)
(kb-retract! kb prop)              ;; remove a proposition
(kb-query kb prop)                 ;; current truth value (bottom if absent)
(kb-true? kb prop)                 ;; is prop designated?
(kb-false? kb prop)                ;; is ¬prop designated? (can both be #t!)
(kb-consistent? kb)                ;; no contradictions?
(kb-contradictions kb)             ;; list of contradicted propositions
(kb-add-rule! kb prop (λ (kb) v)) ;; add a forward-chaining rule
(kb-close! kb)                     ;; run rules to fixed point
(kb-propositions kb)               ;; list non-bottom propositions

;; Custom logic
(make-logic 'name bottom top meet join complement
  'implies:   fn   ;; optional
  'entails?:  fn   ;; optional
  'combine:   fn   ;; optional
  'display:   fn)  ;; optional
```
