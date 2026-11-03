;;; spacetime_gpu_tests.scm — light rays traced on the GPU with GLSL generated
;;; by (curry spacetime), checked against known physics:
;;;   - Schwarzschild: the total swept angle of escaping rays against the
;;;     exact integral, and the capture limit b = 3√3 M
;;;   - Kerr (a = 0.9 M): different capture limits for light moving with and
;;;     against the spin (b ≈ 2.844 M and 6.834 M)
;;;   - conservation of energy and of the Hamiltonian along each ray
;;; Prints SKIP without a GPU device unless CURRY_GPU_REQUIRED is set.

(import (scheme base) (scheme inexact) (scheme write) (scheme process-context)
        (curry qt6) (curry f64vector) (curry spacetime))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got (guard (e (#t (list 'raised (if (error-object? e) (error-object-message e) e)))) expr)))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))

(define (string-contains? s sub)
  (let ((n (string-length s)) (m (string-length sub)))
    (let loop ((i 0))
      (cond ((> (+ i m) n) #f)
            ((string=? (substring s i (+ i m)) sub) #t)
            (else (loop (+ i 1)))))))

(unless (guard (e (#t #f)) (gpu-device-destroy! (make-gpu-device)) #t)
  (display "spacetime gpu tests: SKIP (no GPU device)") (newline)
  (exit (if (get-environment-variable "CURRY_GPU_REQUIRED") 1 0)))

(define (ray-tracer metric param-map params bs R)
  (let* ((dev (make-gpu-device))
         (n (length bs))
         (decls (apply string-append
                  (map (lambda (pm) (string-append "  float " (cdr pm) ";\n")) param-map)))
         (src (string-append "#version 440
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer In  { float b[]; };
layout(std430, binding = 1) buffer Out { float o[]; };
layout(std140, binding = 2) uniform U {\n  float R;\n" decls "};\n"
                 (metric->glsl metric param-map) (geodesic-glsl) "
void main() {
  uint i = gl_GlobalInvocationID.x;
  vec4 X = vec4(0.0, -R, b[i], 0.0);
  mat4 g, d0, d1, d2, d3;
  metric_eval(X, g, d0, d1, d2, d3);
  float gtt = g[0][0], gtx = g[1][0], gxx = g[1][1];
  float T = (-gtx - sqrt(gtx * gtx - gtt * gxx)) / gtt;
  vec4 P = g * vec4(T, 1.0, 0.0, 0.0);
  float E = -P.x, L = X.y * P.z - X.z * P.y;
  float H0 = geodesic_hamiltonian(X, P);
  float phi = atan(X.z, X.y), swept = 0.0;
  int status = 0;
  for (int k = 0; k < 200000 && status == 0; k++) {
    float r = length(X.yzw);
    geodesic_rk4(X, P, clamp(0.01 * r, 0.002, 2.0));
    float phi2 = atan(X.z, X.y);
    float dphi = phi2 - phi;
    if (dphi > 3.14159265) dphi -= 6.2831853;
    if (dphi < -3.14159265) dphi += 6.2831853;
    swept += dphi; phi = phi2;
    float r2 = length(X.yzw);
    // Captured once well inside the horizon: Cartesian radius 1.2 M is inside
    // it for Schwarzschild (2 M) and for Kerr a = 0.9 (1.44 M or more).
    // Not a smaller radius: equatorial Kerr rays approach the ring
    // singularity at radius a and the step size shrinks to nothing there.
    if (r2 < 1.2) status = 2;
    else if (r2 > R + 1.0 && dot(X.yzw, P.yzw) > 0.0) status = 1;
  }
  o[i * 6u + 0u] = float(status);
  o[i * 6u + 1u] = abs(swept);
  o[i * 6u + 2u] = L / E;
  o[i * 6u + 3u] = length(X.yzw);
  o[i * 6u + 4u] = geodesic_hamiltonian(X, P) - H0;
  o[i * 6u + 5u] = -P.x - E;
}"))
         (cs (make-gpu-shader 'compute src))
         (bin (make-gpu-buffer 'storage bs))
         (out (make-gpu-buffer 'storage (* 4 6 n)))
         (ub (make-gpu-buffer 'uniform (gpu-std140 (cons (list 'float R)
                                                         (map (lambda (v) (list 'float v)) params)))))
         (pl (make-gpu-compute-pipeline cs (make-gpu-bindings
               (list (list 'storage 0 'compute bin) (list 'storage 1 'compute out)
                     (list 'uniform 2 'compute ub))))))
    (gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p pl n)))))
    (let ((o (gpu-bytes->f32 (gpu-read-buffer dev out))))
      (gpu-device-destroy! dev)
      ;; list of (status swept L/E r-end H-drift E-drift)
      (let loop ((i (- n 1)) (acc '()))
        (if (< i 0) acc
            (loop (- i 1)
                  (cons (map (lambda (k) (f64vector-ref o (+ (* i 6) k))) '(0 1 2 3 4 5)) acc)))))))



;; exact swept angle for Schwarzschild, u = 1/r, M = 1
(define (F u b) (+ (/ 1.0 (* b b)) (- (* u u)) (* 2.0 u u u)))
(define (turning-u b)
  (let loop ((lo 0.0) (hi (/ 1.0 3.0)) (k 0))
    (if (= k 200) lo
        (let ((mid (/ (+ lo hi) 2))) (if (> (F mid b) 0) (loop mid hi (+ k 1)) (loop lo mid (+ k 1)))))))
(define (I ua u0 b)
  (let* ((wmax (sqrt (- u0 ua))) (N 4000) (hw (/ wmax N))
         (f (lambda (w) (if (= w 0.0) (/ 2.0 (sqrt (- (* 2.0 u0) (* 6.0 u0 u0))))
                            (/ (* 2.0 w) (sqrt (F (- u0 (* w w)) b)))))))
    (let loop ((k 0) (acc 0.0))
      (if (> k N) (* acc (/ hw 3.0))
          (loop (+ k 1) (+ acc (* (f (* k hw)) (cond ((or (= k 0) (= k N)) 1) ((odd? k) 4) (else 2)))))))))

(define R 400.0)
(define s-bs '(5.15 5.25 6.0 8.0 20.0 40.0))
(define s-rays (ray-tracer (schwarzschild-metric) '((M . "mass")) '(1.0) s-bs R))

(check "Schwarzschild: b = 5.15 M is captured (limit 3√3 M ≈ 5.196 M)" (car (list-ref s-rays 0)) 2.0)
(check "Schwarzschild: b = 5.25 M escapes" (car (list-ref s-rays 1)) 1.0)
(for-each
 (lambda (b ray)
   (when (= (car ray) 1.0)
     (let* ((u0 (turning-u (list-ref ray 2)))
            (exact (+ (I (/ 1.0 (sqrt (+ (* R R) (* b b)))) u0 (list-ref ray 2))
                      (I (/ 1.0 (list-ref ray 3)) u0 (list-ref ray 2))))
            (rel (/ (abs (- (list-ref ray 1) exact)) exact)))
       (check (string-append "Schwarzschild: swept angle matches the exact integral, b = " (number->string b))
              (< rel 1e-4) #t))
     (check (string-append "Schwarzschild: Hamiltonian conserved, b = " (number->string b))
            (< (abs (list-ref ray 4)) 1e-4) #t)
     (check (string-append "Schwarzschild: energy conserved, b = " (number->string b))
            (< (abs (list-ref ray 5)) 1e-5) #t)))
 s-bs s-rays)

;; Kerr, a = 0.9 M, spin about +z.  A ray at y = b moving in +x has
;; L/E = -b: b < 0 is prograde (with the spin), b > 0 retrograde.
(define k-bs '(-2.75 -2.95 6.70 6.98))
(define k-rays (ray-tracer (kerr-metric) '((M . "mass") (a . "spin")) '(1.0 0.9) k-bs R))
(check "Kerr a=0.9: prograde |b| = 2.75 M is captured (limit 2.844 M)" (car (list-ref k-rays 0)) 2.0)
(check "Kerr a=0.9: prograde |b| = 2.95 M escapes" (car (list-ref k-rays 1)) 1.0)
(check "Kerr a=0.9: retrograde b = 6.70 M is captured (limit 6.834 M)" (car (list-ref k-rays 2)) 2.0)
(check "Kerr a=0.9: retrograde b = 6.98 M escapes" (car (list-ref k-rays 3)) 1.0)
(check "Kerr: energy conserved on escaping rays"
       (and (< (abs (list-ref (list-ref k-rays 1) 5)) 1e-5) (< (abs (list-ref (list-ref k-rays 3) 5)) 1e-5)) #t)

;;; ---- Summary ----

(newline)
(display "spacetime gpu tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
