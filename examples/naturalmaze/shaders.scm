;;; shaders.scm — GLSL fragment shader for the naturalmaze renderer
;;; Pure Scheme (string definitions). Compile with: curry -c shaders.scm

(define *main-prog* #f)   ; set in renderer after GL init
(define *maze-tex*  #f)

(define FRAG-SRC "
#version 330 core
out vec4 frag_color;

uniform vec2  u_resolution;
uniform float u_dpr;
uniform vec2  u_pos;          // player XZ
uniform float u_yaw;
uniform float u_pitch;        // radians, + = looking up
uniform float u_pw;           // visual W (fractional during transition)
uniform float u_time;
uniform float u_half_fov;     // horizontal half-FOV (radians)
uniform float u_wall_h;
uniform float u_fog_dist;
uniform sampler2D u_maze;
uniform int   u_DX;
uniform int   u_DZ;
uniform int   u_DW;

// ── Hash and noise  (prefixed c_ to avoid Qt/driver built-in conflicts) ─────

float c_h1(float n) { return fract(sin(n) * 43758.5453); }
float c_h2(vec2 p)  { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2  c_h22(vec2 p) {
    p = vec2(dot(p, vec2(127.1,311.7)), dot(p, vec2(269.5,183.3)));
    return fract(sin(p) * 43758.5453);
}

float c_n2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(c_h2(i), c_h2(i + vec2(1,0)), f.x),
               mix(c_h2(i + vec2(0,1)), c_h2(i + vec2(1,1)), f.x), f.y);
}

float c_fbm(vec2 p) {
    return 0.500 * c_n2(p)
         + 0.250 * c_n2(p * 2.0 + 7.31)
         + 0.125 * c_n2(p * 4.0 + 3.17);
}

// ── Maze query ──────────────────────────────────────────────────────────────

int cell_bits(int ix, int iz, int iw) {
    if (ix < 0 || ix >= u_DX || iz < 0 || iz >= u_DZ || iw < 0 || iw >= u_DW)
        return 0;
    float u = (float(ix) + 0.5) / float(u_DX);
    float v = (float(iz + iw * u_DZ) + 0.5) / float(u_DZ * u_DW);
    return int(texture(u_maze, vec2(u, v)).r * 255.0 + 0.5);
}

bool blocked_x(int fx, int fz, int iw, int sx) {
    if (sx > 0) return (cell_bits(fx, fz, iw) & 1) == 0;
    else        return (cell_bits(fx, fz, iw) & 2) == 0;
}
bool blocked_z(int fx, int fz, int iw, int sz) {
    if (sz > 0) return (cell_bits(fx, fz, iw) & 4) == 0;
    else        return (cell_bits(fx, fz, iw) & 8) == 0;
}
bool has_w_passage(int ix, int iz, int iw) {
    return (cell_bits(ix, iz, iw) & 48) != 0;  // B+W=16 or B-W=32
}

// ── Biome colour (per W-slice) ──────────────────────────────────────────────
// Returns the dominant fungi glow colour for W-slice iw.

vec3 biome_glow(int iw) {
    if (iw == 0) return vec3(1.00, 0.62, 0.00);   // amber
    if (iw == 1) return vec3(0.00, 0.78, 0.92);   // cyan
    if (iw == 2) return vec3(0.72, 0.10, 0.96);   // violet
    return             vec3(1.00, 0.18, 0.12);    // crimson
}

// ── Procedural wall texture ─────────────────────────────────────────────────

vec3 wall_color(vec2 uv, vec2 cell, int iw) {
    // Base rock — two c_fbm layers for variation
    float r1 = c_fbm(uv * 2.8 + cell * 6.3 + 1.7);
    float r2 = c_fbm(uv * 0.9 + cell * 2.1);
    vec3 rock = mix(vec3(0.26, 0.22, 0.18), vec3(0.44, 0.38, 0.30), r1 + r2 * 0.3);

    // Moss — heavier near the bottom
    float moss_mask = smoothstep(0.55, 0.15, uv.y);
    float moss_n    = c_n2(uv * 5.5 + cell * 3.3 + 9.1);
    float moss      = smoothstep(0.38, 0.58, moss_n) * moss_mask;
    rock = mix(rock, vec3(0.14, 0.30, 0.10), moss * 0.65);

    // Vines — thin verticals hanging from the top
    float vine_mask = smoothstep(0.35, 0.80, uv.y);
    float vine_n    = c_n2(vec2(uv.x * 18.0 + cell.x * 7.7, 1.0));
    float vine      = smoothstep(0.82, 0.90, vine_n) * vine_mask * 0.45;
    rock = mix(rock, vec3(0.11, 0.25, 0.07), vine);

    // Fungi spots — position-seeded so each cell grows them differently
    vec2 f_grid   = floor(uv * 7.0);
    float f_prob  = c_h2(f_grid + cell * 11.3 + float(iw) * 5.7);
    float f_size  = c_h2(f_grid + cell *  4.1) * 0.055 + 0.012;
    vec2  f_ctr   = (f_grid + 0.5) / 7.0;
    float f_d     = length(uv - f_ctr);
    float fungi   = smoothstep(f_size, f_size * 0.25, f_d) * step(0.72, f_prob);

    float pulse   = 0.7 + 0.3 * sin(u_time * 1.6 + c_h2(cell) * 6.28);
    vec3 fc       = biome_glow(iw);
    rock = mix(rock, fc * 1.5, fungi * pulse);
    rock += fc * fungi * pulse * 0.6;   // additive glow

    return rock;
}

// ── Procedural floor texture ────────────────────────────────────────────────

vec3 floor_color(vec2 wp, int iw) {
    vec2 cell  = floor(wp);
    vec2 local = fract(wp);

    float n = c_fbm(wp * 2.4 + 3.1);
    vec3 col = mix(vec3(0.22, 0.16, 0.09), vec3(0.38, 0.28, 0.16), n);

    // Scattered grass tufts
    float grass = c_n2(wp * 3.7 + 7.2);
    col = mix(col, vec3(0.13, 0.32, 0.08), smoothstep(0.46, 0.60, grass) * 0.55);

    // Small mushrooms — 4 candidates per cell
    for (int i = 0; i < 4; i++) {
        vec2 sp = c_h22(cell + vec2(float(i) * 4.3, float(i) * 8.1));
        float sd = length(local - sp) * 5.0;
        float shroom = smoothstep(0.25, 0.0, sd);
        float hv = c_h1(float(i) * 3.7 + c_h2(cell));
        vec3 sc = mix(vec3(0.88, 0.28, 0.10), vec3(0.85, 0.85, 0.92), hv);
        col = mix(col, sc, shroom * 0.8);
        // Cap sheen
        col += vec3(0.6, 0.5, 0.4) * shroom * 0.2;
    }

    // Puddle reflections (flat dark areas)
    float puddle = smoothstep(0.56, 0.62, c_fbm(wp * 1.4 + 5.0));
    col = mix(col, vec3(0.06, 0.07, 0.10), puddle * 0.7);

    // W-rift runes: pulsing ring on cells with W-passages
    ivec2 ic = ivec2(int(floor(wp.x)), int(floor(wp.y)));
    if (has_w_passage(ic.x, ic.y, iw)) {
        float dist_c = length(local - 0.5);
        float ring   = smoothstep(0.04, 0.0, abs(dist_c - 0.36));
        float spoke  = 0.0;
        for (int s = 0; s < 6; s++) {
            float a = float(s) * 1.0472;
            vec2 dir = vec2(cos(a), sin(a));
            float t = dot(local - 0.5, dir);
            float side = abs(dot(local - 0.5, vec2(-dir.y, dir.x)));
            spoke = max(spoke, smoothstep(0.025, 0.0, side) *
                                step(0.0, t) * step(t, 0.32));
        }
        float pulse = 0.5 + 0.5 * sin(u_time * 3.2 - dist_c * 12.0);
        vec3 wc = vec3(0.48, 0.12, 1.0) * (1.0 + pulse * 0.6);
        col = mix(col, wc, (ring + spoke * 0.5) * clamp(pulse + 0.3, 0.3, 1.0));
    }

    return col;
}

// ── Procedural ceiling texture ──────────────────────────────────────────────

vec3 ceiling_color(vec2 wp, int iw) {
    float n = c_fbm(wp * 2.2 + 7.1);
    vec3 col = mix(vec3(0.18, 0.15, 0.13), vec3(0.30, 0.25, 0.20), n);

    // Stalactite tips
    float stala = c_n2(wp * 7.5 + 2.9);
    col = mix(col, vec3(0.42, 0.35, 0.28), smoothstep(0.64, 0.80, stala) * 0.4);

    // Bioluminescent patches — slowly breathing
    float glow  = c_n2(wp * 4.8 + vec2(u_time * 0.08, 0.0));
    float glow2 = c_n2(wp * 11.0 - u_time * 0.04);
    float lit   = smoothstep(0.58, 0.74, glow) * smoothstep(0.48, 0.66, glow2);
    float pulse = 0.6 + 0.4 * sin(u_time * 1.2 + c_h2(floor(wp)) * 6.28);
    vec3 gc     = biome_glow(iw);
    col = mix(col, gc * 1.3, lit * pulse * 0.7);
    col += gc * lit * pulse * 0.5;

    return col;
}

// ── Fog ─────────────────────────────────────────────────────────────────────

vec3 apply_fog(vec3 col, float dist) {
    vec3  fog_col = vec3(0.018, 0.010, 0.035);   // dark violet cave air
    float t = clamp(dist / u_fog_dist, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);                 // smoothstep for soft edge
    return mix(col, fog_col, t);
}

// ── Main ────────────────────────────────────────────────────────────────────

void main() {
    vec2  fragCoord = gl_FragCoord.xy;
    float sw = u_resolution.x;
    float sh = u_resolution.y;

    // Column ray direction
    float col_t     = (fragCoord.x / sw - 0.5) * 2.0;
    float ray_angle = u_yaw + col_t * u_half_fov;
    vec2  ray_dir   = vec2(sin(ray_angle), cos(ray_angle));

    // Horizon Y (pitch shifts it)
    float horizon_y = sh * 0.5 - u_pitch * sh * 0.42;

    // Current W slice and blend fraction
    int   iw_int  = clamp(int(floor(u_pw + 0.5)), 0, u_DW - 1);
    float iw_frac = abs(fract(u_pw));              // ripple strength during transit

    // ── DDA ray march ──────────────────────────────────────────────────────
    ivec2 map_c  = ivec2(int(floor(u_pos.x)), int(floor(u_pos.y)));
    vec2  delta  = abs(1.0 / max(abs(ray_dir), vec2(1e-6)));
    ivec2 step_i = ivec2(ray_dir.x < 0.0 ? -1 : 1, ray_dir.y < 0.0 ? -1 : 1);
    vec2  side_d;
    side_d.x = (ray_dir.x < 0.0) ? (u_pos.x - float(map_c.x)) * delta.x
                                  : (float(map_c.x + 1) - u_pos.x) * delta.x;
    side_d.y = (ray_dir.y < 0.0) ? (u_pos.y - float(map_c.y)) * delta.y
                                  : (float(map_c.y + 1) - u_pos.y) * delta.y;

    float perp_dist = 0.0;
    int   hit_face  = 0;   // 0=+X 1=-X 2=+Z 3=-Z
    bool  hit_wall  = false;

    for (int step = 0; step < 96; step++) {
        if (side_d.x < side_d.y) {
            int nx = map_c.x + step_i.x;
            bool wall = (nx < 0 || nx >= u_DX) || blocked_x(map_c.x, map_c.y, iw_int, step_i.x);
            perp_dist = side_d.x;
            side_d.x += delta.x;
            if (wall) { hit_face = (step_i.x > 0) ? 0 : 1; hit_wall = true; break; }
            map_c.x = nx;
        } else {
            int nz = map_c.y + step_i.y;
            bool wall = (nz < 0 || nz >= u_DZ) || blocked_z(map_c.x, map_c.y, iw_int, step_i.y);
            perp_dist = side_d.y;
            side_d.y += delta.y;
            if (wall) { hit_face = (step_i.y > 0) ? 2 : 3; hit_wall = true; break; }
            map_c.y = nz;
        }
    }

    if (!hit_wall) { frag_color = vec4(0.01, 0.006, 0.02, 1.0); return; }

    // ── Wall stripe geometry ────────────────────────────────────────────────
    float line_h    = sh * u_wall_h / max(perp_dist, 0.001);
    float draw_top  = horizon_y + line_h * 0.5;
    float draw_bot  = horizon_y - line_h * 0.5;
    float sy        = fragCoord.y;

    // Where along the wall did the ray hit? (wall texture U)
    float wall_u;
    if (hit_face == 0 || hit_face == 1) {
        wall_u = fract(u_pos.y + perp_dist * ray_dir.y);
        if (hit_face == 1) wall_u = 1.0 - wall_u;
    } else {
        wall_u = fract(u_pos.x + perp_dist * ray_dir.x);
        if (hit_face == 2) wall_u = 1.0 - wall_u;
    }

    vec3 final_col;

    if (sy >= draw_bot && sy <= draw_top) {
        // ── Wall pixel ──────────────────────────────────────────────────────
        float wall_v = (sy - draw_bot) / max(draw_top - draw_bot, 1.0);
        vec2  wall_uv = vec2(wall_u, wall_v);
        vec2  cell_f  = vec2(float(map_c.x), float(map_c.y));

        vec3 wc = wall_color(wall_uv, cell_f, iw_int);

        // Slight side-face darkening for depth cue
        if (hit_face == 0 || hit_face == 1) wc *= 0.82;

        final_col = apply_fog(wc, perp_dist);

    } else {
        // ── Floor / ceiling ray ──────────────────────────────────────────────
        float row_dist_raw = sh * u_wall_h * 0.5 / max(abs(sy - horizon_y), 0.5);
        vec2  world_pos    = u_pos + row_dist_raw * ray_dir;

        if (sy < draw_bot) {
            // Ceiling
            final_col = apply_fog(ceiling_color(world_pos, iw_int), row_dist_raw * 1.1);
        } else {
            // Floor
            final_col = apply_fog(floor_color(world_pos, iw_int), row_dist_raw);
        }
    }

    // ── Dimensional ripple during W-transition ───────────────────────────────
    if (iw_frac > 0.02) {
        float ripple = sin(sy * 0.04 + u_time * 6.0) * iw_frac * 0.06;
        // Chromatic shift hint (tint toward biome colour of dest slice)
        vec3 dest_glow = biome_glow(clamp(int(floor(u_pw + 0.5 + 0.5 * sign(fract(u_pw) - 0.5))), 0, u_DW - 1));
        final_col = mix(final_col, dest_glow * length(final_col), iw_frac * 0.15 * (0.5 + 0.5 * ripple));
    }

    frag_color = vec4(final_col, 1.0);
}
")
