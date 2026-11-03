;;; (curry spacetime) — symbolic spacetime metrics, compiled to GLSL
;;;
;;; A metric is written once, symbolically, with curry's CAS.  The CAS
;;; derives every partial derivative ∂_k g_ij exactly, and (curry glsl)
;;; turns the metric and its derivatives into one GLSL function,
;;;
;;;   void metric_eval(vec4 X, out mat4 g, out mat4 d0, out mat4 d1,
;;;                    out mat4 d2, out mat4 d3)
;;;
;;; where X = (t, x, y, z), g is g_ij and dk is ∂g_ij/∂X^k.  That is all a
;;; geodesic integrator needs (see examples/blackhole.scm): in Hamiltonian
;;; form, with momentum p_m and u = g^-1 p,
;;;
;;;   dX^m/dλ = u^m        dp_m/dλ = ½ u^a u^b ∂_m g_ab
;;;
;;; so no Christoffel symbols and no symbolic matrix inverse are needed.
;;;
;;; Coordinates are Cartesian (t x y z).  The built-in black-hole metrics
;;; use the ingoing Kerr–Schild form g = η + f l⊗l, which is regular at the
;;; event horizon (a camera can fall through it) and has no pole
;;; singularities, unlike Schwarzschild or Boyer–Lindquist coordinates.
;;; Signature (- + + +); geometric units G = c = 1.
;;;
;;; Usage:
;;;   (import (curry spacetime))
;;;   (define m (kerr-metric))                 ; parameters M and a stay symbolic
;;;   (metric->glsl m '((M . "u.mass") (a . "u.spin")))   ; -> GLSL source string
;;;
;;; Your own metric:
;;;   (define-values (t x y z) (apply values (spacetime-coordinates)))
;;;   (make-metric (list (list g00 g01 g02 g03) ...) (list (sym-var 'k)))

(define-library (curry spacetime)
  (import (scheme base) (scheme inexact) (curry glsl))
  (export spacetime-coordinates make-metric metric? metric-components
          metric-parameters metric-component metric-derivatives
          minkowski-metric kerr-schild-metric
          schwarzschild-metric kerr-metric reissner-nordstrom-metric
          kerr-newman-metric
          metric->glsl geodesic-glsl)
  (begin

    ;; Sequential map: curry's map may run in parallel threads above a
    ;; small list length, and the CAS calls below are not meant to race.
    (define (smap f l)
      (let loop ((l l) (acc '()))
        (if (null? l) (reverse acc) (loop (cdr l) (cons (f (car l)) acc)))))

    (define T (sym-var 't 'real))
    (define X (sym-var 'x 'real))
    (define Y (sym-var 'y 'real))
    (define Z (sym-var 'z 'real))

    ;; The four coordinate variables every metric here is written in.
    (define (spacetime-coordinates) (list T X Y Z))

    (define-record-type <metric>
      (%make-metric components params)
      metric?
      (components metric-components)   ; 4-vector of 4-vectors, symmetric
      (params metric-parameters))      ; list of sym-vars

    ;; components: 4x4 nested lists or vectors.  Must be symmetric; only the
    ;; upper triangle (i <= j) is used, so a lower triangle that disagrees
    ;; raises instead of being silently ignored.
    (define (make-metric components params)
      (let* ((rows (cond ((vector? components) (vector->list components))
                         ((list? components) components)
                         (else (error "make-metric: components must be a 4x4 list or vector"))))
             (rows (smap (lambda (r) (cond ((vector? r) r) ((list? r) (list->vector r))
                                          (else (error "make-metric: each row must be a list or vector"))))
                        rows)))
        (unless (and (= (length rows) 4) (let loop ((r rows))
                                           (or (null? r) (and (= (vector-length (car r)) 4) (loop (cdr r))))))
          (error "make-metric: components must be 4x4"))
        (let ((m (list->vector rows)))
          (do ((i 0 (+ i 1))) ((= i 4))
            (do ((j (+ i 1) (+ j 1))) ((= j 4))
              (let ((a (vector-ref (vector-ref m i) j)) (b (vector-ref (vector-ref m j) i)))
                (unless (or (eq? a b) (numerically-equal? a b (append (spacetime-coordinates) params)))
                  (error "make-metric: components are not symmetric at" i j)))))
          (%make-metric m params))))

    ;; simplify does not always cancel a - b when the factors of a product
    ;; are in a different order, so symmetry is checked numerically: equal
    ;; at a few irrational-ish sample points, to a relative 1e-9.
    (define sample-points
      '((0.731 1.913 -0.557 2.249 0.317 0.611)
        (1.377 -2.718 0.913 -1.141 0.271 0.493)
        (-0.443 3.301 1.709 0.389 0.137 0.211)))

    (define (evaluate-at expr vars values)
      (let loop ((e expr) (vs vars) (xs values))
        (cond ((null? vs) e)
              ((null? xs) (error "make-metric: too many parameters to check symmetry"))
              (else (loop (if (number? e) e (substitute e (car vs) (car xs))) (cdr vs) (cdr xs))))))

    (define (numerically-equal? a b vars)
      (let loop ((pts sample-points))
        (or (null? pts)
            (let ((va (evaluate-at a vars (car pts))) (vb (evaluate-at b vars (car pts))))
              (and (number? va) (number? vb)
                   (<= (magnitude (- va vb)) (* 1e-9 (max 1.0 (magnitude va) (magnitude vb))))
                   (loop (cdr pts)))))))

    (define (metric-component m i j)
      (vector-ref (vector-ref (metric-components m) i) j))

    (define (metric-derivatives m)
      ;; (vector d0 d1 d2 d3), dk a 4x4 vector of vectors of ∂_k g_ij
      (let ((coords (spacetime-coordinates)))
        (list->vector
         (smap (lambda (c)
                (let ((d (make-vector 4)))
                  (do ((i 0 (+ i 1))) ((= i 4) d)
                    (vector-set! d i (make-vector 4 0))
                    (do ((j 0 (+ j 1))) ((= j 4))
                      (vector-set! (vector-ref d i) j
                                   (if (<= i j)
                                       (simplify (∂ (metric-component m i j) c))
                                       (vector-ref (vector-ref d j) i)))))))
              coords))))

    ;; ---- metric families ----

    (define (minkowski-metric)
      (make-metric '((-1 0 0 0) (0 1 0 0) (0 0 1 0) (0 0 0 1)) '()))

    ;; g_ij = η_ij + f l_i l_j, l a covector given as a 4-list.
    (define (kerr-schild-metric f l params)
      ;; Built symmetric by construction: (i,j) and (j,i) share one object.
      (let ((eta '#(-1 1 1 1))
            (m (make-vector 4)))
        (do ((i 0 (+ i 1))) ((= i 4))
          (vector-set! m i (make-vector 4 0)))
        (do ((i 0 (+ i 1))) ((= i 4))
          (do ((j i (+ j 1))) ((= j 4))
            (let ((c (+ (if (= i j) (vector-ref eta i) 0)
                        (* f (list-ref l i) (list-ref l j)))))
              (vector-set! (vector-ref m i) j c)
              (vector-set! (vector-ref m j) i c))))
        (make-metric m params)))

    (define (param name default)
      (if (sym-var? default) default (sym-var name 'positive)))

    ;; Non-rotating, uncharged.  mass: a number, or omit to keep a symbolic M.
    (define (schwarzschild-metric . opt)
      (let* ((M (if (pair? opt) (car opt) (sym-var 'M 'positive)))
             (r (sqrt (+ (* X X) (* Y Y) (* Z Z)))))
        (kerr-schild-metric (/ (* 2 M) r)
                            (list 1 (/ X r) (/ Y r) (/ Z r))
                            (if (number? M) '() (list M)))))

    ;; Charged, non-rotating: f = (2Mr - Q^2) / r^2.
    (define (reissner-nordstrom-metric . opt)
      (let* ((M (if (pair? opt) (car opt) (sym-var 'M 'positive)))
             (Q (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) (sym-var 'Q 'real)))
             (r (sqrt (+ (* X X) (* Y Y) (* Z Z)))))
        (kerr-schild-metric (/ (- (* 2 M r) (* Q Q)) (* r r))
                            (list 1 (/ X r) (/ Y r) (/ Z r))
                            (filter-symbolic (list M Q)))))

    ;; Rotating about the z axis with angular momentum J = aM; Kerr–Newman
    ;; adds charge Q.  r is the Kerr–Schild radial function, defined by
    ;; x²+y²+z² = r² + a² (1 - z²/r²).
    (define (kerr-newman-metric . opt)
      (let* ((M (if (pair? opt) (car opt) (sym-var 'M 'positive)))
             (a (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) (sym-var 'a 'real)))
             (Q (if (and (pair? opt) (pair? (cdr opt)) (pair? (cddr opt))) (caddr opt) (sym-var 'Q 'real)))
             (rho2 (+ (* X X) (* Y Y) (* Z Z)))
             (w (/ (- rho2 (* a a)) 2))
             (r (sqrt (+ w (sqrt (+ (* w w) (* a a Z Z))))))
             (r2a2 (+ (* r r) (* a a))))
        (kerr-schild-metric (/ (* r r (- (* 2 M r) (* Q Q)))
                               (+ (* r r r r) (* a a Z Z)))
                            (list 1
                                  (/ (+ (* r X) (* a Y)) r2a2)
                                  (/ (- (* r Y) (* a X)) r2a2)
                                  (/ Z r))
                            (filter-symbolic (list M a Q)))))

    (define (kerr-metric . opt)
      (let ((M (if (pair? opt) (car opt) (sym-var 'M 'positive)))
            (a (if (and (pair? opt) (pair? (cdr opt))) (cadr opt) (sym-var 'a 'real))))
        (kerr-newman-metric M a 0)))

    (define (filter-symbolic xs)
      (let loop ((xs xs) (acc '()))
        (cond ((null? xs) (reverse acc))
              ((sym-var? (car xs)) (loop (cdr xs) (cons (car xs) acc)))
              (else (loop (cdr xs) acc)))))

    (define (cddr p) (cdr (cdr p)))
    (define (caddr p) (car (cdr (cdr p))))

    ;; ---- GLSL ----

    ;; param-map: alist from each parameter's name to its GLSL text,
    ;; e.g. '((M . "u.mass") (a . "u.spin")).  The coordinates are bound
    ;; from the function's vec4 X argument.
    (define (metric->glsl m param-map)
      (for-each (lambda (p)
                  (unless (assq (sym-var-name p) param-map)
                    (error "metric->glsl: no GLSL name given for metric parameter" (sym-var-name p))))
                (metric-parameters m))
      (let* ((var-map (append '((t . "X.x") (x . "X.y") (y . "X.z") (z . "X.w")) param-map))
             (ds (metric-derivatives m))
             (targets '()))
        (define (add! name expr) (set! targets (cons (cons name expr) targets)))
        (do ((i 0 (+ i 1))) ((= i 4))
          (do ((j i (+ j 1))) ((= j 4))
            (add! (glsl-index "g" i j) (metric-component m i j))
            (do ((k 0 (+ k 1))) ((= k 4))
              (add! (glsl-index (string-append "d" (number->string k)) i j)
                    (vector-ref (vector-ref (vector-ref ds k) i) j)))))
        (string-append
         "void metric_eval(vec4 X, out mat4 g, out mat4 d0, out mat4 d1, out mat4 d2, out mat4 d3) {\n"
         (indent (glsl-assignments (reverse targets) var-map "m_"))
         "\n"
         (indent (mirror-lower-triangles '("g" "d0" "d1" "d2" "d3")))
         "\n}\n")))

;; GLSL for integrating geodesics with any metric_eval (from
    ;; metric->glsl), in Hamiltonian form.  State: position X^m and
    ;; covariant momentum P_m.  Provides
    ;;
    ;;   void  geodesic_rhs(vec4 X, vec4 P, out vec4 dX, out vec4 dP)
    ;;   void  geodesic_rk4(inout vec4 X, inout vec4 P, float h)
    ;;   float geodesic_hamiltonian(vec4 X, vec4 P)   // ½ g^ab P_a P_b:
    ;;                                                 // 0 light, -½ massive
    ;;   vec4  raise_index(vec4 X, vec4 P)             // g^ab P_b
    ;;   vec4  lower_index(vec4 X, vec4 U)             // g_ab U^b
    ;;
    ;; Conserved: the Hamiltonian, and P_t for a time-independent metric
    ;; (minus the energy at infinity).  32-bit floats: keep h proportional
    ;; to the distance from the singularity.
    (define (geodesic-glsl)
      "void geodesic_rhs(vec4 X, vec4 P, out vec4 dX, out vec4 dP) {
    mat4 g, d0, d1, d2, d3;
    metric_eval(X, g, d0, d1, d2, d3);
    vec4 u = inverse(g) * P;
    dX = u;
    dP = 0.5 * vec4(dot(u, d0 * u), dot(u, d1 * u), dot(u, d2 * u), dot(u, d3 * u));
}

void geodesic_rk4(inout vec4 X, inout vec4 P, float h) {
    vec4 k1x, k1p, k2x, k2p, k3x, k3p, k4x, k4p;
    geodesic_rhs(X, P, k1x, k1p);
    geodesic_rhs(X + 0.5 * h * k1x, P + 0.5 * h * k1p, k2x, k2p);
    geodesic_rhs(X + 0.5 * h * k2x, P + 0.5 * h * k2p, k3x, k3p);
    geodesic_rhs(X + h * k3x, P + h * k3p, k4x, k4p);
    X += h / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
    P += h / 6.0 * (k1p + 2.0 * k2p + 2.0 * k3p + k4p);
}

vec4 raise_index(vec4 X, vec4 P) {
    mat4 g, d0, d1, d2, d3;
    metric_eval(X, g, d0, d1, d2, d3);
    return inverse(g) * P;
}

vec4 lower_index(vec4 X, vec4 U) {
    mat4 g, d0, d1, d2, d3;
    metric_eval(X, g, d0, d1, d2, d3);
    return g * U;
}

float geodesic_hamiltonian(vec4 X, vec4 P) {
    mat4 g, d0, d1, d2, d3;
    metric_eval(X, g, d0, d1, d2, d3);
    return 0.5 * dot(P, inverse(g) * P);
}
")

    ;; GLSL mat4 is indexed [column][row]; the metric is symmetric, so the
    ;; upper triangle is assigned and mirrored.
    (define (glsl-index name i j)
      (string-append name "[" (number->string j) "][" (number->string i) "]"))

    (define (mirror-lower-triangles names)
      (let ((out '()))
        (for-each
         (lambda (n)
           (do ((i 0 (+ i 1))) ((= i 4))
             (do ((j (+ i 1) (+ j 1))) ((= j 4))
               (set! out (cons (string-append (glsl-index n j i) " = " (glsl-index n i j) ";") out)))))
         names)
        (let loop ((ls (reverse out)) (acc ""))
          (if (null? ls) acc (loop (cdr ls) (string-append acc (if (string=? acc "") "" "\n") (car ls)))))))

    (define (indent text)
      (let loop ((cs (string->list text)) (acc (list #\space #\space #\space #\space)))
        (cond ((null? cs) (list->string (reverse acc)))
              ((char=? (car cs) #\newline)
               (loop (cdr cs) (append (list #\space #\space #\space #\space #\newline) acc)))
              (else (loop (cdr cs) (cons (car cs) acc))))))))
