#!/usr/bin/env curry
;;; reaction_wheel.scm — LQR attitude stabilisation via reaction wheel
;;;
;;; A rigid body (spacecraft/robot) is stabilised in one axis by a
;;; reaction wheel driven by a DC motor.  The system is a two-DOF
;;; Lagrangian: body angle φ and wheel speed ψ̇.
;;;
;;; Equations of motion:
;;;   (I_b + I_w) φ̈ + I_w ψ̈ = 0         (free body + wheel)
;;;   I_w (φ̈ + ψ̈) = τ                    (wheel driven by motor torque τ)
;;; In state variables x = (φ, φ̇):
;;;   φ̈ = −τ / I_b
;;; So it reduces to a double integrator with gain −1/I_b:
;;;   A = [[0, 1], [0, 0]],   B = [[0], [−1/I_b]]
;;;
;;; For control, u = +τ, so B = [[0],[1/I_b]].
;;; This is identical to the inverted pendulum without gravity.

(import (curry sicm))
(import (scheme write))

;;; ── Physical parameters ─────────────────────────────────────────────────

(define I_b 0.05)   ; body moment of inertia  kg·m²
(define I_w 0.005)  ; wheel moment of inertia kg·m²

;;; ── System matrices ─────────────────────────────────────────────────────

(define A-rw '((0.0 1.0) (0.0 0.0)))                  ; integrator
(define B-rw `((0.0) (,(/ 1.0 I_b))))                  ; torque coupling

;;; Cost: penalise angle 50× more than rate; moderate control effort.
(define Q-rw '((50.0 0.0) (0.0 1.0)))
(define R-rw '((10.0)))

;;; For a pure integrator A is marginally stable — policy iteration from K=0
;;; requires A-BK to be Schur-stable.  Use a small initial K.
(define K0-rw `((,(/ 1.0 I_b) ,(* 2.0 (/ 1.0 I_b)))))

(display "Computing reaction-wheel LQR gain...") (newline)
(define K-rw (lqr-continuous A-rw B-rw Q-rw R-rw 500 1e-9 K0-rw))
(display "K = ") (display K-rw) (newline)

;;; ── Closed-loop simulation ──────────────────────────────────────────────

;;; Hamiltonian (no potential, just kinetic): H = p²/(2 I_b)
(define (H-rw s)
  (/ (* (momentum s) (momentum s)) (* 2.0 I_b)))

(define x-eq  (up 0.0 0.0 0.0))
(define dt    0.005)      ; 5 ms
(define t-end 4.0)
(define n-steps (exact (round (/ t-end dt))))

;;; Initial condition: 10° attitude error
(define phi0 (* 10.0 (/ (acos -1.0) 180.0)))
(define s0   (up 0.0 phi0 0.0))

(define ctrl (make-controller H-rw K-rw x-eq dt))
((cdr (assq 'reset! ctrl)) s0)

(display "Simulating reaction-wheel stabilisation...") (newline)

(let loop ((i 0))
  (when (< i n-steps) (step!) (loop (+ i 1))))

; Wait - need to use step! properly
((cdr (assq 'reset! ctrl)) s0)
(define step! (cdr (assq 'step! ctrl)))

(let loop ((i 0))
  (when (< i n-steps) (step!) (loop (+ i 1))))

(define final ((cdr (assq 'current-state ctrl))))
(define phi-final (coordinate final))
(display (string-append "  Initial angle: "
                        (number->string (inexact (* phi0 180.0 (/ 1.0 (acos -1.0)))))
                        "°")) (newline)
(display (string-append "  Final angle:   "
                        (number->string (inexact (* phi-final 180.0 (/ 1.0 (acos -1.0)))))
                        "°")) (newline)

(define converged? (< (abs phi-final) (* 1.0 (/ (acos -1.0) 180.0))))  ; < 1°
(display (if converged?
             "✓ Attitude error < 1° after 4 s"
             "✗ Attitude not converged — tune Q/R"))
(newline)

;;; ── Discrete-time variant ───────────────────────────────────────────────
;;;
;;; On a microcontroller or Pi, discrete-time LQR is often preferred:
;;;
;;; (define dt-d 0.01)
;;; (define A-d `((1.0 ,dt-d) (0.0 1.0)))           ; Euler discretisation
;;; (define B-d `((0.0) (,(* dt-d (/ 1.0 I_b)))))
;;; (define K-d (lqr A-d B-d Q-rw R-rw))
;;;
;;; ;; I2C motor driver (DRV8830 at address 0x64)
;;; ;; (import (curry rpi))
;;; ;; (define motor (i2c-open 1))
;;; ;; (define (set-torque! tau)
;;; ;;   (let* ((v    (min 1.0 (max -1.0 (* tau 0.1))))
;;; ;;          (vset (inexact (round (* (abs v) 63)))))
;;; ;;     (i2c-write motor #x64 0x00
;;; ;;       (make-bytevector 2 (if (> v 0) vset (bitwise-or vset #x40))))))
