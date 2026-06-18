#ifndef RAYCAST_H
#define RAYCAST_H

#include <stdint.h>

#define MAP_W      32
#define MAP_H      32
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
#define NUM_PARTITIONS_MAX  12
typedef struct { fx_t x1, y1, x2, y2; } partition_t;
extern partition_t partitions[NUM_PARTITIONS_MAX];
extern int num_partitions;
/* Per-partition wallpaper: 0 = chevron (like the walls), 1 = spotted olive. */
extern uint8_t partition_style[NUM_PARTITIONS_MAX];
/* Per-partition render height: 0 = full, else fraction*256 (192 = 3/4 height)
 * for a low cubicle-style divider you see over (ceiling shows above). */
extern uint8_t partition_height[NUM_PARTITIONS_MAX];
/* When set, the ceiling uses the lobby's hand-authored fluorescent runs. */
extern int g_lobby_ceiling;

/* Wall-mounted decals (the lobby outlet). Count is reset per-map so the
 * outlet only renders in the lobby; the array itself lives in raycast.c. */
extern int num_decals;

void raycast_init(void);
/* Scale the gameplay palette to brightness 0..FADE_STEPS (full..black) for
 * the lobby->map fade. Call inside vblank. */
#define FADE_STEPS 16
void raycast_set_brightness(int lvl);
/* Fill world_map/partitions and park the player. Call before raycast_init
 * (or before re-calling init_lights) so the lighting grid matches. */
void raycast_load_fixed(void);
void raycast_load_lobby(void);
void raycast_render(void);
void player_update(uint16_t pad);
void raycast_shimmer(void);
void raycast_draw_ceiling_grid(int col_start, int col_end);
void raycast_draw_carpet(int col_start, int col_end);
void raycast_draw_walls(int col_start, int col_end);
/* Secondary CPU: drop stale partition-face cache lines before the wall pass so
 * it re-reads the primary's fresh per-frame writes. Primary never calls this. */
void raycast_purge_partition_cache(void);
void raycast_clear_half(int col_start, int col_end);

#endif
