#ifndef CUSTOM_MAPS_H
#define CUSTOM_MAPS_H
/* Hand-authored level descriptors compiled into the ROM.
 *
 * tools/gen_maps.py reads maps/ *.map (human-readable text) + registry.json and
 * emits sh_src/custom_maps.c, which defines the custom_maps[] table below. The
 * generated file is PLAIN POD ONLY — it never touches the engine's private
 * decal_t / partition decor encodings. raycast_load_custom() (in raycast.c,
 * where those internals are visible) replays a descriptor into the live world,
 * exactly like raycast_load_fixed(). Adding/regenerating maps needs no edits
 * here; this header is the stable contract between the two. */
#include <stdint.h>
#include "raycast.h"   /* fx_t, FX, MAP_W/H, NUM_PARTITIONS_MAX */

/* Caps mirror the engine arrays the loader replays into. The codegen also
 * enforces these (and a _Static_assert below keeps them honest). */
#define CUSTOM_MAP_MAX_DIM    64    /* grid width/height upper bound  */
#define CUSTOM_DECAL_MAX      16    /* == sizeof decals[] in raycast.c */
#define CUSTOM_CRAWL_MAX       8    /* == MAX_LOWCEIL_RECTS            */
#define CUSTOM_LIGHT_MAX     512    /* == MAX_LIGHTS in raycast.c      */
#define CUSTOM_DARK_MAX       64    /* only bound is uint8 n_dark (255) */

/* POD mirrors of the engine structures — primitives only, so custom_maps.c
 * compiles without seeing decal_t / the partition decor statics. */
/* Edge-partition: one CELL-EDGE of a divider (first-class model). x,y locate
 * the edge's cell; flags: bit7 always set (present), bit6 axis (0 = the WEST
 * edge of cell x — a vertical divider on the line x; 1 = the NORTH edge —
 * horizontal on the line y), bit0 style (spotted), bits1-2 height class
 * (0 full, 1 low/192, 2 half/96). The codegen rasterizes each [partitions]
 * segment into these; the DDA hits them like walls (no per-column
 * intersection math), which is what makes partitions first-class: any
 * length, no global count cap. */
typedef struct { uint8_t x, y, flags; } cm_pedge_t;
#define CM_PEDGE_PRESENT 0x80
#define CM_PEDGE_AXIS_N  0x40   /* set: north edge (horizontal); clear: west */
#define CM_PEDGE_SPOTTED 0x01
#define CM_PEDGE_HCLASS(f) (((f) >> 1) & 3)   /* 0 full, 1 low, 2 half */
/* Slab shift: by default the slab is CENTERED on its line. When the run is
 * COLLINEAR with an adjacent wall face, the codegen/stamper shifts it so the
 * slab's face lands exactly on the wall's plane (no 0.05 seam jog):
 * FLUSH_LO = slab on the positive side of the line (face on the line from
 * below/west), FLUSH_HI = slab on the negative side (face on the line from
 * above/east). */
#define CM_PEDGE_FLUSH_LO 0x10
#define CM_PEDGE_FLUSH_HI 0x20
typedef struct { fx_t x, y, z; uint8_t axis, kind, facing; }          cm_decal_t;  /* facing: engine angle (E0 S64 W128 N192); free-standing kinds use it, wall decals ignore it */
typedef struct { uint8_t cx, cy; int8_t dx, dy; uint8_t len, h; }     cm_crawl_t;  /* one ceil_h_add_run_h (dx,dy signed: N/W = -1; h = slab ceil_h, registry crawl.h) */
typedef struct cm_light_s { uint8_t cx, cy; }                        cm_light_t;  /* authored ceiling fixture, cell coords */
typedef struct cm_dark_s { uint8_t x0, y0, x1, y1; }                 cm_dark_t;   /* DARK ROOM: unlit rect, inclusive cells */

typedef struct {
    const char           *name;        /* shown in the menus; keep <= 16 chars  */
    const char           *author;      /* map credit: automap top + pause CREDITS.
                                        * "" => the project (shown as -BACKROOMS-). */
    uint8_t               w, h;        /* authored grid size (8/16/32/64, <=64)  */
    const uint8_t        *grid;        /* w*h row-major cells: 0 open,1 wall,2 void */
    const cm_pedge_t     *pedges; uint8_t n_pedges;   /* rasterized partitions */
    const cm_decal_t     *decals; uint8_t n_decals;
    const cm_crawl_t     *crawls; uint8_t n_crawls;
    /* Authored ceiling fixtures. n_lights == 0 => init_lights() falls back to
     * its procedural every-other-cell grid (what every map did before). */
    const cm_light_t     *lights; uint16_t n_lights;
    /* Dark rooms: rects that get no ceiling fixtures and render toward fog —
     * "lit only by whatever leaks in". n_dark == 0 => the map is lit normally. */
    const cm_dark_t      *dark;   uint8_t n_dark;   /* >255 is real: a 32x32 can want 468 */
    fx_t                  spawn_x, spawn_y;  uint8_t spawn_angle;
    uint8_t               lobby_ceiling;     /* 0 = auto fixture grid (normal)   */
    uint8_t               place_outlets;     /* >0: raycast_place_outlets(N) too */
    uint8_t               place_exit_door;   /* 1: BFS-place the exit door too   */
    int8_t                next_map;          /* story chain: exit door leads to
                                              * custom_maps[next_map]; -1 = none
                                              * (door falls through to procgen).
                                              * Codegen resolves the .map file's
                                              * `next: NAME` to this index.     */
} custom_map_t;

extern const custom_map_t custom_maps[];
extern const int          custom_map_count;   /* total, incl. lobby (load bounds) */
extern const int          custom_pick_count;  /* selectable maps (pickable roles), ordered first */
extern const int          custom_start_count; /* starter maps: [0, start); play/test: [start, core) */
/* Tier blocks, in custom_maps[] order (gen_maps sorts by role priority):
 *   [0, start)            starter        "-- START MAPS --"
 *   [start, core)         play + test    "-- TEST --"
 *   [core, core+curated)  curated        "-- MAPS --" / "-- STORIES --"
 *   [core+curated, pick)  community      "-- COMMUNITY --"
 * The community block is EMPTY in the flagship ROM: those maps compile in only
 * for `make community` / `make author AUTHOR=<handle>`. */
extern const int          custom_core_count;
extern const int          custom_curated_count;
/* "" on the flagship, else "COMMUNITY BUILD" / "BUILD FOR <AUTHOR>" — printed
 * on the start menu + CREDITS so a side build never passes for the release. */
extern const char         custom_build_label[];

#endif /* CUSTOM_MAPS_H */
