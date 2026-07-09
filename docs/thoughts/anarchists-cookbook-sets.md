# The Boundaries Are Not What They Seem
## A Rogue's Guide to Set Theory

*From The Anarchist's Curry Cookbook*

---

You have been lied to about sets.

Not about the algebra — the union is still the union, the intersection is still the
intersection. The lie is subtler than that. It is a lie about *membership*.

Every data structure course tells you the same story: an element is either in a set or
it isn't. True or false. Yes or no. This is called the **characteristic function** of a
set — a function from the universe to `{0, 1}` — and it is presented as the only
possible story. But it's not. The characteristic function is a choice. And you have
already seen, in the previous chapter, that `{0, 1}` is not the only choice for a truth
domain.

Once you see that a set is just a membership function, and a membership function returns a
truth value, and truth values live in a logic — you can see that **classical sets are just
sets whose membership function runs in classical logic**. And all the other logics from
the previous chapter immediately give you different set theories for free.

There's more. Even staying within integer membership values, you can have elements that
belong **more than once** — and the algebra of "more than once" is richer than you'd
expect. The number of times something appears in a bag tells you things that a flat set
cannot.

Curry ships with three layers of set theory:

1. **Core hash-sets** — mutable, hashed, three comparator modes. The standard toolkit.
2. **Multisets** — elements with integer multiplicities. Bags, histograms, word counts.
3. **Logical sets** — membership returns a truth value in any (curry logic) logic.
   Classical logic → plain set. Fuzzy logic → fuzzy set. Belnap → paraconsistent set.
   Probabilistic logic → probabilistic set. And so on.

```scheme
(import (curry sets))   ;; multisets + logical sets
(import (curry logic))  ;; for the logic values
```

---

## A Brief History of the Catastrophe

Before we subvert set theory, let us appreciate that set theory has already subverted
*itself*.

Georg Cantor invented naïve set theory in the 1870s. A set is any collection of objects
satisfying some property. Any property at all. Beautiful. Simple. Disastrously wrong.

Bertrand Russell, in 1901, asked: what about the set of all sets that do not contain
themselves? Call it R. Does R contain itself?

- If yes: R contains itself, contradicting the rule that R only contains sets that *don't*
  contain themselves.
- If no: R doesn't contain itself, but R is *exactly* the collection of sets that don't
  contain themselves, so it should be in R.

Either way: explosion. Naïve set theory turned out to be *inconsistent*. The entire
edifice collapsed and mathematicians spent the next thirty years building a careful,
restricted replacement (ZFC — Zermelo-Fraenkel with the Axiom of Choice) that blocked the
paradox by forbidding certain self-referential constructions.

The moral: even the "standard" set theory is a workaround. A political compromise between
expressiveness and consistency. There are other workarounds, with different trade-offs,
and some of them are more useful for real programs than ZFC.

---

## Layer 1: The Core Hash-Set

Curry's core hash-set is a mutable open-addressing hash table. Three comparator modes:

```scheme
(make-set)              ;; default: equal? comparator
(make-set 0)            ;; eq?    comparator (fastest, pointer identity)
(make-set 1)            ;; eqv?   comparator (numeric equality + eq?)
(make-set 2)            ;; equal? comparator (structural; default)

(list->set '(1 2 3 2 1))       ;; => {1, 2, 3}  (deduplication)

(set-add!    s elem)           ;; mutable add
(set-delete! s elem)           ;; mutable remove
(set-member? s elem)           ;; predicate
(set-size    s)                ;; element count
(set=?       s1 s2)            ;; equality
(set-empty?  s)                ;; is it empty?
(set-copy    s)                ;; independent copy

(set-union        a b)         ;; A ∪ B
(set-intersection a b)         ;; A ∩ B
(set-difference   a b)         ;; A \ B
(set-symmetric-difference a b) ;; (A\B) ∪ (B\A)
(set-subset?      a b)         ;; A ⊆ B?

;; Functional (non-destructive)
(set-adjoin  s x y z)          ;; copy + add multiple; returns new set
(set-delete  s x)              ;; copy - one element; returns new set

;; Higher-order
(set-for-each  proc s)          ;; (proc elem) → void
(set-map       proc s)          ;; (proc elem) → new-elem; collect in new set
(set-filter    pred s)          ;; (pred elem) → bool; returns new set
(set-filter!   pred s)          ;; destructive filter
(set-fold      proc init s)     ;; (proc acc elem); left fold
(set-any?      pred s)          ;; short-circuit: any elem satisfies pred?
(set-every?    pred s)          ;; short-circuit: all elems satisfy pred?
(set-count     pred s)          ;; count elems satisfying pred
(set-find      pred s)          ;; first matching elem, or #f
(set-find      pred s default)  ;; first matching elem, or default
```

You know what these do. Let's go somewhere interesting.

---

## Layer 2: Multisets — When Counting Matters

A **multiset** (also called a *bag*) is a set where elements can appear more than once,
and the multiplicity matters. The multiset `{a, a, b}` is not the same as `{a, b}`.

This is more common than you'd think. Every word-frequency counter, histogram, shopping
cart, inventory system, and biological sequence alignment is already implicitly working
with a multiset. Most languages make you fake it with a hash-table-of-counts. Curry
makes it first class.

```scheme
(import (curry sets))

(define words (list->multiset '(the cat sat on the mat the cat)))

(multiset-count  words 'the)      ;; => 3
(multiset-count  words 'cat)      ;; => 2
(multiset-count  words 'dog)      ;; => 0
(multiset-member? words 'sat)     ;; => #t
(multiset-size   words)           ;; => 5  (distinct elements)
(multiset-total  words)           ;; => 8  (total count across all)
```

### Two Different Unions

Multisets have **two natural union operations** with different meanings. This is where
things get interesting.

**Set-theoretic union** (`multiset-union`, using max): "the largest multiplicity from
either bag." If A has three apples and B has five apples, the union has five. This is the
lattice-theoretic union — `max` is the join of the count lattice.

**Bag sum** (`multiset-sum`, using +): "all copies from both bags." If A has three apples
and B has five apples, the bag sum has eight. This is concatenation of the underlying
lists — the monoidal operation.

```scheme
(define A (list->multiset '(apple apple apple orange)))
(define B (list->multiset '(apple apple apple apple apple banana)))

;; Union: max per element
(multiset-count (multiset-union A B) 'apple)    ;; => 5  (max 3, 5)
(multiset-count (multiset-union A B) 'orange)   ;; => 1  (max 1, 0)
(multiset-count (multiset-union A B) 'banana)   ;; => 1  (max 0, 1)

;; Bag sum: + per element (how many total across both bags?)
(multiset-count (multiset-sum A B) 'apple)      ;; => 8  (3 + 5)
```

Union + intersection form a **distributive lattice** on multisets.
Sum + intersection form the **free commutative monoid** — the algebra of bags.
These are different structures, and which one you want depends on your semantics.

### The Full Algebra

```scheme
;; Algebra
(multiset-union        a b)    ;; max per element
(multiset-intersection a b)    ;; min per element
(multiset-sum          a b)    ;; + per element (bag concatenation)
(multiset-difference   a b)    ;; max(0, count_a - count_b) per element
(multiset-scale        ms n)   ;; multiply every count by n

(multiset-subset? a b)         ;; every count in a ≤ corresponding count in b

;; Higher-order — proc receives (element count), not just element
(multiset-for-each proc ms)    ;; (proc elem count) → void
(multiset-map      proc ms)    ;; (proc elem count) → (new-elem . new-count)
                                ;;   counts sum when two elements collapse to one
(multiset-filter   pred ms)    ;; (pred elem count) → bool
(multiset-fold     proc init ms) ;; (proc acc elem count) → acc
(multiset-any?     pred ms)    ;; short-circuit; (pred elem count) → bool
(multiset-every?   pred ms)    ;; short-circuit; (pred elem count) → bool
```

### Use case 1: Word frequency and Zipf's Law

Zipf's law says the frequency of a word is proportional to the inverse of its rank.
Count it and see:

```scheme
(define (word-frequencies text)
  (list->multiset (string-split text)))  ; (string-split splits on whitespace)

(define corpus
  (word-frequencies
    "to be or not to be that is the question whether tis nobler in the mind to suffer"))

;; Most common words
(define freq-list
  (list-sort (lambda (a b) (> (cdr a) (cdr b)))
             (multiset->alist corpus)))

freq-list
;; => ((to . 3) (be . 2) (the . 2) (or . 1) (not . 1) ...)

;; Rank 1 appears 3 times, rank 3 appears ~1 time — Zipfian
```

### Use case 2: Inventory difference

You received a shipment. You expected something else. The shortages are `multiset-difference
(expected, received)`, the overages are `multiset-difference(received, expected)`:

```scheme
(define expected  (list->multiset '(widget widget widget bolt bolt screw screw screw)))
(define received  (list->multiset '(widget widget bolt bolt bolt screw)))

(define shortages (multiset-difference expected received))
(define overages  (multiset-difference received expected))

(multiset->alist shortages)   ;; => ((widget . 1) (screw . 2))
(multiset->alist overages)    ;; => ((bolt . 1))
```

### Use case 3: Histogram normalization

```scheme
(define (normalize-histogram ms)
  ;; Convert count histogram to probability distribution
  (let ((total (multiset-total ms)))
    (multiset-map (lambda (elem count) (cons elem (/ count total))) ms)))

(define die-rolls (list->multiset '(1 3 2 4 6 3 2 1 5 3 2 3)))
(define probabilities (normalize-histogram die-rolls))

(multiset-count probabilities 3)   ;; => 4/12 = 1/3  (exact rational)
(multiset-count probabilities 1)   ;; => 2/12 = 1/6
```

Notice: `multiset-map` here maps `(elem . count)` to `(elem . probability)`, and if
two elements ever produce the same key, their values are summed — exactly the right
behavior for distribution normalization.

### Use case 4: Multiset as algebra element

The multiset `{a²b}` in algebra is exactly a multiset: `a` appears twice, `b` appears
once. Polynomial multiplication is bag sum under the exponent multiset:

```scheme
;; x²y · xy³ = x³y⁴
(define x2y  (multiset 'x 'x 'y))          ; x²y
(define xy3  (multiset 'x 'y 'y 'y))       ; xy³

(multiset->alist (multiset-sum x2y xy3))
;; => ((x . 3) (y . 4))    i.e. x³y⁴
```

---

## Layer 3: Logical Sets — Membership Has a Truth Value

Now we arrive at the main event.

The observation: every set is defined by a membership function. In classical set theory,
that function returns `{0, 1}` — in or out. But we've already built a framework for
reasoning about truth values that go beyond `{0, 1}`. What happens when we let the
membership function return values in any `(curry logic)` logic?

We get a different set theory for every logic:

| Logic              | Membership domain    | What you get              |
|--------------------|----------------------|---------------------------|
| `classical-logic`  | `{#f, #t}`           | Plain ZFC set             |
| `fuzzy-logic`      | `[0.0, 1.0]`         | Fuzzy set (Zadeh, 1965)   |
| `belnap-four`      | `{N, F, T, B}`       | Paraconsistent set        |
| `probabilistic-logic` | `[0.0, 1.0]`      | Probabilistic set         |
| `intuitionistic-logic` | `{refuted, open, proved}` | Constructive set |
| `defeasible-logic` | `{strict-false…}`    | Priority-ordered set      |

The algebra is **pointwise through the logic's meet and join**:

- **Membership** of x in A: `(logical-set-member A x)` → truth-value
- **Union** A ∪ B: for each known x, `(l-join (member A x) (member B x))`
- **Intersection** A ∩ B: for each known x, `(l-meet (member A x) (member B x))`
- **Complement** ¬A: for each explicit x, `(l-not (member A x))`
- **Difference** A \ B: for each x in A, `(l-meet a-tv (l-not b-tv))`

```scheme
;; A logical set is created with a logic and populated via logical-set-assert!
;; which accumulates evidence using logic-combine — same as kb-assert!
(define s (make-logical-set belnap-four))

;; Convenience constructors:
(logical-set belnap-four 'paris 'london 'berlin)     ;; all elements get top ('T)
(alist->logical-set fuzzy-logic '((hot . 1.0) (warm . 0.7) (cool . 0.2)))
(fuzzy-set 'hot 1.0  'warm 0.7  'cool 0.2)          ;; same, more readable
(belnap-set 'paris 'T  'atlantis 'N  'camelot 'B)   ;; with explicit Belnap values
```

### Fuzzy Sets — Shades of Membership

Lotfi Zadeh introduced fuzzy sets in 1965, two years before "fuzzy logic" had that name.
A **fuzzy set** is a set where membership comes in degrees. The set "tall people" doesn't
have a sharp boundary — someone 1.79m is a little bit in, someone 2.10m is very much in.

```scheme
(define tall
  (fuzzy-set
    'alice  0.3   ; she's 5'6" — kind of tall
    'bob    0.85  ; he's 6'2" — quite tall
    'carol  1.0   ; she's 6'6" — definitely tall
    'dave   0.05)) ; he's 5'2" — barely registering

;; Membership returns the degree, not a boolean
(logical-set-member  tall 'bob)    ;; => 0.85
(logical-set-member  tall 'dave)   ;; => 0.05

;; contains? uses the entailment predicate (degree > 0.5 by default)
(logical-set-contains? tall 'bob)  ;; => #t
(logical-set-contains? tall 'dave) ;; => #f

;; Fuzzy union: max of degrees (as tall as the tallest claim)
;; Fuzzy intersection: min of degrees (only as tall as the shorter claim)
(define also-tall
  (fuzzy-set 'alice 0.8  'bob 0.6  'eve 0.9))

(logical-set-member (logical-set-union        tall also-tall) 'alice)  ;; => 0.8
(logical-set-member (logical-set-intersection tall also-tall) 'alice)  ;; => 0.3
(logical-set-member (logical-set-intersection tall also-tall) 'bob)    ;; => 0.6
```

#### Alpha-cuts: Back to Classical Land

An **alpha-cut** of a fuzzy set at threshold α is the classical set of elements with
membership degree > α. This lets you bridge between fuzzy membership and hard decisions:

```scheme
(define warm-temperatures
  (fuzzy-set 'freezing 0.0  'cold 0.1  'cool 0.3  'tepid 0.6
             'warm     0.85 'hot  1.0  'scorching  0.9))

;; Alpha-cut at 0.5: elements we'd call "warm" in conversation
(define warmish (fuzzy-alpha-cut warm-temperatures 0.5))
(set->list warmish)  ;; => '(tepid warm hot scorching)  (order varies)

;; Alpha-cut at 0.8: elements we'd definitely call warm
(define definitely-warm (fuzzy-alpha-cut warm-temperatures 0.8))
(set->list definitely-warm)  ;; => '(warm hot scorching)
```

### Paraconsistent Sets — Belonging and Not Belonging

A **Belnap set** is a set where each element's membership carries a Belnap truth value:
`T` (known member), `F` (known non-member), `N` (no information), or `B` (contradictory
information — sources disagree).

The key property: `B`-membership means the element *both* belongs and doesn't belong.
This is not a bug. It means you have contradictory information and you haven't lost it.

```scheme
(define european-capitals (make-logical-set belnap-four))

;; Source A (reliable gazetteer): Paris is a European capital
(logical-set-assert! european-capitals 'paris 'T)

;; Source B (garbled import): Paris is not in Europe (wrong!)
(logical-set-assert! european-capitals 'paris 'F)

;; Source C: Rome is a European capital
(logical-set-assert! european-capitals 'rome 'T)

;; What do we have?
(logical-set-member european-capitals 'paris) ;; => 'B  (both — contradiction!)
(logical-set-member european-capitals 'rome)  ;; => 'T  (unambiguous)
(logical-set-member european-capitals 'tokyo) ;; => 'N  (no information)

;; Classical membership test (uses entails?; T and B are designated)
(logical-set-contains? european-capitals 'paris)  ;; => #t  (we have some T evidence)
(logical-set-contains? european-capitals 'tokyo)  ;; => #f

;; Find the problem elements
(belnap-set-contradictions european-capitals)  ;; => '(paris)
(belnap-set-unknowns european-capitals)        ;; => (explicitly-tracked N elements)
```

The payoff: `rome` is unambiguous and queryable. The contradiction in `paris` is
**contained**. No explosion. No corruption. You know exactly what you know and what's
contested.

#### Set algebra over Belnap values is worth examining carefully:

```scheme
(define A (belnap-set 'x 'T  'y 'F))
(define B (belnap-set 'y 'T  'z 'B))

;; Union: join in the truth ordering
;; join(T, N) = T   (T dominates no-information)
;; join(F, T) = T   (both sources combined give T)
;; join(N, B) = T   (B is above N in the truth ordering)
(logical-set-member (logical-set-union A B) 'x)  ;; => 'T  (T from A, N from B → T)
(logical-set-member (logical-set-union A B) 'y)  ;; => 'T  (F from A, T from B → T)

;; Intersection: meet in the truth ordering
;; meet(T, N) = N   (no info from B drags down T from A)
;; meet(F, T) = F   (F from A, T from B → F)
(logical-set-member (logical-set-intersection A B) 'x)  ;; => 'N
(logical-set-member (logical-set-intersection A B) 'y)  ;; => 'F
```

Wait — `meet(T, N) = N`? Isn't that wrong? Shouldn't the intersection include something
that's definitely in A? No, and this is the surprise. In the truth ordering, `N` means
"no information" — not "don't know if it's in or out," but specifically occupying a
position *above* `F` and *below* `T`. The meet of T and N is N because N is the greatest
lower bound. B's silence on `x` is not a vote in its favor — it's an absence of claim.
The bilattice structure is doing exactly what Belnap designed it to do: being honest about
what we know.

### Probabilistic Sets — When Membership is a Bet

A **probabilistic set** maps elements to probabilities. Think of it as "how confident are
we that this element belongs?" Multiple sources update the probability via Bayesian
combination.

```scheme
(define possibly-spam
  (alist->logical-set probabilistic-logic
    '((msg-1 . 0.15)   ; prior: normal email
      (msg-2 . 0.9)    ; highly suspicious
      (msg-3 . 0.45))) ; ambiguous

;; Assert new evidence for msg-1: it says "WINNER"
(logical-set-assert! possibly-spam 'msg-1 0.95)

;; Bayesian combination: 0.15 + 0.95 evidence → posterior ≈ 0.77
(logical-set-member possibly-spam 'msg-1)    ;; => ~0.77

;; Union of two classifier results: max probability per message
;; Intersection: min probability per message (conservative AND)
```

### The Unifying View

```scheme
;; All four create sets with the same interface
(define classic-set   (logical-set classical-logic    'a 'b 'c))
(define fuzzy-set-ex  (alist->logical-set fuzzy-logic '((a . 0.9) (b . 0.3) (c . 0.7))))
(define belnap-set-ex (belnap-set 'a 'T 'b 'B 'c 'F))
(define prob-set-ex   (alist->logical-set probabilistic-logic '((a . 0.8) (b . 0.4))))

;; Same operations on all of them
(for-each (lambda (s)
            (display (logical-set-member s 'a))
            (newline))
  (list classic-set fuzzy-set-ex belnap-set-ex prob-set-ex))
;; prints: #t  0.9  T  0.8

;; Same algebra on all of them
;; (logical-set-union s1 s2) works for any logic, using that logic's join
;; (logical-set-intersection ...) uses that logic's meet
;; etc.
```

---

## Building Your Own Set Theory

The framework is open. A set theory is just a logic. Plug in a logic, get a set theory.
Here are some directions that aren't in the standard library:

### Rough Sets (Pawlak, 1982)

A **rough set** has two components: a lower approximation and an upper approximation,
defined with respect to an equivalence relation (an indiscernibility relation). An element
is:

- **definitely in** the set (lower approx): in every equivalence class fully contained in
  the set
- **possibly in** the set (upper approx): in at least one equivalence class that
  intersects the set
- **in the boundary** (upper minus lower): indiscernible from both members and non-members

You can build rough set membership as a pair:

```scheme
;; A rough truth value is (definite . possible)
;; Both are booleans: (definite . possible) where definite implies possible
;; (definite . possible) = (#f . #f) = definitely OUT
;; (#f . #t)             = in the boundary (might be in or out)
;; (#t . #t)             = definitely IN

(define rough-logic
  (make-logic 'rough-logic
    '(#f . #f)   ; bottom: definitely out
    '(#t . #t)   ; top: definitely in
    (lambda (a b) (cons (and (car a) (car b)) (and (cdr a) (cdr b))))  ; AND
    (lambda (a b) (cons (or  (car a) (car b)) (or  (cdr a) (cdr b))))  ; OR
    (lambda (a)   (cons (not (cdr a)) (not (car a))))  ; NOT: swap and negate
    'entails?:  (lambda (v) (car v))     ; definitely in
    'combine:   (lambda (a b)            ; merge approximations
                  (cons (or (car a) (car b))   ; accumulate definite
                        (or (cdr a) (cdr b))))))  ; accumulate possible

;; Now a rough set is a logical-set under rough-logic
(define rough-mammal (make-logical-set rough-logic))

;; A bat: definitely a mammal but also classified by some as "flying bird"
;; Upper approximation: yes (bat matches mammal features)
;; Lower approximation: no (bat is in an equivalence class that includes birds)
(logical-set-assert! rough-mammal 'bat '(#f . #t))   ; boundary element

;; A dog: unambiguously a mammal
(logical-set-assert! rough-mammal 'dog '(#t . #t))   ; definitely in

;; A goldfish: unambiguously not a mammal
(logical-set-assert! rough-mammal 'goldfish '(#f . #f))  ; definitely out

;; Query
(logical-set-member rough-mammal 'bat)      ;; => (#f . #t)  — boundary
(logical-set-contains? rough-mammal 'bat)   ;; => #f  — NOT definitely a mammal
(logical-set-contains? rough-mammal 'dog)   ;; => #t
```

### Interval Sets (epistemic uncertainty)

When you know a quantity lies in a range but not exactly where:

```scheme
;; An interval truth value is (lo . hi) with 0 ≤ lo ≤ hi ≤ 1
;; lo = lower bound on membership, hi = upper bound on membership
;; (0 . 1) = complete ignorance
;; (0.7 . 0.9) = "somewhere between 70% and 90% a member"

(define interval-logic
  (make-logic 'interval-logic
    '(0.0 . 0.0)   ; bottom: definitely not a member
    '(1.0 . 1.0)   ; top: definitely a member
    (lambda (a b) (cons (min (car a) (car b)) (min (cdr a) (cdr b))))  ; AND
    (lambda (a b) (cons (max (car a) (car b)) (max (cdr a) (cdr b))))  ; OR
    (lambda (a)   (cons (- 1.0 (cdr a)) (- 1.0 (car a))))             ; NOT
    'entails?:  (lambda (v) (> (car v) 0.5))  ; lo-bound must exceed threshold
    'combine:   (lambda (a b)                  ; tighten the interval
                  (cons (max (car a) (car b))
                        (min (cdr a) (cdr b))))))  ; intersection of ranges
```

### Signed Multisets (integer membership, possibly negative)

Some algebraic structures need elements to appear with negative multiplicity — formal
differences in a free abelian group:

```scheme
;; The "integer lattice" logic
;; Truth values are integers; bottom = 0
;; meet = min, join = max, combine = +
;; entails = positive
(define integer-multiset-logic
  (make-logic 'integer-multiset-logic
    0 +inf.0
    min max
    (lambda (a) (- a))    ; NOT = negation
    'entails?: positive?
    'combine:  +))        ; evidence ADDS (not joins)

;; Now logical-set-assert! with a negative count removes copies:
;; (logical-set-assert! signed-ms 'x -2)  removes 2 copies of x
```

---

## Causing Educational Chaos

### Chaos 1: The Set That Won't Decide

Build a set where membership is genuinely undecidable within the system's reasoning horizon — the set of propositions the Collatz conjecture requires you to evaluate:

```scheme
(define (collatz-sequence n)
  ;; Returns the Collatz sequence starting from n
  (let loop ((n n) (acc '()))
    (if (= n 1) (cons 1 acc)
        (loop (if (even? n) (/ n 2) (+ (* 3 n) 1))
              (cons n acc)))))

;; The "reaches-one" set under intuitionistic logic:
;; An element is 'proved if we've computed it reaches 1.
;; An element is 'open if we don't know yet.
;; (Nothing is 'refuted — we've never found a counterexample.)

(define reaches-one (make-logical-set intuitionistic-logic))

(define (verify-collatz! n)
  (guard (exn (#t 'open))        ; if it times out: open
    (let ((seq (collatz-sequence n)))
      (if (= (car (list-tail seq (- (length seq) 1))) 1)
          (logical-set-assert! reaches-one n 'proved)
          (logical-set-assert! reaches-one n 'refuted)))))

(for-each verify-collatz! '(2 3 4 5 6 7 8 12 27))

;; All verified numbers are 'proved
;; The conjecture itself — that ALL numbers reach 1 — remains 'open
;; because we haven't proved it, and the set tells you so
(logical-set-member reaches-one 27)   ;; => 'proved  (sequence terminates)
(kb-query (make-kb intuitionistic-logic) 'collatz-conjecture)  ;; => 'open
```

### Chaos 2: The Fuzzy Government

Access control where privilege is a matter of degree, not binary permission. Who's "sufficiently authorized"?

```scheme
(define authorities (make-logical-set (make-fuzzy-logic 0.75)))
;; Threshold raised to 0.75 — you need 75% confidence to be granted access

(logical-set-assert! authorities 'alice 0.95)   ; head admin
(logical-set-assert! authorities 'bob   0.8)    ; senior admin
(logical-set-assert! authorities 'carol 0.6)    ; junior admin — below threshold
(logical-set-assert! authorities 'dave  0.4)    ; intern

(define (may-access? user resource)
  (with-logic (make-fuzzy-logic 0.75)
    (l-holds? (logical-set-member authorities user))))

(may-access? 'alice 'secret-db)  ;; => #t  (0.95 > 0.75)
(may-access? 'bob   'secret-db)  ;; => #t  (0.8 > 0.75)
(may-access? 'carol 'secret-db)  ;; => #f  (0.6 < 0.75)

;; The interesting case: raise the threshold on-the-fly for high-value assets
(define (may-access-critical? user)
  (with-logic (make-fuzzy-logic 0.9)
    (l-holds? (logical-set-member authorities user))))

(may-access-critical? 'alice)  ;; => #t  (0.95 > 0.9)
(may-access-critical? 'bob)    ;; => #f  (0.8 < 0.9)
```

No code changed. The threshold is a parameter. Bob is authorized for normal access but
not critical access, and the same data structure expresses both without duplication.

### Chaos 3: Paraconsistent Categories

Build a document classifier that doesn't crash on ambiguous documents, and surfaces the
ambiguities for human review:

```scheme
(define (classify-document text classifiers)
  ;; classifiers: list of (category . (text → Belnap-value))
  (let ((membership (make-logical-set belnap-four)))
    (for-each
      (lambda (classifier)
        (let* ((category (car classifier))
               (vote     ((cdr classifier) text)))
          (logical-set-assert! membership category vote)))
      classifiers)
    membership))

(define spam-classifiers
  (list
    (cons 'spam   (lambda (t) (if (string-contains t "WINNER") 'T 'N)))
    (cons 'spam   (lambda (t) (if (string-contains t "invoice") 'F 'N)))
    (cons 'urgent (lambda (t) (if (string-contains t "URGENT") 'T 'N)))))

(define result
  (classify-document "WINNER: you have an URGENT invoice to pay" spam-classifiers))

;; The document matches both spam and non-spam indicators
(logical-set-member result 'spam)    ;; => 'B  (both spam and non-spam signals)
(logical-set-member result 'urgent)  ;; => 'T  (clear signal)

;; Instead of picking one: surface the contradiction
(belnap-set-contradictions result)   ;; => '(spam)
;; Route contradicted documents to human review, not the spam folder
```

### Chaos 4: Set Algebra Across Multiple Universes

Combine sets from different theories into a single operation by normalizing through a
common logic:

```scheme
;; You have a fuzzy set and a probabilistic set
;; They're about the same elements, but in different logics
;; How do you combine them?

(define fuzzy-warmth
  (fuzzy-set 'equator 1.0  'tropics 0.85  'temperate 0.5  'arctic 0.1))

(define prob-habitable
  (alist->logical-set probabilistic-logic
    '((equator . 0.7) (tropics . 0.9) (temperate . 0.95) (arctic . 0.2))))

;; Strategy: convert both to classical by alpha-cut / threshold, then intersect
(define habitable-warm
  (set-intersection
    (fuzzy-alpha-cut fuzzy-warmth 0.6)          ; warm enough
    (logical-set->set prob-habitable)))          ; probably habitable

(set->list habitable-warm)
;; => '(equator tropics)  — only these meet both criteria
```

### Chaos 5: The Self-Referential Multiset

Cantor's paradox meets modern programming: the histogram of the histogram.

```scheme
;; Build the frequency distribution of a frequency distribution
(define (histogram-of-histogram ms)
  ;; The "second-order" histogram: how often does each count appear?
  (list->multiset (map cdr (multiset->alist ms))))

(define text (list->multiset
  '(the the the a a and and and and of of is is is is)))

(multiset->alist text)
;; => ((the . 3) (a . 2) (and . 4) (of . 2) (is . 4))

(multiset->alist (histogram-of-histogram text))
;; => ((2 . 2) (3 . 1) (4 . 2))
;; "Count 2 appears twice (a, of), count 3 appears once (the), count 4 appears twice"

;; And again...
(multiset->alist (histogram-of-histogram (histogram-of-histogram text)))
;; => ((1 . 1) (2 . 2))
;; The meta-structure: count 1 appears once (the count 3), count 2 appears twice
;; Every time you take the histogram-of-histogram, it compresses toward Zipf
```

---

## Connecting the Two Chapters

If you've read the previous chapter, you'll have noticed something: `logical-set-assert!`
and `kb-assert!` do almost exactly the same thing. Both accumulate evidence via
`logic-combine`. Both use `bottom` as the "not yet asserted" sentinel.

This is not a coincidence. A **knowledge base** (`make-kb`) and a **logical set**
(`make-logical-set`) are the same structure applied to different domains:

- KB: propositions as elements, truth values as membership
- Logical set: domain elements as elements, truth values as membership

The KB is a logical set where the "elements" are propositions about the world. A logical
set is a KB where the "propositions" are `(x ∈ S)` for each element x.

This means every technique from the logic chapter applies here. Forward-chaining rules
(`kb-add-rule!`, `kb-close!`) can compute set membership transitively. Consistency
checking (`kb-consistent?`) detects contradictory membership claims. And every logic
(`belnap-four`, `fuzzy-logic`, `intuitionistic-logic`, ...) gives a different semantics
to the set algebra.

The framework is the same. The domain changes. This is the point.

---

## Why Scáth Added This

Because "either it's in the set or it isn't" is exactly as wrong as "either it's true or
it isn't," and for exactly the same reasons.

Because every real collection you've ever worked with has membership that doesn't cleave
cleanly. "Who are our customers?" — some are active, some lapsed, some duplicate
records, some former customers who came back, some companies that merged. The classical
characteristic function forces you to draw a line somewhere arbitrary, and then you build
logic on top of that arbitrary line, and then you wonder why edge cases break things.

Because fuzzy sets have been the correct formalization of categories since 1965 and most
languages still don't have them natively. Eleanor Rosch's prototype theory in cognitive
science (1975) showed that human categories have *graded membership* — "robin" is a more
typical bird than "penguin," which is a more typical bird than "ostrich." We reason about
prototypes and gradations, not crisp boundaries. The languages didn't get the memo.

Because multisets solve a specific problem that lists and sets both fail at: "how many
of these do I have?" is not the same question as "do I have any of these?" and forcing
the answer into a hash-map-of-counts is boilerplate that compiles away the semantics.

Because the paraconsistent set is the honest representation of what your data warehouse
actually contains after three rounds of ETL from inconsistent sources, and the current
industry answer ("pick one and log a warning") destroys information and silently corrupts
downstream decisions.

Because the connection between the logic chapter and this one — that a set is just a
membership function, and a membership function is just a proposition, and propositions
live in a logic — is one of those structural observations that once seen cannot be unseen.
Types are propositions (Curry-Howard). Proofs are programs (Curry-Howard again). Logic is
set theory (Lindenbaum-Tarski algebras). It's the same table.

Because Curry already has an extended numeric tower, a symbolic CAS, Clifford algebras,
sexagesimal arithmetic, quantum superpositions, and Akkadian error messages. A language
that speaks cuneiform and computes with surreal numbers really has no excuse for not
supporting fuzzy membership.

Because the classical boundary is a choice, and so is every other boundary, and you should
know what you're choosing and why.

---

## Quick Reference

```scheme
(import (curry sets))   ;; provides all of the below
(import (curry logic))  ;; for the logic values: belnap-four, fuzzy-logic, etc.

;;; Core hash-sets (also available without import)
(make-set)                           ;; equal? comparator
(set=? a b) (set-empty? s)
(set-copy s)
(set-adjoin s x y z)                 ;; non-destructive add-many
(set-delete s x)                     ;; non-destructive remove
(set-symmetric-difference a b)       ;; (A\B)∪(B\A)
(set-for-each proc s)                ;; (proc elem)
(set-map proc s)                     ;; (proc elem) → new-elem
(set-filter pred s)                  ;; functional
(set-filter! pred s)                 ;; destructive
(set-fold proc init s)               ;; (proc acc elem)
(set-any? pred s) (set-every? pred s)
(set-count pred s) (set-find pred s [default])

;;; Multisets
(make-multiset)                       ;; empty bag
(multiset e1 e2 ...)                  ;; bag from elements
(list->multiset lst)                  ;; one increment per element
(alist->multiset '((elem . count) …)) ;; explicit counts
(multiset-copy ms)
(multiset-count ms elem)              ;; → fixnum (0 if absent)
(multiset-member? ms elem)
(multiset-size ms)                    ;; distinct elements
(multiset-total ms)                   ;; sum of all counts
(multiset-empty? ms) (multiset=? a b) (multiset-subset? a b)
(multiset-add!  ms elem)              ;; destructive +1
(multiset-add-n! ms elem n)           ;; destructive +n
(multiset-remove! ms elem)            ;; destructive −1 (floor 0)
(multiset-remove-all! ms elem)        ;; destructive → 0
(multiset-set-count! ms elem n)
(multiset-add ms elem)               ;; functional (copy + add)
(multiset-remove ms elem)            ;; functional (copy + remove)
(multiset-union a b)                 ;; max per element
(multiset-intersection a b)          ;; min per element
(multiset-sum a b)                   ;; + per element (bag concat)
(multiset-difference a b)            ;; max(0, a−b) per element
(multiset-scale ms n)                ;; multiply all counts by n
(multiset-for-each proc ms)          ;; (proc elem count)
(multiset-map proc ms)               ;; (proc elem count) → (new-elem . new-count)
(multiset-filter pred ms)            ;; (pred elem count) → bool
(multiset-fold proc init ms)         ;; (proc acc elem count)
(multiset-any? pred ms) (multiset-every? pred ms)
(multiset->alist ms) (multiset->list ms)

;;; Logical sets
(make-logical-set logic)             ;; empty set in this logic
(logical-set logic e1 e2 …)         ;; elements get logic-top membership
(list->logical-set logic lst)
(alist->logical-set logic '((e . tv) …))
(logical-set-copy s)
(logical-set-logic s)                ;; → the logic
(logical-set-member s elem)          ;; → truth-value (bottom if absent)
(logical-set-contains? s elem)       ;; → bool (uses logic's entails?)
(logical-set-assert! s elem tv)      ;; combine new evidence via logic-combine
(logical-set-retract! s elem)        ;; remove element entirely
(logical-set-size s) (logical-set-empty? s)
(logical-set-elements s) (logical-set->alist s)
(logical-set-union a b)              ;; pointwise join
(logical-set-intersection a b)       ;; pointwise meet
(logical-set-complement s)           ;; pointwise not
(logical-set-difference a b)         ;; a ∩ ¬b (computed directly)
(logical-set-symmetric-difference a b)
(logical-set-for-each proc s)        ;; (proc elem tv)
(logical-set-map proc s)             ;; (proc elem tv) → (new-elem . new-tv)
(logical-set-filter pred s)          ;; (pred elem tv) → bool
(logical-set-fold proc init s)       ;; (proc acc elem tv)
(logical-set-any? pred s) (logical-set-every? pred s)

;;; Convenience constructors
(fuzzy-set e1 d1  e2 d2 … e)        ;; elements with optional degrees (default 1.0)
(belnap-set e1 v1 e2 v2 … e)        ;; elements with optional Belnap values (default 'T)
(fuzzy-alpha-cut fs alpha)           ;; → core set of elements with degree > alpha
(belnap-set-contradictions s)        ;; → list of elements with 'B membership
(belnap-set-unknowns s)             ;; → list of elements explicitly tracked as 'N
(logical-set->set s)                 ;; → core hash-set of entailed elements
(set->logical-set logic core-set)    ;; → logical-set with top membership for each element
```
