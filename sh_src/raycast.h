#ifndef RAYCAST_H
#define RAYCAST_H

#include <stdint.h>

#define MAP_W      64
#define MAP_H      64
/* The hand-authored entry maps (fixed_map, lobby_map) are 32x32. They load into
 * the top-left of the larger live grid with the rest filled solid wall (they're
 * self-sealed entry rooms). Procgen fills the full MAP_W x MAP_H. */
#define AUTH_W     32
#define AUTH_H     32
#define SCREEN_W   320
#define SCREEN_H   224

/* 16.16 fixed point */
typedef int32_t fx_t;
#define FX_SHIFT    16
#define FX_ONE      (1 << FX_SHIFT)
#define FX(d)       ((fx_t)((d) * 65536.0))   /* compile-time constants only */
#define FX_INT(x)   ((int32_t)(x) >> FX_SHIFT)
#define FX_MUL(a,b) ((fx_t)(((int64_t)(a) * (b)) >> FX_SHIFT))
#define FX_DIV(a,b) ((fx_t)(((int64_t)(a) << FX_SHIFT) / (b)))
#define FX_ABS(x)   ((x) < 0 ? -(x) : (x))

typedef struct {
    fx_t x, y;       /* world-space position */
    uint8_t angle;   /* 0..255 = 0..2pi */
} player_t;

extern player_t player;
/* Non-const so procgen can overwrite it at boot. The hand-tuned default
 * still lives in the .data section initializer in raycast.c. */
extern uint8_t world_map[MAP_H][MAP_W];

/* Free-standing wallpaper partitions. Same data shape as in raycast.c.
 * procgen writes into partitions[] / sets num_partitions at boot. */
#define NUM_PARTITIONS_MAX  32
typedef struct { fx_t x1, y1, x2, y2; } partition_t;
extern partition_t partitions[NUM_PARTITIONS_MAX];
extern int num_partitions;
/* Per-partition wallpaper: 0 = chevron (like the walls), 1 = spotted olive. */
extern uint8_t partition_style[NUM_PARTITIONS_MAX];
/* Per-partition render height: 0 = full, else fraction*256 (192 = 3/4 height)
 * for a low cubicle-style divider you see over (ceiling shows above). */
extern uint8_t partition_height[NUM_PARTITIONS_MAX];
/* Per-partition crawl-under: 1 = open gap at the foot, solid above; collides
 * only when standing (crouch low to crawl under). */
extern uint8_t partition_crawl[NUM_PARTITIONS_MAX];
/* When set, the ceiling uses the lobby's hand-authored fluorescent runs. */
extern int g_lobby_ceiling;
/* When set, at least one low-ceiling crawlspace cell exists this map. */
extern int g_lowceil_active;
/* Per-cell ceiling height (255 = full open ceiling, lower = crawlspace slab).
 * The first-class crawlspace data model: collision, forced-crouch, light
 * culling and the slab render all read this. Loaders/procgen author it. */
extern uint8_t ceil_h[MAP_H][MAP_W];
void ceil_h_clear(void);          /* reset all cells to full-height ceiling */
void ceil_h_set_low(int cx, int cy);  /* mark a single cell as a low ceiling */
/* Mark a straight run of `len` cells from (cx,cy) along (dx,dy) as ONE
 * crawlspace (so its mouth is capped as a unit). dx,dy in {0,1}. */
void ceil_h_add_run(int cx, int cy, int dx, int dy, int len);

/* Wall-mounted decals (the lobby outlet). Count is reset per-map so the
 * outlet only renders in the lobby; the array itself lives in raycast.c. */
extern int num_decals;
/* Pepper outlets across the live world_map's visible wall faces until decals[]
 * holds `target` total. Map-agnostic; loaders and procgen both call it. */
void raycast_place_outlets(int target);

void raycast_init(void);
/* Scale the gameplay palette to brightness 0..FADE_STEPS (full..black) for
 * the lobby->map fade. Call inside vblank. */
#define FADE_STEPS 16
void raycast_set_brightness(int lvl);
/* Fill world_map/partitions and park the player. Call before raycast_init
 * (or before re-calling init_lights) so the lighting grid matches. */
void raycast_load_fixed(void);
void raycast_load_lobby(void);
/* Load a hand-authored map (index into the generated custom_maps[] table). */
void raycast_load_custom(int idx);
void raycast_render(void);
void player_update(uint16_t pad);
/* 1 when the player has stepped into the open EXIT door — fire the procgen portal. */
int  raycast_door_portal_check(void);
/* Stamp the recurring exit door (behind spawn) into the live map. procgen calls it. */
void raycast_place_exit_door(void);
void raycast_shimmer(void);
void raycast_draw_ceiling_grid(int col_start, int col_end);
void raycast_draw_carpet(int col_start, int col_end);
void raycast_draw_walls(int col_start, int col_end);
/* Secondary CPU: drop stale partition-face cache lines before the wall pass so
 * it re-reads the primary's fresh per-frame writes. Primary never calls this. */
void raycast_purge_partition_cache(void);
/* Secondary CPU: purge cell_light once per map-load (gen change) before walls. */
void raycast_purge_cell_light(void);
void raycast_clear_half(int col_start, int col_end);
/* Crawlspace tail (low-ceiling slab + bulkhead caps) for a column range — runs
 * after the wall barrier, split across both SH-2s via CMD_TAIL. */
void raycast_draw_tail(int col_start, int col_end);
/* Secondary CPU: drop stale crawlspace-geometry cache lines before CMD_TAIL. */
void raycast_purge_lowceil_cache(void);
/* Sprite pass (ceiling lights + standups) for a column range — runs after the
 * tail, split across both SH-2s in the same CMD_TAIL phase. */
void raycast_draw_sprites(int col_start, int col_end);
/* Secondary CPU: drop stale lights[] cache lines before the sprite pass. */
void raycast_purge_sprite_cache(void);

#endif
