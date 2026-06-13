;;; holo_explorer.scm — arbitrary-polynomial fractal explorer
;;;
;;; Polynomial: a₀z^n + a₁z^(n-1) + … + aₙ
;;; The constant term can be the escape parameter c or a fixed complex value.
;;; Degree 2..8; coefficients edited live in the sidebar.
;;;
;;; Parameter space mode: z₀=0, c=pixel → Mandelbrot analogue
;;; Julia mode: z₀=pixel, c=fixed → Julia set for that c
;;; Click in parameter space mode to set Julia c.
;;;
;;; Controls:
;;;   Left-drag      pan         Scroll     zoom to cursor
;;;   Click          set Julia c (param-space mode)
;;;   Double-click   zoom 2×     R          reset view
;;;
;;; Run: ./build/curry examples/holo_explorer.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ── Fragment shader ──────────────────────────────────────────────────────────

(define holo-frag-src "
#version 400 core
out vec4 frag_color;

uniform vec2  u_resolution;
uniform float u_dpr;
uniform vec2  u_center;
uniform float u_zoom;
uniform int   u_max_iter;
uniform int   u_palette;
uniform int   u_degree;          // 2..8
uniform vec2  u_c[9];            // coefficients in descending degree order
uniform int   u_julia_mode;      // 0=parameter space  1=Julia
uniform vec2  u_julia_c;         // fixed c in Julia mode
uniform int   u_c_slot_fixed;    // 0=last coeff replaced by c  1=always fixed
uniform int   u_coloring;        // 0=smooth escape  1=orbit trap
uniform int   u_trap_type;       // 0=point  1=circle  2=line
uniform vec4  u_trap;            // point/circle-center: .xy; circle-r: .z; line-dir: .zw

vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x);
}

vec2 poly_eval(vec2 z, vec2 c_val) {
    vec2 r = u_c[0];
    for (int i = 1; i <= 8; i++) {
        vec2 coeff = (i == u_degree && u_c_slot_fixed == 0) ? c_val : u_c[i];
        r = cmul(r, z) + coeff;
        if (i == u_degree) break;
    }
    return r;
}

float trap_dist(vec2 z) {
    if (u_trap_type == 1)
        return abs(length(z - u_trap.xy) - u_trap.z);
    if (u_trap_type == 2) {
        vec2 d = z - u_trap.xy;
        return abs(d.x * u_trap.w - d.y * u_trap.z);
    }
    return length(z - u_trap.xy);
}

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

void main() {
    float pzoom = u_zoom * u_dpr;
    float wx = ( gl_FragCoord.x - u_resolution.x * 0.5) / pzoom;
    float wy = -(gl_FragCoord.y - u_resolution.y * 0.5) / pzoom;
    vec2 pixel = vec2(u_center.x + wx, u_center.y + wy);

    vec2 z     = (u_julia_mode == 1) ? pixel : vec2(0.0);
    vec2 c_val = (u_julia_mode == 1) ? u_julia_c : pixel;

    float escape_t = -1.0;
    float min_trap = 1e10;

    for (int i = 0; i < u_max_iter; i++) {
        if (u_coloring == 1) min_trap = min(min_trap, trap_dist(z));
        float ns = dot(z, z);
        if (ns > 4.0) { escape_t = smooth_n(float(i), ns); break; }
        z = poly_eval(z, c_val);
    }

    vec3 col;
    if (u_coloring == 1) {
        col = (min_trap < 1e9) ? palette(min_trap * 15.0) : vec3(0.0);
    } else {
        col = (escape_t < 0.0) ? vec3(0.0) : palette(escape_t);
    }
    frag_color = vec4(col, 1.0);
}
")

(define holo-prog (make-gl-shader holo-frag-src))

;;; ── State ────────────────────────────────────────────────────────────────────

(define *degree*  2)
(define *coeffs-re* (make-vector 9 0.0))
(define *coeffs-im* (make-vector 9 0.0))
(vector-set! *coeffs-re* 0 1.0)   ; default: z² + c

(define *c-slot-fixed* #f)

(define *julia-mode* #f)
(define *julia-cx*   0.0)
(define *julia-cy*   0.0)

(define *coloring*    0)   ; 0=smooth  1=trap
(define *trap-type*   0)   ; 0=point  1=circle  2=line
(define *trap-px*     0.0) (define *trap-py* 0.0)
(define *trap-r*      0.5)
(define *trap-angle*  0.0)

(define *cx* 0.0) (define *cy* 0.0) (define *zoom* 200.0)
(define *max-iter* 150) (define *palette* 0)
(define *canvas* #f) (define *W* 800) (define *H* 600)

(define *drag-x* #f) (define *drag-y* #f)
(define *drag-cx* 0.0) (define *drag-cy* 0.0)
(define *click-moved* #f)

;;; ── Helpers ──────────────────────────────────────────────────────────────────

(define (inexact-or-zero s)
  (let ((v (string->number s))) (if v (inexact v) #f)))

(define (fmt n)
  (let* ((s (number->string (inexact (round (* n 1e6))))))
    (string-append (substring s 0 (max 1 (- (string-length s) 6)))
                   "."
                   (let ((f (string-append "000000"
                              (if (< (string-length s) 6) s
                                  (substring s (- (string-length s) 6)
                                               (string-length s))))))
                     (substring f (- (string-length f) 6) (string-length f))))))

(define (coeff-label i)
  (let ((power (- *degree* i)))
    (string-append
      "a" (number->string i)
      (cond ((= power 0)
             (string-append "   1"
                            (if *c-slot-fixed* " (fixed)" " (c)")))
            ((= power 1) "   z")
            (else (string-append "   z^" (number->string power)))))))

(define (trap-uniform)
  (cond
    ((= *trap-type* 0) (list *trap-px* *trap-py* 0.0 0.0))
    ((= *trap-type* 1) (list *trap-px* *trap-py* *trap-r* 0.0))
    (else              (list *trap-px* *trap-py*
                              (cos *trap-angle*) (sin *trap-angle*)))))

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (set! *W* w) (set! *H* h)
  (gl-shader-draw! holo-prog painter
    (append
      (list
        (cons "u_center"       (list *cx* *cy*))
        (cons "u_zoom"         *zoom*)
        (cons "u_max_iter"     *max-iter*)
        (cons "u_palette"      *palette*)
        (cons "u_degree"       *degree*)
        (cons "u_julia_mode"   (if *julia-mode* 1 0))
        (cons "u_julia_c"      (list *julia-cx* *julia-cy*))
        (cons "u_c_slot_fixed" (if *c-slot-fixed* 1 0))
        (cons "u_coloring"     *coloring*)
        (cons "u_trap_type"    *trap-type*)
        (cons "u_trap"         (trap-uniform)))
      (let loop ((i 0) (acc '()))
        (if (> i 8) (reverse acc)
            (loop (+ i 1)
                  (cons (cons (string-append "u_c[" (number->string i) "]")
                              (list (vector-ref *coeffs-re* i)
                                    (vector-ref *coeffs-im* i)))
                        acc))))))
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      "deg " (number->string *degree*)
      (if *julia-mode*
          (string-append "  julia c=(" (number->string *julia-cx*)
                         "," (number->string *julia-cy*) ")")
          "  param-space")
      "  iter=" (number->string *max-iter*)
      "  zoom×" (number->string (inexact->exact (round *zoom*))))))

;;; ── Navigation ───────────────────────────────────────────────────────────────

(define (reset-view!)
  (set! *cx* 0.0) (set! *cy* 0.0) (set! *zoom* 200.0)
  (canvas-redraw! *canvas*))

(define (zoom-to-cursor! factor sx sy)
  (let* ((wx (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
         (wy (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))
         (nz (* *zoom* factor)))
    (set! *cx* (- (+ *cx* wx) (/ (- (inexact sx) (/ (inexact *W*) 2.0)) nz)))
    (set! *cy* (- (+ *cy* wy) (/ (- (inexact sy) (/ (inexact *H*) 2.0)) nz)))
    (set! *zoom* nz)
    (canvas-redraw! *canvas*)))

;;; ── Window ───────────────────────────────────────────────────────────────────

(define win     (make-window "Holo Explorer" 1200 750))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))
(set! *canvas* canvas)

;;; ── Coefficient group widgets ────────────────────────────────────────────────

;; Each entry: #(vbox label re-input im-input)
(define *coeff-groups* (make-vector 9 #f))
(define coeff-scroll (make-scroll-area))

(define (coeff-group-box i)      (vector-ref (vector-ref *coeff-groups* i) 0))
(define (coeff-group-label i)    (vector-ref (vector-ref *coeff-groups* i) 1))
(define (coeff-group-re-in i)    (vector-ref (vector-ref *coeff-groups* i) 2))
(define (coeff-group-im-in i)    (vector-ref (vector-ref *coeff-groups* i) 3))

(define (make-coeff-update-fn idx part)
  (lambda (s)
    (let ((v (inexact-or-zero s)))
      (when v
        (if (eq? part 're)
            (vector-set! *coeffs-re* idx v)
            (vector-set! *coeffs-im* idx v))
        (canvas-redraw! *canvas*)))))

(let loop ((i 0))
  (when (<= i 8)
    (let* ((vb  (make-vbox))
           (lbl (make-label (coeff-label i)))
           (rei (make-text-input "Re" (make-coeff-update-fn i 're)))
           (imi (make-text-input "Im" (make-coeff-update-fn i 'im))))
      (layout-add! vb lbl)
      (layout-add! vb rei)
      (layout-add! vb imi)
      (layout-add! coeff-scroll vb)
      (vector-set! *coeff-groups* i (vector vb lbl rei imi)))
    (loop (+ i 1))))

(define (refresh-coeff-ui!)
  (let loop ((i 0))
    (when (<= i 8)
      (widget-set-visible! (coeff-group-box i) (<= i *degree*))
      (loop (+ i 1))))
  (let loop ((i 0))
    (when (<= i *degree*)
      (label-set-text! (coeff-group-label i) (coeff-label i))
      (text-set-value! (coeff-group-re-in i)
                       (number->string (vector-ref *coeffs-re* i)))
      (text-set-value! (coeff-group-im-in i)
                       (number->string (vector-ref *coeffs-im* i)))
      (loop (+ i 1)))))

(define (apply-preset! degree re-list im-list)
  (set! *degree* degree)
  (let loop ((i 0))
    (when (<= i 8)
      (vector-set! *coeffs-re* i (if (< i (length re-list)) (list-ref re-list i) 0.0))
      (vector-set! *coeffs-im* i (if (< i (length im-list)) (list-ref im-list i) 0.0))
      (loop (+ i 1))))
  (refresh-coeff-ui!)
  (canvas-redraw! *canvas*))

;;; Initial display: show groups 0..2, hide 3..8
(let loop ((i 0))
  (when (<= i 8)
    (widget-set-visible! (coeff-group-box i) (<= i *degree*))
    (loop (+ i 1))))
(text-set-value! (coeff-group-re-in 0) "1")

;;; ── Tab 1: Polynomial ────────────────────────────────────────────────────────

(define tab1 (make-vbox))
(define (t1! w) (layout-add! tab1 w))

(t1! (make-label "Degree"))
(define degree-drop
  (make-dropdown '("2" "3" "4" "5" "6" "7" "8") 0
    (lambda (i)
      (set! *degree* (+ i 2))
      (refresh-coeff-ui!)
      (canvas-redraw! *canvas*))))
(t1! degree-drop)

(t1! (make-separator))
(t1! (make-label "Coefficients  (descending degree)"))
(t1! coeff-scroll)

(t1! (make-separator))
(t1! (make-toggle "c-slot is fixed constant" #f
       (lambda (on?)
         (set! *c-slot-fixed* on?)
         (refresh-coeff-ui!)
         (canvas-redraw! *canvas*))))

(t1! (make-separator))
(t1! (make-label "Presets"))
(t1! (make-button "z² + c"
       (lambda ()
         (apply-preset! 2 '(1.0 0.0 0.0) '(0.0 0.0 0.0))
         (dropdown-set-index! degree-drop 0))))
(t1! (make-button "z³ + c"
       (lambda ()
         (apply-preset! 3 '(1.0 0.0 0.0 0.0) '(0.0 0.0 0.0 0.0))
         (dropdown-set-index! degree-drop 1))))
(t1! (make-button "z⁴ + c"
       (lambda ()
         (apply-preset! 4 '(1.0 0.0 0.0 0.0 0.0) '(0.0 0.0 0.0 0.0 0.0))
         (dropdown-set-index! degree-drop 2))))
(t1! (make-button "z² - z + c"
       (lambda ()
         (apply-preset! 2 '(1.0 -1.0 0.0) '(0.0 0.0 0.0))
         (dropdown-set-index! degree-drop 0))))
(t1! (make-button "z³ - 3z + c  (tricorn)"
       (lambda ()
         (apply-preset! 3 '(1.0 0.0 -3.0 0.0) '(0.0 0.0 0.0 0.0))
         (dropdown-set-index! degree-drop 1))))

;;; ── Tab 2: Mode ──────────────────────────────────────────────────────────────

(define tab2 (make-vbox))
(define (t2! w) (layout-add! tab2 w))

(define julia-cx-input #f)
(define julia-cy-input #f)

(define (update-julia-display!)
  (when julia-cx-input
    (text-set-value! julia-cx-input (number->string *julia-cx*))
    (text-set-value! julia-cy-input (number->string *julia-cy*))))

(t2! (make-label "Iteration mode"))
(t2! (make-radio-group '("Parameter space" "Julia") 0
       (lambda (i)
         (set! *julia-mode* (= i 1))
         (canvas-redraw! *canvas*))))

(t2! (make-separator))
(t2! (make-label "Julia parameter c"))
(t2! (make-label "(click canvas in param mode to pick)"))
(set! julia-cx-input
  (make-text-input "Re  e.g. -0.7"
    (lambda (s)
      (let ((v (inexact-or-zero s)))
        (when v (set! *julia-cx* v) (canvas-redraw! *canvas*))))))
(set! julia-cy-input
  (make-text-input "Im  e.g. 0.27"
    (lambda (s)
      (let ((v (inexact-or-zero s)))
        (when v (set! *julia-cy* v) (canvas-redraw! *canvas*))))))
(t2! julia-cx-input)
(t2! julia-cy-input)
(t2! (make-button "Show Julia for current c"
       (lambda ()
         (set! *julia-mode* #t)
         (canvas-redraw! *canvas*))))
(t2! (make-button "Back to parameter space"
       (lambda ()
         (set! *julia-mode* #f)
         (canvas-redraw! *canvas*))))

;;; ── Tab 3: Colour ────────────────────────────────────────────────────────────

(define tab3 (make-vbox))
(define (t3! w) (layout-add! tab3 w))

(define trap-group (make-vbox))

(t3! (make-label "Colouring"))
(t3! (make-radio-group '("Smooth escape time" "Orbit trap") 0
       (lambda (i)
         (set! *coloring* i)
         (widget-set-visible! trap-group (= i 1))
         (canvas-redraw! *canvas*))))

(t3! (make-separator))
(t3! (make-label "Palette"))
(t3! (make-radio-group '("Cycle" "Ultra" "Ice" "Electric" "Gold") 0
       (lambda (i) (set! *palette* i) (canvas-redraw! *canvas*))))

(t3! (make-separator))

;;; Trap controls — hidden until orbit trap selected
(layout-add! trap-group (make-label "Orbit trap"))
(layout-add! trap-group
  (make-radio-group '("Point" "Circle" "Line") 0
    (lambda (i) (set! *trap-type* i) (canvas-redraw! *canvas*))))
(layout-add! trap-group (make-separator))
(layout-add! trap-group (make-label "Trap X"))
(layout-add! trap-group
  (make-slider "X" -200 200 1 0
    (lambda (v) (set! *trap-px* (* v 0.01)) (canvas-redraw! *canvas*))))
(layout-add! trap-group (make-label "Trap Y"))
(layout-add! trap-group
  (make-slider "Y" -200 200 1 0
    (lambda (v) (set! *trap-py* (* v 0.01)) (canvas-redraw! *canvas*))))
(layout-add! trap-group (make-label "Radius  (circle)"))
(layout-add! trap-group
  (make-slider "R" 0 300 1 50
    (lambda (v) (set! *trap-r* (* v 0.01)) (canvas-redraw! *canvas*))))
(layout-add! trap-group (make-label "Angle °  (line)"))
(layout-add! trap-group
  (make-slider "Angle" 0 360 1 0
    (lambda (v)
      (set! *trap-angle* (* v (/ 3.14159265358979 180.0)))
      (canvas-redraw! *canvas*))))

(widget-set-visible! trap-group #f)
(t3! trap-group)

;;; ── Tab 4: View ──────────────────────────────────────────────────────────────

(define tab4 (make-vbox))
(define (t4! w) (layout-add! tab4 w))

(t4! (make-label "Max iterations"))
(t4! (make-slider "Iterations" 20 800 10 150
       (lambda (v) (set! *max-iter* (inexact->exact v)) (canvas-redraw! *canvas*))))
(t4! (make-separator))
(t4! (make-button "Reset view  (R)" reset-view!))
(t4! (make-separator))
(t4! (make-label "Export"))
(t4! (make-button "Save PNG…"
       (lambda ()
         (let ((path (file-save-dialog "Save PNG" "Images (*.png)")))
           (when (and path (> (string-length path) 0))
             (canvas-save-png! canvas path))))))

;;; ── Assemble tabs ────────────────────────────────────────────────────────────

(define tabs (make-tabs))
(tabs-add! tabs tab1 "Poly")
(tabs-add! tabs tab2 "Mode")
(tabs-add! tabs tab3 "Colour")
(tabs-add! tabs tab4 "View")
(box-add! sidebar tabs)

;;; ── Canvas events ────────────────────────────────────────────────────────────

(canvas-on-draw! canvas
  (lambda (painter w h) (draw-frame painter w h)))

(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)
       (when (equal? btn 'left)
         (set! *drag-x* x) (set! *drag-y* y)
         (set! *drag-cx* *cx*) (set! *drag-cy* *cy*)
         (set! *click-moved* #f)))
      ((or (equal? ev 'move) (equal? ev 'drag))
       (when *drag-x*
         (when (or (> (abs (- x *drag-x*)) 3) (> (abs (- y *drag-y*)) 3))
           (set! *click-moved* #t))
         (when *click-moved*
           (set! *cx* (- *drag-cx* (/ (- (inexact x) *drag-x*) *zoom*)))
           (set! *cy* (- *drag-cy* (/ (- (inexact y) *drag-y*) *zoom*)))
           (canvas-redraw! *canvas*))))
      ((equal? ev 'release)
       (when (equal? btn 'left)
         (when (and (not *click-moved*) (not *julia-mode*))
           (let* ((wx (/ (- (inexact x) (/ (inexact *W*) 2.0)) *zoom*))
                  (wy (/ (- (inexact y) (/ (inexact *H*) 2.0)) *zoom*)))
             (set! *julia-cx* (+ *cx* wx))
             (set! *julia-cy* (+ *cy* wy))
             (update-julia-display!)
             (canvas-redraw! *canvas*)))
         (set! *drag-x* #f)
         (set! *click-moved* #f)))
      ((equal? ev 'double-press)
       (when (equal? btn 'left) (zoom-to-cursor! 2.0 x y))))))

(canvas-on-scroll! canvas
  (lambda (dx dy x y mods) (zoom-to-cursor! (expt 1.002 dy) x y)))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")   (reset-view!))
      ((or (equal? key "=") (equal? key "+"))
       (set! *zoom* (* *zoom* 1.5)) (canvas-redraw! *canvas*))
      ((equal? key "-")
       (set! *zoom* (/ *zoom* 1.5)) (canvas-redraw! *canvas*))
      ((or (equal? key "q") (equal? key "Escape")) (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (unless (qt-gpu? canvas)
      (display "[holo_explorer] warning: OpenGL unavailable\n"))
    (canvas-redraw! canvas)))

(window-on-close! win (lambda () (quit-event-loop)))

(window-show! win)
(run-event-loop)
