#include "procgen.h"
#include "raycast.h"

/* xorshift32 — 10-line PRNG, fast and deterministic. State must be
 * non-zero (we kick it to 1 if a 0 seed comes in). */
static uint32_t prng_state = 1;
static uint32_t xs32(void) {
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}

/* Quadrant templates — 16×16 cell grids. 0 = walkable, 1 = wall.
 * Hand-authored so each template captures a distinct Backrooms
 * "zone" feel. Two templates ship initially; a 4-quadrant map gives
 * 2⁴ = 16 distinct layouts via permutation. */

/* OFFICE_CUBICLES — small partition-bounded rooms with irregular
 * doorways, like the lobby of a deserted floor plan. */
static const uint8_t template_office[16][16] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,0},
    {0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,0},
    {0,1,0,0,0,0,0,1,0,1,0,0,0,1,1,0},
    {0,1,1,1,0,1,0,1,0,1,1,1,0,0,0,0},
    {0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0},
    {0,1,1,1,0,1,1,1,0,1,1,0,0,0,1,0},
    {0,1,0,1,0,0,0,0,0,1,1,0,0,1,1,0},
    {0,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0},
    {0,1,1,1,0,1,0,1,0,1,1,1,0,1,1,0},
    {0,0,0,0,0,1,0,1,0,1,0,1,0,1,0,0},
    {0,1,1,1,0,1,0,0,0,1,0,0,0,0,0,0},
    {0,1,0,1,0,1,1,1,0,1,1,1,0,1,1,0},
    {0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0},
    {0,1,1,1,0,1,1,1,0,1,1,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* PILLAR_LOUNGE — mostly open space with scattered pillars and a few
 * stub walls. The "where am I" Backrooms vibe. */
static const uint8_t template_lounge[16][16] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0},
    {0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0},
    {0,1,0,0,0,1,1,0,0,0,0,0,0,1,0,0},
    {0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0},
    {0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,0},
    {0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0},
    {0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* LONG_HALLWAY — wide open corridors broken up by partial partition
 * walls. The "endless office floor with the cubicles knocked down"
 * vibe; very open, lots of sightlines. */
static const uint8_t template_hallway[16][16] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,0,1,1,1,1,0,1,1,1,1,0,1,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,0,1,1,1,0,1,1,1,0,1,1,0,1,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* DEAD_END_WARREN — dense grid of small rooms with one or two
 * doorways each. Easy to get lost in; reads as a cramped office
 * floor plan that someone added partitions to twice over. */
static const uint8_t template_warren[16][16] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,0},
    {0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,0},
    {0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0},
    {0,1,1,1,0,1,0,1,1,1,0,1,0,1,0,0},
    {0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0},
    {0,1,0,1,1,1,1,1,0,1,1,1,1,1,0,0},
    {0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0},
    {0,1,1,1,1,1,0,1,1,1,1,1,0,1,0,0},
    {0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,0},
    {0,1,0,1,0,1,1,1,1,1,0,1,0,1,0,0},
    {0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,0},
    {0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,0},
    {0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0},
    {0,1,1,1,0,1,1,1,1,1,1,1,0,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

static const uint8_t (*const templates[])[16] = {
    template_office,
    template_lounge,
    template_hallway,
    template_warren,
};
#define NUM_TEMPLATES 4

/* Copy a 16×16 template into the given quadrant origin (ox, oy) of
 * the world_map. */
static void stamp_template(int template_index, int ox, int oy) {
    const uint8_t (*tpl)[16] = templates[template_index];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            world_map[oy + y][ox + x] = tpl[y][x];
        }
    }
}

void procgen_run(uint32_t seed) {
    prng_state = seed ? seed : 1;
    /* Discard the first few outputs — xorshift on a "small" seed
     * (small frame counter at start press) needs a few rounds of
     * mixing before the bits look random. */
    for (int i = 0; i < 8; i++) xs32();

    /* Stamp four randomly-picked templates into the 4 quadrants. */
    stamp_template(xs32() % NUM_TEMPLATES,  0,  0);   /* NW */
    stamp_template(xs32() % NUM_TEMPLATES, 16,  0);   /* NE */
    stamp_template(xs32() % NUM_TEMPLATES,  0, 16);   /* SW */
    stamp_template(xs32() % NUM_TEMPLATES, 16, 16);   /* SE */

    /* Outer boundary wall — non-negotiable, otherwise the DDA walks
     * off the map. */
    for (int x = 0; x < MAP_W; x++) {
        world_map[0]        [x] = 1;
        world_map[MAP_H - 1][x] = 1;
    }
    for (int y = 0; y < MAP_H; y++) {
        world_map[y][0]         = 1;
        world_map[y][MAP_W - 1] = 1;
    }

    /* Carve a cross-shaped corridor system through the centre so all
     * four quadrants are reachable from spawn, regardless of which
     * templates landed where. Two cells wide on each axis — readable
     * as a corridor, not a single-tile slit. */
    int cx = MAP_W / 2;
    int cy = MAP_H / 2;
    for (int x = 1; x < MAP_W - 1; x++) {
        world_map[cy - 1][x] = 0;
        world_map[cy    ][x] = 0;
    }
    for (int y = 1; y < MAP_H - 1; y++) {
        world_map[y][cx - 1] = 0;
        world_map[y][cx    ] = 0;
    }

    /* Player spawn is at (16.5, 28.5) — ensure the cell + a small
     * vestibule around it is open. */
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int sx = 16 + dx, sy = 28 + dy;
            if (sx > 0 && sx < MAP_W - 1 && sy > 0 && sy < MAP_H - 1) {
                world_map[sy][sx] = 0;
            }
        }
    }
}
