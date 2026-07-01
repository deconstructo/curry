;;; mandelbrot.scm — Hypercomplex Mandelbrot / Julia viewer  (GPU edition)
;;; Version: 6.0
;;;
;;; Precision ladder (ℂ mode):
;;;   zoom < 1e6       float32 GPU
;;;   zoom ≥ 1e6       double-double GPU  (~10^15 ceiling on δ alone)
;;;   perturbation on  DD reference orbit on CPU + DD delta on GPU  (~10^28)
;;;
;;; Centre is stored as a double-double (hi, lo) pair.  Drag operations
;;; accumulate the pixel offset via two-sum, so precision does not degrade
;;; even after thousands of pan steps at 10^12× zoom.
;;;
;;; New in v6:
;;;   · DD centre coordinates for navigation to ~10^28
;;;   · Auto-perturbation at zoom > 1e6 (quality degrades gracefully to DD
;;;     while dragging, snaps back to perturbation on release)
;;;   · Julia-set mode (float32 and DD; perturbation not applicable)
;;;   · Adaptive iteration count recommendation
;;;   · Cleaner status line
;;;
;;; Controls:
;;;   Left-drag          pan
;;;   Scroll / pinch     zoom to cursor
;;;   Double-click       zoom 2×
;;;   R   reset view     P   toggle auto-perturbation
;;;   J   toggle Julia   Q / Escape   quit

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Fragment shader ──────────────────────────────────────────────────────────

(define mandel-frag-src "
#version 400 core
out vec4 frag_color;

uniform vec2  u_resolution;
uniform float u_dpr;
uniform vec2  u_center;
uniform vec4  u_center_dd;     /* cx_hi  cx_lo  cy_hi  cy_lo */
uniform float u_zoom;
uniform int   u_max_iter;
uniform int   u_algebra;
uniform float u_theta;
uniform float u_phi;
uniform int   u_palette;
uniform int   u_mode;           /* 0=float32  1=DD  2=perturbation */
uniform int   u_julia;          /* 0=Mandelbrot  1=Julia */
uniform vec2  u_julia_c;        /* Julia c  (float32 / DD hi) */
uniform vec4  u_julia_c_dd;     /* cx_hi  cx_lo  cy_hi  cy_lo */
uniform sampler2D u_ref_orbit;
uniform int   u_ref_len;

/* ── Palette ──────────────────────────────────────────────────────────────── */
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

/* ── Double-double ────────────────────────────────────────────────────────── */
vec2 ts(float a, float b)  { float s=a+b,v=s-a; return vec2(s,(a-(s-v))+(b-v)); }
vec2 tp(float a, float b)  { float p=a*b; return vec2(p,fma(a,b,-p)); }
vec2 dd_add(vec2 a, vec2 b){ vec2 s=ts(a.x,b.x); return ts(s.x,s.y+a.y+b.y); }
vec2 dd_sub(vec2 a, vec2 b){ return dd_add(a,vec2(-b.x,-b.y)); }
vec2 dd_mul(vec2 a, vec2 b){ vec2 p=tp(a.x,b.x); p.y+=a.x*b.y+a.y*b.x; return ts(p.x,p.y); }
vec2 dd_scale(vec2 a, float s){ return ts(a.x*s, a.y*s); }

/* Pixel offset → DD coordinate of this fragment */
void pixel_dd(float wx, float wy, out vec2 px, out vec2 py) {
    px = ts(u_center_dd.x, u_center_dd.y + wx);
    py = ts(u_center_dd.z, u_center_dd.w + wy);
}

/* ── Mandelbrot float32 ───────────────────────────────────────────────────── */
float mandel_2d(vec2 c) {
    vec2 z = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float x2=z.x*z.x, y2=z.y*z.y;
        if (x2+y2 > 4.0) return smooth_n(float(i), x2+y2);
        z = vec2(x2-y2+c.x, 2.0*z.x*z.y+c.y);
    }
    return -1.0;
}

/* ── Julia float32 ────────────────────────────────────────────────────────── */
float julia_2d(vec2 z, vec2 c) {
    for (int i = 0; i < u_max_iter; i++) {
        float x2=z.x*z.x, y2=z.y*z.y;
        if (x2+y2 > 4.0) return smooth_n(float(i), x2+y2);
        z = vec2(x2-y2+c.x, 2.0*z.x*z.y+c.y);
    }
    return -1.0;
}

/* ── Mandelbrot DD ────────────────────────────────────────────────────────── */
float mandel_2d_dd(float wx, float wy) {
    vec2 cx, cy; pixel_dd(wx, wy, cx, cy);
    vec2 zx = vec2(0.0), zy = vec2(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = zx.x*zx.x + zy.x*zy.x;
        if (ns > 4.0) return smooth_n(float(i), ns);
        vec2 zx2  = dd_sub(dd_mul(zx,zx), dd_mul(zy,zy));
        vec2 zxzy = dd_mul(zx, zy);
        zx = dd_add(zx2, cx);
        zy = dd_add(dd_scale(zxzy, 2.0), cy);
    }
    return -1.0;
}

/* ── Julia DD ─────────────────────────────────────────────────────────────── */
/* z₀ = pixel position (from u_center_dd + offset);  c = u_julia_c_dd */
float julia_2d_dd(float wx, float wy) {
    vec2 zx, zy; pixel_dd(wx, wy, zx, zy);
    vec2 cx = vec2(u_julia_c_dd.x, u_julia_c_dd.y);
    vec2 cy = vec2(u_julia_c_dd.z, u_julia_c_dd.w);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = zx.x*zx.x + zy.x*zy.x;
        if (ns > 4.0) return smooth_n(float(i), ns);
        vec2 zx2  = dd_sub(dd_mul(zx,zx), dd_mul(zy,zy));
        vec2 zxzy = dd_mul(zx, zy);
        zx = dd_add(zx2, cx);
        zy = dd_add(dd_scale(zxzy, 2.0), cy);
    }
    return -1.0;
}

/* ── Perturbation (Mandelbrot only) ──────────────────────────────────────── */
float mandel_2d_perturb(float wx, float wy) {
    vec2 dx = vec2(wx, 0.0), dy = vec2(wy, 0.0);
    vec2 ex = vec2(0.0), ey = vec2(0.0);
    int lim = min(u_max_iter, u_ref_len);
    for (int n = 0; n < lim; n++) {
        vec4 orb = texelFetch(u_ref_orbit, ivec2(n, 0), 0);
        vec2 Zx = orb.xy, Zy = orb.zw;
        vec2 tx = dd_add(Zx, ex), ty = dd_add(Zy, ey);
        float ns = tx.x*tx.x + ty.x*ty.x;
        if (ns > 4.0) return smooth_n(float(n), ns);
        vec2 new_ex = dd_add(dd_sub(dd_scale(dd_mul(Zx,ex), 2.0),
                                    dd_scale(dd_mul(Zy,ey), 2.0)),
                             dd_add(dd_sub(dd_mul(ex,ex), dd_mul(ey,ey)), dx));
        vec2 new_ey = dd_add(dd_add(dd_scale(dd_mul(Zx,ey), 2.0),
                                    dd_scale(dd_mul(Zy,ex), 2.0)),
                             dd_add(dd_scale(dd_mul(ex,ey), 2.0), dy));
        ex = new_ex; ey = new_ey;
        if (abs(ex.x) + abs(ey.x) > 1e6) return -2.0;
    }
    return -1.0;
}

/* ── Quaternion ──────────────────────────────────────────────────────────── */
float mandel_4d(vec4 c) {
    vec4 z = vec4(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = dot(z,z); if (ns > 4.0) return smooth_n(float(i), ns);
        float a = z.x; z = vec4(a*a - dot(z.yzw,z.yzw), 2.0*a*z.yzw) + c;
    }
    return -1.0;
}

/* ── Octonion ────────────────────────────────────────────────────────────── */
float mandel_8d(float c0, vec4 cl, vec4 ch) {
    float z0 = 0.0; vec4 zl = vec4(0.0), zh = vec4(0.0);
    for (int i = 0; i < u_max_iter; i++) {
        float ns = z0*z0 + dot(zl,zl) + dot(zh,zh);
        if (ns > 4.0) return smooth_n(float(i), ns);
        float nv2 = dot(zl,zl)+dot(zh,zh), z0n = z0*z0-nv2+c0;
        zl = 2.0*z0*zl + cl; zh = 2.0*z0*zh + ch; z0 = z0n;
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
        if (u_julia == 1) {
            if (u_mode >= 1)
                t = julia_2d_dd(wx, wy);
            else
                t = julia_2d(vec2(u_center.x + wx, u_center.y + wy), u_julia_c);
        } else {
            if (u_mode == 2) {
                t = mandel_2d_perturb(wx, wy);
                if (t == -2.0) t = mandel_2d_dd(wx, wy);
            } else if (u_mode == 1) {
                t = mandel_2d_dd(wx, wy);
            } else {
                t = mandel_2d(vec2(u_center.x + wx, u_center.y + wy));
            }
        }
    } else if (u_algebra == 1) {
        t = mandel_4d(vec4(wx*cos(u_theta) + u_center.x,
                           wy*cos(u_phi)   + u_center.y,
                           wx*sin(u_theta), wy*sin(u_phi)));
    } else {
        t = mandel_8d(wx*cos(u_theta) + u_center.x,
                      vec4(wy*cos(u_phi)+u_center.y, wx*sin(u_theta), wy*sin(u_phi), 0),
                      vec4(0.0));
    }
    frag_color = (t < 0.0) ? vec4(0,0,0,1) : vec4(palette(t), 1);
}
")

(define mandel-prog (make-gl-shader mandel-frag-src))

;;; ── View state ───────────────────────────────────────────────────────────────
;;;
;;; Centre is a double-double: *cx-hi* + *cx-lo* is the true real part,
;;; *cy-hi* + *cy-lo* is the imaginary part.  For a fresh location entered as a
;;; float64, lo = 0; after many DD-arithmetic panning steps lo accumulates the
;;; sub-ULP correction.

(define *algebra*  'complex)
(define *max-iter* 150)

(define *cx-hi* -0.5)  (define *cx-lo* 0.0)
(define *cy-hi*  0.0)  (define *cy-lo* 0.0)
(define *zoom*  200.0)
(define *theta*   0.0)
(define *phi*     0.0)
(define *palette*  0)
(define *perturb*  #t)   ; auto-perturbation on by default
(define *julia-mode* #f)
(define *jcx*  -0.7)    ; Julia parameter real part
(define *jcy*   0.27)   ; Julia parameter imag part
(define *W* 800) (define *H* 600)

(define (algebra->int a) (case a ((complex) 0) ((quaternion) 1) (else 2)))

;;; ── DD arithmetic helpers ────────────────────────────────────────────────────

;;; Veltkamp split (factor 2^29+1) — returns (list hi lo)
(define (dd-split x)
  (let* ((c (* 536870913.0 x)) (hi (- c (- c x)))) (list hi (- x hi))))

(define (scheme-two-sum a b)
  (let* ((s (+ a b)) (v (- s a))) (cons s (+ (- a (- s v)) (- b v)))))

(define (scheme-two-prod a b)
  (let* ((p (* a b))
         (c  (* 134217729.0 a)) (ah (- c (- c a))) (al (- a ah))
         (c2 (* 134217729.0 b)) (bh (- c2 (- c2 b))) (bl (- b bh))
         (e (+ (- (* ah bh) p) (* ah bl) (* al bh) (* al bl))))
    (cons p e)))

;;; DD + DD
(define (dd+ ah al bh bl)
  (let* ((s (scheme-two-sum ah bh)))
    (scheme-two-sum (car s) (+ (cdr s) al bl))))

;;; DD × DD
(define (dd* ah al bh bl)
  (let* ((p (scheme-two-prod ah bh)))
    (scheme-two-sum (car p) (+ (cdr p) (* ah bl) (* al bh)))))

;;; DD - DD
(define (dd- ah al bh bl) (dd+ ah al (- bh) (- bl)))

;;; DD + float64  (exact Knuth two-sum, no catastrophic cancellation)
(define (dd+f hi lo x)
  (let* ((s (scheme-two-sum hi x)))
    (scheme-two-sum (car s) (+ (cdr s) lo))))

;;; ── Reference orbit ──────────────────────────────────────────────────────────

(define *orbit-tex*    #f)
(define *orbit-len*    0)
(define *orbit-cx-hi*  #f)   ; hi component when orbit was computed
(define *orbit-cx-lo*  0.0)
(define *orbit-cy-hi*  #f)
(define *orbit-cy-lo*  0.0)
(define *orbit-iters*  0)

;;; Pack a float64 as IEEE 754 single-precision (little-endian) into bv at offset.
;;; Handles zero, subnormals, normals.  Inf/NaN from the orbit are impossible
;;; (the escape check fires first), so we don't guard for them.
(define (bv-set-f32! bv offset x)
  (let* ((x32 (car (dd-split x))) (neg (< x32 0.0)) (ax (if neg (- x32) x32)))
    (define (store! bits)
      (bytevector-u8-set! bv offset       (bitwise-and bits #xff))
      (bytevector-u8-set! bv (+ offset 1) (bitwise-and (arithmetic-shift bits  -8) #xff))
      (bytevector-u8-set! bv (+ offset 2) (bitwise-and (arithmetic-shift bits -16) #xff))
      (bytevector-u8-set! bv (+ offset 3) (bitwise-and (arithmetic-shift bits -24) #xff)))
    (define sign (if neg #x80000000 0))
    (if (= ax 0.0)
        (store! sign)
        (let* ((e-raw  (inexact->exact (floor (/ (log ax) (log 2.0)))))
               ;; Guard: log may round down for exact powers of 2
               (e-raw  (if (>= (/ ax (expt 2.0 e-raw)) 2.0) (+ e-raw 1) e-raw))
               (biased (+ e-raw 127)))
          (if (< biased 1)
              ;; Subnormal: shift mantissa right so the exponent would be -126
              (let* ((shift  (- 1 biased))
                     (mant-i (inexact->exact
                               (floor (* (/ ax (expt 2.0 -126)) (expt 2.0 23)))))
                     (mant-i (arithmetic-shift mant-i (- shift))))
                (store! (bitwise-or sign (max 0 (min #x7fffff mant-i)))))
              (let* ((mant-i (inexact->exact
                               (round (* (- (/ ax (expt 2.0 e-raw)) 1.0) (expt 2.0 23)))))
                     ;; mant-i = 2^23 at exact powers of 2 — carry into exponent
                     (e-raw  (if (= mant-i (expt 2 23)) (+ e-raw 1) e-raw))
                     (mant-i (if (= mant-i (expt 2 23)) 0 mant-i))
                     (biased (+ e-raw 127)))
                (store! (bitwise-or sign
                                    (arithmetic-shift (max 0 (min 255 biased)) 23)
                                    (max 0 (min #x7fffff mant-i))))))))))

(define (compute-reference-orbit!)
  (when (or (not *orbit-cx-hi*)
            (not (= *orbit-cx-hi* *cx-hi*))
            (not (= *orbit-cx-lo* *cx-lo*))
            (not (= *orbit-cy-hi* *cy-hi*))
            (not (= *orbit-cy-lo* *cy-lo*))
            (not (= *orbit-iters* *max-iter*)))
    (let* ((iters  (inexact->exact *max-iter*))
           (bv     (make-bytevector (* 16 iters) 0))
           (cx-hi  *cx-hi*) (cx-lo *cx-lo*)
           (cy-hi  *cy-hi*) (cy-lo *cy-lo*))
      (let loop ((zx-hi 0.0) (zx-lo 0.0) (zy-hi 0.0) (zy-lo 0.0) (n 0))
        (if (>= n iters)
            (set! *orbit-len* iters)
            (let ((base (* 16 n)))
              (bv-set-f32! bv base        zx-hi)
              (bv-set-f32! bv (+ base  4) zx-lo)
              (bv-set-f32! bv (+ base  8) zy-hi)
              (bv-set-f32! bv (+ base 12) zy-lo)
              (if (>= (+ (* zx-hi zx-hi) (* zy-hi zy-hi)) 4.0)
                  (set! *orbit-len* (+ n 1))
                  (let* ((zx2  (dd* zx-hi zx-lo zx-hi zx-lo))
                         (zy2  (dd* zy-hi zy-lo zy-hi zy-lo))
                         (zxzy (dd* zx-hi zx-lo zy-hi zy-lo))
                         (sq   (dd- (car zx2) (cdr zx2) (car zy2) (cdr zy2)))
                         (nzx  (dd+ (car sq) (cdr sq) cx-hi cx-lo))
                         (nzy  (dd+ (* 2.0 (car zxzy))
                                    (* 2.0 (cdr zxzy))
                                    cy-hi cy-lo)))
                    (loop (car nzx) (cdr nzx) (car nzy) (cdr nzy) (+ n 1)))))))
      (if *orbit-tex*
          (gl-texture-update! *orbit-tex* bv)
          (set! *orbit-tex* (make-gl-texture bv iters 1 'rgba32f)))
      (set! *orbit-cx-hi* *cx-hi*) (set! *orbit-cx-lo* *cx-lo*)
      (set! *orbit-cy-hi* *cy-hi*) (set! *orbit-cy-lo* *cy-lo*)
      (set! *orbit-iters* *max-iter*))))

;;; ── Bookmarks ────────────────────────────────────────────────────────────────

(define *bookmarks*
  (list
    (list "Default view"          -0.5                  0.0                  200.0)
    (list "Seahorse Valley"       -0.7454               0.1130               4000.0)
    (list "Elephant Valley"       -0.7350088086         0.1786057659         8000.0)
    (list "San Marco basin"       -0.1565               1.0322               3000.0)
    (list "Period-3 island"       -0.1226               0.7449               5000.0)
    (list "Feigenbaum point"      -1.4011551890920505   0.0                  20000.0)
    (list "The Dragon"             0.001643721971153    0.822467633298876    50000.0)
    (list "Real-axis spike"       -2.0                  0.0                  500.0)
    (list "Deep spiral"           -0.10109636384        0.95628651080        80000.0)
    (list "Seahorse head"         -0.74364388703        0.13182590421        100000.0)))
(define (bookmark-name b) (list-ref b 0))
(define (bookmark-cx   b) (list-ref b 1))
(define (bookmark-cy   b) (list-ref b 2))
(define (bookmark-zoom b) (list-ref b 3))
(define *n-builtins* 10)

(define *bookmarks-path*
  (string-append (or (get-environment-variable "HOME") ".") "/.mandelbrot_bookmarks.scm"))

;;; load-bookmarks! reads saved entries and appends them to *bookmarks*.
;;; Call it after bm-dropdown is defined; pass the dropdown to update the UI.
(define (load-bookmarks! . args)
  (let ((dd (if (null? args) #f (car args))))
    (guard (e (#t #f))
      (call-with-port (open-input-file *bookmarks-path*)
        (lambda (p)
          (let loop ()
            (let ((x (read p)))
              (unless (eof-object? x)
                (set! *bookmarks* (append *bookmarks* (list x)))
                (when dd (dropdown-add-item! dd (bookmark-name x)))
                (loop)))))))))

(define (save-bookmarks!)
  (guard (e (#t #f))
    (call-with-port (open-output-file *bookmarks-path*)
      (lambda (p)
        (for-each (lambda (b) (write b p) (newline p))
                  (list-tail *bookmarks* *n-builtins*))))))

;;; ── Drag / canvas state ──────────────────────────────────────────────────────

;;; We snapshot the full DD centre on mouse-press and accumulate the pixel
;;; delta relative to that snapshot.  This avoids iterative floating-point
;;; drift when dragging long distances.

(define *drag-x*      #f) (define *drag-y*      #f)
(define *drag-cx-hi*  0.0) (define *drag-cx-lo* 0.0)
(define *drag-cy-hi*  0.0) (define *drag-cy-lo* 0.0)
(define *is-dragging* #f)
(define *canvas* #f)

;;; ── Recommended iteration count ──────────────────────────────────────────────

(define (recommended-iters)
  (inexact->exact (min 4000 (round (* 80 (/ (log (max *zoom* 1.0)) (log 10.0)))))))

;;; ── Coordinate display ───────────────────────────────────────────────────────

(define (coord->string x)
  (let* ((places (min 15 (+ 2 (inexact->exact (ceiling (/ (log (max *zoom* 1.0)) (log 10.0)))))))
         (neg (< x 0.0)) (ax (if neg (- x) x))
         (scaled  (inexact->exact (round (* ax (expt 10.0 places)))))
         (s       (number->string scaled))
         (padded  (let loop ((s s))
                    (if (>= (string-length s) places) s
                        (loop (string-append "0" s)))))
         (ilen    (- (string-length padded) places))
         (ipart   (if (= ilen 0) "0" (substring padded 0 ilen)))
         (fpart   (substring padded ilen (string-length padded))))
    (string-append (if neg "-" "") ipart "." fpart)))

;;; ── Render mode selection ────────────────────────────────────────────────────
;;;
;;; Auto-perturbation: enabled whenever zoom > 1e6 and we are in Mandelbrot
;;; mode AND the user has not disabled it.  During a drag we drop back to DD
;;; (mode 1) to avoid recomputing the reference orbit on every mouse-move
;;; event; the orbit is (re)computed on mouse-release.

(define (current-mode)
  (cond
    ((not (eq? *algebra* 'complex)) 0)        ; float32 for ℍ / 𝕆
    (*julia-mode*
     (if (> *zoom* 1e6) 1 0))                 ; Julia: float32 or DD (no perturb)
    ((and *perturb* (> *zoom* 1e6) (not *is-dragging*)) 2)   ; perturbation
    ((> *zoom* 1e6) 1)                         ; DD
    (else 0)))                                 ; float32

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define *dummy-orbit-tex* #f)

(define (draw-frame painter w h)
  (let* ((cxhi *cx-hi*) (cxlo *cx-lo*)
         (cyhi *cy-hi*) (cylo *cy-lo*)
         (jxdd (dd-split *jcx*))
         (jydd (dd-split *jcy*))
         (mode (current-mode)))
    (when (= mode 2) (compute-reference-orbit!))
    (unless *dummy-orbit-tex*
      (set! *dummy-orbit-tex* (make-gl-texture (make-bytevector 64 0) 1 1 'rgba32f)))
    (gl-shader-draw! mandel-prog painter
      (list (cons "u_center"      (list *cx-hi* *cy-hi*))
            (cons "u_center_dd"   (list cxhi cxlo cyhi cylo))
            (cons "u_zoom"        *zoom*)
            (cons "u_max_iter"    *max-iter*)
            (cons "u_algebra"     (algebra->int *algebra*))
            (cons "u_theta"       *theta*)
            (cons "u_phi"         *phi*)
            (cons "u_palette"     *palette*)
            (cons "u_mode"        mode)
            (cons "u_julia"       (if *julia-mode* 1 0))
            (cons "u_julia_c"     (list *jcx* *jcy*))
            (cons "u_julia_c_dd"  (list (car jxdd) (cadr jxdd)
                                        (car jydd) (cadr jydd)))
            (cons "u_ref_orbit"   (if *orbit-tex* *orbit-tex* *dummy-orbit-tex*))
            (cons "u_ref_len"     (if (= mode 2) *orbit-len* 0)))))
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      (if *julia-mode* "Julia" (case *algebra* ((complex) "C  2D") ((quaternion) "H  4D") (else "O  8D")))
      "  iter=" (number->string *max-iter*)
      (let ((rec (recommended-iters)))
        (if (> rec *max-iter*) (string-append " (rec " (number->string rec) ")") ""))
      "  zoom×" (number->string (inexact->exact (round *zoom*)))
      (case (current-mode)
        ((2) "  PERTURB")
        ((1) "  DD")
        (else ""))
      (if *is-dragging* "  …" "")
      "  (" (coord->string *cx-hi*) ", " (coord->string *cy-hi*) "i)")))

;;; ── Navigation helpers ───────────────────────────────────────────────────────

;;; Set DD centre from a plain float64 (lo = 0).
(define (set-center! cx cy)
  (set! *cx-hi* (inexact cx)) (set! *cx-lo* 0.0)
  (set! *cy-hi* (inexact cy)) (set! *cy-lo* 0.0)
  (set! *orbit-cx-hi* #f))

(define (navigate-to! cx cy zoom)
  (set-center! cx cy)
  (set! *zoom* zoom)
  (canvas-redraw! *canvas*))

(define (reset-view!)
  (set-center! -0.5 0.0)
  (set! *zoom* 200.0)
  (set! *perturb* #t)
  (set! *julia-mode* #f)
  (canvas-redraw! *canvas*))

(define (zoom-step! factor)
  (set! *zoom* (* *zoom* factor))
  (canvas-redraw! *canvas*))

;;; Zoom centred on screen position (sx, sy), keeping that point fixed.
;;; The pan delta is exact float64 arithmetic; at realistic zoom levels the
;;; double-double addition absorbs it without precision loss.
(define (zoom-to-cursor! factor sx sy)
  (let* ((nz  (* *zoom* factor))
         ;; offset of cursor from canvas centre in fractal coords
         (wx  (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
         (wy  (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))
         ;; how much the centre must shift so the cursor stays on the same point
         (dx  (- wx (/ (- (inexact sx) (/ (inexact *W*) 2.0)) nz)))
         (dy  (- wy (/ (- (inexact sy) (/ (inexact *H*) 2.0)) nz)))
         (nx  (dd+f *cx-hi* *cx-lo* dx))
         (ny  (dd+f *cy-hi* *cy-lo* dy)))
    (set! *cx-hi* (car nx)) (set! *cx-lo* (cdr nx))
    (set! *cy-hi* (car ny)) (set! *cy-lo* (cdr ny))
    (set! *zoom* nz)
    (set! *orbit-cx-hi* #f)
    (canvas-redraw! *canvas*)))

;;; ── Input handlers ───────────────────────────────────────────────────────────

(define (on-mouse-press! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x*     x) (set! *drag-y*     y)
    (set! *drag-cx-hi* *cx-hi*) (set! *drag-cx-lo* *cx-lo*)
    (set! *drag-cy-hi* *cy-hi*) (set! *drag-cy-lo* *cy-lo*)
    (set! *is-dragging* #t)))

(define (on-mouse-release! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x* #f)
    (set! *is-dragging* #f)
    ;; Full-quality redraw after pan (triggers orbit recompute if needed)
    (canvas-redraw! *canvas*)))

(define (on-mouse-move! x y)
  (when *drag-x*
    ;; Pan delta relative to drag-start — accumulated into a DD centre.
    ;; Dividing by *zoom* is fine at float64: the drag pixel count is << 1e15.
    (let* ((dx (/ (- (inexact x) (inexact *drag-x*)) *zoom*))
           (dy (/ (- (inexact y) (inexact *drag-y*)) *zoom*))
           (nx (dd+f *drag-cx-hi* *drag-cx-lo* (- dx)))
           (ny (dd+f *drag-cy-hi* *drag-cy-lo* (- dy))))
      (set! *cx-hi* (car nx)) (set! *cx-lo* (cdr nx))
      (set! *cy-hi* (car ny)) (set! *cy-lo* (cdr ny))
      (set! *orbit-cx-hi* #f)
      (canvas-redraw! *canvas*))))

;;; ── Window ───────────────────────────────────────────────────────────────────

(define win     (make-window "Hypercomplex Mandelbrot" 1200 720))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))
(set! *canvas* canvas)

;;; ── Tab 1: Render ────────────────────────────────────────────────────────────

(define tab1 (make-vbox))
(define (t1! w) (layout-add! tab1 w))

(t1! (make-label "Algebra"))
(t1! (make-radio-group
       '("Complex  ℂ (d=2)" "Quaternion  ℍ (d=4)" "Octonion  𝕆 (d=8)") 0
       (lambda (i)
         (set! *algebra* (list-ref '(complex quaternion octonion) i))
         (reset-view!))))

(t1! (make-separator))
(t1! (make-label "Colour palette"))
(t1! (make-radio-group '("Cycle" "Ultra" "Ice" "Electric" "Gold") 0
       (lambda (i) (set! *palette* i) (canvas-redraw! *canvas*))))

(t1! (make-separator))
(t1! (make-label "Max iterations"))
(define iter-slider
  (make-slider "Iterations" 20 4000 10 150
    (lambda (v)
      (set! *max-iter* (inexact->exact v))
      (set! *orbit-iters* 0)
      (canvas-redraw! *canvas*))))
(t1! iter-slider)
(t1! (make-button "Set recommended"
       (lambda ()
         (let ((rec (recommended-iters)))
           (set! *max-iter* rec)
           (slider-set-value! iter-slider (inexact rec))
           (set! *orbit-iters* 0)
           (canvas-redraw! *canvas*)))))

(t1! (make-separator))
(t1! (make-label "Slice plane  (ℍ / 𝕆)"))
(t1! (make-slider "θ  e₀↔e₂" -100 100 1 0
       (lambda (v) (set! *theta* (* v 0.031416)) (canvas-redraw! *canvas*))))
(t1! (make-slider "φ  e₁↔e₃" -100 100 1 0
       (lambda (v) (set! *phi* (* v 0.031416)) (canvas-redraw! *canvas*))))

(t1! (make-separator))
(t1! (make-button "Perturbation  (P)"
       (lambda ()
         (set! *perturb* (not *perturb*))
         (when *perturb* (set! *orbit-cx-hi* #f))
         (canvas-redraw! *canvas*))))
(t1! (make-button "Reset view  (R)" reset-view!))

;;; ── Tab 2: Julia ─────────────────────────────────────────────────────────────

(define tab2 (make-vbox))
(define (t2! w) (layout-add! tab2 w))

(t2! (make-label "Julia set"))
(t2! (make-button "Toggle Julia  (J)"
       (lambda ()
         (set! *julia-mode* (not *julia-mode*))
         (when *julia-mode*
           ;; Reset to z-plane view centred at origin
           (set-center! 0.0 0.0)
           (set! *zoom* 200.0))
         (canvas-redraw! *canvas*))))

(t2! (make-separator))
(t2! (make-label "Julia parameter c"))
(define julia-presets
  (list
    (list "Dragon       c = -0.7 + 0.27i"    -0.7     0.27)
    (list "Rabbit       c =  0.285 + 0.01i"   0.285   0.01)
    (list "Dendrite     c = -0.4 + 0.6i"     -0.4     0.6)
    (list "San Marco    c = -0.75"            -0.75    0.0)
    (list "Douady       c = -0.123 + 0.745i" -0.123   0.745)
    (list "Siegel disk  c = -0.391 - 0.587i" -0.391  -0.587)
    (list "Airplane     c = -1.755"           -1.755   0.0)
    (list "Custom" #f #f)))

(define (preset-name p) (list-ref p 0))
(define (preset-cx p)   (list-ref p 1))
(define (preset-cy p)   (list-ref p 2))

(define jcx-slider #f)
(define jcy-slider #f)

(define julia-dropdown
  (make-dropdown (map preset-name julia-presets) 0
    (lambda (i)
      (let ((p (list-ref julia-presets i)))
        (when (preset-cx p)
          (set! *jcx* (preset-cx p))
          (set! *jcy* (preset-cy p))
          (when jcx-slider (slider-set-value! jcx-slider (* *jcx* 100.0)))
          (when jcy-slider (slider-set-value! jcy-slider (* *jcy* 100.0)))
          (canvas-redraw! *canvas*))))))
(t2! julia-dropdown)

(t2! (make-separator))
(t2! (make-label "Re(c)"))
(set! jcx-slider
  (make-slider "Re(c)" -200 200 1 (inexact->exact (round (* *jcx* 100)))
    (lambda (v)
      (set! *jcx* (/ v 100.0))
      (canvas-redraw! *canvas*))))
(t2! jcx-slider)

(t2! (make-label "Im(c)"))
(set! jcy-slider
  (make-slider "Im(c)" -200 200 1 (inexact->exact (round (* *jcy* 100)))
    (lambda (v)
      (set! *jcy* (/ v 100.0))
      (canvas-redraw! *canvas*))))
(t2! jcy-slider)

;;; ── Tab 3: Navigate ──────────────────────────────────────────────────────────

(define tab3 (make-vbox))
(define (t3! w) (layout-add! tab3 w))

(t3! (make-label "Preset locations"))
(define bm-dropdown
  (make-dropdown (map bookmark-name *bookmarks*) 0
    (lambda (i)
      (let ((bm (list-ref *bookmarks* i)))
        (navigate-to! (bookmark-cx bm) (bookmark-cy bm) (bookmark-zoom bm))))))
(t3! bm-dropdown)

(t3! (make-separator))
(t3! (make-label "Go to coordinates"))
(define cx-field   (make-text-input "Real (cx)"  (lambda (s) (void))))
(define cy-field   (make-text-input "Imag (cy)"  (lambda (s) (void))))
(define zoom-field (make-text-input "Zoom"       (lambda (s) (void))))
(t3! cx-field) (t3! cy-field) (t3! zoom-field)
(t3! (make-button "Go"
       (lambda ()
         (guard (e (#t #f))
           (let* ((cx   (string->number (text-value cx-field)))
                  (cy   (string->number (text-value cy-field)))
                  (zoom (string->number (text-value zoom-field))))
             (when (and cx cy zoom (> zoom 0))
               (navigate-to! (inexact cx) (inexact cy) (inexact zoom))))))))

(t3! (make-separator))
(t3! (make-label "Save current view"))
(define name-field (make-text-input "Name" (lambda (s) (void))))
(t3! name-field)
(t3! (make-button "Save bookmark"
       (lambda ()
         (let ((name (text-value name-field)))
           (when (> (string-length name) 0)
             (set! *bookmarks*
               (append *bookmarks* (list (list name *cx-hi* *cy-hi* *zoom*))))
             (save-bookmarks!)
             (dropdown-add-item! bm-dropdown name)
             (dropdown-set-index! bm-dropdown (- (dropdown-count bm-dropdown) 1))
             (text-set-value! name-field "")
             (display (string-append "Bookmark saved: " name "\n")))))))

(t3! (make-separator))
(t3! (make-label "Export"))
(t3! (make-button "Save PNG…"
       (lambda ()
         (let ((path (file-save-dialog "Save PNG" "Images (*.png)")))
           (when (and path (> (string-length path) 0))
             (let ((ok (canvas-save-png! canvas path)))
               (display (if ok (string-append "Saved: " path "\n")
                             "PNG save failed\n"))))))))

;;; ── Assemble tabs ────────────────────────────────────────────────────────────

(define tabs (make-tabs))
(tabs-add! tabs tab1 "Render")
(tabs-add! tabs tab2 "Julia")
(tabs-add! tabs tab3 "Navigate")
(box-add! sidebar tabs)

;;; ── Canvas event handlers ────────────────────────────────────────────────────

(canvas-on-draw! canvas
  (lambda (painter w h) (set! *W* w) (set! *H* h) (draw-frame painter w h)))

(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)        (on-mouse-press!   x y btn))
      ((equal? ev 'release)      (on-mouse-release! x y btn))
      ((or (equal? ev 'move)
           (equal? ev 'drag))    (on-mouse-move!    x y))
      ((equal? ev 'double-press)
       (when (equal? btn 'left)  (zoom-to-cursor! 2.0 x y))))))

(canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (zoom-to-cursor! (expt 1.002 dy) x y)))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")   (reset-view!))
      ((equal? key "p")
       (set! *perturb* (not *perturb*))
       (when *perturb* (set! *orbit-cx-hi* #f))
       (canvas-redraw! *canvas*))
      ((equal? key "j")
       (set! *julia-mode* (not *julia-mode*))
       (when *julia-mode*
         (set-center! 0.0 0.0)
         (set! *zoom* 200.0))
       (canvas-redraw! *canvas*))
      ((or (equal? key "=") (equal? key "+")) (zoom-step! 1.5))
      ((equal? key "-")                        (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q") (equal? key "Escape")) (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (unless (qt-gpu? canvas)
      (display "[mandelbrot] warning: OpenGL unavailable\n")
      (flush-output-port (current-output-port)))
    (canvas-redraw! canvas)))

(window-on-close! win (lambda () (quit-event-loop)))

(load-bookmarks! bm-dropdown)
(window-show! win)
(run-event-loop)
