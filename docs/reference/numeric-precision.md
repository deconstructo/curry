# Arbitrary-precision floats and number theory

This document covers curry's MPFR-backed arbitrary-precision floating-point
type and the always-on number theory library.  The MPFR feature requires the
interpreter to be built with `-DBUILD_MPFR=ON` (and `libmpfr` installed).
The number theory functions are always available — they only depend on GMP.

## MPFR floats

MPFR (the [GNU MPFR library](https://www.mpfr.org/)) adds correctly-rounded
arbitrary-precision IEEE-style floats to curry's numeric tower.  An MPFR value
behaves like a flonum but carries a precision tag (in bits of mantissa) and
participates in arithmetic with full precision.

### Construction

```scheme
(mpfr x)              ; coerce any real value to MPFR at the current default precision
(mpfr x precision)    ; coerce x to MPFR at the given precision (in bits)
(mpfr? v)             ; type predicate
(mpfr-precision v)    ; precision (in bits) of an MPFR value
(mpfr-set-precision v prec)  ; re-round v to a different precision
```

### Precision context

Computations honour a thread-local precision context.  Set it for the duration
of a body with `with-precision`:

```scheme
(with-precision 256
  (mpfr-pi))                  ; π to 256 bits ≈ 77 decimal digits
(with-precision 1024
  (+ (mpfr 1/3) (mpfr 2/3)))  ; 1.0 to 1024 bits

(current-precision)           ; query the active precision (bits)
(mpfr-rounding-mode)          ; query current rounding mode (default 'rndn)
(mpfr-rounding-mode 'rndd)    ; set rounding mode: 'rndn 'rndz 'rndu 'rndd 'rnda
```

When the context is inactive the default precision is 128 bits.

Equivalent low-level entry point:

```scheme
(call-with-precision 256 (lambda () (mpfr-pi)))
```

### Constants

```scheme
(mpfr-pi      [prec])  ; π
(mpfr-e       [prec])  ; Euler's number e
(mpfr-phi     [prec])  ; golden ratio (1+√5)/2
(mpfr-log2    [prec])  ; ln(2)
(mpfr-euler   [prec])  ; Euler–Mascheroni constant γ
(mpfr-catalan [prec])  ; Catalan's constant G
(mpfr-apery   [prec])  ; ζ(3)
```

When the optional `prec` argument is omitted, the current precision context
applies.

### Transcendentals

All of the standard trig, hyperbolic, exponential and logarithmic primitives
in curry (`sin`, `cos`, `tan`, `exp`, `log`, `sqrt`, `sinh`, `tanh`, etc.)
dispatch to MPFR when given an MPFR operand and preserve full precision.

MPFR-specific functions for special values that the standard tower does not
carry:

```scheme
(mpfr-gamma  x)    ; Γ(x)
(mpfr-lgamma x)    ; log Γ(x)
(mpfr-zeta   x)    ; Riemann ζ(x)
(mpfr-erf    x)    ; error function
(mpfr-erfc   x)    ; complementary error
(mpfr-j0     x)    ; Bessel J₀
(mpfr-j1     x)    ; Bessel J₁
(mpfr-hypot  x y)  ; √(x²+y²) with no overflow
(mpfr-fma    a b c) ; correctly-rounded a·b + c
(mpfr-log2-of  x)  ; base-2 logarithm of x
(mpfr-log10    x)  ; base-10 logarithm of x
```

### YBC 7289

A simple Babylonian-style approximation example.  The cuneiform tablet
YBC 7289 gives √2 ≈ 1;24,51,10 in base 60 — about 5.7 decimal digits.  We can
beat that with MPFR easily:

```scheme
(with-precision 200
  (mpfr-sqrt (mpfr 2)))     ; 60-digit √2
```

### Interval arithmetic

For certified bounds, curry provides interval values with directed-rounding
MPFR endpoints:

```scheme
(make-interval lo hi)        ; build an interval from any reals
(interval x)                 ; point interval [x, x]
(interval? v)                ; type predicate
(interval-lo iv)             ; lower endpoint (rounded down)
(interval-hi iv)             ; upper endpoint (rounded up)
(interval-midpoint iv)
(interval-width    iv)
(interval-contains? iv x)
```

Arithmetic on intervals (when needed by the host program) is performed at the
endpoint level using directed-rounded MPFR.

### Conversion back

```scheme
(inexact (with-precision 256 (mpfr-pi)))   ; → IEEE 754 double (53-bit)
(exact   (mpfr-pi))                         ; → exact rational
(number->string (with-precision 200 (mpfr-pi)))
```

## Number theory

The number theory library is always present (GMP only — no MPFR dependency).
All results are exact (fixnum, bignum or rational) wherever an exact answer
exists.

### Primality and factoring

```scheme
(prime? n)               ; Miller–Rabin (25 rounds)
(next-prime n)
(prev-prime n)
(factor n)               ; ascending list of prime factors with multiplicity
(prime-factors n)        ; distinct prime factors
```

Factoring uses trial division up to 10⁵ then Brent's variant of Pollard ρ for
the remaining composite cofactors.

### Arithmetic functions

```scheme
(totient n)              ; Euler φ
(carmichael n)           ; λ
(mobius n)               ; μ ∈ {-1, 0, 1}
(divisors n)             ; ascending list
(divisor-count n)        ; τ(n)
(divisor-sum   n)        ; σ(n)
(perfect?   n) (abundant? n) (deficient? n)
(omega n)                ; # distinct primes
(big-omega n)            ; # primes with multiplicity
```

### Modular arithmetic

```scheme
(mod-expt b e m)         ; b^e mod m
(mod-inverse a m)        ; raises if not invertible
(jacobi-symbol    a n)
(kronecker-symbol a n)
(legendre-symbol  a p)
(extended-gcd a b)       ; (values gcd s t) with a·s + b·t = gcd
(chinese-remainder rems mods)   ; pairwise-coprime mods
```

### Sequences

```scheme
(fibonacci n)            ; fast doubling — O(log n) multiplications
(lucas    n)
(binomial n k)
(multinomial n (k1 k2 ...))
(catalan  n)
(bernoulli n)            ; exact rational
(euler-number n)
(stirling1 n k)          ; unsigned, first kind
(stirling2 n k)          ; second kind
(bell n)
(partition-count n)      ; p(n), Euler's pentagonal recurrence
```

### Continued fractions

```scheme
(continued-fraction x)        ; list of partial quotients
(continued-fraction x terms)  ; cap the expansion at `terms` quotients
(convergents cf-list)         ; list of rational convergents
(best-rational-approx x max-den)
```

### Predicates

```scheme
(squarefree? n)
(perfect-power? n)       ; → #f or (values base exp)
(smooth? n k)            ; all prime factors ≤ k
```

## Performance notes

* Fibonacci uses the doubling identities `F(2k) = F(k)·(2·F(k+1)-F(k))` and
  `F(2k+1) = F(k)² + F(k+1)²`, giving `O(log n)` arbitrary-precision
  multiplications.
* Bernoulli numbers are computed via the standard recurrence and memoized in
  a process-wide cache, so once `(bernoulli 100)` has been evaluated, future
  small calls are O(1).
* Partition numbers use Euler's pentagonal recurrence, `O(n √n)` integer
  additions; sufficient for n up to a few thousand without effort.
* Factoring beyond ~10²⁰ falls back to Pollard ρ — useful for moderate
  composites but not a cryptographic factorizer.

## Build

```bash
# Linux
sudo apt install libmpfr-dev
cmake -B build -DBUILD_MPFR=ON
cmake --build build -j$(nproc)
ctest --test-dir build -V -R mpfr

# macOS
brew install mpfr
cmake -B build -DBUILD_MPFR=ON
cmake --build build -j$(sysctl -n hw.logicalcpu)
```
