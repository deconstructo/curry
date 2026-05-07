;;; examples/mcp_nbody.scm — N-body gravity simulation in D spatial dimensions
;;;
;;; Tools:
;;;   sim-reset    — clear all bodies, restore defaults
;;;   sim-set      — set simulation parameters (D, G, dt)
;;;   sim-add-body — add a body with position, velocity, mass, name
;;;   sim-add-orbit — add a satellite in circular orbit around a central body
;;;   sim-step     — advance simulation by N steps
;;;   sim-state    — query current state of all bodies
;;;   sim-energy   — compute total mechanical energy
;;;
;;; The force law generalises to arbitrary spatial dimension D (may be non-integer):
;;;   F_k = G * m_i * m_j * dr_k / |dr|^D
;;;
;;; Usage:
;;;   ./build/curry examples/mcp_nbody.scm
;;;
;;; Claude Code config (~/.claude.json):
;;;   { "mcpServers": { "curry-nbody": {
;;;       "command": "/path/to/build/curry",
;;;       "args":    ["/path/to/examples/mcp_nbody.scm"] } } }

(import (curry mcp))


;;; ---- Simulation state ----

(define *bodies* '())       ; list of body vectors
(define *D*  3.0)           ; spatial dimension (may be non-integer)
(define *G*  1.0)           ; gravitational constant
(define *dt* 0.01)          ; time step
(define *t*  0.0)           ; current time


;;; ---- Body accessors ----
;;; A body is a vector: #(name mass pos vel)
;;; pos and vel are Scheme vectors of length (ceiling *D*) or just 3 floats.

(define (make-body name mass pos vel) (vector name mass pos vel))
(define (body-name b)  (vector-ref b 0))
(define (body-mass b)  (vector-ref b 1))
(define (body-pos  b)  (vector-ref b 2))
(define (body-vel  b)  (vector-ref b 3))
(define (body-set-vel! b v) (vector-set! b 3 v))
(define (body-set-pos! b p) (vector-set! b 2 p))


;;; ---- Vector math (variable dimension) ----

(define (vec-dim v)    (vector-length v))
(define (vec-zero d)   (make-vector d 0.0))

(define (vec-add a b)
  (let* ((d (vec-dim a)) (r (make-vector d 0.0)))
    (let loop ((k 0))
      (if (= k d) r
          (begin (vector-set! r k (+ (vector-ref a k) (vector-ref b k)))
                 (loop (+ k 1)))))))

(define (vec-sub a b)
  (let* ((d (vec-dim a)) (r (make-vector d 0.0)))
    (let loop ((k 0))
      (if (= k d) r
          (begin (vector-set! r k (- (vector-ref a k) (vector-ref b k)))
                 (loop (+ k 1)))))))

(define (vec-scale s v)
  (let* ((d (vec-dim v)) (r (make-vector d 0.0)))
    (let loop ((k 0))
      (if (= k d) r
          (begin (vector-set! r k (* s (vector-ref v k)))
                 (loop (+ k 1)))))))

(define (vec-dot a b)
  (let loop ((k 0) (acc 0.0))
    (if (= k (vec-dim a)) acc
        (loop (+ k 1) (+ acc (* (vector-ref a k) (vector-ref b k)))))))

(define (vec-norm2 v) (vec-dot v v))
(define (vec-norm  v) (sqrt (vec-norm2 v)))

(define (vec->list v)
  (let loop ((k 0) (acc '()))
    (if (= k (vec-dim v)) (reverse acc)
        (loop (+ k 1) (cons (vector-ref v k) acc)))))

(define (list->vec lst)
  (let* ((n (length lst)) (v (make-vector n 0.0)))
    (let loop ((i 0) (l lst))
      (if (null? l) v
          (begin (vector-set! v i (exact->inexact (car l)))
                 (loop (+ i 1) (cdr l)))))))


;;; ---- Physics ----

;;; Acceleration on body i due to body j in D dimensions.
;;; F_k = G * mj * dr_k / |dr|^D     (softened: |dr|^2 + eps² under the root)
(define (accel-pair pos-i pos-j mass-j D G)
  (let* ((dr   (vec-sub pos-j pos-i))
         (r2   (+ (vec-norm2 dr) 1e-10))   ; softening
         (r    (sqrt r2))
         (rD   (expt r D))
         (c    (/ (* G mass-j) rD)))
    (vec-scale c dr)))

;;; Sum accelerations from all other bodies onto body i.
(define (total-accel bodies i D G)
  (let* ((bi  (list-ref bodies i))
         (pi  (body-pos bi))
         (d   (vec-dim pi))
         (acc (vec-zero d)))
    (let loop ((j 0) (bs bodies) (a acc))
      (if (null? bs) a
          (loop (+ j 1) (cdr bs)
                (if (= i j) a
                    (vec-add a (accel-pair pi (body-pos (car bs))
                                           (body-mass (car bs)) D G))))))))

;;; Leapfrog (velocity Verlet) step for the whole system.
(define (leapfrog-step! bodies D G dt)
  (let ((n (length bodies)))
    ; Half-kick: v += a * dt/2
    (let loop1 ((i 0) (bs bodies))
      (unless (null? bs)
        (let* ((b  (car bs))
               (a  (total-accel bodies i D G)))
          (body-set-vel! b (vec-add (body-vel b) (vec-scale (* 0.5 dt) a))))
        (loop1 (+ i 1) (cdr bs))))
    ; Drift: x += v * dt
    (for-each (lambda (b)
                (body-set-pos! b (vec-add (body-pos b)
                                          (vec-scale dt (body-vel b)))))
              bodies)
    ; Half-kick again
    (let loop2 ((i 0) (bs bodies))
      (unless (null? bs)
        (let* ((b  (car bs))
               (a  (total-accel bodies i D G)))
          (body-set-vel! b (vec-add (body-vel b) (vec-scale (* 0.5 dt) a))))
        (loop2 (+ i 1) (cdr bs))))))


;;; ---- Helpers ----

(define (arg args name)
  (let ((p (assq name args)))
    (if p (cdr p) (error "missing argument" name))))

(define (arg? args name default)
  (let ((p (assq name args)))
    (if p (cdr p) default)))

;;; Format a float for display (4 decimal places).
(define (fmt-num x)
  (let* ((x  (exact->inexact x))
         (s  (number->string x)))
    s))

;;; Format a body as a readable string.
(define (fmt-body b)
  (string-append
    (body-name b) ": "
    "pos=" (fmt-vec (body-pos b)) " "
    "vel=" (fmt-vec (body-vel b)) " "
    "mass=" (fmt-num (body-mass b))))

(define (fmt-vec v)
  (let loop ((k 0) (parts '()))
    (if (= k (vec-dim v))
        (string-append "(" (string-join (reverse parts) " ") ")")
        (loop (+ k 1) (cons (fmt-num (vector-ref v k)) parts)))))

(define (string-join lst sep)
  (if (null? lst) ""
      (let loop ((rest (cdr lst)) (acc (car lst)))
        (if (null? rest) acc
            (loop (cdr rest) (string-append acc sep (car rest)))))))

;;; Circular orbit speed for satellite at distance r from central mass M in D dims.
;;; v² = G * M / r^(D-2)   (only well-defined for D > 2)
(define (orbit-speed G M r D)
  (if (<= D 2.0)
      (error "circular orbit speed undefined for D <= 2")
      (sqrt (/ (* G M) (expt r (- D 2.0))))))

;;; Number of integer coordinate dimensions we actually store.
(define (coord-dim) (max 2 (min 8 (inexact->exact (ceiling *D*)))))

(define (make-list n val)
  (let loop ((i n) (acc '()))
    (if (= i 0) acc (loop (- i 1) (cons val acc)))))


;;; ---- Tool handlers ----

;;; sim-reset
(define (tool-reset args)
  (set! *bodies* '())
  (set! *D*  3.0)
  (set! *G*  1.0)
  (set! *dt* 0.01)
  (set! *t*  0.0)
  (mcp-text "Simulation reset. D=3, G=1, dt=0.01, t=0, no bodies."))

;;; sim-set
(define (tool-set args)
  (let ((D  (arg? args 'D  *D*))
        (G  (arg? args 'G  *G*))
        (dt (arg? args 'dt *dt*)))
    (set! *D*  (exact->inexact D))
    (set! *G*  (exact->inexact G))
    (set! *dt* (exact->inexact dt))
    (mcp-text (string-append "Parameters updated: D=" (fmt-num *D*)
                              " G=" (fmt-num *G*)
                              " dt=" (fmt-num *dt*)))))

;;; sim-add-body
(define (tool-add-body args)
  (let* ((name (arg  args 'name))
         (mass (exact->inexact (arg args 'mass)))
         (pos  (list->vec (arg args 'position)))
         (raw-vel (arg? args 'velocity '()))
         (vel  (if (null? raw-vel)
                   (vec-zero (coord-dim))
                   (list->vec raw-vel))))
    (set! *bodies* (append *bodies* (list (make-body name mass pos vel))))
    (mcp-text (string-append "Added body '" name
                              "' (mass=" (fmt-num mass) ")"))))

;;; sim-add-orbit — add satellite in circular orbit around the named central body
(define (tool-add-orbit args)
  (let* ((name     (arg  args 'name))
         (central  (arg  args 'central))
         (mass     (exact->inexact (arg args 'mass)))
         (radius   (exact->inexact (arg args 'radius)))
         (cb       (let loop ((bs *bodies*))
                     (cond ((null? bs)
                            (error (string-append "body not found: " central)))
                           ((string=? (body-name (car bs)) central)
                            (car bs))
                           (else (loop (cdr bs))))))
         (cp       (body-pos cb))
         (cm       (body-mass cb))
         (v0       (orbit-speed *G* cm radius *D*))
         ; Orbit in the x-y plane: position at (r,0,...), velocity at (0,v0,...)
         (d        (coord-dim))
         (pos      (let ((p (vec-zero d)))
                     (vector-set! p 0 (+ (vector-ref cp 0) radius))
                     p))
         (vel      (let ((v (vec-zero d)))
                     (vector-set! v 1 v0)
                     v)))
    (set! *bodies* (append *bodies* (list (make-body name mass pos vel))))
    (mcp-text (string-append "Added '" name "' orbiting '" central
                              "' at r=" (fmt-num radius)
                              " v=" (fmt-num v0)))))

;;; sim-step
(define (tool-step args)
  (let* ((n    (inexact->exact (arg? args 'steps 1)))
         (prog (arg? args 'progress #f)))
    (if (null? *bodies*)
        (mcp-text "No bodies in simulation.")
        (begin
          (let loop ((i 0))
            (when (< i n)
              (leapfrog-step! *bodies* *D* *G* *dt*)
              (set! *t* (+ *t* *dt*))
              (when (and prog (= (modulo i (max 1 (quotient n 10))) 0))
                (mcp-notify-progress i n (string-append "t=" (fmt-num *t*))))
              (loop (+ i 1))))
          (mcp-text (string-append "Stepped " (number->string n) " iterations. t="
                                   (fmt-num *t*)))))))

;;; sim-state
(define (tool-state args)
  (if (null? *bodies*)
      (mcp-text (string-append "t=" (fmt-num *t*) " D=" (fmt-num *D*) " — no bodies"))
      (mcp-text
        (apply string-append
               (list* "t=" (fmt-num *t*)
                      " D=" (fmt-num *D*)
                      " G=" (fmt-num *G*)
                      " dt=" (fmt-num *dt*) "\n"
                      (map (lambda (b) (string-append (fmt-body b) "\n"))
                           *bodies*))))))

;;; sim-energy
(define (tool-energy args)
  (if (null? *bodies*)
      (mcp-text "No bodies.")
      (let* ((KE (apply + (map (lambda (b)
                                 (* 0.5 (body-mass b) (vec-norm2 (body-vel b))))
                               *bodies*)))
             ; Potential: -G*mi*mj / ((D-2)*r^(D-2))  for D≠2
             ;            +G*mi*mj * ln(r)             for D=2
             (PE (let loop ((bs *bodies*) (acc 0.0))
                   (if (null? bs) acc
                       (let inner ((js (cdr bs)) (bi (car bs)) (a acc))
                         (if (null? js)
                             (loop (cdr bs) a)
                             (let* ((dr  (vec-sub (body-pos (car js)) (body-pos bi)))
                                    (r   (vec-norm dr))
                                    (mm  (* (body-mass bi) (body-mass (car js))))
                                    (u   (if (< (abs (- *D* 2.0)) 1e-10)
                                             (* *G* mm (log r))
                                             (- (/ (* *G* mm)
                                                   (* (- *D* 2.0) (expt r (- *D* 2.0))))))))
                               (inner (cdr js) bi (+ a u)))))))))
        (mcp-text (string-append "KE=" (fmt-num KE)
                                  " PE=" (fmt-num PE)
                                  " E=" (fmt-num (+ KE PE)))))))


;;; ---- Tools ----

(mcp-tool "sim-reset"
  "Reset the simulation: remove all bodies and restore default parameters
(D=3, G=1, dt=0.01, t=0)."
  '()
  tool-reset)


(mcp-tool "sim-set"
  "Set simulation parameters. All arguments are optional.
  D  — spatial dimension (default 3.0; may be non-integer, e.g. 2.5)
  G  — gravitational constant (default 1.0)
  dt — time step for the leapfrog integrator (default 0.01)"
  '((D  . ((type . "number") (description . "Spatial dimension") (default . 3.0)))
    (G  . ((type . "number") (description . "Gravitational constant") (default . 1.0)))
    (dt . ((type . "number") (description . "Time step") (default . 0.01))))
  tool-set)


(mcp-tool "sim-add-body"
  "Add a body to the simulation.
  name     — identifier string
  mass     — body mass
  position — list of coordinates, e.g. [1.0, 0.0, 0.0]
  velocity — list of velocity components (default zero)"
  '((name     . ((type . "string") (description . "Body name")))
    (mass     . ((type . "number") (description . "Body mass")))
    (position . ((type . "array")  (description . "Position vector, e.g. [1.0, 0.0, 0.0]")))
    (velocity . ((type . "array")  (description . "Velocity vector (default zero)")
                                   (default . ()))))
  tool-add-body)


(mcp-tool "sim-add-orbit"
  "Add a satellite body in a circular orbit around a named central body.
The orbit is in the x-y plane. Requires D > 2 (circular orbit undefined for D ≤ 2).
Orbital speed: v = sqrt(G * M_central / r^(D-2))."
  '((name    . ((type . "string") (description . "New satellite name")))
    (central . ((type . "string") (description . "Name of the central body to orbit")))
    (mass    . ((type . "number") (description . "Satellite mass")))
    (radius  . ((type . "number") (description . "Orbital radius"))))
  tool-add-orbit)


(mcp-tool "sim-step"
  "Advance the simulation by N leapfrog steps.
  steps    — number of steps (default 1)
  progress — if true, emit progress notifications every 10%"
  '((steps    . ((type . "integer") (description . "Number of steps to run") (default . 1)))
    (progress . ((type . "boolean") (description . "Emit progress notifications") (default . #f))))
  tool-step)


(mcp-tool "sim-state"
  "Return the current simulation state: time, parameters, and all body positions/velocities."
  '()
  tool-state)


(mcp-tool "sim-energy"
  "Compute the total mechanical energy of the system.
Reports kinetic energy (KE), potential energy (PE), and total E = KE + PE.
The potential uses the D-dimensional formula:
  U = -G*mi*mj / ((D-2) * r^(D-2))   for D ≠ 2
  U =  G*mi*mj * ln(r)                for D = 2"
  '()
  tool-energy)


;;; ---- Resource ----

(mcp-resource "sim://state"
  "Current simulation state (live snapshot)"
  (lambda (uri) (tool-state '())))


(mcp-serve "curry-nbody" "0.7.2")
