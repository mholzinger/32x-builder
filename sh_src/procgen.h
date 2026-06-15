#ifndef PROCGEN_H
#define PROCGEN_H
#include <stdint.h>

/* Replace world_map with a procedurally generated 32×32 layout seeded
 * from the given uint32. Same seed = same layout (xorshift32 is
 * deterministic). Called from m_main when the player picks NOCLIP
 * PROCEDURAL on the title screen. */
void procgen_run(uint32_t seed);

#endif
