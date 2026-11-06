#!/usr/bin/env curry
;;; spacetime-horizon -- falling through a black hole's horizon, drawn as spacetime
;;;
;;; The equatorial plane of a Kerr black hole (spin a = 0 is Schwarzschild),
;;; seen as a 2+1 dimensional spacetime: x and y across, time t upward.
;;; Everything is drawn from the metric, not from a picture of one:
;;;
;;;   * the light cones are built in the vertex shader from the GLSL that
;;;     (curry spacetime) generates for the CAS Kerr metric;
;;;   * the observer is a free-falling geodesic (kerr-slice.scm), E = 1;
;;;   * the equations pane evaluates the same formulas at the observer.
;;;
;;; Watch the cones.  Far away they stand upright.  Closer in they lean
;;; toward the hole.  At the event horizon (for no spin; with spin, at the
;;; static limit, for light aimed straight outward) the outer edge of the
;;; cone is vertical: light cannot move outward.  Inside, every direction in the
;;; cone points inward -- there is no way to stay at the same radius, for
;;; the same reason there is no way to stay at the same time.  With spin,
;;; the cones also lean sideways (frame dragging) before the horizon.
;;;
;;; Time here is Kerr-Schild time t = v - r, which is regular at the
;;; horizons.  For a distant observer it agrees with the usual clock.
;;;
;;;   ./build/curry examples/spacetime-horizon/main.scm
;;;   ... --spin 0.9 --tau 14 --snapshot out.png   (save one frame and exit)
;;;
;;; Mouse: drag to turn, scroll to zoom.  Space: play/pause.  R: restart.
;;; Left/Right: step.  Escape: quit.

(import (scheme base) (scheme inexact) (scheme write) (scheme process-context)
        (curry qt6) (curry gpu-math) (curry spacetime))

(load "kerr-slice.scm")

;;; ---- command line ----

(define (arg-value name default)
  (let loop ((args (command-line)))
    (cond ((or (null? args) (null? (cdr args))) default)
          ((string=? (car args) name) (cadr args))
          (else (loop (cdr args))))))

;; A number from the command line, clamped to [lo, hi]; the default if the
;; option is missing or is not a finite real number.
(define (num-arg name default lo hi)
  (let* ((str (arg-value name #f)) (v (and str (string->number str))))
    (if (and v (real? v) (not (nan? (exact->inexact v))) (not (infinite? (exact->inexact v))))
        (exact->inexact (max lo (min hi v)))
        default)))

(define snapshot (arg-value "--snapshot" #f))
(define start-spin (num-arg "--spin" 0.0 0.0 0.998))
(define start-tau  (let ((v (num-arg "--tau" -1.0 -1.0 1e6))) (and (>= v 0.0) v)))
(define start-L    (num-arg "--L" 0.0 -4.0 3.0))
(define start-slices
  (let ((v (num-arg "--slices" 1.0 1.0 5.0))) (cond ((< v 2.0) 1) ((< v 4.0) 3) (else 5))))

;;; ---- state ----

(define spin start-spin)
(define L-in start-L)
(define r0 10.0)
(define cone-h 1.3)
(define speed 1.0)                      ; proper time per second
(define slices start-slices)            ; time slices of cones: 1, 3 or 5
(define show-cones #t)
(define show-horizons #t)
(define show-grid #t)
(define show-drag #t)
(define playing (not start-tau))
(define tau-cur (or start-tau 0.0))

(define yaw (num-arg "--yaw" 0.55 -100.0 100.0))
(define pitch (num-arg "--pitch" 0.30 -1.5 1.5))
(define dist (num-arg "--dist" 30.0 6.0 90.0))
(define drag-from #f)

(define wl #f)                          ; full worldline
(define thin #f)                        ; thinned copy for drawing
(define tau-end 1.0)
(define wl-dirty #t)
(define wl-dirty-tick 0)           ; timer tick of the latest change
(define geom-dirty #t)

(define T-LO -14.0)
(define T-HI 90.0)
(define slice-dt 2.5)
(define tspan 12.0)

;;; ---- shaders ----

(define params-block "layout(std140, binding = 0) uniform Params {
  mat4 mvp; float tc; float spin; float cone_h; float tspan;
  float follow; float k0; float dts; float pad;
} u;\n")

(define line-vs (make-gpu-shader 'vertex (string-append "#version 440
layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 col;
layout(location = 0) out vec4 v_col;\n" params-block "
void main() {
  float t = pos.z + u.follow * u.tc;
  float fade = clamp(1.0 - abs(t - u.tc) / u.tspan, 0.0, 1.0);
  v_col = vec4(col.rgb, col.a * (0.22 + 0.78 * fade));
  gl_Position = u.mvp * vec4(pos.x, t - u.tc, -pos.y, 1.0);
}")))

(define frag (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 frag;
void main() { frag = v_col; }"))

;; One instance per cone: (x, y, slice, region).  Vertices 0..47 are the
;; generators (apex to rim), 48..95 the rim segments.
(define cone-vs (make-gpu-shader 'vertex (string-append "#version 440
layout(location = 0) in vec4 inst;
layout(location = 0) out vec4 v_col;\n" params-block
(metric->glsl (kerr-metric 1) '((a . "u.spin"))) "
const int NR = 24;
const float TAU2 = 6.28318530718;

// A point on the rim of the future light cone at (x, y), h above its apex.
vec3 cone_offset(vec2 xy, float al) {
  mat4 g, d0, d1, d2, d3;
  metric_eval(vec4(0.0, xy.x, xy.y, 0.0), g, d0, d1, d2, d3);
  mat3 g3 = mat3(g[0].xyz, g[1].xyz, g[2].xyz);       // (t, x, y) block
  mat3 gi = inverse(g3);
  vec3 e0 = -gi[0] / sqrt(-gi[0][0]);                 // along grad t: the cone axis
  vec3 v1 = vec3(0.0, 1.0, 0.0);
  v1 += dot(v1, g3 * e0) * e0;
  vec3 e1 = v1 / sqrt(dot(v1, g3 * v1));
  vec3 v2 = vec3(0.0, 0.0, 1.0);
  v2 += dot(v2, g3 * e0) * e0;
  v2 -= dot(v2, g3 * e1) * e1;
  vec3 e2 = v2 / sqrt(dot(v2, g3 * v2));
  vec3 k = e0 + cos(al) * e1 + sin(al) * e2;          // a null vector
  return vec3(u.cone_h * k.yz / k.x, u.cone_h);
}

void main() {
  int k = gl_VertexIndex;
  int seg = k / 2;
  int e = k - seg * 2;
  bool gen = seg < NR;
  int ri = gen ? seg : (seg - NR + e);
  ri = ri - (ri / NR) * NR;
  bool apex = gen && e == 0;
  bool me = inst.z > 50.0;
  float t0 = me ? u.tc : (u.k0 + inst.z) * u.dts;
  vec3 off = apex ? vec3(0.0) : cone_offset(inst.xy, TAU2 * float(ri) / float(NR));
  vec3 p = vec3(inst.xy + off.xy, t0 + off.z);
  vec3 c = inst.w < 0.5 ? vec3(0.30, 0.80, 1.00)
         : (inst.w < 1.5 ? vec3(1.00, 0.70, 0.20) : vec3(0.95, 0.30, 0.55));
  float fade = clamp(1.0 - abs(t0 - u.tc) / u.tspan, 0.0, 1.0);
  float al = me ? 1.0 : (gen ? 0.22 : 0.45) * (0.15 + 0.85 * fade);
  v_col = vec4(me ? vec3(1.0) : c, al);
  gl_Position = u.mvp * vec4(p.x, p.z - u.tc, -p.y, 1.0);
}")))

;;; ---- buffers and pipelines ----

(define STATIC-CAP 40000)
(define line-inputs '((28 (0 float3 0) (1 float4 12))))
(define static-buf (make-gpu-buffer 'vertex (* STATIC-CAP 28)))
(define grid-buf   (make-gpu-buffer 'vertex (* 4000 28)))
(define drag-buf   (make-gpu-buffer 'vertex (* 8000 28)))
(define wl-buf     (make-gpu-buffer 'vertex (* 5000 28)))
(define tick-buf   (make-gpu-buffer 'vertex (* 1200 28)))
(define marker-buf (make-gpu-buffer 'vertex (* 8 28)))
(define lattice-buf (make-gpu-buffer 'vertex (* 600 16)))
(define me-buf      (make-gpu-buffer 'vertex 16))

(define ubuf-abs (make-gpu-buffer 'uniform 96))
(define ubuf-flw (make-gpu-buffer 'uniform 96))
(define bind-abs (make-gpu-bindings (list (list 'uniform 0 '(vertex fragment) ubuf-abs))))
(define bind-flw (make-gpu-bindings (list (list 'uniform 0 '(vertex fragment) ubuf-flw))))

(define (line-pipeline topology)
  (make-gpu-pipeline 'vertex line-vs 'fragment frag 'bindings bind-abs
                     'inputs line-inputs 'topology topology
                     'depth-test #f 'depth-write #f 'blend 'additive))
(define lines-pipe (line-pipeline 'lines))
(define strip-pipe (line-pipeline 'line-strip))
(define cone-pipe
  (make-gpu-pipeline 'vertex cone-vs 'fragment frag 'bindings bind-abs
                     'inputs '((16 per-instance (0 float4 0)))
                     'topology 'lines 'depth-test #f 'depth-write #f 'blend 'additive))

;;; ---- geometry (rebuilt when the spin changes) ----

(define acc '())
(define nv 0)
(define (v! x y t r g b a)
  (set! acc (cons a (cons b (cons g (cons r (cons t (cons y (cons x acc))))))))
  (set! nv (+ nv 1)))
(define (seg! x1 y1 t1 x2 y2 t2 r g b a) (v! x1 y1 t1 r g b a) (v! x2 y2 t2 r g b a))
(define (take-geometry!)
  (let ((flat (reverse acc)) (n nv)) (set! acc '()) (set! nv 0) (cons flat n)))
(define TWO-PI 6.283185307179586)

(define (cylinder! rho r g b ring-a gen-a)
  (let ((ns 48) (ng 32))
    (do ((t T-LO (+ t 2.0))) ((> t T-HI))
      (do ((i 0 (+ i 1))) ((= i ns))
        (let ((a1 (* TWO-PI (/ i ns))) (a2 (* TWO-PI (/ (+ i 1) ns))))
          (seg! (* rho (cos a1)) (* rho (sin a1)) t (* rho (cos a2)) (* rho (sin a2)) t
                r g b ring-a))))
    (do ((i 0 (+ i 1))) ((= i ng))
      (let ((al (* TWO-PI (/ i ng))))
        (seg! (* rho (cos al)) (* rho (sin al)) T-LO (* rho (cos al)) (* rho (sin al)) T-HI
              r g b gen-a)))))

(define (build-static!)
  (set! acc '()) (set! nv 0)
  (let ((rp (ks-r-plus spin)) (rm (ks-r-minus spin)))
    (when show-horizons
      ;; event horizon (orange); for a = 0 it is also the static limit
      (cylinder! (ks-rho-of-r rp spin) 1.0 0.55 0.1 0.45 0.8)
      (when (> spin 0.0)
        ;; static limit / ergosphere r = 2 (teal) and inner horizon (violet)
        (cylinder! (ks-rho-of-r 2.0 spin) 0.1 0.8 0.7 0.28 0.5)
        (cylinder! (ks-rho-of-r rm spin) 0.7 0.4 1.0 0.40 0.7)))
    ;; the singularity: a ring of radius a (red cylinder), or the axis for a = 0
    (if (> spin 0.0)
        (cylinder! spin 1.0 0.15 0.15 0.6 1.0)
        (do ((i 0 (+ i 1))) ((= i 5))
          (let ((dx (* 0.04 (cos (* i 1.2566)))) (dy (* 0.04 (sin (* i 1.2566)))))
            (seg! dx dy T-LO dx dy T-HI 1.0 0.15 0.15 0.9)))))
  (let ((g (take-geometry!)))
    (when (> (cdr g) STATIC-CAP) (error "static geometry too large" (cdr g)))
    (gpu-buffer-update! static-buf (car g))
    (set! static-count (cdr g)))
  ;; the "now" slice: a polar grid that rides along with the observer's time
  (let ((radii '(0.5 1.0 1.5 2.0 3.0 4.0 5.5 7.5 10.0 13.0)) (ns 64))
    (for-each
     (lambda (r)
       (let ((rho (ks-rho-of-r r spin)))
         (do ((i 0 (+ i 1))) ((= i ns))
           (let ((a1 (* TWO-PI (/ i ns))) (a2 (* TWO-PI (/ (+ i 1) ns))))
             (seg! (* rho (cos a1)) (* rho (sin a1)) 0.0 (* rho (cos a2)) (* rho (sin a2)) 0.0
                   0.45 0.55 0.8 0.28)))))
     radii)
    (do ((i 0 (+ i 1))) ((= i 16))
      (let ((al (* TWO-PI (/ i 16))) (r-in (ks-rho-of-r 0.5 spin)) (r-out (ks-rho-of-r 13.0 spin)))
        (seg! (* r-in (cos al)) (* r-in (sin al)) 0.0 (* r-out (cos al)) (* r-out (sin al)) 0.0
              0.45 0.55 0.8 0.18))))
  (let ((g (take-geometry!)))
    (gpu-buffer-update! grid-buf (car g))
    (set! grid-count (cdr g)))
  ;; Frame dragging: at each radius, the path of a point carried round at the
  ;; angular velocity at which space is dragged there (a locally non-rotating
  ;; observer): w(r) = 2a / (r^3 + a^2 r + 2 a^2), M = 1.  Straight vertical
  ;; lines for a = 0; the twist grows with the spin and toward the hole.
  (for-each
   (lambda (r)
     (let ((w (/ (* 2.0 spin) (+ (* r r r) (* spin spin r) (* 2.0 spin spin)))))
       (do ((ph 0 (+ ph 1))) ((= ph 3))
         (let ((phi0 (* TWO-PI (/ ph 3))))
           (do ((t T-LO (+ t 0.5))) ((>= t T-HI))
             (let* ((p1 (+ phi0 (* w t))) (p2 (+ phi0 (* w (+ t 0.5)))))
               (seg! (- (* r (cos p1)) (* spin (sin p1))) (+ (* r (sin p1)) (* spin (cos p1))) t
                     (- (* r (cos p2)) (* spin (sin p2))) (+ (* r (sin p2)) (* spin (cos p2))) (+ t 0.5)
                     0.85 0.7 1.0 0.55)))))))
   '(1.2 2.0 3.0 4.5 7.0))
  (let ((g (take-geometry!)))
    (gpu-buffer-update! drag-buf (car g))
    (set! drag-count (cdr g)))
  ;; cone lattice: (x y slice region) for each place, each slice
  (let* ((radii '(0.7 1.2 1.8 2.6 3.8 5.5 8.0))
         (azimuths 6)
         (js (cond ((= slices 1) '(0)) ((= slices 5) '(-2 -1 0 1 2)) (else '(-1 0 1))))
         (out '()) (n 0))
    (for-each
     (lambda (r)
       (let ((rho (ks-rho-of-r r spin))
             (region (cond ((> r (ks-r-plus spin)) 0.0) ((> r (ks-r-minus spin)) 1.0) (else 2.0)))
             (twist (* 0.37 r)))                  ; stagger the rings so cones do not line up
         (do ((i 0 (+ i 1))) ((= i azimuths))
           (let ((al (+ twist (* TWO-PI (/ i azimuths)))))
             (for-each
              (lambda (j)
                (set! out (cons region (cons (exact->inexact j)
                                             (cons (* rho (sin al)) (cons (* rho (cos al)) out)))))
                (set! n (+ n 1)))
              js)))))
     radii)
    (gpu-buffer-update! lattice-buf (reverse out))
    (set! lattice-count n))
  (set! geom-dirty #f))

(define static-count 0)
(define grid-count 0)
(define drag-count 0)
(define lattice-count 0)

;;; ---- the worldline ----

(define wl-count 0)
(define tick-count 0)
(define thin-taus #f)

(define (region-colour r)
  (cond ((> r (ks-r-plus spin)) '(0.55 0.9 1.0))
        ((> r (ks-r-minus spin)) '(1.0 0.8 0.35))
        (else '(1.0 0.4 0.55))))

(define (rebuild-worldline!)
  (set! wl (ks-worldline spin L-in r0))
  (set! tau-end (vector-ref (vector-ref wl (- (vector-length wl) 1)) 0))
  (set! tau-cur (min tau-cur tau-end))
  ;; thin to at most ~4500 vertices, at least 0.02 of proper time apart
  (let* ((n (vector-length wl))
         (step (max 0.02 (/ tau-end 4000.0)))
         (kept (let loop ((i 1) (last-tau 0.0) (kept (list (vector-ref wl 0))))
                 (cond ((= i n) (reverse (if (eq? (car kept) (vector-ref wl (- n 1)))
                                             kept (cons (vector-ref wl (- n 1)) kept))))
                       ((>= (- (vector-ref (vector-ref wl i) 0) last-tau) step)
                        (loop (+ i 1) (vector-ref (vector-ref wl i) 0)
                              (cons (vector-ref wl i) kept)))
                       (else (loop (+ i 1) last-tau kept))))))
    (set! thin (list->vector kept)))
  (set! thin-taus (vector-map (lambda (s) (vector-ref s 0)) thin))
  (set! acc '()) (set! nv 0)
  (vector-for-each
   (lambda (s)
     (let ((c (region-colour (vector-ref s 4))))
       (v! (vector-ref s 2) (vector-ref s 3) (vector-ref s 1) (car c) (cadr c) (caddr c) 0.9)))
   thin)
  (let ((g (take-geometry!)))
    (gpu-buffer-update! wl-buf (car g))
    (set! wl-count (cdr g)))
  ;; a cross at every unit of the observer's own proper time
  (do ((k 1 (+ k 1))) ((> k (min 200 (exact (floor tau-end)))))
    (let* ((s (ks-sample-at wl (exact->inexact k)))
           (x (vector-ref s 2)) (y (vector-ref s 3)) (t (vector-ref s 1)) (d 0.16))
      (seg! (- x d) y t (+ x d) y t 1.0 0.95 0.3 0.9)
      (seg! x (- y d) t x (+ y d) t 1.0 0.95 0.3 0.9)
      (seg! x y (- t d) x y (+ t d) 1.0 0.95 0.3 0.9)))
  (let ((g (take-geometry!)))
    (gpu-buffer-update! tick-buf (car g))
    (set! tick-count (cdr g)))
  (set! wl-dirty #f))

(define (index-at-tau tau)              ; number of thinned vertices with tau_i <= tau
  (let loop ((lo 0) (hi (vector-length thin-taus)))
    (if (>= lo hi) lo
        (let ((mid (quotient (+ lo hi) 2)))
          (if (<= (vector-ref thin-taus mid) tau) (loop (+ mid 1) hi) (loop lo mid))))))

;;; ---- the equations pane ----

(define (fix x n)
  (cond ((or (nan? x) (infinite? x)) "—")
        ((> (abs x) 1e9) (if (< x 0) "−big" "big"))
        (else
         (let* ((m (expt 10 n)) (v (exact (round (* (abs x) m))))
                (ip (quotient v m)) (fs (number->string (remainder v m))))
           (string-append (if (and (< x 0) (> v 0)) "−" "") (number->string ip)
                          (if (> n 0)
                              (string-append "." (make-string (- n (string-length fs)) #\0) fs)
                              ""))))))

(define (span colour text) (string-append "<span style='color:" colour "'>" text "</span>"))
(define C-OUT "#6fd0ff") (define C-MID "#ffc040") (define C-IN "#ff6a8c")
(define C-DIM "#8a93a8") (define C-HI "#ffffff")

(define (pad-right s n)
  (if (>= (string-length s) n) s (string-append s (make-string (- n (string-length s)) #\space))))

(define (region-label r)
  (cond ((> r (ks-r-plus spin)) (span C-OUT "outside the event horizon"))
        ((= spin 0.0) (span C-MID "inside the horizon"))
        ((> r (ks-r-minus spin)) (span C-MID "between the horizons"))
        (else (span C-IN "inside the inner horizon"))))

(define (equations-html s)
  (let* ((tau (vector-ref s 0)) (t (vector-ref s 1)) (x (vector-ref s 2)) (y (vector-ref s 3))
         (r (vector-ref s 4)) (rd (vector-ref s 5)) (td (vector-ref s 6))
         (a spin) (rp (ks-r-plus a)) (rm (ks-r-minus a))
         (f (/ 2.0 r)) (gtt (- f 1.0)) (delta (+ (- (* r r) (* 2.0 r)) (* a a)))
         (sp (ks-radial-light-speeds x y a))
         (col (cond ((> r rp) C-OUT) ((> r rm) C-MID) (else C-IN))))
    (string-append
     "<pre style='font-family:Menlo,monospace; font-size:12px; color:#d8dde8'>"
     (span C-HI "<b>Metric</b>") " (ingoing Kerr–Schild, equator, M = 1)\n"
     "  g = η + f l⊗l       f = 2/r\n"
     "  r² = x² + y² − a²\n"
     "  ds² = −dt² + dx² + dy² + f (dt + l·dx)²\n\n"
     (span C-HI "<b>Black hole</b>") "\n"
     "  spin    a  = " (fix a 3) "\n"
     "  r₊ = 1+√(1−a²) = " (fix rp 3) "   " (span "#ff8c1a" "event horizon") "\n"
     "  r₋ = 1−√(1−a²) = " (fix rm 3) "   " (span "#b366ff" "inner horizon") "\n"
     "  r_E = 2          = 2.000   " (span "#1acdb3" "static limit") "\n"
     "  ring singularity at ρ = a, r = 0\n\n"
     (span C-HI "<b>Observer</b>") "   E = 1,  L = " (fix (min L-in (ks-max-L a)) 2) "\n"
     "  proper time  τ  = " (span C-HI (fix tau 2)) "\n"
     "  far time     t  = " (fix t 2) "\n"
     "  radius       r  = " (span col (fix r 3)) "   " (region-label r) "\n"
     "  dr/dτ           = " (fix rd 3) "\n"
     "  dτ/dt           = " (fix (/ 1.0 td) 3) "   (the observer's clock rate)\n\n"
     (span C-HI "<b>What the metric says here</b>") "\n"
     "  f = 2/r             = " (fix f 3) "\n"
     "  g_tt = −1 + f       = " (span (if (< gtt 0.0) C-OUT C-IN) (fix gtt 3)) "\n"
     "    " (if (< gtt 0.0)
               (span C-OUT "∂t is timelike: a clock can stay at rest here")
               (span C-IN "∂t is spacelike: nothing can stay at rest here")) "\n"
     "  Δ = r² − 2r + a²   = " (span (if (< delta 0.0) C-MID C-OUT) (fix delta 3)) "\n"
     "    " (if (< delta 0.0)
               (span C-MID "Δ < 0: r is timelike, it can only decrease")
               (span C-OUT "Δ > 0: r is a space direction")) "\n"
     "  light, outward dρ/dt = "
     (if sp (span (if (> (car sp) 0.0) C-OUT C-IN) (fix (car sp) 3)) "—") "\n"
     "  light, inward  dρ/dt = " (if sp (fix (cdr sp) 3) "—") "\n"
     "    " (cond ((and sp (> (car sp) 0.0)) (span C-OUT "light can still climb outward"))
                   ((and (> a 0.0) (> r rp))
                    (span C-MID "inside the static limit: radial light moves inward;\n    light aimed sideways can still escape"))
                   (else (span C-IN "even outward-aimed light moves inward"))) "\n"
     "</pre>")))

;;; ---- how the fall ends ----

(define (ending-message)
  (let* ((last (vector-ref wl (- (vector-length wl) 1)))
         (tau (vector-ref last 0)) (r (vector-ref last 4)))
    (cond ((< r 0.06)
           (string-append (if (> spin 0.0) "Reached the ring singularity" "Reached the singularity (r = 0)")
                          " at τ = " (fix tau 2) ". Nothing continues past this point."))
          ((and (> spin 0.0) (> r (* 0.9 (ks-r-minus spin))) (< r (ks-r-plus spin)))
           (string-append "Turned around inside the horizons at τ = " (fix tau 2)
                          " and is heading back out through the inner horizon. These coordinates"
                          " do not cover where it goes next, so the worldline stops here."))
          ((> r (* 2.9 r0))
           (string-append "Swung around the hole and climbed back out (τ = " (fix tau 2) ")."))
          (else (string-append "Stopped at τ = " (fix tau 2)
                               " (the limit of the computed worldline).")))))

;;; ---- widgets ----

(define eq-label (make-label ""))
(define banner (make-label ""))
(widget-set-style! banner "background:#3a1020; color:#ffd8e0; padding:8px; font-size:14px;")
(widget-set-visible! banner #f)
(define loop? #f)
(define at-end #f)
(define end-tick #f)                    ; tick at which the fall reached its end
(define scrub #f)
(define ignore-scrub #f)

(define ticks 0)
(define (mark-wl-dirty!) (set! wl-dirty #t) (set! wl-dirty-tick ticks))

(define spin-slider
  (make-slider "spin  a" 0.0 0.998 0.002 start-spin
               (lambda (v) (set! spin v) (set! geom-dirty #t) (mark-wl-dirty!))))
(define r0-slider
  (make-slider "start radius  r₀" 4.0 16.0 0.5 r0
               (lambda (v) (set! r0 v) (mark-wl-dirty!))))
(define L-slider
  (make-slider "angular momentum  L" -4.0 3.0 0.05 start-L
               (lambda (v) (set! L-in v) (mark-wl-dirty!))))
(define cone-slider
  (make-slider "cone height" 0.5 3.0 0.1 cone-h (lambda (v) (set! cone-h v))))
(define speed-slider
  (make-slider "playback speed" 0.1 4.0 0.1 speed (lambda (v) (set! speed v))))
(set! scrub
  (make-slider "proper time  τ" 0.0 1000.0 1.0 0.0
               (lambda (v)
                 (unless ignore-scrub
                   (set! playing #f)
                   (set! tau-cur (* tau-end (/ v 1000.0)))))))

(define play-button
  (make-button "Play / Pause" (lambda () (set! playing (not playing)))))
(define restart-button
  (make-button "Restart" (lambda () (set! tau-cur 0.0) (set! playing #t))))

(define slices-dd
  (make-dropdown '("1 time slice" "3 time slices" "5 time slices")
                 (cond ((= start-slices 1) 0) ((= start-slices 5) 2) (else 1))
                 (lambda (i) (set! slices (case i ((0) 1) ((2) 5) (else 3))) (set! geom-dirty #t))))
(define loop-toggle (make-toggle "loop" #f (lambda (on) (set! loop? on))))
(define cones-toggle (make-toggle "light cones" #t (lambda (on) (set! show-cones on))))
(define horizons-toggle
  (make-toggle "horizons and singularity" #t (lambda (on) (set! show-horizons on) (set! geom-dirty #t))))
(define grid-toggle (make-toggle "\"now\" slice grid" #t (lambda (on) (set! show-grid on))))
(define drag-toggle (make-toggle "frame-dragging lines" #t (lambda (on) (set! show-drag on))))

(define legend
  (make-label
   (string-append
    "<span style='color:#6fd0ff'>cyan cones</span>: outside the horizon<br>"
    "<span style='color:#ffc040'>amber cones</span>: between the horizons<br>"
    "<span style='color:#ff6a8c'>rose cones</span>: inside the inner horizon<br>"
    "white cone: the observer's<br>"
    "<span style='color:#ff8c1a'>orange</span>: event horizon<br>"
    "<span style='color:#b366ff'>violet</span>: inner horizon<br>"
    "<span style='color:#1acdb3'>teal</span>: static limit<br>"
    "<span style='color:#ff3030'>red</span>: singularity<br>"
    "<span style='color:#d9b3ff'>lilac lines</span>: how space is dragged round by the spin (straight when a = 0)<br>"
    "<span style='color:#ffee50'>yellow crosses</span>: one tick of the observer's own clock<br><br>"
    "Up is time.  The horizontal plane is the equator of the black hole.")))

(define controls (make-scroll-area))
(for-each (lambda (w) (layout-add! controls w))
          (list (make-label "<b>Black hole</b>") spin-slider
                (make-label "<b>Observer</b>") r0-slider L-slider
                (make-separator)
                (make-label "<b>Time</b>") scrub play-button restart-button loop-toggle speed-slider
                (make-separator)
                (make-label "<b>View</b>") cone-slider slices-dd cones-toggle horizons-toggle grid-toggle drag-toggle
                (make-separator) legend))

(define eq-pane (make-scroll-area))
(layout-add! eq-pane eq-label)

;;; ---- the canvas ----

(define canvas (make-gpu-canvas 'samples 4))

(define (params-bytes mvp follow tc)
  (gpu-std140 (list (list 'mat4 mvp) (list 'float tc) (list 'float spin) (list 'float cone-h)
                    (list 'float tspan) (list 'float follow)
                    (list 'float (floor (/ tc slice-dt))) (list 'float slice-dt) (list 'float 0.0))))

(define (current-sample) (ks-sample-at wl tau-cur))

(gpu-canvas-on-render! canvas
  (lambda (frame w h)
    (when geom-dirty (build-static!))
    (let* ((s (current-sample))
           (tc (vector-ref s 1)) (x (vector-ref s 2)) (y (vector-ref s 3)) (r (vector-ref s 4))
           (region (cond ((> r (ks-r-plus spin)) 0.0) ((> r (ks-r-minus spin)) 1.0) (else 2.0)))
           (eye (list (* dist (cos pitch) (sin yaw)) (* dist (sin pitch)) (* dist (cos pitch) (cos yaw))))
           (proj (mat4* (gpu-clip-space-matrix frame)
                        (mat4-perspective (deg->rad 40) (/ w (max h 1)) 0.5 400.0)))
           (mvp (mat4* proj (mat4-look-at eye '(0.0 0.0 0.0) '(0.0 1.0 0.0))))
           (d 0.22)
           (k (index-at-tau tau-cur)))
      (gpu-buffer-update! ubuf-abs (params-bytes mvp 0.0 tc))
      (gpu-buffer-update! ubuf-flw (params-bytes mvp 1.0 tc))
      (gpu-buffer-update! me-buf (list x y 99.0 region))
      (gpu-buffer-update! marker-buf
        (list (- x d) y tc 1.0 1.0 1.0 1.0  (+ x d) y tc 1.0 1.0 1.0 1.0
              x (- y d) tc 1.0 1.0 1.0 1.0  x (+ y d) tc 1.0 1.0 1.0 1.0
              x y (- tc d) 1.0 1.0 1.0 1.0  x y (+ tc d) 1.0 1.0 1.0 1.0))
      (gpu-render-pass! frame
        (lambda (pass)
          (when show-grid
            (gpu-draw! pass lines-pipe grid-count 'vertices grid-buf 'bindings bind-flw))
          (when show-drag (gpu-draw! pass lines-pipe drag-count 'vertices drag-buf))
          (gpu-draw! pass lines-pipe static-count 'vertices static-buf)
          (gpu-draw! pass strip-pipe wl-count 'vertices wl-buf)
          (when (> k 1)                 ; the part already travelled, drawn twice: brighter
            (gpu-draw! pass strip-pipe k 'vertices wl-buf)
            (gpu-draw! pass strip-pipe k 'vertices wl-buf))
          (gpu-draw! pass lines-pipe tick-count 'vertices tick-buf)
          (gpu-draw! pass lines-pipe 6 'vertices marker-buf)
          (when show-cones
            (gpu-draw! pass cone-pipe 96 'vertices lattice-buf 'instances lattice-count))
          (gpu-draw! pass cone-pipe 96 'vertices me-buf 'instances 1))
        'clear '(0.02 0.025 0.04 1.0)))))

(gpu-canvas-on-mouse! canvas
  (lambda (event button x y mods)
    (case event
      ((press) (set! drag-from (cons x y)))
      ((release) (set! drag-from #f))
      ((drag)
       (when drag-from
         (set! yaw (- yaw (* 0.008 (- x (car drag-from)))))
         (set! pitch (max -1.5 (min 1.5 (+ pitch (* 0.008 (- y (cdr drag-from)))))))
         (set! drag-from (cons x y)))))))

(gpu-canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (set! dist (max 6.0 (min 90.0 (- dist (* 0.02 dy dist)))))))

(gpu-canvas-on-key! canvas
  (lambda (key mods)
    (cond ((string=? key "Escape") (quit-event-loop))
          ((string=? key "space") (set! playing (not playing)))
          ((string=? key "r") (set! tau-cur 0.0) (set! playing #t))
          ((string=? key "Left") (set! playing #f) (set! tau-cur (max 0.0 (- tau-cur 0.1))))
          ((string=? key "Right") (set! playing #f) (set! tau-cur (min tau-end (+ tau-cur 0.1)))))))

;;; ---- window ----

(define split (make-splitter 'horizontal))
(splitter-add! split controls)
(define centre (make-vbox))
(layout-add! centre banner)
(layout-add! centre canvas)
(splitter-add! split centre)
(splitter-add! split eq-pane)
(splitter-set-sizes! split '(270 800 430))

(define win (make-plain-window "Spacetime near a black hole" 1520 880))
(window-set-central-widget! win split)
(window-on-close! win quit-event-loop)

(rebuild-worldline!)
(label-set-text! eq-label (equations-html (current-sample)))

(define timer
  (make-timer/dt 16
    (lambda (dt)
      (set! ticks (+ ticks 1))
      ;; wait until the slider has been still for ~130 ms: a long worldline
      ;; is thousands of interpreted RK4 steps
      (when (and wl-dirty (>= (- ticks wl-dirty-tick) 8)) (rebuild-worldline!))
      (when playing
        (set! tau-cur (+ tau-cur (* speed (/ dt 1000.0))))
        (when (>= tau-cur tau-end) (set! tau-cur tau-end) (set! playing #f)))
      (let ((now-end (>= tau-cur (- tau-end 1e-9))))
        (when (and now-end (not at-end)) (set! end-tick ticks))
        (set! at-end now-end))
      ;; looping: rest on the last frame for about 1.5 s, then start again
      (when (and loop? at-end end-tick (>= (- ticks end-tick) 90))
        (set! tau-cur 0.0) (set! playing #t) (set! at-end #f))
      (when (= (remainder ticks 5) 0)
        (label-set-text! eq-label (equations-html (current-sample)))
        (if at-end
            (begin (label-set-text! banner (string-append "<b>" (ending-message) "</b>"))
                   (widget-set-visible! banner #t))
            (widget-set-visible! banner #f)))
      (when (and playing (= (remainder ticks 3) 0))
        (set! ignore-scrub #t)
        (slider-set-value! scrub (* 1000.0 (/ tau-cur tau-end)))
        (set! ignore-scrub #f))
      (gpu-canvas-redraw! canvas)
      (when (and snapshot (= ticks 30))
        (display (equations-html (current-sample))) (newline)
        (display (if (gpu-canvas-save-png! canvas snapshot) "saved " "could not save "))
        (display snapshot) (newline)
        (quit-event-loop)))))

(window-show! win)
(gpu-canvas-focus! canvas)
(timer/dt-start! timer)
(run-event-loop)
