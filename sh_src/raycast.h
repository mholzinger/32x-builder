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
#define NUM_PARTITIONS_MAX  8
typedef struct { fx_t x1, y1, x2, y2; } partition_t;
extern partition_t partitions[NUM_PARTITIONS_MAX];
extern int num_partitions;

void raycast_init(void);
void raycast_render(void);
void player_update(void);
void raycast_shimmer(void);
void raycast_draw_ceiling_grid(int col_start, int col_end);
void raycast_draw_carpet(int col_start, int col_end);
void raycast_draw_walls(int col_start, int col_end);
void raycast_clear_half(int col_start, int col_end);

#endif
