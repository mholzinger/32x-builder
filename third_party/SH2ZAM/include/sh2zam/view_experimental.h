/* Experimental integer view transform, projection and clip codes for SH-2
 * renderers. Not selected by any default dispatch. MIT; SH2ZAM Contributors.
 *
 * Contract tag: view-contract-1. Struct layouts and the numeric definitions
 * below are frozen at that tag. A faster path may replace the implementation
 * only if it produces identical bytes; new behaviour gets a new function. */
#ifndef SH2ZAM_VIEW_EXPERIMENTAL_H
#define SH2ZAM_VIEW_EXPERIMENTAL_H

#include "sh2zam.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A model-space position in whole model units. Host byte order. */
typedef struct { int16_t x,y,z; } sh2z_pos16_t;

/** Camera for sh2z_view_batch.
 *
 *  r       rotation rows, Q1.14 (16384 is one). Any values are accepted if the
 *          bound below holds; an orthonormal Q1.14 rotation always does.
 *  t       translation, Q18.14 model units. Compute it with sh2z_view_place
 *          from the same quantized r, so that view = r * (p - eye) exactly up
 *          to the eye's rounding and error grows with distance from the
 *          camera rather than from the world origin.
 *  focal_x, focal_y  pixels per unit of x/z and y/z, Q16.16, both > 0.
 *  center_x, center_y  pixel position of the view axis.
 *  near_z, far_z     Q18.14, 0 < near_z < far_z.
 *  clip    inclusive pixel rectangle {left, top, right, bottom}; left <= right
 *          and top <= bottom.
 *
 *  Validity also requires, for every row i:
 *      32768 * (|r[i][0]| + |r[i][1]| + |r[i][2]|) + |t[i]| <= INT32_MAX
 *  which proves the 32-bit transform below cannot wrap for any int16 input. */
typedef struct {
    int16_t r[3][3];
    int32_t t[3];
    int32_t focal_x,focal_y;
    int16_t center_x,center_y;
    int32_t near_z,far_z;
    int16_t clip[4];
} sh2z_view_t;

#define SH2Z_OUT_NEAR   0x01u
#define SH2Z_OUT_FAR    0x02u
#define SH2Z_OUT_LEFT   0x04u
#define SH2Z_OUT_RIGHT  0x08u
#define SH2Z_OUT_TOP    0x10u
#define SH2Z_OUT_BOTTOM 0x20u

/** One transformed vertex (20 bytes).
 *
 *  x, y, z  view space, Q18.14; z > 0 is in front of the camera.
 *  sx, sy   pixels; both 0 when SH2Z_OUT_NEAR is set.
 *  outcode  SH2Z_OUT_* bits. A NEAR vertex carries no side bits, so the AND
 *           of a polygon's outcodes is a conservative trivial reject.
 *  reserved always written as zero. */
typedef struct {
    int32_t x,y,z;
    int16_t sx,sy;
    uint8_t outcode;
    uint8_t reserved[3];
} sh2z_view_vertex_t;

/** Copy *in to *out if it is a valid view (see sh2z_view_t). On failure out is
 *  unchanged. in and out may alias. */
bool sh2z_view_init(sh2z_view_t *out,const sh2z_view_t *in);

/** Set v->t from an eye position given in Q16.16 model units, using v's
 *  current rotation:
 *      t[i] = -floor((r[i][0]*eye.x + r[i][1]*eye.y + r[i][2]*eye.z) / 65536)
 *  computed exactly in 64 bits. Fails, leaving v unchanged, if a result does
 *  not fit int32 or the resulting view would be invalid. Other fields of v
 *  must already be valid. */
bool sh2z_view_place(sh2z_view_t *v,sh2z_vec3_t eye);

/** Transform, project and classify count positions.
 *
 *  Input: position k is three int16 values at
 *  (const char *)positions + k * in_stride. positions must be 2-byte aligned,
 *  in_stride even and >= 6. Output: out[0..count-1].
 *
 *  Definition, for each position p:
 *      view[i] = r[i][0]*p.x + r[i][1]*p.y + r[i][2]*p.z + t[i]   (exact, 32-bit)
 *      if view.z < near_z:
 *          outcode = NEAR, sx = sy = 0
 *      else:
 *          qx = sat32(trunc((int64)focal_x * view.x / view.z))
 *          qy = sat32(trunc((int64)focal_y * view.y / view.z))
 *          sx = sat16(center_x + (qx >> 16))     (>> is arithmetic: floor)
 *          sy = sat16(center_y - (qy >> 16))
 *          outcode = FAR if view.z > far_z
 *                  | LEFT if sx < clip[0] | RIGHT if sx > clip[2]
 *                  | TOP if sy < clip[1]  | BOTTOM if sy > clip[3]
 *  sat32/sat16 clamp to the type's range.
 *
 *  Returns false, writing nothing, if v is invalid, count > 0 with a null
 *  pointer, positions or in_stride violate the rules above, or the output
 *  overlaps the input bytes. count == 0 succeeds and permits null pointers.
 *  No other failure exists: every valid input has a defined result.
 *  No global state, allocation, floating point or hardware divider use;
 *  reentrant. */
bool sh2z_view_batch(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count);

/** Identical arguments, failures and output bytes to sh2z_view_batch, but on
 *  SH-2 it uses this CPU's hardware divider (see divu_experimental.h for the
 *  caller promise about interrupt handlers). Every division sent to the
 *  divider is proven in software to fit, so no hardware overflow or
 *  divide-by-zero result is ever used. Portable builds compute in C. */
bool sh2z_view_batch_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const void *positions,size_t in_stride,size_t count);

/* ---- Contract tag view-edges-1 ------------------------------------------- */

/** Project and classify view-space points: for each in[k], only x, y, z are
 *  read, and out[k] is exactly what sh2z_view_batch writes after its
 *  transform for that view position. in == out is allowed (projection in
 *  place); otherwise the arrays must not overlap. Returns false, writing
 *  nothing, if v is invalid, count > 0 with a null pointer, or the arrays
 *  partly overlap. count == 0 succeeds. The _divu form uses this CPU's
 *  hardware divider on SH-2 (see divu_experimental.h). */
bool sh2z_view_project(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count);
bool sh2z_view_project_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *in,size_t count);

/** Points along the view-space segment from -> to, projected.
 *
 *  Only the x, y, z of from and to are read. For each axis, with
 *  delta = to - from (exact, 64-bit) and 16 extra fraction bits:
 *      F = trunc(delta * first_num * 65536 / den)
 *      S = trunc(delta * step_num  * 65536 / den)
 *      P(k) = from + trunc((F + k * S) / 65536),   k = 0 .. count-1
 *  Each P(k) is then projected and classified exactly as sh2z_view_batch does
 *  after its transform. Six divisions per call; none per point beyond the
 *  projection. |P(k) - exact| < 1 + (k + 1) / 65536 units per axis, where
 *  exact = from + delta * (first_num + k * step_num) / den.
 *
 *  Requires den > 0 and first_num + (count - 1) * step_num <= den, so every
 *  point lies between from and to. Points are a function of the direction:
 *  stepping a shared edge from its other end can differ by one unit, so step
 *  shared edges in one canonical direction.
 *
 *  from and to are read before anything is written; they may point into out.
 *  Returns false, writing nothing, if v is invalid, count > 0 with a null
 *  pointer, den == 0 or the bound above fails. count == 0 succeeds. */
bool sh2z_view_edge_steps(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    uint16_t first_num,uint16_t step_num,uint16_t den,size_t count);

/** Clip a quad against the near plane only; side clipping is left to VDP1.
 *
 *  in[0..3] are the quad's vertices in order, as written by sh2z_view_batch
 *  with the same view. A vertex is inside when z >= near_z. Walking the edges
 *  in[i] -> in[(i+1) % 4] from i = 0, each inside vertex is copied unchanged
 *  and each edge with exactly one inside end adds its crossing point:
 *      with i the inside end and o the outside end,
 *      z = near_z,
 *      a = i.a + trunc((o.a - i.a) * (i.z - near_z) / (i.z - o.z))  for x, y,
 *  projected as sh2z_view_batch would (so never NEAR). The crossing depends
 *  only on the unordered pair of ends, so two quads sharing an edge produce
 *  the same point.
 *
 *  Returns the number of vertices written:
 *      0  all behind, or v invalid / a pointer null (nothing written)
 *      3  one in front          4  two adjacent in front, or all four
 *      5  three in front        6  two opposite in front (only possible for a
 *                                  non-planar or non-convex quad)
 *  in is read before anything is written; it may alias out. */
unsigned sh2z_view_clip_near(sh2z_view_vertex_t out[6],const sh2z_view_t *v,
    const sh2z_view_vertex_t in[4]);

/** The same results as sh2z_view_edge_steps and sh2z_view_clip_near, but on
 *  SH-2 each projection uses this CPU's hardware divider (see
 *  divu_experimental.h for the caller promise). Portable builds compute in C. */
bool sh2z_view_edge_steps_divu(sh2z_view_vertex_t *out,const sh2z_view_t *v,
    const sh2z_view_vertex_t *from,const sh2z_view_vertex_t *to,
    uint16_t first_num,uint16_t step_num,uint16_t den,size_t count);
unsigned sh2z_view_clip_near_divu(sh2z_view_vertex_t out[6],const sh2z_view_t *v,
    const sh2z_view_vertex_t in[4]);

#ifdef __cplusplus
}
#endif
#endif
