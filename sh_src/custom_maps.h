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
#define CUSTOM_PARTITION_MAX  32    /* == NUM_PARTITIONS_MAX          */
#define CUSTOM_DECAL_MAX      16    /* == sizeof decals[] in raycast.c */
#define CUSTOM_CRAWL_MAX       8    /* == MAX_LOWCEIL_RECTS            */

/* POD mirrors of the engine structures — primitives only, so custom_maps.c
 * compiles without seeing decal_t / the partition decor statics. */
typedef struct { fx_t x1, y1, x2, y2; uint8_t style, height, crawl; } cm_partition_t;
typedef struct { fx_t x, y, z; uint8_t axis, kind; }                  cm_decal_t;
typedef struct { uint8_t cx, cy; int8_t dx, dy; uint8_t len; }        cm_crawl_t;  /* one ceil_h_add_run (dx,dy signed: N/W = -1) */

typedef struct {
    const char           *name;        /* shown in the menus; keep <= 16 chars  */
    uint8_t               w, h;        /* authored grid size (8/16/32/64, <=64)  */
    const uint8_t        *grid;        /* w*h row-major cells: 0 open,1 wall,2 void */
    const cm_partition_t *parts;  uint8_t n_parts;
    const cm_decal_t     *decals; uint8_t n_decals;
    const cm_crawl_t     *crawls; uint8_t n_crawls;
    fx_t                  spawn_x, spawn_y;  uint8_t spawn_angle;
    uint8_t               lobby_ceiling;     /* 0 = auto fixture grid (normal)   */
    uint8_t               place_outlets;     /* >0: raycast_place_outlets(N) too */
    uint8_t               place_exit_door;   /* 1: BFS-place the exit door too   */
} custom_map_t;

extern const custom_map_t custom_maps[];
extern const int          custom_map_count;   /* total, incl. lobby (load bounds) */
extern const int          custom_pick_count;  /* selectable maps (pickable roles), ordered first */
extern const int          custom_core_count;  /* core (starter/play) pickable; community span = [core, pick) */

#endif /* CUSTOM_MAPS_H */
