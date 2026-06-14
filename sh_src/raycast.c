#include "mars.h"
#include "raycast.h"
#include "sin_table.h"

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
#define SPRITE_BASE  49     /* 49..64 = furniture gray, 16 distance shades */
#define SHADE_LEVELS 16

/* Procedural wallpaper texture size — no bitmap, the pattern is derived
 * from (texX, texY) coordinates inline in the wall loop. */
#define TEX_W 32
#define TEX_H 32

/* Sprite texture is a 16x16 procedural silhouette. Same shading model as
 * walls (distance shade clamped to SHADE_LEVELS). */
#define SPRITE_TEX_W 16
#define SPRITE_TEX_H 16

typedef struct {
    fx_t x, y;
    uint8_t type;   /* reserved for future sprite types */
} sprite_t;

#define SPRITE_TYPE_CHAIR 0

static const sprite_t sprites[] = {
    { FX(4.5),  FX(7.5),  SPRITE_TYPE_CHAIR },   /* spine, just east of spawn */
    { FX(11.5), FX(8.5),  SPRITE_TYPE_CHAIR },   /* spine, far east */
    { FX(2.5),  FX(12.5), SPRITE_TYPE_CHAIR },   /* SW alcove */
    { FX(7.5),  FX(3.5),  SPRITE_TYPE_CHAIR },   /* N alcove near center */
    { FX(13.5), FX(5.5),  SPRITE_TYPE_CHAIR },   /* NE alcove */
};
#define NUM_SPRITES (int)(sizeof(sprites) / sizeof(sprites[0]))

/* Per-column z-buffer captured during wall draw, then consumed by sprite
 * draw for occlusion. Holds perpendicular distance in FX. */
static fx_t wall_dist[SCREEN_W];

/* Sort indices for back-to-front sprite render. */
static int    sprite_order[NUM_SPRITES];
static int32_t sprite_dist[NUM_SPRITES];

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
}

static void build_palette(void) {
    Hw32xSetBGColor(0, 0, 0, 0);
    /* Wall: vivid Backrooms mustard. Needs big R-B gap to read as YELLOW
     * (not drab tan) after distance shading + 5-bit quantization. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        int s = SHADE_LEVELS - i;
        Hw32xSetBGColor(WALL_BASE + i,
                        30 * s / SHADE_LEVELS,
                        25 * s / SHADE_LEVELS,
                        6  * s / SHADE_LEVELS);
    }
    /* Carpet: warm brown */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        int s = SHADE_LEVELS - i;
        Hw32xSetBGColor(FLOOR_BASE + i,
                        14 * s / SHADE_LEVELS,
                        7  * s / SHADE_LEVELS,
                        2  * s / SHADE_LEVELS);
    }
    /* Ceiling: neutral light gray-white (balanced RGB) so it doesn't
     * cross-read as yellow next to the wall. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        int s = SHADE_LEVELS - i;
        int v = 26 * s / SHADE_LEVELS;
        Hw32xSetBGColor(CEIL_BASE + i, v, v, v);
    }
    /* Furniture: medium gray with slight warm cast so chairs sit a touch
     * cooler than the yellow walls and don't disappear into the carpet. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        int s = SHADE_LEVELS - i;
        Hw32xSetBGColor(SPRITE_BASE + i,
                        14 * s / SHADE_LEVELS,
                        13 * s / SHADE_LEVELS,
                        12 * s / SHADE_LEVELS);
    }
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

/* 16x16 folding chair silhouette. Returns non-zero where opaque. */
static int chair_pattern(int x, int y) {
    if (y <= 1)              return (x >= 4 && x <= 11);   /* back top bar */
    if (y >= 2  && y <= 6)   return (x == 4 || x == 11);   /* back uprights */
    if (y >= 7  && y <= 8)   return (x >= 2 && x <= 13);   /* seat */
    if (y >= 9  && y <= 14)  return (x == 4 || x == 11);   /* legs */
    return 0;
}

/* Compute squared distance for sort ordering (monotonic; saves a sqrt). */
static int32_t sprite_sqdist(int i) {
    fx_t dx = sprites[i].x - player.x;
    fx_t dy = sprites[i].y - player.y;
    /* Each term up to ~ (16<<16)^2 / 65536 = 16*16*65536 = ~16M; fits int32. */
    return (int32_t)(FX_MUL(dx, dx) + FX_MUL(dy, dy));
}

/* Sort sprite_order indices so farthest is first (painter's algorithm). */
static void sort_sprites_back_to_front(void) {
    for (int i = 0; i < NUM_SPRITES; i++) {
        sprite_order[i] = i;
        sprite_dist[i] = sprite_sqdist(i);
    }
    /* Bubble sort by sprite_dist[sprite_order[i]] descending. */
    for (int i = 0; i < NUM_SPRITES - 1; i++) {
        for (int j = 0; j < NUM_SPRITES - i - 1; j++) {
            if (sprite_dist[sprite_order[j]] < sprite_dist[sprite_order[j + 1]]) {
                int tmp = sprite_order[j];
                sprite_order[j] = sprite_order[j + 1];
                sprite_order[j + 1] = tmp;
            }
        }
    }
}

static void draw_sprites(fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY) {
    /* inv_det = 1 / (planeX*dirY - dirX*planeY). For our 66° FOV with
     * orthogonal basis this is ~ -1.515. Avoid div-by-zero defensively. */
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = FX_DIV(FX_ONE, det);

    sort_sprites_back_to_front();
    volatile uint8_t *fb = fb_pixels();

    for (int idx = 0; idx < NUM_SPRITES; idx++) {
        int si = sprite_order[idx];
        fx_t sx = sprites[si].x - player.x;
        fx_t sy = sprites[si].y - player.y;

        /* Camera-space transform.
         *   transformX = inv_det * ( dirY * sx - dirX  * sy)
         *   transformY = inv_det * (-planeY * sx + planeX * sy)
         * transformY is depth (must be > 0 to be in front of camera). */
        fx_t transformX = FX_MUL(inv_det,
                            FX_MUL( dirY,  sx) - FX_MUL( dirX,  sy));
        fx_t transformY = FX_MUL(inv_det,
                            FX_MUL(-planeY, sx) + FX_MUL( planeX, sy));
        if (transformY < FX(0.2)) continue;

        /* Project to screen. */
        fx_t ratio = FX_DIV(transformX, transformY);
        int spriteScreenX = (SCREEN_W >> 1)
            + (int)(((int64_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);

        int spriteHeight = (int)(((int64_t)SCREEN_H << FX_SHIFT) / transformY);
        int spriteWidth  = spriteHeight;  /* square texture */

        int drawStartY_u = SCREEN_H / 2 - spriteHeight / 2;
        int drawEndY_u   = SCREEN_H / 2 + spriteHeight / 2;
        int drawStartX_u = spriteScreenX - spriteWidth / 2;
        int drawEndX_u   = spriteScreenX + spriteWidth / 2;
        int drawStartY = drawStartY_u < 0 ? 0 : drawStartY_u;
        int drawEndY   = drawEndY_u >= SCREEN_H ? SCREEN_H - 1 : drawEndY_u;
        int drawStartX = drawStartX_u < 0 ? 0 : drawStartX_u;
        int drawEndX   = drawEndX_u >= SCREEN_W ? SCREEN_W - 1 : drawEndX_u;

        if (spriteWidth < 1 || spriteHeight < 1) continue;

        int shade = FX_INT(transformY);
        if (shade < 0) shade = 0;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        uint8_t spriteColor = SPRITE_BASE + shade;

        for (int stripe = drawStartX; stripe <= drawEndX; stripe++) {
            /* Z-buffer: skip column if a wall is in front of this sprite. */
            if (transformY >= wall_dist[stripe]) continue;

            int texX = ((stripe - drawStartX_u) * SPRITE_TEX_W) / spriteWidth;
            if (texX < 0 || texX >= SPRITE_TEX_W) continue;

            for (int y = drawStartY; y <= drawEndY; y++) {
                int texY = ((y - drawStartY_u) * SPRITE_TEX_H) / spriteHeight;
                if (texY < 0 || texY >= SPRITE_TEX_H) continue;
                if (chair_pattern(texX, texY)) {
                    fb[y * SCREEN_W + stripe] = spriteColor;
                }
            }
        }
    }
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

void raycast_render(void) {
    volatile uint8_t *fb = fb_pixels();

    /* Base layer: per-row floor/ceiling with distance shading. Use 32-bit
     * stores (4 pixels per write) to fit a full clear under the SH-2's
     * frame budget. Framebuffer is 4-byte aligned; rows are 320 bytes. */
    volatile uint32_t *fb32 = (volatile uint32_t *)fb;
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t  c   = row_color[y];
        uint32_t c32 = ((uint32_t)c << 24) | ((uint32_t)c << 16)
                     | ((uint32_t)c <<  8) |  (uint32_t)c;
        volatile uint32_t *row = fb32 + y * (SCREEN_W / 4);
        for (int x = 0; x < SCREEN_W / 4; x++) row[x] = c32;
    }

    /* Camera basis: forward = (cos a, sin a); camera plane perpendicular,
     * length 0.66 -> ~66° horizontal FOV. */
    fx_t dirX   = COS_FX(player.angle);
    fx_t dirY   = SIN_FX(player.angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    for (int col = 0; col < SCREEN_W; col++) {
        /* Default z-buffer to "infinity" so columns where the ray misses
         * still let sprites render in front. Overwritten on a wall hit. */
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
        wall_dist[col] = perpDist;  /* z-buffer for sprite occlusion */

        /* Projected line height in pixels = SCREEN_H / perpDist. */
        int lineHeight = (int)(((int64_t)SCREEN_H << FX_SHIFT) / perpDist);
        int wall_top  = SCREEN_H / 2 - lineHeight / 2;   /* may be < 0 */
        int wall_bot  = SCREEN_H / 2 + lineHeight / 2;   /* may be >= H */
        int drawStart = wall_top < 0 ? 0 : wall_top;
        int drawEnd   = wall_bot >= SCREEN_H ? SCREEN_H - 1 : wall_bot;

        int wall_shade = FX_INT(perpDist);
        if (side == 1) wall_shade += 1;
        if (wall_shade < 0) wall_shade = 0;

        /* Where on the wall did the ray hit, fractional [0,1). */
        fx_t wall_hit = (side == 0)
            ? (player.y + FX_MUL(perpDist, rayDirY))
            : (player.x + FX_MUL(perpDist, rayDirX));
        wall_hit -= (fx_t)FX_INT(wall_hit) << FX_SHIFT;
        int texX = (int)(((int64_t)wall_hit * TEX_W) >> FX_SHIFT);
        if (texX < 0) texX = 0;
        if (texX >= TEX_W) texX = TEX_W - 1;

        /* Wallpaper "panel" seam every 8 texture columns -> +2 shade. */
        int col_pattern = ((texX & 7) == 0) ? 2 : 0;

        /* Texture Y walks from 0 at wall_top to TEX_H at wall_bot,
         * sampled per screen pixel. Compute initial tex_pos in FX so the
         * inner loop is one add + one shift. */
        fx_t tex_step = ((fx_t)TEX_H << FX_SHIFT) / lineHeight;
        fx_t tex_pos  = (fx_t)(drawStart - wall_top) * tex_step;

        volatile uint8_t *p = fb + col;
        for (int y = drawStart; y <= drawEnd; y++) {
            int texY = tex_pos >> FX_SHIFT;
            int pattern = col_pattern;
            /* Top molding and baseboard: 2 rows at each end of the wall. */
            if (texY < 2 || texY >= TEX_H - 2) pattern += 3;
            int s = wall_shade + pattern;
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            p[y * SCREEN_W] = WALL_BASE + s;
            tex_pos += tex_step;
        }
    }

    draw_sprites(dirX, dirY, planeX, planeY);
}
