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

/* Player spawn cell. The spine corridor passes through this row. */
#define SPAWN_CX 16
#define SPAWN_CY 28

/* ── Helpers ──────────────────────────────────────────────────────── */

static int in_bounds_room(int x, int y, int w, int h) {
    /* Room must leave a 1-cell buffer from map edges so walls fit. */
    if (x < 1 || y < 1) return 0;
    if (x + w >= MAP_W - 1) return 0;
    if (y + h >= MAP_H - 1) return 0;
    return 1;
}

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

/* True if every cell of a proposed rectangle plus a 1-cell margin is
 * currently wall (i.e. nothing carved there yet). Used to avoid
 * overlapping side rooms or room-pair clusters. */
static int region_is_unclaimed(int x, int y, int w, int h) {
    for (int j = -1; j <= h; j++) {
        for (int i = -1; i <= w; i++) {
            int cx = x + i, cy = y + j;
            if (cx < 0 || cx >= MAP_W || cy < 0 || cy >= MAP_H) continue;
            if (world_map[cy][cx] == 0) return 0;
        }
    }
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

static int spine_y;  /* row of the upper spine corridor cell */
static int spine_w;  /* corridor width in cells (1 or 2) */

static void carve_spine(void) {
    /* Spine runs through the spawn row so spawn is naturally on it. */
    spine_y = SPAWN_CY;
    spine_w = (xs32() & 1) ? 2 : 1;   /* coin flip on width */
    for (int x = 1; x < MAP_W - 1; x++) {
        for (int dy = 0; dy < spine_w; dy++) {
            world_map[spine_y - dy][x] = 0;
        }
    }
}

static void place_side_rooms(void) {
    /* Try up to N times to place rooms branching off the spine.
     * Some attempts will fail because they collide with prior rooms;
     * the surprise factor comes from how many actually land. */
    int target_count = xs32_range(3, 4 + g_procgen_params.openness);
    int attempts = target_count * 4;
    int placed = 0;
    while (attempts-- > 0 && placed < target_count) {
        int side = (xs32() & 1);   /* 0 = north of spine, 1 = south */
        int w = xs32_range(3, 5);
        int h = xs32_range(3, 5);
        int rx = xs32_range(2, MAP_W - 3 - w);
        int ry;
        if (side == 0) {
            /* North of spine: room sits above the upper spine row,
             * with a 1-cell gap for the door. */
            ry = spine_y - spine_w + 1 - h - 1;
        } else {
            /* South of spine: room sits below the lower spine row. */
            ry = spine_y + 2;
        }
        if (!in_bounds_room(rx, ry, w, h)) continue;
        if (!region_is_unclaimed(rx, ry, w, h)) continue;

        carve_room(rx, ry, w, h);
        /* Single-cell door connecting to the spine. */
        int door_x = rx + xs32_range(0, w - 1);
        int door_y = (side == 0) ? (ry + h) : (ry - 1);
        open_cell(door_x, door_y);

        /* Drop a divider in big rooms, gated by the partition-density weight. */
        if (w >= 4 && h >= 4 && prob(g_procgen_params.partitions) &&
            num_partitions < NUM_PARTITIONS_MAX) {
            /* Place a partition straddling the interior, leaving a
             * 1-cell pad from each side so collision feels right. */
            if (xs32() & 1) {
                /* Horizontal partition. */
                fx_t y_mid = (fx_t)((ry + h / 2)) << FX_SHIFT;
                fx_t x1   = (fx_t)(rx + 1)     << FX_SHIFT;
                fx_t x2   = (fx_t)(rx + w - 1) << FX_SHIFT;
                add_partition(x1, y_mid, x2, y_mid);
            } else {
                /* Vertical partition. */
                fx_t x_mid = (fx_t)((rx + w / 2)) << FX_SHIFT;
                fx_t y1   = (fx_t)(ry + 1)     << FX_SHIFT;
                fx_t y2   = (fx_t)(ry + h - 1) << FX_SHIFT;
                add_partition(x_mid, y1, x_mid, y2);
            }
        }

        placed++;
    }
}

static void place_room_pair_clusters(void) {
    /* Two rooms side-by-side joined by a single-cell door, dropped
     * into otherwise-unclaimed map space, connected back to the spine
     * via a snaking corridor. */
    int target = xs32_range(1, 1 + g_procgen_params.openness);
    int attempts = target * 5;
    int placed = 0;
    while (attempts-- > 0 && placed < target) {
        int w = xs32_range(3, 4);
        int h = xs32_range(3, 4);
        int gap = 1;   /* shared wall between the pair */
        int total_w = w * 2 + gap;
        int rx = xs32_range(2, MAP_W - 3 - total_w);
        int ry = xs32_range(2, MAP_H - 3 - h);

        /* Stay clear of the spine band. */
        if (ry + h >= spine_y - spine_w && ry <= spine_y + spine_w) continue;
        if (!in_bounds_room(rx, ry, total_w, h)) continue;
        if (!region_is_unclaimed(rx, ry, total_w, h)) continue;

        /* Carve room A. */
        carve_room(rx, ry, w, h);
        /* Carve room B. */
        int rx2 = rx + w + gap;
        carve_room(rx2, ry, w, h);
        /* Single-cell door between them. */
        int door_y = ry + xs32_range(0, h - 1);
        open_cell(rx + w, door_y);

        /* Connect cluster back to spine via an L-shaped corridor from
         * a random edge cell of room A. */
        int hook_x = rx + xs32_range(0, w - 1);
        int hook_y = (ry > spine_y) ? ry - 1 : ry + h;
        int step = (hook_y > spine_y) ? -1 : 1;
        for (int cy = hook_y; cy != spine_y; cy += step) {
            open_cell(hook_x, cy);
        }
        open_cell(hook_x, spine_y);

        placed++;
    }
}

static void scatter_pockets(void) {
    /* Single-cell alcoves perpendicular to the spine. About 1 per 6
     * corridor cells; just enough to add discoverable nooks. */
    for (int x = 2; x < MAP_W - 2; x++) {
        /* More open => more discoverable nooks. */
        if ((int)(xs32() % 7) > g_procgen_params.openness) continue;
        int side = (xs32() & 1);
        int py = (side == 0) ? (spine_y - spine_w) : (spine_y + 1);
        /* The pocket only lands if it doesn't already open into an
         * existing carved space (we only want pockets bordered by
         * walls so they read as nooks, not corridor branches). */
        if (py < 1 || py >= MAP_H - 1) continue;
        if (world_map[py][x] == 0) continue;
        /* Check the cell beyond the pocket is still wall — pockets
         * should be dead-ends, not shortcuts. */
        int beyond_y = (side == 0) ? (py - 1) : (py + 1);
        if (beyond_y < 0 || beyond_y >= MAP_H) continue;
        if (world_map[beyond_y][x] == 0) continue;
        open_cell(x, py);
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

/* Carve `count` low-ceiling crawl tubes: straight 1-wide corridor runs (walls
 * on both sides, both ends opening into walkable space) marked low via
 * ceil_h_set_low, so the player must crouch to pass through. */
static void place_crawlspaces(int count) {
    int placed = 0, attempts = count * 40;
    while (attempts-- > 0 && placed < count) {
        int horiz = xs32() & 1;
        int dx = horiz ? 1 : 0, dy = horiz ? 0 : 1;
        int L  = xs32_range(3, 5);
        int x = xs32_range(2, MAP_W - 3 - (horiz ? L : 0));
        int y = xs32_range(2, MAP_H - 3 - (horiz ? 0 : L));
        int ok = 1;
        for (int k = 0; k < L && ok; k++) {
            int cx = x + dx * k, cy = y + dy * k;
            if (world_map[cy][cx] != 0) { ok = 0; break; }   /* run must be floor   */
            /* 1-wide tube: a wall on each perpendicular side (dy,dx is perp). */
            if (world_map[cy + dx][cx + dy] == 0 ||
                world_map[cy - dx][cx - dy] == 0) { ok = 0; break; }
            if (ceil_h[cy][cx] != 255) { ok = 0; break; }    /* not already low     */
            int ddx = cx - SPAWN_CX, ddy = cy - SPAWN_CY;    /* keep clear of spawn */
            if (ddx > -2 && ddx < 2 && ddy > -2 && ddy < 2) { ok = 0; break; }
        }
        /* Both ends must open into walkable space so the tube is reachable. */
        if (ok && (world_map[y - dy][x - dx] != 0 ||
                   world_map[y + dy * L][x + dx * L] != 0)) ok = 0;
        if (ok) {
            ceil_h_add_run(x, y, dx, dy, L);   /* one run = one capped crawlspace */
            placed++;
        }
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

    /* Layout (weights scale room/pocket density). */
    fill_walls();
    carve_spine();
    place_side_rooms();
    place_room_pair_clusters();
    scatter_pockets();
    enforce_boundary();
    clear_spawn_vestibule();

    /* Elements (the lobby features), all weight-driven:
     *  - extra free-standing dividers on top of the room dividers
     *  - per-divider spotted/partial-height decor
     *  - low-ceiling crawl tubes carved into 1-wide corridors
     *  - electrical outlets peppered across visible wall faces */
    scatter_partitions(g_procgen_params.partitions * 2);
    assign_partition_decor();
    place_crawlspaces(g_procgen_params.crawlspaces);
    raycast_place_outlets(g_procgen_params.outlets * 3);
}
