#ifndef BOX_TRANSFORM_H
#define BOX_TRANSFORM_H
#include <sh2zam/sh2zam.h>
/* Scratch and output must be disjoint, each count elements long.
 * Baked box poses only: coordinate subtraction and negated depth must fit
 * int32_t. Camera subtraction precedes the dot product, as in the original renderer.
 * kernel: 0 ordinary prepared, 1 experimental MAC, 2 experimental DMULS.
 * Native candidates retain upstream qualification and fallback checks.
 * This deliberately rebuilds bounds: the exported box morphs every frame. */
bool box_transform_prepared(sh2z_vec3_t *out, sh2z_vec3_t *scratch,
    const int32_t (*vertices)[3], size_t count,
    const int32_t right[3], const int32_t up[3],
    const int32_t back[3], const int32_t position[3], unsigned kernel);
#endif
