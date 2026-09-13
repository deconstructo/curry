;; babylonian-eclipse-atlas.scm
;;
;; A small atlas combining most of what makes curry curry, all in one
;; program instead of five separate feature demos:
;;
;;   - (curry babylonian-astronomy): the System A "zigzag" daylight-length
;;     function and the Saros eclipse-recurrence cycle -- two genuinely
;;     attested Babylonian mathematical-astronomy techniques (Neugebauer,
;;     ACT), not decorative theming.
;;   - the symbolic CAS: the zigzag's rising phase as an actual algebraic
;;     expression in a variable n, differentiated to recover its (real,
;;     constant) rate of change -- 12 UŠ/month, the genuine slope System A
;;     used.
;;   - sexagesimal/cuneiform number notation: the synodic month and the
;;     daylight extremes, printed the way a Babylonian scribe would have
;;     written them, alongside the modern decimal value.
;;   - the actor system: one actor per city, computing (toy, illustrative
;;     -- see the comment at its definition below) eclipse-visibility for
;;     a set of Saros-cycle predictions concurrently, reporting results
;;     back to the main thread over real message-passing.
;;   - (curry qt6): the daylight zigzag plotted as a curve and saved to a
;;     PNG snapshot.
;;
;; Run: ./build/curry --clear-cache examples/babylonian-eclipse-atlas.scm

(import (scheme base) (scheme write))
(import (curry babylonian-astronomy))
(import (curry sync))
(import (curry qt6))

;; -----------------------------------------------------------------------
;; 1. The synodic month, in three notations
;; -----------------------------------------------------------------------

(display "== The synodic month ==") (newline)
(display "  decimal:     ") (display (exact->inexact (synodic-month-length)))
(display " days") (newline)
(display "  neugebauer:  ") (display (number->string (synodic-month-length) 'neugebauer))
(newline)
(display "  cuneiform:   ") (display (number->string (synodic-month-length) 'cuneiform))
(newline)
(newline)

;; -----------------------------------------------------------------------
;; 2. The daylight zigzag, symbolically
;; -----------------------------------------------------------------------

(symbolic n)
(define max-us 216)     ; 3,36 UŠ -- summer solstice daylight (14h24m)
(define min-us 144)     ; 2,24 UŠ -- winter solstice daylight (9h36m)
(define half-period 6)  ; months from minimum to maximum

;; The rising-phase formula as an actual algebraic expression, not just a
;; numeric closure -- this is exactly what babylonian-zigzag computes
;; underneath for 0 <= n <= half-period.
(define daylight-expr
  (simplify (+ min-us (* n (/ (- max-us min-us) half-period)))))

(display "== Daylight length, rising phase, as a formula ==") (newline)
(display "  f(n)  = ") (display (sym->infix daylight-expr)) (display " UŠ") (newline)

;; Differentiating it symbolically recovers the zigzag's constant slope --
;; a real astronomical quantity: the rate of change of daylight length,
;; 12 UŠ/month in this System A scheme.
(define velocity-expr (simplify (∂ daylight-expr n)))
(display "  f'(n) = ") (display (sym->infix velocity-expr)) (display " UŠ/month") (newline)
(newline)

;; -----------------------------------------------------------------------
;; 3. Saros-cycle eclipse predictions
;; -----------------------------------------------------------------------

(display "== Saros cycle ==") (newline)
(display "  223 synodic months = ") (display (exact->inexact (saros-length-days)))
(display " days") (newline)

;; J2000.0 as an illustrative "known eclipse" JDN -- not a transcription of
;; any specific historical eclipse (same convention as
;; examples/mul-apin-akkadian.scm's own use of this constant).
(define known-eclipse-jdn 2451545)

;; babylonian-next-eclipse-window adds the (rational, sexagesimal-derived)
;; Saros length in days, so successive JDNs drift off integer day numbers
;; -- round each one back to a whole day before using it as an integer
;; elsewhere (e.g. modulo in the visibility toy-model below).
(define (saros-predictions start n)
  (let loop ((jdn start) (k 0) (acc '()))
    (if (= k n)
        (reverse acc)
        (let ((next (round (babylonian-next-eclipse-window jdn))))
          (loop next (+ k 1) (cons next acc))))))

(define eclipse-jdns (saros-predictions known-eclipse-jdn 4))
(display "  next 4 predicted eclipse JDNs: ") (display eclipse-jdns) (newline)
(newline)

;; -----------------------------------------------------------------------
;; 4. Per-city visibility, computed concurrently by actors
;; -----------------------------------------------------------------------
;;
;; Toy and illustrative only: real eclipse visibility depends on
;; geographic position relative to the Moon's shadow path, which this
;; does not model. This exists to demonstrate curry's actor
;; message-passing (spawn/send!/receive), not as an astronomical claim.

(define cities '("Babylon" "Uruk" "Nippur" "Larsa" "Ur"))

(define (visible? city jdn)
  (zero? (modulo (+ jdn (string-length city)) 4)))

(define collected #f)
(define done-sem (make-semaphore 0))

(define collector
  (spawn (lambda ()
           (let loop ((remaining (length cities)) (acc '()))
             (if (= remaining 0)
                 (begin (set! collected acc) (sem-post! done-sem))
                 (loop (- remaining 1) (cons (receive) acc)))))))

(for-each
  (lambda (city)
    (spawn (lambda ()
             (send! collector
                    (cons city (map (lambda (jdn) (visible? city jdn)) eclipse-jdns))))))
  cities)

(sem-wait! done-sem)
(display "== Per-city visibility, one Saros cycle each (toy model) ==") (newline)
(for-each (lambda (pair)
            (display "  ") (display (car pair)) (display ": ") (display (cdr pair)) (newline))
          collected)
(newline)

;; -----------------------------------------------------------------------
;; 5. Plot the zigzag curve and save a PNG snapshot
;; -----------------------------------------------------------------------

(define win (make-window "Babylonian daylight zigzag" 1600 800))

(window-on-realize! win
  (lambda ()
    (let ((canvas (window-canvas win)))
      (canvas-on-draw! canvas
        (lambda (painter w h)
          (gfx-clear! painter 0.07 0.07 0.1)
          (gfx-set-color! painter 0.9 0.85 0.6 1.0)
          (gfx-set-font! painter "Helvetica" 24)
          (gfx-draw-text! painter 20 30
            "Babylonian System A daylight-length zigzag (24 months)")

          (let* ((margin-left 60) (margin-right 20)
                 (margin-top 60) (margin-bottom 40)
                 (plot-w (- w margin-left margin-right))
                 (plot-h (- h margin-top margin-bottom))
                 (n-months 24)
                 (y-of (lambda (v)
                         (+ margin-top
                            (* plot-h (- 1 (/ (- v min-us) (- max-us min-us)))))))
                 (x-of (lambda (m) (+ margin-left (* plot-w (/ m n-months))))))

            ;; axes
            (gfx-set-pen-color! painter 0.5 0.5 0.5 1.0)
            (gfx-draw-line! painter margin-left margin-top margin-left (- h margin-bottom))
            (gfx-draw-line! painter margin-left (- h margin-bottom) (- w margin-right) (- h margin-bottom))

            ;; the curve itself
            (gfx-set-pen-color! painter 1.0 0.8 0.3 1.0)
            (gfx-set-pen-width! painter 3)
            (let loop ((m 0))
              (when (< m n-months)
                (gfx-draw-line! painter
                  (x-of m)       (y-of (babylonian-zigzag max-us min-us half-period m))
                  (x-of (+ m 1)) (y-of (babylonian-zigzag max-us min-us half-period (+ m 1))))
                (loop (+ m 1))))

            ;; mark solstices (min at 0/12/24, max at 6/18)
            (gfx-set-color! painter 0.3 0.9 0.5 1.0)
            (for-each
              (lambda (m)
                (gfx-fill-circle! painter (x-of m)
                  (y-of (babylonian-zigzag max-us min-us half-period m)) 6))
              '(0 6 12 18 24))

            (gfx-set-color! painter 0.9 0.85 0.6 1.0)
            (gfx-set-font! painter "Helvetica" 16)
            (gfx-draw-text! painter (x-of 0) (- (y-of min-us) 8)
              (string-append "min " (number->string min-us 'cuneiform) " UŠ"))
            (gfx-draw-text! painter (x-of 6) (- (y-of max-us) 8)
              (string-append "max " (number->string max-us 'cuneiform) " UŠ")))))
      (canvas-redraw! canvas)
      (let ((snap (make-timer 250
                    (lambda ()
                      (canvas-save-png! canvas "babylonian-eclipse-atlas.png")
                      (quit-event-loop)))))
        (timer-start! snap)))))

(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)

(display "Saved plot to babylonian-eclipse-atlas.png") (newline)
