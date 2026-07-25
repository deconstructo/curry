;;; tetrabrot.scm — Bicomplex (ℂ²) Tetrabrot explorer
;;;
;;; Iterates w → w² + c in the bicomplex number system ℂ(i₁,i₂):
;;;   w = (a + b·i₁) + (c + d·i₁)·i₂,  i₁²=i₂²=-1, i₁i₂=i₂i₁
;;; The ℝ⁴ parameter space is explored as selectable 2D cross-sections.
;;; The (x₀,x₁) slice reproduces the ordinary Mandelbrot set.
;;; Diagonal slices (x₀×x₂, x₁×x₃ …) reveal the four-fold symmetry
;;; and the characteristic "Tetrabrot" shape.
;;;
;;; Controls:
;;;   Left-drag   pan              Scroll / pinch  zoom to cursor
;;;   Double-click  zoom 2×        R  reset view
;;;
;;; Run:  ./build/curry examples/tetrabrot.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Fragment shader ──────────────────────────────────────────────────────────

(define tetra-frag-src "
#version 400 core
out vec4 frag_color;

uniform vec2  u_resolution;
uniform float u_dpr;
uniform vec4  u_c4;       /* 4D centre (x0,x1,x2,x3) */
uniform float u_zoom;
uniform int   u_max_iter;
uniform int   u_palette;
uniform int   u_axis_h;   /* ℝ⁴ component → screen-x  (0-3) */
uniform int   u_axis_v;   /* ℝ⁴ component → screen-y  (0-3) */

vec3 iqpal(float t,vec3 a,vec3 b,vec3 c,vec3 d){return a+b*cos(6.28318530718*(c*t+d));}
vec3 palette(float r){
    float t=fract(r*0.015);
    if(u_palette==1)return iqpal(t,vec3(.5),vec3(.5),vec3(1,1,.5),vec3(.8,.9,.3));
    if(u_palette==2)return iqpal(t,vec3(.5),vec3(.5),vec3(.8,.8,.5),vec3(0,.2,.5));
    if(u_palette==3)return iqpal(t,vec3(.5),vec3(.5),vec3(2,1,0),vec3(.5,.2,.25));
    if(u_palette==4)return iqpal(t,vec3(.5,.4,.2),vec3(.5,.4,.3),vec3(1,1,.5),vec3(0,.1,.2));
    return iqpal(t,vec3(.5),vec3(.5),vec3(1.),vec3(0.,.33,.67));
}
float smooth_n(float i,float ns){return i+1.0-log2(log2(sqrt(ns)));}

/* Bicomplex squaring  w=(a+bi₁)+(c+di₁)i₂
   w² = (a²-b²-c²+d²) + 2(ab-cd)i₁ + 2(ac-bd)i₂ + 2(ad+bc)i₁i₂ */
vec4 bc_sq(vec4 w){
    float a=w.x,b=w.y,c=w.z,d=w.w;
    return vec4(a*a-b*b-c*c+d*d,
                2.*(a*b-c*d),
                2.*(a*c-b*d),
                2.*(a*d+b*c));
}

/* Dynamic vec4 access — GLSL lacks variable indexing on vec4 */
float get4(vec4 v,int i){if(i==0)return v.x;if(i==1)return v.y;if(i==2)return v.z;return v.w;}
vec4  set4(vec4 v,int i,float x){
    if(i==0)return vec4(x,v.y,v.z,v.w); if(i==1)return vec4(v.x,x,v.z,v.w);
    if(i==2)return vec4(v.x,v.y,x,v.w); return vec4(v.x,v.y,v.z,x);
}

float tetrabrot(vec4 c){
    vec4 z=vec4(0.);
    for(int i=0;i<u_max_iter;i++){
        float ns=dot(z,z);
        if(ns>4.) return smooth_n(float(i),ns);
        z=bc_sq(z)+c;
    }
    return -1.;
}

void main(){
    float pzoom=u_zoom*u_dpr;
    float wx=( gl_FragCoord.x-u_resolution.x*.5)/pzoom;
    float wy=-(gl_FragCoord.y-u_resolution.y*.5)/pzoom;
    /* inject screen offsets into the two displayed axes; others hold u_c4 fixed */
    vec4 c=set4(set4(u_c4,u_axis_h,get4(u_c4,u_axis_h)+wx),
                          u_axis_v,get4(u_c4,u_axis_v)+wy);
    float t=tetrabrot(c);
    frag_color=(t<0.)?vec4(0,0,0,1):vec4(palette(t),1);
}
")

(define tetra-prog (make-gl-shader tetra-frag-src))

;;; ── View state ───────────────────────────────────────────────────────────────

(define *c4*       (list 0.0 0.0 0.0 0.0))  ; full 4D centre
(define *zoom*     200.0)
(define *max-iter* 150)
(define *palette*  0)
(define *axis-h*   0)   ; which ℝ⁴ component → screen-x
(define *axis-v*   1)   ; which ℝ⁴ component → screen-y
(define *W* 800) (define *H* 600)

(define *axis-names* (list "x₀  Re(z₁)" "x₁  Im(z₁)" "x₂  Re(z₂)" "x₃  Im(z₂)"))
(define (axis-name i) (list-ref *axis-names* i))

(define (c4-ref i) (list-ref *c4* i))
(define (c4-set! i v)
  (set! *c4* (let lp ((j 0) (lst *c4*))
               (if (null? lst) '()
                   (cons (if (= j i) v (car lst)) (lp (+ j 1) (cdr lst)))))))

(define *axis-pairs*
  (list (list 0 1) (list 0 2) (list 0 3)
        (list 1 2) (list 1 3) (list 2 3)))

(define *axis-pair-names*
  (list "x₀ × x₁  (standard ℂ)"
        "x₀ × x₂  (Re diagonal)"
        "x₀ × x₃  (anti-diagonal)"
        "x₁ × x₂  (Im-Re cross)"
        "x₁ × x₃  (Im diagonal)"
        "x₂ × x₃  (z₂ plane)"))

(define (fixed-axes h v)
  (filter (lambda (i) (and (not (= i h)) (not (= i v)))) '(0 1 2 3)))

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f) (define *drag-y* #f)
(define *drag-ch* 0.0) (define *drag-cv* 0.0)
(define *canvas* #f)

;;; ── Fixed-axis slider state (forward-referenced from update-fixed-sliders!) ──
;;; These are assigned in the tab2 section; update-fixed-sliders! is only ever
;;; called at runtime (not at load time), so the forward references are safe.

(define *fax-labels*  #f)   ; list of 2 QLabel widgets
(define *fax-current* #f)   ; list of 2 axis indices currently controlled

(define (update-fixed-sliders!)
  (when *fax-labels*
    (let ((fa (fixed-axes *axis-h* *axis-v*)))
      (set! *fax-current* fa)
      (label-set-text! (list-ref *fax-labels* 0)
                       (string-append (axis-name (car fa))  "  (fixed)"))
      (label-set-text! (list-ref *fax-labels* 1)
                       (string-append (axis-name (cadr fa)) "  (fixed)")))))

;;; ── Coordinate display ───────────────────────────────────────────────────────

(define (coord->string x)
  (let* ((places (min 10 (+ 2 (inexact->exact (ceiling (/ (log (max *zoom* 1.0)) (log 10.0)))))))
         (neg (< x 0.0)) (ax (if neg (- x) x))
         (scaled (inexact->exact (round (* ax (expt 10.0 places)))))
         (s (number->string scaled))
         (padded (let lp ((s s))
                   (if (>= (string-length s) places) s (lp (string-append "0" s)))))
         (ilen (- (string-length padded) places))
         (ipart (if (= ilen 0) "0" (substring padded 0 ilen)))
         (fpart (substring padded ilen (string-length padded))))
    (string-append (if neg "-" "") ipart "." fpart)))

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (gl-shader-draw! tetra-prog painter
    (list (cons "u_c4"       (list (c4-ref 0) (c4-ref 1) (c4-ref 2) (c4-ref 3)))
          (cons "u_zoom"     *zoom*)
          (cons "u_max_iter" *max-iter*)
          (cons "u_palette"  *palette*)
          (cons "u_axis_h"   *axis-h*)
          (cons "u_axis_v"   *axis-v*)))
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (let ((fa (fixed-axes *axis-h* *axis-v*)))
    (gfx-draw-text! painter 8 (- h 10)
      (string-append
        "Tetrabrot ℂ²"
        "  iter=" (number->string *max-iter*)
        "  zoom×" (number->string (inexact->exact (round *zoom*)))
        "  [" (axis-name *axis-h*) " × " (axis-name *axis-v*) "]"
        "  (" (coord->string (c4-ref *axis-h*))
        ", " (coord->string (c4-ref *axis-v*)) ")"
        "  fixed: x" (number->string (car fa))  "=" (coord->string (c4-ref (car fa)))
        "  x" (number->string (cadr fa)) "=" (coord->string (c4-ref (cadr fa)))))))

;;; ── Navigation ───────────────────────────────────────────────────────────────

(define (reset-view!)
  (set! *c4* (list 0.0 0.0 0.0 0.0))
  (set! *zoom* 200.0)
  (canvas-redraw! *canvas*))

(define (zoom-step! factor)
  (set! *zoom* (* *zoom* factor)) (canvas-redraw! *canvas*))

(define (zoom-to-cursor! factor sx sy)
  (let* ((wh (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
         (wv (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))
         (nz (* *zoom* factor)))
    (c4-set! *axis-h* (- (+ (c4-ref *axis-h*) wh)
                         (/ (- (inexact sx) (/ (inexact *W*) 2.0)) nz)))
    (c4-set! *axis-v* (- (+ (c4-ref *axis-v*) wv)
                         (/ (- (inexact sy) (/ (inexact *H*) 2.0)) nz)))
    (set! *zoom* nz) (canvas-redraw! *canvas*)))

;;; ── Mouse handlers ───────────────────────────────────────────────────────────

(define (on-mouse-press! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x* x) (set! *drag-y* y)
    (set! *drag-ch* (c4-ref *axis-h*))
    (set! *drag-cv* (c4-ref *axis-v*))))

(define (on-mouse-release! x y btn)
  (when (equal? btn 'left) (set! *drag-x* #f)))

(define (on-mouse-move! x y)
  (when *drag-x*
    (c4-set! *axis-h* (- *drag-ch* (/ (- x *drag-x*) *zoom*)))
    (c4-set! *axis-v* (- *drag-cv* (/ (- y *drag-y*) *zoom*)))
    (canvas-redraw! *canvas*)))

;;; ── Window ───────────────────────────────────────────────────────────────────

(define win     (make-window "Tetrabrot — Bicomplex Mandelbrot ℂ²" 1200 720))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))
(set! *canvas* canvas)

;;; ── Tab 1: Render ────────────────────────────────────────────────────────────

(define tab1 (make-vbox))
(define (t1! w) (layout-add! tab1 w))

(t1! (make-label "Display axis pair"))
(define axis-dropdown
  (make-dropdown *axis-pair-names* 0
    (lambda (i)
      (let ((p (list-ref *axis-pairs* i)))
        (set! *axis-h* (car p))
        (set! *axis-v* (cadr p))
        (update-fixed-sliders!)
        (canvas-redraw! *canvas*)))))
(t1! axis-dropdown)
(t1! (make-separator))
(t1! (make-label "Colour palette"))
(t1! (make-radio-group '("Cycle" "Ultra" "Ice" "Electric" "Gold") 0
       (lambda (i) (set! *palette* i) (canvas-redraw! *canvas*))))
(t1! (make-separator))
(t1! (make-label "Max iterations"))
(t1! (make-slider "Iterations" 20 800 10 150
       (lambda (v) (set! *max-iter* (inexact->exact v)) (canvas-redraw! *canvas*))))
(t1! (make-separator))
(t1! (make-button "Reset view  (R)" reset-view!))

;;; ── Tab 2: Navigate ──────────────────────────────────────────────────────────

(define tab2 (make-vbox))
(define (t2! w) (layout-add! tab2 w))

;;; Fixed-axis sliders — relabelled whenever axis pair changes
(t2! (make-label "Fixed-axis values  (slice position in ℝ⁴)"))

(set! *fax-labels*  (list (make-label "–") (make-label "–")))
(set! *fax-current* (fixed-axes 0 1))

(t2! (list-ref *fax-labels* 0))
(define fslider0
  (make-slider "–" -200 200 1 0
    (lambda (v)
      (c4-set! (car *fax-current*) (* v 0.01))
      (canvas-redraw! *canvas*))))
(t2! fslider0)

(t2! (list-ref *fax-labels* 1))
(define fslider1
  (make-slider "–" -200 200 1 0
    (lambda (v)
      (c4-set! (cadr *fax-current*) (* v 0.01))
      (canvas-redraw! *canvas*))))
(t2! fslider1)

(t2! (make-separator))

;;; Bookmarks — store full 4D state: (name x0 x1 x2 x3 axis-h axis-v zoom)
(define *bookmarks*
  (list
    (list "Origin — standard ℂ slice"  0.0  0.0  0.0  0.0  0 1 200.0)
    (list "x₀×x₂ — Re diagonal"        0.0  0.0  0.0  0.0  0 2 200.0)
    (list "x₀×x₃ — anti-diagonal"      0.0  0.0  0.0  0.0  0 3 200.0)
    (list "x₁×x₂ — Im-Re cross"        0.0  0.0  0.0  0.0  1 2 200.0)
    (list "x₁×x₃ — Im diagonal"        0.0  0.0  0.0  0.0  1 3 200.0)
    (list "x₂×x₃ — z₂ plane"           0.0  0.0  0.0  0.0  2 3 200.0)))
(define *n-builtins* 6)

(define *bookmarks-path*
  (string-append (or (get-environment-variable "HOME") ".") "/.tetrabrot_bookmarks.scm"))

(define (load-bookmarks!)
  (guard (e (#t #f))
    (call-with-port (open-input-file *bookmarks-path*)
      (lambda (p)
        (let loop ((acc *bookmarks*))
          (let ((x (read p)))
            (if (eof-object? x) (set! *bookmarks* acc)
                (loop (append acc (list x))))))))))

(define (save-bookmarks!)
  (guard (e (#t #f))
    (call-with-port (open-output-file *bookmarks-path*)
      (lambda (p)
        (for-each (lambda (b) (write b p) (newline p))
                  (list-tail *bookmarks* *n-builtins*))))))

(define (sync-axis-dropdown-to! ah av)
  (let ((pi (let lp ((ps *axis-pairs*) (k 0))
              (if (null? ps) 0
                  (if (and (= (caar ps) ah) (= (cadar ps) av)) k
                      (lp (cdr ps) (+ k 1)))))))
    (dropdown-set-index! axis-dropdown pi)))

(t2! (make-label "Bookmarks"))
(define bm-dropdown
  (make-dropdown (map car *bookmarks*) 0
    (lambda (i)
      (let* ((bm (list-ref *bookmarks* i))
             (ah (list-ref bm 5)) (av (list-ref bm 6)))
        (set! *c4*    (list (list-ref bm 1) (list-ref bm 2)
                            (list-ref bm 3) (list-ref bm 4)))
        (set! *axis-h* ah)
        (set! *axis-v* av)
        (set! *zoom*   (list-ref bm 7))
        (sync-axis-dropdown-to! ah av)
        (update-fixed-sliders!)
        (canvas-redraw! *canvas*)))))
(t2! bm-dropdown)

(t2! (make-separator))
(t2! (make-label "Save current view"))
(define name-field (make-text-input "Name" (lambda (s) (void))))
(t2! name-field)
(t2! (make-button "Save bookmark"
       (lambda ()
         (let ((name (text-value name-field)))
           (when (> (string-length name) 0)
             (let ((bm (list name
                             (c4-ref 0) (c4-ref 1) (c4-ref 2) (c4-ref 3)
                             *axis-h* *axis-v* *zoom*)))
               (set! *bookmarks* (append *bookmarks* (list bm)))
               (save-bookmarks!)
               (dropdown-add-item! bm-dropdown name)
               (dropdown-set-index! bm-dropdown (- (dropdown-count bm-dropdown) 1))
               (text-set-value! name-field "")
               (display (string-append "Bookmark saved: " name "\n"))))))))

(t2! (make-separator))
(t2! (make-label "Export"))
(t2! (make-button "Save PNG…"
       (lambda ()
         (let ((path (file-save-dialog "Save PNG" "Images (*.png)")))
           (when (and path (> (string-length path) 0))
             (display (if (canvas-save-png! canvas path)
                          (string-append "Saved: " path "\n")
                          "PNG save failed\n")))))))

;;; ── Assemble tabs ────────────────────────────────────────────────────────────

(define tabs (make-tabs))
(tabs-add! tabs tab1 "Render")
(tabs-add! tabs tab2 "Navigate")
(box-add! sidebar tabs)

;;; ── Canvas events ────────────────────────────────────────────────────────────

(canvas-on-draw! canvas
  (lambda (painter w h)
    (set! *W* w) (set! *H* h)
    (draw-frame painter w h)))

(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)                  (on-mouse-press!   x y btn))
      ((equal? ev 'release)                (on-mouse-release! x y btn))
      ((or (equal? ev 'move)
           (equal? ev 'drag))              (on-mouse-move!    x y))
      ((equal? ev 'double-press)
       (when (equal? btn 'left)            (zoom-to-cursor! 2.0 x y))))))

(canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (zoom-to-cursor! (expt 1.002 dy) x y)))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")                       (reset-view!))
      ((or (equal? key "=") (equal? key "+")) (zoom-step! 1.5))
      ((equal? key "-")                       (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q") (equal? key "Escape")) (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (update-fixed-sliders!)
    (unless (qt-gpu? canvas)
      (display "[tetrabrot] warning: OpenGL unavailable\n"))
    (canvas-redraw! canvas)))

(window-on-close! win (lambda () (quit-event-loop)))

(load-bookmarks!)
(window-show! win)
(run-event-loop)
