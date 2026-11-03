#!/usr/bin/env curry
;;; gpu-cube.scm — a lit, rotating cube on the QRhi gpu-* layer
;;;
;;; Runs on Metal (macOS), Vulkan (Linux) or D3D (Windows) — whatever QRhi
;;; picks for the platform.  Shows the basic shape of a gpu-* program:
;;; shaders as strings, a vertex buffer, a std140 uniform block, a pipeline
;;; with depth testing, and a render pass per frame.
;;;
;;;   ./build/curry examples/gpu-cube.scm
;;;   ./build/curry examples/gpu-cube.scm --snapshot cube.png   ; save a frame and exit
;;;
;;; Drag with the mouse to turn the cube, scroll to zoom, Escape to quit.

(import (scheme base) (scheme inexact) (scheme process-context)
        (curry qt6) (curry gpu-math))

;;; ---- shaders (Vulkan-style GLSL: explicit locations and bindings) ----

(define vs (make-gpu-shader 'vertex "#version 440
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 colour;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_colour;
layout(std140, binding = 0) uniform Frame {
  mat4 mvp;
  mat4 model;
  vec3 light_dir;
};
void main() {
  v_normal = mat3(model) * normal;
  v_colour = colour;
  gl_Position = mvp * vec4(pos, 1.0);
}"))

(define fs (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_colour;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform Frame {
  mat4 mvp;
  mat4 model;
  vec3 light_dir;
};
void main() {
  float diffuse = max(dot(normalize(v_normal), normalize(light_dir)), 0.0);
  frag = vec4(v_colour * (0.25 + 0.75 * diffuse), 1.0);
}"))

;;; ---- geometry: 6 faces x 2 triangles, position + normal + colour ----

(define faces
  ;; normal          colour            two edge directions spanning the face
  '(((1 0 0)  (0.9 0.3 0.3) (0 1 0) (0 0 1))
    ((-1 0 0) (0.3 0.9 0.9) (0 0 1) (0 1 0))
    ((0 1 0)  (0.3 0.9 0.3) (0 0 1) (1 0 0))
    ((0 -1 0) (0.9 0.3 0.9) (1 0 0) (0 0 1))
    ((0 0 1)  (0.3 0.3 0.9) (1 0 0) (0 1 0))
    ((0 0 -1) (0.9 0.9 0.3) (0 1 0) (1 0 0))))

(define (face-vertices f)
  (let* ((n (car f)) (c (cadr f)) (u (caddr f)) (v (list-ref f 3))
         (corner (lambda (a b) (v3+ n (v3+ (v3* u a) (v3* v b)))))
         (vert (lambda (p) (append p n c))))
    (apply append
           (map vert (list (corner -1 -1) (corner 1 -1) (corner -1 1)
                           (corner -1 1) (corner 1 -1) (corner 1 1))))))

(define cube (make-gpu-buffer 'vertex (apply append (map face-vertices faces))))

;;; ---- pipeline ----

(define frame-ubuf
  (make-gpu-buffer 'uniform (gpu-std140 (list (list 'mat4 (mat4-identity))
                                              (list 'mat4 (mat4-identity))
                                              '(vec3 (0 0 1))))))

(define bindings (make-gpu-bindings (list (list 'uniform 0 '(vertex fragment) frame-ubuf))))

(define pipeline
  (make-gpu-pipeline 'vertex vs 'fragment fs 'bindings bindings
                     ;; 36-byte stride: pos @0, normal @12, colour @24
                     'inputs '((36 (0 float3 0) (1 float3 12) (2 float3 24)))
                     'depth-test #t 'depth-write #t))

;;; ---- state and input ----

(define yaw 0.6)
(define pitch 0.4)
(define distance 6.0)
(define auto-spin #t)
(define drag-from #f)

(define canvas (make-gpu-canvas 'samples 4))

(gpu-canvas-on-render! canvas
  (lambda (frame w h)
    (let* ((proj  (mat4* (gpu-clip-space-matrix frame)
                         (mat4-perspective (deg->rad 45) (/ w (max h 1)) 0.1 100.0)))
           (view  (mat4-look-at (list 0.0 0.0 distance) '(0.0 0.0 0.0) '(0.0 1.0 0.0)))
           (model (mat4* (mat4-rotate-x pitch) (mat4-rotate-y yaw))))
      (gpu-buffer-update! frame-ubuf
        (gpu-std140 (list (list 'mat4 (mat4* proj (mat4* view model)))
                          (list 'mat4 model)
                          '(vec3 (0.5 0.8 1.0)))))
      (gpu-render-pass! frame
        (lambda (pass) (gpu-draw! pass pipeline 36 'vertices cube))
        'clear '(0.08 0.08 0.1 1.0)))))

(gpu-canvas-on-mouse! canvas
  (lambda (event button x y mods)
    (case event
      ((press) (set! drag-from (cons x y)) (set! auto-spin #f))
      ((release) (set! drag-from #f))
      ((drag)
       (when drag-from
         (set! yaw (+ yaw (* 0.01 (- x (car drag-from)))))
         (set! pitch (+ pitch (* 0.01 (- y (cdr drag-from)))))
         (set! drag-from (cons x y))
         (gpu-canvas-redraw! canvas))))))

(gpu-canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (set! distance (min 30.0 (max 2.5 (- distance (* 0.01 dy)))))
    (gpu-canvas-redraw! canvas)))

(gpu-canvas-on-key! canvas
  (lambda (key mods)
    (when (string=? key "Escape") (quit-event-loop))))

;;; ---- window and animation ----

(define win (make-plain-window "Curry gpu cube" 800 600))
(window-set-central-widget! win canvas)
(window-on-close! win quit-event-loop)

;; --snapshot PATH: render a few frames, save one, exit.
(define snapshot
  (let loop ((args (command-line)))
    (cond ((or (null? args) (null? (cdr args))) #f)
          ((string=? (car args) "--snapshot") (cadr args))
          (else (loop (cdr args))))))

(define ticks 0)
(define timer
  (make-timer/dt 16
    (lambda (dt)
      (set! ticks (+ ticks 1))
      (when auto-spin (set! yaw (+ yaw (* 0.0008 dt))))
      (gpu-canvas-redraw! canvas)
      (when (and snapshot (= ticks 20))
        (display (if (gpu-canvas-save-png! canvas snapshot) "saved " "could not save "))
        (display snapshot) (newline)
        (quit-event-loop)))))

(window-show! win)
(timer/dt-start! timer)
(run-event-loop)
