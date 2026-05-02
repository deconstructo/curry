# Matrices and Tensors

Curry provides first-class matrices and tensors: dense, mutable, double-precision floating-point arrays in 2D and N-dimensional forms. Both types are GC-managed and use a dedicated namespace (`mat-*`, `tensor-*`) to make dimension-mismatch errors explicit rather than silently surfacing through `+` or `*`.

## Matrix

A matrix is a 2D array of `double` values stored in row-major order. Indices are 0-based throughout.

### Construction

```scheme
; (make-matrix rows cols) → zero matrix
(define m (make-matrix 3 4))          ; 3×4 zero matrix

; (make-matrix rows cols fill) → filled matrix
(define m (make-matrix 3 3 1.0))      ; 3×3 matrix of ones

; (matrix-identity n) → n×n identity matrix
(define I (matrix-identity 4))

; (matrix rows cols list) → matrix from a flat list, row-major
(define A (matrix 2 3 '(1 2 3 4 5 6)))
; ┌ 1 2 3 ┐
; └ 4 5 6 ┘
```

### Access and mutation

```scheme
(matrix-rows m)           ; number of rows
(matrix-cols m)           ; number of columns
(matrix-ref  m i j)       ; element at row i, col j  (0-based)
(matrix-set! m i j v)     ; mutate element at row i, col j
(matrix-copy m)           ; shallow copy (same dimensions, independent data)
```

### Arithmetic

All arithmetic returns a **new** matrix; the inputs are not mutated.

```scheme
; Element-wise addition / subtraction / negation
(mat+ A B)          ; A and B must have identical dimensions
(mat+ A B C ...)    ; variadic — left-associative
(mat- A B)
(mat- A)            ; unary negation

; Matrix product (non-commutative)
(mat* A B)          ; A's cols must equal B's rows
(mat* A B C ...)    ; left-associative chain product

; Scalar operations
(mat-scale A 2.5)   ; multiply every element by 2.5

; Transpose
(mat-transpose A)   ; rows ↔ cols
```

### Higher-order operations

```scheme
; Apply a procedure to every element, return new matrix of same shape
(mat-map proc m)

; Fold a procedure over all elements row-major, accumulating a result
(mat-fold proc init m)    ; (proc acc element) called for each element

; Extract a single row or column as a list
(mat-row m i)    ; i-th row as a list of flonums
(mat-col m j)    ; j-th column as a list of flonums

; Convert the whole matrix to a nested list of rows
(mat->list m)    ; ((r0c0 r0c1 …) (r1c0 …) …)
```

### Norms and invariants

```scheme
(mat-trace     m)   ; sum of diagonal elements (non-square: min(rows,cols) diagonal)
(mat-frobenius m)   ; Frobenius norm  √(Σ xᵢⱼ²)
```

### Predicates

```scheme
(matrix? x)    ; #t iff x is a matrix
```

### Display

Matrices display as `#<matrix RxC [r0c0 r0c1 … | r1c0 …]>`:

```
#<matrix 2x3 [1 2 3 | 4 5 6]>
```

---

## Tensor

A tensor is an N-dimensional array of `double` values stored in row-major (C) order. The shape is a list of positive integers. Indices are 0-based throughout.

### Construction

```scheme
; (make-tensor shape) → zero tensor; shape is a list of dimension sizes
(define t (make-tensor '(3 4 5)))    ; 3×4×5 zero tensor

; (make-tensor shape fill)
(define t (make-tensor '(2 3) 1.0))  ; 2×3 tensor filled with 1.0
```

### Access and mutation

```scheme
(tensor-shape t)              ; shape as a list, e.g. (3 4 5)
(tensor-ndim  t)              ; number of dimensions
(tensor-size  t)              ; total element count = product of dims
(tensor-ref   t i0 i1 …)     ; element — pass one index per dimension
(tensor-set!  t i0 i1 … v)   ; mutate — last argument is the new value
(tensor-copy  t)              ; independent copy
```

### Arithmetic

All arithmetic returns a **new** tensor; inputs are not mutated.

```scheme
; Element-wise operations — shapes must match exactly
(tensor+ a b)
(tensor+ a b c …)   ; variadic
(tensor- a b)
(tensor- a)          ; unary negation

; Scalar scaling
(tensor-scale t 3.0)

; Apply a procedure element-wise
(tensor-map proc t)

; Outer (tensor) product — result shape is concat of both shapes
(tensor-outer a b)   ; e.g. shape (2 3) ⊗ shape (4 5) → shape (2 3 4 5)

; Reshape — total size must be preserved
(tensor-reshape t '(12))          ; flatten a 3×4 tensor to length-12 vector
(tensor-reshape t '(2 6))         ; reinterpret as 2×6
```

### Conversion

```scheme
; Convert to nested Scheme lists matching the shape
(tensor->list t)   ; (((1.0 2.0) (3.0 4.0)) ((5.0 6.0) (7.0 8.0)))

; Round-trip between Matrix and 2D Tensor
(matrix->tensor m)   ; wraps a matrix as a 2×… tensor (no data copy)
(tensor->matrix t)   ; requires exactly 2 dimensions
```

### Predicates

```scheme
(tensor? x)    ; #t iff x is a tensor
```

### Display

Tensors display as `#<tensor d0xd1x… nested-data>`:

```
#<tensor 2x3 ((1 2 3) (4 5 6))>
#<tensor 2x2x2 (((1 2) (3 4)) ((5 6) (7 8)))>
```

---

## Examples

### Solving a system of linear equations (Gaussian elimination)

```scheme
;;; Solve A·x = b by Gaussian elimination with partial pivoting.
;;; Returns a vector of solutions, or raises if A is singular.

(define (solve-linear A b)
  (let* ((n   (matrix-rows A))
         (aug (make-matrix n (+ n 1))))
    ; Build augmented matrix [A | b]
    (do ((i 0 (+ i 1))) ((= i n))
      (do ((j 0 (+ j 1))) ((= j n))
        (matrix-set! aug i j (matrix-ref A i j)))
      (matrix-set! aug i n (list-ref b i)))
    ; Forward elimination with partial pivoting
    (do ((col 0 (+ col 1))) ((= col n))
      (let* ((pivot-row
              (let loop ((r col) (best col))
                (if (>= r n) best
                    (loop (+ r 1)
                          (if (> (abs (matrix-ref aug r col))
                                 (abs (matrix-ref aug best col)))
                              r best)))))
             (pv (matrix-ref aug pivot-row col)))
        (when (< (abs pv) 1e-12) (error "solve-linear: singular matrix"))
        ; Swap rows
        (when (not (= pivot-row col))
          (do ((j 0 (+ j 1))) ((= j (+ n 1)))
            (let ((tmp (matrix-ref aug col j)))
              (matrix-set! aug col j (matrix-ref aug pivot-row j))
              (matrix-set! aug pivot-row j tmp))))
        ; Eliminate below
        (do ((row (+ col 1) (+ row 1))) ((= row n))
          (let ((factor (/ (matrix-ref aug row col)
                           (matrix-ref aug col col))))
            (do ((j col (+ j 1))) ((= j (+ n 1)))
              (matrix-set! aug row j
                (- (matrix-ref aug row j)
                   (* factor (matrix-ref aug col j)))))))))
    ; Back substitution
    (let ((x (make-vector n 0.0)))
      (do ((i (- n 1) (- i 1))) ((< i 0) x)
        (let ((sum (matrix-ref aug i n)))
          (do ((j (+ i 1) (+ j 1))) ((= j n))
            (set! sum (- sum (* (matrix-ref aug i j) (vector-ref x j)))))
          (vector-set! x i (/ sum (matrix-ref aug i i))))))))

; Solve  x + 2y = 5
;       3x +  y = 10
(define A (matrix 2 2 '(1 2 3 1)))
(define b '(5.0 10.0))
(display (solve-linear A b))   ; → #(3. 1.)
(newline)
```

### Convolution kernel application

```scheme
;;; Apply a 3×3 kernel to an image represented as a 2D matrix.
;;; Out-of-bounds pixels are clamped to zero.

(define (convolve2d image kernel)
  (let ((rows (matrix-rows image))
        (cols (matrix-cols image))
        (kr   (matrix-rows kernel))
        (kc   (matrix-cols kernel)))
    (let ((out   (make-matrix rows cols))
          (kr/2  (quotient kr 2))
          (kc/2  (quotient kc 2)))
      (do ((i 0 (+ i 1))) ((= i rows) out)
        (do ((j 0 (+ j 1))) ((= j cols))
          (let ((sum 0.0))
            (do ((ki 0 (+ ki 1))) ((= ki kr))
              (do ((kj 0 (+ kj 1))) ((= kj kc))
                (let ((si (+ i ki (- kr/2)))
                      (sj (+ j kj (- kc/2))))
                  (when (and (>= si 0) (< si rows) (>= sj 0) (< sj cols))
                    (set! sum (+ sum (* (matrix-ref image si sj)
                                        (matrix-ref kernel ki kj))))))))
            (matrix-set! out i j sum)))))))

; Sharpen kernel
(define sharpen (matrix 3 3 '(0 -1 0  -1 5 -1  0 -1 0)))

; 5×5 test image (a bright spot in the centre)
(define img (make-matrix 5 5))
(matrix-set! img 2 2 100.0)

(display (convolve2d img sharpen))
(newline)
```

### Tensor outer product and contraction

```scheme
;;; Build a stress tensor from two force vectors using the outer product,
;;; then contract on one index to get a traction vector.

; Force vectors (3D)
(define F (make-tensor '(3)))
(tensor-set! F 0 1.0)   ; Fx = 1
(tensor-set! F 1 0.0)   ; Fy = 0
(tensor-set! F 2 0.5)   ; Fz = 0.5

; Normal vector
(define N (make-tensor '(3)))
(tensor-set! N 0 0.0)
(tensor-set! N 1 1.0)
(tensor-set! N 2 0.0)

; Outer product: T[i,j] = F[i] * N[j]  (shape 3×3)
(define T (tensor-outer F N))
(display (tensor-shape T))   ; (3 3)
(newline)
(display T)
(newline)
; #<tensor 3x3 ((0 1 0) (0 0 0) (0 0.5 0))>

; Traction = T * N: sum over second index of T[i,j] * N[j]
; (manual contraction for illustration)
(define (contract-second T v)
  (let* ((n (car (tensor-shape T)))
         (m (cadr (tensor-shape T)))
         (result (make-tensor (list n))))
    (do ((i 0 (+ i 1))) ((= i n) result)
      (let ((sum 0.0))
        (do ((j 0 (+ j 1))) ((= j m))
          (set! sum (+ sum (* (tensor-ref T i j) (tensor-ref v j)))))
        (tensor-set! result i sum)))))

(define traction (contract-second T N))
(display traction)   ; #<tensor 3 (1 0 0.5)>
(newline)
```

### Neural-network weight matrix — forward pass

```scheme
;;; Single linear layer: y = W·x + b
;;; W is a (out × in) matrix, x is a column vector (in × 1), b is (out × 1).

(define (linear-forward W x b)
  (mat+ (mat* W x) b))

; 3-input → 2-output layer (random-ish weights for demo)
(define W (matrix 2 3 '(0.5 -0.3  0.8
                        -0.1  0.7  0.2)))
(define x (matrix 3 1 '(1.0 2.0 3.0)))
(define b (matrix 2 1 '(0.1 -0.1)))

(display (linear-forward W x b))
(newline)
; W·x = [0.5*1 + -0.3*2 + 0.8*3,  -0.1*1 + 0.7*2 + 0.2*3]ᵀ
;      = [2.3,  1.9]ᵀ
; + b  = [2.4,  1.8]ᵀ
; → #<matrix 2x1 [2.4 | 1.8]>
```

---

## Relationship to other numeric types

| Type | Description | Use |
|------|-------------|-----|
| `matrix` | 2D `double` array | Linear algebra, image processing, ML layers |
| `tensor` | N-D `double` array | Deep learning, physics, multi-index data |
| `multivector` | Clifford algebra Cl(p,q,r) | Geometric algebra, rotors, conformal geometry |
| `quaternion` | Hamilton quaternion | 3D rotation, animation |
| `complex` | Complex number (exact or inexact parts) | Signal processing, complex analysis |

Matrices and tensors are distinct from Clifford multivectors: a multivector encodes geometric objects (vectors, bivectors, rotors) with non-commutative blade multiplication, while a matrix or tensor is a plain indexed array suitable for numerical computation. A `(matrix->tensor m)` wraps a matrix as a 2D tensor; there is no direct path to/from multivectors.
