# Quantum Superposition Values

Curry has a first-class quantum superposition type. A quantum value is a normalized probability amplitude distribution over ordinary Scheme values. It can be operated on arithmetically without collapsing, and collapses to a single value only when explicitly observed.

This is not a quantum circuit simulator. The amplitudes are classical complex numbers. What is unusual is that the *values being superposed* are arbitrary Scheme values — numbers, strings, lists, procedures, actors — not qubits.

## Construction

### `superpose` — explicit amplitudes

```scheme
(superpose '((amplitude . value) ...))
```

Constructs a quantum value from an association list of `(amplitude . value)` pairs. The amplitude can be any number (real or complex). Amplitudes are automatically normalised so that Σ|αᵢ|² = 1.

```scheme
; Equal superposition of 0 and 1
(superpose '((1 . 0) (1 . 1)))
; => #|0.7071|0> + 0.7071|1>|

; Weighted: spin-up twice as probable as spin-down
(superpose '((2 . 'up) (1 . 'down)))
; => #|0.8944|up> + 0.4472|down>|

; Complex amplitudes (interference)
(superpose `((,(make-rectangular 1 0) . "spin+")
             (,(make-rectangular 0 1) . "spin-")))
```

### `quantum-uniform` — equal amplitudes

```scheme
(quantum-uniform '(value ...))
```

Constructs a uniformly-weighted superposition over a list of values. All amplitudes are 1/√n.

```scheme
(quantum-uniform '(red green blue))
; => #|0.5774|red> + 0.5774|green> + 0.5774|blue>|

(quantum-uniform '(1 2 3 4 5 6))   ; fair quantum die
```

## Observation (collapse)

```scheme
(observe q)
```

Collapses the quantum value to a single outcome, chosen with probability |αᵢ|². Each call to `observe` makes an independent measurement.

```scheme
(define coin (quantum-uniform '(heads tails)))
(observe coin)   ; => heads  (50% probability)
(observe coin)   ; => tails  (independent — coin is not modified)
```

`observe` does not modify the quantum value. The original superposition remains intact; you can observe it again.

## Display

Quantum values print in Dirac bra-ket notation:

```
#|α₁|v₁> + α₂|v₂> + ...|
```

where αᵢ is the magnitude of the amplitude (printed as a decimal) and vᵢ is the value printed in the usual Scheme way.

```scheme
(quantum-uniform '(1 2 3))
; displays as: #|0.5774|1> + 0.5774|2> + 0.5774|3>|

(superpose '((3 . "cat") (4 . "dog")))
; displays as: #|0.6|cat> + 0.8|dog>|
```

## Arithmetic

All standard arithmetic operators lift over quantum values. The operation is applied independently to each branch (state). The amplitudes are preserved unchanged — this is like a quantum gate that only affects the value register.

### Scalar arithmetic

```scheme
(define q (quantum-uniform '(1 2 3)))

(+ q 10)           ; => #|0.5774|11> + 0.5774|12> + 0.5774|13>|
(* q 2)            ; => #|0.5774|2> + 0.5774|4> + 0.5774|6>|
(- q 1)            ; => #|0.5774|0> + 0.5774|1> + 0.5774|2>|
(/ q 2)            ; => #|0.5774|1/2> + 0.5774|1> + 0.5774|3/2>|
(expt q 2)         ; => #|0.5774|1> + 0.5774|4> + 0.5774|9>|
(sqrt q)           ; => #|0.5774|1> + 0.5774|1.4142> + 0.5774|1.7320>|
```

### Quantum + quantum (superposition)

Adding two quantum values creates a new superposition combining both sets of states. Each set of amplitudes is scaled by 1/√2 to maintain normalisation.

```scheme
(define q1 (quantum-uniform '(a b)))
(define q2 (quantum-uniform '(c d)))
(+ q1 q2)
; => superposition of a, b, c, d each with amplitude 1/2
```

This is analogous to combining two quantum registers. Division of one quantum value by another raises an error.

### Symbolic + quantum

Symbolic expressions and quantum values compose. The result is a quantum value whose branches are symbolic expressions.

```scheme
(symbolic m v)
(define q-v (quantum-uniform '(1 2 3)))    ; velocity in superposition
(define q-ke (* 1/2 m (expt q-v 2)))
; Each branch: (* 1/2 m 1), (* 1/2 m 4), (* 1/2 m 9)
; i.e., the kinetic energy expression in each velocity eigenstate
```

## Inspection

```scheme
(quantum? v)          ; #t if v is a quantum value
(quantum-n q)         ; number of states (branches)
(quantum-states q)    ; list of (amplitude . value) pairs (exact representation)
```

```scheme
(define q (superpose '((3 . "cat") (4 . "dog"))))
(quantum?  q)          ; => #t
(quantum-n q)          ; => 2
(quantum-states q)     ; => ((0.6 . "cat") (0.8 . "dog"))
```

## Quantum map

Lower-level interface for applying an arbitrary function to each branch value:

```scheme
(quantum-map f q)
```

Returns a new quantum value with the same amplitudes and values `(f vᵢ)`. This is the primitive underlying the arithmetic operators.

```scheme
(define q (quantum-uniform '(1 2 3 4)))
(quantum-map (lambda (x) (* x x)) q)
; => #|0.5|1> + 0.5|4> + 0.5|9> + 0.5|16>|
```

## Patterns

### Probabilistic branching without `if`

```scheme
(define result
  (quantum-uniform '(option-a option-b option-c)))
(define chosen (observe result))
```

### Quantum random walk

```scheme
(define (step pos)
  (observe (quantum-uniform (list (- pos 1) (+ pos 1)))))

(let loop ((pos 0) (t 0))
  (display pos) (newline)
  (if (< t 100) (loop (step pos) (+ t 1))))
```

### Quantum die

```scheme
(define d6 (quantum-uniform '(1 2 3 4 5 6)))
(define (roll) (observe d6))
(roll)   ; => some integer 1–6
```

### Physics: wavepacket in D dimensions

The quantum type lets you represent a discretised probability amplitude over a spatial grid as a Scheme value:

```scheme
(define (gaussian-wavepacket xs x0 sigma)
  (superpose
    (map (lambda (x)
           (cons (exp (- (/ (expt (- x x0) 2)
                            (* 2 sigma sigma))))
                 x))
         xs)))

(define xs (map (lambda (i) (- i 50)) (iota 100)))
(define psi (gaussian-wavepacket xs 0 5))
(quantum-n psi)   ; => 100
(observe psi)     ; => some x value near 0
```

## Quick-reference table

| Procedure | Description |
|-----------|-------------|
| `(superpose alist)` | Create quantum from (amplitude . value) list |
| `(quantum-uniform lst)` | Create uniform superposition over list |
| `(observe q)` | Collapse to a single value probabilistically |
| `(quantum? v)` | Predicate |
| `(quantum-n q)` | Number of states |
| `(quantum-states q)` | List of (amplitude . value) pairs |
| `(quantum-map f q)` | Apply f to each branch value |
| `(+ q s)` `(* q s)` etc. | Scalar arithmetic over all branches |
| `(+ q1 q2)` | Combine two quantum values (superpose) |

## Notes on physics accuracy

The type uses classical probability amplitudes, not a unitary quantum circuit model. There is no notion of entanglement between separate quantum values, no gate set, and no decoherence. The type is useful for:

- Probabilistic value selection with interference-weight control
- Representing ensemble uncertainty over Scheme values
- Combining with symbolic expressions to track uncertainty through a calculation

For actual quantum circuit simulation, use a dedicated library.
