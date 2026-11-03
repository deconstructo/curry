;;; (curry gpu-math) — vectors and 4x4 matrices for GPU work
;;;
;;; Pure Scheme, no Qt dependency.  Matrices are 16-element vectors in
;;; COLUMN-MAJOR order (element at row r, column c is at index c*4 + r),
;;; the layout GLSL's mat4 expects, the layout (curry qt6)'s
;;; gpu-clip-space-matrix returns, and the layout gpu-std140's 'mat4 field
;;; packs.  Vectors are plain lists: (x y z) and (x y z w).
;;;
;;; Projection matrices follow the OpenGL convention (right-handed eye
;;; space looking down -z, clip-space depth -1..1).  Metal, Vulkan and D3D
;;; use depth 0..1, and Vulkan's NDC has y pointing down; multiply by the
;;; backend's correction matrix first so one shader works everywhere:
;;;
;;;   (mat4* (gpu-clip-space-matrix frame)
;;;          (mat4-perspective (/ pi 3) (/ w h) 0.1 100.0))
;;;
;;; Usage:
;;;   (import (curry gpu-math))
;;;   (define mvp (mat4* proj (mat4* view model)))

(define-library (curry gpu-math)
  (import (scheme base) (scheme cxr) (scheme inexact))
  (export
    v3+ v3- v3* v3-dot v3-cross v3-length v3-normalize
    mat4-identity mat4-ref mat4* mat4*v4 mat4-transpose mat4-inverse
    mat4-translate mat4-scale
    mat4-rotate-x mat4-rotate-y mat4-rotate-z mat4-rotate
    mat4-perspective mat4-orthographic mat4-look-at
    deg->rad)
  (begin

    (define (deg->rad d) (* d (/ (* 4 (atan 1)) 180)))

    ;; ---- 3-vectors ----

    (define (v3+ a b) (map + a b))
    (define (v3- a b) (map - a b))
    (define (v3* a s) (map (lambda (x) (* x s)) a))
    (define (v3-dot a b) (apply + (map * a b)))
    (define (v3-cross a b)
      (let ((ax (car a)) (ay (cadr a)) (az (caddr a))
            (bx (car b)) (by (cadr b)) (bz (caddr b)))
        (list (- (* ay bz) (* az by))
              (- (* az bx) (* ax bz))
              (- (* ax by) (* ay bx)))))
    (define (v3-length a) (sqrt (v3-dot a a)))
    (define (v3-normalize a)
      (let ((l (v3-length a)))
        (if (zero? l)
            (error "v3-normalize: zero-length vector")
            (v3* a (/ 1.0 l)))))

    ;; ---- 4x4 matrices ----

    ;; Build a matrix from a procedure of (row col).
    (define (mat4-generate f)
      (let ((m (make-vector 16 0.0)))
        (do ((c 0 (+ c 1))) ((= c 4) m)
          (do ((r 0 (+ r 1))) ((= r 4))
            (vector-set! m (+ (* c 4) r) (f r c))))))

    (define (check-mat4 m who)
      (unless (and (vector? m) (= (vector-length m) 16))
        (error (string-append who ": not a 16-element matrix vector") m)))

    (define (mat4-ref m r c) (vector-ref m (+ (* c 4) r)))

    (define (mat4-identity)
      (mat4-generate (lambda (r c) (if (= r c) 1.0 0.0))))

    (define (mat4* a b)
      (check-mat4 a "mat4*") (check-mat4 b "mat4*")
      (mat4-generate
       (lambda (r c)
         (+ (* (mat4-ref a r 0) (mat4-ref b 0 c))
            (* (mat4-ref a r 1) (mat4-ref b 1 c))
            (* (mat4-ref a r 2) (mat4-ref b 2 c))
            (* (mat4-ref a r 3) (mat4-ref b 3 c))))))

    (define (mat4*v4 m v)
      (check-mat4 m "mat4*v4")
      (let ((x (car v)) (y (cadr v)) (z (caddr v)) (w (list-ref v 3)))
        (map (lambda (r)
               (+ (* (mat4-ref m r 0) x) (* (mat4-ref m r 1) y)
                  (* (mat4-ref m r 2) z) (* (mat4-ref m r 3) w)))
             '(0 1 2 3))))

    (define (mat4-transpose m)
      (check-mat4 m "mat4-transpose")
      (mat4-generate (lambda (r c) (mat4-ref m c r))))

    ;; General inverse by cofactor expansion; raises on a singular matrix.
    (define (mat4-inverse m)
      (check-mat4 m "mat4-inverse")
      (let* ((a (lambda (r c) (mat4-ref m r c)))
             ;; 3x3 minor with row i and column j removed
             (minor
              (lambda (i j)
                (let* ((rs (let loop ((k 3) (acc '()))
                             (if (< k 0) acc (loop (- k 1) (if (= k i) acc (cons k acc))))))
                       (cs (let loop ((k 3) (acc '()))
                             (if (< k 0) acc (loop (- k 1) (if (= k j) acc (cons k acc))))))
                       (r0 (car rs)) (r1 (cadr rs)) (r2 (caddr rs))
                       (c0 (car cs)) (c1 (cadr cs)) (c2 (caddr cs)))
                  (- (+ (* (a r0 c0) (- (* (a r1 c1) (a r2 c2)) (* (a r1 c2) (a r2 c1))))
                        (* (a r0 c2) (- (* (a r1 c0) (a r2 c1)) (* (a r1 c1) (a r2 c0)))))
                     (* (a r0 c1) (- (* (a r1 c0) (a r2 c2)) (* (a r1 c2) (a r2 c0))))))))
             (cof (lambda (i j) (* (if (even? (+ i j)) 1 -1) (minor i j))))
             (det (+ (* (a 0 0) (cof 0 0)) (* (a 0 1) (cof 0 1))
                     (* (a 0 2) (cof 0 2)) (* (a 0 3) (cof 0 3)))))
        (when (zero? det) (error "mat4-inverse: matrix is singular"))
        ;; inverse = adjugate / det, adjugate = transpose of cofactors
        (mat4-generate (lambda (r c) (/ (cof c r) det)))))

    (define (mat4-translate x y z)
      (mat4-generate
       (lambda (r c)
         (cond ((= r c) 1.0)
               ((= c 3) (case r ((0) x) ((1) y) ((2) z)))
               (else 0.0)))))

    (define (mat4-scale x y z)
      (mat4-generate
       (lambda (r c)
         (if (= r c) (case r ((0) x) ((1) y) ((2) z) (else 1.0)) 0.0))))

    ;; Rotation about a unit axis (x y z) by angle radians (right-handed).
    (define (mat4-rotate angle axis)
      (let* ((n (v3-normalize axis))
             (x (car n)) (y (cadr n)) (z (caddr n))
             (c (cos angle)) (s (sin angle)) (t (- 1 c))
             (rot (vector (+ (* t x x) c)       (+ (* t x y) (* s z)) (- (* t x z) (* s y)) 0.0
                          (- (* t x y) (* s z)) (+ (* t y y) c)       (+ (* t y z) (* s x)) 0.0
                          (+ (* t x z) (* s y)) (- (* t y z) (* s x)) (+ (* t z z) c)       0.0
                          0.0                   0.0                   0.0                   1.0)))
        rot))

    (define (mat4-rotate-x a) (mat4-rotate a '(1.0 0.0 0.0)))
    (define (mat4-rotate-y a) (mat4-rotate a '(0.0 1.0 0.0)))
    (define (mat4-rotate-z a) (mat4-rotate a '(0.0 0.0 1.0)))

    ;; OpenGL-style perspective: vertical field of view fovy (radians).
    (define (mat4-perspective fovy aspect near far)
      (unless (and (> near 0) (> far near) (> aspect 0))
        (error "mat4-perspective: need 0 < near < far and aspect > 0"))
      (let ((f (/ 1.0 (tan (/ fovy 2)))))
        (vector (/ f aspect) 0.0 0.0 0.0
                0.0 f 0.0 0.0
                0.0 0.0 (/ (+ far near) (- near far)) -1.0
                0.0 0.0 (/ (* 2 far near) (- near far)) 0.0)))

    (define (mat4-orthographic left right bottom top near far)
      (vector (/ 2.0 (- right left)) 0.0 0.0 0.0
              0.0 (/ 2.0 (- top bottom)) 0.0 0.0
              0.0 0.0 (/ -2.0 (- far near)) 0.0
              (- (/ (+ right left) (- right left)))
              (- (/ (+ top bottom) (- top bottom)))
              (- (/ (+ far near) (- far near)))
              1.0))

    ;; View matrix for a camera at eye looking at center.
    (define (mat4-look-at eye center up)
      (let* ((f (v3-normalize (v3- center eye)))
             (s (v3-normalize (v3-cross f up)))
             (u (v3-cross s f)))
        (vector (car s) (car u) (- (car f)) 0.0
                (cadr s) (cadr u) (- (cadr f)) 0.0
                (caddr s) (caddr u) (- (caddr f)) 0.0
                (- (v3-dot s eye)) (- (v3-dot u eye)) (v3-dot f eye) 1.0)))))
