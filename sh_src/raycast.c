#include "mars.h"
#include "raycast.h"
#include "shared.h"
#include "sh2_asm.h"
#include "sin_table.h"
#include "wall_tex.h"
#include "wall_tex_hi.h"
#include "neander_tex.h"
#include "neander_tex_hi.h"
#include "outlet_tex.h"
#include "partition_tex.h"

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
static const uint8_t fixed_map[MAP_H][MAP_W] = {
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

/* The HobbyTown lobby — a tiny 5x5 room (open cols 2-6, rows 2-6; the rest
 * of the 32x32 grid is solid, unused). The box is grid walls; the dividers
 * are free-standing wallpaper PARTITIONS set in raycast_load_lobby. Spawn
 * (S) is bottom-centre facing north; walk up through the entrance gap,
 * around the T-divider, and out the east exit to load the chosen level.
 *
 *        col: 2 3 4 5 6 7
 *        r2   . | . . . E    | = T-stem (partition, x=3)
 *        r3   . |== . . #    == = T-arm (partition, x3->5)
 *        r4   . . . . . #
 *        r5   == . == . #    entrance wall (partition), gap cols 3-4
 *        r6   . . S . . #    S = spawn (faces north); E = east exit */
static const uint8_t lobby_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* The live world grid the raycaster reads. Filled at runtime by
 * raycast_load_fixed() / raycast_load_lobby() / procgen_run() — the
 * hand-tuned layout now lives in fixed_map above and is copied in on
 * demand, so the lobby and procedural maps can replace it in place. */
uint8_t world_map[MAP_H][MAP_W];

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
#define OUTLET_BASE   72    /* 5 entries: 0=slot-dark .. 4=plate-white */
#define OUTLET_LEVELS 5
#define PARTITION_BASE 77   /* 16 entries: olive spotted-wallpaper, bright..fog */

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
 * Both CPUs write to this array (primary cols 0-159, secondary 160-319) and
 * the primary reads it back for the sprite z-test. ALL accesses must
 * go through the WALL_DIST() macro below, which routes them via the
 * | 0x20000000 cache-through alias so neither CPU sees stale cached
 * values written by the other. */
static fx_t wall_dist[SCREEN_W];
#define WALL_DIST(i) (((volatile fx_t *)((uintptr_t)wall_dist | 0x20000000))[i])

/* Per-frame list of visible partition face segments, populated by
 * partition_build_faces() once per frame on primary and consumed by
 * both CPUs in raycast_draw_walls via per-ray ray-segment intersection.
 * Each face is a 2D line segment in world space; ua/ub are the texture-U
 * world coordinates at the two endpoints. Cache-through aliased so the
 * secondary's reads are coherent with primary's per-frame writes.
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
static uint8_t pface_style[MAX_PARTITION_FACES];   /* 0=chevron, 1=spotted */
static uint8_t pface_height[MAX_PARTITION_FACES];  /* 0=full, 1..255 = fraction*256 */
static int  pface_count = 0;
/* pface_* are now read CACHED (no 0x20000000 alias). They're written once per
 * frame by the primary in partition_build_faces, then read thousands of times
 * in the per-ray partition loop on both CPUs — uncached reads (~12 cyc) were
 * pure waste. Write-through cache pushes the primary's writes to SDRAM; the
 * secondary purges these lines once per frame (raycast_purge_partition_cache)
 * before the wall pass so it re-reads fresh. The primary wrote them this frame,
 * so its cache is already current. */
#define PFACE_AX(i)    (((volatile fx_t *)pface_ax)[i])
#define PFACE_AY(i)    (((volatile fx_t *)pface_ay)[i])
#define PFACE_BX(i)    (((volatile fx_t *)pface_bx)[i])
#define PFACE_BY(i)    (((volatile fx_t *)pface_by)[i])
#define PFACE_UA(i)    (((volatile fx_t *)pface_ua)[i])
#define PFACE_UB(i)    (((volatile fx_t *)pface_ub)[i])
#define PFACE_STYLE(i) (((volatile uint8_t *)pface_style)[i])
#define PFACE_HEIGHT(i)(((volatile uint8_t *)pface_height)[i])
#define PFACE_COUNT    (*(volatile int *)&pface_count)

/* Purge a byte range from the SH-2 cache via the 0x40000000 alias (one store
 * invalidates a 16-byte line). */
static inline void purge_cache_range(const void *p, unsigned bytes) {
    uintptr_t a   = (uintptr_t)p & ~(uintptr_t)15;
    uintptr_t end = (uintptr_t)p + bytes;
    for (; a < end; a += 16) *(volatile uint32_t *)(a | 0x40000000) = 0;
}

/* Secondary calls this before the wall pass to drop its stale pface_* lines. */
void raycast_purge_partition_cache(void) {
    purge_cache_range(pface_ax,     sizeof pface_ax);
    purge_cache_range(pface_ay,     sizeof pface_ay);
    purge_cache_range(pface_bx,     sizeof pface_bx);
    purge_cache_range(pface_by,     sizeof pface_by);
    purge_cache_range(pface_ua,     sizeof pface_ua);
    purge_cache_range(pface_ub,     sizeof pface_ub);
    purge_cache_range(pface_style,  sizeof pface_style);
    purge_cache_range(pface_height, sizeof pface_height);
    purge_cache_range(&pface_count, sizeof pface_count);
}

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

/* Hardware (a<<16)/b in 16.16 — signed 64÷32 on the SH-2 divide unit at
 * 0xFFFFFF00, ~39 cycles vs ~250 for the libgcc software int64 divide that
 * fx_div_sat compiles to. The 48-bit dividend (a<<16) is fed as DVDNTH:DVDNTL
 * with the high word sign-extended; the unit divides signed natively and the
 * read of DVDNTL stalls until the divide completes.
 *
 * No saturation: the sole caller (the per-ray partition intersection) gates
 * every divide behind |denom| >= 128, which bounds both quotients inside 32
 * bits — so overflow is impossible here and the libgcc saturation path isn't
 * needed. Used ONLY under that guarantee; for general division use fx_div_sat. */
static inline fx_t fx_div_hw(fx_t a, fx_t b) {
    int32_t  hi = a >> 16;               /* sign-extended high word of (a<<16) */
    uint32_t lo = (uint32_t)a << 16;
    int32_t  q;
    __asm__ __volatile__ (
        "mov #-128, r1\n\t"
        "add r1, r1\n\t"                 /* r1 = 0xFFFFFF00 */
        "mov.l %1, @(0, r1)\n\t"         /* DVSR  = divisor          */
        "mov.l %2, @(16, r1)\n\t"        /* DVDNTH = dividend high    */
        "mov.l %3, @(20, r1)\n\t"        /* DVDNTL = dividend low → start */
        "mov.l @(20, r1), %0\n\t"        /* quotient (stalls if !done) */
        : "=r"(q) : "r"(b), "r"(hi), "r"(lo) : "r1"
    );
    return q;
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
    /* Neanderthal ~5 cells north of spawn, pulled west to hug the col-15
     * flat wall (x=16 face) so it stands against the wall and leaves the east
     * side of the col-16 corridor walkable. Solid (collides), the "iconic
     * Backrooms cardboard cutout" moment. Audio via the Voyager hello loop. */
    { FX(16.3), FX(23.5), 64,  0 },
};

/* Wall-mounted decals (currently just the lobby outlet): small billboards
 * anchored at a height fraction z (0=floor, 1=ceiling) instead of the floor.
 * Populated per-map — raycast_load_lobby sets one; load_fixed/procgen clear
 * num_decals so the outlet only shows in the lobby. z is the plate CENTRE. */
/* axis: which wall the plate lies flat on. 1 = wall runs along X (plate spans
 * X, normal +/-Y, e.g. the lobby entrance wall); 0 = wall runs along Y (plate
 * spans Y, normal +/-X, e.g. a side-corridor wall). Drives the foreshortened,
 * wall-flat projection in draw_decals so the plate doesn't pivot to face the
 * camera like a billboard. */
typedef struct { fx_t x, y, z; uint8_t axis; } decal_t;
#define DECAL_OUTLET_H  FX(0.098)  /* outlet plate height as a fraction of wall (30% smaller) */
#define DECAL_OUTLET_HW FX(0.031)  /* half the plate's world width; it lies flat on its wall */
decal_t decals[16];
int     num_decals = 0;

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
/* Per-partition wallpaper: 0 = chevron (like the main walls), 1 = spotted
 * olive divider. Indexed alongside partitions[]; set per-map. */
uint8_t partition_style[NUM_PARTITIONS_MAX] = {0};
/* Per-partition render height: 0 = full ceiling-to-floor (default), else a
 * fraction*256 (e.g. 192 = 3/4) for a low cubicle-style divider anchored at
 * the floor — the ceiling shows above it. Matches the HobbyTown reference's
 * low office partitions. */
uint8_t partition_height[NUM_PARTITIONS_MAX] = {0};
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
/* When set, init_lights uses the lobby's hand-authored fluorescent runs
 * instead of the default every-other-cell auto-grid. Set per-map. */
int g_lobby_ceiling = 0;

/* Per-cell light boost (0..LIGHT_BOOST_MAX): each fixture brightens its own
 * cell and its neighbours, so a wall or partition adjacent to a light reads
 * as lit. Built in init_lights; read by both CPUs in the wall loop via the
 * cache-through alias. */
static uint8_t cell_light[MAP_H][MAP_W];
#define CELL_LIGHT(y,x) (((volatile uint8_t *)((uintptr_t)cell_light | 0x20000000))[(y)*MAP_W + (x)])
#define LIGHT_BOOST_MAX 3
#define LIT_FOG_CAP     9   /* a lit surface never fogs darker than this (-2 per light level) */
#define SIDE_SHADE      1   /* N/S-facing faces are this many shades darker (form cue) */

static void init_lights(void) {
    num_lights = 0;
    if (g_lobby_ceiling) {
        /* Hand-authored lobby ceiling: a fixture in every even column
         * (x=2,4,6,8) down rows 2-5 and 7 (skip the entrance row 6) — gives
         * continuous fluorescent runs like the reference photo. */
        for (int my = 2; my <= 7 && num_lights < MAX_LIGHTS; my++) {
            if (my == 6) continue;
            for (int mx = 2; mx <= 8 && num_lights < MAX_LIGHTS; mx += 2) {
                if (world_map[my][mx] != 0) continue;
                lights[num_lights].x = FX(mx) + FX(0.5);
                lights[num_lights].y = FX(my) + FX(0.5);
                num_lights++;
            }
        }
    } else {
        for (int my = 1; my < MAP_H - 1 && num_lights < MAX_LIGHTS; my += 2) {
            for (int mx = 1; mx < MAP_W - 1 && num_lights < MAX_LIGHTS; mx += 2) {
                if (world_map[my][mx] != 0) continue;
                lights[num_lights].x = FX(mx) + FX(0.5);
                lights[num_lights].y = FX(my) + FX(0.5);
                num_lights++;
            }
        }
    }
    /* Build the per-cell light boost from the placed fixtures. */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) cell_light[y][x] = 0;
    for (int i = 0; i < num_lights; i++) {
        int lcx = FX_INT(lights[i].x), lcy = FX_INT(lights[i].y);
        for (int dy = -1; dy <= 1; dy++) {
            int cy = lcy + dy; if (cy < 0 || cy >= MAP_H) continue;
            for (int dx = -1; dx <= 1; dx++) {
                int cx = lcx + dx; if (cx < 0 || cx >= MAP_W) continue;
                int v = cell_light[cy][cx] + ((dx == 0 && dy == 0) ? 2 : 1);
                if (v > LIGHT_BOOST_MAX) v = LIGHT_BOOST_MAX;
                cell_light[cy][cx] = (uint8_t)v;
            }
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
/* Spotted partition wallpaper. PARTITION_TILE = how many times the 64x64 dot
 * tile repeats per cell (overall dot scale). PARTITION_DETAIL = peak dot
 * darkness vs the yellow — kept low so the dots read as a faint tint up
 * close, and it's distance-faded in the draw loop so far partitions fade to
 * plain yellow (you only notice the dots on the near wall). */
#define PARTITION_TILE_X   8
#define PARTITION_TILE_Y   8
#define PARTITION_DETAIL   8
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

/* Set the gameplay palette scaled to brightness lvl/FADE_STEPS (FADE_STEPS
 * = full bright, 0 = black) — drives the lobby->map fade-through-black.
 * Must be called inside vblank (CRAM write). FADE_STEPS is in raycast.h. */
void raycast_set_brightness(int lvl) {
    if (lvl < 0) lvl = 0; else if (lvl > FADE_STEPS) lvl = FADE_STEPS;
    Hw32xSetBGColor(0, 0, 0, 0);
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(WALL_BASE + i,
            MIX(30,FOG_R,i)*lvl/FADE_STEPS, MIX(28,FOG_G,i)*lvl/FADE_STEPS, MIX(18,FOG_B,i)*lvl/FADE_STEPS);
        Hw32xSetBGColor(FLOOR_BASE + i,
            MIX(25,FOG_R,i)*lvl/FADE_STEPS, MIX(21,FOG_G,i)*lvl/FADE_STEPS, MIX(15,FOG_B,i)*lvl/FADE_STEPS);
        Hw32xSetBGColor(CEIL_BASE + i,
            MIX(25,FOG_R,i)*lvl/FADE_STEPS, MIX(23,FOG_G,i)*lvl/FADE_STEPS, MIX(16,FOG_B,i)*lvl/FADE_STEPS);
    }
    {
        static const uint8_t lt[4][3] = {{31,31,28},{23,23,21},{15,15,14},{7,7,7}};
        for (int i = 0; i < 4; i++)
            Hw32xSetBGColor(LIGHT_BASE + i, lt[i][0]*lvl/FADE_STEPS, lt[i][1]*lvl/FADE_STEPS, lt[i][2]*lvl/FADE_STEPS);
    }
    {
        static const uint8_t nb[8][3] = {{16,11,5},{2,2,1},{7,5,3},{11,8,6},{16,12,9},{19,16,13},{23,20,17},{26,22,19}};
        for (int i = 0; i < 8; i++)
            Hw32xSetBGColor(NEANDER_BASE + i, nb[i][0]*lvl/FADE_STEPS, nb[i][1]*lvl/FADE_STEPS, nb[i][2]*lvl/FADE_STEPS);
    }
    {
        static const uint8_t ob[OUTLET_LEVELS][3] = {{2,2,2},{9,9,8},{16,15,13},{22,21,18},{28,27,23}};
        for (int i = 0; i < OUTLET_LEVELS; i++)
            Hw32xSetBGColor(OUTLET_BASE + i, ob[i][0]*lvl/FADE_STEPS, ob[i][1]*lvl/FADE_STEPS, ob[i][2]*lvl/FADE_STEPS);
    }
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(PARTITION_BASE + i,
            MIX(24,FOG_R,i)*lvl/FADE_STEPS, MIX(25,FOG_G,i)*lvl/FADE_STEPS, MIX(15,FOG_B,i)*lvl/FADE_STEPS);
    }
}

static void build_palette(void) {
    Hw32xSetBGColor(0, 0, 0, 0);
    /* Walls: milky cream-yellow. Desaturated from the old gold (30,27,13,
     * R-B gap 17) by lifting B to 18 (gap 12, sat ~0.40) — reads as pale
     * old wallpaper under fluorescent light rather than school-bus gold,
     * matching the washed-out HobbyTown reference. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(WALL_BASE + i,
                        MIX(30, FOG_R, i),
                        MIX(28, FOG_G, i),
                        MIX(18, FOG_B, i));
    }
    /* Carpet: muted warm tan, hue leaned back toward the original warm-orange
     * cast while keeping the desaturated look. Journey: old Tang (27,22,11,
     * sat ~0.59) -> flat tan (23,21,17, ~0.26, too dead) -> (24,21,16, ~0.33)
     * -> here (25,21,15): R-G gap widened to 4 (the warm/orange character)
     * and B nudged down, sat ~0.40 — warmer than neutral tan but well clear
     * of the old orange. Still a touch darker than the walls so the seam reads. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(FLOOR_BASE + i,
                        MIX(25, FOG_R, i),
                        MIX(21, FOG_G, i),
                        MIX(15, FOG_B, i));
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
    /* Electrical outlet decal: slot-dark -> warm plate-white. */
    Hw32xSetBGColor(OUTLET_BASE + 0,  2,  2,  2);
    Hw32xSetBGColor(OUTLET_BASE + 1,  9,  9,  8);
    Hw32xSetBGColor(OUTLET_BASE + 2, 16, 15, 13);
    Hw32xSetBGColor(OUTLET_BASE + 3, 22, 21, 18);
    Hw32xSetBGColor(OUTLET_BASE + 4, 28, 27, 23);
    /* Partition wallpaper: muted olive-green eggshell (the spotted divider
     * in the reference) — greener / less saturated than the yellow walls,
     * bright..fog like the wall ramp so distance shading + dot motif work. */
    for (int i = 0; i < SHADE_LEVELS; i++) {
        Hw32xSetBGColor(PARTITION_BASE + i,
                        MIX(24, FOG_R, i),
                        MIX(25, FOG_G, i),
                        MIX(15, FOG_B, i));
    }
}

/* Byte pointer to the start of pixel data in the current back framebuffer.
 * (32X 8bpp layout: 0x100 words of line table, then pixels at byte offset 0x200.)
 * Non-volatile: the 32X framebuffer at 0x24000000 isn't SH-2 cached, so
 * writes go through directly. A single `asm("" ::: "memory")` barrier at
 * end-of-render commits any reordered stores before the VDP sees them. */
static inline uint8_t *fb_pixels(void) {
    return (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200);
}

/* Eye height (8.8 fraction of room height): 128 = standing (mid-wall, the
 * original symmetric wall projection); 64 = crawling (eye a quarter up
 * from the floor). Held in SHARED_UC->eye_h so both CPUs' wall draw see it. */
#define STAND_EYE  128
#define CROUCH_EYE 40          /* ~1/6 up the wall — down near the carpet */
/* Crouch tone-gradient shift: how many row_color steps to slide the
 * floor/ceiling fade as the eye drops (more bright carpet before the fade,
 * ceiling fogs sooner). Applied identically in clear_half, the carpet, and
 * the ceiling-grid so the whole gradient travels together. Reuses the same
 * row_color shift the look-pitch uses, just keyed to eye height instead of
 * pitch. 0 when standing. NOTE: this shifts COLOR only — the carpet's stain
 * skip/LOD must stay on the *geometric* (unshifted) distance or far rows
 * un-skip into aliased noise. */
#define CROUCH_GRAD_SHIFT(eh)  (((STAND_EYE - (int)(eh)) * 1) >> 2)

/* SDRAM staging for the wall textures. The .rodata arrays live in cart ROM
 * (0x02000000); these copies live in SDRAM .bss (0x06000000). The SH-2's 4KB
 * write-through cache thrashes on the ~8KB of texture data, and a cache-miss
 * refill from cart ROM is far slower than from SDRAM — so the per-column
 * shade_lut build (which reads a tex_h-byte column slice) gets cheaper refills.
 * Copied once; write-through means the secondary sees it with no flush. */
static uint8_t wall_tex_hi_ram[WALL_TEX_HI_WIDTH][WALL_TEX_HI_HEIGHT];
static uint8_t partition_tex_ram[PARTITION_TEX_WIDTH][PARTITION_TEX_HEIGHT];
static uint8_t wall_tex_ram[WALL_TEX_WIDTH][WALL_TEX_HEIGHT];

static void stage_textures_to_sdram(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    const uint8_t *s; uint8_t *d; int i, n;
    s = (const uint8_t *)wall_tex_hi;   d = (uint8_t *)wall_tex_hi_ram;
    n = (int)sizeof(wall_tex_hi);   for (i = 0; i < n; i++) d[i] = s[i];
    s = (const uint8_t *)partition_tex; d = (uint8_t *)partition_tex_ram;
    n = (int)sizeof(partition_tex); for (i = 0; i < n; i++) d[i] = s[i];
    s = (const uint8_t *)wall_tex;      d = (uint8_t *)wall_tex_ram;
    n = (int)sizeof(wall_tex);      for (i = 0; i < n; i++) d[i] = s[i];
}

void raycast_init(void) {
    stage_textures_to_sdram();
    build_palette();
    build_shading_tables();
    init_lights();
    SHARED_UC->eye_h = STAND_EYE;        /* standing until the player crouches */
    /* Precompute cameraX[col] = 2*col/SCREEN_W - 1 in FX. */
    for (int col = 0; col < SCREEN_W; col++) {
        cameraX_table[col] = ((fx_t)col << (FX_SHIFT + 1)) / SCREEN_W - FX_ONE;
    }
}

/* --- Map loaders --------------------------------------------------- *
 * Each fills world_map[]/partitions[] and parks the player. Call BEFORE
 * raycast_init() (or re-call init_lights via raycast_init) so the
 * ceiling-fixture grid is laid over the new map. */

/* Pepper outlets across the live world_map's visible wall faces (a wall cell
 * with an open orthogonal neighbour), appending to decals[] until it holds
 * `target` total. Two passes: count candidates, then place every stride-th so
 * they spread across the whole map instead of clustering in the first rows.
 * Each plate sits at the face's grid plane, centred on the cell, receptacle
 * height. This is the hand-map analogue of the procgen placement to come. */
static void place_outlets_fixed(int target) {
    if (num_decals >= target) return;
    int count = 0;
    for (int y = 1; y < MAP_H - 1; y++)
        for (int x = 1; x < MAP_W - 1; x++) {
            if (world_map[y][x] == 0) continue;
            if (world_map[y][x-1] == 0 || world_map[y][x+1] == 0 ||
                world_map[y-1][x] == 0 || world_map[y+1][x] == 0) count++;
        }
    if (count == 0) return;
    int stride = count / (target - num_decals);
    if (stride < 1) stride = 1;

    int seen = 0;
    for (int y = 1; y < MAP_H - 1 && num_decals < target; y++)
        for (int x = 1; x < MAP_W - 1 && num_decals < target; x++) {
            if (world_map[y][x] == 0) continue;
            uint8_t axis; fx_t px, py;
            fx_t cx = ((fx_t)x << FX_SHIFT) + (FX_ONE >> 1);
            fx_t cy = ((fx_t)y << FX_SHIFT) + (FX_ONE >> 1);
            if      (world_map[y][x-1] == 0) { axis = 0; px = (fx_t)x     << FX_SHIFT; py = cy; }
            else if (world_map[y][x+1] == 0) { axis = 0; px = (fx_t)(x+1) << FX_SHIFT; py = cy; }
            else if (world_map[y-1][x] == 0) { axis = 1; px = cx; py = (fx_t)y     << FX_SHIFT; }
            else if (world_map[y+1][x] == 0) { axis = 1; px = cx; py = (fx_t)(y+1) << FX_SHIFT; }
            else continue;
            if ((seen++ % stride) != 0) continue;
            decals[num_decals++] = (decal_t){ px, py, FX(0.20), axis };
        }
}

/* True if (mx,my) is >= 5 cells from every existing partition centre, so newly
 * placed dividers spread out instead of piling up. */
static int partition_clear_of_others(fx_t mx, fx_t my) {
    for (int i = 0; i < num_partitions; i++) {
        fx_t ox = (partitions[i].x1 + partitions[i].x2) >> 1;
        fx_t oy = (partitions[i].y1 + partitions[i].y2) >> 1;
        if (FX_ABS(mx - ox) < FX(5) && FX_ABS(my - oy) < FX(5)) return 0;
    }
    return 1;
}

/* Pepper a few free-standing partition dividers into the live world_map's open
 * areas, appending to partitions[] until it holds `target` total. A candidate
 * needs a fully-open 2-row x (L+2)-col block (both sides of the divider plus a
 * cell past each end) so the divider floats in the room and you can always walk
 * around either end — it never seals a path. Alternating wallpaper style. The
 * hand-map analogue of the procgen partition placement to come. */
static void place_partitions_fixed(int target) {
    const int L = 3;
    uint8_t style = 0;
    /* Horizontal dividers: line y=gy, span cols cx..cx+L. */
    for (int gy = 3; gy < MAP_H - 3 && num_partitions < target; gy++)
        for (int cx = 2; cx < MAP_W - 2 - L && num_partitions < target; cx++) {
            int ok = 1;
            for (int x = cx - 1; x <= cx + L && ok; x++)
                if (world_map[gy-1][x] || world_map[gy][x]) ok = 0;
            if (!ok) continue;
            fx_t mx = ((fx_t)cx << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            fx_t my = (fx_t)gy << FX_SHIFT;
            if (!partition_clear_of_others(mx, my)) continue;
            partitions[num_partitions] = (partition_t){
                (fx_t)cx << FX_SHIFT, my, (fx_t)(cx + L) << FX_SHIFT, my };
            partition_style[num_partitions] = style & 1; style++;
            num_partitions++;
        }
    /* Vertical dividers: line x=gx, span rows cy..cy+L. */
    for (int gx = 3; gx < MAP_W - 3 && num_partitions < target; gx++)
        for (int cy = 2; cy < MAP_H - 2 - L && num_partitions < target; cy++) {
            int ok = 1;
            for (int y = cy - 1; y <= cy + L && ok; y++)
                if (world_map[y][gx-1] || world_map[y][gx]) ok = 0;
            if (!ok) continue;
            fx_t mx = (fx_t)gx << FX_SHIFT;
            fx_t my = ((fx_t)cy << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            if (!partition_clear_of_others(mx, my)) continue;
            partitions[num_partitions] = (partition_t){
                mx, (fx_t)cy << FX_SHIFT, mx, (fx_t)(cy + L) << FX_SHIFT };
            partition_style[num_partitions] = style & 1; style++;
            num_partitions++;
        }
}

/* The hand-tuned 32x32 Backrooms map + its two dividers. */
void raycast_load_fixed(void) {
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            world_map[r][c] = fixed_map[r][c];
    partitions[0] = (partition_t){ FX(22), FX(22), FX(26), FX(22) };
    partitions[1] = (partition_t){ FX(20), FX(11), FX(20), FX(14) };
    num_partitions = 2;
    partition_style[0] = 1; partition_style[1] = 1;   /* both spotted polka-dot */
    /* Pepper several more free-standing dividers through the map's open rooms
     * (non-blocking — each floats with walkable ends). ~10% more wall variety. */
    place_partitions_fixed(8);
    /* All fixed-map dividers are low 3/4-height cubicle partitions you see over
     * (HobbyTown look). They sit in open bands with walls far behind, so the
     * ceiling correctly shows above them — and partial columns draw fewer
     * pixels, lightening the partition-heavy fixed map. */
    for (int i = 0; i < NUM_PARTITIONS_MAX; i++)
        partition_height[i] = (i < num_partitions) ? 192 : 0;
    g_lobby_ceiling = 0;
    /* Wall outlet on the east wall of the spawn corridor (col 17, west face,
     * rows 24-28), ~2 cells ahead and just right of spawn so it reads on the
     * way out. X 0.16 west of the X=17 face so it sits just in front; z=0.20
     * receptacle height (same as the lobby outlet). */
    /* One curated outlet on the spawn-corridor wall (col-17 west face, x=17.0
     * plane, y=26.5 — ~2 cells ahead-right of spawn), then ~11 more peppered
     * across the map's visible wall faces. */
    num_decals = 0;
    decals[num_decals++] = (decal_t){ FX(17.0), FX(26.5), FX(0.20), 0 };
    place_outlets_fixed(12);
    player.x = FX(16.5); player.y = FX(28.5); player.angle = 192;
}

/* Load the tiny 8x8 lobby: the grid box (lobby_map) plus the free-standing
 * wallpaper PARTITION that IS the photo's divider. Spawn (X) sits bottom-
 * west facing north so the divider stands on your right; walk up the west
 * side, across the top, and out the east exit doorway (col 10, rows 2-4) to
 * enter the chosen level. */
void raycast_load_lobby(void) {
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            world_map[r][c] = lobby_map[r][c];
    /* Free-standing wallpaper PARTITION dividers, per the sketch (5x5):
     *  - T-divider top-left: vertical stem (x=3, rows 2-3) + arm (row 3, x3->5)
     *  - entrance wall (row 5) split by a centre gap (cols 3-4) you walk up. */
    partitions[0] = (partition_t){ FX(3), FX(2), FX(3), FX(4) };  /* T stem     */
    partitions[1] = (partition_t){ FX(3), FX(3), FX(4), FX(3) };  /* T arm (1 cell) */
    partitions[2] = (partition_t){ FX(2), FX(6), FX(4), FX(6) };  /* entrance L (depth +1, wall @ y=6) */
    partitions[3] = (partition_t){ FX(5), FX(6), FX(7), FX(6) };  /* entrance R */
    num_partitions = 4;
    /* T-stem, T-arm, entrance-L = spotted olive wallpaper; entrance-R (the
     * outlet wall) = chevron, same as the main walls (per the reference). */
    partition_style[0] = 1; partition_style[1] = 1;
    partition_style[2] = 1; partition_style[3] = 0;
    /* T-divider per the HobbyTown reference: the N/S stem (partitions[0],
     * vertical) runs full height to the ceiling; the E/W arm (partitions[1],
     * horizontal) is a low 3/4-height cubicle divider you see over. Entrance
     * walls stay full (room boundary / outlet wall). */
    partition_height[0] = 0;   partition_height[1] = 192;   /* stem full, arm 3/4 */
    partition_height[2] = 0;   partition_height[3] = 0;
    g_lobby_ceiling = 1;                  /* hand-authored fluorescent runs */
    /* Outlet on entrance-R's south face (the photo's right-hand partition),
     * low and right-of-center in the spawn/menu view. Placed FX(0.16) south
     * of the y=5 wall line so it sits just in front of the face, not inside
     * it; z=0.15 = standard receptacle height up the 1.0-tall wall. */
    /* Outlet embedded in entrance-R's south face (the partition line y=6),
     * centred at x=5.33. axis 1 = the wall runs along X, so the plate spans X
     * on the y=6 plane. z=0.20 receptacle height. */
    decals[0] = (decal_t){ FX(5.33), FX(6.0), FX(0.20), 1 };
    num_decals = 1;
    player.x = FX(5.0); player.y = FX(7.6); player.angle = 184;
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

/* Returns 1 if (px, py) would intersect a SOLID standup (the cardboard
 * cutout), treated as a small axis-aligned box + player radius. Silhouette
 * "watcher" standups stay intangible — they're meant to be a glimpse, not a
 * wall. Makes the cutout a real free-standing obstacle you bump into. */
#define STANDUP_HALF_THICK FX(0.12)   /* slim box so a 1-cell corridor stays squeezable past */
static int standup_collides(fx_t px, fx_t py) {
    fx_t margin = STANDUP_HALF_THICK + PLAYER_RADIUS;
    for (int i = 0; i < NUM_STANDUPS; i++) {
        if (standups[i].silhouette) continue;
        fx_t dx = px - standups[i].x;
        fx_t dy = py - standups[i].y;
        if (dx > -margin && dx < margin && dy > -margin && dy < margin) return 1;
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
    if (standup_collides(px, py))   return 0;
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

/* Eye height (8.8 fraction of room height), eased toward STAND/CROUCH as
 * the player holds X. STAND_EYE/CROUCH_EYE are #defined up by raycast_init. */
static int     eye_smooth = STAND_EYE;
/* Transient look-down "head dip" while standing up out of a crouch — glances
 * at the floor as the eye climbs through the lower half of the rise, then
 * eases back to level by mid-stand. Added as extra pitch in raycast_render. */
static int     standup_dip = 0;
#define STANDUP_DIP 40         /* peak look-down pixels while rising; 0 disables */

/* Read controller, advance player by one frame. Axis-separated collision
 * gives natural sliding along walls. */
/* pad is read ONCE per frame by the caller (the loop already reads it for
 * menu/metrics) and passed in — the old self-read here was a second 68K
 * HwMdReadPad round-trip per frame, pure overhead. */
void player_update(uint16_t pad) {
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

    /* Hold X to crawl — ease the eye down toward the floor (the wall draw
     * reads SHARED_UC->eye_h). Variable eye height now; partial-height
     * crawl-under walls come later. */
    /* Crouch = A+B held together. We deliberately do NOT use the 6-button X
     * button: emulators routinely bind X to the same key as D-pad Left (so
     * X-crouch fires on Left), and they report a 6-button type, so we can't
     * distinguish them from a real MiSTer 6-button pad to gate it. A+B is
     * collision-free on every pad. Trade-off: holding sprint+strafe now
     * crouch-strafes instead of sprint-strafing. */
    int crouching = (pad & SEGA_CTRL_A) && (pad & SEGA_CTRL_B);
    {
        int target = crouching ? CROUCH_EYE : STAND_EYE;
        int d = target - eye_smooth;
        /* Drop into crouch 2× faster than we rise: d<0 (eye falling toward the
         * floor) eases at 50%/frame, d>0 (standing back up) stays at 25%. */
        if (d > -4 && d < 4) eye_smooth = target;
        else eye_smooth += (d < 0) ? (d >> 1) : (d >> 2);
        SHARED_UC->eye_h = (uint8_t)eye_smooth;

        /* Stand-up floor-glance: while rising (not crouching) through the
         * lower 3/4 of the climb, aim the dip toward the floor; once we pass
         * 75% of the rise the target drops to 0 so the view eases back to
         * level — mimicking how you glance at the ground as you get up. */
        int dip_target = (!crouching && eye_smooth > CROUCH_EYE + 8
                          && eye_smooth < (STAND_EYE * 3 + CROUCH_EYE) / 4)
                         ? STANDUP_DIP : 0;
        /* Dip toward the floor fast (>>1), but ease the gaze back to level at
         * HALF that speed (>>2) so it reads as the player taking a moment to
         * collect before settling into the standing perspective. */
        int dd = dip_target - standup_dip;
        standup_dip += (dd > 0) ? (dd >> 1) : (dd >> 2);
    }

    /* Hold A to run — bumps walk speed and turn rate together so you can
     * quickly reorient while sprinting. No running while crouched; crawling
     * is slow. */
    int sprinting = !crouching && (pad & SEGA_CTRL_A) != 0;
    fx_t walk = crouching ? FX(0.04) : (sprinting ? FX(0.15) : FX(0.08));
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
/* The outlet is no longer a separate object — it is painted INTO the wall
 * column during the wall raster (see the wall-embedded outlet pass at the end
 * of raycast_draw_walls). decals[] just carries where/how big it is; there is
 * no billboard pass any more. */

static void draw_standups(uint8_t *fb,
                          fx_t dirX, fx_t dirY, fx_t planeX, fx_t planeY) {
    fx_t det = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
    if (det == 0) return;
    fx_t inv_det = fx_div_hw(FX_ONE, det);   /* det == -0.66 const; bounded */
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
        if (transformY < FX(0.2))     continue;     /* behind / right on top of it */
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

        /* Floor row at this distance: horizon_y + (focal·eyeH)/transformY.
         * horizon_y shifts with pitch; the focal·eyeH term drops with the
         * crouch so the standup's feet ride the floor up toward the horizon
         * in lockstep with the wall floor-edge (no floating). */
        int floor_y = horizon_y
                    + (int)(((int32_t)((SCREEN_H * (int)SHARED_UC->eye_h) >> 8) << FX_SHIFT) / transformY);
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
 * Runs ONCE on the primary before MARS_SYS_COMM4 wakes the secondary; both
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
        int face_start = n;

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
        /* Tag every face of this partition with its wallpaper style. */
        for (int k = face_start; k < n; k++) {
            PFACE_STYLE(k)  = partition_style[i];
            PFACE_HEIGHT(k) = partition_height[i];
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
    fx_t inv_det = fx_div_hw(FX_ONE, det);   /* det == -0.66 const; bounded */
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
            /* Ceiling offset uses focal·(1-eyeH): as the eye drops the
             * ceiling rises away, so the light tile climbs in lockstep with
             * the wall ceiling-edge. */
            int yoff = (int)(((int32_t)((SCREEN_H * (256 - (int)SHARED_UC->eye_h)) >> 8) << FX_SHIFT) / tY);
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

/* Drop-ceiling grid pass — called from the secondary SH-2's dispatch loop
 * after the primary writes the player snapshot and signals
 * MARS_CMD_CEILING on COMM4. Primary can also call this directly during
 * single-CPU testing; in both cases the function reads player state
 * from SHARED_UC (cache-through alias) so the secondary sees the primary's
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
    /* Ceiling depth scales with focal·(1-eyeH): crouch -> ceiling recedes,
     * grid lines spread, in step with the wall ceiling-edge. Both CPUs read
     * eye_h here, so the secondary's column half stays aligned. */
    const int focal_const = (SCREEN_H * (256 - (int)SHARED_UC->eye_h)) >> 8;
    /* Same trick as the carpet: rebase row_color sampling so the
     * ceiling fog gradient and grid-line shade follow the shifted
     * horizon instead of staying glued to absolute screen Y. The crouch
     * term (matching raycast_clear_half) fogs the ceiling sooner as it
     * looms, in step with the floor brightening. */
    int sample_bias = (SCREEN_H / 2 - horizon_y)
                    + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);

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
        /* Clamp to the fog midpoint so the crouch shift can't pull floor
         * colors into the ceiling sampling — that bypassed the fog-skip below
         * and drew dense grid lines near the horizon (the speckle band). */
        if (sy > SCREEN_H / 2) sy = SCREEN_H / 2;
        if (sy < 0)            sy = 0;
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
 * from the shared snapshot so the secondary can run it alongside the
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
    /* Floor depth scales with focal·eyeH: crouch -> the carpet comes up
     * close, stains land at the nearer world distance, in step with the
     * wall floor-edge. Both CPUs read eye_h, keeping the secondary's half aligned. */
    const int focal_const = (SCREEN_H * (int)SHARED_UC->eye_h) >> 8;
    /* sample_bias rebases row_color sampling so its color gradient
     * (and the stain-LOD derived from base_shade) travels with the
     * shifted horizon. Without this the gradient stayed glued to
     * absolute Y while the geometry math moved — visible as static
     * stain density bands and a non-perspective color band when
     * tilting up or down. The crouch term (matching raycast_clear_half)
     * slides the carpet tone gradient with eye height: lower eye => more
     * bright carpet before the fade, geometry untouched. */
    int geo_bias    = SCREEN_H / 2 - horizon_y;        /* unshifted: skip + LOD */
    int sample_bias = geo_bias + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);
    for (int y = horizon_y + 1; y < SCREEN_H; y++) {
        if (y < 0) continue;        /* extreme positive pitch */
        /* Skip + LOD key off the geometric (unshifted) distance so far rows
         * stay fog-skipped. The crouch color shift only brightens what we DO
         * draw — it must NOT un-skip the aliased near-horizon rows (that was
         * the noisy "broken horizon" band). */
        int gy = y + geo_bias;
        if (gy < 0)         gy = 0;
        if (gy >= SCREEN_H) gy = SCREEN_H - 1;
        int geo_shade = row_color[gy] - FLOOR_BASE;
        if (geo_shade >= SHADE_LEVELS - 2) continue;
        int sy = y + sample_bias;
        if (sy < 0)         sy = 0;
        if (sy >= SCREEN_H) sy = SCREEN_H - 1;
        int base_shade = row_color[sy] - FLOOR_BASE;   /* shifted: stain brightness */

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
        if      (geo_shade >= 8) x_step = 16;
        else if (geo_shade >= 4) x_step = 8;
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
 * column range [col_start, col_end) lets the primary and secondary divide
 * the screen — primary does [0, SCREEN_W/2), secondary does [SCREEN_W/2,
 * SCREEN_W). Writes the per-column z-buffer (WALL_DIST) through the
 * cache-through alias so the sprite passes on primary see secondary's
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
     * Sticking with the primary 0..SCREEN_W/2 / secondary SCREEN_W/2..
     * SCREEN_W static split until we have a low-contention work-
     * stealing pattern (e.g. primary pre-chunking into 8-column
     * batches and the secondary just reading a written-once index). */
    /* Load pitch once — read via cache-through alias so primary's
     * latest write is visible. Walls center on the shifted horizon. */
    int horizon_y = SCREEN_H / 2 - (int)SHARED_UC->pitch_y;
    /* Eye height once: splits the wall column about the horizon. 128 =
     * standing (symmetric, lineHeight/2 below); lower drops the eye toward
     * the floor (crawling) so the floor sits close and the ceiling looms. */
    int eye_h = (int)SHARED_UC->eye_h;

    /* ── Partition screen-span cull ─────────────────────────────────────────
     * Project each visible partition face's two endpoints to screen columns
     * once, so the per-column intersection loop below can skip any face whose
     * span doesn't cover the current column — turning partition cost from
     * full-screen × faces into just-the-columns-they-occupy. This is what makes
     * partitions closer to first-class vs grid walls. An endpoint at/behind the
     * camera plane can't be projected, so that face falls back to the full
     * column range (correctness over the optimization for that one face). */
    int pcolmin[MAX_PARTITION_FACES], pcolmax[MAX_PARTITION_FACES];
    {
        fx_t pdet = FX_MUL(planeX, dirY) - FX_MUL(dirX, planeY);
        fx_t pinv = (pdet != 0) ? fx_div_hw(FX_ONE, pdet) : 0;
        int nf = PFACE_COUNT;
        for (int fi = 0; fi < nf; fi++) {
            fx_t sax = PFACE_AX(fi) - px, say = PFACE_AY(fi) - py;
            fx_t sbx = PFACE_BX(fi) - px, sby = PFACE_BY(fi) - py;
            fx_t tya = FX_MUL(pinv, FX_MUL(-planeY, sax) + FX_MUL(planeX, say));
            fx_t tyb = FX_MUL(pinv, FX_MUL(-planeY, sbx) + FX_MUL(planeX, sby));
            if (tya < FX(0.05) || tyb < FX(0.05)) {        /* an end is at/behind us */
                pcolmin[fi] = col_start; pcolmax[fi] = col_end - 1;
                continue;
            }
            fx_t txa = FX_MUL(pinv, FX_MUL(dirY, sax) - FX_MUL(dirX, say));
            fx_t txb = FX_MUL(pinv, FX_MUL(dirY, sbx) - FX_MUL(dirX, sby));
            /* tya/tyb >= 0.05 (guarded above) bounds the quotient, so the
             * hardware divide is safe and replaces the last software FX_DIV in
             * the wall pass. */
            int xa = (SCREEN_W >> 1)
                   + (int)(((int64_t)(SCREEN_W >> 1) * fx_div_hw(txa, tya)) >> FX_SHIFT);
            int xb = (SCREEN_W >> 1)
                   + (int)(((int64_t)(SCREEN_W >> 1) * fx_div_hw(txb, tyb)) >> FX_SHIFT);
            int lo = (xa < xb ? xa : xb) - 1;            /* 1-col margin each side */
            int hi = (xa > xb ? xa : xb) + 1;
            if (lo < col_start) lo = col_start;
            if (hi >= col_end)  hi = col_end - 1;
            pcolmin[fi] = lo; pcolmax[fi] = hi;          /* lo>hi ⇒ off this half, skipped */
        }
    }

    for (int col = col_start; col < col_end; col++) {
        WALL_DIST(col) = 0x7FFFFFFF;
        fx_t cameraX = cameraX_table[col];
        fx_t rayDirX = dirX + FX_MUL(planeX, cameraX);
        fx_t rayDirY = dirY + FX_MUL(planeY, cameraX);

        int mapX = FX_INT(px);
        int mapY = FX_INT(py);

        /* deltaDist = 1/|rayDir| in 16.16. Hardware DIVU (fx_div_hw, ~39 cyc)
         * vs the libgcc int64 FX_DIV (~200 cyc) — runs twice per column. The
         * |rayDir| < 4 guard (was == 0) keeps fx_div_hw's quotient inside 31
         * bits (it has no overflow saturation); a sub-0.00006 ray is treated as
         * axis-parallel ("never crosses"), same intent as the old zero guard. */
        fx_t deltaDistX = (FX_ABS(rayDirX) < 4) ? 0x7FFFFFFF
                                                : fx_div_hw(FX_ONE, FX_ABS(rayDirX));
        fx_t deltaDistY = (FX_ABS(rayDirY) < 4) ? 0x7FFFFFFF
                                                : fx_div_hw(FX_ONE, FX_ABS(rayDirY));

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
        int hit_cell = 0;          /* world_map value at the hit (2 = black exit) */
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
            if (world_map[mapY][mapX]) { hit = 1; hit_cell = world_map[mapY][mapX]; break; }
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
        int  partition_hit       = 0;   /* a FULL-height partition is the background */
        int  part_style          = 0;
        int  part_height         = 0;   /* background is always full height (0) */
        fx_t partition_wallhit_w = 0;
        /* Foreground = nearest PARTIAL-height partition. Drawn as a band OVER
         * the background (solid wall / full partition) after the main column
         * draw, so a surface behind it (e.g. the lobby T-stem) shows above it.
         * Same renderer the crawl-under gap will use. */
        int  fg_hit = 0, fg_style = 0, fg_height = 0, fg_side = 0;
        fx_t fg_t = 0, fg_wallhit = 0;
        int  n_faces = PFACE_COUNT;
        for (int fi = 0; fi < n_faces; fi++) {
            /* Screen-span cull: skip the divide entirely for faces this column
             * can't possibly cross (precomputed above). */
            if (col < pcolmin[fi] || col > pcolmax[fi]) continue;
            fx_t ax = PFACE_AX(fi);
            fx_t ay = PFACE_AY(fi);
            fx_t bx = PFACE_BX(fi);
            fx_t by = PFACE_BY(fi);
            fx_t dxs = bx - ax;
            fx_t dys = by - ay;
            fx_t cx  = ax - px;
            fx_t cy  = ay - py;

            fx_t denom = FX_MUL(rayDirY, dxs) - FX_MUL(rayDirX, dys);
            if (denom < 128 && denom > -128) continue;   /* edge-on sliver */

            fx_t t_num = FX_MUL(cy, dxs) - FX_MUL(cx, dys);
            fx_t t = fx_div_hw(t_num, denom);
            if (t <= FX(0.1)) continue;
            int fh = PFACE_HEIGHT(fi);
            if (fh == 0) {
                if (t >= perpDist) continue;          /* full: behind the background */
            } else {
                if (fg_hit && t >= fg_t) continue;    /* partial: not the closest one */
            }
            fx_t s_num = FX_MUL(rayDirX, cy) - FX_MUL(rayDirY, cx);
            fx_t s = fx_div_hw(s_num, denom);
            if (s < 0 || s > FX_ONE) continue;
            fx_t wh = PFACE_UA(fi) + FX_MUL(s, PFACE_UB(fi) - PFACE_UA(fi));
            int sd = (dxs == 0) ? 0 : 1;   /* vertical face = E/W (X-side), horizontal = N/S */
            if (fh == 0) {
                perpDist = t; partition_wallhit_w = wh; partition_hit = 1;
                part_style = PFACE_STYLE(fi); side = sd;
            } else {
                fg_hit = 1; fg_t = t; fg_wallhit = wh;
                fg_style = PFACE_STYLE(fi); fg_height = fh; fg_side = sd;
            }
        }
        /* Partial partition only shows if in front of the final background. */
        if (fg_hit && fg_t >= perpDist) fg_hit = 0;
        int spotted = partition_hit && part_style;

        /* Nothing in range — leave the ceiling/floor earlier passes painted. */
        if (!hit && !partition_hit && !fg_hit) continue;

        /* See-over decision. The slow per-pixel overlay is only needed when the
         * background pokes ABOVE the partial (the lobby T-stem behind the low
         * arm). For every free-standing divider — background a far wall the
         * band fully covers — PROMOTE the partial to the fast MAIN draw path
         * (asm inner loop, full shade) and skip the overlay entirely. A point at
         * world height h/256 at distance d sits above the horizon ~(h-eye)/d, so
         * the full (h=256) background pokes above the partial (h=fg_height) iff
         * (256-eye)*fg_t > (fg_height-eye)*perpDist. */
        if (fg_hit) {
            int bg_pokes = (hit || partition_hit) &&
                ((int64_t)(256 - eye_h) * fg_t
                 > (int64_t)(fg_height - eye_h) * perpDist);
            if (!bg_pokes) {
                perpDist = fg_t; partition_wallhit_w = fg_wallhit; partition_hit = 1;
                part_style = fg_style; part_height = fg_height; side = fg_side;
                spotted = part_style;     /* partition_hit is now 1 */
                fg_hit = 0;               /* drawn by the fast main path; no overlay */
            }
        }

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
        /* Subtle directional form cue: faces whose normal runs N/S (side 1)
         * are one shade darker than E/W faces, so a wall or partition reads
         * with form — you can see where it turns a corner — instead of flat. */
        if (side) wall_shade += SIDE_SHADE;
        if (wall_shade < 0) wall_shade = 0;
        /* Final clamp so the baseboard color lookup (WALL_BASE +
         * wall_shade) and any downstream shade-index user can't walk
         * off the end of the wall palette into FLOOR_BASE — that bug
         * was painting distant baseboards as bright carpet yellow. */
        if (wall_shade > SHADE_LEVELS - 1) wall_shade = SHADE_LEVELS - 1;

        /* Light boost: a wall/partition next to a ceiling fixture reads
         * brighter. Sample the hit cell's precomputed light level and pull
         * the shade toward bright. Partitions live between cells, so derive
         * the cell from the actual hit point; solid walls use the DDA cell. */
        {
            int litX, litY;
            if (partition_hit) {
                litX = FX_INT(px + FX_MUL(perpDist, rayDirX));
                litY = FX_INT(py + FX_MUL(perpDist, rayDirY));
                if ((unsigned)litX >= (unsigned)MAP_W ||
                    (unsigned)litY >= (unsigned)MAP_H) { litX = mapX; litY = mapY; }
            } else {
                litX = mapX; litY = mapY;
            }
            int lit = CELL_LIGHT(litY, litX);
            if (lit) {
                /* A lit surface resists distance fog: cap how dark it gets
                 * (more headroom the more lit). Near surfaces are already
                 * below the cap; far-but-lit ones — the rear T-divider —
                 * stay mid-bright and readable instead of fogging out. */
                int cap = LIT_FOG_CAP - (lit - 1) * 2;
                if (wall_shade > cap) wall_shade = cap;
            }
        }

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
        if (spotted) {
            /* Spotted partitions use the olive polka-dot wallpaper. */
            tex_data = (const uint8_t *)partition_tex_ram;   /* SDRAM-staged */
            tex_w    = PARTITION_TEX_WIDTH;
            tex_h    = PARTITION_TEX_HEIGHT;
            tile_x   = PARTITION_TILE_X;
            tile_y   = PARTITION_TILE_Y;
        } else if (perpDist < WALL_LOD_THRESHOLD) {
            tex_data = (const uint8_t *)wall_tex_hi_ram;     /* SDRAM-staged */
            tex_w    = WALL_TEX_HI_WIDTH;
            tex_h    = WALL_TEX_HI_HEIGHT;
            tile_x   = WALL_TILE_HI_X;
            tile_y   = WALL_TILE_HI_Y;
        } else {
            tex_data = (const uint8_t *)wall_tex_ram;        /* SDRAM-staged */
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

        int wall_bot  = horizon_y + ((lineHeight * eye_h) >> 8);
        /* Partial-height partition: a low divider anchored at the floor. Keep
         * the floor line (wall_bot) but shorten the drawn column so the ceiling
         * shows above it; the texture maps over the reduced band. part_height is
         * a fraction*256 (0 = full). */
        int draw_lineHeight = (part_height)
                            ? ((lineHeight * part_height) >> 8)
                            : lineHeight;
        int wall_top  = wall_bot - draw_lineHeight;
        int drawStart = wall_top < 0 ? 0 : wall_top;
        int drawEnd   = wall_bot >= SCREEN_H ? SCREEN_H - 1 : wall_bot;

        /* Black-exit cell (world_map == 2): a solid void wall — the dark
         * doorway you walk through to leave the lobby. Fill the column
         * black and skip all texture/baseboard work. Partitions still
         * occlude it (they override perpDist above). */
        if (hit_cell == 2 && !partition_hit) {
            uint8_t *pb = (uint8_t *)fb + col + drawStart * SCREEN_W;
            for (int y = drawStart; y <= drawEnd; y++) { *pb = 0; pb += SCREEN_W; }
            continue;
        }

        /* DIVU latency hide #2: start tex_step = (tex_h*tile_y
         * << 16) / lineHeight, then do detail_factor + shade_lut in
         * parallel. The shade_lut loop alone is ~256 cycles, far
         * exceeding the 39-cycle DIVU latency. */
        divu_start_u32((uint32_t)((tex_h * tile_y) << FX_SHIFT),
                       (uint32_t)draw_lineHeight);   /* texture maps over the drawn band */

        int detail_factor;
        if (spotted) {
            /* Subtle up close, fading to nothing with distance — you only
             * notice the dots on the near partition; far ones go plain yellow. */
            if (perpDist < FX(2)) {
                detail_factor = PARTITION_DETAIL;
            } else if (perpDist < FX(3.5)) {
                detail_factor = (int)(((FX(3.5) - perpDist) * PARTITION_DETAIL) / FX(1.5));
                if (detail_factor < 0) detail_factor = 0;
            } else {
                detail_factor = 0;
            }
        } else if (perpDist < FX(2)) {
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
        /* Both chevron walls and the spotted partitions use the yellow
         * WALL_BASE ramp — same color scheme, only the motif differs (the
         * spotted partition just swaps the chevron texture for the dots). */
        uint8_t lut_base = WALL_BASE;
        for (int v = 0; v < 5; v++) {
            int pattern = (v * detail_factor) >> 4;
            int s = wall_shade + pattern;
            if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
            lut5[v] = (uint8_t)(lut_base + s);
        }
        uint8_t shade_lut[WALL_TEX_HI_HEIGHT];
        if (detail_factor) {   /* uniform when faded — skip the build, flat-fill below */
            for (int ty = 0; ty < tex_h; ty++) shade_lut[ty] = lut5[wall_col[ty]];
        }

        /* Baseboard molding: bottom ~3% of the wall in world space gets
         * a darker flat-shade band, the iconic Backrooms wood-trim look.
         * Anchored to wall_bot (unclipped) so the strip sits at the same
         * world height regardless of whether the wall extends off-screen.
         * Split the inner pixel loop in two so the per-pixel hot path
         * stays branch-free: wall portion runs the textured loop, then
         * the baseboard portion writes the flat color. */
        int base_h = draw_lineHeight >> 5;
        if (base_h < 1) base_h = 1;
        int base_y = wall_bot - base_h;
        int wall_end;
        if      (base_y > drawEnd)    wall_end = drawEnd;
        else if (base_y <= drawStart) wall_end = drawStart - 1;
        else                          wall_end = base_y - 1;
        /* Molding color = the yellow wall background (no chevron/dot offset),
         * the same on the main walls and the spotted partitions. */
        uint8_t mold_base  = WALL_BASE;
        uint8_t base_color = (uint8_t)(mold_base + wall_shade);
        /* 1-pixel darker line at the wall/molding boundary suggests
         * the shadow gap of a recessed baseboard — a small depth cue
         * that reads as the molding standing slightly proud of the
         * wall. Two shades darker than the molding base. */
        int shadow_shade = wall_shade + 2;
        if (shadow_shade > SHADE_LEVELS - 1) shadow_shade = SHADE_LEVELS - 1;
        uint8_t shadow_color = (uint8_t)(mold_base + shadow_shade);

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
        if (total > 0 && detail_factor == 0) {
            /* Faded distance: the pattern adds nothing, so the whole wall
             * column is one flat color. Skip the shade_lut build (above) and
             * the per-pixel texture sampling — just fill. Advances p like the
             * textured path so the baseboard below lines up. Big win for far
             * walls and (64-tall) spotted partitions. */
            uint8_t flat = lut5[0];
            for (int k = 0; k < total; k++) { *p = flat; p += SCREEN_W; }
        } else if (total > 0) {
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

        /* ── Wall-embedded outlet ───────────────────────────────────────────
         * Paint the outlet plate INTO this finished wall column when the hit
         * point lands on a decal's footprint. It draws at the wall's own depth,
         * so it's genuinely part of the surface (correct perspective + occlusion)
         * instead of a billboard. Cheap: only runs when the map placed a decal,
         * and the footprint test rejects almost every column with one compare. */
        if (num_decals > 0) {
            fx_t hx = px + FX_MUL(perpDist, rayDirX);
            fx_t hy = py + FX_MUL(perpDist, rayDirY);
            /* Plane tolerance must exceed the partition HALF_THICK (0.15) so a
             * decal placed on the partition CENTRE line still matches its thick
             * face (the visible face sits +/-0.15 off centre). Solid walls hit
             * exact integer planes, so this wide band never false-matches them
             * (the nearest other plane is a whole cell away). */
            for (int d = 0; d < num_decals; d++) {
                fx_t along;
                if (decals[d].axis) {                 /* wall plane = Y, plate spans X */
                    if (FX_ABS(hy - decals[d].y) > FX(0.2)) continue;
                    along = hx - (decals[d].x - DECAL_OUTLET_HW);
                } else {                              /* wall plane = X, plate spans Y */
                    if (FX_ABS(hx - decals[d].x) > FX(0.2)) continue;
                    along = hy - (decals[d].y - DECAL_OUTLET_HW);
                }
                if (along < 0 || along > 2 * DECAL_OUTLET_HW) continue;
                int otx = (int)(((int64_t)along * OUTLET_TEX_WIDTH)
                                / (2 * DECAL_OUTLET_HW));
                if (otx < 0) otx = 0;
                else if (otx >= OUTLET_TEX_WIDTH) otx = OUTLET_TEX_WIDTH - 1;

                /* Vertical band: centre at height fraction z, height
                 * DECAL_OUTLET_H, projected through lineHeight. wall_bot is the
                 * floor line (fraction 0); up the wall subtracts lineHeight. */
                int oc = wall_bot - (int)(((int64_t)lineHeight * decals[d].z) >> FX_SHIFT);
                int oh = (int)(((int64_t)lineHeight * DECAL_OUTLET_H) >> (FX_SHIFT + 1));
                if (oh < 1) oh = 1;
                int oy0  = oc - oh, oy1 = oc + oh;
                int span = oy1 - oy0;
                int ylo  = oy0 < drawStart ? drawStart : oy0;
                int yhi  = oy1 > drawEnd   ? drawEnd   : oy1;
                uint8_t *po = (uint8_t *)fb + col + ylo * SCREEN_W;
                /* Shade the plate with the wall it's mounted on: subtract the
                 * wall's fog/light shade (0..15, already folded with cell_light)
                 * scaled into the 5-bucket outlet ramp, so a far or unlit outlet
                 * darkens with its wall instead of staying bright in the fog. */
                int oshade = (wall_shade * 5) >> 4;
                for (int yy = ylo; yy <= yhi; yy++) {
                    int oty = ((yy - oy0) * OUTLET_TEX_HEIGHT) / span;
                    if ((unsigned)oty < (unsigned)OUTLET_TEX_HEIGHT) {
                        int ob = (int)outlet_tex[oty][otx] - oshade;
                        if (ob < 0) ob = 0;
                        *po = (uint8_t)(OUTLET_BASE + ob);
                    }
                    po += SCREEN_W;
                }
                break;   /* one outlet per column */
            }
        }

        /* ── Partial-height partition overlay ───────────────────────────────
         * Draw the foreground partial partition as a textured band OVER the
         * background just drawn, so a wall/stem behind it shows above it. It's
         * validated to be in front (fg_t < perpDist), so it draws over the
         * column unconditionally. Replicates the wall pass's shade/texture/
         * baseboard so it reads identical — just shorter. Only reached for the
         * see-over case (lobby T-stem); free-standing dividers were promoted to
         * the fast main path above (fg_hit cleared). */
        if (fg_hit) {
            /* The band is the nearest solid surface in this column, so the
             * sprite z-buffer must read its depth — otherwise the ceiling
             * lights (drawn later, z-tested per column) bleed through it. */
            WALL_DIST(col) = fg_t;
            int flh  = (int)divu_u32((uint32_t)(SCREEN_H << FX_SHIFT), (uint32_t)fg_t);
            int fdlh = (flh * fg_height) >> 8;          /* band height in px */
            if (fdlh > 0) {
                int fbot = horizon_y + ((flh * eye_h) >> 8);
                int ftop = fbot - fdlh;
                int fds  = ftop < 0 ? 0 : ftop;
                int fde  = fbot >= SCREEN_H ? SCREEN_H - 1 : fbot;
                /* Shade: distance ramp + uniform, then cell-light cap. */
                int fsh;
                if (fg_t < FX(2.5)) fsh = (int)((fg_t * 2) / FX(2.5));
                else { fx_t past = fg_t - FX(2.5); fx_t span = FOG_RAMP_DIST - FX(2.5);
                       fsh = 2 + (int)((past * 13) / span); }
                if (fsh > SHADE_LEVELS - 1) fsh = SHADE_LEVELS - 1;
                fsh += 1;
                if (fsh > SHADE_LEVELS - 1) fsh = SHADE_LEVELS - 1;
                {
                    int lx = FX_INT(px + FX_MUL(fg_t, rayDirX));
                    int ly = FX_INT(py + FX_MUL(fg_t, rayDirY));
                    if ((unsigned)lx < (unsigned)MAP_W && (unsigned)ly < (unsigned)MAP_H) {
                        int lit = CELL_LIGHT(ly, lx);
                        if (lit) { int cap = LIT_FOG_CAP - (lit - 1) * 2; if (fsh > cap) fsh = cap; }
                    }
                }
                /* Texture + detail (spotted dots fade with distance). */
                const uint8_t *ftex; int ftw, fth, ftlx, ftly, fdetail;
                if (fg_style) {
                    ftex = (const uint8_t *)partition_tex_ram;
                    ftw = PARTITION_TEX_WIDTH;  fth = PARTITION_TEX_HEIGHT;
                    ftlx = PARTITION_TILE_X;    ftly = PARTITION_TILE_Y;
                    if (fg_t < FX(2))        fdetail = PARTITION_DETAIL;
                    else if (fg_t < FX(3.5)) { fdetail = (int)(((FX(3.5) - fg_t) * PARTITION_DETAIL) / FX(1.5)); if (fdetail < 0) fdetail = 0; }
                    else                     fdetail = 0;
                } else if (fg_t < WALL_LOD_THRESHOLD) {
                    ftex = (const uint8_t *)wall_tex_hi_ram;
                    ftw = WALL_TEX_HI_WIDTH;    fth = WALL_TEX_HI_HEIGHT;
                    ftlx = WALL_TILE_HI_X;      ftly = WALL_TILE_HI_Y; fdetail = 0;
                } else {
                    ftex = (const uint8_t *)wall_tex_ram;
                    ftw = WALL_TEX_WIDTH;       fth = WALL_TEX_HEIGHT;
                    ftlx = WALL_TILE_X;         ftly = WALL_TILE_Y;    fdetail = 0;
                }
                fx_t fwh = fg_wallhit - ((fx_t)FX_INT(fg_wallhit) << FX_SHIFT);
                int ftexX = (int)(((uint32_t)fwh * (uint32_t)(ftw * ftlx)) >> FX_SHIFT) & (ftw - 1);
                const uint8_t *fcol = ftex + ftexX * fth;
                uint8_t flut[5];
                for (int v = 0; v < 5; v++) {
                    int s = fsh + ((v * fdetail) >> 4);
                    if (s >= SHADE_LEVELS) s = SHADE_LEVELS - 1;
                    flut[v] = (uint8_t)(WALL_BASE + s);
                }
                int fbase_h  = fdlh >> 5; if (fbase_h < 1) fbase_h = 1;
                int ftex_end = (fbot - fbase_h) - 1;     /* texture above, molding below */
                if (ftex_end > fde) ftex_end = fde;
                int fmask = fth - 1;
                fx_t fstep = ((fx_t)(fth * ftly) << FX_SHIFT) / fdlh;
                fx_t fpos  = (fx_t)(fds - ftop) * fstep;
                uint8_t *fp = (uint8_t *)fb + col + fds * SCREEN_W;
                int y = fds;
                for (; y <= ftex_end; y++) {
                    *fp = flut[fcol[(fpos >> FX_SHIFT) & fmask]];
                    fp += SCREEN_W; fpos += fstep;
                }
                int fshadow = fsh + 2; if (fshadow > SHADE_LEVELS - 1) fshadow = SHADE_LEVELS - 1;
                if (y <= fde) { *fp = (uint8_t)(WALL_BASE + fshadow); fp += SCREEN_W; y++; }
                uint8_t fmold = (uint8_t)(WALL_BASE + fsh);
                for (; y <= fde; y++) { *fp = fmold; fp += SCREEN_W; }
            }
        }
    }
}

/* The other tex_pos reference in this file is in the old fixed-split
 * placeholder. Compiler dead-code-eliminates it once raycast_render
 * stops calling fixed-range walls. */

/* Profile counters. Both written by raycast_render, read by m_main.c
 * for the on-screen overlay. half = primary's parallel work
 * (clear+ceiling+carpet+walls of its column range); idle = time
 * spent spinning on the secondary-done sync after that. */
volatile uint16_t prof_primary_idle_ticks = 0;
volatile uint16_t prof_primary_half_ticks = 0;
/* Per-pass FRT breakdown of the primary's half (clear/ceiling/carpet/walls). */
volatile uint16_t prof_pass_clear = 0, prof_pass_ceil = 0,
                  prof_pass_carpet = 0, prof_pass_walls = 0;
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
    /* Crouch slides the row_color gradient UP — the SAME shift the look-pitch
     * applies via sample_bias, just decoupled from horizon_y so the geometry
     * stays flat to the player. Lower eye => more bright carpet before the
     * fade, the fade-point creeps toward the horizon (and the ceiling fogs
     * sooner as it looms). Both CPUs run this on their half, so the secondary
     * matches. The carpet and ceiling-grid passes apply the identical term so
     * the whole tone gradient travels together. */
    int sample_bias = (SCREEN_H / 2 - horizon_y)
                    + CROUCH_GRAD_SHIFT(SHARED_UC->eye_h);
    for (int y = 0; y < SCREEN_H; y++) {
        int sy = y + sample_bias;
        /* Ceiling rows must never sample floor colors: the crouch shift would
         * otherwise pull floor (mustard) up above the horizon — the bleed band
         * the ceiling-grid then drew dense lines into. Clamp the ceiling side
         * to the fog midpoint; the floor side keeps the full shift. */
        if (y <= horizon_y && sy > SCREEN_H / 2) sy = SCREEN_H / 2;
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

    /* Snapshot player state for the secondary to read via cache-through.
     * Must land before COMM4 wakes the secondary so it sees the new frame. */
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
    /* Stand-up head dip — transient look-down while rising out of a crouch
     * (positive = look down). Eased in player_update; 0 at rest. */
    pitch_combined += standup_dip;
    if (pitch_combined > 127)  pitch_combined = 127;
    if (pitch_combined < -128) pitch_combined = -128;
    SHARED_UC->pitch_y = (int8_t)pitch_combined;

    /* Build the visible-partition-faces list once. Primary populates
     * pface_* via cache-through alias; both halves of raycast_draw_walls
     * read them when doing per-ray ray-segment intersection. Must finish
     * BEFORE the secondary wake below.
     *
     * Drain: a read-back of the last-written cache-through address
     * serializes against all prior writes through the same alias bus
     * path. Without it the MARS controller can forward COMM4=HALF
     * before SDRAM writes are visible from secondary's view. */
    partition_build_faces();
    (void)PFACE_COUNT;
    __asm__ __volatile__("" ::: "memory");

    /* Single dispatch: secondary does clear + ceiling + carpet + walls for
     * cols 160..319, primary does the same for cols 0..159 in parallel.
     * Column ownership eliminates the previous CEILING→WALLS sequential
     * dependency (primary used to idle ~26ms waiting for secondary's ceiling
     * before walls could start). One sync point at the end. */
    /* Adaptive load balance: nudge the split column to equalize last frame's
     * primary (H) and secondary (S) half-render FRT times. Feedback controller,
     * no per-column cost model — converges in a few frames as the view changes.
     * On emulators H/S read 0, so split stays at SCREEN_W/2 (static 50/50). */
    static int split = SCREEN_W / 2;
    {
        int h = (int)prof_primary_half_ticks;            /* last frame, primary  */
        int s = (int)SHARED_UC->secondary_render_ticks;  /* last frame, secondary */
        int sum = h + s;
        if (sum > 2000) {                                /* valid FRT reading */
            int shift = ((h - s) * SCREEN_W) / (sum << 1);  /* full balancing step */
            shift >>= 1;                                 /* damp to avoid oscillation */
            if      (shift >  16) shift =  16;
            else if (shift < -16) shift = -16;
            split -= shift;                              /* h>s: primary overloaded -> shrink */
            if      (split < 64)            split = 64;
            else if (split > SCREEN_W - 64) split = SCREEN_W - 64;
            split &= ~3;                                 /* clear pass writes 4-px words */
        }
        SHARED_UC->split_col = (uint16_t)split;          /* secondary reads this */
    }

    MARS_SYS_COMM4 = MARS_CMD_HALF;

    uint16_t pp = prof_frt_read();
    raycast_clear_half(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_clear  = (uint16_t)(n - pp); pp = n; }
    raycast_draw_ceiling_grid(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_ceil   = (uint16_t)(n - pp); pp = n; }
    raycast_draw_carpet(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_carpet = (uint16_t)(n - pp); pp = n; }
    raycast_draw_walls(0, split);
    { uint16_t n = prof_frt_read(); prof_pass_walls  = (uint16_t)(n - pp); }

    uint16_t idle_start = prof_frt_read();
    prof_primary_half_ticks = (uint16_t)(idle_start - prof_start);
    while (MARS_SYS_COMM4 != MARS_CMD_NONE) {
        /* Throttle the primary-side ACK wait to reduce 68K-bridge
         * pressure — bare-loop polling at ~5M reads/sec was lining
         * up with the 68K's joypad-read window often enough to drop
         * COMM8 updates. ~30 cycles of NOPs per loop iteration brings
         * the primary poll rate down to a friendlier ~700K/sec while
         * keeping latency well under one frame. */
        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop\n\t"
                             "nop\n\tnop\n\tnop\n\tnop");
    }
    prof_primary_idle_ticks = (uint16_t)(prof_frt_read() - idle_start);

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
