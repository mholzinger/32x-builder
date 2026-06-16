#include "mars.h"
#include "raycast.h"
#include "shared.h"
#include "sh2_asm.h"
#include "sin_table.h"
#include "wall_tex.h"
#include "wall_tex_hi.h"
#include "neander_tex.h"
#include "neander_tex_hi.h"

/* Player spawn — south end of the col-16 spine corridor in the
 * hand-tuned 32x32 Backrooms map. Walls flank tightly at cols 15/17
 * for the iconic "infinite hallway" first frame. The corridor opens
 * north into a central band, then four distinct zones branch off:
 * NW = office cubicles, NE = nested rooms, SW = twisty maze,
 * SE = lounge with pillars. */
player_t player = {
    .x = FX(16.5),
    .y = FX(28.5),
    .angle = 192,
};

/* Hand-tuned 32x32 Backrooms map (tools/gen_backrooms_map.py).
 * Pivoted away from the movie.blend extraction because that geometry
 * was either too cramped (32x32 of an 82-wide model swallowed all the
 * doorways) or too sprawling (64x64 had doorways but felt like one
 * long corridor in any direction). This hand-tuned map captures the
 * "AI-generated procedural rooms" Backrooms feel by giving each
 * cardinal direction a distinct character:
 *
 *   NW (rows 1-8, cols 1-13):  office cubicles — 4 small rooms with
 *                              irregular doorways, Level-0 office floor.
 *   NE (rows 1-8, cols 17-30): nested rooms — three concentric boxes
 *                              ("room within a room within a room",
 *                              the iconic Backrooms doorway shot).
 *   CENTER (rows 10-15):       open band with pillar islands.
 *   SW (rows 17-30, cols 1-14): twisty maze with partial walls.
 *   SE (rows 17-30, cols 17-30): open lounge with scattered pillars
 *                                and a couple of "stub walls" that
 *                                make no logical sense (the uncanny
 *                                "why is this here" Backrooms vibe).
 *   SPAWN CORRIDOR: tight col-16 N-S corridor from row 17 to 28,
 *                   side-doors at (15,20), (17,23), (15,26). */
uint8_t world_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,1,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0,0,1,0,0,1,1,1,1,1,1,0,0,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,1,1,1,0,1,1,0,0,1,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,1,1,1,1,1,1,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,0,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,0,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,1,0,0,1,1,1,1,1,0,1,0,1,0,1,0,0,0,0,1,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,0,0,1,1,1,1,1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1},
    {1,0,0,0,1,1,1,1,1,1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,1,0,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,1,0,1,1,1,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,1,1,1,1,0,1,0,1,1,1,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
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
#define NEANDER_BASE 64     /* 8 entries: 0=cardboard back, 1-7=figure shades */
#define SHADE_LEVELS 16

/* Wall texture comes from wall_tex.h (generated from images/walltile.jpg). */
#define TEX_W WALL_TEX_WIDTH
#define TEX_H WALL_TEX_HEIGHT

/* Liminal fog. DDA walks rays out to MAX_VIEW_DIST then bails — walls
 * beyond that simply aren't hit. The shade ramp uses the shorter
 * FOG_RAMP_DIST so walls reach full-fog shade by ~6 cells; anything
 * the DDA finds between FOG_RAMP_DIST and MAX_VIEW_DIST renders at
 * shade 15 (indistinguishable from the floor/ceiling fog), giving the
 * Backrooms "emerges from the greenish darkness" effect rather than a
 * hard pop-in at the cutoff. */
#define FOG_RAMP_DIST     FX(6)
#define MAX_VIEW_DIST     FX(10)
#define MAX_VIEW_DIST_INT 10

/* Drop-ceiling grid density — number of panel boundaries per 1-unit map
 * cell. Higher = denser grid. The cost is identical at any density; we
 * just scale world coordinates by this factor before integer-crossing
 * detection so a boundary at every (1/CEIL_GRID_DENSITY) units triggers. */
#define CEIL_GRID_DENSITY 4

/* Wallpaper chevron pattern strength at close range, 0-16. 8 caps
 * the chevron-vs-base shade gap at 2 levels (out of 16) — subtle
 * enough that the wall reads as yellow first, motif second, matching
 * how an actual Backrooms wallpaper sits against the yellow base. */
#define WALL_PATTERN_MAX 8

/* Per-column z-buffer captured during wall draw so the light billboards
 * can z-test against walls. 0x7FFFFFFF = no wall hit (light wins).
 *
 * Both CPUs write to this array (master cols 0-159, slave 160-319) and
 * the master reads it back for the sprite z-test. ALL accesses must
 * go through the WALL_DIST() macro below, which routes them via the
 * | 0x20000000 cache-through alias so neither CPU sees stale cached
 * values written by the other. */
static fx_t wall_dist[SCREEN_W];
#define WALL_DIST(i) (((volatile fx_t *)((uintptr_t)wall_dist | 0x20000000))[i])

/* Per-frame list of visible partition face segments, populated by
 * partition_build_faces() once per frame on master and consumed by
 * both CPUs in raycast_draw_walls via per-ray ray-segment intersection.
 * Each face is a 2D line segment in world space; ua/ub are the texture-U
 * world coordinates at the two endpoints. Cache-through aliased so the
 * slave's reads are coherent with master's per-frame writes.
 *
 * Per-ray (not per-segment-projection) means each column independently
 * tests against each visible face and takes the closest hit — exactly
 * like the cell-wall DDA. No linear inv_z interpolation between
 * endpoints, so no "wedge" artifact at glancing corners. Costs ~1.5ms
 * per frame at 320 cols × 4 faces × ~20 cycles + saturated divides. */
/* NUM_PARTITIONS_MAX declared in raycast.h so procgen sees the same cap. */
#define MAX_PARTITION_FACES (NUM_PARTITIONS_MAX * 4)
static fx_t pface_ax[MAX_PARTITION_FACES];
static fx_t pface_ay[MAX_PARTITION_FACES];
static fx_t pface_bx[MAX_PARTITION_FACES];
static fx_t pface_by[MAX_PARTITION_FACES];
static fx_t pface_ua[MAX_PARTITION_FACES];
static fx_t pface_ub[MAX_PARTITION_FACES];
static int  pface_count = 0;
#define PFACE_AX(i)    (((volatile fx_t *)((uintptr_t)pface_ax | 0x20000000))[i])
#define PFACE_AY(i)    (((volatile fx_t *)((uintptr_t)pface_ay | 0x20000000))[i])
#define PFACE_BX(i)    (((volatile fx_t *)((uintptr_t)pface_bx | 0x20000000))[i])
#define PFACE_BY(i)    (((volatile fx_t *)((uintptr_t)pface_by | 0x20000000))[i])
#define PFACE_UA(i)    (((volatile fx_t *)((uintptr_t)pface_ua | 0x20000000))[i])
#define PFACE_UB(i)    (((volatile fx_t *)((uintptr_t)pface_ub | 0x20000000))[i])
#define PFACE_COUNT    (*(volatile int *)((uintptr_t)&pface_count | 0x20000000))

/* Saturated fixed-point divide: same as FX_DIV but clamps to ±INT32_MAX
 * instead of silently wrapping on overflow. Used in the per-ray ray-
 * segment intersection where denominators can be very small at glancing
 * angles (the same trap that originally pushed us to projection-based
 * partition rendering). */
static inline fx_t fx_div_sat(fx_t a, fx_t b) {
    if (b == 0) return 0;
    int64_t result = ((int64_t)a << FX_SHIFT) / b;
    if (result > (int64_t)0x7FFFFFFFLL)         return (fx_t)0x7FFFFFFF;
    if (result < (int64_t)0xFFFFFFFF80000000LL) return (fx_t)0x80000000;
    return (fx_t)result;
}

/* Floor-standing cardboard cutouts. Each standup has a world position
 * and a facing direction. silhouette=1 renders as flat dark outline only
 * (the iconic "something is watching" Backrooms vibe) and disappears when
 * the player gets too close. */
typedef struct {
    fx_t x, y;
    uint8_t facing_angle;
    uint8_t silhouette;
} standup_t;

static const standup_t standups[] = {
    /* Neanderthal ~5 cells north of spawn in the col-16 spine corridor.
     * Solid, walkable-through, the "iconic Backrooms cardboard cutout"
     * moment. Audio-positioned via the Voyager-record hello loop. */
    { FX(16.5), FX(23.5), 64,  0 },
};

/* Free-standing wallpaper partitions ("fake walls"). Defined by two
 * world-space endpoints — rendered via per-ray segment intersection
 * (see partition_build_faces and the consumer in raycast_draw_walls),
 * full ceiling-to-floor height. Mutable so procgen can populate them
 * at boot; fixed-map mode keeps the hand-authored pair below.
 * partition_t / NUM_PARTITIONS_MAX are declared in raycast.h. */
partition_t partitions[NUM_PARTITIONS_MAX] = {
    /* SE lounge — 4-cell partition along Y=22. */
    { FX(22), FX(22), FX(26), FX(22) },
    /* Central band — 3-cell partition along X=20. */
    { FX(20), FX(11), FX(20), FX(14) },
};
int num_partitions = 2;
#define NUM_PARTITIONS num_partitions
#define NUM_STANDUPS (int)(sizeof(standups) / sizeof(standups[0]))

/* Illuminated drop-ceiling panels (the Backrooms iconic recessed
 * fluorescent panels). Positions are scattered across the map rather
 * than placed at every cell — that scattered "some tiles are lit,
 * most aren't" pattern is what makes the lobby reference photo read
 * as actual drop-ceiling lighting rather than "lights everywhere". */
typedef struct { fx_t x, y; } light_t;

/* Grid of recessed fluorescent fixtures populated by init_lights() at
 * boot — one panel every 2 cells in both axes, skipping cells that
 * are inside walls. Matches the regular cadence of the Sketchfab
 * Backrooms reference where the drop ceiling holds a fixture roughly
 * every other panel run. 200 slots is enough for the densest possible
 * 32×32 walkable map (~250 cells / 4 = ~62 fixtures in practice). */
#define MAX_LIGHTS 200
static light_t lights[MAX_LIGHTS];
static int num_lights = 0;
#define NUM_LIGHTS num_lights

static void init_lights(void) {
    num_lights = 0;
    for (int my = 1; my < MAP_H - 1; my += 2) {
        for (int mx = 1; mx < MAP_W - 1; mx += 2) {
            if (world_map[my][mx] != 0) continue;
            if (num_lights >= MAX_LIGHTS) return;
            lights[num_lights].x = FX(mx) + FX(0.5);
            lights[num_lights].y = FX(my) + FX(0.5);
            num_lights++;
        }
    }
}

/* How many times the wallpaper tile repeats per 1-unit map cell.
 * TEX_W/H must be powers of 2 so the wrap can be a cheap bitmask.
 * The new source (square_walltile_composite.jpg) is a designed
 * seamless tile with 8 chevron ribbons baked into the frame — one
 * tile per cell renders 8 ribbons per wall cell with clean tile
 * seams (left edge matches right edge). Lo and hi share the same
 * tile rate; the LOD swap is purely a resolution upgrade per
 * source repeat. */
#define WALL_TILE_X        4
#define WALL_TILE_Y        4
#define WALL_TILE_HI_X     4
#define WALL_TILE_HI_Y     4
#define WALL_LOD_THRESHOLD FX(2)
#define TEX_W_MASK  (TEX_W - 1)
#define TEX_H_MASK  (TEX_H - 1)


/* Precomputed pixel color per screen row for the base floor/ceiling layer.
 * Indexes [0..SCREEN_H/2-1] = ceiling, bright at top dim toward horizon;
 * [SCREEN_H/2..SCREEN_H-1] = floor, dim near horizon bright at bottom. */
static uint8_t row_color[SCREEN_H];

/* Precomputed cameraX value per screen column. Replaces a per-column divide
 * (one of the few remaining ones in the wall loop) with a single table load. */
static fx_t cameraX_table[SCREEN_W];

static void build_shading_tables(void) {
    int mid = SCREEN_H / 2;
    for (int y = 0; y < SCREEN_H; y++) {
        int yy;
        if (y < mid)      yy = mid - y;
        else if (y > mid) yy = y - mid;
        else              yy = 1;          /* avoid div-by-zero at horizon */
        /* Compressed perspective shade ramp. True mid/yy keeps the
         * close ceiling/floor at shade 0 across most of the screen
         * and only ramps to fog in the last ~10 rows at the horizon.
         * Multiplying by 3/2 makes the fade visible across the full
         * vertical extent — matches the cadence of the wall_shade
         * ramp which hits full fog at perpDist 6. */
        int shade = ((mid * 3) / (yy * 2)) - 1;
        if (shade < 0) shade = 0;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        row_color[y] = (y <= mid) ? (CEIL_BASE + shade)
                                  : (FLOOR_BASE + shade);
    }

    /* The pre-rendered "distance ring" ceiling shading used to live
     * here — darken every CEIL_GRID_DENSITY-th screen-distance row,
     * computed once into row_color. It produced static horizontal
     * bands tied to screen row (not world Y) that READ as a grid but
     * didn't animate. Now that raycast_draw_ceiling_grid does per-row
     * band fallback for world-Y crossings when facing cardinal, those
     * dynamic bands collide with the pre-rendered ones and show as
     * doubled horizontal lines. Pre-rendered version removed. */
}

/* Fade target — what every surface fades toward at maximum distance.
 * Pure black: misty grey was reading as "too bright" at depth, removing
 * the contrast between near and far. Black gives the classic raycaster
 * "darkness eats the corridor" look.
 *
 * Note: this isn't a perf knob — it's just palette base values at init.
 * Changing these has zero runtime cost. */
/* Mid-grey fog. Equal RGB so the far distance reads as neutral rather
 * than tinted any direction. RGB(8,8,8) ≈ 26% brightness — distinctly
 * "in the haze" but still a clearly visible surface rather than a hole
 * punched in the world. Distance shade 15 lands at ≈ RGB(9, 9, 8). */
#define FOG_R 8
#define FOG_G 8
#define FOG_B 8

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
                        MIX(25, FOG_R, i),
                        MIX(23, FOG_G, i),
                        MIX(16, FOG_B, i));
    }
    /* Fluorescent lights: 4 brightness states for flicker (full / 75 / 50 / 25%). */
    Hw32xSetBGColor(LIGHT_BASE + 0, 31, 31, 28);
    Hw32xSetBGColor(LIGHT_BASE + 1, 23, 23, 21);
    Hw32xSetBGColor(LIGHT_BASE + 2, 15, 15, 14);
    Hw32xSetBGColor(LIGHT_BASE + 3,  7,  7,  7);
    /* Neanderthal cardboard standup. Index 0 = cardboard back (warm tan
     * brown). 1-7 = quantized figure shades pulled from the 32x64 PNG
     * texture by 7-bucket brightness quantization. */
    Hw32xSetBGColor(NEANDER_BASE + 0, 16, 11,  5);
    Hw32xSetBGColor(NEANDER_BASE + 1,  2,  2,  1);
    Hw32xSetBGColor(NEANDER_BASE + 2,  7,  5,  3);
    Hw32xSetBGColor(NEANDER_BASE + 3, 11,  8,  6);
    Hw32xSetBGColor(NEANDER_BASE + 4, 16, 12,  9);
    Hw32xSetBGColor(NEANDER_BASE + 5, 19, 16, 13);
    Hw32xSetBGColor(NEANDER_BASE + 6, 23, 20, 17);
    Hw32xSetBGColor(NEANDER_BASE + 7, 26, 22, 19);
}

/* Byte pointer to the start of pixel data in the current back framebuffer.
 * (32X 8bpp layout: 0x100 words of line table, then pixels at byte offset 0x200.)
 * Non-volatile: the 32X framebuffer at 0x24000000 isn't SH-2 cached, so
 * writes go through directly. A single `asm("" ::: "memory")` barrier at
 * end-of-render commits any reordered stores before the VDP sees them. */
static inline uint8_t *fb_pixels(void) {
    return (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
}

void raycast_init(void) {
    build_palette();
    build_shading_tables();
    init_lights();
    /* Precompute cameraX[col] = 2*col/SCREEN_W - 1 in FX. */
    for (int col = 0; col < SCREEN_W; col++) {
        cameraX_table[col] = ((fx_t)col << (FX_SHIFT + 1)) / SCREEN_W - FX_ONE;
    }
}

/* Per-frame palette nudge on the brightest wall and ceiling entries —
 * mimics a dying fluorescent's flicker. Must be called from inside
 * vblank (after the COMM12 tick wait) to avoid mid-frame CRAM tearing. */
void raycast_shimmer(void) {
    /* Gated by LIGHTING_SHIMMER. When off, leave CRAM at the original
     * build_palette values — the WALL_BASE/CEIL_BASE entries stop
     * pulsing each frame. */
    if (!(SHARED_UC->lighting_flags & LIGHTING_SHIMMER)) {
        Hw32xSetBGColor(WALL_BASE, 30, 25, 6);
        Hw32xSetBGColor(CEIL_BASE, 26, 26, 26);
        return;
    }
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

/* Player's body radius in world cells — used by both wall and partition
 * collision so the camera maintains a small visible gap from any
 * surface. 0.25 = 25 cm ≈ 10". Larger feels sluggish, smaller lets
 * the camera press up against a wall and break the immersion. */
#define PLAYER_RADIUS FX(0.25)

/* Returns 1 if (px, py) world position would intersect any partition's
 * thickened axis-aligned bounding box (rendered thickness + player
 * radius), 0 otherwise. */
static int partition_collides(fx_t px, fx_t py) {
    const fx_t PARTITION_HALF_THICK = FX(0.15);   /* matches HALF_THICK in partition_project_all */
    fx_t margin = PARTITION_HALF_THICK + PLAYER_RADIUS;
    for (int i = 0; i < NUM_PARTITIONS; i++) {
        fx_t x1 = partitions[i].x1, x2 = partitions[i].x2;
        fx_t y1 = partitions[i].y1, y2 = partitions[i].y2;
        if (x1 > x2) { fx_t t = x1; x1 = x2; x2 = t; }
        if (y1 > y2) { fx_t t = y1; y1 = y2; y2 = t; }
        if (px >= x1 - margin && px <= x2 + margin &&
            py >= y1 - margin && py <= y2 + margin) {
            return 1;
        }
    }
    return 0;
}

static int position_clear(fx_t px, fx_t py) {
    /* Check all 4 corners of the player's bounding box against wall
     * cells. The 4-CARDINAL check (N/S/E/W edges) missed diagonal
     * corner clips — when player approaches an isolated 1-cell pillar
     * from a diagonal, all 4 cardinal points land in walkable cells
     * while the box's corner clips the pillar. Symptom was thin
     * "phantom walls" that the player could walk through in hallways
     * with isolated pillar columns. The 4-CORNER check catches this:
     * each diagonal corner of the box is tested against the cell it
     * lands in, so any pillar touching any corner blocks the move. */
    int xL = FX_INT(px - PLAYER_RADIUS);
    int xR = FX_INT(px + PLAYER_RADIUS);
    int yT = FX_INT(py - PLAYER_RADIUS);
    int yB = FX_INT(py + PLAYER_RADIUS);
    if (!cell_passable(xL, yT)) return 0;
    if (!cell_passable(xR, yT)) return 0;
    if (!cell_passable(xL, yB)) return 0;
    if (!cell_passable(xR, yB)) return 0;
    if (partition_collides(px, py)) return 0;
    return 1;
}

/* Head-bob state. bob_phase advances when the player is moving, and
 * raycast_render applies a small perpendicular position sway derived
 * from sin(bob_phase) before computing the camera basis. Cheap (just
 * two extra muls per frame) and reads as "walking through a hallway"
 * — the single biggest immersion bump per line of code. */
static uint8_t bob_phase   = 0;
static uint8_t is_walking  = 0;
/* Eased manual pitch (signed pixels). C button drives it toward +40
 * (look down) when held, eases back to 0 on release. Walking pitch bob
 * (±1 from SIN_FX(bob_phase)) is added on top each frame. */
static int     pitch_smooth_y = 0;

/* Read controller, advance player by one frame. Axis-separated collision
 * gives natural sliding along walls. */
void player_update(void) {
    HwMdReadPad(0);
    uint16_t pad = MARS_SYS_COMM8;

    /* Hold C → look mode. D-pad UP/DOWN drive pitch in two phases:
     *   Phase 1 — ease toward the comfortable angle ±40 at 25%/frame
     *             (~11 frames / 183 ms to settle). Same exponential
     *             ramp shape as the LEFT/RIGHT pivot at walk speed
     *             (4 angle units/frame) so the gaze and pivot feel
     *             paced the same.
     *   Phase 2 — once you've reached ±40 and KEEP holding, linear
     *             ramp at 1 px/frame from ±40 out to ±80. ~40 more
     *             frames / 670 ms to fully extend. This is the
     *             "extra" the player discovers if they really lean.
     * Release direction (or C) → spring back to 0 at 25%/frame
     * (symmetric to phase 1). Forward/back walking is suspended during
     * the C hold since UP/DOWN are repurposed for pitch. Y-shear caps
     * the convincing tilt at about ±80; past that walls visibly slide
     * rather than tilt. */
    int look_mode = (pad & SEGA_CTRL_C) != 0;
    int up_held   = look_mode && (pad & SEGA_CTRL_UP)   != 0;
    int down_held = look_mode && (pad & SEGA_CTRL_DOWN) != 0;
    if (up_held && !down_held) {
        if (pitch_smooth_y > -40) {
            /* Phase 1: ease toward -40 at 25%/frame. */
            pitch_smooth_y += (-40 - pitch_smooth_y) >> 2;
        } else {
            /* Phase 2: slow linear extension past -40. */
            pitch_smooth_y -= 1;
            if (pitch_smooth_y < -80) pitch_smooth_y = -80;
        }
    } else if (down_held && !up_held) {
        if (pitch_smooth_y < 40) {
            pitch_smooth_y += (40 - pitch_smooth_y) >> 2;
        } else {
            pitch_smooth_y += 1;
            if (pitch_smooth_y > 80) pitch_smooth_y = 80;
        }
    } else {
        /* No direction (or C released): ease toward 0 at 25%/frame. */
        pitch_smooth_y += (0 - pitch_smooth_y) >> 2;
    }

    /* Hold A to run — bumps walk speed and turn rate together so you can
     * quickly reorient while sprinting. */
    int sprinting = (pad & SEGA_CTRL_A) != 0;
    fx_t walk = sprinting ? FX(0.15) : FX(0.08);
    uint8_t turn = sprinting ? 8 : 4;

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
    /* Suspend walking forward/back while C is held — UP/DOWN are
     * borrowed for look-up/look-down in look_mode (above). */
    if (!look_mode) {
    if (pad & SEGA_CTRL_UP)   { dx += FX_MUL(dirX, walk); dy += FX_MUL(dirY, walk); }
    if (pad & SEGA_CTRL_DOWN) { dx -= FX_MUL(dirX, walk); dy -= FX_MUL(dirY, walk); }
    if (strafing) {
        if (pad & SEGA_CTRL_RIGHT) { dx += FX_MUL(rightX, walk); dy += FX_MUL(rightY, walk); }
        if (pad & SEGA_CTRL_LEFT)  { dx -= FX_MUL(rightX, walk); dy -= FX_MUL(rightY, walk); }
    }
    }   /* end if (!look_mode) — UP/DOWN handled as look pitch above. */

    /* Axis-separated collision: try X first, then Y. */
    fx_t newX = player.x + dx;
    if (position_clear(newX, player.y)) player.x = newX;

    fx_t newY = player.y + dy;
    if (position_clear(player.x, newY)) player.y = newY;

    /* Track walking state and advance bob phase. */
    is_walking = (dx != 0 || dy != 0);
    if (is_walking) bob_phase += 20;         /* ~4.7 Hz — tight micro-bob cadence */
}

/* Render each cardboard standup as a textured Wolf3D-style billboard.
 * Same camera-space transform as draw_lights. Vertical centering on the
 * horizon places the figure's feet on the floor and head 1 world unit up.
 * Front/back is dot(player - standup, standup_forward) — positive = front
 * (sample texture), negative = back (cardboard fill). */
static void draw_standups(uint8_t *fb,
                          fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY) {
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = FX_DIV(FX_ONE, det);
    /* Standup feet sit on the floor → anchor floor_y to the shifted
     * horizon so they slide with the wall/carpet when the camera pitches. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;

    for (int i = 0; i < NUM_STANDUPS; i++) {
        fx_t sx = standups[i].x - player.x;
        fx_t sy = standups[i].y - player.y;

        fx_t transformX = FX_MUL(inv_det,
                            FX_MUL( dirY,  sx) - FX_MUL( dirX,  sy));
        fx_t transformY = FX_MUL(inv_det,
                            FX_MUL(-planeY, sx) + FX_MUL( planeX, sy));
        if (transformY < FX(0.5))     continue;     /* behind / too close */
        if (transformY >= MAX_VIEW_DIST) continue;  /* beyond fog */
        /* Watcher: vanishes when you get within 3 cells. Iconic Backrooms
         * "did I see something?" tell. */
        if (standups[i].silhouette && transformY < FX(3)) continue;

        fx_t ratio = FX_DIV(transformX, transformY);
        int screenX = (SCREEN_W >> 1)
                    + (int)(((int32_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);

        /* 2/3 world unit tall, 1:2 aspect. Floor-anchored: bottom (feet)
         * lands on the floor row at this distance; top (head) is
         * spriteHeight pixels above. Also benefits texture quality —
         * keeps apparent sprite from upscaling the 64-row texture too
         * far past 1:1 at close range. */
        int spriteHeight = (int)((((int32_t)SCREEN_H * 2) << FX_SHIFT) / (transformY * 3));
        int spriteWidth  = spriteHeight >> 1;
        if (spriteWidth < 1) spriteWidth = 1;
        if (spriteHeight < 1) continue;

        /* Floor row at this distance: horizon_y + (SCREEN_H/2)/transformY.
         * horizon_y shifts with pitch (camera tilt); the SCREEN_H/2 inside
         * the division is the focal constant and stays unshifted. */
        int floor_y = horizon_y
                    + (int)(((int32_t)(SCREEN_H >> 1) << FX_SHIFT) / transformY);
        int drawEndY_u   = floor_y;
        int drawStartY_u = floor_y - spriteHeight;
        int drawStartX_u = screenX - (spriteWidth >> 1);
        int drawEndX_u   = screenX + (spriteWidth >> 1);
        int drawStartY = drawStartY_u < 0 ? 0 : drawStartY_u;
        int drawEndY   = drawEndY_u >= SCREEN_H ? SCREEN_H - 1 : drawEndY_u;
        int drawStartX = drawStartX_u < 0 ? 0 : drawStartX_u;
        int drawEndX   = drawEndX_u >= SCREEN_W ? SCREEN_W - 1 : drawEndX_u;

        /* Front/back: standup forward is (cos angle, sin angle).
         * sx, sy = standup - player, so player - standup = -sx, -sy.
         * dot(player - standup, forward) > 0 means player is in front;
         * equivalently (sx*fx + sy*fy) < 0. */
        fx_t fwdX = COS_FX(standups[i].facing_angle);
        fx_t fwdY = SIN_FX(standups[i].facing_angle);
        int is_front = (FX_MUL(sx, fwdX) + FX_MUL(sy, fwdY)) < 0;
        uint8_t back_color = NEANDER_BASE + 0;
        /* Watcher silhouette: every non-transparent texture pixel becomes
         * the darkest figure shade. No front/back distinction — just a
         * flat dark outline against the wallpaper. */
        int is_silhouette = standups[i].silhouette;
        uint8_t silhouette_color = NEANDER_BASE + 1;

        /* LOD: swap to the 128x256 hi-res texture when the sprite is
         * close enough that the 32x64 lo-res starts showing block
         * artifacts. At transformY < 3, the sprite is ~75+ px tall —
         * each lo-res texel is ~2.3 screen pixels (visibly chunky).
         * Hi-res cuts that to ~0.6 (silky). Beyond distance 3 the
         * sprite is small enough that lo-res reads as crisp.
         *
         * Hi-res is stored column-major: walking down a screen column
         * walks sequential bytes in memory, so adjacent texY fetches
         * share a cache line. The lo-res 32x64 (2KB) fits entirely in
         * the SH-2 4KB cache so its row-major layout doesn't suffer
         * the same penalty. col_step expresses the row-stride: 1 for
         * column-major (sequential), tex_w for row-major (strided). */
        const uint8_t *tex;
        int tex_w, tex_h, col_step;
        if (transformY < FX(3)) {
            tex      = (const uint8_t *)neander_tex_hi;
            tex_w    = NEANDER_TEX_HI_WIDTH;
            tex_h    = NEANDER_TEX_HI_HEIGHT;
            col_step = 1;                 /* column-major */
        } else {
            tex      = (const uint8_t *)neander_tex;
            tex_w    = NEANDER_TEX_WIDTH;
            tex_h    = NEANDER_TEX_HEIGHT;
            col_step = NEANDER_TEX_WIDTH; /* row-major: next row is W bytes */
        }

        /* Precompute texY increment per screen row — same trick as wall
         * texture stepping. Was doing one divide per pixel (~30 cycles each
         * on SH-2), now one divide per sprite + add per pixel. For a sprite
         * at distance 3 (~3000 pixels) this is ~4ms saved per frame. */
        fx_t texY_step    = ((fx_t)tex_h << FX_SHIFT) / spriteHeight;
        fx_t texY_start_v = (fx_t)(drawStartY - drawStartY_u) * texY_step;

        for (int stripe = drawStartX; stripe <= drawEndX; stripe++) {
            if (transformY >= WALL_DIST(stripe)) continue;

            int texX = ((stripe - drawStartX_u) * tex_w) / spriteWidth;
            if (texX < 0 || texX >= tex_w) continue;

            /* Pointer to the start of this texture column. Column-major:
             * column texX begins at tex + texX*tex_h. Row-major: column
             * texX starts at tex + texX, and consecutive rows are tex_w
             * apart. col_step encodes which. */
            const uint8_t *col_base = (col_step == 1)
                ? (tex + texX * tex_h)
                : (tex + texX);

            uint8_t *p = fb + drawStartY * SCREEN_W + stripe;
            fx_t tex_pos = texY_start_v;
            /* texY can only overshoot the bottom of the texture (not go
             * negative — tex_pos starts >= 0 and step is positive). */
            for (int y = drawStartY; y <= drawEndY; y++) {
                int texY = tex_pos >> FX_SHIFT;
                if (texY >= tex_h) texY = tex_h - 1;
                uint8_t v = col_base[texY * col_step];
                if (v != 0) {
                    *p = is_silhouette ? silhouette_color
                       : is_front      ? (NEANDER_BASE + v)
                                       : back_color;
                }
                p += SCREEN_W;
                tex_pos += texY_step;
            }
        }
    }
}


/* Doom-style segment projection — borrowed from r_phase2.c (32X Doom
 * Resurrection / d32xr) and r_segs.c (PC Doom). Transforms a wall
 * segment's two endpoints to camera space, near-plane clips, projects
 * to a screen-X range, and walks every column in that range writing
 * (inv_z, u_over_z) into PART_INV_Z[] / PART_U_OVER_Z[] if the segment
 * is closer than any previously projected partition for this column.
 *
 * The structural cure for the gap bug: NO per-column division by a
 * ray direction. The only per-segment divides (FX_DIV by ty1/ty2 for
 * the screen-X projection, and inv_z_step/u_step) operate on values
 * bounded by the near-plane clip (ty >= FX(0.5)), so int32 fixed-point
 * never overflows.
 *
 * u1_world/u2_world are the long-axis world coordinates at the segment
 * endpoints — passing the partition's X for horizontal segments (Y for
 * vertical) gives chevron-continuous tiling around the 4 sides. */
/* Build the visible-faces list once per frame. Each partition is a
 * thin axis-aligned rectangle (~0.3 cells thick). From any vantage,
 * the player can see at most 2 of its 4 faces. We pick those and
 * write them as line segments into pface_* arrays for the per-ray
 * intersection in raycast_draw_walls to consume.
 *
 * This replaces the previous Doom-style segment-projection approach
 * (which linearly interpolated inv_z between projected endpoints and
 * produced a "wedge" artifact at glancing close-corner poses). The
 * per-ray approach mirrors how regular cell walls work — each column
 * independently finds its closest hit, no cross-column interpolation,
 * no wedge by construction.
 *
 * Runs ONCE on the master before MARS_SYS_COMM4 wakes the slave; both
 * CPUs then read the populated arrays during their draw_walls half. */
static void partition_build_faces(void) {
    const fx_t HALF_THICK = FX(0.15);    /* 0.3 cell ≈ 1' */
    int n = 0;

    for (int i = 0; i < NUM_PARTITIONS; i++) {
        fx_t px1 = partitions[i].x1, py1 = partitions[i].y1;
        fx_t px2 = partitions[i].x2, py2 = partitions[i].y2;
        fx_t pdx = px2 - px1, pdy = py2 - py1;

        /* Thick-rectangle AABB extents. */
        fx_t xmin = px1 < px2 ? px1 : px2;
        fx_t xmax = px1 > px2 ? px1 : px2;
        fx_t ymin = py1 < py2 ? py1 : py2;
        fx_t ymax = py1 > py2 ? py1 : py2;
        if (pdx == 0) { xmin -= HALF_THICK; xmax += HALF_THICK; }
        if (pdy == 0) { ymin -= HALF_THICK; ymax += HALF_THICK; }

        int show_west  = player.x < xmin;
        int show_east  = player.x > xmax;
        int show_north = player.y < ymin;
        int show_south = player.y > ymax;
        if (!(show_west || show_east || show_north || show_south)) continue;

        if (show_west && n < MAX_PARTITION_FACES) {
            /* West face: x = xmin, y from ymin..ymax. U = world Y. */
            PFACE_AX(n) = xmin; PFACE_AY(n) = ymin;
            PFACE_BX(n) = xmin; PFACE_BY(n) = ymax;
            PFACE_UA(n) = ymin; PFACE_UB(n) = ymax;
            n++;
        }
        if (show_east && n < MAX_PARTITION_FACES) {
            PFACE_AX(n) = xmax; PFACE_AY(n) = ymin;
            PFACE_BX(n) = xmax; PFACE_BY(n) = ymax;
            PFACE_UA(n) = ymin; PFACE_UB(n) = ymax;
            n++;
        }
        if (show_north && n < MAX_PARTITION_FACES) {
            /* North face: y = ymin, x from xmin..xmax. U = world X. */
            PFACE_AX(n) = xmin; PFACE_AY(n) = ymin;
            PFACE_BX(n) = xmax; PFACE_BY(n) = ymin;
            PFACE_UA(n) = xmin; PFACE_UB(n) = xmax;
            n++;
        }
        if (show_south && n < MAX_PARTITION_FACES) {
            PFACE_AX(n) = xmin; PFACE_AY(n) = ymax;
            PFACE_BX(n) = xmax; PFACE_BY(n) = ymax;
            PFACE_UA(n) = xmin; PFACE_UB(n) = xmax;
            n++;
        }
    }
    PFACE_COUNT = n;
}

/* Project each ceiling light to screen space, paint a small bright bar
 * with z-test against wall_dist, apply per-light flicker. The math is the
 * sprite-billboard transform; the cost is ~50-100 cycles per light. */
static void draw_lights(uint8_t *fb,
                        fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY) {
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = FX_DIV(FX_ONE, det);
    /* Ceiling tiles project against the shifted horizon so they stay
     * on the ceiling when the camera pitches up or down. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;

    static uint32_t light_frame = 0;
    light_frame++;

    /* Lights are now CEILING TILES — flat axis-aligned rectangles in
     * the ceiling plane at world position (lx, ly). Each tile spans
     * (lx-HALF .. lx+HALF) × (ly-HALF .. ly+HALF). We project all 4
     * corners to screen and fill the bounding rectangle, so the lit
     * area tracks the same perspective as the surrounding ceiling
     * grid lines instead of being a separate billboard sprite. */
    /* One actual ceiling panel. CEIL_GRID_DENSITY = 4 means 4 panels
     * per 1-unit cell side, so a single panel is 0.25 units wide and
     * TILE_HALF = 0.125 (half-extent in each axis). Matches the
     * "single recessed fluorescent in one drop-ceiling tile" look of
     * the Backrooms reference renders. */
    const fx_t TILE_HALF = FX(0.125);

    for (int i = 0; i < NUM_LIGHTS; i++) {
        fx_t lx = lights[i].x;
        fx_t ly = lights[i].y;

        /* Center-distance check for view culling. */
        fx_t cx = lx - player.x;
        fx_t cy = ly - player.y;
        fx_t centerY = FX_MUL(inv_det,
                              FX_MUL(-planeY, cx) + FX_MUL(planeX, cy));
        if (centerY < FX(0.5)) continue;
        if (centerY >= MAX_VIEW_DIST) continue;

        /* Project all 4 corners of the axis-aligned ceiling tile,
         * saving (sx, sy) for each so we can do proper scanline
         * trapezoid fill instead of bounding-box. The bbox lit area
         * was reading as a hovering rectangular sprite; this fill
         * tracks the same perspective as the surrounding ceiling
         * grid lines so the lit area looks like a real tile. */
        fx_t corner_dx[4] = { -TILE_HALF, +TILE_HALF, +TILE_HALF, -TILE_HALF };
        fx_t corner_dy[4] = { -TILE_HALF, -TILE_HALF, +TILE_HALF, +TILE_HALF };
        int corner_sx[4], corner_sy[4];
        int min_y = SCREEN_H, max_y = -1;
        int valid = 1;
        for (int k = 0; k < 4; k++) {
            fx_t rx = (lx + corner_dx[k]) - player.x;
            fx_t ry = (ly + corner_dy[k]) - player.y;
            fx_t tX = FX_MUL(inv_det, FX_MUL( dirY,  rx) - FX_MUL( dirX,  ry));
            fx_t tY = FX_MUL(inv_det, FX_MUL(-planeY, rx) + FX_MUL( planeX, ry));
            if (tY < FX(0.2)) { valid = 0; break; }

            fx_t ratio = FX_DIV(tX, tY);
            corner_sx[k] = (SCREEN_W >> 1)
                  + (int)(((int32_t)(SCREEN_W >> 1) * ratio) >> FX_SHIFT);
            int yoff = (int)(((int32_t)(SCREEN_H >> 1) << FX_SHIFT) / tY);
            corner_sy[k] = horizon_y - yoff;

            if (corner_sy[k] < min_y) min_y = corner_sy[k];
            if (corner_sy[k] > max_y) max_y = corner_sy[k];
        }
        if (!valid) continue;
        if (min_y < 0)         min_y = 0;
        if (max_y >= SCREEN_H) max_y = SCREEN_H - 1;
        if (max_y < min_y) continue;

        /* Precompute per-edge slope (dx per dy, fixed-point). Each
         * scanline reads two edges and reconstructs x via slope * (y -
         * y_start) — no division in the scanline loop. */
        fx_t edge_dx[4];
        for (int e = 0; e < 4; e++) {
            int e1 = (e + 1) & 3;
            int dy = corner_sy[e1] - corner_sy[e];
            edge_dx[e] = (dy != 0)
                ? ((fx_t)(corner_sx[e1] - corner_sx[e]) << FX_SHIFT) / dy
                : 0;
        }

        /* Per-light flicker — converts a random roll into a brightness
         * offset added to the per-row pattern below. 0 = full bright,
         * +1 = 75%, +2 = 50% (dim glitch). Gated by LIGHTING_FLICKER:
         * with the bit clear, every panel stays at full brightness. */
        int flicker_off = 0;
        if (SHARED_UC->lighting_flags & LIGHTING_FLICKER) {
            uint32_t r = light_frame * 1103515245u + i * 12347u;
            int roll = (r >> 24) & 0x1F;
            if      (roll < 2)  flicker_off = 2;
            else if (roll < 5)  flicker_off = 1;
        }

        /* Two-bulb fluorescent troffer pattern: divide the panel
         * vertically into 16 bands. Outer 2 bands top + bottom = dim
         * frame, bands 2-5 and 10-13 = bright bulbs, middle 4 bands =
         * dimmer gap between the bulbs. Suggests a real fixture with
         * two parallel tubes behind a diffuser. */
        int panel_h = max_y - min_y + 1;

        /* Scanline fill: at each row, find the two edges that span
         * this y and reconstruct left/right x from precomputed slopes.
         * Z-test per column against walls so foreground walls occlude
         * the lit area cleanly. */
        for (int y = min_y; y <= max_y; y++) {
            int xs[2];
            int n = 0;
            for (int e = 0; e < 4 && n < 2; e++) {
                int e1 = (e + 1) & 3;
                int lo = corner_sy[e] < corner_sy[e1] ? corner_sy[e] : corner_sy[e1];
                int hi = corner_sy[e] < corner_sy[e1] ? corner_sy[e1] : corner_sy[e];
                if (y < lo || y > hi) continue;
                if (corner_sy[e] == corner_sy[e1]) continue;
                xs[n++] = corner_sx[e]
                       + (int)(((fx_t)(y - corner_sy[e]) * edge_dx[e]) >> FX_SHIFT);
            }
            if (n < 2) continue;
            int lx_s = xs[0] < xs[1] ? xs[0] : xs[1];
            int rx_s = xs[0] > xs[1] ? xs[0] : xs[1];
            if (lx_s < 0)         lx_s = 0;
            if (rx_s >= SCREEN_W) rx_s = SCREEN_W - 1;

            /* Pick row brightness from the bulb pattern, then add
             * flicker offset and clamp to the 4-entry light ramp. */
            int frac = ((y - min_y) << 4) / panel_h;
            int base_off;
            if      (frac < 2 || frac >= 14) base_off = 2;
            else if (frac < 6 || frac >= 10) base_off = 0;
            else                             base_off = 1;
            int idx = base_off + flicker_off;
            if (idx > 3) idx = 3;
            uint8_t color = (uint8_t)(LIGHT_BASE + idx);

            uint8_t *p = fb + y * SCREEN_W + lx_s;
            for (int x = lx_s; x <= rx_s; x++) {
                if (centerY < WALL_DIST(x)) *p = color;
                p++;
            }
        }
    }
}

/* Drop-ceiling grid pass — called from the slave SH-2's dispatch loop
 * after the master writes the player snapshot and signals
 * MARS_CMD_CEILING on COMM4. Master can also call this directly during
 * single-CPU testing; in both cases the function reads player state
 * from SHARED_UC (cache-through alias) so the slave sees the master's
 * latest writes without explicit cache flushes.
 *
 * Writes only to the top half of the framebuffer (rows 0..SCREEN_H/2-1).
 * Disjoint from the carpet pass (bottom half) so they run in parallel
 * without races. Walls overwrite grid pixels where they intersect,
 * matching the previous sequential behavior. */
void raycast_draw_ceiling_grid(int col_start, int col_end) {
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    fx_t leftDirX  = dirX - planeX;
    fx_t leftDirY  = dirY - planeY;
    fx_t rightDirX = dirX + planeX;
    fx_t rightDirY = dirY + planeY;

    uint8_t *fb = fb_pixels();
    /* horizon_y shifts with pitch; focal_const stays unshifted so
     * depth math stays calibrated regardless of head tilt. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    const int focal_const = SCREEN_H / 2;
    /* Same trick as the carpet: rebase row_color sampling so the
     * ceiling fog gradient and grid-line shade follow the shifted
     * horizon instead of staying glued to absolute screen Y. */
    int sample_bias = SCREEN_H / 2 - horizon_y;

    /* For band detection when dX or dY is exactly 0 (facing cardinal):
     * we track wxL_s / wyL_s across rows and emit a full-width band
     * whenever the integer part crosses between adjacent rows. */
    fx_t prev_wxL_s = 0;
    fx_t prev_wyL_s = 0;
    int  has_prev   = 0;

    for (int y = 0; y < horizon_y && y < SCREEN_H; y++) {
        int p = horizon_y - y;
        /* rowDist always positive; DIVU is ~3× faster than software. */
        fx_t rowDist = (fx_t)divu_u32((uint32_t)((fx_t)focal_const << FX_SHIFT),
                                      (uint32_t)p);
        fx_t wxL = px + FX_MUL(rowDist, leftDirX);
        fx_t wxR = px + FX_MUL(rowDist, rightDirX);
        fx_t wyL = py + FX_MUL(rowDist, leftDirY);
        fx_t wyR = py + FX_MUL(rowDist, rightDirY);

        fx_t wxL_s = wxL * CEIL_GRID_DENSITY;
        fx_t wxR_s = wxR * CEIL_GRID_DENSITY;
        fx_t wyL_s = wyL * CEIL_GRID_DENSITY;
        fx_t wyR_s = wyR * CEIL_GRID_DENSITY;

        int sy = y + sample_bias;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        int base_shade = row_color[sy] - CEIL_BASE;
        if (base_shade >= SHADE_LEVELS - 1) {
            /* Skip drawing but keep prev_* coherent for next row's band test. */
            prev_wxL_s = wxL_s; prev_wyL_s = wyL_s; has_prev = 1;
            continue;
        }
        int shade = base_shade + 3;
        if (shade >= SHADE_LEVELS) shade = SHADE_LEVELS - 1;
        uint8_t grid_c = CEIL_BASE + shade;

        uint8_t *row_p = fb + y * SCREEN_W;

        /* World-X grid lines: per-pixel crossings when there's spread,
         * full-width band when facing exactly E/W (dX == 0). */
        fx_t dX = wxR_s - wxL_s;
        if (dX != 0) {
            int lo = FX_INT(wxL_s), hi = FX_INT(wxR_s);
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            if (lo + 1 <= hi) {
                fx_t scale = FX_DIV((fx_t)SCREEN_W << FX_SHIFT, dX);
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wxL_s;
                    int col = mul_hi32_s(num, scale);
                    if (col >= col_start && col < col_end) row_p[col] = grid_c;
                }
            }
        } else if (has_prev && FX_INT(wxL_s) != FX_INT(prev_wxL_s)) {
            for (int col = col_start; col < col_end; col++) row_p[col] = grid_c;
        }

        /* World-Y grid lines: per-pixel crossings when there's spread,
         * full-width band when facing exactly N/S (dY == 0). */
        fx_t dY = wyR_s - wyL_s;
        if (dY != 0) {
            int lo = FX_INT(wyL_s), hi = FX_INT(wyR_s);
            if (lo > hi) { int t = lo; lo = hi; hi = t; }
            if (lo + 1 <= hi) {
                fx_t scale = FX_DIV((fx_t)SCREEN_W << FX_SHIFT, dY);
                for (int target = lo + 1; target <= hi; target++) {
                    fx_t num = ((fx_t)target << FX_SHIFT) - wyL_s;
                    int col = mul_hi32_s(num, scale);
                    if (col >= col_start && col < col_end) row_p[col] = grid_c;
                }
            }
        } else if (has_prev && FX_INT(wyL_s) != FX_INT(prev_wyL_s)) {
            for (int col = col_start; col < col_end; col++) row_p[col] = grid_c;
        }

        prev_wxL_s = wxL_s;
        prev_wyL_s = wyL_s;
        has_prev   = 1;
    }
}

/* Carpet wear pass — stamps dark "stains" across the floor (bottom
 * half of screen) at world-position-hashed locations. Reads player
 * from the shared snapshot so the slave can run it alongside the
 * ceiling grid pass on the top half (disjoint framebuffer regions,
 * no race). */
void raycast_draw_carpet(int col_start, int col_end) {
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));
    fx_t leftDirX  = dirX - planeX;
    fx_t leftDirY  = dirY - planeY;
    fx_t rightDirX = dirX + planeX;
    fx_t rightDirY = dirY + planeY;

    uint8_t *fb = fb_pixels();
    /* horizon_y is the on-screen dividing line between ceiling and floor;
     * shifts with pitch. focal_const stays at SCREEN_H/2 (the
     * camera-height·focal-length product in the perspective formula) so
     * depth-per-row remains calibrated when the camera pitches. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    const int focal_const = SCREEN_H / 2;
    /* sample_bias rebases row_color sampling so its color gradient
     * (and the stain-LOD derived from base_shade) travels with the
     * shifted horizon. Without this the gradient stayed glued to
     * absolute Y while the geometry math moved — visible as static
     * stain density bands and a non-perspective color band when
     * tilting up or down. */
    int sample_bias = SCREEN_H / 2 - horizon_y;
    for (int y = horizon_y + 1; y < SCREEN_H; y++) {
        if (y < 0) continue;        /* extreme positive pitch */
        int sy = y + sample_bias;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        uint8_t base_c = row_color[sy];
        int base_shade = base_c - FLOOR_BASE;
        if (base_shade >= SHADE_LEVELS - 2) continue;

        int p = y - horizon_y;
        /* rowDist always positive (y > horizon_y); DIVU. */
        fx_t rowDist = (fx_t)divu_u32((uint32_t)((fx_t)focal_const << FX_SHIFT),
                                      (uint32_t)p);
        fx_t worldX = px + FX_MUL(rowDist, leftDirX);
        fx_t worldY = py + FX_MUL(rowDist, leftDirY);
        fx_t stepX  = FX_MUL(rowDist, rightDirX - leftDirX) / SCREEN_W;
        fx_t stepY  = FX_MUL(rowDist, rightDirY - leftDirY) / SCREEN_W;
        uint8_t dark_c = (uint8_t)(FLOOR_BASE + base_shade + 2);

        /* Distance-based screen-stamp step (LOD). Each carpet stain is
         * defined in world space — the screen step is just how often we
         * sample. At distance, fewer screen pixels cover the same world
         * area so we can sample sparser without losing visible density.
         *   base_shade 0-3 (close):  every 4th  px (densest, normal pass)
         *   base_shade 4-7 (mid):    every 8th  px
         *   base_shade 8+  (far):    every 16th px
         * Saves ~40% of carpet pass work on a typical scene where most
         * rows are mid/far range. */
        int x_step = 4;
        if      (base_shade >= 8) x_step = 16;
        else if (base_shade >= 4) x_step = 8;
        fx_t stepWX = stepX * x_step;
        fx_t stepWY = stepY * x_step;

        /* Pre-advance worldX/Y to col_start so this CPU only does the
         * iterations covering its column range. col_start is a multiple
         * of 4; we round it down to a multiple of x_step too. */
        int x_start = (col_start / x_step) * x_step;
        if (x_start < col_start) x_start += x_step;
        int skip = x_start / x_step;
        worldX += stepWX * skip;
        worldY += stepWY * skip;
        for (int x = x_start; x < col_end; x += x_step) {
            int wx = (int)(worldX >> 13) & 0xFF;
            int wy = (int)(worldY >> 13) & 0xFF;
            int hash = (wx * 73 + wy * 31) & 0xF;
            if (hash < 6) fb[y * SCREEN_W + x] = dark_c;
            worldX += stepWX;
            worldY += stepWY;
        }
    }
}

/* Wall column pass — DDA, perspective-correct textured columns, fog
 * cutoff, distance-based detail falloff. Caller-supplied half-open
 * column range [col_start, col_end) lets the master and slave divide
 * the screen — master does [0, SCREEN_W/2), slave does [SCREEN_W/2,
 * SCREEN_W). Writes the per-column z-buffer (WALL_DIST) through the
 * cache-through alias so the sprite passes on master see slave's
 * writes after the COMM4 sync. Reads player from SHARED_UC. */
void raycast_draw_walls(int col_start, int col_end) {
    fx_t px = SHARED_UC->player.x;
    fx_t py = SHARED_UC->player.y;
    uint8_t angle = (uint8_t)SHARED_UC->player.angle;

    fx_t dirX   = COS_FX(angle);
    fx_t dirY   = SIN_FX(angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    uint8_t *fb = fb_pixels();

    /* Fixed col_start..col_end split — work-stealing via TAS + COMM6
     * was attempted but the ~190K atomic bus ops/sec during the wall
     * pass drowned the 68K→SH2 bridge that carries joypad reads back
     * to MARS_SYS_COMM8, re-introducing the controller-drop stall.
     * Sticking with the master 0..SCREEN_W/2 / slave SCREEN_W/2..
     * SCREEN_W static split until we have a low-contention work-
     * stealing pattern (e.g. master pre-chunking into 8-column
     * batches and the slave just reading a written-once index). */
    /* Load pitch once — read via cache-through alias so master's
     * latest write is visible. Walls center on the shifted horizon. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    for (int col = col_start; col < col_end; col++) {
        WALL_DIST(col) = 0x7FFFFFFF;
        fx_t cameraX = cameraX_table[col];
        fx_t rayDirX = dirX + FX_MUL(planeX, cameraX);
        fx_t rayDirY = dirY + FX_MUL(planeY, cameraX);

        int mapX = FX_INT(px);
        int mapY = FX_INT(py);

        fx_t deltaDistX = (rayDirX == 0) ? 0x7FFFFFFF
                                         : FX_DIV(FX_ONE, FX_ABS(rayDirX));
        fx_t deltaDistY = (rayDirY == 0) ? 0x7FFFFFFF
                                         : FX_DIV(FX_ONE, FX_ABS(rayDirY));

        int stepX, stepY;
        fx_t sideDistX, sideDistY;
        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = FX_MUL(px - ((fx_t)mapX << FX_SHIFT), deltaDistX);
        } else {
            stepX = 1;
            sideDistX = FX_MUL(((fx_t)(mapX + 1) << FX_SHIFT) - px, deltaDistX);
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = FX_MUL(py - ((fx_t)mapY << FX_SHIFT), deltaDistY);
        } else {
            stepY = 1;
            sideDistY = FX_MUL(((fx_t)(mapY + 1) << FX_SHIFT) - py, deltaDistY);
        }

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
            if (world_map[mapY][mapX]) { hit = 1; break; }
            if (sideDistX > MAX_VIEW_DIST && sideDistY > MAX_VIEW_DIST) break;
        }
        /* Do NOT continue on !hit yet — even when the DDA bails because
         * the nearest wall is past MAX_VIEW_DIST, a partition can still
         * be right in front of the player. Initialize perpDist to
         * effectively-infinity in that case so the partition override
         * below has something to beat. The !hit && !partition_hit case
         * gets skipped after the override test. */
        fx_t perpDist;
        if (hit) {
            perpDist = (side == 0) ? (sideDistX - deltaDistX)
                                   : (sideDistY - deltaDistY);
            if (perpDist < FX(0.1)) perpDist = FX(0.1);
        } else {
            perpDist = 0x7FFFFFFF;
        }

        /* Per-ray partition intersection. Test this column's ray against
         * each visible partition face (line segment in world space) using
         * standard 2D ray-segment intersection. Take the closest hit. No
         * cross-column interpolation, so no wedge artifact possible — each
         * column gets its own independent t (= perpDist, since dot(rayDir,
         * dir) = 1 by construction of rayDir = dir + cameraX*plane).
         *
         * Saturated divide guards against denom underflow at glancing
         * angles where the wall DDA would similarly lose precision — same
         * trap that originally pushed us to projection. */
        int  partition_hit       = 0;
        fx_t partition_wallhit_w = 0;
        int  n_faces = PFACE_COUNT;
        for (int fi = 0; fi < n_faces; fi++) {
            fx_t ax = PFACE_AX(fi);
            fx_t ay = PFACE_AY(fi);
            fx_t bx = PFACE_BX(fi);
            fx_t by = PFACE_BY(fi);
            fx_t dxs = bx - ax;
            fx_t dys = by - ay;
            fx_t cx  = ax - px;
            fx_t cy  = ay - py;

            fx_t denom = FX_MUL(rayDirY, dxs) - FX_MUL(rayDirX, dys);
            if (denom == 0) continue;

            fx_t t_num = FX_MUL(cy, dxs) - FX_MUL(cx, dys);
            fx_t t = fx_div_sat(t_num, denom);
            if (t <= FX(0.1)) continue;
            if (t >= perpDist) continue;

            fx_t s_num = FX_MUL(rayDirX, cy) - FX_MUL(rayDirY, cx);
            fx_t s = fx_div_sat(s_num, denom);
            if (s < 0 || s > FX_ONE) continue;

            perpDist = t;
            fx_t ua = PFACE_UA(fi);
            fx_t ub = PFACE_UB(fi);
            partition_wallhit_w = ua + FX_MUL(s, ub - ua);
            partition_hit = 1;
            side = 0;
        }

        /* No wall AND no partition in range — leave the column as the
         * sky/ceiling/floor that earlier passes painted. */
        if (!hit && !partition_hit) continue;

        WALL_DIST(col) = perpDist;
        /* No hard cutoff at MAX_VIEW_DIST — let walls render through
         * fog. The shade ramp clamps them to shade 15 past FOG_RAMP_DIST
         * so they're already fog-colored before they would have popped
         * in/out abruptly. */

        /* DIVU latency hide #1: start lineHeight = (SCREEN_H << 16) /
         * perpDist, then do wall_shade + wall_hit + texX in parallel
         * — none of those depend on lineHeight. 39 cycles of divide
         * disappear under ~40 cycles of column setup. */
        divu_start_u32((uint32_t)(SCREEN_H << FX_SHIFT),
                       (uint32_t)perpDist);

        /* Inflection at FX(2.5) (was 3.5) so mid-distance walls darken
         * sooner into the fog. The close ramp (0..2.5) is unchanged at
         * the rounding level so the wallpaper region stays bright; the
         * far ramp now covers a longer span (2.5..6 instead of 3.5..6),
         * pulling perpDist 3-5 walls down 2-3 shade levels — they read
         * as "in the fog" instead of "lit yellow far away". */
        int wall_shade;
        if (perpDist < FX(2.5)) {
            wall_shade = (int)((perpDist * 2) / FX(2.5));
        } else {
            fx_t past = perpDist - FX(2.5);
            fx_t span = FOG_RAMP_DIST - FX(2.5);
            wall_shade = 2 + (int)((past * 13) / span);
        }
        /* Past FOG_RAMP_DIST the formula keeps going up; clamp here
         * so the LOD/shade_lut math sees a valid index. Everything in
         * the 6..10 distance band renders at shade 15. */
        if (wall_shade > SHADE_LEVELS - 1) wall_shade = SHADE_LEVELS - 1;
        /* Bump all walls one shade darker so the wall reads as the
         * muted yellow of an actual Backrooms hallway instead of the
         * brightest palette entry. Was previously applied as a side==1
         * depth cue (one wall darker than the other); now uniform on
         * both sides so the wallpaper reads consistently and the
         * chevron sits in the same perceptual region everywhere. */
        wall_shade += 1;
        if (wall_shade < 0) wall_shade = 0;
        /* Final clamp so the baseboard color lookup (WALL_BASE +
         * wall_shade) and any downstream shade-index user can't walk
         * off the end of the wall palette into FLOOR_BASE — that bug
         * was painting distant baseboards as bright carpet yellow. */
        if (wall_shade > SHADE_LEVELS - 1) wall_shade = SHADE_LEVELS - 1;

        /* Distant fluorescent strobe burst. Each cell past
         * FOG_RAMP_DIST has its own pseudo-random phase. Most of the
         * time it sits dark (shade 15). Phase 0..5 of every 256-frame
         * cycle (~2.3% per cell) it enters a 6-frame "burst" where it
         * flickers on/off at a per-frame coin flip — reads as a
         * fluorescent panel struggling to start, not a single-frame
         * pop. Cell offset (cell_seed >> 4) staggers which cells fire
         * when, so distant scenes get a steady drip of flickers from
         * different positions over time. */
        if (perpDist >= FOG_RAMP_DIST
            && (SHARED_UC->lighting_flags & LIGHTING_STROBE)) {
            uint32_t cell_seed = (uint32_t)mapX * 12347u
                               + (uint32_t)mapY * 7919u;
            uint32_t phase = (SHARED_UC->frame_count + (cell_seed >> 4))
                             & 0xFF;
            if (phase < 6) {
                uint32_t flicker = SHARED_UC->frame_count * 1103515245u
                                 + cell_seed;
                if (flicker & 0x800000) wall_shade = 12;
            }
        }

        /* Per-column LOD: hi-res chevron when the wall hit is close
         * enough to read the motif, lo-res noise otherwise. Adjacent
         * columns can land on opposite sides of the threshold — that's
         * the seam tradeoff we accepted up front. */
        const uint8_t *tex_data;
        int tex_w, tex_h, tile_x, tile_y;
        if (perpDist < WALL_LOD_THRESHOLD) {
            tex_data = (const uint8_t *)wall_tex_hi;
            tex_w    = WALL_TEX_HI_WIDTH;
            tex_h    = WALL_TEX_HI_HEIGHT;
            tile_x   = WALL_TILE_HI_X;
            tile_y   = WALL_TILE_HI_Y;
        } else {
            tex_data = (const uint8_t *)wall_tex;
            tex_w    = WALL_TEX_WIDTH;
            tex_h    = WALL_TEX_HEIGHT;
            tile_x   = WALL_TILE_X;
            tile_y   = WALL_TILE_Y;
        }
        const int tex_w_mask = tex_w - 1;
        const int tex_h_mask = tex_h - 1;

        fx_t wall_hit = partition_hit
            ? partition_wallhit_w
            : (side == 0)
                ? (py + FX_MUL(perpDist, rayDirY))
                : (px + FX_MUL(perpDist, rayDirX));
        wall_hit -= (fx_t)FX_INT(wall_hit) << FX_SHIFT;
        /* wall_hit ∈ [0, FX_ONE), tex_w*tile_x ≤ 1024 → product ≤ 67M
         * which doesn't fit in int32 signed but does fit in uint32. The
         * uint32 cast lets SH-2 use a single MUL.L (~3 cycles) instead
         * of the int64 software multiply (~40 cycles). Mask wraps. */
        int texX = (int)(((uint32_t)wall_hit * (uint32_t)(tex_w * tile_x))
                          >> FX_SHIFT)
                   & tex_w_mask;

        int lineHeight = (int)divu_read();

        int wall_top  = horizon_y - lineHeight / 2;
        int wall_bot  = horizon_y + lineHeight / 2;
        int drawStart = wall_top < 0 ? 0 : wall_top;
        int drawEnd   = wall_bot >= SCREEN_H ? SCREEN_H - 1 : wall_bot;

        /* DIVU latency hide #2: start tex_step = (tex_h*tile_y
         * << 16) / lineHeight, then do detail_factor + shade_lut in
         * parallel. The shade_lut loop alone is ~256 cycles, far
         * exceeding the 39-cycle DIVU latency. */
        divu_start_u32((uint32_t)((tex_h * tile_y) << FX_SHIFT),
                       (uint32_t)lineHeight);

        int detail_factor;
        if (perpDist < FX(2)) {
            detail_factor = WALL_PATTERN_MAX;
        } else if (perpDist < FX(3.5)) {
            fx_t remaining = FX(3.5) - perpDist;
            fx_t span      = FX(1.5);
            detail_factor  = (int)((remaining * WALL_PATTERN_MAX) / span);
            if (detail_factor < 0)               detail_factor = 0;
            if (detail_factor > WALL_PATTERN_MAX) detail_factor = WALL_PATTERN_MAX;
        } else {
            detail_factor = 0;
        }
        /* Column-major wall_tex: tex_data[texX * tex_h ..] is the
         * contiguous tex_h-byte strip this loop walks. Sized for the
         * largest possible TEX_H (hi-res 64), tiny stack cost. */
        const uint8_t *wall_col = tex_data + texX * tex_h;

        /* Two-stage LUT build. wall_tex values are 0..4 (5 buckets
         * from bake_wall.py --levels 5). Precompute the final palette
         * byte for each bucket once per column, then the per-ty loop
         * becomes a pure table indirection — no multiply, shift, or
         * clamp per ty. At hi-res LOD (tex_h=64) this cuts ~5 cycles
         * × 64 ty ≈ 320 cycles per column × 160 cols per CPU ≈ ~2ms
         * per CPU per frame on wall-heavy scenes. */
        uint8_t lut5[5];
        for (int v = 0; v < 5; v++) {
            int pattern = (v * detail_factor) >> 4;
            int s = wall_shade + pattern;
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            lut5[v] = (uint8_t)(WALL_BASE + s);
        }
        uint8_t shade_lut[WALL_TEX_HI_HEIGHT];
        for (int ty = 0; ty < tex_h; ty++) {
            shade_lut[ty] = lut5[wall_col[ty]];
        }

        /* Baseboard molding: bottom ~3% of the wall in world space gets
         * a darker flat-shade band, the iconic Backrooms wood-trim look.
         * Anchored to wall_bot (unclipped) so the strip sits at the same
         * world height regardless of whether the wall extends off-screen.
         * Split the inner pixel loop in two so the per-pixel hot path
         * stays branch-free: wall portion runs the textured loop, then
         * the baseboard portion writes the flat color. */
        int base_h = lineHeight >> 5;
        if (base_h < 1) base_h = 1;
        int base_y = wall_bot - base_h;
        int wall_end;
        if      (base_y > drawEnd)    wall_end = drawEnd;
        else if (base_y <= drawStart) wall_end = drawStart - 1;
        else                          wall_end = base_y - 1;
        /* Molding color = wall_shade with no chevron pattern offset, so
         * the strip reads as the wallpaper's background yellow with the
         * chevron motif simply stopping at the molding line. */
        uint8_t base_color = (uint8_t)(WALL_BASE + wall_shade);
        /* 1-pixel darker line at the wall/molding boundary suggests
         * the shadow gap of a recessed baseboard — a small depth cue
         * that reads as the molding standing slightly proud of the
         * wall. Two shades darker than the molding base. */
        int shadow_shade = wall_shade + 2;
        if (shadow_shade > SHADE_LEVELS - 1) shadow_shade = SHADE_LEVELS - 1;
        uint8_t shadow_color = (uint8_t)(WALL_BASE + shadow_shade);

        fx_t tex_step = (fx_t)divu_read();
        fx_t tex_pos  = (fx_t)(drawStart - wall_top) * tex_step;
        uint8_t *p = (uint8_t *)fb + col + drawStart * SCREEN_W;
        /* Hand-rolled SH-2 asm wall column inner loop. 4 pixels per
         * iteration, no spills, indexed byte load via @(R0,Rm), DT-
         * driven count-down for one cmp/bra per 4 pixels.
         *
         * Per pixel: mov+shlr16+and+mov.b(load)+add+mov.b(store)+add
         * = 7 instructions. 4 pixels + dt + bf = 30 instructions per
         * 4-pixel iter. GCC's auto-unrolled C version was generating
         * around 36-40 with some spills; the asm version both keeps
         * tex_pos/p/lut/step/mask in registers across the unroll AND
         * schedules the load-use chains tightly so SH-2's narrow
         * pipeline stays full.
         *
         * Preserves the original post-loop state of p and tex_pos
         * (advances exactly (wall_end - drawStart + 1) writes) so the
         * baseboard loop below picks up at the right framebuffer row. */
        int total = wall_end - drawStart + 1;
        if (total > 0) {
            int iters4 = total >> 2;
            int tail   = total & 3;
            if (iters4 > 0) {
                /* clang LSP runs with host-arch register widths and
                 * flags every 32-bit fx_t operand below as
                 * "size doesn't match register width". sh-elf-gcc
                 * (32-bit SH-2 registers) sees no mismatch. Silence
                 * the LSP-only noise here. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wasm-operand-widths"
#endif
                __asm__ __volatile__ (
                    "1:\n\t"
                    /* pixel A */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel B */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel C */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* pixel D */
                    "mov   %[tp], r0\n\t"
                    "shlr16 r0\n\t"
                    "and   %[mask], r0\n\t"
                    "mov.b @(r0,%[lut]), r1\n\t"
                    "add   %[step], %[tp]\n\t"
                    "mov.b r1, @%[p]\n\t"
                    "add   %[sw], %[p]\n\t"
                    /* DT decrements iters4 and sets T when zero. */
                    "dt    %[it4]\n\t"
                    "bf    1b\n\t"
                    : [tp] "+r"(tex_pos), [p] "+r"(p), [it4] "+r"(iters4)
                    : [step] "r"(tex_step), [mask] "r"(tex_h_mask),
                      [lut] "r"(shade_lut), [sw] "r"((int)SCREEN_W)
                    : "r0", "r1", "memory"
                );
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            }
            while (tail-- > 0) {
                *p = shade_lut[(tex_pos >> FX_SHIFT) & tex_h_mask];
                p += SCREEN_W;
                tex_pos += tex_step;
            }
        }
        /* Top row of the molding gets the shadow-line color, rest of
         * the strip stays at base_color. */
        int by = wall_end + 1;
        if (by <= drawEnd) {
            *p = shadow_color;
            p += SCREEN_W;
            by++;
        }
        for (; by <= drawEnd; by++) {
            *p = base_color;
            p += SCREEN_W;
        }
    }
}

/* The other tex_pos reference in this file is in the old fixed-split
 * placeholder. Compiler dead-code-eliminates it once raycast_render
 * stops calling fixed-range walls. */

/* Profile counters. Both written by raycast_render, read by m_main.c
 * for the on-screen overlay. half = master's parallel work
 * (clear+ceiling+carpet+walls of its column range); idle = time
 * spent spinning on the slave-done sync after that. */
volatile uint16_t prof_master_idle_ticks = 0;
volatile uint16_t prof_master_half_ticks = 0;
static inline uint16_t prof_frt_read(void) {
    uint8_t hi = SH2_FRT_FRCH;
    uint8_t lo = SH2_FRT_FRCL;
    return ((uint16_t)hi << 8) | lo;
}

/* Clear the framebuffer for a column range, using row_color[] for the
 * per-row constant fill. Each CPU calls this on its own half so the
 * clear runs in parallel and no row is touched twice. col_start must
 * be a multiple of 4 (we use 32-bit stores = 4 pixels per write). */
void raycast_clear_half(int col_start, int col_end) {
    uint8_t *fb = fb_pixels();
    uint32_t *fb32 = (uint32_t *)fb;
    int col_words = (col_end - col_start) >> 2;
    int col_word_start = col_start >> 2;
    /* Background fill must follow the shifted horizon — otherwise the
     * ceiling-vs-floor color split stays pinned at SCREEN_H/2 while the
     * wall draws and grid overlays move with pitch, producing a visible
     * band of "ceiling gray" below the walls (when looking down) or
     * "floor orange" above them (looking up). Sample row_color at
     * (y - horizon_y + SCREEN_H/2) so the table's ceiling/floor gradient
     * stays centered on the shifted horizon. */
    int horizon_y   = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    int sample_bias = SCREEN_H / 2 - horizon_y;     /* = pitch_y */
    for (int y = 0; y < SCREEN_H; y++) {
        int sy = y + sample_bias;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        uint8_t  c   = row_color[sy];
        uint32_t c32 = ((uint32_t)c << 24) | ((uint32_t)c << 16)
                     | ((uint32_t)c <<  8) |  (uint32_t)c;
        uint32_t *row = fb32 + y * (SCREEN_W / 4) + col_word_start;
        for (int x = 0; x < col_words; x++) row[x] = c32;
    }
}

void raycast_render(void) {
    uint8_t *fb = fb_pixels();
    uint16_t prof_start = prof_frt_read();

    /* Vertical head bob is applied below via the framebuffer line table —
     * no position translation needed (lateral sway felt like drunk
     * swagger, not walking). */

    /* Camera basis: forward = (cos a, sin a); camera plane perpendicular,
     * length 0.66 -> ~66° horizontal FOV. */
    fx_t dirX   = COS_FX(player.angle);
    fx_t dirY   = SIN_FX(player.angle);
    fx_t planeX = FX_MUL(-dirY, FX(0.66));
    fx_t planeY = FX_MUL( dirX, FX(0.66));

    /* Snapshot player state for the slave to read via cache-through.
     * Must land before COMM4 wakes the slave so it sees the new frame. */
    SHARED_UC->player.x     = player.x;
    SHARED_UC->player.y     = player.y;
    SHARED_UC->player.angle = player.angle;
    SHARED_UC->is_walking   = is_walking;   /* gates carpet footsteps in pump */

    /* Camera pitch — eased manual hold-C tilt (pitch_smooth_y) plus
     * the ±1 walking pitch bob from bob_phase. The bob couples to the
     * same phase as the vertical line-table bob so foot-strike dips
     * both pitch AND vertical image in lockstep. Clamp the combined
     * value to int8_t before publishing to SHARED_UC. */
    int pitch_combined = pitch_smooth_y;
    if (is_walking) {
        pitch_combined += (int)((SIN_FX(bob_phase) * 1) >> FX_SHIFT);
    }
    if (pitch_combined > 127)  pitch_combined = 127;
    if (pitch_combined < -128) pitch_combined = -128;
    SHARED_UC->pitch_y = (int8_t)pitch_combined;

    /* Build the visible-partition-faces list once. Master populates
     * pface_* via cache-through alias; both halves of raycast_draw_walls
     * read them when doing per-ray ray-segment intersection. Must finish
     * BEFORE the slave wake below.
     *
     * Drain: a read-back of the last-written cache-through address
     * serializes against all prior writes through the same alias bus
     * path. Without it the MARS controller can forward COMM4=HALF
     * before SDRAM writes are visible from slave's view. */
    partition_build_faces();
    (void)PFACE_COUNT;
    __asm__ __volatile__("" ::: "memory");

    /* Single dispatch: slave does clear + ceiling + carpet + walls for
     * cols 160..319, master does the same for cols 0..159 in parallel.
     * Column ownership eliminates the previous CEILING→WALLS sequential
     * dependency (master used to idle ~26ms waiting for slave's ceiling
     * before walls could start). One sync point at the end. */
    MARS_SYS_COMM4 = MARS_CMD_HALF;

    raycast_clear_half(0, SCREEN_W / 2);
    raycast_draw_ceiling_grid(0, SCREEN_W / 2);
    raycast_draw_carpet(0, SCREEN_W / 2);
    raycast_draw_walls(0, SCREEN_W / 2);

    uint16_t idle_start = prof_frt_read();
    prof_master_half_ticks = (uint16_t)(idle_start - prof_start);
    while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
        /* Throttle the master-side ACK wait to reduce 68K-bridge
         * pressure — bare-loop polling at ~5M reads/sec was lining
         * up with the 68K's joypad-read window often enough to drop
         * COMM8 updates. ~30 cycles of NOPs per loop iteration brings
         * the master poll rate down to a friendlier ~700K/sec while
         * keeping latency well under one frame. */
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop");
    }
    prof_master_idle_ticks = (uint16_t)(prof_frt_read() - idle_start);

    /* Commit any reordered stores from the non-volatile draw loops before
     * the next swapBuffers() makes them visible via the VDP page flip. */
    __asm__ __volatile__("" ::: "memory");

    /* Lights first, then standups — so foreground sprites (like the
     * neanderthal) overpaint any ceiling-panel pixels that project
     * into the same screen rows. The light z-test handles walls; the
     * draw-order handles sprites. */
    draw_lights(fb, dirX, dirY, planeX, planeY);
    /* Partitions are now tested inline in the wall-column DDA — each
     * ray does a ray-segment intersection against partitions[] and
     * overrides perpDist if a partition is closer. Drops to the same
     * column-rendering code path as regular walls. */
    draw_standups(fb, dirX, dirY, planeX, planeY);

    /* Vertical head bob via framebuffer line table.
     *
     * The 32X displays pixel data through a 224-entry line table that
     * maps screen row i to an arbitrary pixel-line offset. By rewriting
     * the table each frame to map row i -> (i + bob_y), the entire
     * displayed image shifts vertically by bob_y pixels without
     * re-rendering anything. Cost is 224 word writes (~0.05ms).
     *
     * Source lines past the ends are clamped to the first/last visible
     * line; the resulting 1-3 row duplication at the bob boundary is
     * invisible against the smooth ceiling/floor gradient. */
    int bob_y = 0;
    if (is_walking) {
        /* sin in -FX_ONE..+FX_ONE, scaled to ±2 pixels for a tight micro-bob. */
        bob_y = (int)((SIN_FX(bob_phase) * 2) >> FX_SHIFT);
    }
    volatile uint16_t *line_table = &MARS_FRAMEBUFFER;
    for (int i = 0; i < SCREEN_H; i++) {
        int src = i + bob_y;
        if (src < 0)         src = 0;
        if (src >= SCREEN_H) src = SCREEN_H - 1;
        line_table[i] = (uint16_t)(src * 160 + 0x100);
    }
}
