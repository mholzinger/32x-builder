#ifndef PROCGEN_H
#define PROCGEN_H
#include <stdint.h>

/* Player-tunable generation weights. Each knob is 0..PROCGEN_MAX_W
 * (NONE / LOW / MED / HIGH / MAX) and is dialed in the lobby tuning
 * screen before walking the exit. procgen_run reads g_procgen_params. */
#define PROCGEN_MAX_W 4

typedef struct {
    uint8_t openness;     /* room count / how carved-out the map is        */
    uint8_t partitions;   /* free-standing wallpaper divider density       */
    uint8_t crawlspaces;  /* low-ceiling crawl-tube frequency              */
    uint8_t outlets;      /* wall electrical-outlet density                */
    uint8_t spotted;      /* spotted-vs-chevron partition ratio            */
    uint8_t lowdivs;      /* partial-height (see-over) divider ratio       */
} procgen_params_t;

/* Live weights driving the next generation. Defaults to a balanced preset
 * in procgen_params_default(); the lobby UI overwrites it. */
extern procgen_params_t g_procgen_params;

/* Reset g_procgen_params to the balanced default preset. */
void procgen_params_default(void);

/* Replace world_map (and partitions/decals/ceil_h) with a procedurally
 * generated 32×32 layout seeded from the given uint32, using the current
 * g_procgen_params weights. Same seed + same weights = same layout. */
void procgen_run(uint32_t seed);

#endif
