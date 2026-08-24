/* Chair model data for TRUE-3D rendering. draw_chair_3d() in raycast.c
 * projects these vertices through the live raycaster camera (perspective
 * divide per vertex) and rasterizes the faces with a per-column wall z-test —
 * it is real world-anchored geometry, NOT a billboard. Walk around it and it
 * is correctly oriented from every side, no view-lock hat-trick.
 *
 * Nine axis-aligned boxes (seat, two front legs, back posts split at the
 * seat plane, two back slats) in 8.8 fixed model units: feet at y=0, height
 * 1.0 (=256), centred in x/z. Scaled to CHAIR_WORLD_H_FX world units at
 * render. */
#ifndef CHAIR3D_H
#define CHAIR3D_H
#include <stdint.h>

#define CHAIR_SPRITE_KIND 3
#define CHAIR_WORLD_H_FX  24576          /* FX(0.375): world height of model 1.0 */
#define CHAIR_NBOXES 9

/* A box, or — when taper != 0 — a WEDGE: same six faces, but the TOP
 * rectangle is tx0..tx1 / tz0..tz1 instead of the base extents. That single
 * extra rectangle expresses every sloped shape the format needs (frustum,
 * single-axis ramp, asymmetric front slope) while keeping all three things
 * the axis-aligned format was chosen for:
 *
 *   - The faces stay PLANAR. Each side's top and bottom edges are parallel
 *     (both run along one axis), and two parallel lines are coplanar — so the
 *     quad rasteriser and the winding backface cull work untouched.
 *   - The solid stays CONVEX, so box_nearer's separating-axis test still
 *     holds: a gap between two bounding boxes is a gap between the hulls. It
 *     is merely conservative — a wedge pair can fall through to the 0
 *     (order-is-a-no-op) case where a tighter test would have ranked them.
 *     Those bounds must come from cbox_bounds, NOT the six base fields; see
 *     the note there, a wedge's top may overhang its base.
 *   - Shading stays a table lookup. A shallow taper's sides read correctly on
 *     chair_face_shade's six-way axis mapping; a STEEP one would want a baked
 *     normal, which is the extension point if a future model needs it.
 *
 * The six base fields come first and taper is last, so every existing
 * six-value initialiser in the bakes stays valid and zero-fills to taper = 0,
 * a plain box. */
typedef struct {
    int16_t x0, y0, z0, x1, y1, z1;
    int16_t tx0, tz0, tx1, tz1;
    uint8_t taper;
} cbox_t;
#define CM(v) ((int16_t)((v) * 256))

/* Write a box with BOX6 and a wedge with WEDGE. Brace-eliding the wedge
 * fields is legal and zero-fills correctly, but -Wmissing-field-initializers
 * then fires on every row of every model table, which buries the warnings that
 * mean something. These say the same thing and say it out loud. */
#define BOX6(x0, y0, z0, x1, y1, z1) \
    { (x0), (y0), (z0), (x1), (y1), (z1), 0, 0, 0, 0, 0 }
#define WEDGE(x0, y0, z0, x1, y1, z1, tx0, tz0, tx1, tz1) \
    { (x0), (y0), (z0), (x1), (y1), (z1), (tx0), (tz0), (tx1), (tz1), 1 }

/* One corner in model units. Bit0 = the x1 side, bit1 = top, bit2 = the z1
 * side — the indexing chair_face_v is written against. Shared by the in-game
 * renderer and the asset viewer so the two can never disagree about a wedge. */
static inline void cbox_corner(const cbox_t *b, int v,
                               int16_t *x, int16_t *y, int16_t *z) {
    int top = v & 2;
    *y = top ? b->y1 : b->y0;
    if (top && b->taper) {
        *x = (v & 1) ? b->tx1 : b->tx0;
        *z = (v & 4) ? b->tz1 : b->tz0;
    } else {
        *x = (v & 1) ? b->x1 : b->x0;
        *z = (v & 4) ? b->z1 : b->z0;
    }
}

/* True AABB of a box or wedge, as lo[3]/hi[3] in x,y,z.
 *
 * NOT just the six base fields. A wedge's top rectangle may be WIDER than its
 * base — the Master System's lower body flares out slightly as it rises, which
 * is what the model actually does — and then the base fields do not contain
 * the solid. box_nearer's separating-axis test reads these as the bounds, so
 * feeding it the base alone would let it report a gap that isn't there and
 * return a CONFIDENTLY WRONG paint order, not a conservative one. The union of
 * the two rectangles is the real bound in every case, flare or inset. */
static inline void cbox_bounds(const cbox_t *b, int16_t *lo, int16_t *hi) {
    lo[0] = b->x0; hi[0] = b->x1;
    lo[1] = b->y0; hi[1] = b->y1;
    lo[2] = b->z0; hi[2] = b->z1;
    if (!b->taper) return;
    if (b->tx0 < lo[0]) lo[0] = b->tx0;
    if (b->tx1 > hi[0]) hi[0] = b->tx1;
    if (b->tz0 < lo[2]) lo[2] = b->tz0;
    if (b->tz1 > hi[2]) hi[2] = b->tz1;
}
/* NO box interpenetrates or spans past another's occlusion plane: the painter
 * sort keys on per-triangle centroid depth, and any triangle whose depth span
 * straddles a neighbour's can win the sort while losing the geometry (post
 * slivers painted through the seat top, rail notches through the posts).
 * Hence: posts split at the seat-top plane (0.48), rails butted against the
 * posts' inner faces (|x| <= 0.20), seat flush behind at z=0.20. With disjoint
 * boxes a strict painting order exists and painter is exact.
 *
 * DELIBERATE DETAIL: the two back slats are RECESSED in depth. Their front
 * face sits at z=0.23, a 0.03 step behind the posts' front face (z=0.20),
 * so the posts stand proud and the notch reads as an intentional recessed-
 * panel joint instead of a rendering seam. The slat FACES (x width, y height)
 * are unchanged — only their depth is pulled back. They stay wholly within
 * the posts' z-span [0.20, 0.26], so the disjoint painter sort is preserved. */
static const cbox_t chair_boxes[CHAIR_NBOXES] = {
    BOX6(CM(-0.26), CM(0.42), CM(-0.26), CM( 0.26), CM(0.48), CM( 0.20)),  /* seat */
    BOX6(CM(-0.26), CM(0.00), CM(-0.26), CM(-0.20), CM(0.42), CM(-0.20)),  /* front-L */
    BOX6(CM( 0.20), CM(0.00), CM(-0.26), CM( 0.26), CM(0.42), CM(-0.20)),  /* front-R */
    BOX6(CM(-0.26), CM(0.00), CM( 0.20), CM(-0.20), CM(0.48), CM( 0.26)),  /* post BL low */
    BOX6(CM( 0.20), CM(0.00), CM( 0.20), CM( 0.26), CM(0.48), CM( 0.26)),  /* post BR low */
    BOX6(CM(-0.26), CM(0.48), CM( 0.20), CM(-0.20), CM(1.00), CM( 0.26)),  /* post BL up */
    BOX6(CM( 0.20), CM(0.48), CM( 0.20), CM( 0.26), CM(1.00), CM( 0.26)),  /* post BR up */
    BOX6(CM(-0.20), CM(0.90), CM( 0.23), CM( 0.20), CM(1.00), CM( 0.245)), /* top rail — recessed, thin */
    BOX6(CM(-0.20), CM(0.68), CM( 0.23), CM( 0.20), CM(0.76), CM( 0.245)), /* mid slat — recessed, thin */
};
#undef CM

/* Box corner index bits: bit0 = x1, bit1 = y1, bit2 = z1. Faces CCW from
 * outside; index by outward-normal axis 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z. */
static const uint8_t chair_face_v[6][4] = {
    { 1, 3, 7, 5 },   /* +x */
    { 0, 4, 6, 2 },   /* -x */
    { 2, 6, 7, 3 },   /* +y (top)    */
    { 0, 1, 5, 4 },   /* -y (bottom) */
    { 4, 5, 7, 6 },   /* +z */
    { 0, 2, 3, 1 },   /* -z */
};

#endif /* CHAIR3D_H */
