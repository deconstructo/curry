# Module: `(curry qt6)` — `gpu-*` layer (QRhi)

GPU rendering and compute from Scheme through **QRhi**, Qt's Rendering
Hardware Interface. QRhi uses the native graphics API of each platform:

| Platform | Backend used by default |
|----------|-------------------------|
| macOS    | Metal |
| Linux    | Vulkan, falling back to OpenGL |
| Windows  | D3D12 / D3D11 |

The `gpu-*` layer is separate from the older `gl-*` layer described in
[`module-qt6.md`](module-qt6.md). Both are in `(curry qt6)` and they do not
interact. Use `gpu-*` for new work: it is not tied to OpenGL, it has compute
shaders on every backend that supports them (including Metal), and it has
real 3D state (depth test, culling, blending, instancing, index buffers).

Matrix helpers are in a separate pure-Scheme library,
[`(curry gpu-math)`](#curry-gpu-math).

- [Requirements](#requirements)
- [Concepts](#concepts)
- [Quickstart: compute without a window](#quickstart-compute-without-a-window)
- [Quickstart: a window](#quickstart-a-window)
- [Devices and canvases](#devices-and-canvases)
- [Shaders](#shaders)
- [Buffers](#buffers)
- [Textures and samplers](#textures-and-samplers)
- [Bindings](#bindings)
- [Pipelines](#pipelines)
- [Frames, passes and commands](#frames-passes-and-commands)
- [Readback](#readback)
- [Uniform and storage block packing](#uniform-and-storage-block-packing)
- [Queries](#queries)
- [Coordinate conventions](#coordinate-conventions)
- [Resource lifetime](#resource-lifetime)
- [What is checked, and what is not](#what-is-checked-and-what-is-not)
- [`(curry gpu-math)`](#curry-gpu-math)
- [Limits of this version](#limits-of-this-version)

## Requirements

- Qt **6.7 or newer** (for `QRhiWidget`) and the **Qt Shader Tools** package
  (for compiling GLSL at runtime).
  - macOS: `brew install qt` (includes `qtshadertools`)
  - Debian/Ubuntu: `sudo apt install qt6-base-dev qt6-base-private-dev qt6-shadertools-dev`
- Build with `-DBUILD_MODULE_QT6=ON`. The `gpu-*` layer is built
  automatically when both are found (`BUILD_QT6_GPU`, default `ON`). If one
  is missing, CMake prints a warning and the rest of `(curry qt6)` still
  builds.

QRhi is public Qt API, but Qt gives it "limited compatibility guarantees":
it can change between Qt minor versions. Its headers come through Qt's
`GuiPrivate` and `ShaderToolsPrivate` targets, which is why CMake prints a
"private module" warning. This is expected.

## Concepts

The model is the same as Vulkan, Metal and WebGPU, at a smaller scale:

- **Resources** — buffers, textures, samplers — hold data.
- **Shaders** are GLSL source compiled once.
- **Bindings** say which resource goes to which `binding = N` slot in the
  shaders.
- A **pipeline** combines shaders, bindings, the vertex layout, and fixed
  state (depth test, culling, blending, topology).
- Work happens in a **frame**. Inside a frame, a **render pass** draws into
  one target and a **compute pass** runs compute shaders. Draws and
  dispatches are commands inside a pass.

Resources can be created at any time, before any GPU device exists. Each one
creates its native GPU object the first time it is used inside a frame.

## Quickstart: compute without a window

```scheme
(import (curry qt6) (curry f64vector) (srfi 1))   ; srfi 1 for iota

(define dev (make-gpu-device))            ; Metal / Vulkan / ... headless

(define cs (make-gpu-shader 'compute "#version 440
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Data { float v[]; };
void main() { uint i = gl_GlobalInvocationID.x; v[i] = v[i] * v[i]; }"))

(define data (make-gpu-buffer 'storage (list->vector (iota 128 1.0))))
(define pipe (make-gpu-compute-pipeline cs
               (make-gpu-bindings (list (list 'storage 0 'compute data)))))

(gpu-run! dev
  (lambda (frame)
    (gpu-compute-pass! frame
      (lambda (pass) (gpu-dispatch! pass pipe 2)))))   ; 2 groups x 64 = 128

(gpu-bytes->f32 (gpu-read-buffer dev data))           ; => #f64(1.0 4.0 9.0 ...)
```

## Quickstart: a window

Abbreviated from [`examples/gpu-cube.scm`](../../examples/gpu-cube.scm):

```scheme
(import (curry qt6) (curry gpu-math))

(define vs (make-gpu-shader 'vertex "#version 440
layout(location = 0) in vec3 pos;
layout(std140, binding = 0) uniform Frame { mat4 mvp; };
void main() { gl_Position = mvp * vec4(pos, 1.0); }"))
(define fs (make-gpu-shader 'fragment "#version 440
layout(location = 0) out vec4 frag;
void main() { frag = vec4(1.0, 0.5, 0.2, 1.0); }"))

(define tri  (make-gpu-buffer 'vertex '(-1 -1 0   1 -1 0   0 1 0)))
(define ubuf (make-gpu-buffer 'uniform (gpu-std140 (list (list 'mat4 (mat4-identity))))))
(define pipe (make-gpu-pipeline 'vertex vs 'fragment fs
               'bindings (make-gpu-bindings (list (list 'uniform 0 'vertex ubuf)))
               'inputs '((12 (0 float3 0)))))

(define canvas (make-gpu-canvas 'samples 4))
(gpu-canvas-on-render! canvas
  (lambda (frame w h)
    (gpu-buffer-update! ubuf
      (gpu-std140 (list (list 'mat4 (mat4* (gpu-clip-space-matrix frame)
                                           (mat4-scale 0.5 0.5 0.5))))))
    (gpu-render-pass! frame
      (lambda (pass) (gpu-draw! pass pipe 3 'vertices tri))
      'clear '(0.1 0.1 0.1 1.0))))

(define win (make-plain-window "triangle" 640 480))
(window-set-central-widget! win canvas)
(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)
```

## Devices and canvases

```scheme
(make-gpu-device)              ; headless device, platform default backend
(make-gpu-device backend)      ; 'metal 'vulkan 'opengl 'd3d11 'd3d12 'null
(gpu-device-destroy! dev)      ; native resources made on it are dropped

(make-gpu-canvas option ...)   ; → a qt6 widget (use with window-set-central-widget!,
                               ;   layout-add!, box-add! like any widget)
    ;; 'backend  sym      same names as make-gpu-device; default: platform default
    ;; 'samples  n        MSAA sample count: 1 (default), 2, 4 or 8
    ;; 'format   sym      colour buffer: 'rgba8 (default) 'rgba16f 'rgba32f 'rgb10a2
    ;; 'debug    #t       enable the backend's validation layer

(gpu-canvas-on-render! canvas (lambda (frame w h) ...))   ; w h: pixels (HiDPI-correct)
(gpu-canvas-redraw!    canvas)                            ; schedule a render
(gpu-canvas-on-key!    canvas (lambda (key mods) ...))
(gpu-canvas-on-key-up! canvas (lambda (key mods) ...))
(gpu-canvas-on-mouse!  canvas (lambda (event button x y mods) ...))
    ;; event: 'press 'release 'move 'drag 'double-press
(gpu-canvas-on-scroll! canvas (lambda (dx dy x y mods) ...))
(gpu-canvas-on-resize! canvas (lambda (w h) ...))         ; logical pixels
(gpu-canvas-save-png!  canvas path)                       ; renders a frame and saves it
```

The canvas renders only when asked. For animation, call
`gpu-canvas-redraw!` from a timer (`make-timer/dt`), or from inside the
render procedure for a loop paced by the display.

If a render procedure does not run a render pass on the canvas, the canvas
is cleared to black, because its contents would otherwise be undefined.

A canvas has no GPU device until its window is shown. Queries on a canvas
before that return `#f` or raise; queries on the **frame** inside the render
procedure always work.

## Shaders

```scheme
(make-gpu-shader stage source)   ; stage: 'vertex 'fragment 'compute
```

Source is **Vulkan-style GLSL** (`#version 440` or `450`): every input and
output has `layout(location = N)`, every uniform block, texture and storage
buffer has `layout(binding = N)`, and uniforms live in blocks, not as loose
`uniform float x;` declarations. The shader is compiled immediately, to
SPIR-V, Metal Shading Language, HLSL and GLSL. A compile error raises with
the compiler's message.

Use `gl_VertexIndex` / `gl_InstanceIndex` (Vulkan names), not
`gl_VertexID` / `gl_InstanceID`.

## Buffers

```scheme
(make-gpu-buffer usage data)       ; usage: 'vertex 'index 'uniform 'storage,
                                   ;        or a list such as '(vertex storage)
(make-gpu-buffer usage size)       ; exact integer: zero-filled bytes
(gpu-buffer-update! buf data)      ; replace from byte 0
(gpu-buffer-update! buf data off)  ; replace starting at byte offset off
(gpu-buffer-size buf)              ; → bytes
```

`data` is one of:

- a **bytevector** — used as raw bytes;
- numbers in a **list, vector or f64vector**, nested to any depth — packed
  as 32-bit floats, or as 32-bit unsigned integers for a buffer whose only
  usage is `'index`.

A buffer's size is fixed when it is created; an update that does not fit
raises. A uniform buffer can't have any other usage, and its size is
rounded up to a multiple of 16 bytes. Index buffers are always 32-bit.

Updates take effect at the next pass that uses the buffer. Within one frame,
a uniform buffer holds a single value: if you update it between two passes
of the same frame, both passes may see the second value. Use one uniform
buffer per object for per-object data.

## Textures and samplers

```scheme
(make-gpu-texture w h option ...)
    ;; 'format         'rgba8 (default) 'r8 'rg8 'r16f 'rgba16f 'r32f 'rgba32f
    ;;                 'r32ui 'r32si 'rgba32ui 'rgba32si
    ;; 'depth d        make a 3D texture w x h x d
    ;; 'data  data     initial contents (see below)
    ;; 'render-target #t   can be the target of a render pass
    ;; 'storage       #t   can be bound as an image in shaders (imageLoad/imageStore)
    ;; 'mipmaps       #t   allocate and generate mipmaps after each upload (2D only)
(gpu-texture-update! tex data)
(gpu-texture-size tex)          ; → (w h) or (w h d)

(make-gpu-sampler option ...)
    ;; 'filter  'linear (default) or 'nearest
    ;; 'wrap    'clamp (default), 'repeat or 'mirror
    ;; 'mipmaps #t   sample between mipmap levels
```

Texture `data` must cover the whole texture exactly. A bytevector is raw
texel bytes, row by row, starting at the top row (then slice by slice for
3D). Numbers are converted per format: 8-bit formats take numbers in
[0, 1], `16f`/`32f` formats take floats, `ui`/`si` formats take integers.

## Bindings

```scheme
(make-gpu-bindings entries)
```

Each entry is a list:

| Entry | GLSL |
|-------|------|
| `(uniform N stages buffer)` | `layout(std140, binding = N) uniform Block { ... };` |
| `(texture N stages texture sampler)` | `layout(binding = N) uniform sampler2D t;` (or `sampler3D`) |
| `(storage-read N stages buffer)` | `layout(std430, binding = N) readonly buffer B { ... };` |
| `(storage-write N stages buffer)` | `... writeonly buffer ...` |
| `(storage N stages buffer)` | `... buffer ...` (read and write) |
| `(image-read N stages texture [level])` | `layout(rgba32f, binding = N) readonly uniform image2D i;` |
| `(image-write N stages texture [level])` | `... writeonly ...` |
| `(image N stages texture [level])` | read and write |

`stages` is `'vertex`, `'fragment`, `'compute`, or a list of them. Storage
entries need a buffer made with usage `'storage`; image entries need a
texture made with `'storage #t`.

## Pipelines

```scheme
(make-gpu-pipeline 'vertex vs 'fragment fs option ...)
    ;; 'bindings b        the binding layout (default: no resources)
    ;; 'inputs   layout   vertex buffer layout (default: none — vertices made in the shader)
    ;; 'topology          'triangles (default) 'triangle-strip 'triangle-fan
    ;;                    'lines 'line-strip 'points
    ;; 'depth-test  bool  default #t
    ;; 'depth-write bool  default #t
    ;; 'depth-op          default 'less-or-equal; also 'less 'greater 'equal
    ;;                    'not-equal 'greater-or-equal 'always 'never
    ;; 'cull              'none (default) 'back 'front
    ;; 'front-face        'ccw (default) 'cw
    ;; 'blend             'none (default) 'alpha 'additive
    ;; 'polygon           'fill (default) 'line   (needs gpu-feature? 'line-polygon-mode)
    ;; 'line-width  w     default 1.0           (needs gpu-feature? 'wide-lines)

(make-gpu-compute-pipeline cs)
(make-gpu-compute-pipeline cs bindings)
```

The `'inputs` layout is a list of vertex buffer slots, in the order the
buffers are passed to `gpu-draw!`:

```scheme
'inputs '((stride-bytes (location format offset-bytes) ...)          ; per vertex
          (stride-bytes per-instance (location format offset-bytes) ...))
;; format: 'float 'float2 'float3 'float4 'uint 'uint2 'uint3 'uint4
;;         'sint 'sint2 'sint3 'sint4 'unorm-byte4
```

## Frames, passes and commands

```scheme
(gpu-run! device (lambda (frame) ...))     ; one headless frame; waits for the GPU;
                                           ; returns the procedure's value

(gpu-render-pass! frame (lambda (pass) ...) option ...)
    ;; 'target  texture        render into a 'render-target texture
    ;;                         (default: the canvas; required on a device)
    ;; 'clear   (r g b a)      clear colour, components 0..1 (default opaque black)
    ;; 'depth-clear d          default 1.0

(gpu-compute-pass! frame (lambda (pass) ...))

(gpu-draw! pass pipeline count option ...)
    ;; 'vertices  buffer | (buffer ...)   one per 'inputs slot
    ;; 'index     buffer                  indexed draw: count is the index count
    ;; 'bindings  b                       default: the pipeline's bindings
    ;; 'instances n   'first n   'first-instance n   'vertex-offset n
    ;; 'viewport (x y w h)   'scissor (x y w h)      in pixels

(gpu-dispatch! pass compute-pipeline x [y [z]] ['bindings b])  ; work-group counts
```

`'bindings` on a draw or dispatch can swap in different resources with the
same layout as the pipeline's (for example the same shader drawing several
objects, each with its own uniform buffer).

Frames and passes only exist inside their procedures. Keeping a frame or
pass and using it later raises an error instead of crashing. Passes cannot
be nested. Draws and dispatches are recorded while the pass procedure runs
and sent to the GPU when it returns. So if the procedure raises an error,
nothing from that pass reaches the GPU, and the error propagates normally.

## Readback

```scheme
;; outside any frame, on a device — waits and returns a bytevector
(gpu-read-buffer  device buffer)
(gpu-read-texture device texture)           ; 2D textures
(gpu-save-texture-png device texture path)  ; 2D 'rgba8 textures

;; inside a frame, outside any pass — proc receives the bytevector later
(gpu-read-buffer!  frame buffer  (lambda (bytes) ...))
(gpu-read-texture! frame texture (lambda (bytes) ...))
```

On a device frame the procedure runs right after `gpu-run!`'s frame ends.
On a canvas frame it runs when the GPU has finished that frame, during a
later event-loop turn.

Texture readback returns rows as stored in the texture. With OpenGL, a
texture used as a render target has its first row at the **bottom** of the
image; with Metal, Vulkan and D3D, at the top. `gpu-framebuffer-y-up?`
tells you which; `gpu-save-texture-png` corrects for it.

## Uniform and storage block packing

```scheme
(gpu-std140 fields)   ; → bytevector, laid out like a std140 uniform block
(gpu-std430 fields)   ; → bytevector, laid out like a std430 storage block
    ;; field: (type value) or (array type (value ...))
    ;; type:  float int uint vec2 vec3 vec4 ivec2..4 uvec2..4 mat2 mat3 mat4
```

The packing follows GLSL's rules, including the ones that are easy to get
wrong: a `vec3` is aligned to 16 bytes; in std140, array elements and
matrix columns are padded to 16 bytes. Matrices are column-major number
sequences. For example:

```glsl
layout(std140, binding = 0) uniform Frame { mat4 mvp; vec3 light; float time; };
```

```scheme
(gpu-buffer-update! ubuf
  (gpu-std140 (list (list 'mat4 mvp) '(vec3 (0.5 0.8 1.0)) (list 'float t))))
```

Byte helpers: `(gpu-f32-bytes data)`, `(gpu-u32-bytes data)`,
`(gpu-i32-bytes data)` pack numbers; `(gpu-bytes->f32 bv)` returns an
f64vector, and `(gpu-bytes->u32 bv)` / `(gpu-bytes->i32 bv)` return vectors.

## Queries

These accept a device, a canvas, or a frame:

```scheme
(gpu-backend x)              ; → 'metal 'vulkan 'opengl 'd3d11 'd3d12 'null (#f: canvas not shown yet)
(gpu-device-name x)          ; → string, e.g. "Apple M1 Max"
(gpu-feature? x feature)     ; → boolean
(gpu-features)               ; → list of feature names gpu-feature? knows
(gpu-limit x limit)          ; → integer: 'texture-size-max 'max-color-attachments
                             ;   'max-thread-groups-per-dimension 'max-threads-per-thread-group
                             ;   'max-thread-group-x/-y/-z 'max-uniform-buffer-range
                             ;   'max-vertex-inputs 'max-vertex-outputs
(gpu-clip-space-matrix x)    ; → 16-element column-major correction matrix
(gpu-framebuffer-y-up? x)    ; → #t for OpenGL
```

Feature names: `compute`, `instancing`, `base-instance`, `base-vertex`,
`3d-textures`, `texture-arrays`, `geometry-shader`, `tessellation`,
`multiview`, `variable-rate-shading`, `draw-indirect`, `wide-lines`,
`line-polygon-mode`, `int-attributes`, `read-back-any-format`,
`depth-clamp`, `timestamps`.

## Coordinate conventions

The backends disagree on clip space: OpenGL uses depth −1..1, the others
0..1, and Vulkan's y axis points down. Write projection matrices in the
OpenGL convention (`mat4-perspective` does) and multiply by the backend's
correction matrix:

```scheme
(mat4* (gpu-clip-space-matrix frame) (mat4-perspective fovy aspect near far))
```

Then the same shaders and the same Scheme code give the same picture on
every backend.

## Resource lifetime

Resources are not garbage-collected: the GPU objects stay until you call
`(gpu-release! resource)` or the device they live on is destroyed. Create
resources once and update them; do not create new ones every frame.
`gpu-release!` cannot be called while a frame is being recorded. Using a
released resource raises.

A resource can be used with more than one device or canvas, but it lives
on one at a time and is re-created when used on another. Re-creation
uploads the CPU-side copy of the data, so results a compute shader wrote
on the old device are not carried over.

## What is checked, and what is not

Metal, and some drivers for other APIs, do not report a mismatch as an
error. They abort the whole process, or read memory past the end of a
buffer. So the `gpu-*` procedures check everything they can before any
command reaches the GPU, and raise an ordinary Scheme error instead:

- **Against the shaders.** A pipeline reads Qt's reflection data for its
  shaders. Every uniform block, sampled texture, storage block and storage
  image a shader declares must be in the bindings at that binding number,
  with the right kind, with that shader's stage listed, and big enough
  (uniform and storage buffers are compared with the block size; textures
  must be 2D or 3D as the shader expects; storage images must have the
  format the shader declares). Every vertex shader input needs an
  attribute in `'inputs`. Bindings passed to `gpu-draw!`/`gpu-dispatch!`
  must have the pipeline's layout and are checked the same way.
- **Vertex layouts.** At most 8 vertex buffer slots and 16 attributes;
  attribute locations are unique; every attribute fits inside its stride.
- **Draw ranges.** Every vertex and instance a draw can fetch must be
  inside its buffer. For an indexed draw, the largest index in the index
  buffer is used. Because of this, an index buffer cannot also be a
  storage buffer: its CPU copy must always be the real contents.
- **Viewport and scissor.** Values must be finite and within ±65536; the
  scissor rectangle is clipped to the target, and a draw whose scissor is
  entirely outside is skipped.
- **Compute.** Group counts and the shader's `local_size` are compared
  with the device's limits.
- **Shader source.** At most 256 KB. The compiler runs on a thread with a
  large stack, so a very long expression gives a compile error and does
  not crash the process.
- **Data.** Circular lists, self-containing vectors, and data with more
  than 2^28 elements raise.
- **Escapes.** A `call/cc` escape out of a frame or pass procedure ends
  that frame or pass properly; its handles stop working.

**Not checked: what shader code does with memory.** Inside a shader,
`s[i]` with `i` past the end of a storage buffer is not bounds-checked on
Metal or OpenGL. It can read or overwrite other GPU data of the same
process. Treat shader source like native code: run only shaders you trust.
A shader that loops for a very long time can also make the whole display
stutter until the GPU driver stops it.

## `(curry gpu-math)`

Pure Scheme. Vectors are lists `(x y z)` / `(x y z w)`; matrices are
16-element column-major vectors.

```scheme
(import (curry gpu-math))

(v3+ a b) (v3- a b) (v3* a s) (v3-dot a b) (v3-cross a b) (v3-length a) (v3-normalize a)

(mat4-identity)  (mat4-ref m row col)  (mat4* a b)  (mat4*v4 m v)
(mat4-transpose m)  (mat4-inverse m)                 ; inverse raises if singular
(mat4-translate x y z)  (mat4-scale x y z)
(mat4-rotate angle axis)  (mat4-rotate-x a)  (mat4-rotate-y a)  (mat4-rotate-z a)
(mat4-perspective fovy aspect near far)              ; fovy in radians
(mat4-orthographic left right bottom top near far)
(mat4-look-at eye center up)
(deg->rad degrees)
```

## Limits of this version

- No geometry or tessellation shaders, texture arrays, cube maps, indirect
  draws or multiview yet, even where the backend supports them.
- Index buffers are 32-bit only, and cannot be written by shaders.
- 3D textures cannot be read back directly; copy them to a storage buffer
  with a compute shader.
- One uniform buffer value per frame (see [Buffers](#buffers)).
- Metal has no 64-bit floats in shaders; GPU work is 32-bit float.
- Mouse capture (first-person mode) is not implemented for gpu canvases yet.
