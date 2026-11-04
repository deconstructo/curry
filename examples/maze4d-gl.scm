#!/usr/bin/env curry
;;; maze4d.scm — Wander a 4D maze as a 3D creature.
;;;
;;; The maze has passages in ±x, ±z within each w-slice, plus ±w connections
;;; between slices.  At any moment you see a first-person view (GPU DDA
;;; raycaster with anaglyph stereo) of the 3D cross-section at your current w.
;;; Press Q/E to step through w — the maze instantly resets around you.
;;;
;;; Goal: reach cell (DX-1, DZ-1) on w-slice (DW-1).  Yellow dot on minimap.
;;;
;;; Controls:
;;;   W / ↑    move forward        Q   step −w
;;;   S / ↓    move back           E   step +w
;;;   A / ←    turn left           Esc quit
;;;   D / →    turn right

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
;;; Maze cell data is uploaded as a DX × (DZ×DW) R8 texture.
;;; Byte at column ix, row (iz + iw*DZ) = *cells*[cidx(ix,iz,iw)].
;;;
;;; The fragment shader casts two DDA rays per pixel (left/right eye for
;;; anaglyph stereo) and composes ceiling + floor background + wall stripes
;;; directly in a single pass.

(define *raycaster-frag* "
#version 330 core
out vec4 frag_color;

uniform vec2  u_resolution;   /* physical pixels — auto-set by gl-shader-draw! */
uniform float u_dpr;          /* device pixel ratio — auto-set                 */
uniform sampler2D u_maze;     /* DX × (DZ*DW), R8                              */
uniform vec2  u_pos;          /* (px, pz) player position                      */
uniform float u_yaw;
uniform float u_half_fov;     /* camera-plane half-width (0.66)                */
uniform float u_wall_h;       /* wall height in world units (1.0)              */
uniform float u_fog;          /* fog falloff distance                          */
uniform float u_eye_sep;      /* anaglyph inter-ocular separation              */
uniform int   u_pw;           /* current w-slice                               */
uniform int   u_DX;
uniform int   u_DZ;
uniform int   u_DW;

int cell_bits(int ix, int iz) {
    if (ix < 0 || ix >= u_DX || iz < 0 || iz >= u_DZ) return 0;
    float u = (float(ix) + 0.5) / float(u_DX);
    float v = (float(iz + u_pw * u_DZ) + 0.5) / float(u_DZ * u_DW);
    return int(texture(u_maze, vec2(u, v)).r * 255.0 + 0.5);
}

float cast_ray(vec2 pos, vec2 rd, out bool x_side) {
    float ddx = abs(rd.x) < 1e-10 ? 1e30 : abs(1.0 / rd.x);
    float ddz = abs(rd.y) < 1e-10 ? 1e30 : abs(1.0 / rd.y);
    int ix = int(floor(pos.x));
    int iz = int(floor(pos.y));
    int sx = rd.x > 0.0 ? 1 : -1;
    int sz = rd.y > 0.0 ? 1 : -1;
    float sdx = rd.x > 0.0 ? (float(ix + 1) - pos.x) * ddx
                            : (pos.x - float(ix))     * ddx;
    float sdz = rd.y > 0.0 ? (float(iz + 1) - pos.y) * ddz
                            : (pos.y - float(iz))     * ddz;
    for (int i = 0; i < 64; i++) {
        if (min(sdx, sdz) > 18.0) { x_side = false; return -1.0; }
        if (sdx < sdz) {
            int bit = sx > 0 ? 1 : 2;
            if ((cell_bits(ix, iz) & bit) == 0) { x_side = true;  return max(0.01, sdx - ddx); }
            ix += sx; sdx += ddx;
        } else {
            int bit = sz > 0 ? 4 : 8;
            if ((cell_bits(ix, iz) & bit) == 0) { x_side = false; return max(0.01, sdz - ddz); }
            iz += sz; sdz += ddz;
        }
    }
    x_side = false; return -1.0;
}

/* Brick/masonry pattern on walls.
 *   tx  = fractional position along the wall (one cell = 1.0)
 *   ty  = fractional position top→bottom within the wall stripe
 * Returns a brightness multiplier:
 *   mortar gaps → 0.25  (dark recessed lines)
 *   brick faces → 0.80–0.95  (random per-brick variation)
 *
 * Mortar lines run horizontally every ¼ wall height and vertically every
 * ½ world unit with a half-brick stagger per row.  The mortar lines
 * compress together as distance grows — the strongest depth cue.
 * Brick faces are slightly randomised so long corridors don't look uniform. */
float brick(float tx, float ty) {
    float row = floor(ty * 4.0);
    float fy  = fract(ty * 4.0);
    float fx  = fract(tx * 2.0 + mod(row, 2.0) * 0.5);
    if (fy < 0.07 || fx < 0.06) return 0.25;          /* mortar */
    float col = floor(tx * 2.0 + mod(row, 2.0) * 0.5);
    return 0.80 + fract(sin(row * 7.31 + col * 13.17) * 43758.55) * 0.15;
}

void main() {
    float sw  = u_resolution.x / u_dpr;
    float sh  = u_resolution.y / u_dpr;
    float lx  = gl_FragCoord.x / u_dpr;
    float ly  = sh - gl_FragCoord.y / u_dpr;   /* flip Y: 0 = top */
    float hsh = sh * 0.5;

    float cam = 2.0 * lx / sw - 1.0;
    float sy  = sin(u_yaw);
    float cy  = cos(u_yaw);
    vec2  rd  = vec2(sy + cy * cam * u_half_fov, cy - sy * cam * u_half_fov);
    float hs  = u_eye_sep * 0.5;
    vec2 l_pos = u_pos + vec2(-cy,  sy) * hs;
    vec2 r_pos = u_pos + vec2( cy, -sy) * hs;

    /* ── Background ─────────────────────────────────────────────────────── */
    vec3 bg;
    if (ly < hsh) {
        /* Ceiling: plain dark purple */
        bg = vec3(0.04, 0.03, 0.06);
    } else {
        /* Floor: perspective grid.
         * row_dist = perpendicular distance to the floor at this screen row.
         * Player eye is at height 0.5 (mid-wall), floor at 0.
         * lp = 0.5 / (ly/hsh - 1)  →  infinity at horizon, 0.5 at feet. */
        float row_dist = 0.5 / max(0.001, ly / hsh - 1.0);
        vec2  nrd = normalize(rd);
        float fx  = fract(u_pos.x + row_dist * nrd.x);
        float fz  = fract(u_pos.y + row_dist * nrd.y);
        bool  gline = fx < 0.05 || fz < 0.05;
        /* Grid lines stay at the base dark colour; open tiles lighten slightly
         * near the player so the lines read as recessed cracks. */
        float fog_f = max(0.0, 1.0 - row_dist / u_fog);
        float extra = gline ? 0.0 : fog_f * 0.07;
        bg = vec3(0.09 + extra, 0.08 + extra, 0.06 + extra);
    }

    /* ── Wall rays ───────────────────────────────────────────────────────── */
    bool lxs, rxs;
    float lp = cast_ray(l_pos, rd, lxs);
    float rp = cast_ray(r_pos, rd, rxs);

    /* hit.coord = pos + dist * rd  (exact: rd·view_dir = 1 by construction) */
    float ra = 0.0, ba = 0.0;
    if (lp > 0.0) {
        float h = u_wall_h * hsh / lp;
        if (ly >= hsh - h && ly <= hsh + h) {
            float tx  = lxs ? fract(l_pos.y + lp * rd.y) : fract(l_pos.x + lp * rd.x);
            float ty  = (ly - (hsh - h)) / (2.0 * h);
            float fog = max(0.0, 1.0 - lp / u_fog);
            ra = fog * (lxs ? 1.0 : 0.7) * brick(tx, ty);
        }
    }
    if (rp > 0.0) {
        float h = u_wall_h * hsh / rp;
        if (ly >= hsh - h && ly <= hsh + h) {
            float tx  = rxs ? fract(r_pos.y + rp * rd.y) : fract(r_pos.x + rp * rd.x);
            float ty  = (ly - (hsh - h)) / (2.0 * h);
            float fog = max(0.0, 1.0 - rp / u_fog);
            ba = fog * (rxs ? 1.0 : 0.7) * brick(tx, ty);
        }
    }

    frag_color = vec4(clamp(bg.r + ra, 0.0, 1.0),
                      clamp(bg.g,      0.0, 1.0),
                      clamp(bg.b + ba, 0.0, 1.0), 1.0);
}
")

(define *raycaster-prog* #f)
(define *maze-tex*       #f)

; Pack *cells* into a bytevector laid out as DX × (DZ*DW) rows.
; Row (iz + iw*DZ), column ix → byte index = iw*DX*DZ + iz*DX + ix = cidx(ix,iz,iw).
(define (make-maze-bvec)
  (let* ((n  (* DX DZ DW))
         (bv (make-bytevector n 0)))
    (do ((i 0 (+ i 1))) ((= i n))
      (bytevector-u8-set! bv i (vector-ref *cells* i)))
    bv))

;;; ── HUD: minimap ──────────────────────────────────────────────────────────

(define (draw-minimap! painter sw sh)
  (let* ((mw  (* DX MAP-CELL))
         (mh  (* DZ MAP-CELL))
         (ox  (- sw mw MAP-PAD))
         (oy  MAP-PAD)
         (iw  *pw*))
    (gfx-set-color! painter 0.0 0.0 0.15 0.75)
    (gfx-fill-rect! painter ox oy mw mh)
    (gfx-set-pen-color! painter 0.4 0.5 1.0 1.0)
    (gfx-set-pen-width! painter 1.0)
    (do ((k 0 (+ k 1))) ((> k DX))
      (do ((iz 0 (+ iz 1))) ((= iz DZ))
        (when (or (= k 0) (= k DX) (not (passage? (- k 1) iz iw B+X)))
          (let ((px (+ ox (* k MAP-CELL))) (py (+ oy (* iz MAP-CELL))))
            (gfx-draw-line! painter px py px (+ py MAP-CELL))))))
    (do ((k 0 (+ k 1))) ((> k DZ))
      (do ((ix 0 (+ ix 1))) ((= ix DX))
        (when (or (= k 0) (= k DZ) (not (passage? ix (- k 1) iw B+Z)))
          (let ((px (+ ox (* ix MAP-CELL))) (py (+ oy (* k MAP-CELL))))
            (gfx-draw-line! painter px py (+ px MAP-CELL) py)))))
    (when (= iw (- DW 1))
      (gfx-set-color! painter 1.0 0.9 0.1 1.0)
      (gfx-fill-circle! painter
        (+ ox (* (- DX 0.5) MAP-CELL))
        (+ oy (* (- DZ 0.5) MAP-CELL))
        4.5))
    (let* ((pcx (+ ox (* *px* MAP-CELL)))
           (pcy (+ oy (* *pz* MAP-CELL))))
      (gfx-set-color! painter 1.0 0.3 0.3 1.0)
      (gfx-fill-circle! painter pcx pcy 4.5)
      (gfx-set-pen-color! painter 1.0 0.6 0.6 1.0)
      (gfx-draw-line! painter pcx pcy
        (+ pcx (* 9.0 (sin *yaw*)))
        (+ pcy (* 9.0 (cos *yaw*)))))))

;;; ── HUD: w-level indicator ────────────────────────────────────────────────

(define (draw-w-indicator! painter)
  (let ((y 14.0) (r 6.0) (gap 18.0) (x0 16.0))
    (do ((i 0 (+ i 1))) ((= i DW))
      (let ((cx (+ x0 (* i gap))))
        (if (= i *pw*)
            (begin (gfx-set-color! painter 0.2 1.0 0.5 1.0)
                   (gfx-fill-circle! painter cx y r))
            (begin (gfx-set-color! painter 0.1 0.3 0.2 0.9)
                   (gfx-fill-circle! painter cx y r)))))))

;;; ── Win check ─────────────────────────────────────────────────────────────

(define (at-goal?)
  (and (= (player-ix) (- DX 1))
       (= (player-iz) (- DZ 1))
       (= *pw* (- DW 1))))

;;; ── Main draw ─────────────────────────────────────────────────────────────

(define (draw-frame! painter sw sh)
  ; GPU renders the 3D view (background + walls, both anaglyph eyes)
  (gl-shader-draw! *raycaster-prog* painter
    (list (cons "u_maze"     *maze-tex*)
          (cons "u_pos"      (list *px* *pz*))
          (cons "u_yaw"      *yaw*)
          (cons "u_half_fov" HALF-FOV)
          (cons "u_wall_h"   WALL-H)
          (cons "u_fog"      FOG-DIST)
          (cons "u_eye_sep"  *eye-sep*)
          (cons "u_pw"       *pw*)
          (cons "u_DX"       DX)
          (cons "u_DZ"       DZ)
          (cons "u_DW"       DW)))
  ; HUD via QPainter on top (gl-shader-draw! calls endNativePainting first)
  (draw-minimap!     painter sw sh)
  (draw-w-indicator! painter)
  (when (at-goal?)
    (gfx-set-color! painter 1.0 1.0 0.0 0.95)
    (gfx-draw-text! painter (/ sw 2.0) (/ sh 2.0) "YOU ESCAPED THE 4D MAZE")))

;;; ── Input ─────────────────────────────────────────────────────────────────

(define *held* (make-hash-table equal?))  ; currently held keys

(define (key-held? k) (hash-table-ref *held* k #f))

(define (handle-key-down! key mods)
  (hash-table-set! *held* key #t)
  (cond
    ((equal? key "q") (try-step-w! -1))
    ((equal? key "e") (try-step-w!  1))
    ((equal? key "Escape")
     (if (canvas-mouse-grabbed? canvas)
         (canvas-release-mouse! canvas)
         (quit-event-loop)))
    ((equal? key "Q") (quit-event-loop))))

(define (handle-key-up! key mods)
  (hash-table-delete! *held* key))

(define (tick-movement!)
  (let* ((sy (sin *yaw*)) (cy (cos *yaw*)))
    (when (or (key-held? "w") (key-held? "Up"))
      (try-move! (* MOVE-SPEED sy) (* MOVE-SPEED cy)))
    (when (or (key-held? "s") (key-held? "Down"))
      (try-move! (- (* MOVE-SPEED sy)) (- (* MOVE-SPEED cy))))
    ; Arrow/AD turning only active when mouse is NOT grabbed
    (when (not (canvas-mouse-grabbed? canvas))
      (when (or (key-held? "a") (key-held? "Left"))
        (set! *yaw* (- *yaw* TURN-SPEED)))
      (when (or (key-held? "d") (key-held? "Right"))
        (set! *yaw* (+ *yaw* TURN-SPEED))))))

;;; ── Window ────────────────────────────────────────────────────────────────

(define win     (make-window "4D Maze" 1080 640))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

(box-add! sidebar (make-label "Stereo (red/blue)"))
(box-add! sidebar
  (make-slider "Eye separation" 0 50 1 10
    (lambda (v) (set! *eye-sep* (* v 0.01)))))
(box-add! sidebar (make-separator))
(box-add! sidebar (make-label "Controls"))
(box-add! sidebar (make-label "W/S/↑/↓  move"))
(box-add! sidebar (make-label "Click     grab mouse"))
(box-add! sidebar (make-label "Mouse     look"))
(box-add! sidebar (make-label "A/D/←/→  turn (ungrabbed)"))
(box-add! sidebar (make-label "Q/E      step ±w"))
(box-add! sidebar (make-label "Esc      release / quit"))

(canvas-on-draw!  canvas draw-frame!)
(window-on-key!    win handle-key-down!)
(window-on-key-up! win handle-key-up!)
(window-on-close!  win (lambda () (quit-event-loop)))

(canvas-on-grab-move! canvas
  (lambda (dx dy)
    (set! *yaw* (+ *yaw* (* dx MOUSE-SENS)))))

(canvas-on-mouse! canvas
  (lambda (event btn x y mods)
    (when (eq? event 'press)
      (canvas-grab-mouse! canvas))))

(define timer
  (make-timer 16 (lambda ()
    (tick-movement!)
    (canvas-redraw! canvas))))

(window-on-realize! win
  (lambda ()
    (set! *raycaster-prog* (make-gl-shader *raycaster-frag*))
    (set! *maze-tex*       (make-gl-texture (make-maze-bvec) DX (* DZ DW)))
    (timer-start! timer)))

(window-show! win)
(run-event-loop)
