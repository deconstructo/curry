;;; SRFI-141: Integer Division.
;;;
;;; R7RS itself already standardizes the floor and truncate families
;;; (floor/, floor-quotient, floor-remainder, truncate/, ...) -- this
;;; library only needs to add the four families SRFI-141 defines beyond
;;; that: ceiling, round, euclidean, and balanced. All four are derived
;;; here purely in terms of the already-existing floor-quotient/
;;; floor-remainder, rather than raw division, specifically to avoid
;;; getting a sign/off-by-one wrong -- every formula below was checked
;;; by hand against concrete positive/negative/tie examples before being
;;; written down (see the derivation notes on each family).
;;;
;;; The SRFI says it's an error for any argument to be a non-integer or
;;; for the denominator to be zero. Zero-denominator is genuinely
;;; inherited for free -- every family here bottoms out in
;;; floor-quotient/floor-remainder, which already raise on that. The
;;; non-integer case is NOT actually enforced, however: curry's own
;;; core floor-quotient/floor-remainder silently accept a flonum or
;;; exact rational and return a plausible-looking but not-per-spec
;;; result (e.g. (floor-quotient 15/2 2) => 3, (floor-remainder 7.5 2)
;;; => 1.5) rather than raising -- confirmed directly, not assumed.
;;; That's a pre-existing gap in the core primitives this library is
;;; built on, not something introduced here, but it does mean this
;;; library's own error behavior is correspondingly permissive rather
;;; than fully spec-conformant.
(define-library (srfi s141 division-operators)
  (import (scheme base))
  (export
    ceiling/ ceiling-quotient ceiling-remainder
    round/ round-quotient round-remainder
    euclidean/ euclidean-quotient euclidean-remainder
    balanced/ balanced-quotient balanced-remainder)
  (begin

    ;; ceiling(n/d) = -floor(-n/d), a standard identity -- avoids ever
    ;; having to reason about remainder signs directly for this family.
    (define (ceiling-quotient n d) (- (floor-quotient (- n) d)))
    (define (ceiling-remainder n d) (- n (* d (ceiling-quotient n d))))
    (define (ceiling/ n d) (values (ceiling-quotient n d) (ceiling-remainder n d)))

    ;; Round to nearest, ties to even. fq/fr (floor-quotient/-remainder)
    ;; always satisfy n = d*fq + fr with |fr| < |d|; comparing 2*|fr|
    ;; against |d| exactly (both are integers, so this is an exact
    ;; comparison, never a real-number one) tells us which of fq or
    ;; fq+1 is nearer, and an exact match is the tie case.
    ;;
    ;; Checked by hand: round(7/2)=4 (tie, fq=3 is odd -> fq+1=4, even);
    ;; round(5/2)=2 (tie, fq=2 is already even); round(-7/2)=-4 (tie,
    ;; fq=-4 is even); round(7/3)=2 and round(8/3)=3 (plain non-ties).
    (define (round-quotient n d)
      (let* ((fq (floor-quotient n d)) (fr (floor-remainder n d))
             (twice-|fr| (* 2 (abs fr))) (|d| (abs d)))
        (cond
          ((< twice-|fr| |d|) fq)
          ((> twice-|fr| |d|) (+ fq 1))
          (else (if (even? fq) fq (+ fq 1))))))
    (define (round-remainder n d) (- n (* d (round-quotient n d))))
    (define (round/ n d) (values (round-quotient n d) (round-remainder n d)))

    ;; euclidean-remainder is always in [0, |d|) regardless of d's
    ;; sign -- floor-remainder already gives exactly that range when
    ;; d > 0 (its own sign convention matches the divisor's sign), so
    ;; only the d < 0 case needs the ceiling family instead.
    ;;
    ;; Checked by hand: euclidean(7,-2)=(-3,1); euclidean(-7,2)=(-4,1);
    ;; euclidean(-7,-2)=(4,1) -- remainder is 1 in [0,2) every time.
    (define (euclidean-quotient n d) (if (positive? d) (floor-quotient n d) (ceiling-quotient n d)))
    (define (euclidean-remainder n d) (- n (* d (euclidean-quotient n d))))
    (define (euclidean/ n d) (values (euclidean-quotient n d) (euclidean-remainder n d)))

    ;; balanced-remainder is always in [-|d|/2, |d|/2) -- note the range
    ;; is asymmetric (lower bound inclusive, upper bound exclusive), so
    ;; which quotient a tie resolves to isn't a free choice: whichever
    ;; way you break it, exactly one of the two candidate remainders
    ;; (+|d|/2 or -|d|/2) is actually in range, and it depends on the
    ;; SIGN OF d which one that is -- an earlier version of this file
    ;; only checked positive-d examples by hand and got this wrong for
    ;; negative d, caught by independent review running brute-force
    ;; checks across all sign combinations, not just reasoning about it.
    ;;
    ;; For d > 0: a tie's two candidates are r=+|d|/2 (from fq) and
    ;; r=-|d|/2 (from fq+1); only the latter is in [-|d|/2, |d|/2), so
    ;; ties resolve toward fq+1.
    ;; For d < 0: floor-quotient's fq is already one *smaller* than it
    ;; would be for the same |d| with d>0 (verify: floor-quotient(7,-2)
    ;; = -4, floor-quotient(7,2) = 3 -- not simply negated), so the tie
    ;; candidates flip: r=-|d|/2 comes from fq itself, r=+|d|/2 comes
    ;; from fq+1 -- only fq's remainder is in range, so ties resolve
    ;; toward fq, not fq+1.
    ;;
    ;; Checked directly against curry (not just derived on paper) for
    ;; both signs of d at a tie:
    ;; balanced(6,4): fq=1,fr=2 (tie, d>0) -> q=2 (fq+1), r=-2, in [-2,2)
    ;; balanced(7,-2): fq=-4,fr=-1 (tie, d<0) -> q=-4 (fq), r=-1, in [-1,1)
    ;; balanced(10,-4): fq=-3,fr=-2 (tie, d<0) -> q=-3 (fq), r=-2, in [-2,2)
    ;; The non-tie branches (strictly under or over half) are unaffected
    ;; by d's sign -- only an exact tie is sign-sensitive, since that's
    ;; the only case where two different quotients both give a
    ;; remainder of the same magnitude.
    (define (balanced-quotient n d)
      (let* ((fq (floor-quotient n d)) (fr (floor-remainder n d))
             (twice-|fr| (* 2 (abs fr))) (|d| (abs d)))
        (cond
          ((< twice-|fr| |d|) fq)
          ((> twice-|fr| |d|) (+ fq 1))
          (else (if (positive? d) (+ fq 1) fq)))))
    (define (balanced-remainder n d) (- n (* d (balanced-quotient n d))))
    (define (balanced/ n d) (values (balanced-quotient n d) (balanced-remainder n d)))))
