;;; matrix-tensor-demo.scm — Demonstration of matrix and tensor support.
;;;
;;; Topics covered:
;;;   1. Matrix construction, access, arithmetic, display
;;;   2. Gaussian elimination (solve A·x = b)
;;;   3. Matrix statistics via mat-map and mat-fold
;;;   4. 2D convolution (sharpen kernel on a synthetic image)
;;;   5. Tensor construction, reshaping, outer product
;;;   6. Manual index contraction (stress-tensor example)
;;;   7. A simple neural-network linear layer forward pass
;;;   8. Matrix ↔ tensor round-trip
;;;
;;; Run: ./build/curry examples/matrix-tensor-demo.scm

(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ── helpers ───────────────────────────────────────────────────────────────

(define (section title)
  (newline)
  (display "══════════════════════════════════════════")
  (newline)
  (display title)
  (newline)
  (display "══════════════════════════════════════════")
  (newline))

(define (show label val)
  (display label)
  (display ": ")
  (display val)
  (newline))


;;; ── 1. Matrix basics ──────────────────────────────────────────────────────

(section "1. Matrix construction and access")

(define I3 (matrix-identity 3))
(show "3×3 identity" I3)

(define A (matrix 2 3 '(1 2 3 4 5 6)))
(show "2×3 from list" A)
(show "rows" (matrix-rows A))
(show "cols" (matrix-cols A))
(show "A[1,2]" (matrix-ref A 1 2))

(matrix-set! A 0 0 99.0)
(show "after set! A[0,0]=99" A)

(show "row 0" (mat-row A 0))
(show "col 1" (mat-col A 1))
(show "->list" (mat->list A))


;;; ── 2. Matrix arithmetic ──────────────────────────────────────────────────

(section "2. Matrix arithmetic")

(define M (matrix 2 2 '(1 2 3 4)))
(define N (matrix 2 2 '(5 6 7 8)))

(show "M"      M)
(show "N"      N)
(show "M + N"  (mat+ M N))
(show "M - N"  (mat- M N))
(show "M * N"  (mat* M N))           ; matrix product: [[19 22][43 50]]
(show "-M"     (mat- M))
(show "2.5 * M" (mat-scale M 2.5))
(show "Mᵀ"    (mat-transpose M))

(show "trace M"      (mat-trace M))        ; 1+4 = 5
(show "Frobenius M"  (mat-frobenius M))    ; sqrt(1+4+9+16) ≈ 5.477

; Chain product: M * N * M
(show "M * N * M" (mat* M N M))


;;; ── 3. Gaussian elimination: solve A·x = b ────────────────────────────────

(section "3. Gaussian elimination")

(define (solve-linear A b)
  ;; Solve A*x = b by Gaussian elimination with partial pivoting.
  ;; Returns a vector of solutions. Raises on singular A.
  (let* ((n   (matrix-rows A))
         (aug (make-matrix n (+ n 1))))
    ;; Build augmented matrix [A | b]
    (do ((i 0 (+ i 1))) ((= i n))
      (do ((j 0 (+ j 1))) ((= j n))
        (matrix-set! aug i j (matrix-ref A i j)))
      (matrix-set! aug i n (list-ref b i)))
    ;; Forward elimination with partial pivoting
    (do ((col 0 (+ col 1))) ((= col n))
      ;; Find pivot row: largest absolute value in this column
      (let ((pivot-row col))
        (do ((r (+ col 1) (+ r 1))) ((= r n))
          (when (> (abs (matrix-ref aug r col))
                   (abs (matrix-ref aug pivot-row col)))
            (set! pivot-row r)))
        (when (< (abs (matrix-ref aug pivot-row col)) 1e-12)
          (error "solve-linear: singular matrix"))
        ;; Swap rows col and pivot-row
        (when (not (= pivot-row col))
          (do ((j 0 (+ j 1))) ((= j (+ n 1)))
            (let ((tmp (matrix-ref aug col j)))
              (matrix-set! aug col j (matrix-ref aug pivot-row j))
              (matrix-set! aug pivot-row j tmp))))
        ;; Eliminate below
        (do ((row (+ col 1) (+ row 1))) ((= row n))
          (let ((factor (/ (matrix-ref aug row col)
                           (matrix-ref aug col col))))
            (do ((j col (+ j 1))) ((= j (+ n 1)))
              (matrix-set! aug row j
                (- (matrix-ref aug row j)
                   (* factor (matrix-ref aug col j)))))))))
    ;; Back substitution
    (let ((x (make-vector n 0.0)))
      (do ((i (- n 1) (- i 1))) ((< i 0) x)
        (let ((sum (matrix-ref aug i n)))
          (do ((j (+ i 1) (+ j 1))) ((= j n))
            (set! sum (- sum (* (matrix-ref aug i j) (vector-ref x j)))))
          (vector-set! x i (/ sum (matrix-ref aug i i))))))))

;; x + 2y = 5
;; 3x +  y = 10  →  x=3, y=1
(define sys-A (matrix 2 2 '(1 2 3 1)))
(define sys-b '(5.0 10.0))
(show "Solution [x y]" (solve-linear sys-A sys-b))

;; 3×3 system: 2x+y-z=8, -3x-y+2z=-11, -2x+y+2z=-3 → x=2, y=3, z=-1
(define sys-A3 (matrix 3 3 '(2 1 -1  -3 -1 2  -2 1 2)))
(define sys-b3 '(8.0 -11.0 -3.0))
(show "3×3 solution [x y z]" (solve-linear sys-A3 sys-b3))


;;; ── 4. Matrix statistics via mat-map and mat-fold ─────────────────────────

(section "4. mat-map and mat-fold")

(define D (matrix 3 3 '(1 4 9  16 25 36  49 64 81)))
(show "D" D)

;; Square roots of every element
(show "sqrt of each" (mat-map sqrt D))

;; Sum of all elements
(show "sum" (mat-fold + 0.0 D))

;; Max element
(show "max" (mat-fold max -inf.0 D))


;;; ── 5. 2D convolution ─────────────────────────────────────────────────────

(section "5. 2D convolution (sharpen)")

(define (convolve2d image kernel)
  ;; Apply kernel to image with zero-pad boundary.
  (let* ((rows (matrix-rows image))
         (cols (matrix-cols image))
         (kr   (matrix-rows kernel))
         (kc   (matrix-cols kernel))
         (kr/2 (quotient kr 2))
         (kc/2 (quotient kc 2))
         (out  (make-matrix rows cols)))
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
          (matrix-set! out i j sum))))))

;; Sharpen kernel:  0 -1  0
;;                 -1  5 -1
;;                  0 -1  0
(define sharpen (matrix 3 3 '(0 -1 0  -1 5 -1  0 -1 0)))

;; 5×5 image: bright spot at centre
(define img (make-matrix 5 5))
(matrix-set! img 2 2 100.0)
(show "image" img)
(show "sharpened" (convolve2d img sharpen))

;; Edge-detect kernel:  -1 -1 -1 / -1 8 -1 / -1 -1 -1
(define edge (matrix 3 3 '(-1 -1 -1  -1 8 -1  -1 -1 -1)))
(show "edge-detected" (convolve2d img edge))


;;; ── 6. Tensor basics ──────────────────────────────────────────────────────

(section "6. Tensor construction and access")

(define t3 (make-tensor '(2 3 4)))
(show "shape" (tensor-shape t3))
(show "ndim"  (tensor-ndim  t3))
(show "size"  (tensor-size  t3))

;; Set and retrieve a specific element
(tensor-set! t3 0 1 2 42.0)
(tensor-set! t3 1 2 3 -7.5)
(show "t3[0,1,2]" (tensor-ref t3 0 1 2))
(show "t3[1,2,3]" (tensor-ref t3 1 2 3))


;;; ── 7. Tensor arithmetic: outer product and contraction ───────────────────

(section "7. Outer product and contraction")

;; Force vector F and surface normal N (3D)
(define F (make-tensor '(3)))
(tensor-set! F 0 1.0)
(tensor-set! F 1 0.0)
(tensor-set! F 2 0.5)

(define Nv (make-tensor '(3)))
(tensor-set! Nv 0 0.0)
(tensor-set! Nv 1 1.0)
(tensor-set! Nv 2 0.0)

;; Outer product T[i,j] = F[i] * N[j]  →  shape (3 3)
(define T (tensor-outer F Nv))
(show "T = F⊗N, shape" (tensor-shape T))
(show "T" T)

;; Traction vector: t[i] = Σⱼ T[i,j] * N[j]
(define (contract-mv2 T v)
  ;; Contract T(i,j) with v(j) -> result(i).
  (let* ((n (car  (tensor-shape T)))
         (m (cadr (tensor-shape T)))
         (r (make-tensor (list n))))
    (do ((i 0 (+ i 1))) ((= i n) r)
      (let ((s 0.0))
        (do ((j 0 (+ j 1))) ((= j m))
          (set! s (+ s (* (tensor-ref T i j) (tensor-ref v j)))))
        (tensor-set! r i s)))))

(show "traction t = T·N" (contract-mv2 T Nv))


;;; ── 8. Reshape ────────────────────────────────────────────────────────────

(section "8. Tensor reshape")

(define flat (make-tensor '(12) 1.0))
(do ((i 0 (+ i 1))) ((= i 12))
  (tensor-set! flat i (exact->inexact (+ i 1))))
(show "1D (12)"    flat)
(show "2D (3×4)"   (tensor-reshape flat '(3 4)))
(show "2D (4×3)"   (tensor-reshape flat '(4 3)))
(show "3D (2×2×3)" (tensor-reshape flat '(2 2 3)))


;;; ── 9. Neural-network linear layer ───────────────────────────────────────

(section "9. Linear layer forward pass  y = W·x + b")

;; 3-input → 2-output layer
(define W (matrix 2 3 '( 0.5 -0.3  0.8
                         -0.1  0.7  0.2)))
(define x (matrix 3 1 '(1.0 2.0 3.0)))
(define b (matrix 2 1 '(0.1 -0.1)))

(define (linear-forward W x b)
  (mat+ (mat* W x) b))

(show "W"    W)
(show "x"    x)
(show "b"    b)
(show "y = W·x + b" (linear-forward W x b))
;; W·x = [0.5-0.6+2.4, -0.1+1.4+0.6]ᵀ = [2.3, 1.9]ᵀ
;; + b  = [2.4, 1.8]ᵀ

;; ReLU activation applied element-wise
(define (relu m)
  (mat-map (lambda (x) (if (< x 0.0) 0.0 x)) m))

(define W2 (matrix 3 2 '( 1.0  0.0
                           0.0  1.0
                          -1.0  0.5)))
(define b2 (matrix 3 1 '(0.0 0.0 0.0)))

(define y1 (linear-forward W x b))
(define y2 (relu (linear-forward W2 y1 b2)))
(show "Two-layer ReLU output" y2)


;;; ── 10. Matrix ↔ tensor round-trip ───────────────────────────────────────

(section "10. matrix->tensor and tensor->matrix")

(define mat (matrix 2 3 '(1 2 3 4 5 6)))
(define ten (matrix->tensor mat))
(show "matrix" mat)
(show "as tensor" ten)
(show "tensor shape" (tensor-shape ten))
(show "back to matrix" (tensor->matrix ten))
(show "round-trip equal?" (equal? (mat->list mat)
                                   (mat->list (tensor->matrix ten))))
