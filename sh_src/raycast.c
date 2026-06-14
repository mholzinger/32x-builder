#include "mars.h"
#include "raycast.h"
#include "sin_table.h"

/* Player starts in the middle of the room, facing east. */
player_t player = {
    .x = FX(3.5),
    .y = FX(3.5),
    .angle = 0,
};

/* 8x8 grid: 1 = wall, 0 = floor. */
const uint8_t world_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1},
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
#define SHADE_LEVELS 16

static void build_palette(void) {
    /* Color 0 = bright magenta so we can spot anywhere the renderer
     * left the framebuffer untouched (a debug aid, removed later). */
    Hw32xSetBGColor(0, 31, 0, 31);
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
}

/* Byte pointer to the start of pixel data in the current back framebuffer.
 * (32X 8bpp layout: 0x100 words of line table, then pixels at byte offset 0x200.)
 * volatile is required — the VDP observes these writes, the compiler doesn't. */
static inline volatile uint8_t *fb_pixels(void) {
    return (volatile uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
}

static void vline(int x, int top, int bot, uint8_t color) {
    volatile uint8_t *p = fb_pixels() + x;
    for (int y = top; y <= bot; y++) {
        p[y * SCREEN_W] = color;
    }
}

void raycast_init(void) {
    build_palette();
}

void raycast_render(void) {
    volatile uint8_t *fb = fb_pixels();

    /* Flat ceiling + flat floor as the base layer. We'll add per-row
     * shading once the renderer is proven correct.
     * Using a mid-bright shade (index 4) so they read clearly on hardware. */
    uint8_t ceil_color  = CEIL_BASE  + 4;
    uint8_t floor_color = FLOOR_BASE + 4;
    for (int y = 0; y < SCREEN_H / 2; y++) {
        for (int x = 0; x < SCREEN_W; x++) fb[y * SCREEN_W + x] = ceil_color;
    }
    for (int y = SCREEN_H / 2; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) fb[y * SCREEN_W + x] = floor_color;
    }

    /* Camera basis: forward = (cos a, sin a); camera plane perpendicular,
     * length 0.66 -> ~66° horizontal FOV. */
    fx_t dirX   = COS_FX(player.angle);
    fx_t dirY   = SIN_FX(player.angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    for (int col = 0; col < SCREEN_W; col++) {
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

        /* Projected line height in pixels = SCREEN_H / perpDist. */
        int lineHeight = (int)(((int64_t)SCREEN_H << FX_SHIFT) / perpDist);
        int drawStart = SCREEN_H / 2 - lineHeight / 2;
        int drawEnd   = SCREEN_H / 2 + lineHeight / 2;
        if (drawStart < 0) drawStart = 0;
        if (drawEnd >= SCREEN_H) drawEnd = SCREEN_H - 1;

        int shade = FX_INT(perpDist);
        if (side == 1) shade += 1;        /* horizontal hits dimmer */
        if (shade < 0) shade = 0;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;

        vline(col, drawStart, drawEnd, WALL_BASE + shade);
    }
}
