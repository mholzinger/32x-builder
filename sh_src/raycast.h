#ifndef RAYCAST_H
#define RAYCAST_H

#include <stdint.h>

#define MAP_W      16
#define MAP_H      16
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
extern const uint8_t world_map[MAP_H][MAP_W];

void raycast_init(void);
void raycast_render(void);
void player_update(void);
void raycast_shimmer(void);

#endif
