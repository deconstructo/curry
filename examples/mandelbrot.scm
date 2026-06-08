;;; mandelbrot.scm — Hypercomplex Mandelbrot viewer (GPU edition)
;;; Version: 2.1
;;;
;;; Renders directly on the GPU via a GLSL fragment shader written in Scheme.
;;; The shader handles all three Cayley-Dickson algebras:
;;;   Complex    (2 real dims)  — the classic Mandelbrot set
;;;   Quaternion (4 real dims)  — 2-D slice through ℍ
;;;   Octonion   (8 real dims)  — 2-D slice through 𝕆
;;;
;;; Controls:
;;;   Left-drag          pan
;;;   Scroll wheel       zoom (centred on cursor); trackpad pinch works too
;;;   Double-click       zoom 2× to cursor
;;;   R                  reset view
;;;
;;; Run:  ./build/curry examples/mandelbrot.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Fragment shader ──────────────────────────────────────────────────────────
;;;
;;; u_resolution is auto-supplied by gl-shader-draw! (physical pixels, HiDPI-correct).
;;; All other uniforms are passed per-frame via the uniforms alist.

(define mandel-frag-src "
#version 330 core
out vec4 frag_color;

uniform vec2  u_resolution;   /* physical (W, H) — set automatically        */
uniform float u_dpr;          /* device pixel ratio — set automatically     */
uniform vec2  u_center;       /* (cx, cy) world-space                       */
uniform float u_zoom;         /* logical pixels per world unit              */
uniform int   u_max_iter;
uniform int   u_algebra;      /* 0=complex  1=quaternion  2=octonion        */
uniform float u_theta;        /* slice rotation θ                           */
uniform float u_phi;          /* slice rotation φ                           */

vec3 palette(float t) {
    float a = mod(t * 7.3, 512.0) / 512.0 * 6.28318530718;
    return vec3(0.5 + 0.5*sin(a),
                0.5 + 0.5*sin(a + 2.09439510239),
                0.5 + 0.5*sin(a + 4.18879020479));
}

/* Smooth escape-time colouring: n + 1 − log₂(log₂|z|)  (|z| > 2 at escape) */
float smooth_n(float i_f, float ns) {
    return i_f + 1.0 - log2(log2(sqrt(ns)));
}

float mandel_2d(vec2 c) {
    vec2 z = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float x2 = z.x*z.x, y2 = z.y*z.y;
        if (x2 + y2 > 4.0) return smooth_n(float(i), x2+y2);
        z = vec2(x2 - y2 + c.x, 2.0*z.x*z.y + c.y);
    }
    return -1.0;
}

/* Quaternion: (a+v)² = (a²−|v|²) + 2a·v */
float mandel_4d(vec4 c) {
    vec4 z = vec4(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = dot(z, z);
        if (ns > 4.0) return smooth_n(float(i), ns);
        float a = z.x;
        z = vec4(a*a - dot(z.yzw, z.yzw), 2.0*a*z.yzw) + c;
    }
    return -1.0;
}

/* Octonion: same Cayley-Dickson formula, packed as (z0, zl[1..4], zh[5..7], 0) */
float mandel_8d(float c0, vec4 c_lo, vec4 c_hi) {
    float z0 = 0.0; vec4 zl = vec4(0.0); vec4 zh = vec4(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = z0*z0 + dot(zl,zl) + dot(zh,zh);
        if (ns > 4.0) return smooth_n(float(i), ns);
        float nv2 = dot(zl,zl) + dot(zh,zh);
        float z0n = z0*z0 - nv2 + c0;
        zl = 2.0*z0*zl + c_lo;
        zh = 2.0*z0*zh + c_hi;
        z0 = z0n;
    }
    return -1.0;
}

void main() {
    /* u_zoom is in logical px/world-unit; scale to physical for gl_FragCoord */
    float pzoom = u_zoom * u_dpr;
    float wx = ( gl_FragCoord.x - u_resolution.x * 0.5) / pzoom;
    float wy = -(gl_FragCoord.y - u_resolution.y * 0.5) / pzoom;

    float t;
    if (u_algebra == 0) {
        t = mandel_2d(vec2(u_center.x + wx, u_center.y + wy));
    } else if (u_algebra == 1) {
        t = mandel_4d(vec4(wx*cos(u_theta) + u_center.x,
                           wy*cos(u_phi)   + u_center.y,
                           wx*sin(u_theta),
                           wy*sin(u_phi)));
    } else {
        t = mandel_8d(wx*cos(u_theta) + u_center.x,
                      vec4(wy*cos(u_phi) + u_center.y, wx*sin(u_theta), wy*sin(u_phi), 0.0),
                      vec4(0.0));
    }

    frag_color = (t < 0.0) ? vec4(0.0, 0.0, 0.0, 1.0)
                            : vec4(palette(t), 1.0);
}
")

;;; Compile once — lazy: actual GL compilation happens on first gl-shader-draw! call.
(define mandel-prog (make-gl-shader mandel-frag-src))

;;; ── View state ───────────────────────────────────────────────────────────────

(define *algebra*  'complex)
(define *max-iter* 150)
(define *cx*      -0.5)
(define *cy*       0.0)
(define *zoom*   200.0)   ; logical pixels per world unit
(define *theta*    0.0)
(define *phi*      0.0)
(define *W* 800)
(define *H* 600)

(define (algebra->int a)
  (case a ((complex) 0) ((quaternion) 1) (else 2)))

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f) (define *drag-y* #f)
(define *drag-cx* 0.0) (define *drag-cy* 0.0)

(define *canvas* #f)

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (gl-shader-draw! mandel-prog painter
    (list (cons "u_center"   (list *cx* *cy*))
          (cons "u_zoom"     *zoom*)                  ; logical px/world; shader scales by u_dpr
          (cons "u_max_iter" *max-iter*)
          (cons "u_algebra"  (algebra->int *algebra*))
          (cons "u_theta"    *theta*)
          (cons "u_phi"      *phi*)))
  ; HUD — drawn by QPainter on top after endNativePainting
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      (case *algebra*
        ((complex)    "C  2D")
        ((quaternion) "H  4D slice")
        (else         "O  8D slice"))
      "  iter=" (number->string *max-iter*)
      "  zoom×" (number->string (inexact->exact (round *zoom*))))))

;;; ── Input ────────────────────────────────────────────────────────────────────

(define (on-mouse-press! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x* x) (set! *drag-y* y)
    (set! *drag-cx* *cx*) (set! *drag-cy* *cy*)))

(define (on-mouse-release! x y btn)
  (when (equal? btn 'left) (set! *drag-x* #f)))

(define (on-mouse-move! x y)
  (when *drag-x*
    (set! *cx* (- *drag-cx* (/ (- x *drag-x*) *zoom*)))
    (set! *cy* (+ *drag-cy* (/ (- y *drag-y*) *zoom*)))
    (canvas-redraw! *canvas*)))

(define (reset-view!)
  (set! *cx* -0.5) (set! *cy* 0.0) (set! *zoom* 200.0)
  (canvas-redraw! *canvas*))

(define (zoom-step! factor)
  (set! *zoom* (* *zoom* factor))
  (canvas-redraw! *canvas*))

(define (zoom-to-cursor! factor sx sy)
  (let* ((wx (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
         (wy (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))
         (nz (* *zoom* factor)))
    (set! *cx* (- (+ *cx* wx) (/ (- (inexact sx) (/ (inexact *W*) 2.0)) nz)))
    (set! *cy* (+ (- *cy* wy) (/ (- (inexact sy) (/ (inexact *H*) 2.0)) nz)))
    (set! *zoom* nz)
    (canvas-redraw! *canvas*)))

;;; ── Window and UI ────────────────────────────────────────────────────────────

(define win     (make-window "Hypercomplex Mandelbrot" 1060 680))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

(set! *canvas* canvas)

(box-add! sidebar (make-label "Algebra"))
(box-add! sidebar
  (make-radio-group
    '("Complex  ℂ (d=2)" "Quaternion  ℍ (d=4)" "Octonion  𝕆 (d=8)")
    0
    (lambda (i)
      (set! *algebra* (list-ref '(complex quaternion octonion) i))
      (reset-view!))))

(box-add! sidebar (make-separator))

(box-add! sidebar (make-label "Max iterations"))
(box-add! sidebar
  (make-slider "Iterations" 20 400 10 150
    (lambda (v) (set! *max-iter* v) (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))

(box-add! sidebar (make-label "Slice plane  (ℍ / 𝕆)"))
(box-add! sidebar
  (make-slider "θ  e₀↔e₂" -100 100 1 0
    (lambda (v) (set! *theta* (* v 0.031416)) (canvas-redraw! *canvas*))))
(box-add! sidebar
  (make-slider "φ  e₁↔e₃" -100 100 1 0
    (lambda (v) (set! *phi* (* v 0.031416)) (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))
(box-add! sidebar (make-button "Reset view  (R)" reset-view!))

(canvas-on-draw! canvas
  (lambda (painter w h)
    (set! *W* w) (set! *H* h)
    (draw-frame painter w h)))

(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)        (on-mouse-press!   x y btn))
      ((equal? ev 'release)      (on-mouse-release! x y btn))
      ((or (equal? ev 'move)
           (equal? ev 'drag))    (on-mouse-move!    x y))
      ((equal? ev 'double-press) (when (equal? btn 'left)
                                   (zoom-to-cursor! 2.0 x y))))))

(canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (zoom-to-cursor! (expt 1.002 dy) x y)))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")                        (reset-view!))
      ((or (equal? key "=") (equal? key "+"))  (zoom-step! 1.5))
      ((equal? key "-")                        (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q")
           (equal? key "Escape"))              (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (unless (qt-gpu? canvas)
      (display "[mandelbrot] warning: OpenGL unavailable — GPU render will not work\n")
      (flush-output-port (current-output-port)))
    (canvas-redraw! canvas)))

(window-on-close! win (lambda () (quit-event-loop)))

(window-show! win)
(run-event-loop)
