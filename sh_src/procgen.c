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

/* Count the open-floor cells a candidate rect would overlap — the connectivity
 * probe for the organic carve: a new blob is only kept if it TOUCHES the
 * already-open region (so the floor stays one connected space) AND still adds
 * fresh floor (so it isn't a no-op inside an existing blob). */
static int overlaps_open(int x, int y, int w, int h) {
    int n = 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int cx = x + i, cy = y + j;
            if (cx >= 0 && cx < MAP_W && cy >= 0 && cy < MAP_H
                && world_map[cy][cx] == 0) n++;
        }
    return n;
}

/* Open the walkable floor everything else sits in — but as an ORGANIC footprint,
 * not a rectangle. The old version carved one big rect (2,2 .. MAP_W-4,MAP_H-4),
 * which is exactly why every map read as a square. Instead we grow the floor as
 * a UNION of overlapping blobs: a chunky seed over spawn, then accreted blobs
 * that each must touch the already-open region (kept connected by construction)
 * and add fresh floor. A quarter of the blobs are thin+long — jogged halls and
 * necks between the rooms — so the space has both open sprawl and tight runs,
 * and the outer boundary comes out irregular (the un-carved cells stay wall).
 * Structure still drops in afterward with clear margins, so the 1-cell margin on
 * every placer keeps it from sealing a narrow neck (a 2-wide hall can't fit a
 * pillar's 3x3 clearance). */
static void carve_open_field(void) {
    /* Seed blob over spawn, opening north (spawn sits near the bottom edge). */
    int sw = xs32_range(8, 12), sh = xs32_range(8, 11);
    int sx = SPAWN_CX - sw / 2;
    int sy = SPAWN_CY - sh + 1;
    if (sx < 2) sx = 2;
    if (sx + sw > MAP_W - 2) sx = MAP_W - 2 - sw;
    if (sy < 2) sy = 2;
    carve_room(sx, sy, sw, sh);

    /* Accrete blobs onto the open region — a dramatic mix of scales so the
     * sprawl has rhythm: long thin runs that reach for the corners and throw
     * off jogged necks, small alcoves that pocket the edges, and chunky rooms
     * for the open beats. Each still must touch existing floor (connected) and
     * add fresh floor. More blobs than the first pass = the footprint stretches
     * further and gets wilder. */
    int target = xs32_range(8, 13);
    int placed = 0, attempts = target * 28;
    while (attempts-- > 0 && placed < target) {
        int roll = (int)(xs32() % 100);
        int bw, bh;
        if (roll < 35) {                          /* long thin hall — reach + necks */
            if (xs32() & 1) { bw = xs32_range(9, 16); bh = xs32_range(2, 3); }
            else            { bw = xs32_range(2, 3);  bh = xs32_range(9, 16); }
        } else if (roll < 55) {                   /* small alcove — side pockets */
            bw = xs32_range(3, 5); bh = xs32_range(3, 5);
        } else {                                  /* chunky room — the open beats */
            bw = xs32_range(5, 12); bh = xs32_range(5, 12);
        }
        int bx = xs32_range(2, MAP_W - 3 - bw);
        int by = xs32_range(2, MAP_H - 3 - bh);
        int ov = overlaps_open(bx, by, bw, bh);
        if (ov < 2 || ov > bw * bh - 2) continue; /* must connect AND add new floor */
        carve_room(bx, by, bw, bh);
        placed++;
    }
}

/* True if the structure footprint (x..x+w-1, y..y+h-1) plus a 1-cell margin is
 * clear open floor AND clear of spawn — the gate every placer uses. */
static int footprint_clear(int x, int y, int w, int h) {
    if (!cells_open(x - 1, y - 1, x + w, y + h)) return 0;
    if (x - 1 <= SPAWN_CX && SPAWN_CX <= x + w &&
        y - 1 <= SPAWN_CY && SPAWN_CY <= y + h) return 0;
    return 1;
}

/* Scatter free-standing neanderthal cutouts on clear floor, UPRIGHT. Every
 * generated level gets at least one (the iconic "something is standing there"
 * beat) — until now procgen placed none and only ever showed a leaked one from
 * the previous map (fallen, the bug we just fixed). */
static void place_neanderthals(int count) {
    int placed = 0, attempts = count * 16;
    while (attempts-- > 0 && placed < count) {
        int x = xs32_range(2, MAP_W - 3);
        int y = xs32_range(2, MAP_H - 3);
        if (!footprint_clear(x, y, 1, 1)) continue;
        if (raycast_standup_in_cell(x, y)) continue;   /* no two assets in one cell */
        if (raycast_exit_path_cell(x, y)) continue;    /* keep the exit corridor clear */
        uint8_t facing = (uint8_t)(xs32_range(0, 3) * 64);   /* cardinal */
        raycast_add_standup(((fx_t)x << FX_SHIFT) + FX(0.5),
                            ((fx_t)y << FX_SHIFT) + FX(0.5), facing, 2);
        placed++;
    }
    if (placed) return;
    /* Darts can miss, and "at least one" is a promise rather than a tendency:
     * measured on the host harness, ~10% of seeds threw all their attempts
     * away and shipped a level with nothing standing in it. Same lesson the
     * desk scan learned — a full scan cannot miss. Reservoir sampling keeps
     * the choice uniform without a candidate list in RAM. */
    {
        int seen = 0, bx = -1, by = -1;
        for (int y = 2; y < MAP_H - 2; y++)
            for (int x = 2; x < MAP_W - 2; x++) {
                if (!footprint_clear(x, y, 1, 1)) continue;
                if (raycast_standup_in_cell(x, y)) continue;
                if (raycast_exit_path_cell(x, y)) continue;
                if (xs32_range(0, seen++) == 0) { bx = x; by = y; }
            }
        if (bx >= 0)
            raycast_add_standup(((fx_t)bx << FX_SHIFT) + FX(0.5),
                                ((fx_t)by << FX_SHIFT) + FX(0.5),
                                (uint8_t)(xs32_range(0, 3) * 64), 2);
    }
}

/* EXACTLY ONE PVM per generated level (Mike: every procedural level has the
 * monitor, but they FEEL expensive — budget is a hard 2 per map, lint-side,
 * and procgen spends just 1 of it). Since 2026-08-10 the procgen monitor is
 * the DESK-MOUNTED composite, and the FACING RULE is law: the screen must
 * face an open floor cell, never a wall — a monitor nobody could watch is
 * set dressing gone wrong. Cells with no open cardinal are skipped. */
/* PARKED, not dropped. A desk standing in open floor reads as scenery the
 * generator sprinkled; a desk with its BACK TO A WALL reads as one somebody
 * pushed there and sat at. That single test does all the work Mike asked for:
 *
 *   dead end / nook   3 walls, 1 way out   -> back to the dead end, facing out
 *   corner of a room  2 walls              -> tucked in, facing the room
 *   against a wall    1 wall               -> the ordinary office answer
 *   middle of a room  0 walls              -> REJECTED, nothing to back onto
 *   1-wide corridor   walls on the sides   -> REJECTED for free: facing either
 *                                             way leaves the back open, and a
 *                                             desk mid-corridor is a roadblock
 *                                             nobody would have left there.
 *
 * Returns the facings that satisfy it (watchable floor ahead, wall behind) and
 * reports how walled-in the cell is, so the caller can hold out for a nook.
 * Facing: E0 S64 W128 N192 -> front dir (+x),(+y),(-x),(-y); d^2 is the
 * opposite cardinal in that order, which is what "behind" means. */
/* Grid steps from spawn the strict pass demands, so the desk is something you
 * go and find. Deliberately modest: the map is 32x32 and the walk is not a
 * straight line, so 10 already means several rooms of travel. Push it much
 * higher and the strict pass starves on small maps and quietly hands every
 * level to the relaxed one, which is the rule without the hunt. */
#define DESK_HUNT_MIN 10

static int desk_park_facings(int x, int y, uint8_t *out, int *nwall) {
    static const int8_t fdx[4] = { 1, 0, -1, 0 };
    static const int8_t fdy[4] = { 0, 1, 0, -1 };
    int isopen[4], n = 0;
    *nwall = 0;
    for (int d = 0; d < 4; d++) {
        /* WALKABLE is the test, not placeable: footprint_clear demands a
         * 1-cell margin too, and requiring that of a neighbor turned the rule
         * into "needs a plaza" — zero monitors ever placed. */
        isopen[d] = cells_open(x + fdx[d], y + fdy[d], x + fdx[d], y + fdy[d]);
        if (!isopen[d]) (*nwall)++;
    }
    for (int d = 0; d < 4; d++)
        if (isopen[d] && !isopen[d ^ 2]) out[n++] = (uint8_t)(d * 64);
    return n;
}

/* A desk BACKS ONTO a wall, so it cannot be judged by the all-open 3x3
 * footprint the free-standing placers use. That test and the wall-at-back
 * test below are mutually exclusive — an open 3x3 means no wall neighbour to
 * back onto — so requiring both rejected every cell on the grid and
 * place_pvms returned having placed nothing. Every generated level shipped
 * without the console (proven over 5000 seeds on a host harness, 2026-08-12).
 * The desk needs its own cell open and distance from spawn; the approach side
 * is guaranteed by desk_park_facings, which only offers a facing whose front
 * cell is open. */
static int desk_cell_free(int x, int y) {
    if (world_map[y][x] != 0) return 0;
    return !(x >= SPAWN_CX - 1 && x <= SPAWN_CX + 1 &&
             y >= SPAWN_CY - 1 && y <= SPAWN_CY + 1);
}

/* Pick one parked spot, scanning the WHOLE grid rather than sampling it.
 *
 * Random attempts were fine for the old rule, which accepted almost any open
 * cell. This one rejects most of the floor, and 64 darts at a target that
 * small can miss — a miss means the level ships with no monitor at all, and
 * "every procedural level has the monitor" is a standing promise. A full scan
 * cannot miss: if a single qualifying cell exists anywhere, it is found.
 *
 * Reservoir sampling keeps the choice uniform without a candidate list in RAM
 * (each match has a 1/seen chance of replacing the keeper, so every match ends
 * up equally likely). Runs once per level generation. */
static int desk_park_scan(int need_nook, int need_far,
                          int *bx, int *by, uint8_t *bfacing) {
    int seen = 0;
    for (int y = 2; y < MAP_H - 2; y++)
        for (int x = 2; x < MAP_W - 2; x++) {
            uint8_t cand[4]; int nwall;
            if (!desk_cell_free(x, y)) continue;
            /* Facing test before the standup/exit lookups: it is plain
             * neighbour reads and it rejects most of the floor, so the
             * costlier queries only run on cells that could actually win. */
            int n = desk_park_facings(x, y, cand, &nwall);
            if (!n) continue;                 /* nothing to back onto */
            if (need_nook && nwall < 2) continue;   /* holding out for a nook */
            if (need_far) {
                /* Make it a HUNT. A desk parked in the first room you
                 * walk into is found, not discovered — the point is to give
                 * a reason to check rooms and look around corners. Manhattan
                 * distance is the right measure here and not a shortcut: you
                 * walk corridors, so grid steps are closer to real travel
                 * than a straight line through walls would be. */
                int dx = x - SPAWN_CX, dy = y - SPAWN_CY;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx + dy < DESK_HUNT_MIN) continue;
            }
            if (raycast_standup_in_cell(x, y)) continue;
            if (raycast_exit_path_cell(x, y)) continue;
            seen++;
            if (xs32_range(0, seen - 1) == 0) {
                *bx = x; *by = y;
                *bfacing = cand[xs32_range(0, n - 1)];
            }
        }
    return seen;
}

static void place_pvms(int count) {
    for (int placed = 0; placed < count; placed++) {
        int x = 0, y = 0; uint8_t facing = 0;
        /* Three tiers, best first. Dropping the NOOK before dropping the
         * DISTANCE is deliberate: a plain wall far away is still something
         * you had to go and find, while a perfect corner two steps from
         * spawn is not a discovery at all.
         *   1  a nook or corner, far from spawn        the one we want
         *   2  any wall at its back, far from spawn    still a hunt
         *   3  any wall at its back, anywhere          last resort
         * If even tier 3 finds nothing, the map has no floor cell touching a
         * wall — meaning it has no walls. Place nothing rather than maroon
         * the desk in open floor, which is the exact thing this rule exists
         * to prevent. */
        if (!desk_park_scan(1, 1, &x, &y, &facing)
            && !desk_park_scan(0, 1, &x, &y, &facing)
            && !desk_park_scan(0, 0, &x, &y, &facing)) return;
        raycast_add_standup(((fx_t)x << FX_SHIFT) + FX(0.5),
                            ((fx_t)y << FX_SHIFT) + FX(0.5), facing,
                            PVM_ASSET_KIND);
        raycast_standup_make_desk();          /* the composite, not the stand */
    }
}

/* Scatter live-3D chairs on clear floor. The engine's render guard only draws
 * the nearest few, so a couple per level reads as furniture without crating
 * the frame. kind 3 = CHAIR_SPRITE_KIND. */
static void place_chairs(int count) {
    int placed = 0, attempts = count * 16;
    while (attempts-- > 0 && placed < count) {
        int x = xs32_range(2, MAP_W - 3);
        int y = xs32_range(2, MAP_H - 3);
        if (!footprint_clear(x, y, 1, 1)) continue;
        if (raycast_standup_in_cell(x, y)) continue;   /* no two assets in one cell */
        if (raycast_exit_path_cell(x, y)) continue;    /* chairs don't shove: never
                                                        * on the exit corridor */
        uint8_t facing = (uint8_t)(xs32_range(0, 3) * 64);
        raycast_add_standup(((fx_t)x << FX_SHIFT) + FX(0.5),
                            ((fx_t)y << FX_SHIFT) + FX(0.5), facing, 3);
        placed++;
    }
}

/* Scatter desks, each with a chance of a COMPANION pulled up to it.
 *
 * kind 5 = DESK. A desk is only 3 boxes / 18 faces (cheaper than the chair's 9 /
 * 54) but it is physically much bigger, and registry limits.max_desks caps
 * authored maps at 8; procgen stays well under that so a generated level still
 * reads as sparse office rather than a showroom.
 *
 * PAIRING. The desk's model front is -z, which maps to these world cell steps
 * for the four cardinal facings (E0 S64 W128 N192) — derived from the same
 * fc/fs rotation draw_chair_3d uses, not guessed:
 *     E -> (+1, 0)   S -> (0, +1)   W -> (-1, 0)   N -> (0, -1)
 * A companion goes in that neighbouring cell, turned back toward the desk, which
 * keeps the one-asset-per-cell rule intact and reads as a workstation.
 *
 * SURFACE props (a CRT sitting ON the tabletop) are the obvious next companion
 * and are NOT possible yet: standup_t has no base height, so every free-standing
 * object's feet are pinned to the floor. Adding a base-height field is the one
 * engine change that unlocks them; the slot table below is where they'd hang. */
#define DESK_KIND  5
#define CHAIR_KIND 3
static const int8_t DESK_FRONT_DX[4] = {  1,  0, -1,  0 };   /* E, S, W, N */
static const int8_t DESK_FRONT_DY[4] = {  0,  1,  0, -1 };

static void place_desks(int count) {
    int placed = 0, attempts = count * 16;
    while (attempts-- > 0 && placed < count) {
        int x = xs32_range(2, MAP_W - 3);
        int y = xs32_range(2, MAP_H - 3);
        if (!footprint_clear(x, y, 1, 1)) continue;
        if (raycast_standup_in_cell(x, y)) continue;
        if (raycast_exit_path_cell(x, y)) continue;   /* never block the way out */
        int q = xs32_range(0, 3);
        uint8_t facing = (uint8_t)(q * 64);
        raycast_add_standup(((fx_t)x << FX_SHIFT) + FX(0.5),
                            ((fx_t)y << FX_SHIFT) + FX(0.5), facing, DESK_KIND);
        placed++;
        /* Companion at the knee, most of the time — a desk with nothing at it
         * reads as stock, and an office is chairs pushed up to desks. */
        if (!prob(PROCGEN_MAX_W * 3 / 4)) continue;
        int cxp = x + DESK_FRONT_DX[q], cyp = y + DESK_FRONT_DY[q];
        if (cxp < 1 || cyp < 1 || cxp >= MAP_W - 1 || cyp >= MAP_H - 1) continue;
        if (!footprint_clear(cxp, cyp, 1, 1)) continue;
        if (raycast_standup_in_cell(cxp, cyp)) continue;
        if (raycast_exit_path_cell(cxp, cyp)) continue;
        raycast_add_standup(((fx_t)cxp << FX_SHIFT) + FX(0.5),
                            ((fx_t)cyp << FX_SHIFT) + FX(0.5),
                            (uint8_t)(facing + 128), CHAIR_KIND);  /* turned to face it */
    }
}

/* Build a wall-enclosed room and mark its interior DARK. Stamps its own
 * perimeter + one doorway (rather than needing a pre-clear rect that a dense
 * map rarely has), so it's a guaranteed "dark room surrounded by walls".
 * Shrinks the target size until a clear footprint is found. Returns the interior
 * rect via *ix0.. (for the crawl variant to reuse) and 1 on success. */
static int build_dark_room(int *ix0, int *iy0, int *ix1, int *iy1, int *side_out) {
    for (int w = 6; w >= 4; w--) {
        int h = w;
        for (int a = 0; a < 120; a++) {
            int rx = xs32_range(3, MAP_W - 4 - w);
            int ry = xs32_range(3, MAP_H - 4 - h);
            if (!footprint_clear(rx, ry, w, h)) continue;
            for (int i = 0; i < w; i++) {
                world_map[ry][rx + i] = 1; world_map[ry + h - 1][rx + i] = 1;
            }
            for (int j = 0; j < h; j++) {
                world_map[ry + j][rx] = 1; world_map[ry + j][rx + w - 1] = 1;
            }
            for (int j = 1; j < h - 1; j++)
                for (int i = 1; i < w - 1; i++) world_map[ry + j][rx + i] = 0;
            int side = xs32() & 3;                       /* 0 N 1 S 2 W 3 E */
            int dx, dy;                                  /* doorway cell */
            switch (side) {
                case 0:  dx = rx + 1 + xs32_range(0, w - 3); dy = ry;         break;
                case 1:  dx = rx + 1 + xs32_range(0, w - 3); dy = ry + h - 1; break;
                case 2:  dx = rx;         dy = ry + 1 + xs32_range(0, h - 3); break;
                default: dx = rx + w - 1; dy = ry + 1 + xs32_range(0, h - 3); break;
            }
            open_cell(dx, dy);
            raycast_add_dark_room(rx + 1, ry + 1, rx + w - 2, ry + h - 2);
            *ix0 = rx; *iy0 = ry; *ix1 = rx + w - 1; *iy1 = ry + h - 1;
            *side_out = side;
            return 1;
        }
    }
    return 0;
}

/* Guaranteed dark room surrounded by walls, entered through a normal doorway. */
static void place_dark_enclosed(void) {
    int x0, y0, x1, y1, side;
    build_dark_room(&x0, &y0, &x1, &y1, &side);
}

/* Guaranteed dark room whose ONLY entrance is a forced-crouch crawlspace: build
 * the walled dark room, then tunnel a low-ceiling passage through one wall so
 * you crawl into the dark instead of walking. */
static void place_dark_crawlspace(void) {
    int x0, y0, x1, y1, side;
    if (!build_dark_room(&x0, &y0, &x1, &y1, &side)) return;
    /* Tunnel a low-ceiling crawl through whichever wall opens onto reachable
     * floor (scan all four mid-wall cells). The room already has a doorway; the
     * crawl is the crouch-in moment. */
    int w = x1 - x0, h = y1 - y0;
    const int wc[4][4] = {                        /* {cellx, celly, dx_out, dy_out} */
        { x0 + 1 + (w >> 1), y1, 0,  1 },         /* S wall */
        { x0 + 1 + (w >> 1), y0, 0, -1 },         /* N wall */
        { x1, y0 + 1 + (h >> 1), 1,  0 },         /* E wall */
        { x0, y0 + 1 + (h >> 1), -1, 0 },         /* W wall */
    };
    for (int s = 0; s < 4; s++) {
        int cx = wc[s][0], cy = wc[s][1], dx = wc[s][2], dy = wc[s][3];
        int ox = cx + dx, oy = cy + dy;           /* cell just outside the wall */
        if ((unsigned)ox >= MAP_W || (unsigned)oy >= MAP_H) continue;
        if (world_map[oy][ox] != 0) continue;     /* need open floor outside */
        open_cell(cx, cy);                        /* carve the wall cell */
        ceil_h_add_run_h(cx - dx, cy - dy, dx, dy, 2, CRAWL_CEIL_H); /* low: interior + wall cell */
        return;
    }
}

/* Guaranteed hallway stretch of a few DARK unlit cells: find a straight run of
 * open floor flanked by walls (a passage), mark 3-5 consecutive cells dark so
 * the lights that would sit there are suppressed — a corridor where the lights
 * are out. Falls back to any straight open run if no walled corridor is found. */
static void place_dark_hallway(void) {
    for (int pass = 0; pass < 2; pass++) {       /* pass 0: walled corridor; 1: any run */
        for (int a = 0; a < 300; a++) {
            int horiz = xs32() & 1;
            int run = xs32_range(3, 5);
            int x = xs32_range(2, MAP_W - 3 - (horiz ? run : 0));
            int y = xs32_range(2, MAP_H - 3 - (horiz ? 0 : run));
            int ok = 1;
            for (int k = 0; k < run && ok; k++) {
                int cx = x + (horiz ? k : 0), cy = y + (horiz ? 0 : k);
                if (world_map[cy][cx] != 0) { ok = 0; break; }
                int ddx = cx - SPAWN_CX, ddy = cy - SPAWN_CY;
                if (ddx > -3 && ddx < 3 && ddy > -3 && ddy < 3) { ok = 0; break; }
                if (pass == 0) {                 /* require flanking walls (a corridor) */
                    int wa = horiz ? (world_map[cy - 1][cx] == 1) : (world_map[cy][cx - 1] == 1);
                    int wb = horiz ? (world_map[cy + 1][cx] == 1) : (world_map[cy][cx + 1] == 1);
                    if (!(wa && wb)) { ok = 0; break; }
                }
            }
            if (!ok) continue;
            int ex = x + (horiz ? run - 1 : 0), ey = y + (horiz ? 0 : run - 1);
            raycast_add_dark_room(x, y, ex, ey);
            return;
        }
    }
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
 * full-vs-partial height, rolled independently for each divider. A partial
 * divider sub-rolls low (192, see-over cubicle) vs half (96, counter/desk) at
 * 2:1 — counters read as furniture, so they stay the rarer of the two. Both
 * heights ride the same SEE-OVER tuning knob. */
static void assign_partition_decor(void) {
    for (int i = 0; i < num_partitions; i++) {
        partition_style[i]  = prob(g_procgen_params.spotted) ? 1 : 0;
        partition_height[i] = prob(g_procgen_params.lowdivs)
                            ? ((xs32() % 3 == 0) ? 96 : 192) : 0;
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
            if (raycast_exit_path_cell(cx, cy)) { ok = 0; break; }  /* never tunnel
                                                 * through the exit door's cavity */
            if (world_map[cy + dx][cx + dy] != 1 ||              /* perp sides wall   */
                world_map[cy - dx][cx - dy] != 1) { ok = 0; break; }
            int ddx = cx - SPAWN_CX, ddy = cy - SPAWN_CY;        /* keep clear of spawn */
            if (ddx > -2 && ddx < 2 && ddy > -2 && ddy < 2) { ok = 0; break; }
        }
        if (!ok) continue;
        for (int k = 0; k < L; k++) world_map[y + dy * k][x + dx * k] = 0;  /* carve passage */
        ceil_h_add_run_h(x, y, dx, dy, L, CRAWL_CEIL_H);                    /* mark it low   */
        placed++;
    }
    if (placed) return;
    /* Same guarantee as the neanderthal above: ~9% of seeds spent every dart
     * and left the level with no forced-crouch passage at all. Scan for the
     * simplest legal choke — ONE wall cell with open floor either side and
     * wall on both flanks — which is the thinnest thing that still reads as
     * a crawl. Carving only ever OPENS a cell, so connectivity cannot break. */
    {
        int seen = 0, bx = -1, by = -1, bhoriz = 0;
        for (int horiz = 0; horiz < 2; horiz++) {
            int dx = horiz ? 1 : 0, dy = horiz ? 0 : 1;
            for (int y = 2; y < MAP_H - 3; y++)
                for (int x = 2; x < MAP_W - 3; x++) {
                    if (world_map[y - dy][x - dx] != 0) continue;   /* entrance open */
                    if (world_map[y + dy][x + dx] != 0) continue;   /* exit open     */
                    if (world_map[y][x] != 1) continue;             /* wall to carve */
                    if (raycast_exit_path_cell(x, y)) continue;
                    if (world_map[y + dx][x + dy] != 1 ||           /* flanks wall   */
                        world_map[y - dx][x - dy] != 1) continue;
                    int ddx = x - SPAWN_CX, ddy = y - SPAWN_CY;
                    if (ddx > -2 && ddx < 2 && ddy > -2 && ddy < 2) continue;
                    if (xs32_range(0, seen++) == 0) { bx = x; by = y; bhoriz = horiz; }
                }
        }
        if (bx >= 0) {
            int dx = bhoriz ? 1 : 0, dy = bhoriz ? 0 : 1;
            world_map[by][bx] = 0;
            ceil_h_add_run_h(bx, by, dx, dy, 1, CRAWL_CEIL_H);
        }
    }
}

/* ── Driver ───────────────────────────────────────────────────────── */

void procgen_run(uint32_t seed) {
    pedge_clear();                     /* procgen partitions stay legacy (inc 3) */
    prng_state = seed ? seed : 1;
    for (int i = 0; i < 8; i++) xs32();   /* mix the small-seed bits */

    /* Reset partitions for this generation pass. */
    num_partitions = 0;
    num_decals = 0;                       /* outlet is lobby-only */
    standups_clear();                     /* drop leftover neanderthals/chairs from
                                           * the previous map - procgen authors none,
                                           * and a toppled one leaked in FALLEN */
    g_lobby_ceiling = 0;                  /* auto-grid ceiling for procgen */
    /* Procgen has no authored fixtures: clear any left by a custom map, or the
     * previous map's lights would light this one (init_lights runs after us). */
    g_map_lights = 0; g_map_n_lights = 0;
    g_map_dark = 0; g_map_n_dark = 0;
    ceil_h_clear();                       /* full ceilings; mark crawlspaces below */
    for (int i = 0; i < NUM_PARTITIONS_MAX; i++) {
        partition_style[i]  = 0;   /* chevron */
        partition_height[i] = 0;   /* full height */
    }

    /* Layout: a big open floor with structure dropped into it — open by
     * construction, never corridor-y. `openness` thins the structure (higher
     * openness = sparser = more wide-open floor). */
    int dens = PROCGEN_MAX_W - g_procgen_params.openness;   /* 0 = airy .. 4 = busy */
    fill_walls();
    carve_open_field();
    place_enclosed_rooms(xs32_range(5, 8 + dens));
    /* Guaranteed dark features, placed while the floor is still open so their
     * footprints land: a walled dark room, a dark room entered by a crawlspace,
     * and (after structure exists, below) a hallway of unlit cells. */
    place_dark_enclosed();
    place_dark_crawlspace();
    place_pillars(xs32_range(10, 14 + dens * 2));
    place_stub_walls(xs32_range(6, 9 + dens));
    enforce_boundary();
    clear_spawn_vestibule();
    /* The way out: every generated level gets ONE exit, and the search is part
     * of the game — a coin flip between the hinged EXIT door on the farthest
     * reachable wall face and the dark ceiling HOLE you pull up through over
     * the farthest reachable cell. Either way the spawn->exit corridor is
     * recorded and protected from later placement. Only opens into the NEXT
     * generated level. */
    if (xs32() & 1) raycast_place_exit_door();
    else            raycast_place_exit_hole();

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
    raycast_stamp_partition_edges();   /* procgen dividers go first-class */
    /* -1 vs before: place_dark_crawlspace now carves one guaranteed crawl (into
     * the dark room), so the general count drops by one to keep the overall
     * crawlspace occurrence where it was. */
    place_crawlspaces(g_procgen_params.crawlspaces);
    raycast_place_outlets(g_procgen_params.outlets * 5);
    /* 1-2 usually, 3 at the cap and rarely (Mike, 2026-08-12). This was 2-4,
     * raised back when 1-2 felt too sparse to FIND — but that was before
     * placement was guaranteed, so a "sparse" level was often a level with
     * none at all. With at least one now certain, scarcity is the point: the
     * beat is "something is standing there", and four of them on a floor
     * reads as a crowd instead. */
    {
        int neander = 1 + xs32_range(0, 1);        /* 1 or 2, the norm */
        if (xs32_range(0, 5) == 0) neander = 3;    /* 3 now and then, never more */
        place_neanderthals(neander);
    }
    /* 6-9 chairs: the directional-billboard LOD made count nearly free (far
     * chairs are small sprites; only the nearest 3 render true-3D), stress-
     * verified at 21 chairs with no frame drops. Furnished, not spammed. */
    /* Desks BEFORE chairs: each desk may pull a chair up to it, and the
     * one-asset-per-cell guard is first-come. Placing loose chairs first would
     * let them squat the knee cells and starve the pairings. 2-4 keeps a
     * generated floor sparse (authored maps may go to max_desks 8) and leaves
     * room in the 36-slot standup table for the chairs below. */
    /* The monitor first: it's the guaranteed set-piece (one per level, hard
     * budget 2) and the rarest, so it picks its floor before desks and
     * chairs crowd the cells. */
    place_pvms(1);
    place_desks(2 + xs32_range(0, 2));
    place_chairs(6 + xs32_range(0, 3));
    /* Structure exists now, so a walled corridor can be found: a stretch of
     * unlit hallway cells. */
    place_dark_hallway();
}
