;;; world.scm — maze geometry, generation, and queries
;;; No Qt6 dependency — pure Scheme. Compile with: curry -c world.scm

;;; ── Dimensions ───────────────────────────────────────────────────────────

(define DX 16)   ; cells in X
(define DZ 16)   ; cells in Z (depth)
(define DW  4)   ; W-slices (4th dimension layers)

;;; ── Passage bit flags ────────────────────────────────────────────────────

(define B+X  1)   ; passage to x+1
(define B-X  2)   ; passage to x-1
(define B+Z  4)   ; passage to z+1
(define B-Z  8)   ; passage to z-1
(define B+W 16)   ; passage to w+1
(define B-W 32)   ; passage to w-1

;;; Direction table: #(bit-fwd bit-back dx dz dw)
(define *ALL-DIRS*
  (list (vector B+X B-X  1  0  0)
        (vector B-X B+X -1  0  0)
        (vector B+Z B-Z  0  1  0)
        (vector B-Z B+Z  0 -1  0)
        (vector B+W B-W  0  0  1)
        (vector B-W B+W  0  0 -1)))

(define (dir-fwd  d) (vector-ref d 0))
(define (dir-bk   d) (vector-ref d 1))
(define (dir-dx   d) (vector-ref d 2))
(define (dir-dz   d) (vector-ref d 3))
(define (dir-dw   d) (vector-ref d 4))

;;; ── Cell storage ─────────────────────────────────────────────────────────

(define *cells* (make-vector (* DX DZ DW) 0))

;;; ── Bounds check — two separate 2-arg < (Curry VM workaround for 3-arg <) ──

(define (in-bounds? ix iz iw)
  (and (< -1 ix) (< ix DX)
       (< -1 iz) (< iz DZ)
       (< -1 iw) (< iw DW)))

(define (cidx ix iz iw) (+ (* iw (* DX DZ)) (* iz DX) ix))

(define (passage? ix iz iw bit)
  (and (in-bounds? ix iz iw)
       (not (zero? (bitwise-and (vector-ref *cells* (cidx ix iz iw)) bit)))))

(define (open-bit! ix iz iw bit)
  (let ((i (cidx ix iz iw)))
    (vector-set! *cells* i (bitwise-or (vector-ref *cells* i) bit))))

(define (open-passage! ax az aw fwd bk bx bz bw)
  (open-bit! ax az aw fwd)
  (open-bit! bx bz bw bk))

;;; ── LCG random (seeded from clock) ──────────────────────────────────────

(define *lcg* (inexact->exact (floor (current-second))))

(define (lcg-next!)
  (set! *lcg* (remainder (+ (* *lcg* 1664525) 1013904223) 4294967296))
  *lcg*)

(define (rand-int n)  (abs (remainder (lcg-next!) n)))
(define (rand-frac)   (/ (lcg-next!) 4294967296.0))

;;; Fisher-Yates shuffle — returns a fresh list in random order
(define (shuffle lst)
  (let* ((v (list->vector lst))
         (n (vector-length v)))
    (do ((i (- n 1) (- i 1)))
        ((= i 0) (vector->list v))
      (let* ((j (rand-int (+ i 1)))
             (t (vector-ref v i)))
        (vector-set! v i (vector-ref v j))
        (vector-set! v j t)))))

;;; ── Maze generation ──────────────────────────────────────────────────────

(define (generate-maze!)
  ;; 1. DFS spanning tree (iterative, avoids deep recursion)
  (let ((visited (make-vector (* DX DZ DW) #f))
        (stack   (list (list 1 1 0))))           ; start inside border
    (vector-set! visited (cidx 1 1 0) #t)
    (let loop ()
      (when (pair? stack)
        (let* ((cur (car stack))
               (ix  (car cur)) (iz (cadr cur)) (iw (caddr cur))
               (dirs (shuffle *ALL-DIRS*))
               (moved #f))
          (let try ((ds dirs))
            (when (and (pair? ds) (not moved))
              (let* ((d  (car ds))
                     (nx (+ ix (dir-dx d)))
                     (nz (+ iz (dir-dz d)))
                     (nw (+ iw (dir-dw d))))
                (if (and (in-bounds? nx nz nw)
                         (not (vector-ref visited (cidx nx nz nw))))
                    (begin
                      (vector-set! visited (cidx nx nz nw) #t)
                      (open-passage! ix iz iw (dir-fwd d) (dir-bk d) nx nz nw)
                      (set! stack (cons (list nx nz nw) stack))
                      (set! moved #t))
                    (try (cdr ds))))))
          (when (not moved) (set! stack (cdr stack)))
          (loop)))))

  ;; 2. Extra XZ passages (~28% of remaining walls) for open-world feel
  (do ((ix 0 (+ ix 1))) ((= ix DX))
    (do ((iz 0 (+ iz 1))) ((= iz DZ))
      (do ((iw 0 (+ iw 1))) ((= iw DW))
        (when (and (< ix (- DX 1)) (< (rand-frac) 0.28)
                   (not (passage? ix iz iw B+X)))
          (open-passage! ix iz iw B+X B-X (+ ix 1) iz iw))
        (when (and (< iz (- DZ 1)) (< (rand-frac) 0.28)
                   (not (passage? ix iz iw B+Z)))
          (open-passage! ix iz iw B+Z B-Z ix (+ iz 1) iw)))))

  ;; 3. W-passages — some cells connect to adjacent slices
  (do ((ix 1 (+ ix 1))) ((>= ix (- DX 1)))
    (do ((iz 1 (+ iz 1))) ((>= iz (- DZ 1)))
      (do ((iw 0 (+ iw 1))) ((= iw (- DW 1)))
        (when (< (rand-frac) 0.22)
          (open-passage! ix iz iw B+W B-W ix iz (+ iw 1))))))

  ;; 4. 2×2 open rooms — 6 per W-slice for clearings
  (do ((iw 0 (+ iw 1))) ((= iw DW))
    (do ((k 0 (+ k 1))) ((= k 6))
      (let ((rx (+ 2 (rand-int (- DX 4))))
            (rz (+ 2 (rand-int (- DZ 4)))))
        (open-passage! rx rz iw B+X B-X (+ rx 1) rz iw)
        (open-passage! rx rz iw B+Z B-Z rx (+ rz 1) iw)
        (open-passage! (+ rx 1) rz iw B+Z B-Z (+ rx 1) (+ rz 1) iw)
        (open-passage! rx (+ rz 1) iw B+X B-X (+ rx 1) (+ rz 1) iw)))))

;;; ── Texture upload helper ─────────────────────────────────────────────────
;;; Returns a bytevector for make-gl-texture: DX × (DZ*DW), R8

(define (maze-bytevec)
  (let* ((n  (* DX DZ DW))
         (bv (make-bytevector n 0)))
    (do ((i 0 (+ i 1))) ((= i n))
      (bytevector-u8-set! bv i (vector-ref *cells* i)))
    bv))
