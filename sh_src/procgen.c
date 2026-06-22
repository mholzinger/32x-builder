#include "procgen.h"
#include "raycast.h"

/* xorshift32 — 10-line PRNG, fast and deterministic. */
static uint32_t prng_state = 1;
static uint32_t xs32(void) {
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}
/* Inclusive integer range [lo, hi]. */
static int xs32_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(xs32() % (uint32_t)(hi - lo + 1));
}

/* ── Player-tunable weights ───────────────────────────────────────────
 * g_procgen_params drives every density/ratio below. A weight of w in
 * 0..PROCGEN_MAX_W maps to a w/MAX probability (0,25,50,75,100%) via
 * prob(), and scales counts directly elsewhere. The lobby tuning screen
 * writes these before generation; procgen_params_default() is the preset. */
procgen_params_t g_procgen_params = { 2, 2, 2, 2, 2, 2 };

void procgen_params_default(void) {
    g_procgen_params = (procgen_params_t){
        .openness    = 2,   /* medium room count            */
        .partitions  = 2,   /* some dividers                */
        .crawlspaces = 2,   /* a couple of crawl tubes      */
        .outlets     = 2,   /* outlets here and there       */
        .spotted     = 2,   /* ~half spotted, half chevron  */
        .lowdivs     = 2,   /* ~half see-over dividers      */
    };
}

/* True with probability weight/PROCGEN_MAX_W (so 0 => never, MAX => always). */
static int prob(int weight) {
    return (int)(xs32() % PROCGEN_MAX_W) < weight;
}

/* ─────────────────────────────────────────────────────────────────────
 * Building-block-based generator.
 *
 * Replaces the original 4-quadrant template stamper. The templates
 * produced too many isolated single-cell pillars and felt random
 * rather than constructed. This generator instead places named
 * building blocks (spine corridor, side rooms, room-pair clusters,
 * wall pockets, partitions) with intent — every wall belongs to
 * something, every walkable cell is reachable from spawn.
 *
 * Pipeline:
 *   PHASE 1  fill all cells with wall
 *   PHASE 2  carve the spine corridor along the spawn row
 *   PHASE 3  attach side rooms branching off the spine, single-cell
 *            doors connecting each one
 *   PHASE 4  carve clustered room pairs elsewhere on the map,
 *            single-cell doors between each pair, single-cell
 *            doors connecting clusters back to the spine
 *   PHASE 5  scatter wall pockets along corridor walls
 *   PHASE 6  drop 1-2 partition segments inside the larger rooms
 *   PHASE 7  enforce outer boundary, ensure spawn vestibule open
 *
 * "Surprise me" parameters: room count, sizes, partition density,
 * pocket density are all rolled from the PRNG per generation. Roadmap
 * task ("Procgen tuning knobs") covers exposing these as constants
 * for tuning.
 * ───────────────────────────────────────────────────────────────────── */

/* Player spawn cell. The spine corridor passes through this row. Near the
 * bottom-centre of the 32x32 grid so the map opens out to the north. */
#define SPAWN_CX 16
#define SPAWN_CY 28

/* ── Helpers ──────────────────────────────────────────────────────── */

/* Defined below in the element passes; the layout structure-placers use it to
 * confirm a footprint (plus margin) is clear floor before dropping a wall. */
static int cells_open(int x0, int y0, int x1, int y1);

/* Carve a rectangular room: interior walkable, perimeter is left to
 * the surrounding wall fill. Caller stamps doors separately. */
static void carve_room(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int cx = x + i, cy = y + j;
            if (cx >= 0 && cx < MAP_W && cy >= 0 && cy < MAP_H) {
                world_map[cy][cx] = 0;
            }
        }
    }
}

/* Open a single cell — typically a doorway through a wall. */
static void open_cell(int x, int y) {
    if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) {
        world_map[y][x] = 0;
    }
}

/* Add a partition segment. Returns 1 if accepted, 0 if at cap. */
static int add_partition(fx_t x1, fx_t y1, fx_t x2, fx_t y2) {
    if (num_partitions >= NUM_PARTITIONS_MAX) return 0;
    partitions[num_partitions].x1 = x1;
    partitions[num_partitions].y1 = y1;
    partitions[num_partitions].x2 = x2;
    partitions[num_partitions].y2 = y2;
    num_partitions++;
    return 1;
}

/* ── Phase implementations ────────────────────────────────────────── */

static void fill_walls(void) {
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            world_map[y][x] = 1;
        }
    }
}

/* Open the whole interior — the big connected floor everything else sits in.
 * This is the inversion of the old spine generator: start OPEN, then drop
 * structure into it (rooms, pillars, stubs) rather than carving corridors out
 * of solid wall. Every structure keeps a clear margin, so the floor always
 * stays one connected open space — open by construction, never spaghetti. */
static void carve_open_field(void) {
    carve_room(2, 2, MAP_W - 4, MAP_H - 4);
}

/* True if the structure footprint (x..x+w-1, y..y+h-1) plus a 1-cell margin is
 * clear open floor AND clear of spawn — the gate every placer uses. */
static int footprint_clear(int x, int y, int w, int h) {
    if (!cells_open(x - 1, y - 1, x + w, y + h)) return 0;
    if (x - 1 <= SPAWN_CX && SPAWN_CX <= x + w &&
        y - 1 <= SPAWN_CY && SPAWN_CY <= y + h) return 0;
    return 1;
}

/* Build `count` enclosed grid-wall rooms: a wall perimeter with ONE doorway,
 * dropped only where the footprint+margin is clear. Reads as a room you step
 * into; the open margin guarantees it never seals off the floor. */
static void place_enclosed_rooms(int count) {
    int placed = 0, attempts = count * 8;
    while (attempts-- > 0 && placed < count) {
        int w = xs32_range(4, 8), h = xs32_range(4, 8);
        int rx = xs32_range(3, MAP_W - 4 - w);
        int ry = xs32_range(3, MAP_H - 4 - h);
        if (!footprint_clear(rx, ry, w, h)) continue;
        for (int i = 0; i < w; i++) { world_map[ry][rx + i] = 1; world_map[ry + h - 1][rx + i] = 1; }
        for (int j = 0; j < h; j++) { world_map[ry + j][rx] = 1; world_map[ry + j][rx + w - 1] = 1; }
        /* one doorway, random side, away from the corners */
        switch (xs32() & 3) {
            case 0:  open_cell(rx + 1 + xs32_range(0, w - 3), ry);          break;
            case 1:  open_cell(rx + 1 + xs32_range(0, w - 3), ry + h - 1);  break;
            case 2:  open_cell(rx,         ry + 1 + xs32_range(0, h - 3));  break;
            default: open_cell(rx + w - 1, ry + 1 + xs32_range(0, h - 3));  break;
        }
        placed++;
    }
}

/* Pillar blocks (mostly 1x1, some 2x2) scattered in the open with a clear
 * margin — structure + sightline breaks that never enclose anything. */
static void place_pillars(int count) {
    int placed = 0, attempts = count * 6;
    while (attempts-- > 0 && placed < count) {
        int s = ((xs32() & 3) == 0) ? 2 : 1;
        int px = xs32_range(4, MAP_W - 5 - s);
        int py = xs32_range(4, MAP_H - 5 - s);
        if (!footprint_clear(px, py, s, s)) continue;
        for (int j = 0; j < s; j++)
            for (int i = 0; i < s; i++) world_map[py + j][px + i] = 1;
        placed++;
    }
}

/* Short free-standing wall stubs (the uncanny "why is this here" backrooms
 * walls). Clear margin, so they break sightlines without enclosing. */
static void place_stub_walls(int count) {
    int placed = 0, attempts = count * 6;
    while (attempts-- > 0 && placed < count) {
        int horiz = xs32() & 1;
        int L = xs32_range(2, 5);
        int w = horiz ? L : 1, h = horiz ? 1 : L;
        int sx = xs32_range(4, MAP_W - 5 - w);
        int sy = xs32_range(4, MAP_H - 5 - h);
        if (!footprint_clear(sx, sy, w, h)) continue;
        for (int k = 0; k < L; k++)
            world_map[sy + (horiz ? 0 : k)][sx + (horiz ? k : 0)] = 1;
        placed++;
    }
}

static void enforce_boundary(void) {
    for (int x = 0; x < MAP_W; x++) {
        world_map[0]        [x] = 1;
        world_map[MAP_H - 1][x] = 1;
    }
    for (int y = 0; y < MAP_H; y++) {
        world_map[y][0]         = 1;
        world_map[y][MAP_W - 1] = 1;
    }
}

static void clear_spawn_vestibule(void) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int sx = SPAWN_CX + dx, sy = SPAWN_CY + dy;
            if (sx > 0 && sx < MAP_W - 1 && sy > 0 && sy < MAP_H - 1) {
                world_map[sy][sx] = 0;
            }
        }
    }
}

/* ── Element passes (the new lobby features) ──────────────────────────── */

/* True if every cell in the inclusive rect [x0,x1]x[y0,y1] is open floor. */
static int cells_open(int x0, int y0, int x1, int y1) {
    if (x0 < 0 || y0 < 0 || x1 >= MAP_W || y1 >= MAP_H) return 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (world_map[y][x] != 0) return 0;
    return 1;
}

/* True if (mx,my) is >= 4 cells from every existing partition centre. */
static int far_from_partitions(fx_t mx, fx_t my) {
    for (int i = 0; i < num_partitions; i++) {
        fx_t ox = (partitions[i].x1 + partitions[i].x2) >> 1;
        fx_t oy = (partitions[i].y1 + partitions[i].y2) >> 1;
        fx_t dx = mx > ox ? mx - ox : ox - mx;
        fx_t dy = my > oy ? my - oy : oy - my;
        if (dx < FX(4) && dy < FX(4)) return 0;
    }
    return 1;
}

/* Scatter up to `add` free-standing wallpaper dividers into open areas — each
 * floats with a 1-cell walkable margin all around, so it never seals a path.
 * (Style/height get assigned afterward by assign_partition_decor.) */
static void scatter_partitions(int add) {
    const int L = 3;
    int placed = 0, attempts = add * 10;
    while (attempts-- > 0 && placed < add && num_partitions < NUM_PARTITIONS_MAX) {
        int horiz = xs32() & 1;
        if (horiz) {
            int cx = xs32_range(2, MAP_W - 3 - L);
            int cy = xs32_range(2, MAP_H - 3);
            if (!cells_open(cx - 1, cy - 1, cx + L + 1, cy + 1)) continue;
            fx_t my = (fx_t)cy << FX_SHIFT;
            fx_t mx = ((fx_t)cx << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            if (!far_from_partitions(mx, my)) continue;
            add_partition((fx_t)cx << FX_SHIFT, my, (fx_t)(cx + L) << FX_SHIFT, my);
        } else {
            int cx = xs32_range(2, MAP_W - 3);
            int cy = xs32_range(2, MAP_H - 3 - L);
            if (!cells_open(cx - 1, cy - 1, cx + 1, cy + L + 1)) continue;
            fx_t mx = (fx_t)cx << FX_SHIFT;
            fx_t my = ((fx_t)cy << FX_SHIFT) + ((fx_t)L << (FX_SHIFT - 1));
            if (!far_from_partitions(mx, my)) continue;
            add_partition(mx, (fx_t)cy << FX_SHIFT, mx, (fx_t)(cy + L) << FX_SHIFT);
        }
        placed++;
    }
}

/* Assign per-partition decor from the weights: spotted-vs-chevron wallpaper and
 * full-vs-partial (see-over) height, rolled independently for each divider. */
static void assign_partition_decor(void) {
    for (int i = 0; i < num_partitions; i++) {
        partition_style[i]  = prob(g_procgen_params.spotted) ? 1 : 0;
        partition_height[i] = prob(g_procgen_params.lowdivs) ? 192 : 0;
    }
}

/* Carve `count` low-ceiling crouch tubes. Rather than HUNT for an existing
 * 1-wide walled corridor (which open procgen maps rarely have — so the old
 * version often placed zero), CARVE a 1-wide passage THROUGH a wall connecting
 * two open areas: open just before, L wall cells (perpendicular sides also wall
 * so it stays a 1-wide choke you can't walk around), open just after. Walls
 * separating open areas are everywhere, so this places reliably, and carving a
 * fresh choke guarantees a real forced-crouch crawlspace. */
static void place_crawlspaces(int count) {
    int placed = 0, attempts = count * 80;
    while (attempts-- > 0 && placed < count) {
        int horiz = xs32() & 1;
        int dx = horiz ? 1 : 0, dy = horiz ? 0 : 1;
        int L  = xs32_range(1, 3);   /* wall thickness to tunnel through */
        int x = xs32_range(2, MAP_W - 3 - (horiz ? L : 0));
        int y = xs32_range(2, MAP_H - 3 - (horiz ? 0 : L));
        if (world_map[y - dy][x - dx] != 0) continue;          /* entrance open */
        if (world_map[y + dy * L][x + dx * L] != 0) continue;  /* exit open     */
        int ok = 1;
        for (int k = 0; k < L && ok; k++) {
            int cx = x + dx * k, cy = y + dy * k;
            if (world_map[cy][cx] != 1) { ok = 0; break; }       /* must be wall to carve */
            if (world_map[cy + dx][cx + dy] != 1 ||              /* perp sides wall   */
                world_map[cy - dx][cx - dy] != 1) { ok = 0; break; }
            int ddx = cx - SPAWN_CX, ddy = cy - SPAWN_CY;        /* keep clear of spawn */
            if (ddx > -2 && ddx < 2 && ddy > -2 && ddy < 2) { ok = 0; break; }
        }
        if (!ok) continue;
        for (int k = 0; k < L; k++) world_map[y + dy * k][x + dx * k] = 0;  /* carve passage */
        ceil_h_add_run(x, y, dx, dy, L);                                    /* mark it low   */
        placed++;
    }
}

/* ── Driver ───────────────────────────────────────────────────────── */

void procgen_run(uint32_t seed) {
    prng_state = seed ? seed : 1;
    for (int i = 0; i < 8; i++) xs32();   /* mix the small-seed bits */

    /* Reset partitions for this generation pass. */
    num_partitions = 0;
    num_decals = 0;                       /* outlet is lobby-only */
    g_lobby_ceiling = 0;                  /* auto-grid ceiling for procgen */
    ceil_h_clear();                       /* full ceilings; mark crawlspaces below */
    for (int i = 0; i < NUM_PARTITIONS_MAX; i++) {
        partition_style[i]  = 0;   /* chevron */
        partition_height[i] = 0;   /* full height */
        partition_crawl[i]  = 0;   /* solid foot */
    }

    /* Layout: a big open floor with structure dropped into it — open by
     * construction, never corridor-y. `openness` thins the structure (higher
     * openness = sparser = more wide-open floor). */
    int dens = PROCGEN_MAX_W - g_procgen_params.openness;   /* 0 = airy .. 4 = busy */
    fill_walls();
    carve_open_field();
    place_enclosed_rooms(xs32_range(5, 8 + dens));
    place_pillars(xs32_range(10, 14 + dens * 2));
    place_stub_walls(xs32_range(6, 9 + dens));
    enforce_boundary();
    clear_spawn_vestibule();
    /* The way out: every generated level gets the exit door behind spawn. It
     * carves its own approach and only opens into the NEXT generated level. */
    raycast_place_exit_door();

    /* Elements (the lobby features), all weight-driven:
     *  - extra free-standing dividers on top of the room dividers
     *  - per-divider spotted/partial-height decor
     *  - low-ceiling crawl tubes carved into 1-wide corridors
     *  - electrical outlets peppered across visible wall faces */
    /* Free-standing dividers define the open rooms. Each one is rendered per
     * visible-face per screen-column every frame, so on the 32x32 grid (where
     * the small footprint keeps most of them in view down the open sightlines)
     * a high count is the dominant per-frame cost and tanks the frame rate on
     * busy seeds. Scaled down from the 64x64 tuning (was 6 + p*5, up to ~26)
     * to 4 + p*3 (up to 16) so even max-divider maps stay inside the budget. */
    scatter_partitions(4 + g_procgen_params.partitions * 3);
    assign_partition_decor();
    place_crawlspaces(g_procgen_params.crawlspaces + 1);
    raycast_place_outlets(g_procgen_params.outlets * 5);
}
