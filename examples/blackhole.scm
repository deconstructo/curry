#!/usr/bin/env curry
;;; blackhole.scm — fall into a rotating black hole, rendered on the GPU
;;;
;;; The spacetime is the Kerr metric, written symbolically in (curry
;;; spacetime).  The CAS derives its derivatives, (curry glsl) turns them
;;; into GLSL, and every pixel traces one light ray backwards through the
;;; curved spacetime on the GPU.  The camera itself is a freely falling
;;; observer, moved along its own geodesic by the same generated code, so
;;; it falls through the event horizon smoothly (Kerr–Schild coordinates
;;; have no singularity there).
;;;
;;; What you see is computed, not painted:
;;;   - the sky and the disk are bent by gravitational lensing; rays that
;;;     cross the disk plane more than once show the far side of the disk
;;;     lifted over the hole, and a thin bright ring hugs the shadow
;;;   - the disk is a thin Novikov–Thorne-like disk; each point's light is
;;;     shifted by g = (p·u_observer) / (p·u_emitter), which includes
;;;     gravitational redshift, Doppler shift, and the camera's own motion:
;;;     brightness scales as g^4 and colour temperature as g
;;;   - the sky is shifted by the camera's motion the same way
;;;
;;;   ./build/curry examples/blackhole.scm
;;;   ./build/curry examples/blackhole.scm --anaglyph       ; red/cyan 3D
;;;   ./build/curry examples/blackhole-anaglyph.scm         ; same thing
;;;
;;; Keys: Space fall/pause · R reset · S spin (0, 0.5, 0.9, 0.998)
;;;       A anaglyph on/off · D disk on/off · arrows or mouse drag look
;;;       + / - fall speed · [ / ] exposure · Q or Escape quit
;;;
;;; Other options: --start-r R (starting distance, default 30 M)
;;;                --spin a  · --snapshot PATH [--snapshot-after SECONDS]
;;;
;;; Units: M = 1 (G = c = 1).  Distances below are in units of M.

(import (scheme base) (scheme inexact) (scheme write) (scheme process-context)
        (curry qt6) (curry spacetime) (curry f64vector))

;;; ---- options ---------------------------------------------------------------

(define args (command-line))
(define (option name default convert)
  (let loop ((a args))
    (cond ((or (null? a) (null? (cdr a))) default)
          ((string=? (car a) name) (convert (cadr a)))
          (else (loop (cdr a))))))
(define (flag? name) (and (member name args) #t))

(define start-anaglyph
  (or (flag? "--anaglyph")
      ;; set by examples/blackhole-anaglyph.scm before it loads this file
      (guard (e (#t #f)) blackhole-start-anaglyph)))
(define start-r (option "--start-r" 30.0 string->number))
(define snapshot (option "--snapshot" #f (lambda (s) s)))
(define snapshot-after (option "--snapshot-after" 1.5 string->number))
(define spins '(0.0 0.5 0.9 0.998))
(define spin (option "--spin" 0.9 string->number))
(define start-fall-speed (option "--fall-speed" 5.0 string->number))
;; start this far above the disk plane, so the fall passes over the disk
(define start-elevation (* (option "--elevation" 25.0 string->number) (/ 3.14159265 180)))

;;; ---- GLSL generated from the symbolic Kerr metric ----------------------------

(define kerr-glsl
  (string-append (metric->glsl (kerr-metric) '((M . "mass") (a . "spin")))
                 (geodesic-glsl)))

(define common-glsl "
// Kerr–Schild radial coordinate r (Boyer–Lindquist r) of a Cartesian point
float r_ks(vec3 p, float a) {
    float w = 0.5 * (dot(p, p) - a * a);
    return sqrt(w + sqrt(w * w + a * a * p.z * p.z));
}
float mdot(mat4 g, vec4 a, vec4 b) { return dot(a, g * b); }
vec4 orthonormal(mat4 g, vec4 u, vec4 v, vec4 a, vec4 b) {
    // remove components along u (timelike, u.u = -1) and unit spacelike a, b
    v += mdot(g, v, u) * u;
    v -= mdot(g, v, a) * a;
    v -= mdot(g, v, b) * b;
    return v / sqrt(max(mdot(g, v, v), 1e-20));
}
")

;;; ---- camera: one thread, advances the observer and builds its frame ---------

(define camera-shader (make-gpu-shader 'compute (string-append "#version 440
layout(local_size_x = 1) in;
layout(std140, binding = 0) uniform CamParams {
    float dtau; float mass; float spin; float yaw;
    float pitch; int init; int falling; int pad0;
    vec4 X0;
};
// X, P (covariant momentum), u = e0, forward, right, up, info
layout(std430, binding = 1) buffer Cam { vec4 cam[7]; };
" kerr-glsl common-glsl "
void main() {
    vec4 X = cam[0], P = cam[1];
    float tau = cam[6].y;
    mat4 g, d0, d1, d2, d3;
    if (init != 0) {
        X = X0;
        metric_eval(X, g, d0, d1, d2, d3);
        // start at rest: static observer if g_tt < 0 (outside the ergosphere)
        vec4 u = g[0][0] < 0.0 ? vec4(1.0 / sqrt(-g[0][0]), 0.0, 0.0, 0.0) : vec4(1.0, 0.0, 0.0, 0.0);
        P = g * u;
        tau = 0.0;
    } else if (falling != 0 && cam[6].w == 0.0) {
        // Stop at the inner (Cauchy) horizon r- (at the singularity r = 0
        // for a = 0): past r- the Kerr solution is not considered physical,
        // and these Cartesian coordinates are discontinuous across the disk
        // bounded by the ring singularity.
        float rminus = mass - sqrt(max(mass * mass - spin * spin, 0.0));
        float rstop = max(1.02 * rminus, 0.06);
        const int N = 24;
        for (int i = 0; i < N; i++) {
            float r = r_ks(X.yzw, spin);
            if (r < rstop) break;
            vec4 Xn = X, Pn = P;
            float h = min(dtau / float(N), 0.02 * r);
            geodesic_rk4(Xn, Pn, h);
            if (any(isnan(Xn)) || any(isnan(Pn))) break;
            X = Xn; P = Pn;
            tau += h;
        }
    }
    metric_eval(X, g, d0, d1, d2, d3);
    vec4 u = inverse(g) * P;
    u /= sqrt(max(-mdot(g, u, u), 1e-20));

    // reference axes: toward the hole, world up = +z
    vec3 to_hole = -normalize(X.yzw);
    vec3 up3 = abs(to_hole.z) > 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right3 = normalize(cross(to_hole, up3));
    up3 = cross(right3, to_hole);
    vec3 f3 = cos(pitch) * (cos(yaw) * to_hole + sin(yaw) * right3) + sin(pitch) * up3;
    vec3 r3 = normalize(cross(f3, up3));
    vec3 u3 = cross(r3, f3);

    vec4 zero = vec4(0.0);
    vec4 ef = orthonormal(g, u, vec4(0.0, f3), zero, zero);
    vec4 er = orthonormal(g, u, vec4(0.0, r3), ef, zero);
    vec4 eu = orthonormal(g, u, vec4(0.0, u3), ef, er);

    float r = r_ks(X.yzw, spin);
    float rplus = mass + sqrt(max(mass * mass - spin * spin, 0.0));
    float rminus = mass - sqrt(max(mass * mass - spin * spin, 0.0));
    // info: r, proper time, inside the event horizon?, stopped at r-?
    float stopped = (init == 0 && r < max(1.02 * rminus, 0.06) * 1.0001) ? 1.0 : 0.0;
    cam[0] = X; cam[1] = P; cam[2] = u; cam[3] = ef; cam[4] = er; cam[5] = eu;
    cam[6] = vec4(r, tau, r < rplus ? 1.0 : 0.0, stopped);
}")))

;;; ---- ray tracer: one thread per pixel ----------------------------------------

(define trace-shader (make-gpu-shader 'compute (string-append "#version 440
layout(local_size_x = 8, local_size_y = 8) in;
layout(std140, binding = 0) uniform TraceParams {
    float mass; float spin; float disk_in; float disk_out;
    float tan_half_fov; float aspect; float eye; int show_disk;
    ivec2 size;
};
layout(std430, binding = 1) readonly buffer Cam { vec4 cam[7]; };
layout(rgba16f, binding = 2) uniform writeonly image2D img;
" kerr-glsl common-glsl "
float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.13));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Approximate colour of a black body at T kelvin (Tanner Helland's fit), linear RGB.
vec3 blackbody(float T) {
    float t = clamp(T, 1000.0, 40000.0) / 100.0;
    float r = t <= 66.0 ? 255.0 : 329.698727446 * pow(t - 60.0, -0.1332047592);
    float g = t <= 66.0 ? 99.4708025861 * log(t) - 161.1195681661
                        : 288.1221695283 * pow(t - 60.0, -0.0755148492);
    float b = t >= 66.0 ? 255.0 : (t <= 19.0 ? 0.0 : 138.5177312231 * log(t - 10.0) - 305.0447927307);
    vec3 c = clamp(vec3(r, g, b) / 255.0, 0.0, 1.0);
    return c * c;   // roughly sRGB -> linear
}

// Celestial sphere: stars (with their own temperatures) and a faint grid,
// as seen with frequency shift gs.
vec3 sky(vec3 dir, float gs) {
    float th = acos(clamp(dir.z, -1.0, 1.0));
    float ph = atan(dir.y, dir.x);
    vec3 col = vec3(0.0);
    // grid every 15 degrees
    float gth = abs(fract(th / 0.2617994 + 0.5) - 0.5);
    float gph = abs(fract(ph / 0.2617994 + 0.5) - 0.5) * sin(th);
    float line = smoothstep(0.03, 0.0, min(gth, gph));
    col += line * vec3(0.05, 0.06, 0.09);
    // a band of faint light along the 'galactic' plane y = 0
    col += vec3(0.025, 0.02, 0.03) * exp(-dir.y * dir.y * 30.0);
    // stars on a lattice of cells
    vec3 cell = floor(dir * 140.0);
    float h = hash(cell);
    if (h > 0.985) {
        vec3 c = (cell + 0.5) / 140.0;
        float d = length(dir * 140.0 - (cell + 0.5));
        float T = mix(3000.0, 12000.0, hash(cell + 7.0));
        col += blackbody(T * gs) * smoothstep(0.45, 0.0, d) * (h - 0.985) * 400.0;
    }
    return col * clamp(gs * gs * gs, 0.0, 30.0);
}

void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    if (pix.x >= size.x || pix.y >= size.y) return;

    vec4 X = cam[0], u_obs = cam[2], ef = cam[3], er = cam[4], eu = cam[5];
    mat4 g, d0, d1, d2, d3;
    if (eye != 0.0) {
        // anaglyph: each eye a little to the side, along the camera's own
        // 'right'.  The camera's frame is orthonormal for the metric at
        // the camera, not at the eye, so rebuild it there -- otherwise the
        // rays below are not null and every star gets a false parallax.
        X += eye * er;
        metric_eval(X, g, d0, d1, d2, d3);
        u_obs /= sqrt(max(-mdot(g, u_obs, u_obs), 1e-20));
        vec4 zero = vec4(0.0);
        ef = orthonormal(g, u_obs, ef, zero, zero);
        er = orthonormal(g, u_obs, er, ef, zero);
        eu = orthonormal(g, u_obs, eu, ef, er);
    }

    vec2 ndc = (vec2(pix) + 0.5) / vec2(size) * 2.0 - 1.0;   // row 0 = bottom
    vec3 d = normalize(vec3(1.0, ndc.x * tan_half_fov * aspect, ndc.y * tan_half_fov));
    // past-directed null vector: -u plus a unit spatial direction
    vec4 K = -u_obs + d.x * ef + d.y * er + d.z * eu;
    metric_eval(X, g, d0, d1, d2, d3);
    vec4 P = g * K;
    float E_obs = dot(P, u_obs);   // photon energy measured by the camera

    vec3 col = vec3(0.0);
    float trans = 1.0;
    float far = max(60.0, 2.0 * length(X.yzw));
    float rplus = mass + sqrt(max(mass * mass - spin * spin, 0.0));
    bool done = false;
    for (int i = 0; i < 1200 && !done; i++) {
        vec3 p0 = X.yzw;
        float r = r_ks(p0, spin);
        if (r < 0.05) { done = true; break; }                // singularity
        // Step by spatial distance, not by affine parameter: traced
        // backwards near the horizon the momentum grows without bound (an
        // infinite blueshift), and a fixed affine step would jump far.
        vec4 dX = raise_index(X, P);
        float speed = length(dX.yzw);
        if (!(speed < 1e7 * E_obs)) { done = true; break; }   // blew up: captured
        float h = clamp(0.035 * r * min(1.0, r), 0.002, 3.0) / max(speed, 1e-6);
        geodesic_rk4(X, P, h);
        vec3 p1 = X.yzw;
        // Traced backwards, a ray from outside cannot cross the (future)
        // horizon; it stalls just outside it.  Reaching it while moving
        // inward means the light came from the hole: black.  (From inside
        // the horizon, backward rays move outward and go on.)
        float r1 = r_ks(p1, spin);
        if (r1 < r && r1 < 1.01 * rplus) { done = true; break; }

        // thin disk in the plane z = 0
        if (show_disk != 0 && p0.z * p1.z < 0.0) {
            float s = p0.z / (p0.z - p1.z);
            vec3 hit = mix(p0, p1, s);
            float rh = r_ks(hit, spin);
            if (rh > disk_in && rh < disk_out) {
                // circular orbit, prograde: dphi/dt = Omega
                float Om = sqrt(mass) / (pow(rh, 1.5) + spin * sqrt(mass));
                vec4 Xh = vec4(X.x, hit);
                metric_eval(Xh, g, d0, d1, d2, d3);
                vec4 v = vec4(1.0, -Om * hit.y, Om * hit.x, 0.0);
                float n = -mdot(g, v, v);
                if (n > 0.0) {
                    vec4 u_em = v / sqrt(n);
                    float gshift = E_obs / dot(P, u_em);
                    // Novikov–Thorne-like: flux ~ r^-3 (1 - sqrt(r_in / r)),
                    // peak at about 1.36 r_in; temperature ~ flux^(1/4)
                    float x = disk_in / rh;
                    float flux = x * x * x * max(1.0 - sqrt(x), 0.0) / 0.0302;   // 1 at the peak
                    float T = 7500.0 * pow(max(flux, 1e-6), 0.25);
                    float edge = smoothstep(disk_out, 0.6 * disk_out, rh);   // fade the outer edge
                    float alpha = 0.92 * edge;
                    col += trans * alpha * blackbody(T * gshift) * (0.04 + 0.9 * flux)
                           * clamp(gshift * gshift * gshift * gshift, 0.0, 30.0);
                    trans *= 1.0 - alpha;
                }
            }
        }
        float rho = length(p1);
        if (rho > far && dot(p1, X.yzw - p0) > 0.0) {
            vec4 dir4 = raise_index(X, P);
            float gs = E_obs / P.x;        // P_t is conserved: energy at infinity
            col += trans * sky(normalize(dir4.yzw), gs);
            done = true;
        }
        if (trans < 0.01) done = true;
    }
    imageStore(img, pix, vec4(col, 1.0));
}")))

;;; ---- display: tone map, gamma, optional anaglyph ----------------------------

(define display-vs (make-gpu-shader 'vertex "#version 440
layout(location = 0) out vec2 uv;
layout(std140, binding = 0) uniform Show { mat4 clip; float exposure; int anaglyph; };
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = clip * vec4(p * 2.0 - 1.0, 0.5, 1.0);
}"))

(define display-fs (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform Show { mat4 clip; float exposure; int anaglyph; };
layout(binding = 1) uniform sampler2D left;
layout(binding = 2) uniform sampler2D right;
// Reinhard on luminance: very bright regions keep their colour instead of
// all clipping to white.
vec3 tone(vec3 c) {
    c *= exposure;
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec3 m = L > 0.0 ? c * (1.0 / (1.0 + L)) : c;
    return pow(clamp(m, 0.0, 1.0), vec3(1.0 / 2.2));
}
void main() {
    vec3 l = tone(texture(left, uv).rgb);
    if (anaglyph == 0) { frag = vec4(l, 1.0); return; }
    vec3 r = tone(texture(right, uv).rgb);
    // red from the left eye, cyan from the right (luminance-preserving, BT.601)
    float yl = dot(l, vec3(0.299, 0.587, 0.114));
    float yr = dot(r, vec3(0.299, 0.587, 0.114));
    frag = vec4(yl, yr, yr, 1.0);
}"))

;;; ---- resources ----------------------------------------------------------------

(define camera-buffer (make-gpu-buffer 'storage (* 7 16)))
(define camera-params (make-gpu-buffer 'uniform 64))
(define trace-params-left (make-gpu-buffer 'uniform 48))
(define trace-params-right (make-gpu-buffer 'uniform 48))
(define show-params (make-gpu-buffer 'uniform 96))
(define sampler (make-gpu-sampler 'filter 'linear))

(define camera-pipeline
  (make-gpu-compute-pipeline camera-shader
    (make-gpu-bindings (list (list 'uniform 0 'compute camera-params)
                             (list 'storage 1 'compute camera-buffer)))))

;; Images are re-created when the window changes size.  The pipelines are
;; made once, with small placeholder bindings as their layout (kept for the
;; whole run: a pipeline's layout must never be released), and every
;; dispatch/draw passes the current bindings, which have the same layout.
(define (make-image w h) (make-gpu-texture w h 'format 'rgba16f 'storage #t))
(define (trace-bindings params image)
  (make-gpu-bindings (list (list 'uniform 0 'compute params)
                           (list 'storage-read 1 'compute camera-buffer)
                           (list 'image-write 2 'compute image))))
(define (show-bindings left right)
  (make-gpu-bindings (list (list 'uniform 0 '(vertex fragment) show-params)
                           (list 'texture 1 'fragment left sampler)
                           (list 'texture 2 'fragment right sampler))))

(define placeholder (make-image 8 8))
(define trace-pipeline
  (make-gpu-compute-pipeline trace-shader (trace-bindings trace-params-left placeholder)))
(define show-pipeline
  (make-gpu-pipeline 'vertex display-vs 'fragment display-fs
                     'bindings (show-bindings placeholder placeholder) 'depth-test #f))

(define image-w 0) (define image-h 0)
(define image-left #f) (define image-right #f)
(define bind-trace-left #f) (define bind-trace-right #f) (define bind-show #f)

;; Rays are traced at one per *logical* pixel: half the physical pixels on
;; a HiDPI screen.  That keeps each frame far below the GPU's time limit;
;; the display pass scales the image up with linear filtering.  Called from
;; the resize callback, outside any frame, where gpu-release! is allowed.
(define (resize-images! w h)
  (let ((w (max 16 w)) (h (max 16 h)))
    (unless (and (= w image-w) (= h image-h))
      (when image-left
        (for-each gpu-release! (list bind-trace-left bind-trace-right bind-show image-left image-right)))
      (set! image-w w) (set! image-h h)
      (set! image-left (make-image w h))
      (set! image-right (make-image w h))
      (set! bind-trace-left (trace-bindings trace-params-left image-left))
      (set! bind-trace-right (trace-bindings trace-params-right image-right))
      (set! bind-show (show-bindings image-left image-right)))))

;;; ---- state ---------------------------------------------------------------------

(define anaglyph start-anaglyph)
(define exposure 1.0)          ; [ and ] change it
(define show-disk #t)
(define falling #f)
(define need-init #t)
(define fall-speed start-fall-speed)   ; proper time per wall-clock second, in M
(define yaw 0.0)
(define pitch 0.0)
(define drag-from #f)
(define camera-info #f)       ; last (r tau inside? r+) read back from the GPU
(define pending-dtau 0.0)

(define (disk-inner a)
  ;; prograde innermost stable circular orbit (Bardeen, Press, Teukolsky 1972)
  (let* ((z1 (+ 1 (* (expt (- 1 (* a a)) 1/3) (+ (expt (+ 1 a) 1/3) (expt (- 1 a) 1/3)))))
         (z2 (sqrt (+ (* 3 a a) (* z1 z1)))))
    (- (+ 3 z2) (sqrt (* (- 3 z1) (+ 3 z1 (* 2 z2)))))))

(define (reset!)
  (set! falling #f) (set! need-init #t) (set! pending-dtau 0.0)
  (set! yaw 0.0) (set! pitch 0.0))

;;; ---- frame ---------------------------------------------------------------------

(define canvas (make-gpu-canvas))
(define win (make-plain-window "Curry: falling into a Kerr black hole" 1000 640))

(define (update-title!)
  (window-set-title! win
    (string-append
     "Kerr black hole, a = " (number->string spin)
     (if camera-info
         (let ((r (f64vector-ref camera-info 0)) (tau (f64vector-ref camera-info 1)))
           (string-append "   r = " (number->string (/ (round (* 100 r)) 100.0)) " M"
                          "   proper time = " (number->string (/ (round (* 10 tau)) 10.0)) " M"
                          (cond ((> (f64vector-ref camera-info 3) 0.5)
                                 "   reached the inner horizon: the end of the physical Kerr solution (R resets)")
                                ((> (f64vector-ref camera-info 2) 0.5) "   INSIDE THE HORIZON")
                                (else ""))))
         "")
     (if anaglyph "   [anaglyph]" "")
     (if falling "" "   (Space: fall)"))))

(gpu-canvas-on-render! canvas
  (lambda (frame w h)
    (unless image-left (resize-images! 1000 640))
    (gpu-buffer-update! camera-params
      (gpu-std140 (list (list 'float pending-dtau) '(float 1.0) (list 'float spin) (list 'float yaw)
                        (list 'float pitch) (list 'int (if need-init 1 0)) (list 'int (if falling 1 0))
                        '(int 0) (list 'vec4 (list 0.0 (* start-r (cos start-elevation)) 0.0
                                                   (* start-r (sin start-elevation)))))))
    (set! need-init #f)
    (set! pending-dtau 0.0)
    (let* ((fov (* 70 (/ 3.14159265 180)))
           (r-cam (if camera-info (f64vector-ref camera-info 0) start-r))
           (eye-sep (* 0.012 (max r-cam 0.5)))
           (params (lambda (eye)
                     (gpu-std140 (list '(float 1.0) (list 'float spin)
                                       (list 'float (disk-inner spin)) '(float 22.0)
                                       (list 'float (tan (/ fov 2))) (list 'float (/ w h))
                                       (list 'float eye) (list 'int (if show-disk 1 0))
                                       (list 'ivec2 (list image-w image-h)))))))
      (gpu-buffer-update! trace-params-left (params (if anaglyph (- eye-sep) 0.0)))
      (gpu-buffer-update! trace-params-right (params eye-sep))
      (gpu-buffer-update! show-params
        (gpu-std140 (list (list 'mat4 (gpu-clip-space-matrix frame)) (list 'float exposure)
                          (list 'int (if anaglyph 1 0)))))
      (let ((gx (quotient (+ image-w 7) 8)) (gy (quotient (+ image-h 7) 8)))
        (gpu-compute-pass! frame
          (lambda (pass)
            (gpu-dispatch! pass camera-pipeline 1)))
        (gpu-compute-pass! frame
          (lambda (pass)
            (gpu-dispatch! pass trace-pipeline gx gy 'bindings bind-trace-left)
            (when anaglyph
              (gpu-dispatch! pass trace-pipeline gx gy 'bindings bind-trace-right)))))
      (gpu-read-buffer! frame camera-buffer
        (lambda (bytes)
          (let ((v (gpu-bytes->f32 bytes)))
            (set! camera-info (f64vector (f64vector-ref v 24) (f64vector-ref v 25)
                                         (f64vector-ref v 26) (f64vector-ref v 27)))
            (update-title!))))
      (gpu-render-pass! frame
        (lambda (pass) (gpu-draw! pass show-pipeline 3 'bindings bind-show))))))

;;; ---- input ---------------------------------------------------------------------

(define (cycle-spin!)
  (let ((rest (member spin spins)))
    (set! spin (if (and rest (pair? (cdr rest))) (cadr rest) (car spins))))
  (reset!))

(gpu-canvas-on-key! canvas
  (lambda (key mods)
    (cond ((string=? key " ") (set! falling (not falling)))
          ((string=? key "space") (set! falling (not falling)))
          ((member key '("r" "R")) (reset!))
          ((member key '("s" "S")) (cycle-spin!))
          ((member key '("a" "A")) (set! anaglyph (not anaglyph)))
          ((member key '("d" "D")) (set! show-disk (not show-disk)))
          ((string=? key "[") (set! exposure (/ exposure 1.3)))
          ((string=? key "]") (set! exposure (* exposure 1.3)))
          ((member key '("+" "=")) (set! fall-speed (* fall-speed 1.5)))
          ((member key '("-" "_")) (set! fall-speed (/ fall-speed 1.5)))
          ((string=? key "Left") (set! yaw (- yaw 0.1)))
          ((string=? key "Right") (set! yaw (+ yaw 0.1)))
          ((string=? key "Up") (set! pitch (min 1.5 (+ pitch 0.1))))
          ((string=? key "Down") (set! pitch (max -1.5 (- pitch 0.1))))
          ((member key '("q" "Q" "Escape")) (quit-event-loop)))
    (update-title!)))

(gpu-canvas-on-mouse! canvas
  (lambda (event button x y mods)
    (case event
      ((press) (set! drag-from (cons x y)))
      ((release) (set! drag-from #f))
      ((drag)
       (when drag-from
         (set! yaw (+ yaw (* 0.005 (- x (car drag-from)))))
         (set! pitch (max -1.5 (min 1.5 (- pitch (* 0.005 (- y (cdr drag-from)))))))
         (set! drag-from (cons x y)))))))

(gpu-canvas-on-resize! canvas resize-images!)
(window-set-central-widget! win canvas)
(window-on-close! win quit-event-loop)

(define elapsed 0)
(define timer
  (make-timer/dt 16
    (lambda (dt)
      (set! elapsed (+ elapsed dt))
      (when falling (set! pending-dtau (+ pending-dtau (* fall-speed (/ dt 1000.0)))))
      (gpu-canvas-redraw! canvas)
      (when (and snapshot (> elapsed (* 1000 snapshot-after)))
        (display (if (gpu-canvas-save-png! canvas snapshot) "saved " "could not save "))
        (display snapshot)
        (when camera-info
          (display "  r = ") (display (f64vector-ref camera-info 0))
          (display "  proper time = ") (display (f64vector-ref camera-info 1))
          (when (> (f64vector-ref camera-info 2) 0.5) (display "  (inside the horizon)")))
        (newline)
        (quit-event-loop)))))

(when (flag? "--fall") (set! falling #t))
(window-show! win)
(update-title!)
(timer/dt-start! timer)
(run-event-loop)
