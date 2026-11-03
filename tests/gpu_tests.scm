;;; gpu_tests.scm — (curry qt6) gpu-* (QRhi) layer tests
;;;
;;; Runs on a headless gpu device (Metal on macOS, Vulkan or OpenGL on
;;; Linux) and checks real GPU results: compute output, rendered pixels
;;; (clear, triangles, depth test, indexed and instanced draws, uniforms,
;;; texture sampling), storage images, 3D textures, readback, and that
;;; misuse raises a catchable error and leaves the device usable.
;;;
;;; With no usable GPU device the suite prints SKIP and exits 0, unless
;;; CURRY_GPU_REQUIRED is set, in which case that is a failure.

(import (scheme base) (scheme cxr) (scheme write) (scheme process-context)
        (curry qt6) (curry f64vector) (curry gpu-math) (curry sync))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got (guard (e (#t (list 'raised (if (error-object? e)
                                                 (error-object-message e)
                                                 e))))
                  expr)))
       (if (equal? got expected)
           (set! pass (+ pass 1))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

(define (raises? thunk)
  (guard (e (#t #t)) (thunk) #f))

;; Message of the raised error, or #f when nothing was raised.
(define (raise-message thunk)
  (guard (e (#t (if (error-object? e) (error-object-message e) "non-error raise")))
    (thunk) #f))

(define (string-contains? s sub)
  (let ((n (string-length s)) (m (string-length sub)))
    (let loop ((i 0))
      (cond ((> (+ i m) n) #f)
            ((string=? (substring s i (+ i m)) sub) #t)
            (else (loop (+ i 1)))))))

(define dev
  (guard (e (#t #f)) (make-gpu-device)))

(unless dev
  (display "gpu tests: SKIP (no GPU device could be created)") (newline)
  (exit (if (get-environment-variable "CURRY_GPU_REQUIRED") 1 0)))

(display "gpu tests: backend ") (display (gpu-backend dev))
(display ", device ") (display (gpu-device-name dev)) (newline)

;;; ---- helpers -------------------------------------------------------------

(define W 64)
(define H 64)

(define (pixel bv x y)
  (let ((i (* 4 (+ x (* y W)))))
    (list (bytevector-u8-ref bv i) (bytevector-u8-ref bv (+ i 1))
          (bytevector-u8-ref bv (+ i 2)) (bytevector-u8-ref bv (+ i 3)))))

(define (make-target) (make-gpu-texture W H 'render-target #t))

(define red   '(255 0 0 255))
(define green '(0 255 0 255))
(define blue  '(0 0 255 255))
(define black '(0 0 0 255))

;; position (vec3) + colour (vec4), interleaved, 28-byte stride
(define vs-colour (make-gpu-shader 'vertex "#version 440
layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 col;
layout(location = 0) out vec4 v_col;
void main() { v_col = col; gl_Position = vec4(pos, 1.0); }"))

(define fs-colour (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 frag;
void main() { frag = v_col; }"))

(define colour-inputs '((28 (0 float3 0) (1 float4 12))))

(define (vertex pos col) (append pos col))
(define (rgba c) (map (lambda (x) (/ x 255.0)) c))

;; A full-target quad as 4 vertices at depth z (for index buffer 0 1 2 2 1 3).
(define (quad z c)
  (append (vertex (list -1.0 -1.0 z) (rgba c)) (vertex (list 1.0 -1.0 z) (rgba c))
          (vertex (list -1.0  1.0 z) (rgba c)) (vertex (list 1.0  1.0 z) (rgba c))))

(define quad-indices (make-gpu-buffer 'index '(0 1 2 2 1 3)))

;;; ---- queries -------------------------------------------------------------

(check "gpu-backend is a symbol" (symbol? (gpu-backend dev)) #t)
(check "gpu-features lists compute" (and (memq 'compute (gpu-features)) #t) #t)
(check "gpu-feature? returns a boolean" (boolean? (gpu-feature? dev 'instancing)) #t)
(check "gpu-feature? unknown feature raises" (raises? (lambda () (gpu-feature? dev 'telepathy))) #t)
(check "gpu-clip-space-matrix is 16 floats"
       (let ((m (gpu-clip-space-matrix dev))) (and (vector? m) (= (vector-length m) 16))) #t)
(check "gpu-limit texture-size-max >= 2048" (>= (gpu-limit dev 'texture-size-max) 2048) #t)

;;; ---- byte packing ----------------------------------------------------------

(check "f32 bytes round trip"
       (f64vector->list (gpu-bytes->f32 (gpu-f32-bytes '(1.5 -2.0 #(3.0 4.25)))))
       '(1.5 -2.0 3.0 4.25))
(check "u32 bytes round trip" (gpu-bytes->u32 (gpu-u32-bytes '(0 7 4294967295))) #(0 7 4294967295))
(check "i32 bytes round trip" (gpu-bytes->i32 (gpu-i32-bytes '(-1 5))) #(-1 5))
(check "u32 out of range raises" (raises? (lambda () (gpu-u32-bytes '(-1)))) #t)
(check "f32 of exact rationals" (f64vector->list (gpu-bytes->f32 (gpu-f32-bytes (list 1/2)))) '(0.5))

;;; ---- compute ---------------------------------------------------------------

(define cs-scale (make-gpu-shader 'compute "#version 440
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Data { float v[]; };
layout(std140, binding = 1) uniform U { float scale; };
void main() { uint i = gl_GlobalInvocationID.x; v[i] = v[i] * scale + float(i); }"))

(define sbuf (make-gpu-buffer 'storage (make-vector 128 1.0)))
(define ubuf (make-gpu-buffer 'uniform '(3.0)))
(define cbind (make-gpu-bindings (list (list 'storage 0 'compute sbuf)
                                       (list 'uniform 1 'compute ubuf))))
(define cpipe (make-gpu-compute-pipeline cs-scale cbind))

(gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p cpipe 2)))))
(define out1 (gpu-bytes->f32 (gpu-read-buffer dev sbuf)))
(check "compute v[0]"   (f64vector-ref out1 0) 3.0)
(check "compute v[1]"   (f64vector-ref out1 1) 4.0)
(check "compute v[127]" (f64vector-ref out1 127) 130.0)

;; Second dispatch works on the GPU-side contents (no re-upload), with an
;; updated uniform.
(gpu-buffer-update! ubuf '(0.5))
(gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p cpipe 2)))))
(define out2 (gpu-bytes->f32 (gpu-read-buffer dev sbuf)))
(check "compute second pass v[1]"   (f64vector-ref out2 1) 3.0)     ; 4*0.5+1
(check "compute second pass v[127]" (f64vector-ref out2 127) 192.0) ; 130*0.5+127

;; Partial CPU update with a byte offset, then read back.
(gpu-buffer-update! sbuf '(-1.0 -2.0) 8)
(define out3 (gpu-bytes->f32 (gpu-read-buffer dev sbuf)))
(check "partial update writes at byte offset"
       (list (f64vector-ref out3 1) (f64vector-ref out3 2) (f64vector-ref out3 3) (f64vector-ref out3 4))
       ;; elements 2 and 3 replaced; 1 and 4 keep the CPU copy (upload is
       ;; of the dirty range only, but the readback reflects the GPU copy,
       ;; which still holds the second pass's values outside the range)
       (list 3.0 -1.0 -2.0 (+ (* 0.5 (+ 4 3)) 4)))

;; Async readback inside a device frame: delivered after the frame ends.
(define async-result #f)
(gpu-run! dev (lambda (f) (gpu-read-buffer! f ubuf (lambda (bv) (set! async-result bv)))))
(check "async readback inside gpu-run!" (f64vector-ref (gpu-bytes->f32 async-result) 0) 0.5)
(check "gpu-run! returns the procedure's value" (gpu-run! dev (lambda (f) 42)) 42)

;;; ---- rendering -------------------------------------------------------------

(define colour-pipe
  (make-gpu-pipeline 'vertex vs-colour 'fragment fs-colour 'inputs colour-inputs))

;; clear only
(define t1 (make-target))
(gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) #t) 'target t1 'clear '(0 0 1 1))))
(define img1 (gpu-read-texture dev t1))
(check "clear colour fills target" (list (pixel img1 0 0) (pixel img1 63 63)) (list blue blue))

;; one triangle around the centre
(define tri (make-gpu-buffer 'vertex
  (append (vertex '(-0.5 -0.5 0.5) (rgba red)) (vertex '(0.5 -0.5 0.5) (rgba red))
          (vertex '( 0.0  0.5 0.5) (rgba red)))))
(define t2 (make-target))
(gpu-run! dev (lambda (f)
  (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices tri)) 'target t2)))
(define img2 (gpu-read-texture dev t2))
(check "triangle covers the centre" (pixel img2 32 32) red)
(check "triangle leaves the corner clear" (pixel img2 1 1) black)

;; depth test: near green drawn first, far red second -> green wins
(define near-green (make-gpu-buffer 'vertex (quad 0.2 green)))
(define far-red    (make-gpu-buffer 'vertex (quad 0.6 red)))
(define (draw-near-then-far pipe target)
  (gpu-run! dev (lambda (f)
    (gpu-render-pass! f (lambda (p)
      (gpu-draw! p pipe 6 'vertices near-green 'index quad-indices)
      (gpu-draw! p pipe 6 'vertices far-red    'index quad-indices))
      'target target)))
  (pixel (gpu-read-texture dev target) 32 32))
(check "depth test keeps the nearer surface" (draw-near-then-far colour-pipe (make-target)) green)
(define no-depth-pipe
  (make-gpu-pipeline 'vertex vs-colour 'fragment fs-colour 'inputs colour-inputs 'depth-test #f))
(check "without depth test the last draw wins" (draw-near-then-far no-depth-pipe (make-target)) red)

;; instanced: one small quad, two instances offset left/right by a
;; per-instance vec2
(define vs-inst (make-gpu-shader 'vertex "#version 440
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 offset;
void main() { gl_Position = vec4(pos + offset, 0.5, 1.0); }"))
(define fs-white (make-gpu-shader 'fragment "#version 440
layout(location = 0) out vec4 frag;
void main() { frag = vec4(1.0); }"))
(define inst-pipe
  (make-gpu-pipeline 'vertex vs-inst 'fragment fs-white
                     'inputs '((8 (0 float2 0)) (8 per-instance (1 float2 0)))))
(define small-quad (make-gpu-buffer 'vertex '(-0.2 -0.2  0.2 -0.2  -0.2 0.2  0.2 0.2)))
(define offsets (make-gpu-buffer 'vertex '(-0.5 0.0  0.5 0.0)))
(define t3 (make-target))
(gpu-run! dev (lambda (f)
  (gpu-render-pass! f (lambda (p)
    (gpu-draw! p inst-pipe 6 'vertices (list small-quad offsets) 'index quad-indices 'instances 2))
    'target t3)))
(define img3 (gpu-read-texture dev t3))
(check "instance 0 drawn on the left"  (pixel img3 16 32) '(255 255 255 255))
(check "instance 1 drawn on the right" (pixel img3 48 32) '(255 255 255 255))
(check "nothing drawn between instances" (pixel img3 32 32) black)

;; uniform buffer colour, updated between frames
(define vs-full (make-gpu-shader 'vertex "#version 440
layout(location = 0) out vec2 uv;
void main() {
  vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  uv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.5, 1.0);
}"))
(define fs-uniform (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform U { vec4 colour; };
void main() { frag = colour; }"))
(define colour-ubuf (make-gpu-buffer 'uniform '(1.0 0.0 0.0 1.0)))
(define ubind (make-gpu-bindings (list (list 'uniform 0 'fragment colour-ubuf))))
(define upipe (make-gpu-pipeline 'vertex vs-full 'fragment fs-uniform 'bindings ubind))
(define (draw-uniform target)
  (gpu-run! dev (lambda (f)
    (gpu-render-pass! f (lambda (p) (gpu-draw! p upipe 3)) 'target target)))
  (pixel (gpu-read-texture dev target) 32 32))
(define t4 (make-target))
(check "uniform colour" (draw-uniform t4) red)
(gpu-buffer-update! colour-ubuf '(0.0 0.0 1.0 1.0))
(check "updated uniform colour" (draw-uniform t4) blue)

;; texture sampling: 2x2 texture, nearest filter -> four quadrant colours
(define fs-tex (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag;
layout(binding = 0) uniform sampler2D tex;
void main() { frag = texture(tex, uv); }"))
(define tex2 (make-gpu-texture 2 2 'data
  (bytevector 255 0 0 255   0 255 0 255
              0 0 255 255   255 255 255 255)))
(define smp (make-gpu-sampler 'filter 'nearest))
(define tbind (make-gpu-bindings (list (list 'texture 0 'fragment tex2 smp))))
(define tpipe (make-gpu-pipeline 'vertex vs-full 'fragment fs-tex 'bindings tbind))
(define t5 (make-target))
(gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) (gpu-draw! p tpipe 3)) 'target t5)))
(define img5 (gpu-read-texture dev t5))
(define quadrants (list (pixel img5 16 16) (pixel img5 48 16) (pixel img5 16 48) (pixel img5 48 48)))
(check "all four texels appear (orientation-independent)"
       (and (member red quadrants) (member green quadrants) (member blue quadrants)
            (member '(255 255 255 255) quadrants) #t)
       #t)

;; one render pass into one target, then sample it in a second pass
(define t6 (make-target))
(define bind6 (make-gpu-bindings (list (list 'texture 0 'fragment t2 smp))))
(define pipe6 (make-gpu-pipeline 'vertex vs-full 'fragment fs-tex 'bindings bind6))
(gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) (gpu-draw! p pipe6 3)) 'target t6)))
(check "render-to-texture result sampled in a later pass" (pixel (gpu-read-texture dev t6) 32 32) red)

;;; ---- storage images and 3D textures -----------------------------------------

(when (gpu-feature? dev 'compute)
  (define img (make-gpu-texture 8 8 'format 'rgba32f 'storage #t))
  (define cs-img (make-gpu-shader 'compute "#version 440
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform writeonly image2D img;
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  imageStore(img, p, vec4(p.x, p.y, p.x * 10 + p.y, 1.0));
}"))
  (define ip (make-gpu-compute-pipeline cs-img (make-gpu-bindings (list (list 'image-write 0 'compute img)))))
  (gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p ip 1 1)))))
  (define px (gpu-bytes->f32 (gpu-read-texture dev img)))
  ;; texel (3, 5): index 4 * (3 + 5*8)
  (check "storage image texel (3,5)"
         (list (f64vector-ref px 172) (f64vector-ref px 173) (f64vector-ref px 174))
         '(3.0 5.0 35.0)))

(when (and (gpu-feature? dev 'compute) (gpu-feature? dev '3d-textures))
  (define vol-data (let loop ((i 63) (acc '())) (if (< i 0) acc (loop (- i 1) (cons (exact->inexact i) acc)))))
  (define vol (make-gpu-texture 4 4 'depth 4 'format 'r32f 'data vol-data))
  (define vout (make-gpu-buffer 'storage 256))
  (define cs-vol (make-gpu-shader 'compute "#version 440
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(binding = 0) uniform sampler3D vol;
layout(std430, binding = 1) buffer Out { float o[]; };
void main() {
  ivec3 p = ivec3(gl_GlobalInvocationID);
  o[p.x + p.y * 4 + p.z * 16] = texelFetch(vol, p, 0).r;
}"))
  (define vp (make-gpu-compute-pipeline cs-vol
               (make-gpu-bindings (list (list 'texture 0 'compute vol (make-gpu-sampler 'filter 'nearest))
                                        (list 'storage 1 'compute vout)))))
  (gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p vp 1 1 1)))))
  (define vr (gpu-bytes->f32 (gpu-read-buffer dev vout)))
  (check "3D texture texelFetch (1,2,3)" (f64vector-ref vr (+ 1 (* 2 4) (* 3 16))) 57.0)
  (check "3D texture size" (gpu-texture-size vol) '(4 4 4)))

;;; ---- std140 / std430 packing ------------------------------------------------

(check "std140 float then vec3: vec3 aligned to 16"
       (bytevector-length (gpu-std140 '((float 1.0) (vec3 (1 2 3))))) 32)
(check "std140 vec3 then float: float fills the vec3's tail"
       (f64vector->list (gpu-bytes->f32 (gpu-std140 '((vec3 (1 2 3)) (float 4)))))
       '(1.0 2.0 3.0 4.0))
(check "std140 float array has a 16-byte stride" (bytevector-length (gpu-std140 '((array float (1 2))))) 32)
(check "std430 float array is tightly packed" (bytevector-length (gpu-std430 '((array float (1 2))))) 8)
(check "std140 mat3 columns padded to vec4" (bytevector-length (gpu-std140 '((mat3 (1 2 3 4 5 6 7 8 9))))) 48)
(check "std140 int field stores an integer"
       (gpu-bytes->i32 (gpu-std140 '((int -7)))) #(-7 0 0 0))
(check "std140 wrong value count raises" (raises? (lambda () (gpu-std140 '((vec3 (1 2)))))) #t)
(check "std140 unknown type raises" (raises? (lambda () (gpu-std140 '((quat (1 2 3 4)))))) #t)

;; The GPU agrees with the packing: a compute shader copies a std140
;; uniform block's fields into a std430 storage buffer.
(define cs-layout (make-gpu-shader 'compute "#version 440
layout(local_size_x = 1) in;
layout(std140, binding = 0) uniform U { float a; vec3 b; mat4 m; float arr[2]; int k; };
layout(std430, binding = 1) buffer Out { float o[]; };
void main() {
  o[0] = a; o[1] = b.x; o[2] = b.y; o[3] = b.z;
  o[4] = m[0][1]; o[5] = m[3][0]; o[6] = arr[0]; o[7] = arr[1]; o[8] = float(k);
}"))
(define layout-ubuf
  (make-gpu-buffer 'uniform
    (gpu-std140 (list '(float 1.5) '(vec3 (2 3 4)) (list 'mat4 (mat4-translate 9.0 0.0 0.0))
                      '(array float (5 6)) '(int 7)))))
(define layout-out (make-gpu-buffer 'storage 64))
(define layout-pipe (make-gpu-compute-pipeline cs-layout
  (make-gpu-bindings (list (list 'uniform 0 'compute layout-ubuf) (list 'storage 1 'compute layout-out)))))
(gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p layout-pipe 1)))))
(check "GPU reads gpu-std140's layout"
       (let ((o (gpu-bytes->f32 (gpu-read-buffer dev layout-out))))
         (map (lambda (i) (f64vector-ref o i)) '(0 1 2 3 4 5 6 7 8)))
       '(1.5 2.0 3.0 4.0 0.0 9.0 5.0 6.0 7.0))

;;; ---- (curry gpu-math) ----------------------------------------------------------

(define (approx-list=? a b) (let loop ((a a) (b b))
  (or (and (null? a) (null? b))
      (and (pair? a) (pair? b) (< (abs (- (car a) (car b))) 1e-9) (loop (cdr a) (cdr b))))))

(check "identity times vector"
       (mat4*v4 (mat4-identity) '(1.0 2.0 3.0 1.0)) '(1.0 2.0 3.0 1.0))
(check "translate moves a point"
       (mat4*v4 (mat4-translate 1.0 2.0 3.0) '(1.0 1.0 1.0 1.0)) '(2.0 3.0 4.0 1.0))
(check "rotate-z by 90 degrees maps x to y"
       (approx-list=? (mat4*v4 (mat4-rotate-z (deg->rad 90)) '(1.0 0.0 0.0 1.0)) '(0.0 1.0 0.0 1.0)) #t)
(check "inverse undoes a transform"
       (let* ((m (mat4* (mat4-translate 1.0 -2.0 3.0) (mat4* (mat4-rotate-y 0.7) (mat4-scale 2.0 2.0 2.0))))
              (p '(0.3 0.4 0.5 1.0)))
         (approx-list=? (mat4*v4 (mat4-inverse m) (mat4*v4 m p)) p))
       #t)
(check "look-at puts the target on the -z axis"
       (let ((v (mat4*v4 (mat4-look-at '(0.0 0.0 5.0) '(0.0 0.0 0.0) '(0.0 1.0 0.0)) '(0.0 0.0 0.0 1.0))))
         (approx-list=? v '(0.0 0.0 -5.0 1.0)))
       #t)
(check "perspective maps the near plane to depth -1"
       (let* ((p (mat4*v4 (mat4-perspective 1.0 1.0 0.5 10.0) '(0.0 0.0 -0.5 1.0))))
         (< (abs (+ 1.0 (/ (caddr p) (list-ref p 3)))) 1e-9))
       #t)
(check "singular inverse raises" (raises? (lambda () (mat4-inverse (mat4-scale 0.0 1.0 1.0)))) #t)
(check "v3-cross" (v3-cross '(1 0 0) '(0 1 0)) '(0 0 1))

;;; ---- errors ------------------------------------------------------------------

(check "shader compile error is raised with the compiler message"
       (let ((m (raise-message (lambda () (make-gpu-shader 'fragment "#version 440\nvoid main() { oops; }")))))
         (and m (string-contains? m "failed to compile")))
       #t)
(check "unknown shader stage raises" (raises? (lambda () (make-gpu-shader 'geometry "x"))) #t)
(check "vertex shader in fragment slot raises"
       (raises? (lambda () (make-gpu-pipeline 'vertex vs-colour 'fragment vs-colour))) #t)
(check "unknown pipeline option raises"
       (raises? (lambda () (make-gpu-pipeline 'vertex vs-colour 'fragment fs-colour 'sparkle #t))) #t)
(check "odd option list raises" (raises? (lambda () (make-gpu-sampler 'filter))) #t)
(check "buffer update past the end raises" (raises? (lambda () (gpu-buffer-update! ubuf '(1.0) 16))) #t)
(check "texture data size mismatch raises"
       (raises? (lambda () (make-gpu-texture 2 2 'data (bytevector 1 2 3)))) #t)
(check "uniform binding with a storage buffer raises"
       (raises? (lambda () (make-gpu-bindings (list (list 'uniform 0 'compute sbuf))))) #t)
(check "duplicate binding number raises"
       (raises? (lambda () (make-gpu-bindings (list (list 'uniform 0 'compute ubuf)
                                                    (list 'storage 0 'compute sbuf))))) #t)
(check "device render pass without a target raises"
       (raises? (lambda () (gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) #t)))))) #t)
(check "nested passes raise"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-compute-pass! f (lambda (q) #t))) 'target t1))))) #t)
(check "dispatch in a render pass raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-dispatch! p cpipe 1)) 'target t1))))) #t)
(check "wrong number of vertex buffers raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3)) 'target t1))))) #t)
(check "index range past the end raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 7 'vertices tri 'index quad-indices))
                           'target t1))))) #t)
(define kept-frame #f)
(gpu-run! dev (lambda (f) (set! kept-frame f)))
(check "frame used after its frame ended raises"
       (raises? (lambda () (gpu-render-pass! kept-frame (lambda (p) #t) 'target t1))) #t)
(define kept-pass #f)
(gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) (set! kept-pass p)) 'target t1)))
(check "pass used after it ended raises"
       (raises? (lambda () (gpu-run! dev (lambda (f) (gpu-draw! kept-pass colour-pipe 3 'vertices tri))))) #t)
(check "wrong handle type raises" (raises? (lambda () (gpu-buffer-size t1))) #t)
(check "forged handle raises" (raises? (lambda () (gpu-buffer-size (cons 'gpu-buffer 42)))) #t)

;; An error inside a pass procedure propagates, and the device still works.
(check "error inside a pass procedure propagates"
       (raise-message (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (error "boom")) 'target t1)))))
       "boom")
(check "device still renders after an error in a pass" (draw-uniform (make-target)) blue)

;; Releasing a resource: later use raises; release during a frame raises.
(define doomed (make-gpu-buffer 'vertex '(1.0 2.0)))
(check "release during a frame raises"
       (raises? (lambda () (gpu-run! dev (lambda (f) (gpu-release! doomed))))) #t)
(gpu-release! doomed)
(check "use after release raises" (raises? (lambda () (gpu-buffer-update! doomed '(0.0)))) #t)

;;; ---- input that used to crash the process, hang, or lose data ----------------
;;; Each of these was found by an independent review; before the fix each one
;;; aborted the process (Metal validation), segfaulted, or hung.

(define (raises-with? thunk text)
  (let ((m (raise-message thunk))) (and m (string-contains? m text) #t)))

(define circular (let ((l (list 1.0 2.0))) (set-cdr! (cdr l) l) l))
(check "circular list as data raises instead of hanging"
       (raises-with? (lambda () (make-gpu-buffer 'vertex circular)) "circular") #t)
(check "circular list in gpu-f32-bytes raises" (raises? (lambda () (gpu-f32-bytes circular))) #t)
(define self-vec (make-vector 1000 0.0))
(let loop ((i 0)) (when (< i 1000) (vector-set! self-vec i self-vec) (loop (+ i 1))))
(check "self-referencing vector raises instead of exploding" (raises? (lambda () (gpu-f32-bytes self-vec))) #t)

;; very long expression: compiles (on the big-stack thread) or raises, no crash
(define long-expr
  (let loop ((k 20000) (acc "")) (if (= k 0) acc (loop (- k 1) (string-append acc "s[1]+")))))
(check "20000-term shader expression does not crash the compiler"
       (guard (e (#t 'raised))
         (make-gpu-shader 'compute (string-append "#version 440
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer S { float s[]; };
void main() { s[0] = " long-expr "1.0; }")) 'compiled)
       'compiled)
(check "oversized shader source raises"
       (raises-with? (lambda () (make-gpu-shader 'compute (make-string 300000 #\space))) "limit") #t)

;; vertex layouts
(define vs-pos (make-gpu-shader 'vertex "#version 440
layout(location = 0) in vec3 pos;
void main() { gl_Position = vec4(pos, 1.0); }"))
(check "attribute past the stride raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-pos 'fragment fs-white 'inputs '((4 (0 float3 0))))) "stride") #t)
(check "duplicate attribute location raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-pos 'fragment fs-white
                                                   'inputs '((16 (0 float3 0) (0 float 12))))) "twice") #t)
(check "too many vertex buffer slots raises"
       (raises? (lambda () (make-gpu-pipeline 'vertex vs-pos 'fragment fs-white
                                              'inputs (cons '(12 (0 float3 0)) (make-list 9 '(16)))))) #t)
(check "shader input with no attribute raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-pos 'fragment fs-white)) "location 0") #t)

;; bindings checked against what the shaders declare
(define small-ubuf (make-gpu-buffer 'uniform 16))
(define fs-big-block (make-gpu-shader 'fragment "#version 440
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform U { vec4 c[8]; };
void main() { frag = c[7]; }"))
(check "uniform buffer smaller than its block raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-full 'fragment fs-big-block
                                  'bindings (make-gpu-bindings (list (list 'uniform 0 'fragment small-ubuf))))) "bytes") #t)
(check "missing binding raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-full 'fragment fs-uniform)) "nothing there") #t)
(check "texture bound where a uniform block is declared raises"
       (raises? (lambda () (make-gpu-pipeline 'vertex vs-full 'fragment fs-uniform
                             'bindings (make-gpu-bindings (list (list 'texture 0 'fragment tex2 smp)))))) #t)
(check "binding without the shader's stage raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-full 'fragment fs-uniform
                                  'bindings (make-gpu-bindings (list (list 'uniform 0 'vertex colour-ubuf))))) "stages") #t)
(define fs-3d (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag;
layout(binding = 0) uniform sampler3D vol;
void main() { frag = texture(vol, vec3(uv, 0.5)); }"))
(check "2D texture where the shader samples 3D raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-full 'fragment fs-3d 'bindings tbind)) "3D") #t)
(check "swapped-in bindings of a different kind raise"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p upipe 3 'bindings tbind)) 'target t1))))) #t)
(when (gpu-feature? dev 'compute)
  (define rgba8-img (make-gpu-texture 8 8 'format 'rgba8 'storage #t))
  (define cs-f32-img (make-gpu-shader 'compute "#version 440
layout(local_size_x = 1) in;
layout(rgba32f, binding = 0) uniform writeonly image2D img;
void main() { imageStore(img, ivec2(0), vec4(1.0)); }"))
  (check "storage image format mismatch raises"
         (raises-with? (lambda () (make-gpu-compute-pipeline cs-f32-img
                                    (make-gpu-bindings (list (list 'image-write 0 'compute rgba8-img))))) "format") #t)
  (check "image mip level that does not exist raises"
         (raises-with? (lambda () (make-gpu-bindings (list (list 'image-write 0 'compute rgba8-img 3)))) "mip level") #t)
  ;; Raised by the shader compiler or by gpu-dispatch!, whichever knows the
  ;; limit first -- either way an error, not a Metal abort.
  (check "work group larger than the device allows raises"
         (raises? (lambda ()
           (let* ((cs (make-gpu-shader 'compute "#version 440
layout(local_size_x = 1024, local_size_y = 2) in;
layout(std430, binding = 0) buffer S { float s[]; };
void main() { s[0] = 1.0; }"))
                  (pl (make-gpu-compute-pipeline cs (make-gpu-bindings (list (list 'storage 0 'compute sbuf))))))
             (gpu-run! dev (lambda (f) (gpu-compute-pass! f (lambda (p) (gpu-dispatch! p pl 1))))))))
         #t))

;; draw ranges
(check "non-indexed draw past the end of the vertex buffer raises"
       (raises-with? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 300000 'vertices tri)) 'target t1)))) "bytes") #t)
(check "draw starting past the end raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices tri 'first 2000000000)) 'target t1))))) #t)
(define wild-indices (make-gpu-buffer 'index '(0 1 1000000)))
(check "index pointing past the vertex buffer raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices tri 'index wild-indices)) 'target t1))))) #t)
(check "instance past the end of a per-instance buffer raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p)
           (gpu-draw! p inst-pipe 6 'vertices (list small-quad offsets) 'index quad-indices 'instances 3)) 'target t1))))) #t)
(check "index buffer that is also a storage buffer raises"
       (raises? (lambda () (make-gpu-buffer '(index storage) '(0 1 2)))) #t)

;; viewport and scissor
(check "NaN scissor raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices tri 'scissor '(+nan.0 0 10 10))) 'target t1))))) #t)
(check "huge viewport raises"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices tri 'viewport '(0 0 1e30 10))) 'target t1))))) #t)
(define t8 (make-target))
(gpu-run! dev (lambda (f)
  (gpu-render-pass! f (lambda (p)
    (gpu-draw! p colour-pipe 3 'vertices tri 'scissor '(-100 -100 50000 50000))   ; clipped to the target
    (gpu-draw! p colour-pipe 3 'vertices tri 'scissor '(500 500 10 10)))          ; outside: skipped
    'target t8)))
(check "scissor larger than the target is clipped" (pixel (gpu-read-texture dev t8) 32 32) red)

;; call/cc escapes out of a pass and out of a frame
(define escaped-frame
  (call/cc (lambda (k) (gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) (k f)) 'target t1))))))
(check "escaped frame handle raises" (raises? (lambda () (gpu-render-pass! escaped-frame (lambda (p) #t) 'target t1))) #t)
(check "device works after a call/cc escape" (gpu-run! dev (lambda (f) 'fine)) 'fine)
(check "rendering works after a call/cc escape" (draw-uniform (make-target)) blue)

;; a failed pass does not lose uploads queued before the failure
(define fresh-tri (make-gpu-buffer 'vertex
  (append (vertex '(-0.5 -0.5 0.5) (rgba green)) (vertex '(0.5 -0.5 0.5) (rgba green))
          (vertex '( 0.0  0.5 0.5) (rgba green)))))
(unless (gpu-feature? dev 'wide-lines)
  (define wide-pipe (make-gpu-pipeline 'vertex vs-colour 'fragment fs-colour 'inputs colour-inputs
                                       'topology 'lines 'line-width 3.0))
  (check "pass using an unsupported feature raises"
         (raises? (lambda () (gpu-run! dev (lambda (f)
           (gpu-render-pass! f (lambda (p)
             (gpu-draw! p colour-pipe 3 'vertices fresh-tri)
             (gpu-draw! p wide-pipe 2 'vertices fresh-tri)) 'target t1))))) #t)
  (define t9 (make-target))
  (gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) (gpu-draw! p colour-pipe 3 'vertices fresh-tri)) 'target t9)))
  (check "upload queued before the failure arrives on the retry" (pixel (gpu-read-texture dev t9) 32 32) green))

;; one value per uniform buffer per frame
(check "updating a uniform buffer after it was used in the same frame raises"
       (raises-with? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p) (gpu-draw! p upipe 3)) 'target t1)
         (gpu-buffer-update! colour-ubuf '(1.0 1.0 1.0 1.0))))) "second buffer") #t)
(gpu-buffer-update! colour-ubuf '(0.0 0.0 1.0 1.0))

;;; ---- review findings on PR #347 ------------------------------------------------

;; An index buffer changed after gpu-draw! recorded the draw is checked again
;; when the pass executes.
(define late-indices (make-gpu-buffer 'index '(0 1 2)))
(check "index buffer updated after the draw was recorded is re-checked"
       (raises? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p)
           (gpu-draw! p colour-pipe 3 'vertices tri 'index late-indices)
           (gpu-buffer-update! late-indices '(100000000 100000001 100000002)))
           'target t1))))) #t)
(gpu-buffer-update! late-indices '(0 1 2))

;; one value per uniform buffer: also between two draws in one pass
(check "updating a uniform buffer between two draws of one pass raises"
       (raises-with? (lambda () (gpu-run! dev (lambda (f)
         (gpu-render-pass! f (lambda (p)
           (gpu-draw! p upipe 3)
           (gpu-buffer-update! colour-ubuf '(1.0 0.0 1.0 1.0))
           (gpu-draw! p upipe 3)) 'target t1)))) "second buffer") #t)
(gpu-buffer-update! colour-ubuf '(0.0 0.0 1.0 1.0))

(check "matrix vertex input raises"
       (raises-with? (lambda () (make-gpu-pipeline
         'vertex (make-gpu-shader 'vertex "#version 440
layout(location = 0) in mat4 m;
void main() { gl_Position = m[0]; }")
         'fragment fs-white 'inputs '((64 (0 float4 0))))) "matrix") #t)
(check "array of textures raises"
       (raises-with? (lambda () (make-gpu-pipeline 'vertex vs-full
         'fragment (make-gpu-shader 'fragment "#version 440
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag;
layout(binding = 0) uniform sampler2D t[2];
void main() { frag = texture(t[0], uv) + texture(t[1], uv); }")
         'bindings tbind)) "array") #t)
(check "3D texture over 2048 raises" (raises? (lambda () (make-gpu-texture 4096 4 'depth 4))) #t)
(check "mipmaps on an integer format raise" (raises? (lambda () (make-gpu-texture 8 8 'format 'r32ui 'mipmaps #t))) #t)
(check "depth-clear outside [0, 1] raises"
       (raises? (lambda () (gpu-run! dev (lambda (f) (gpu-render-pass! f (lambda (p) #t) 'target t1 'depth-clear 2.0))))) #t)
(check "numbers for a '(vertex index) buffer are u32 indices"
       (gpu-bytes->u32 (gpu-read-buffer dev (make-gpu-buffer '(vertex index) '(0 1 2)))) #(0 1 2))

;; gpu-* refuses to run off the main thread (an actor, or a parallel map)
(define actor-result #f)
(define actor-done (make-semaphore 0))
(spawn (lambda ()
         (set! actor-result (guard (e (#t 'raised)) (make-gpu-buffer 'vertex '(1.0)) 'ran))
         (sem-post! actor-done)))
(sem-wait! actor-done)
(check "gpu-* from an actor raises" actor-result 'raised)
(define packed-in-actor #f)
(define packed-done (make-semaphore 0))
(spawn (lambda ()
         (set! packed-in-actor (bytevector-length (gpu-f32-bytes '(1.0 2.0))))
         (sem-post! packed-done)))
(sem-wait! packed-done)
(check "byte packing still works from any thread" packed-in-actor 8)

;; Canvas callbacks survive garbage collection: the render procedure is an
;; inline closure, the heap is churned and collected, then a frame is
;; rendered off screen through gpu-canvas-save-png!.
(define gc-canvas (make-gpu-canvas))
(define gc-render-calls 0)
(let ((tag (make-string 64 #\x)))
  (gpu-canvas-on-render! gc-canvas
    (lambda (frame w h)
      (when (= (string-length tag) 64) (set! gc-render-calls (+ gc-render-calls 1)))
      (gpu-render-pass! frame (lambda (p) #t) 'clear '(0 1 0 1)))))
(let loop ((i 0))
  (when (< i 50) (make-vector 4000 (lambda (a) a)) (gc) (loop (+ i 1))))
(define gc-png (string-append (or (get-environment-variable "TMPDIR") "/tmp/") "/curry-gpu-gc-test.png"))
(when (gpu-canvas-save-png! gc-canvas gc-png)
  (check "canvas render closure survives GC" (> gc-render-calls 0) #t))

;;; ---- device lifetime -----------------------------------------------------------

;; Resources outlive a device: a second device re-creates them.
(define dev2 (make-gpu-device))
(gpu-buffer-update! colour-ubuf '(0.0 1.0 0.0 1.0))
(define t7 (make-target))
(gpu-run! dev2 (lambda (f) (gpu-render-pass! f (lambda (p) (gpu-draw! p upipe 3)) 'target t7)))
(check "resources move to a second device" (pixel (gpu-read-texture dev2 t7) 32 32) green)
(gpu-device-destroy! dev2)
(check "destroyed device raises" (raises? (lambda () (gpu-run! dev2 (lambda (f) #t)))) #t)
(check "resources come back to the first device" (draw-uniform (make-target)) green)

;;; ---- Summary -------------------------------------------------------------------

(newline)
(display "gpu tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
