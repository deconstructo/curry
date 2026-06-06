;;; rpi_tests.scm — (curry rpi) module + Phase 16 controller synthesis tests.
;;;
;;; These tests run on any platform (no hardware required).
;;; They cover:
;;;   § 1  API surface: rpi module exports all expected symbols (Linux only)
;;;   § 2  YUYV frame processing utilities
;;;   § 3  NMEA sentence parsing helper
;;;   § 4  Phase 16 controller synthesis (matrix lib, LQR, linearise)
;;;
;;; The rpi hardware API (gpio-open, camera-open, etc.) is tested only on Linux
;;; via the smoke tests in docs/reference/module-rpi.md.

(import (scheme base))
(import (scheme write))
(import (scheme inexact))
(import (curry sicm))

;;; ── Test harness ────────────────────────────────────────────────────────

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label got expected)
     (if (equal? got expected)
         (begin (display "PASS: ") (display label) (newline)
                (set! pass (+ pass 1)))
         (begin (display "FAIL: ") (display label)
                (display " — got ") (display got)
                (display "  expected ") (display expected) (newline)
                (set! fail (+ fail 1)))))))

(define-syntax check-num
  (syntax-rules ()
    ((_ label got expected tol)
     (let ((err (abs (- (inexact got) (inexact expected)))))
       (if (< err tol)
           (begin (display "PASS: ") (display label) (newline)
                  (set! pass (+ pass 1)))
           (begin (display "FAIL: ") (display label)
                  (display " — got ") (display (inexact got))
                  (display "  expected ") (display (inexact expected))
                  (display "  err=") (display err) (newline)
                  (set! fail (+ fail 1))))))))

;;; ════════════════════════════════════════════════════════════
;;; § 1  rpi module exports (Linux only — skip on macOS)
;;; ════════════════════════════════════════════════════════════

;;; We check whether the module is available by looking for /proc/version.
;;; On Linux the module is compiled in; on macOS it is not.
;;; open-input-file raises on a missing path — guard converts it to #f.
(define on-linux?
  (guard (exn (#t #f))
    (let ((f (open-input-file "/proc/version")))
      (close-input-port f)
      #t)))

(when on-linux?
  (import (curry rpi))
  ;; Just verify the symbols are bound — don't open hardware.
  (check "rpi gpio-open bound"      (procedure? gpio-open)       #t)
  (check "rpi gpio-read bound"      (procedure? gpio-read)       #t)
  (check "rpi gpio-write bound"     (procedure? gpio-write)      #t)
  (check "rpi gpio-close bound"     (procedure? gpio-close)      #t)
  (check "rpi gpio? bound"          (procedure? gpio?)            #t)
  (check "rpi gpio-wait-edge bound" (procedure? gpio-wait-edge)  #t)
  (check "rpi gpio-watch bound"     (procedure? gpio-watch)      #t)
  (check "rpi gpio-unwatch bound"   (procedure? gpio-unwatch)    #t)
  (check "rpi i2c-open bound"       (procedure? i2c-open)        #t)
  (check "rpi i2c-read bound"       (procedure? i2c-read)        #t)
  (check "rpi i2c-write bound"      (procedure? i2c-write)       #t)
  (check "rpi i2c-close bound"      (procedure? i2c-close)       #t)
  (check "rpi spi-open bound"       (procedure? spi-open)        #t)
  (check "rpi spi-transfer bound"   (procedure? spi-transfer)    #t)
  (check "rpi pwm-open bound"       (procedure? pwm-open)        #t)
  (check "rpi pwm-set! bound"       (procedure? pwm-set!)        #t)
  (check "rpi camera-open bound"    (procedure? camera-open)     #t)
  (check "rpi camera-capture bound" (procedure? camera-capture)  #t)
  (check "rpi camera-close bound"   (procedure? camera-close)    #t)
  (check "rpi camera-width bound"   (procedure? camera-width)    #t)
  (check "rpi camera-height bound"  (procedure? camera-height)   #t)
  (check "rpi camera-format bound"  (procedure? camera-format)   #t)
  (check "rpi uart-open bound"      (procedure? uart-open)       #t)
  (check "rpi uart-read bound"      (procedure? uart-read)       #t)
  (check "rpi uart-write bound"     (procedure? uart-write)      #t)
  (check "rpi uart-read-line bound" (procedure? uart-read-line)  #t)
  (check "rpi uart-available? bound"(procedure? uart-available?) #t)
  (check "rpi uart-close bound"     (procedure? uart-close)      #t)
  (check "rpi w1-devices bound"     (procedure? w1-devices)      #t)
  (check "rpi w1-temperature bound" (procedure? w1-temperature)  #t)
  (check "rpi w1-raw bound"         (procedure? w1-raw)          #t)
  (check "rpi watchdog-open bound"  (procedure? watchdog-open)   #t)
  (check "rpi watchdog-kick bound"  (procedure? watchdog-kick)   #t)
  (check "rpi watchdog-timeout bound" (procedure? watchdog-timeout) #t)
  (check "rpi watchdog-close bound" (procedure? watchdog-close)  #t)
  (check "rpi rpi-model bound"      (procedure? rpi-model)       #t)
  (check "rpi rpi-serial bound"     (procedure? rpi-serial)      #t)
  (check "rpi rpi-memory-mb bound"  (procedure? rpi-memory-mb)   #t)
  (check "rpi rpi-os-info bound"    (procedure? rpi-os-info)     #t)

  ;; Board info should return a string or #f (never error) on any Linux machine
  (let ((model (rpi-model)))
    (check "rpi-model returns string or #f"
           (or (string? model) (equal? model #f)) #t))
  (let ((mem (rpi-memory-mb)))
    (check "rpi-memory-mb returns non-negative integer"
           (and (exact-integer? mem) (>= mem 0)) #t)))

;;; ════════════════════════════════════════════════════════════
;;; § 2  YUYV frame processing
;;; ════════════════════════════════════════════════════════════

;;; Synthetic YUYV frame: 4×2 pixels, constant Y=128, U=128, V=128.
;;; Layout: [Y0 U Y1 V] per 2 pixels, repeated.
;;; Mean luminance should be 128.

(define test-yuyv
  ;; 4 pixels wide, 2 pixels tall = 8 pixels = 16 bytes
  ;; All Y=128
  (let ((bv (make-bytevector 16 128)))
    bv))

;;; Mean Y = average of bytes at indices 0,2,4,6,8,10,12,14 = all 128.
(define (yuyv-mean-lum bvec)
  (let* ((n   (bytevector-length bvec))
         (sum (let lp ((i 0) (s 0))
                (if (>= i n) s
                    (lp (+ i 2) (+ s (bytevector-u8-ref bvec i)))))))
    (/ (inexact sum) (inexact (/ n 2)))))

(check-num "yuyv-mean-lum uniform 128"
           (yuyv-mean-lum test-yuyv) 128.0 1e-9)

;;; Vary Y: first row Y=200, second row Y=100.
;;; bvec indices for Y: 0,2,4,6 (first row), 8,10,12,14 (second row).
(define test-yuyv2
  (let ((bv (make-bytevector 16 128)))
    (bytevector-u8-set! bv 0  200) (bytevector-u8-set! bv 2  200)
    (bytevector-u8-set! bv 4  200) (bytevector-u8-set! bv 6  200)
    (bytevector-u8-set! bv 8  100) (bytevector-u8-set! bv 10 100)
    (bytevector-u8-set! bv 12 100) (bytevector-u8-set! bv 14 100)
    bv))

(check-num "yuyv-mean-lum mixed rows"
           (yuyv-mean-lum test-yuyv2) 150.0 1e-9)

;;; Motion score: identical frames → score 0; max-diff frames → score 255.
(define (fake-region-means bvec)
  ;; Simplified: return 16-element vector of mean Y in each quarter
  (let* ((n   (bytevector-length bvec))
         (q   (quotient n 4))
         (means (make-vector 16 0.0)))
    (let lp ((i 0))
      (when (< i 16)
        (let* ((start (* i q))
               (end   (min n (+ start q)))
               (sum   (let s ((j start) (acc 0))
                        (if (>= j end) acc
                            (s (+ j 2) (+ acc (bytevector-u8-ref bvec j))))))
               (cnt   (quotient (- end start) 2)))
          (vector-set! means i (if (= cnt 0) 0.0 (/ (inexact sum) (inexact cnt))))
          (lp (+ i 1)))))
    means))

(define (motion-score-test m1 m2)
  (/ (let lp ((i 0) (s 0.0))
       (if (= i 16) s
           (lp (+ i 1) (+ s (abs (- (vector-ref m1 i) (vector-ref m2 i)))))))
     16.0))

;;; Identical frames → score 0
(let* ((m (fake-region-means test-yuyv)))
  (check-num "motion score identical frames"
             (motion-score-test m m) 0.0 1e-9))

;;; Different frames → score > 0
(let* ((m1 (fake-region-means test-yuyv))
       (m2 (fake-region-means test-yuyv2)))
  (check "motion score different frames > 0"
         (> (motion-score-test m1 m2) 0.0) #t))

;;; ════════════════════════════════════════════════════════════
;;; § 3  NMEA sentence helpers
;;; ════════════════════════════════════════════════════════════

;;; Parse a GGA NMEA sentence for latitude and longitude.
;;; Example: "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"
(define (nmea-split-commas line)
  (let lp ((chars (string->list line)) (current '()) (fields '()))
    (cond
      ((null? chars)
       (reverse (cons (list->string (reverse current)) fields)))
      ((char=? (car chars) #\,)
       (lp (cdr chars) '() (cons (list->string (reverse current)) fields)))
      (else
       (lp (cdr chars) (cons (car chars) current) fields)))))

(define test-gga
  "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47")

(let* ((fields (nmea-split-commas test-gga))
       (msg-type (car fields)))
  (check "NMEA sentence type" msg-type "$GPGGA")
  (check "NMEA field count >= 10" (>= (length fields) 10) #t))

;;; Parse DDMM.mmm → decimal degrees
(define (nmea-parse-lat dms dir)
  (let* ((raw (string->number dms))
         (deg (exact (floor (/ raw 100)))
              )
         (min (- raw (* deg 100.0)))
         (dd  (+ deg (/ min 60.0))))
    (if (string=? dir "S") (- dd) dd)))

(let* ((fields (nmea-split-commas test-gga))
       (lat-str (list-ref fields 2))
       (lat-dir (list-ref fields 3))
       (lat     (nmea-parse-lat lat-str lat-dir)))
  (check-num "NMEA latitude" lat 48.1173 1e-3))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Phase 16 — controller synthesis
;;; ════════════════════════════════════════════════════════════

;;; ── Matrix library ────────────────────────────────────────────────────

;;; Kronecker product: I₂ ⊗ [[a,b],[c,d]] = block-diag(A,A)
(let* ((I2  (lqr-mat-eye 2))
       (A   '((1.0 2.0) (3.0 4.0)))
       (K   (lqr-mat-kron I2 A)))
  ;; K should be 4×4 block diagonal: [[A,0],[0,A]]
  (check-num "kron I₂⊗A [0,0]=1" (lqr-mat-ref K 0 0) 1.0 1e-10)
  (check-num "kron I₂⊗A [0,2]=0" (lqr-mat-ref K 0 2) 0.0 1e-10)
  (check-num "kron I₂⊗A [2,2]=1" (lqr-mat-ref K 2 2) 1.0 1e-10)
  (check-num "kron I₂⊗A [2,0]=0" (lqr-mat-ref K 2 0) 0.0 1e-10))

;;; Gauss-Jordan inverse: A·A⁻¹ = I for 3×3 matrix.
(let* ((A   '((2.0 1.0 0.0)
              (1.0 3.0 1.0)
              (0.0 1.0 2.0)))
       (Ai  (lqr-mat-inv A))
       (I   (lqr-mat-mul A Ai)))
  (check-num "3×3 inverse A*A⁻¹[0,0]" (lqr-mat-ref I 0 0) 1.0 1e-9)
  (check-num "3×3 inverse A*A⁻¹[1,1]" (lqr-mat-ref I 1 1) 1.0 1e-9)
  (check-num "3×3 inverse A*A⁻¹[2,2]" (lqr-mat-ref I 2 2) 1.0 1e-9)
  (check-num "3×3 inverse A*A⁻¹[0,1]" (lqr-mat-ref I 0 1) 0.0 1e-9))

;;; Lyapunov: for A=-2I, M=I the solution is X=I/4 since (-2+(-2))=−4.
(let* ((A  '((-2.0 0.0 0.0)
             (0.0 -2.0 0.0)
             (0.0  0.0 -2.0)))
       (M  '((1.0 0.0 0.0)
             (0.0 1.0 0.0)
             (0.0 0.0 1.0)))
       (X  (lyapunov-solve A M)))
  (check-num "lyapunov 3×3 diagonal X[0,0]=0.25" (lqr-mat-ref X 0 0) 0.25 1e-8)
  (check-num "lyapunov 3×3 diagonal X[1,1]=0.25" (lqr-mat-ref X 1 1) 0.25 1e-8)
  (check-num "lyapunov 3×3 diagonal X[0,1]=0"    (lqr-mat-ref X 0 1) 0.0  1e-8))

;;; CARE residual check: for the solved K, verify A'P + PA - PBR⁻¹B'P + Q ≈ 0.
(let* ((A  '((0.0 1.0) (0.0 0.0)))   ; double integrator
       (B  '((0.0) (1.0)))
       (Q  '((1.0 0.0) (0.0 1.0)))
       (R  '((1.0)))
       (K0 '((1.0 2.0)))
       (K  (lqr-continuous A B Q R 200 1e-8 K0))
       ;; Reconstruct P from K = R⁻¹B'P → P = R·K (B^+)
       ;; Better: verify via CARE residual directly
       ;; Compute P from Lyapunov at converged K
       (Rinv  (lqr-mat-inv R))
       (Bt    (lqr-mat-transpose B))
       (Acl   (lqr-mat-sub A (lqr-mat-mul B K)))
       (KtRK  (lqr-mat-mul (lqr-mat-transpose K) (lqr-mat-mul R K)))
       (P     (lyapunov-solve Acl (lqr-mat-add Q KtRK)))
       ;; CARE residual: A'P + PA - PBR⁻¹B'P + Q
       (At    (lqr-mat-transpose A))
       (PBRinvBt (lqr-mat-mul P (lqr-mat-mul B (lqr-mat-mul Rinv Bt))))
       (residual (lqr-mat-add
                   (lqr-mat-sub (lqr-mat-add (lqr-mat-mul At P)
                                             (lqr-mat-mul P A))
                                (lqr-mat-mul PBRinvBt P))
                   Q)))
  (check-num "CARE residual [0,0]≈0" (lqr-mat-ref residual 0 0) 0.0 1e-6)
  (check-num "CARE residual [1,1]≈0" (lqr-mat-ref residual 1 1) 0.0 1e-6))

;;; Discrete LQR: gain K stabilises A (spectral radius of A-BK < 1).
(let* ((A  '((1.0 0.1) (0.0 1.0)))
       (B  '((0.0) (0.1)))
       (Q  '((1.0 0.0) (0.0 1.0)))
       (R  '((1.0)))
       (K  (lqr A B Q R))
       (Acl (lqr-mat-sub A (lqr-mat-mul B K)))
       ;; 2×2 spectral radius ≤ max |eigenvalue| ≤ (|tr|+√(tr²−4det))/2
       ;; Necessary stability: |det(Acl)| < 1
       (a  (lqr-mat-ref Acl 0 0)) (b (lqr-mat-ref Acl 0 1))
       (c  (lqr-mat-ref Acl 1 0)) (d (lqr-mat-ref Acl 1 1))
       (det (- (* a d) (* b c))))
  (check "discrete LQR closed-loop det < 1" (< (abs det) 1.0) #t))

;;; Linearise: harmonic oscillator at (q=0,p=0) gives A=[[0,1],[-ω²,0]].
(let* ((omega 3.0)
       (H  (lambda (s)
              (+ (* 0.5 (momentum s) (momentum s))
                 (* 0.5 omega omega (coordinate s) (coordinate s)))))
       (s0 (up 0.0 0.0 0.0))
       (A  (linearise H s0)))
  (check-num "linearise HO A[0,1]=1"      (lqr-mat-ref A 0 1)  1.0 1e-3)
  (check-num "linearise HO A[1,0]=-ω²"   (lqr-mat-ref A 1 0) (- (* omega omega)) 1e-2))

;;; Controller object: step!, reset!, current-state, compute-u all present.
(let* ((H   (lambda (s) (* 0.5 (momentum s) (momentum s))))
       (K   '((1.0 1.0)))
       (ctrl (make-controller H K (up 0.0 0.0 0.0) 0.01)))
  (check "controller has step!"         (pair? (assq 'step!         ctrl)) #t)
  (check "controller has reset!"        (pair? (assq 'reset!        ctrl)) #t)
  (check "controller has current-state" (pair? (assq 'current-state ctrl)) #t)
  (check "controller has compute-u"     (pair? (assq 'compute-u     ctrl)) #t))

;;; Inverted pendulum end-to-end: pendulum must stabilise from 5°.
(let* ((g   9.81)
       (A-lin `((0.0 1.0) (,g 0.0)))
       (B-lin '((0.0) (1.0)))
       (Q   '((10.0 0.0) (0.0 1.0)))
       (R   '((0.5)))
       (K0  '((10.0 4.0)))
       (K   (lqr-continuous A-lin B-lin Q R 500 1e-9 K0))
       (H   (lambda (s)
              (- (* 0.5 (momentum s) (momentum s))
                 (* g (cos (coordinate s))))))
       (theta0 (* 5.0 (/ (acos -1.0) 180.0)))
       (s0  (up 0.0 theta0 0.0))
       (ctrl (make-controller H K (up 0.0 0.0 0.0) 0.01))
       (step! (cdr (assq 'step! ctrl))))
  ((cdr (assq 'reset! ctrl)) s0)
  (let loop ((i 0))
    (when (< i 500) (step!) (loop (+ i 1))))
  (let ((theta-final (coordinate ((cdr (assq 'current-state ctrl))))))
    (check-num "inverted pendulum stabilises to <1°"
               (abs theta-final) 0.0
               (* 1.0 (/ (acos -1.0) 180.0)))))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
