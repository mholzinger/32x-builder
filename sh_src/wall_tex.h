#ifndef WALL_TEX_H_INCLUDED
#define WALL_TEX_H_INCLUDED

#include <stdint.h>

#define WALL_TEX_WIDTH  16
#define WALL_TEX_HEIGHT 16

/* Column-major: tex[texX][texY]. Each screen column samples
 * a contiguous run of TEX_H bytes during the per-column
 * shade_lut build, so the SH-2 cache loads one strip and
 * the loop hits every line. */
static const uint8_t wall_tex[WALL_TEX_WIDTH][WALL_TEX_HEIGHT] = {
    {3,3,3,3,3,3,3,4,3,3,3,3,3,3,4,2},
    {0,0,1,1,0,2,4,2,1,0,1,0,0,1,0,1},
    {3,2,2,3,2,3,3,3,2,3,2,2,2,2,3,2},
    {2,3,3,3,2,2,2,2,2,2,2,4,4,3,2,2},
    {0,3,3,2,1,0,1,0,1,0,1,4,2,2,0,1},
    {3,3,3,3,3,3,3,4,3,4,3,3,4,3,4,3},
    {2,1,1,1,1,2,3,3,2,1,1,1,1,1,1,1},
    {1,1,1,1,1,2,4,2,1,1,1,1,1,1,1,1},
    {4,3,3,3,3,4,3,4,3,4,3,3,4,3,4,3},
    {1,3,3,2,1,1,1,1,1,1,1,4,3,2,1,1},
    {2,3,3,3,2,2,2,2,2,2,2,4,3,2,2,2},
    {3,3,3,3,3,3,3,4,3,3,3,3,3,3,3,3},
    {0,0,0,0,0,2,3,2,1,0,0,0,0,0,0,1},
    {3,2,2,2,2,3,3,4,2,3,2,2,3,2,3,2},
    {2,3,3,3,2,2,2,2,2,2,2,4,4,2,2,2},
    {0,3,3,2,1,0,0,0,0,0,1,4,2,1,0,0},
};

#endif
