;;; mandelbrot.scm — Hypercomplex Mandelbrot viewer (GPU edition)
;;; Version: 4.0
;;;
;;; Precision ladder (complex mode only):
;;;   zoom < 1e6    — float32 (fast)
;;;   1e6 – 1e14    — double-double in shader (10× slower, ~10^14 ceiling)
;;;   any depth     — perturbation theory: reference orbit computed in DD on CPU,
;;;                   GPU runs DD on the *delta* only; ceiling ~10^28
;;;                   (activate with 'P' key or auto above zoom 1e7)
;;;
;;; Perturbation theory: z_n = Z_n + ε_n  where Z_n is the reference orbit and
;;;   ε_{n+1} = 2·Z_n·ε_n + ε_n² + δ   (δ = pixel offset from centre, tiny)
;;; Since ε stays small, DD arithmetic on ε is fast and accurate to ~10^28.
;;; Glitched pixels (|ε| blown up) fall back to the plain DD path.
;;;
;;; Controls:
;;;   Left-drag   pan              Scroll / pinch  zoom to cursor
;;;   Double-click  zoom 2×        R  reset view
;;;   P  toggle perturbation       +/-  zoom step
;;;
;;; Run:  ./build/curry examples/mandelbrot.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Fragment shader ──────────────────────────────────────────────────────────

(define mandel-frag-src "
#version 400 core
out vec4 frag_color;

uniform vec2  u_resolution;
uniform float u_dpr;
uniform vec2  u_center;        /* float32 centre (fast path)                */
uniform vec4  u_center_dd;     /* (cx_hi,cx_lo, cy_hi,cy_lo) DD centre      */
uniform float u_zoom;
uniform int   u_max_iter;
uniform int   u_algebra;       /* 0=complex  1=quaternion  2=octonion       */
uniform float u_theta;
uniform float u_phi;
uniform int   u_palette;       /* 0=cycle 1=ultra 2=ice 3=electric 4=gold   */
uniform int   u_mode;          /* 0=float32  1=DD  2=perturbation           */
uniform sampler2D u_ref_orbit; /* DD reference orbit: texel n = (Zx_hi,Zx_lo,Zy_hi,Zy_lo) */
uniform int   u_ref_len;       /* valid texels in u_ref_orbit               */

/* ── Palette ─────────────────────────────────────────────────────────────── */

vec3 iqpal(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
    return a + b * cos(6.28318530718 * (c * t + d));
}
vec3 palette(float raw_t) {
    float t = fract(raw_t * 0.015);
    if (u_palette == 1) return iqpal(t,vec3(0.5),vec3(0.5),vec3(1,1,.5),vec3(.8,.9,.3));
    if (u_palette == 2) return iqpal(t,vec3(0.5),vec3(0.5),vec3(.8,.8,.5),vec3(0,.2,.5));
    if (u_palette == 3) return iqpal(t,vec3(0.5),vec3(0.5),vec3(2,1,0),vec3(.5,.2,.25));
    if (u_palette == 4) return iqpal(t,vec3(.5,.4,.2),vec3(.5,.4,.3),vec3(1,1,.5),vec3(0,.1,.2));
    return iqpal(t,vec3(0.5),vec3(0.5),vec3(1.0),vec3(0,.33,.67));
}
float smooth_n(float i_f, float ns) { return i_f + 1.0 - log2(log2(sqrt(ns))); }

/* ── Double-double arithmetic ────────────────────────────────────────────── */

vec2 ts(float a, float b) {
    float s=a+b, v=s-a; return vec2(s,(a-(s-v))+(b-v));
}
vec2 tp(float a, float b) {
    float p=a*b; return vec2(p, fma(a,b,-p));
}
vec2 dd_add(vec2 a, vec2 b) { vec2 s=ts(a.x,b.x); return ts(s.x,s.y+a.y+b.y); }
vec2 dd_sub(vec2 a, vec2 b) { return dd_add(a,vec2(-b.x,-b.y)); }
vec2 dd_mul(vec2 a, vec2 b) { vec2 p=tp(a.x,b.x); p.y+=a.x*b.y+a.y*b.x; return ts(p.x,p.y); }

/* Build per-pixel c as DD from split centre + float32 pixel offset */
void pixel_c_dd(float wx, float wy, out vec2 cx, out vec2 cy) {
    cx = vec2(u_center_dd.x, u_center_dd.y + wx);
    cy = vec2(u_center_dd.z, u_center_dd.w + wy);
}

/* ── Float32 Mandelbrot ───────────────────────────────────────────────────── */

float mandel_2d(vec2 c) {
    vec2 z = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float x2=z.x*z.x, y2=z.y*z.y;
        if (x2+y2 > 4.0) return smooth_n(float(i), x2+y2);
        z = vec2(x2-y2+c.x, 2.0*z.x*z.y+c.y);
    }
    return -1.0;
}

/* ── DD Mandelbrot ───────────────────────────────────────────────────────── */

float mandel_2d_dd(float wx, float wy) {
    vec2 cx, cy; pixel_c_dd(wx, wy, cx, cy);
    vec2 zx=vec2(0.0), zy=vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = zx.x*zx.x + zy.x*zy.x;
        if (ns > 4.0) return smooth_n(float(i), ns);
        vec2 zx2  = dd_sub(dd_mul(zx,zx), dd_mul(zy,zy));
        vec2 zxzy = dd_mul(zx,zy);
        zx = dd_add(zx2, cx);
        zy = dd_add(2.0*zxzy, cy);
    }
    return -1.0;
}

/* ── Perturbation Mandelbrot ─────────────────────────────────────────────── */
/*   ε_{n+1} = 2·Z_n·ε_n + ε_n² + δ                                         */
/*   Z_n from reference orbit texture (DD: vec4 = Zx_hi,Zx_lo,Zy_hi,Zy_lo)  */
/*   Returns -2.0 for glitched pixels (|ε| blew up).                         */

float mandel_2d_perturb(float wx, float wy) {
    vec2 dx = vec2(wx, 0.0);   /* δ in DD — lo=0 since wx is already float32 */
    vec2 dy = vec2(wy, 0.0);
    vec2 ex = vec2(0.0), ey = vec2(0.0);

    int lim = min(u_max_iter, u_ref_len);
    for (int n = 0; n < lim; n++) {
        vec4 orb = texelFetch(u_ref_orbit, ivec2(n, 0), 0);
        vec2 Zx = orb.xy, Zy = orb.zw;

        /* Escape test on |Z_n + ε|  */
        vec2 tx = dd_add(Zx, ex), ty = dd_add(Zy, ey);
        float ns = tx.x*tx.x + ty.x*ty.x;
        if (ns > 4.0) return smooth_n(float(n), ns);

        /* ε_{n+1} = 2·Z_n·ε + ε² + δ */
        vec2 Zx_ex = dd_mul(Zx, ex), Zy_ey = dd_mul(Zy, ey);
        vec2 Zx_ey = dd_mul(Zx, ey), Zy_ex = dd_mul(Zy, ex);
        vec2 ex2   = dd_sub(dd_mul(ex,ex), dd_mul(ey,ey));
        vec2 ey2   = 2.0 * dd_mul(ex, ey);

        vec2 new_ex = dd_add(dd_sub(2.0*Zx_ex, 2.0*Zy_ey), dd_add(ex2, dx));
        vec2 new_ey = dd_add(dd_add(2.0*Zx_ey, 2.0*Zy_ex), dd_add(ey2, dy));
        ex = new_ex; ey = new_ey;

        /* Glitch: ε has grown far beyond Z — perturbation invalid */
        if (abs(ex.x) + abs(ey.x) > 1e6) return -2.0;
    }
    return -1.0;
}

/* ── Quaternion / octonion (float32 only) ────────────────────────────────── */

float mandel_4d(vec4 c) {
    vec4 z=vec4(0.0);
    for (int i=0;i<u_max_iter;i++) {
        float ns=dot(z,z); if(ns>4.0) return smooth_n(float(i),ns);
        float a=z.x; z=vec4(a*a-dot(z.yzw,z.yzw),2.0*a*z.yzw)+c;
    }
    return -1.0;
}
float mandel_8d(float c0, vec4 cl, vec4 ch) {
    float z0=0.0; vec4 zl=vec4(0.0),zh=vec4(0.0);
    for (int i=0;i<u_max_iter;i++) {
        float ns=z0*z0+dot(zl,zl)+dot(zh,zh); if(ns>4.0) return smooth_n(float(i),ns);
        float nv2=dot(zl,zl)+dot(zh,zh), z0n=z0*z0-nv2+c0;
        zl=2.0*z0*zl+cl; zh=2.0*z0*zh+ch; z0=z0n;
    }
    return -1.0;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

void main() {
    float pzoom = u_zoom * u_dpr;
    float wx = ( gl_FragCoord.x - u_resolution.x*0.5) / pzoom;
    float wy = -(gl_FragCoord.y - u_resolution.y*0.5) / pzoom;

    float t;
    if (u_algebra == 0) {
        if (u_mode == 2) {
            t = mandel_2d_perturb(wx, wy);
            if (t == -2.0)          /* glitch: fall back to DD for this pixel */
                t = mandel_2d_dd(wx, wy);
        } else if (u_mode == 1) {
            t = mandel_2d_dd(wx, wy);
        } else {
            t = mandel_2d(vec2(u_center.x+wx, u_center.y+wy));
        }
    } else if (u_algebra == 1) {
        t = mandel_4d(vec4(wx*cos(u_theta)+u_center.x, wy*cos(u_phi)+u_center.y,
                           wx*sin(u_theta), wy*sin(u_phi)));
    } else {
        t = mandel_8d(wx*cos(u_theta)+u_center.x,
                      vec4(wy*cos(u_phi)+u_center.y, wx*sin(u_theta), wy*sin(u_phi), 0),
                      vec4(0.0));
    }

    frag_color = (t < 0.0) ? vec4(0,0,0,1) : vec4(palette(t), 1);
}
")

(define mandel-prog (make-gl-shader mandel-frag-src))

;;; ── View state ───────────────────────────────────────────────────────────────

(define *algebra*   'complex)
(define *max-iter*  150)
(define *cx*       -0.5)
(define *cy*        0.0)
(define *zoom*    200.0)
(define *theta*     0.0)
(define *phi*       0.0)
(define *palette*   0)
(define *perturb*   #f)   ; #t = perturbation mode active
(define *W* 800)
(define *H* 600)

(define (algebra->int a)
  (case a ((complex) 0) ((quaternion) 1) (else 2)))

;;; ── Veltkamp dd-split ────────────────────────────────────────────────────────
;;; Factor 2^29+1 = 536870913: splits float64 into float32-exact hi + 29-bit lo.

(define (dd-split x)
  (let* ((c  (* 536870913.0 x))
         (hi (- c (- c x)))
         (lo (- x hi)))
    (list hi lo)))

;;; ── Double-double arithmetic in Scheme (for reference orbit) ─────────────────

(define (scheme-two-sum a b)
  (let* ((s (+ a b)) (v (- s a)))
    (values s (+ (- a (- s v)) (- b v)))))

(define (scheme-two-prod a b)
  ;; Veltkamp split for float64 → two float64 halves
  (let* ((p  (* a b))
         (c  (* 134217729.0 a))  ; 2^27+1
         (ah (- c (- c a))) (al (- a ah))
         (c2 (* 134217729.0 b))
         (bh (- c2 (- c2 b))) (bl (- b bh))
         (e  (+ (+ (- (* ah bh) p) (* ah bl)) (+ (* al bh) (* al bl)))))
    (values p e)))

(define (dd+ ah al bh bl)
  (let*-values (((s e) (scheme-two-sum ah bh)))
    (scheme-two-sum s (+ e al bl))))

(define (dd* ah al bh bl)
  (let*-values (((p e) (scheme-two-prod ah bh)))
    (scheme-two-sum p (+ e (* ah bl) (* al bh)))))

;;; ── Reference orbit ──────────────────────────────────────────────────────────
;;; Computes the reference orbit at (*cx*, *cy*) in double-double Scheme
;;; arithmetic.  Returns a bytevector of 4×float32 per step (Zx_hi Zx_lo
;;; Zy_hi Zy_lo) packed as little-endian IEEE 754 single-precision bytes.

(define *orbit-bv*  #f)   ; bytevector of raw float32 bytes
(define *orbit-tex* #f)   ; GL texture object (rgba32f)
(define *orbit-len* 0)    ; number of valid steps
(define *orbit-cx*  #f)   ; cx when orbit was last computed
(define *orbit-cy*  #f)   ; cy when orbit was last computed
(define *orbit-iters* 0)  ; max-iter when orbit was last computed

;;; Pack a Scheme flonum as 4 IEEE 754 float32 bytes into bv at offset.
;;; We write x as a float32 by splitting with dd-split and using the hi word.
(define (bv-set-f32! bv offset x)
  ;; We need to write x as a 32-bit IEEE float.
  ;; Encode via: sign, exponent, mantissa.
  (let* ((x32 (car (dd-split x)))   ; nearest float32 value (as float64)
         (neg (< x32 0.0))
         (ax  (if neg (- x32) x32)))
    (cond
      ((= ax 0.0)
       (bytevector-u8-set! bv offset 0)
       (bytevector-u8-set! bv (+ offset 1) 0)
       (bytevector-u8-set! bv (+ offset 2) 0)
       (bytevector-u8-set! bv (+ offset 3) (if neg #x80 0)))
      (else
       ;; Find exponent: ax = 1.mantissa × 2^exp
       (let* ((log2-ax  (/ (log ax) (log 2.0)))
              (exp-raw  (inexact->exact (floor log2-ax)))
              (exp-bias (+ exp-raw 127))
              (mant-f   (- (/ ax (expt 2.0 exp-raw)) 1.0))
              (mant-i   (inexact->exact (round (* mant-f (expt 2.0 23)))))
              ;; Clamp to valid float32 range
              (exp-ok   (max 0 (min 255 exp-bias)))
              (mant-ok  (max 0 (min #x7fffff mant-i)))
              (bits     (bitwise-or
                          (if neg #x80000000 0)
                          (arithmetic-shift exp-ok 23)
                          mant-ok))
              (b0 (bitwise-and bits #xff))
              (b1 (bitwise-and (arithmetic-shift bits -8) #xff))
              (b2 (bitwise-and (arithmetic-shift bits -16) #xff))
              (b3 (bitwise-and (arithmetic-shift bits -24) #xff)))
         ;; Little-endian
         (bytevector-u8-set! bv offset b0)
         (bytevector-u8-set! bv (+ offset 1) b1)
         (bytevector-u8-set! bv (+ offset 2) b2)
         (bytevector-u8-set! bv (+ offset 3) b3))))))

(define (compute-reference-orbit!)
  (when (or (not *orbit-cx*)
            (not (= *orbit-cx* *cx*))
            (not (= *orbit-cy* *cy*))
            (not (= *orbit-iters* *max-iter*)))
    (let* ((iters *max-iter*)
           (bv    (make-bytevector (* 4 4 iters) 0))  ; 4 floats × 4 bytes
           (cx-dd (dd-split *cx*))
           (cy-dd (dd-split *cy*))
           (cx-hi (car cx-dd)) (cx-lo (cadr cx-dd))
           (cy-hi (car cy-dd)) (cy-lo (cadr cy-dd)))
      (let loop ((zx-hi 0.0) (zx-lo 0.0) (zy-hi 0.0) (zy-lo 0.0) (n 0))
        (when (< n iters)
          ;; Store Z_n as 4 float32s
          (let ((base (* 16 n)))
            (bv-set-f32! bv base       zx-hi)
            (bv-set-f32! bv (+ base 4) zx-lo)
            (bv-set-f32! bv (+ base 8) zy-hi)
            (bv-set-f32! bv (+ base 12) zy-lo))
          ;; Escape check
          (let ((ns (+ (* zx-hi zx-hi) (* zy-hi zy-hi))))
            (when (< ns 4.0)
              ;; Z_{n+1} = Z_n² + C  (DD)
              (let*-values
                (((zx2h zx2l) (dd* zx-hi zx-lo zx-hi zx-lo))
                 ((zy2h zy2l) (dd* zy-hi zy-lo zy-hi zy-lo))
                 ((zxzyh zxzyl) (dd* zx-hi zx-lo zy-hi zy-lo))
                 ((nzxh nzxl) (dd+ (- zx2h zy2h) (- zx2l zy2l) cx-hi cx-lo))
                 ((nzyh nzyl) (dd+ (* 2.0 zxzyh) (* 2.0 zxzyl) cy-hi cy-lo)))
                (loop nzxh nzxl nzyh nzyl (+ n 1)))))))
      ;; Upload to GPU
      (set! *orbit-bv* bv)
      (if *orbit-tex*
          (gl-texture-update! *orbit-tex* bv)
          (set! *orbit-tex* (make-gl-texture bv iters 1 'rgba32f)))
      (set! *orbit-len* iters)
      (set! *orbit-cx* *cx*)
      (set! *orbit-cy* *cy*)
      (set! *orbit-iters* *max-iter*))))

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f) (define *drag-y* #f)
(define *drag-cx* 0.0) (define *drag-cy* 0.0)
(define *canvas* #f)

;;; ── Coordinate display ───────────────────────────────────────────────────────

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

(define (current-mode)
  (cond
    ((not (eq? *algebra* 'complex)) 0)  ; non-complex: always float32
    (*perturb* 2)                        ; perturbation
    ((> *zoom* 1e6) 1)                   ; auto-DD
    (else 0)))                           ; float32

(define (mode-label m)
  (case m ((2) " PERTURB") ((1) " DD") (else "")))

(define (draw-frame painter w h)
  (let* ((xdd  (dd-split *cx*))
         (ydd  (dd-split *cy*))
         (mode (current-mode)))
    (when (= mode 2) (compute-reference-orbit!))
    (gl-shader-draw! mandel-prog painter
      (list (cons "u_center"    (list *cx* *cy*))
            (cons "u_center_dd" (list (car xdd) (cadr xdd) (car ydd) (cadr ydd)))
            (cons "u_zoom"      *zoom*)
            (cons "u_max_iter"  *max-iter*)
            (cons "u_algebra"   (algebra->int *algebra*))
            (cons "u_theta"     *theta*)
            (cons "u_phi"       *phi*)
            (cons "u_palette"   *palette*)
            (cons "u_mode"      mode)
            (cons "u_ref_orbit" (if *orbit-tex* *orbit-tex*
                                    (make-gl-texture (make-bytevector 64 0) 1 1 'rgba32f)))
            (cons "u_ref_len"   (if (= mode 2) *orbit-len* 0)))))
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
      (mode-label (current-mode))
      "  (" (coord->string *cx*) ", " (coord->string *cy*) "i)")))

;;; ── Input ────────────────────────────────────────────────────────────────────

(define (on-mouse-press! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x* x) (set! *drag-y* y)
    (set! *drag-cx* *cx*) (set! *drag-cy* *cy*)))

(define (on-mouse-release! x y btn)
  (when (equal? btn 'left) (set! *drag-x* #f)))

(define (on-mouse-move! x y)
  (when *drag-x*
    ;; Invalidate orbit on pan
    (set! *orbit-cx* #f)
    (set! *cx* (- *drag-cx* (/ (- x *drag-x*) *zoom*)))
    (set! *cy* (- *drag-cy* (/ (- y *drag-y*) *zoom*)))
    (canvas-redraw! *canvas*)))

(define (reset-view!)
  (set! *cx* -0.5) (set! *cy* 0.0) (set! *zoom* 200.0)
  (set! *orbit-cx* #f) (set! *perturb* #f)
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
  (make-slider "Iterations" 20 800 10 150
    (lambda (v)
      (set! *max-iter* v)
      (set! *orbit-iters* 0)   ; force orbit recompute
      (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))

(box-add! sidebar (make-label "Slice plane  (ℍ / 𝕆)"))
(box-add! sidebar
  (make-slider "θ  e₀↔e₂" -100 100 1 0
    (lambda (v) (set! *theta* (* v 0.031416)) (canvas-redraw! *canvas*))))
(box-add! sidebar
  (make-slider "φ  e₁↔e₃" -100 100 1 0
    (lambda (v) (set! *phi* (* v 0.031416)) (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))
(box-add! sidebar
  (make-button "Perturbation  (P)"
    (lambda ()
      (set! *perturb* (not *perturb*))
      (when *perturb* (set! *orbit-cx* #f))
      (canvas-redraw! *canvas*))))
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
      ((equal? key "r")   (reset-view!))
      ((equal? key "p")   (set! *perturb* (not *perturb*))
                          (when *perturb* (set! *orbit-cx* #f))
                          (canvas-redraw! *canvas*))
      ((or (equal? key "=") (equal? key "+")) (zoom-step! 1.5))
      ((equal? key "-")   (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q") (equal? key "Escape")) (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (unless (qt-gpu? canvas)
      (display "[mandelbrot] warning: OpenGL unavailable\n")
      (flush-output-port (current-output-port)))
    (canvas-redraw! canvas)))

(window-on-close! win (lambda () (quit-event-loop)))

(window-show! win)
(run-event-loop)
