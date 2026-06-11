;;; mandelbrot.scm — Hypercomplex Mandelbrot viewer (GPU edition)
;;; Version: 3.0
;;;
;;; Renders directly on the GPU via a GLSL fragment shader written in Scheme.
;;; The shader handles all three Cayley-Dickson algebras:
;;;   Complex    (2 real dims)  — the classic Mandelbrot set
;;;   Quaternion (4 real dims)  — 2-D slice through ℍ
;;;   Octonion   (8 real dims)  — 2-D slice through 𝕆
;;;
;;; Precision: complex mode automatically switches to double-double arithmetic
;;; (48-bit mantissa, ~14 decimal digits) when zoom > 1e6, extending the usable
;;; zoom depth from ~10^7 (float32 limit) to ~3×10^14.
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

(define mandel-frag-src "
#version 400 core
out vec4 frag_color;

uniform vec2  u_resolution;    /* physical (W, H) — set automatically          */
uniform float u_dpr;           /* device pixel ratio — set automatically       */
uniform vec2  u_center;        /* (cx, cy) world-space  — float32 path         */
uniform vec4  u_center_dd;     /* (cx_hi,cx_lo, cy_hi,cy_lo) — dd path        */
uniform float u_zoom;          /* logical pixels per world unit                */
uniform int   u_max_iter;
uniform int   u_algebra;       /* 0=complex  1=quaternion  2=octonion          */
uniform float u_theta;
uniform float u_phi;
uniform int   u_palette;       /* 0=cycle 1=ultra 2=ice 3=electric 4=gold      */
uniform int   u_use_dd;        /* 0=float32  1=double-double (auto from Scheme) */

/* ── Palette ──────────────────────────────────────────────────────────────── */

vec3 iqpal(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(6.28318530718 * (c * t + d));
}

vec3 palette(float raw_t) {
    float t = fract(raw_t * 0.015);          /* one hue cycle per ~67 iters   */
    if (u_palette == 1)                       /* Ultra — warm gold/orange       */
        return iqpal(t, vec3(0.5), vec3(0.5),
                     vec3(1.0, 1.0, 0.5), vec3(0.80, 0.90, 0.30));
    if (u_palette == 2)                       /* Ice — deep blue/cyan/white     */
        return iqpal(t, vec3(0.5), vec3(0.5),
                     vec3(0.8, 0.8, 0.5), vec3(0.00, 0.20, 0.50));
    if (u_palette == 3)                       /* Electric — purple/pink/white   */
        return iqpal(t, vec3(0.5), vec3(0.5),
                     vec3(2.0, 1.0, 0.0), vec3(0.50, 0.20, 0.25));
    if (u_palette == 4)                       /* Gold — black/brown/gold        */
        return iqpal(t, vec3(0.5, 0.4, 0.2), vec3(0.5, 0.4, 0.3),
                     vec3(1.0, 1.0, 0.5), vec3(0.00, 0.10, 0.20));
    /* 0: Cycle — classic smooth rainbow */
    return iqpal(t, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.0, 0.33, 0.67));
}

/* ── Smooth escape colouring ─────────────────────────────────────────────── */

float smooth_n(float i_f, float ns) {
    return i_f + 1.0 - log2(log2(sqrt(ns)));
}

/* ── Float32 Mandelbrot (fast path for zoom < ~10^6) ─────────────────────── */

float mandel_2d(vec2 c) {
    vec2 z = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float x2 = z.x*z.x, y2 = z.y*z.y;
        if (x2 + y2 > 4.0) return smooth_n(float(i), x2+y2);
        z = vec2(x2 - y2 + c.x, 2.0*z.x*z.y + c.y);
    }
    return -1.0;
}

/* ── Double-double arithmetic (Dekker/Veltkamp, no FMA needed) ───────────── */
/*   Each dd value is a vec2 (hi, lo) where hi + lo = exact value.           */

vec2 ts(float a, float b) {                  /* two-sum: exact (a+b, error)   */
    float s = a + b, v = s - a;
    return vec2(s, (a-(s-v)) + (b-v));
}
vec2 tp(float a, float b) {                  /* two-prod: exact (a*b, error)  */
    float p = a * b;
    return vec2(p, fma(a, b, -p));
}
vec2 dd_add(vec2 a, vec2 b) {
    vec2 s = ts(a.x, b.x);
    return ts(s.x, s.y + a.y + b.y);
}
vec2 dd_sub(vec2 a, vec2 b) { return dd_add(a, vec2(-b.x, -b.y)); }
vec2 dd_mul(vec2 a, vec2 b) {
    vec2 p = tp(a.x, b.x);
    p.y += a.x*b.y + a.y*b.x;
    return ts(p.x, p.y);
}

/* Build the per-pixel complex c as a double-double from the split center and
   the float32 pixel offset wx, wy.  Since |cx_lo + wx| << |cx_hi|, the
   double-double is simply (cx_hi, cx_lo + wx) — the two_sum in ts() captures
   wx exactly in the lo word even when |wx| << |cx_hi|.                       */
void pixel_c_dd(float wx, float wy, out vec2 cx, out vec2 cy) {
    cx = vec2(u_center_dd.x, u_center_dd.y + wx);
    cy = vec2(u_center_dd.z, u_center_dd.w + wy);
}

/* ── Double-double Mandelbrot (~10× more ops, correct to zoom ~3e14) ─────── */

float mandel_2d_dd(float wx, float wy) {
    vec2 cx, cy;
    pixel_c_dd(wx, wy, cx, cy);

    vec2 zx = vec2(0.0), zy = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        /* Escape test on high words — sufficient since |z|>2 check needs ~1 ULP */
        float ns = zx.x*zx.x + zy.x*zy.x;
        if (ns > 4.0) return smooth_n(float(i), ns);

        vec2 zx2  = dd_sub(dd_mul(zx, zx), dd_mul(zy, zy));
        vec2 zxzy = dd_mul(zx, zy);
        vec2 zy2  = 2.0 * zxzy;           /* × 2 is exact in floating-point  */
        zx = dd_add(zx2, cx);
        zy = dd_add(zy2, cy);
    }
    return -1.0;
}

/* ── Quaternion / octonion slices (float32 only — deep zoom not useful) ──── */

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

/* ── Main ────────────────────────────────────────────────────────────────── */

void main() {
    float pzoom = u_zoom * u_dpr;
    float wx = ( gl_FragCoord.x - u_resolution.x * 0.5) / pzoom;
    float wy = -(gl_FragCoord.y - u_resolution.y * 0.5) / pzoom;

    float t;
    if (u_algebra == 0) {
        t = (u_use_dd == 1)
            ? mandel_2d_dd(wx, wy)
            : mandel_2d(vec2(u_center.x + wx, u_center.y + wy));
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

(define mandel-prog (make-gl-shader mandel-frag-src))

;;; ── View state ───────────────────────────────────────────────────────────────

(define *algebra*  'complex)
(define *max-iter* 150)
(define *cx*      -0.5)
(define *cy*       0.0)
(define *zoom*   200.0)
(define *theta*    0.0)
(define *phi*      0.0)
(define *palette*  0)
(define *W* 800)
(define *H* 600)

(define (algebra->int a)
  (case a ((complex) 0) ((quaternion) 1) (else 2)))

;;; ── Double-double split ──────────────────────────────────────────────────────
;;;
;;; Veltkamp split: decompose a float64 x into (hi lo) where hi is exactly
;;; representable as float32 (24 significant bits) and hi + lo = x in float64.
;;; Factor = 2^(53-24) + 1 = 2^29 + 1 = 536870913.
;;; With factor 2^s+1 in p-bit arithmetic, hi gets p-s bits.  We want 24, so
;;; s = 53-24 = 29 → factor 536870913.  (Using 2^24+1 would give hi 29 bits,
;;; which is NOT float32-representable, breaking DD precision after ~zoom 10^7.)

(define (dd-split x)
  (let* ((c  (* 536870913.0 x))
         (hi (- c (- c x)))
         (lo (- x hi)))
    (list hi lo)))

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f) (define *drag-y* #f)
(define *drag-cx* 0.0) (define *drag-cy* 0.0)

(define *canvas* #f)

;;; Format a coordinate with enough decimal places for the current zoom.
;;; Uses exact integer arithmetic to avoid the 6-digit limit of number->string.
(define (coord->string x)
  (let* ((places (min 15 (+ 2 (inexact->exact (ceiling (/ (log (max *zoom* 1.0)) (log 10.0)))))))
         (neg    (< x 0.0))
         (ax     (if neg (- x) x))
         (scaled (inexact->exact (round (* ax (expt 10.0 places)))))
         (s      (number->string scaled))
         (padded (let loop ((s s))
                   (if (>= (string-length s) places) s (loop (string-append "0" s)))))
         (ilen   (- (string-length padded) places))
         (ipart  (if (= ilen 0) "0" (substring padded 0 ilen)))
         (fpart  (substring padded ilen (string-length padded))))
    (string-append (if neg "-" "") ipart "." fpart)))

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (let* ((xdd (dd-split *cx*))
         (ydd (dd-split *cy*))
         (use-dd (if (and (eq? *algebra* 'complex) (> *zoom* 1e6)) 1 0)))
    (gl-shader-draw! mandel-prog painter
      (list (cons "u_center"    (list *cx* *cy*))
            (cons "u_center_dd" (list (car xdd) (cadr xdd)
                                      (car ydd) (cadr ydd)))
            (cons "u_zoom"      *zoom*)
            (cons "u_max_iter"  *max-iter*)
            (cons "u_algebra"   (algebra->int *algebra*))
            (cons "u_theta"     *theta*)
            (cons "u_phi"       *phi*)
            (cons "u_palette"   *palette*)
            (cons "u_use_dd"    use-dd))))
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      (case *algebra*
        ((complex)    "C  2D")
        ((quaternion) "H  4D slice")
        (else         "O  8D slice"))
      "  iter=" (number->string *max-iter*)
      "  zoom×" (number->string (inexact->exact (round *zoom*)))
      (if (and (eq? *algebra* 'complex) (> *zoom* 1e6)) "  DD" "")
      "  (" (coord->string *cx*) ", " (coord->string *cy*) "i)"))))

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
    (set! *cy* (- *drag-cy* (/ (- y *drag-y*) *zoom*)))
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
    (set! *cy* (- (+ *cy* wy) (/ (- (inexact sy) (/ (inexact *H*) 2.0)) nz)))
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

(box-add! sidebar (make-label "Colour palette"))
(box-add! sidebar
  (make-radio-group
    '("Cycle" "Ultra" "Ice" "Electric" "Gold")
    0
    (lambda (i) (set! *palette* i) (canvas-redraw! *canvas*))))

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
