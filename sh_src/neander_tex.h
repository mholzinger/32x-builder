#ifndef NEANDER_TEX_H_INCLUDED
#define NEANDER_TEX_H_INCLUDED

#include <stdint.h>

#define NEANDER_TEX_WIDTH  16
#define NEANDER_TEX_HEIGHT 32

/* 16x32 neanderthal cardboard standup texture, generated from
 * images/neanderthal.webp. Each value is a palette offset:
 *   0 = transparent (white background outside the silhouette)
 *   1-7 = figure color, mapped to NEANDER_BASE + value */
static const uint8_t neander_tex[NEANDER_TEX_HEIGHT][NEANDER_TEX_WIDTH] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,7,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,6,0,0,0,4,3,6,0,0,0,0,0},
    {0,0,0,0,6,0,0,3,2,3,3,0,0,0,0,0},
    {0,0,0,0,6,0,7,1,2,4,3,0,0,0,0,0},
    {0,0,0,0,7,0,7,1,1,3,4,0,0,0,0,0},
    {0,0,0,0,6,6,4,1,1,1,6,0,0,0,0,0},
    {0,0,0,0,4,5,4,2,3,5,6,0,0,0,0,0},
    {0,0,0,7,3,4,4,5,4,6,6,7,0,0,0,0},
    {0,0,0,5,3,2,2,3,3,4,5,7,0,0,0,0},
    {0,0,6,1,4,3,2,4,5,5,4,7,0,0,0,0},
    {0,0,5,2,6,4,2,4,6,6,3,6,0,0,0,0},
    {0,0,7,6,7,5,3,3,5,6,3,5,0,0,0,0},
    {0,0,0,0,7,2,3,3,3,6,4,4,0,0,0,0},
    {0,0,0,0,5,2,2,3,2,5,4,4,0,0,0,0},
    {0,0,0,0,3,3,3,2,4,3,4,4,0,0,0,0},
    {0,0,0,7,2,2,3,2,4,3,3,4,0,0,0,0},
    {0,0,0,6,1,2,2,2,4,4,3,4,0,0,0,0},
    {0,0,0,6,1,2,3,2,3,4,3,6,0,0,0,0},
    {0,0,0,0,3,2,2,1,3,4,3,0,0,0,0,0},
    {0,0,0,0,7,2,2,6,3,2,6,0,0,0,0,0},
    {0,0,0,0,7,2,3,0,3,3,0,0,0,0,0,0},
    {0,0,0,0,6,2,5,0,4,3,0,0,0,0,0,0},
    {0,0,0,0,4,3,0,0,4,3,0,0,0,0,0,0},
    {0,0,0,0,4,3,0,0,3,3,0,0,0,0,0,0},
    {0,0,0,0,4,3,0,0,4,4,0,0,0,0,0,0},
    {0,0,0,0,5,4,0,0,3,6,0,0,0,0,0,0},
    {0,0,0,0,6,4,0,0,3,7,0,0,0,0,0,0},
    {0,0,0,0,5,4,0,7,2,3,7,0,0,0,0,0},
    {0,0,0,0,4,3,0,0,7,5,5,0,0,0,0,0},
    {0,0,0,0,7,7,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* Palette colors (5-bit RGB) — drop these into build_palette():
 *   NEANDER_BASE + 0: (16,11, 5)   cardboard back
 *   NEANDER_BASE + 1: ( 3, 2, 1)   figure shade 1
 *   NEANDER_BASE + 2: ( 8, 6, 4)   figure shade 2
 *   NEANDER_BASE + 3: (13,10, 8)   figure shade 3
 *   NEANDER_BASE + 4: (17,14,11)   figure shade 4
 *   NEANDER_BASE + 5: (21,18,15)   figure shade 5
 *   NEANDER_BASE + 6: (25,23,21)   figure shade 6
 *   NEANDER_BASE + 7: (27,27,26)   figure shade 7
 */

#endif
