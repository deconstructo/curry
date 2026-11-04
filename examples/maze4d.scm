#!/usr/bin/env curry
;;; maze4d.scm — Wander a 4D maze as a 3D creature (gpu-* layer).
;;;
;;; The maze has passages in ±x, ±z within each w-slice, plus ±w connections
;;; between slices.  At any moment you see a first-person view (GPU DDA
;;; raycaster with red/blue anaglyph stereo) of the 3D cross-section at your
;;; current w.  Press Q/E to step through w — the maze instantly resets
;;; around you.
;;;
;;; Goal: reach cell (DX-1, DZ-1) on w-slice (DW-1).  Yellow dot on minimap.
;;; On the minimap, a green square marks a passage to the next w-slice (E),
;;; an orange square a passage to the previous one (Q).
;;;
;;; This version runs on the gpu-* (QRhi) layer: Metal on macOS, Vulkan on
;;; Linux.  The whole picture, including the minimap and the w-indicator,
;;; is drawn by one fragment shader.  examples/maze4d-gl.scm is the same
;;; maze on the older gl-* layer, for Qt versions below 6.7.
;;;
;;; Controls:
;;;   W / ↑    move forward        Q   step −w
;;;   S / ↓    move back           E   step +w
;;;   A / ←    turn left           click  grab mouse (mouse look)
;;;   D / →    turn right          Esc    release mouse / quit

(import (curry qt6))

;;; ── Parameters ────────────────────────────────────────────────────────────

(define DX 8)  (define DZ 8)  (define DW 4)

(define WALL-H      1.0)
(define MOVE-SPEED   0.12)
(define TURN-SPEED   0.07)
(define MOUSE-SENS   0.003)  ; radians per pixel
(define *eye-sep*   0.10)   ; anaglyph inter-ocular distance (world units)
(define HALF-FOV    0.66)   ; camera-plane half-width (≈65° FOV)
(define FOG-DIST    7.0)
(define MAP-CELL    12)
(define MAP-PAD      8)

;;; ── Random (LCG) ──────────────────────────────────────────────────────────

(define *lcg* (inexact->exact (floor (current-second))))

(define (lcg-next!)
  (set! *lcg* (remainder (+ (* *lcg* 1664525) 1013904223) 4294967296))
  *lcg*)

(define (rand-int n) (abs (remainder (lcg-next!) n)))

;;; ── Maze ──────────────────────────────────────────────────────────────────

(define B+X  1) (define B-X  2)
(define B+Z  4) (define B-Z  8)
(define B+W 16) (define B-W 32)

(define *DIRS*
  (vector (vector B+X B-X  1  0  0)
          (vector B-X B+X -1  0  0)
          (vector B+Z B-Z  0  1  0)
          (vector B-Z B+Z  0 -1  0)
          (vector B+W B-W  0  0  1)
          (vector B-W B+W  0  0 -1)))

(define *cells* (make-vector (* DX DZ DW) 0))
(define *seen*  (make-vector (* DX DZ DW) #f))

(define (cidx ix iz iw) (+ (* iw DX DZ) (* iz DX) ix))

(define (in-bounds? ix iz iw)
  ; Note: 3-arg < triggers a Curry VM bug after repeated calls from recursive fns;
  ; split each into two 2-arg comparisons as a workaround.
  (and (< -1 ix) (< ix DX)
       (< -1 iz) (< iz DZ)
       (< -1 iw) (< iw DW)))

(define (open-bit! idx bit)
  (vector-set! *cells* idx (bitwise-or (vector-ref *cells* idx) bit)))

(define (passage? ix iz iw bit)
  (and (in-bounds? ix iz iw)
       (not (zero? (bitwise-and (vector-ref *cells* (cidx ix iz iw)) bit)))))

(define (shuffled-dirs)
  (let ((v (vector-copy *DIRS*)))
    (do ((i 5 (- i 1))) ((= i 0) v)
      (let* ((j (rand-int (+ i 1))) (t (vector-ref v i)))
        (vector-set! v i (vector-ref v j))
        (vector-set! v j t)))))

(define (generate-maze!)
  (let ((stack (list (vector 0 0 0))))
    (vector-set! *seen* 0 #t)
    (let loop ()
      (when (pair? stack)
        (let* ((top  (car stack))
               (ix   (vector-ref top 0))
               (iz   (vector-ref top 1))
               (iw   (vector-ref top 2))
               (dirs (shuffled-dirs))
               (next #f))
          (let find ((i 0))
            (when (and (< i 6) (not next))
              (let* ((d  (vector-ref dirs i))
                     (nx (+ ix (vector-ref d 2)))
                     (nz (+ iz (vector-ref d 3)))
                     (nw (+ iw (vector-ref d 4))))
                (if (and (in-bounds? nx nz nw)
                         (not (vector-ref *seen* (cidx nx nz nw))))
                    (set! next (vector d nx nz nw))
                    (find (+ i 1))))))
          (if next
              (let* ((d  (vector-ref next 0))
                     (nx (vector-ref next 1))
                     (nz (vector-ref next 2))
                     (nw (vector-ref next 3)))
                (open-bit! (cidx ix iz iw) (vector-ref d 0))
                (open-bit! (cidx nx nz nw) (vector-ref d 1))
                (vector-set! *seen* (cidx nx nz nw) #t)
                (set! stack (cons (vector nx nz nw) stack)))
              (set! stack (cdr stack)))
          (loop))))))

(generate-maze!)

;;; ── Player state ──────────────────────────────────────────────────────────

(define *px*  0.5)
(define *pz*  0.5)
(define *pw*  0)
(define *yaw* 0.0)

(define (player-ix) (inexact->exact (floor *px*)))
(define (player-iz) (inexact->exact (floor *pz*)))

;;; ── Movement ──────────────────────────────────────────────────────────────

(define (try-move! dx dz)
  (let* ((nx  (+ *px* dx))
         (nix (inexact->exact (floor nx)))
         (oix (player-ix)))
    (if (= nix oix)
        (set! *px* nx)
        (when (passage? oix (player-iz) *pw* (if (> nix oix) B+X B-X))
          (set! *px* nx))))
  (let* ((nz  (+ *pz* dz))
         (niz (inexact->exact (floor nz)))
         (oiz (player-iz)))
    (if (= niz oiz)
        (set! *pz* nz)
        (when (passage? (player-ix) oiz *pw* (if (> niz oiz) B+Z B-Z))
          (set! *pz* nz)))))

(define (try-step-w! dw)
  (let ((nw  (+ *pw* dw))
        (bit (if (> dw 0) B+W B-W)))
    (and (in-bounds? (player-ix) (player-iz) nw)
         (passage? (player-ix) (player-iz) *pw* bit)
         (begin (set! *pw* nw) #t))))

;;; ── GPU raycaster ─────────────────────────────────────────────────────────
;;;
;;; Maze cell data is a DX × (DZ×DW) r8 texture: the byte at column ix,
;;; row (iz + iw*DZ) is *cells*[cidx(ix,iz,iw)], read with texelFetch.
;;;
;;; The fragment shader casts two DDA rays per pixel (left/right eye for the
;;; anaglyph) and composes ceiling, floor, wall stripes and the HUD in a
;;; single pass.  All lengths in the shader are logical pixels with y
;;; pointing down, like the QPainter coordinates the gl-* version used.

(define *frame-block* "
layout(std140, binding = 0) uniform Frame {
    mat4  clip;       /* backend clip-space correction                */
    vec2  res;        /* physical pixels                              */
    vec2  pos;        /* (px, pz) player position                     */
    float dpr;        /* device pixel ratio                           */
    float yaw;
    float half_fov;   /* camera-plane half-width (0.66)               */
    float wall_h;     /* wall height in world units (1.0)             */
    float fog;        /* fog falloff distance                         */
    float eye_sep;    /* anaglyph inter-ocular separation             */
    int   pw;         /* current w-slice                              */
    int   y_up;       /* 1 when the framebuffer's origin is bottom-left */
    int   DX;
    int   DZ;
    int   DW;
    int   map_cell;   /* minimap cell size, logical pixels            */
    int   map_pad;
};
")

(define *raycaster-vert* (string-append "#version 440
" *frame-block* "
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = clip * vec4(p * 2.0 - 1.0, 0.5, 1.0);
}"))

(define *raycaster-frag* (string-append "#version 440
layout(location = 0) out vec4 frag_color;
" *frame-block* "
layout(binding = 1) uniform sampler2D maze;   /* DX x (DZ*DW), r8 */

int cell_bits_w(int ix, int iz, int iw) {
    if (ix < 0 || ix >= DX || iz < 0 || iz >= DZ || iw < 0 || iw >= DW) return 0;
    return int(texelFetch(maze, ivec2(ix, iz + iw * DZ), 0).r * 255.0 + 0.5);
}
int cell_bits(int ix, int iz) { return cell_bits_w(ix, iz, pw); }

float cast_ray(vec2 p, vec2 rd, out bool x_side) {
    float ddx = abs(rd.x) < 1e-10 ? 1e30 : abs(1.0 / rd.x);
    float ddz = abs(rd.y) < 1e-10 ? 1e30 : abs(1.0 / rd.y);
    int ix = int(floor(p.x));
    int iz = int(floor(p.y));
    int sx = rd.x > 0.0 ? 1 : -1;
    int sz = rd.y > 0.0 ? 1 : -1;
    float sdx = rd.x > 0.0 ? (float(ix + 1) - p.x) * ddx : (p.x - float(ix)) * ddx;
    float sdz = rd.y > 0.0 ? (float(iz + 1) - p.y) * ddz : (p.y - float(iz)) * ddz;
    for (int i = 0; i < 64; i++) {
        if (min(sdx, sdz) > 18.0) { x_side = false; return -1.0; }
        if (sdx < sdz) {
            int bit = sx > 0 ? 1 : 2;
            /* the wall is on the boundary about to be crossed, at sdx
             * (sdx - ddx would be the boundary already behind us) */
            if ((cell_bits(ix, iz) & bit) == 0) { x_side = true;  return max(0.01, sdx); }
            ix += sx; sdx += ddx;
        } else {
            int bit = sz > 0 ? 4 : 8;
            if ((cell_bits(ix, iz) & bit) == 0) { x_side = false; return max(0.01, sdz); }
            iz += sz; sdz += ddz;
        }
    }
    x_side = false; return -1.0;
}

/* Brick/masonry pattern on walls: mortar gaps 0.25, brick faces 0.80-0.95.
 * Mortar lines compress with distance -- the strongest depth cue. */
float brick(float tx, float ty) {
    float row = floor(ty * 4.0);
    float fy  = fract(ty * 4.0);
    float fx  = fract(tx * 2.0 + mod(row, 2.0) * 0.5);
    if (fy < 0.07 || fx < 0.06) return 0.25;
    float col = floor(tx * 2.0 + mod(row, 2.0) * 0.5);
    return 0.80 + fract(sin(row * 7.31 + col * 13.17) * 43758.55) * 0.15;
}

/* 1 inside a disc of radius r around c, with a one-pixel soft edge. */
float disc(vec2 p, vec2 c, float r) { return 1.0 - smoothstep(r - 0.5, r + 0.5, length(p - c)); }

/* 1 on a one-pixel-wide line segment from a to b. */
float segment(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return 1.0 - smoothstep(0.5, 1.0, length(p - (a + t * ab)));
}

/* Minimap, top right: background, walls, w-passages, goal, player. */
vec3 minimap(vec2 p, vec3 col, float sw) {
    float cell = float(map_cell);
    vec2 o = vec2(sw - float(DX) * cell - float(map_pad), float(map_pad));
    vec2 m = p - o;
    if (m.x < -1.0 || m.y < -1.0 || m.x > float(DX) * cell + 1.0 || m.y > float(DZ) * cell + 1.0) return col;
    col = mix(col, vec3(0.0, 0.0, 0.15), 0.75);
    int ix = int(floor(m.x / cell)), iz = int(floor(m.y / cell));
    int bits = cell_bits(clamp(ix, 0, DX - 1), clamp(iz, 0, DZ - 1));
    if (ix >= 0 && ix < DX && iz >= 0 && iz < DZ) {
        vec2 c = (vec2(ix, iz) + 0.5) * cell;
        float sq = step(abs(m.x - c.x), cell * 0.18) * step(abs(m.y - c.y), cell * 0.18);
        if ((bits & 16) != 0) col = mix(col, vec3(0.2, 0.8, 0.3), 0.6 * sq);
        else if ((bits & 32) != 0) col = mix(col, vec3(0.9, 0.5, 0.15), 0.6 * sq);
    }
    /* walls: a vertical line at x = k*cell unless the passage is open */
    int kx = int(floor(m.x / cell + 0.5));
    if (abs(m.x - float(kx) * cell) < 0.75 && iz >= 0 && iz < DZ && kx >= 0 && kx <= DX)
        if (kx == 0 || kx == DX || (cell_bits(kx - 1, iz) & 1) == 0) col = vec3(0.4, 0.5, 1.0);
    int kz = int(floor(m.y / cell + 0.5));
    if (abs(m.y - float(kz) * cell) < 0.75 && ix >= 0 && ix < DX && kz >= 0 && kz <= DZ)
        if (kz == 0 || kz == DZ || (cell_bits(ix, kz - 1) & 4) == 0) col = vec3(0.4, 0.5, 1.0);
    if (pw == DW - 1)
        col = mix(col, vec3(1.0, 0.9, 0.1), disc(m, (vec2(DX, DZ) - 0.5) * cell, 4.5));
    vec2 pc = pos * cell;
    col = mix(col, vec3(1.0, 0.6, 0.6), segment(m, pc, pc + 9.0 * vec2(sin(yaw), cos(yaw))));
    col = mix(col, vec3(1.0, 0.3, 0.3), disc(m, pc, 4.5));
    return col;
}

/* One dot per w-slice, top left; the current one bright green. */
vec3 w_indicator(vec2 p, vec3 col) {
    for (int i = 0; i < DW; i++) {
        vec2 c = vec2(16.0 + float(i) * 18.0, 14.0);
        vec3 dot_col = i == pw ? vec3(0.2, 1.0, 0.5) : vec3(0.1, 0.3, 0.2);
        col = mix(col, dot_col, disc(p, c, 6.0) * (i == pw ? 1.0 : 0.9));
    }
    return col;
}

void main() {
    float sw  = res.x / dpr;
    float sh  = res.y / dpr;
    float lx  = gl_FragCoord.x / dpr;
    float ly  = y_up != 0 ? sh - gl_FragCoord.y / dpr : gl_FragCoord.y / dpr;   /* 0 = top */
    float hsh = sh * 0.5;

    float cam = 2.0 * lx / sw - 1.0;
    float sy  = sin(yaw);
    float cy  = cos(yaw);
    vec2  rd  = vec2(sy + cy * cam * half_fov, cy - sy * cam * half_fov);
    float hs  = eye_sep * 0.5;
    vec2 l_pos = pos + vec2(-cy,  sy) * hs;
    vec2 r_pos = pos + vec2( cy, -sy) * hs;

    /* ceiling and floor */
    vec3 bg;
    if (ly < hsh) {
        bg = vec3(0.04, 0.03, 0.06);
    } else {
        /* floor grid: eye at height 0.5, so a screen row at ly looks at the
         * floor 0.5 / (ly/hsh - 1) away (infinite at the horizon) */
        float row_dist = 0.5 / max(0.001, ly / hsh - 1.0);
        /* row_dist is a perpendicular distance and rd's component along
         * the view direction is 1, so the floor point is pos + row_dist*rd
         * (normalizing rd here bent the grid lines like a fish-eye lens) */
        float fx  = fract(pos.x + row_dist * rd.x);
        float fz  = fract(pos.y + row_dist * rd.y);
        bool  gline = fx < 0.05 || fz < 0.05;
        float fog_f = max(0.0, 1.0 - row_dist / fog);
        float extra = gline ? 0.0 : fog_f * 0.07;
        bg = vec3(0.09 + extra, 0.08 + extra, 0.06 + extra);
    }

    /* walls: left eye in red, right eye in blue */
    bool lxs, rxs;
    float lp = cast_ray(l_pos, rd, lxs);
    float rp = cast_ray(r_pos, rd, rxs);
    float ra = 0.0, ba = 0.0;
    if (lp > 0.0) {
        float h = wall_h * hsh / lp;
        if (ly >= hsh - h && ly <= hsh + h) {
            float tx = lxs ? fract(l_pos.y + lp * rd.y) : fract(l_pos.x + lp * rd.x);
            float ty = (ly - (hsh - h)) / (2.0 * h);
            ra = max(0.0, 1.0 - lp / fog) * (lxs ? 1.0 : 0.7) * brick(tx, ty);
        }
    }
    if (rp > 0.0) {
        float h = wall_h * hsh / rp;
        if (ly >= hsh - h && ly <= hsh + h) {
            float tx = rxs ? fract(r_pos.y + rp * rd.y) : fract(r_pos.x + rp * rd.x);
            float ty = (ly - (hsh - h)) / (2.0 * h);
            ba = max(0.0, 1.0 - rp / fog) * (rxs ? 1.0 : 0.7) * brick(tx, ty);
        }
    }
    vec3 col = clamp(vec3(bg.r + ra, bg.g, bg.b + ba), 0.0, 1.0);

    vec2 p = vec2(lx, ly);
    col = minimap(p, col, sw);
    col = w_indicator(p, col);
    frag_color = vec4(col, 1.0);
}"))

; Pack *cells* into a bytevector laid out as DX × (DZ*DW) rows.
; Row (iz + iw*DZ), column ix → byte index = iw*DX*DZ + iz*DX + ix = cidx(ix,iz,iw).
(define (make-maze-bvec)
  (let* ((n  (* DX DZ DW))
         (bv (make-bytevector n 0)))
    (do ((i 0 (+ i 1))) ((= i n))
      (bytevector-u8-set! bv i (vector-ref *cells* i)))
    bv))

(define *maze-tex* (make-gpu-texture DX (* DZ DW) 'format 'r8 'data (make-maze-bvec)))
(define *frame-ubuf* (make-gpu-buffer 'uniform 192))
(define *pipeline*
  (make-gpu-pipeline
    'vertex   (make-gpu-shader 'vertex *raycaster-vert*)
    'fragment (make-gpu-shader 'fragment *raycaster-frag*)
    'bindings (make-gpu-bindings
                (list (list 'uniform 0 '(vertex fragment) *frame-ubuf*)
                      (list 'texture 1 'fragment *maze-tex* (make-gpu-sampler 'filter 'nearest))))
    'depth-test #f))

;;; ── Win check ─────────────────────────────────────────────────────────────

(define (at-goal?)
  (and (= (player-ix) (- DX 1))
       (= (player-iz) (- DZ 1))
       (= *pw* (- DW 1))))

;;; ── Window ────────────────────────────────────────────────────────────────

(define canvas (make-gpu-canvas))
(define *logical-w* 1.0)   ; set by the resize callback; gives the pixel ratio

(gpu-canvas-on-resize! canvas (lambda (w h) (set! *logical-w* (max w 1))))

(gpu-canvas-on-render! canvas
  (lambda (frame w h)
    (gpu-buffer-update! *frame-ubuf*
      (gpu-std140
        (list (list 'mat4  (gpu-clip-space-matrix frame))
              (list 'vec2  (list w h))
              (list 'vec2  (list *px* *pz*))
              (list 'float (/ w *logical-w*))
              (list 'float *yaw*)
              (list 'float HALF-FOV)
              (list 'float WALL-H)
              (list 'float FOG-DIST)
              (list 'float *eye-sep*)
              (list 'int   *pw*)
              (list 'int   (if (gpu-framebuffer-y-up? frame) 1 0))
              (list 'int   DX) (list 'int DZ) (list 'int DW)
              (list 'int   MAP-CELL) (list 'int MAP-PAD))))
    (gpu-render-pass! frame (lambda (pass) (gpu-draw! pass *pipeline* 3)))))

(define win (make-plain-window "4D Maze" 1080 640))
(define status (window-status-bar win))

(define sidebar (make-vbox))
(widget-set-min-size! sidebar 210 0)
(widget-set-max-size! sidebar 210 100000)
(layout-add! sidebar (make-label "Stereo (red/blue)"))
(layout-add! sidebar
  (make-slider "Eye separation" 0 50 1 10
    (lambda (v) (set! *eye-sep* (* v 0.01)))))
(layout-add! sidebar (make-separator))
(layout-add! sidebar (make-label "Controls"))
(layout-add! sidebar (make-label "W/S/↑/↓  move"))
(layout-add! sidebar (make-label "Click     grab mouse"))
(layout-add! sidebar (make-label "Mouse     look"))
(layout-add! sidebar (make-label "A/D/←/→  turn (ungrabbed)"))
(layout-add! sidebar (make-label "Q/E      step ±w"))
(layout-add! sidebar (make-label "Esc      release / quit"))
(layout-add! sidebar (make-separator))
(layout-add! sidebar (make-label "Minimap: green = exit to +w (E), orange = exit to −w (Q)"))

(define central (make-hbox))
(layout-add! central sidebar)
(layout-add! central canvas)
(window-set-central-widget! win central)

(define (update-status!)
  (statusbar-set-text! status
    (if (at-goal?)
        "YOU ESCAPED THE 4D MAZE"
        (string-append "w-slice " (number->string (+ *pw* 1)) " of " (number->string DW)
                       "   —   goal: far corner of slice " (number->string DW)))))

;;; ── Input ─────────────────────────────────────────────────────────────────

(define *held* (make-hash-table equal?))  ; currently held keys

(define (key-held? k) (hash-table-ref *held* k #f))

(gpu-canvas-on-key! canvas
  (lambda (key mods)
    (hash-table-set! *held* key #t)
    (cond
      ((equal? key "q") (try-step-w! -1) (update-status!))
      ((equal? key "e") (try-step-w!  1) (update-status!))
      ((equal? key "Escape")
       (if (gpu-canvas-mouse-grabbed? canvas)
           (gpu-canvas-release-mouse! canvas)
           (quit-event-loop)))
      ((equal? key "Q") (quit-event-loop)))))

(gpu-canvas-on-key-up! canvas
  (lambda (key mods) (hash-table-delete! *held* key)))

(define (tick-movement!)
  (let* ((sy (sin *yaw*)) (cy (cos *yaw*)))
    (when (or (key-held? "w") (key-held? "Up"))
      (try-move! (* MOVE-SPEED sy) (* MOVE-SPEED cy)))
    (when (or (key-held? "s") (key-held? "Down"))
      (try-move! (- (* MOVE-SPEED sy)) (- (* MOVE-SPEED cy))))
    ; Arrow/AD turning only active when mouse is NOT grabbed
    (when (not (gpu-canvas-mouse-grabbed? canvas))
      (when (or (key-held? "a") (key-held? "Left"))
        (set! *yaw* (- *yaw* TURN-SPEED)))
      (when (or (key-held? "d") (key-held? "Right"))
        (set! *yaw* (+ *yaw* TURN-SPEED))))))

(gpu-canvas-on-grab-move! canvas
  (lambda (dx dy) (set! *yaw* (+ *yaw* (* dx MOUSE-SENS)))))

(gpu-canvas-on-mouse! canvas
  (lambda (event btn x y mods)
    (when (eq? event 'press)
      (gpu-canvas-focus! canvas)
      (gpu-canvas-grab-mouse! canvas))))

(window-on-close! win (lambda () (quit-event-loop)))

;; --snapshot PATH: render for a moment, save one frame, quit (for checking).
(define *snapshot*
  (let loop ((a (command-line)))
    (cond ((or (null? a) (null? (cdr a))) #f)
          ((string=? (car a) "--snapshot") (cadr a))
          (else (loop (cdr a))))))
(define *ticks* 0)

(define timer
  (make-timer 16 (lambda ()
    (set! *ticks* (+ *ticks* 1))
    (tick-movement!)
    (gpu-canvas-redraw! canvas)
    (when (and *snapshot* (= *ticks* 40))
      (display (if (gpu-canvas-save-png! canvas *snapshot*) "saved " "could not save "))
      (display *snapshot*) (newline)
      (quit-event-loop)))))

(window-show! win)
(gpu-canvas-focus! canvas)
(update-status!)
(timer-start! timer)
(run-event-loop)
