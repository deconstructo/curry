#!/usr/bin/env curry
;;; inverted_pendulum.scm — LQR stabilisation of an inverted pendulum
;;;
;;; Demonstrates the full pipeline:
;;;   Hamiltonian → linearise → lqr-continuous → make-controller → simulation
;;;
;;; On a Raspberry Pi with a PWM servo, swap the simulation section at the
;;; bottom for real hardware:  (import (curry rpi)) + (pwm-set! ...)
;;;
;;; Usage:
;;;   ./build/curry examples/inverted_pendulum.scm          — simulation mode
;;;   ./build/curry examples/inverted_pendulum.scm --plot   — with PLplot
;;;
;;; Physical model: point mass m on a rigid rod of length l.
;;;   H(θ, p) = p²/(2ml²) − mgl·cos(θ)
;;;   θ̈ = (g/l)·sin(θ) − u/(ml²)
;;; Linearised around upright equilibrium θ=0, p=0:
;;;   ẋ = [[0, 1/(ml²)], [mgl, 0]] · x + [[0], [1]] · u
;;; which simplifies for m=l=1 to:
;;;   A = [[0,1],[g,0]],  b = [[0],[1]]

(import (curry sicm))
(import (scheme inexact))
(import (scheme write))

;;; ── Physical parameters ─────────────────────────────────────────────────

(define g   9.81)     ; gravity m/s²
(define m   1.0)      ; pendulum mass kg
(define l   1.0)      ; rod length m

;;; ── Hamiltonian (nonlinear) ──────────────────────────────────────────────

(define (H-pendulum s)
  ;; H = p²/(2ml²) − mgl·cos(θ)
  (let ((theta (coordinate s))
        (p     (momentum s)))
    (- (/ (* p p) (* 2.0 m l l))
       (* m g l (cos theta)))))

;;; ── LQR design ──────────────────────────────────────────────────────────
;;; Linearised A at θ=0, p=0:
;;;   θ̇ = p/(ml²)  →  ∂/∂p = 1/(ml²), ∂/∂θ = 0
;;;   ṗ = mgl·sin(θ)  →  at θ=0: ∂/∂θ = mgl, ∂/∂p = 0
;;; So A = [[0, 1/(ml²)], [mgl, 0]]
;;; For m=l=1: A = [[0,1],[g,0]]

(define A-lin `((0.0 ,(/ 1.0 (* m l l)))
                (,(* m g l) 0.0)))

;;; Input enters as torque τ applied to joint: ṗ += τ/1 → B = [[0],[1]]
(define B-lin `((0.0) (1.0)))

;;; State-cost: penalise angle 10× more than velocity.
(define Q-lqr '((10.0 0.0) (0.0 1.0)))

;;; Control cost.
(define R-lqr '((0.5)))

;;; Initial stabilising gain (poles at −1, −3) for policy iteration.
;;; Required because A has positive eigenvalue +√g ≈ +3.13 (unstable).
(define K0-init '((10.0 4.0)))

(display "Computing LQR gain...") (newline)
(define K-lqr (lqr-continuous A-lin B-lin Q-lqr R-lqr 500 1e-9 K0-init))
(display "K = ") (display K-lqr) (newline)

;;; ── Closed-loop simulation ──────────────────────────────────────────────

(define x-eq  (up 0.0 0.0 0.0))   ; upright equilibrium (t=0, θ=0, p=0)
(define dt     0.01)                ; 10 ms time step
(define t-end  5.0)                 ; simulate 5 seconds
(define n-steps (exact (round (/ t-end dt))))

;;; Initial condition: 5° off vertical
(define theta0 (* 5.0 (/ (acos -1.0) 180.0)))
(define s0    (up 0.0 theta0 0.0))

(define ctrl (make-controller H-pendulum K-lqr x-eq dt))
(define step! (cdr (assq 'step! ctrl)))
(define current-state (cdr (assq 'current-state ctrl)))

;;; Reset to initial state (the controller starts at x-eq)
((cdr (assq 'reset! ctrl)) s0)

(display "Simulating inverted pendulum...") (newline)
(display (string-append "  Initial angle: "
                        (number->string (exact (round (* theta0 1000))) ) " mrad")) (newline)

;;; Collect trajectory
(define trajectory
  (let loop ((i 0) (acc (list s0)))
    (if (>= i n-steps)
        (reverse acc)
        (let ((result (step!)))
          (loop (+ i 1) (cons (car result) acc))))))

(define final (current-state))
(define theta-final (coordinate final))
(define energy-final (H-pendulum final))
(define energy-0     (H-pendulum s0))

(display (string-append "  Final angle:   "
                        (number->string (inexact (* theta-final 1000.0)))
                        " mrad")) (newline)
(display (string-append "  Energy initial: " (number->string (inexact energy-0)))) (newline)
(display (string-append "  Energy final:   " (number->string (inexact energy-final)))) (newline)

(define converged? (< (abs theta-final) (* 0.5 (/ (acos -1.0) 180.0))))  ; < 0.5°
(display (if converged?
             "✓ Pendulum stabilised within 0.5° after 5 s"
             "✗ Pendulum did NOT stabilise — check LQR gains"))
(newline)

;;; ── Raspberry Pi deployment notes ───────────────────────────────────────
;;;
;;; To run on real hardware, replace the simulation loop with:
;;;
;;; (import (curry rpi))
;;;
;;; ;; PWM output: hardware PWM chip 0, channel 0 (BCM pin 18)
;;; (define servo (pwm-open 0 0))
;;; (pwm-set! servo 20000000 1500000)  ; 20ms period, 1.5ms duty = neutral
;;; (pwm-enable! servo)
;;;
;;; ;; Read IMU angle via I2C (e.g. MPU-6050 at 0x68 on bus 1)
;;; (define imu (i2c-open 1))
;;;
;;; (define (read-angle)
;;;   ;; Read raw accel/gyro and compute angle — sensor-specific
;;;   ;; MPU-6050 ACCEL_XOUT_H = register 0x3B
;;;   (let ((data (i2c-read imu #x68 #x3B 14)))
;;;     (let* ((ax (+ (* (bytevector-u8-ref data 0) 256)
;;;                   (bytevector-u8-ref data 1)))
;;;            (ay (+ (* (bytevector-u8-ref data 2) 256)
;;;                   (bytevector-u8-ref data 3))))
;;;       (atan ax ay))))
;;;
;;; (let loop ()
;;;   (let* ((theta (read-angle))
;;;          (s     (up 0.0 theta 0.0))
;;;          (u     ((cdr (assq 'compute-u ctrl)) s))
;;;          (u0    (inexact (car (car u))))
;;;          ;; Map torque to servo pulse width: 1.5ms ± 0.5ms ≈ ±full torque
;;;          (pw    (+ 1500000 (exact (round (* u0 50000))))))
;;;     (pwm-set! servo 20000000 (max 1000000 (min 2000000 pw)))
;;;     (loop)))
