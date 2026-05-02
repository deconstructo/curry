;;; (curry qt6 stereo) — Stereoscopic rendering utilities for Curry/Qt6
;;;
;;; Provides a stereo-renderer record and a stereo-render! procedure that
;;; wraps any scene-drawing function and composites two eye views.
;;;
;;; Modes:
;;;   anaglyph  — red/cyan composite; requires passive red-cyan glasses.
;;;               Uses additive blending: left eye drawn in red channel,
;;;               right eye in cyan (G+B). Luminance-preserving via BT.601.
;;;   sbs       — side-by-side; left eye in left half, right in right half.
;;;               For phone VR holders or cross-eyed viewing.
;;;   none      — passthrough; draw-proc called once with zero eye offset.
;;;
;;; Usage:
;;;   (import (curry qt6))
;;;   (import (curry qt6 stereo))
;;;
;;;   (define sr (make-stereo-renderer 'anaglyph 0.065))
;;;
;;;   ;; Inside canvas draw callback:
;;;   (define (draw painter w h)
;;;     (gfx-clear! painter 0.0 0.0 0.0)
;;;     (stereo-render! painter sr w h
;;;       (lambda (p eye-offset color-fn)
;;;         ;; eye-offset: horizontal camera shift in world units
;;;         ;; color-fn: (lambda (r g b) → (values r' g' b')) channel filter
;;;         (let ((sx (+ (/ w 2.0) (* eye-offset px-per-unit))))
;;;           (call-with-values (lambda () (color-fn 0.8 0.6 0.2))
;;;             (lambda (r g b)
;;;               (gfx-set-color! p r g b 1.0)
;;;               (gfx-fill-circle! p sx (/ h 2.0) 10.0)))))))

(import (curry qt6))
(import (scheme base))

;;; ---- Stereo renderer record: #(mode ipd) ----

(define (make-stereo-renderer mode ipd)
  ;; mode : 'anaglyph | 'sbs | 'none
  ;; ipd  : inter-pupillary distance in world-space units (≈0.065 at human scale)
  (vector mode (exact->inexact ipd)))

(define (stereo-mode sr)           (vector-ref sr 0))
(define (stereo-ipd  sr)           (vector-ref sr 1))
(define (stereo-mode-set! sr mode) (vector-set! sr 0 mode))
(define (stereo-ipd-set!  sr ipd)  (vector-set! sr 1 (exact->inexact ipd)))

;;; ---- BT.601 perceptual luminance ----

(define (stereo-luma r g b)
  (+ (* 0.299 r) (* 0.587 g) (* 0.114 b)))

;;; ---- Passthrough colour filter (identity) ----

(define (stereo-color-id r g b) (values r g b))

;;; ---- Main render entry point ----
;;;
;;; draw-proc: (lambda (painter eye-offset color-fn) → void)
;;;   painter    : Qt6 painter handle (from canvas-on-draw! callback)
;;;   eye-offset : horizontal shift for this eye in world units
;;;               (negative = left eye, positive = right eye, 0 = mono)
;;;   color-fn   : (lambda (r g b) → (values r' g' b'))
;;;               apply to every colour before passing to gfx-set-color! etc.

(define (stereo-render! painter sr w h draw-proc)
  (let ((mode (stereo-mode sr))
        (half (/ (stereo-ipd sr) 2.0)))
    (case mode

      ;; --- Anaglyph: additive blend, left=red, right=cyan ----------------
      ((anaglyph)
       (gfx-set-blend! painter 'add)
       ;; Left eye — red channel only (luminance-preserving)
       (draw-proc painter (- half)
                  (lambda (r g b)
                    (let ((L (stereo-luma r g b)))
                      (values L 0.0 0.0))))
       ;; Right eye — cyan channel only
       (draw-proc painter half
                  (lambda (r g b)
                    (let ((L (stereo-luma r g b)))
                      (values 0.0 L L))))
       (gfx-set-blend! painter 'src))

      ;; --- Side-by-side: left half then right half -----------------------
      ((sbs)
       (let ((hw (/ w 2.0)))
         ;; Left eye in left half
         (gfx-save! painter)
         (gfx-translate! painter 0.0 0.0)
         (gfx-scale! painter 0.5 1.0)
         (draw-proc painter (- half) stereo-color-id)
         (gfx-restore! painter)
         ;; Right eye in right half
         (gfx-save! painter)
         (gfx-translate! painter hw 0.0)
         (gfx-scale! painter 0.5 1.0)
         (draw-proc painter half stereo-color-id)
         (gfx-restore! painter)))

      ;; --- None / passthrough --------------------------------------------
      (else
       (draw-proc painter 0.0 stereo-color-id)))))
