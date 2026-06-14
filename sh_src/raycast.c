#include "mars.h"
#include "raycast.h"
#include "sin_table.h"
#include "wall_tex.h"

/* Player spawn — west end of the main east-west spine, facing east. */
player_t player = {
    .x = FX(1.5),
    .y = FX(7.5),
    .angle = 0,
};

/* 16x16 Backrooms-ish layout: one long east-west spine corridor at
 * y=7..8 with five north-south branch alcoves. The long sightline
 * down the spine sells the distance shading; the alcoves give you
 * places to peek into / get lost in. */
const uint8_t world_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,0,0,1,1,1,0,0,1,1,1,1,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* Palette layout (8bpp, 256 entries):
 *   0          : black (sky / unrendered)
 *   1..16      : yellow wallpaper, brightest..darkest (distance shading)
 *   17..32     : brown carpet floor, brightest..darkest
 *   33..48     : off-white ceiling, brightest..darkest
 */
#define WALL_BASE    1
#define FLOOR_BASE   17
#define CEIL_BASE    33
#define LIGHT_BASE   49     /* 4 entries: full / 75% / 50% / 25% for flicker */
#define SHADE_LEVELS 16

/* Wall texture comes from wall_tex.h (generated from images/walltile.jpg). */
#define TEX_W WALL_TEX_WIDTH
#define TEX_H WALL_TEX_HEIGHT

/* Liminal fog: hard view-distance cap. Walls past this distance don't get
 * rendered (column falls back to the floor/ceiling haze). Doubles as a
 * Backrooms aesthetic (haze hides the geometry beyond) AND a perf knob
 * (fewer wall pixels per frame on long sightlines). */
#define MAX_VIEW_DIST     FX(6)
#define MAX_VIEW_DIST_INT 6

/* Per-column z-buffer captured during wall draw so the light billboards
 * can z-test against walls. 0x7FFFFFFF = no wall hit (light wins). */
static fx_t wall_dist[SCREEN_W];

/* Ceiling fluorescent tube positions in world coords. Each one renders
 * as a small horizontal bright bar at its world position, sized by
 * distance, flickering pseudo-randomly. */
typedef struct { fx_t x, y; } light_t;
static const light_t lights[] = {
    /* Main east-west spine, every 2 cells */
    { FX(2.5),  FX(7.5)  }, { FX(4.5),  FX(7.5)  },
    { FX(6.5),  FX(7.5)  }, { FX(8.5),  FX(7.5)  },
    { FX(10.5), FX(7.5)  }, { FX(12.5), FX(7.5)  },
    /* North-side branch alcoves */
    { FX(2.5),  FX(3.5)  }, { FX(7.5),  FX(3.5)  }, { FX(13.5), FX(3.5)  },
    /* South-side branch alcoves */
    { FX(2.5),  FX(11.5) }, { FX(7.5),  FX(11.5) }, { FX(13.5), FX(11.5) },
};
#define NUM_LIGHTS (int)(sizeof(lights) / sizeof(lights[0]))

/* How many times the wallpaper tile repeats per 1-unit map cell.
 * TEX_W/H must be powers of 2 so the wrap can be a cheap bitmask. */
#define WALL_TILE_X 16
#define WALL_TILE_Y 16
#define TEX_W_MASK  (TEX_W - 1)
#define TEX_H_MASK  (TEX_H - 1)


/* Precomputed pixel color per screen row for the base floor/ceiling layer.
 * Indexes [0..SCREEN_H/2-1] = ceiling, bright at top dim toward horizon;
 * [SCREEN_H/2..SCREEN_H-1] = floor, dim near horizon bright at bottom. */
static uint8_t row_color[SCREEN_H];

static void build_shading_tables(void) {
    int mid = SCREEN_H / 2;
    for (int y = 0; y < SCREEN_H; y++) {
        int yy;
        if (y < mid)      yy = mid - y;
        else if (y > mid) yy = y - mid;
        else              yy = 1;          /* avoid div-by-zero at horizon */
        /* Classic raycaster row distance: rowDist = mid / yy.
         * Pre-clamp to shade range; visually compresses far rows into the
         * darkest shade (the "Backrooms haze" near the horizon). */
        int shade = mid / yy - 1;
        if (shade < 0) shade = 0;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        row_color[y] = (y <= mid) ? (CEIL_BASE + shade)
                                  : (FLOOR_BASE + shade);
    }

    /* Drop-ceiling grid: darken one row at each integer world-distance
     * boundary. Computed once into row_color so there's no runtime cost.
     * Perspective-compressed naturally — grid lines bunch up near horizon.
     * Provides visual context so the fluorescent tubes read as embedded
     * fixtures, not floating exit signs. */
    int prev_d = -1;
    for (int y = 0; y < mid; y++) {
        int p = mid - y;
        int d = mid / p;
        if (d != prev_d) {
            /* This row is a grid line. Darken by 2 shades for visibility. */
            int shade = row_color[y] - CEIL_BASE;
            shade += 2;
            if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
            row_color[y] = CEIL_BASE + shade;
        }
        prev_d = d;
    }
}

/* Fade target — what every surface fades toward at maximum distance.
 * Pure black: misty grey was reading as "too bright" at depth, removing
 * the contrast between near and far. Black gives the classic raycaster
 * "darkness eats the corridor" look.
 *
 * Note: this isn't a perf knob — it's just palette base values at init.
 * Changing these has zero runtime cost. */
#define FOG_R 0
#define FOG_G 0
#define FOG_B 0

/* Linear blend of bright base (weight: SHADE_LEVELS - i) toward fog (weight: i). */
#define MIX(bright, fog, i) (((bright) * (SHADE_LEVELS - (i)) + (fog) * (i)) / SHADE_LEVELS)

static void build_palette(void) {
    Hw32xSetBGColor(0, 0, 0, 0);
    /* Walls: stronger yellow eggshell. R bumped to 30, B dropped to 13
     * (R-B gap 17) — wallpaper now clearly yellow while still bright
     * cream rather than dingy mustard. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(WALL_BASE + i,
                        MIX(30, FOG_R, i),
                        MIX(27, FOG_G, i),
                        MIX(13, FOG_B, i));
    }
    /* Carpet: yellower stained-mustard brown. Pulled back into the yellow
     * family with R high and B much lower for the saturated Backrooms look,
     * still slightly less bright than walls so the seam reads. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(FLOOR_BASE + i,
                        MIX(27, FOG_R, i),
                        MIX(22, FOG_G, i),
                        MIX(11, FOG_B, i));
    }
    /* Ceiling: pulled further into the yellow family — was reading too
     * "white drop ceiling" against the warm walls. B dropped from 18 to
     * 14, G from 26 to 25; still slightly cooler/less saturated than the
     * walls (30,27,13) but unmistakably in the same warm yellow palette. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(CEIL_BASE + i,
                        MIX(27, FOG_R, i),
                        MIX(25, FOG_G, i),
                        MIX(14, FOG_B, i));
    }
    /* Fluorescent lights: 4 brightness states for flicker (full / 75 / 50 / 25%). */
    Hw32xSetBGColor(LIGHT_BASE + 0, 31, 31, 28);
    Hw32xSetBGColor(LIGHT_BASE + 1, 23, 23, 21);
    Hw32xSetBGColor(LIGHT_BASE + 2, 15, 15, 14);
    Hw32xSetBGColor(LIGHT_BASE + 3,  7,  7,  7);
}

/* Byte pointer to the start of pixel data in the current back framebuffer.
 * (32X 8bpp layout: 0x100 words of line table, then pixels at byte offset 0x200.)
 * volatile is required — the VDP observes these writes, the compiler doesn't. */
static inline volatile uint8_t *fb_pixels(void) {
    return (volatile uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
}

void raycast_init(void) {
    build_palette();
    build_shading_tables();
}

/* Per-frame palette nudge on the brightest wall and ceiling entries —
 * mimics a dying fluorescent's flicker. Must be called from inside
 * vblank (after the COMM12 tick wait) to avoid mid-frame CRAM tearing. */
void raycast_shimmer(void) {
    static uint32_t frame_count = 0;
    frame_count++;
    /* LCG, top bits are the most uncorrelated. */
    uint32_t r = frame_count * 1103515245u + 12345u;
    int wall_f = (r >> 28) & 3;     /* 0..3 */
    int ceil_f = (r >> 26) & 3;
    /* Subtract from bright base. WALL_BASE base is (30,25,6); CEIL_BASE
     * base is (26,26,26). Subtracting up to 3 units is barely visible
     * per pixel but reads as flicker when it changes every frame. */
    Hw32xSetBGColor(WALL_BASE, 30 - wall_f, 25 - wall_f, 6);
    Hw32xSetBGColor(CEIL_BASE, 26 - ceil_f, 26 - ceil_f, 26 - ceil_f);
}


/* Returns 1 if cell (x, y) is walkable, 0 if blocked or out of bounds. */
static int cell_passable(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 0;
    return world_map[y][x] == 0;
}

/* Read controller, advance player by one frame. Axis-separated collision
 * gives natural sliding along walls. */
void player_update(void) {
    HwMdReadPad(0);
    uint16_t pad = MARS_SYS_COMM8;

    fx_t walk = (pad & SEGA_CTRL_C) ? FX(0.10) : FX(0.05);
    uint8_t turn = 2;

    /* B toggles left/right between turning and strafing. */
    int strafing = (pad & SEGA_CTRL_B) != 0;

    if (!strafing) {
        if (pad & SEGA_CTRL_LEFT)  player.angle -= turn;
        if (pad & SEGA_CTRL_RIGHT) player.angle += turn;
    }

    fx_t dirX = COS_FX(player.angle);
    fx_t dirY = SIN_FX(player.angle);
    /* Player's RIGHT is dir rotated +90° (with +y down). */
    fx_t rightX = -dirY;
    fx_t rightY =  dirX;

    fx_t dx = 0, dy = 0;
    if (pad & SEGA_CTRL_UP)   { dx += FX_MUL(dirX, walk); dy += FX_MUL(dirY, walk); }
    if (pad & SEGA_CTRL_DOWN) { dx -= FX_MUL(dirX, walk); dy -= FX_MUL(dirY, walk); }
    if (strafing) {
        if (pad & SEGA_CTRL_RIGHT) { dx += FX_MUL(rightX, walk); dy += FX_MUL(rightY, walk); }
        if (pad & SEGA_CTRL_LEFT)  { dx -= FX_MUL(rightX, walk); dy -= FX_MUL(rightY, walk); }
    }

    /* Axis-separated collision: try X first, then Y. */
    fx_t newX = player.x + dx;
    if (cell_passable(FX_INT(newX), FX_INT(player.y))) player.x = newX;

    fx_t newY = player.y + dy;
    if (cell_passable(FX_INT(player.x), FX_INT(newY))) player.y = newY;
}

/* Project each ceiling light to screen space, paint a small bright bar
 * with z-test against wall_dist, apply per-light flicker. The math is the
 * sprite-billboard transform; the cost is ~50-100 cycles per light. */
static void draw_lights(volatile uint8_t *fb,
                        fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY) {
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = FX_DIV(FX_ONE, det);

    static uint32_t light_frame = 0;
    light_frame++;

    for (int i = 0; i < NUM_LIGHTS; i++) {
        fx_t sx = lights[i].x - player.x;
        fx_t sy = lights[i].y - player.y;

        fx_t transformX = FX_MUL(inv_det,
                            FX_MUL( dirY,  sx) - FX_MUL( dirX,  sy));
        fx_t transformY = FX_MUL(inv_det,
                            FX_MUL(-planeY, sx) + FX_MUL( planeX, sy));
        if (transformY < FX(0.5))     continue;     /* behind / too close */
        if (transformY >= MAX_VIEW_DIST) continue;  /* beyond fog */

        /* Project to screen.
         * screenX = SCREEN_W/2 + (transformX/transformY) * (SCREEN_W/2)
         * screenY = SCREEN_H/2 - (SCREEN_H/2)/transformY
         *   (lights are mounted at ceiling height; sits above horizon). */
        fx_t ratio = FX_DIV(transformX, transformY);
        int screenX = (SCREEN_W >> 1)
                    + (int)(((int32_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);
        int yoff   = (int)(((int32_t)(SCREEN_H >> 1) << FX_SHIFT) / transformY);
        int screenY = (SCREEN_H >> 1) - yoff;

        int dist_int = FX_INT(transformY);
        if (dist_int < 1) dist_int = 1;
        /* Small recessed panels, ~2:1 aspect ratio. Matches the reference
         * photo where multiple modest light panels sit in the drop-ceiling
         * grid rather than long exposed tubes. */
        int width  = (SCREEN_W >> 3) / dist_int;
        int height = (SCREEN_H >> 4) / dist_int;
        if (width  < 2) width  = 2;
        if (height < 1) height = 1;

        int x0 = screenX - width  / 2;
        int x1 = screenX + width  / 2;
        int y0 = screenY - height / 2;
        int y1 = screenY + height / 2;
        if (x0 < 0)         x0 = 0;
        if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
        if (y0 < 0)         y0 = 0;
        if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;

        /* Per-light flicker: pseudo-random brightness state from LCG of
         * (frame, light_index). Most frames the light is fully on; a small
         * fraction it dims for one frame. */
        uint32_t r = light_frame * 1103515245u + i * 12347u;
        int roll = (r >> 24) & 0x1F;
        uint8_t color;
        if      (roll < 2)  color = LIGHT_BASE + 2;   /* deep dim */
        else if (roll < 5)  color = LIGHT_BASE + 1;   /* slight dim */
        else                color = LIGHT_BASE + 0;   /* full on */

        for (int x = x0; x <= x1; x++) {
            if (transformY >= wall_dist[x]) continue;  /* wall in front */
            volatile uint8_t *p = fb + y0 * SCREEN_W + x;
            for (int y = y0; y <= y1; y++) {
                *p = color;
                p += SCREEN_W;
            }
        }
    }
}

void raycast_render(void) {
    volatile uint8_t *fb = fb_pixels();

    /* Camera basis: forward = (cos a, sin a); camera plane perpendicular,
     * length 0.66 -> ~66° horizontal FOV. */
    fx_t dirX   = COS_FX(player.angle);
    fx_t dirY   = SIN_FX(player.angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    /* Floor + ceiling: per-row solid color (32-bit aligned writes,
     * 4 pixels per store). Fast clear. */
    volatile uint32_t *fb32 = (volatile uint32_t *)fb;
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t  c   = row_color[y];
        uint32_t c32 = ((uint32_t)c << 24) | ((uint32_t)c << 16)
                     | ((uint32_t)c <<  8) |  (uint32_t)c;
        volatile uint32_t *row = fb32 + y * (SCREEN_W / 4);
        for (int x = 0; x < SCREEN_W / 4; x++) row[x] = c32;
    }

    /* Drop-ceiling vertical grid lines. For each ceiling row, compute the
     * world XY at the leftmost and rightmost screen columns, then linearly
     * interpolate to find any integer world-X or world-Y boundaries this
     * row crosses. Mark those pixels darker. The result is a perspective-
     * correct grid that slides with player motion and converges to the
     * vanishing point — composes with the existing distance-ring darkening
     * to give a true cross-grid (square panels close, perspective-squashed
     * toward horizon). Cost ~1ms per frame. */
    {
        fx_t leftDirX  = dirX - planeX;
        fx_t leftDirY  = dirY - planeY;
        fx_t rightDirX = dirX + planeX;
        fx_t rightDirY = dirY + planeY;
        int mid = SCREEN_H / 2;
        for (int y = 0; y < mid; y++) {
            int p = mid - y;
            fx_t rowDist = ((fx_t)mid << FX_SHIFT) / p;
            fx_t wxL = player.x + FX_MUL(rowDist, leftDirX);
            fx_t wxR = player.x + FX_MUL(rowDist, rightDirX);
            fx_t wyL = player.y + FX_MUL(rowDist, leftDirY);
            fx_t wyR = player.y + FX_MUL(rowDist, rightDirY);

            /* Grid line color: ceiling row color, darkened by 3 more shades. */
            int shade = row_color[y] - CEIL_BASE + 3;
            if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
            uint8_t grid_c = CEIL_BASE + shade;

            /* World-X integer crossings. */
            fx_t dX = wxR - wxL;
            if (dX != 0) {
                int lo = FX_INT(wxL), hi = FX_INT(wxR);
                if (lo > hi) { int t = lo; lo = hi; hi = t; }
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wxL;
                    fx_t t   = FX_DIV(num, dX);
                    if (t < 0 || t >= FX_ONE) continue;
                    int col = (int)(((int32_t)t * SCREEN_W) >> FX_SHIFT);
                    if (col >= 0 && col < SCREEN_W) {
                        ((volatile uint8_t *)fb)[y * SCREEN_W + col] = grid_c;
                    }
                }
            }
            /* World-Y integer crossings. */
            fx_t dY = wyR - wyL;
            if (dY != 0) {
                int lo = FX_INT(wyL), hi = FX_INT(wyR);
                if (lo > hi) { int t = lo; lo = hi; hi = t; }
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wyL;
                    fx_t t   = FX_DIV(num, dY);
                    if (t < 0 || t >= FX_ONE) continue;
                    int col = (int)(((int32_t)t * SCREEN_W) >> FX_SHIFT);
                    if (col >= 0 && col < SCREEN_W) {
                        ((volatile uint8_t *)fb)[y * SCREEN_W + col] = grid_c;
                    }
                }
            }
        }
    }

    for (int col = 0; col < SCREEN_W; col++) {
        /* Default z-buffer to "infinity" so missed-ray columns let lights
         * render. Overwritten on a wall hit. */
        wall_dist[col] = 0x7FFFFFFF;
        /* cameraX in [-1, +1) */
        fx_t cameraX = ((fx_t)col << (FX_SHIFT + 1)) / SCREEN_W - FX_ONE;
        fx_t rayDirX = dirX + FX_MUL(planeX, cameraX);
        fx_t rayDirY = dirY + FX_MUL(planeY, cameraX);

        int mapX = FX_INT(player.x);
        int mapY = FX_INT(player.y);

        /* Distance the ray covers per one-unit step on each axis. */
        fx_t deltaDistX = (rayDirX == 0) ? 0x7FFFFFFF
                                         : FX_DIV(FX_ONE, FX_ABS(rayDirX));
        fx_t deltaDistY = (rayDirY == 0) ? 0x7FFFFFFF
                                         : FX_DIV(FX_ONE, FX_ABS(rayDirY));

        int stepX, stepY;
        fx_t sideDistX, sideDistY;
        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = FX_MUL(player.x - ((fx_t)mapX << FX_SHIFT), deltaDistX);
        } else {
            stepX = 1;
            sideDistX = FX_MUL(((fx_t)(mapX + 1) << FX_SHIFT) - player.x, deltaDistX);
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = FX_MUL(player.y - ((fx_t)mapY << FX_SHIFT), deltaDistY);
        } else {
            stepY = 1;
            sideDistY = FX_MUL(((fx_t)(mapY + 1) << FX_SHIFT) - player.y, deltaDistY);
        }

        /* DDA loop. Safety cap of 64 to keep ROM-side bugs from hanging. */
        int side = 0;
        int hit = 0;
        for (int i = 0; i < 64 && !hit; i++) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            if (mapX < 0 || mapX >= MAP_W || mapY < 0 || mapY >= MAP_H) break;
            if (world_map[mapY][mapX]) hit = 1;
        }
        if (!hit) continue;

        fx_t perpDist = (side == 0) ? (sideDistX - deltaDistX)
                                    : (sideDistY - deltaDistY);
        if (perpDist < FX(0.1)) perpDist = FX(0.1);
        wall_dist[col] = perpDist;
        /* Fog cutoff: wall is beyond view distance — skip the draw entirely,
         * the column already has the floor/ceiling haze from the row clear. */
        if (perpDist >= MAX_VIEW_DIST) continue;

        /* Projected line height in pixels = SCREEN_H / perpDist.
         * Values fit in int32 — drop the int64 to avoid soft-emul divide. */
        int lineHeight = (int)((SCREEN_H << FX_SHIFT) / perpDist);
        int wall_top  = SCREEN_H / 2 - lineHeight / 2;   /* may be < 0 */
        int wall_bot  = SCREEN_H / 2 + lineHeight / 2;   /* may be >= H */
        int drawStart = wall_top < 0 ? 0 : wall_top;
        int drawEnd   = wall_bot >= SCREEN_H ? SCREEN_H - 1 : wall_bot;

        /* Shade scaled so walls reach max darkness at MAX_VIEW_DIST. Combined
         * with the cutoff above, walls fade smoothly into the haze rather
         * than popping out at the boundary. */
        int wall_shade = (FX_INT(perpDist) * (SHADE_LEVELS - 1))
                       / MAX_VIEW_DIST_INT;
        if (side == 1) wall_shade += 1;
        if (wall_shade < 0) wall_shade = 0;

        /* Where on the wall did the ray hit, fractional [0,1). Multiply
         * by TEX_W * WALL_TILE_X so the texture repeats WALL_TILE_X times
         * across each map cell, then mask to [0, TEX_W). */
        fx_t wall_hit = (side == 0)
            ? (player.y + FX_MUL(perpDist, rayDirY))
            : (player.x + FX_MUL(perpDist, rayDirX));
        wall_hit -= (fx_t)FX_INT(wall_hit) << FX_SHIFT;
        int texX = (int)(((int64_t)wall_hit * (TEX_W * WALL_TILE_X)) >> FX_SHIFT)
                   & TEX_W_MASK;

        /* Texture Y steps so the tile repeats WALL_TILE_Y times across the
         * full wall height. Mask in the loop wraps each repeat. */
        fx_t tex_step = ((fx_t)(TEX_H * WALL_TILE_Y) << FX_SHIFT) / lineHeight;
        fx_t tex_pos  = (fx_t)(drawStart - wall_top) * tex_step;

        /* Stride pointer — avoids a multiply (y * SCREEN_W) per pixel. */
        volatile uint8_t *p = fb + col + drawStart * SCREEN_W;
        for (int y = drawStart; y <= drawEnd; y++) {
            int texY = (tex_pos >> FX_SHIFT) & TEX_H_MASK;
            int s = wall_shade + wall_tex[texY][texX];
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            *p = WALL_BASE + s;
            p += SCREEN_W;
            tex_pos += tex_step;
        }
    }

    draw_lights(fb, dirX, dirY, planeX, planeY);
}
