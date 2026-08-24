#!/usr/bin/env python3
"""Bake a GLB/glTF model into a DISJOINT axis-aligned box list for the in-game
true-3D renderer (draw_model_3d in raycast.c). Headless Blender:

    blender --background --python tools/bake_boxes.py -- \
        models/desk.glb sh_src/desk3d.h DESK [max_boxes] [res]

Why boxes and not the baked tri-mesh: the in-game path projects 8 corners per
box and paints faces with a per-triangle centroid painter sort. That sort is
only EXACT when no box interpenetrates another's occlusion plane — see the long
comment in chair3d.h, where the chair's 9 boxes were hand-placed to guarantee
it. This tool earns the same guarantee automatically: it voxelizes the mesh and
carves the volume into boxes that CONSUME their voxels, so two boxes can never
overlap by construction.

Pipeline:
  - import, join, apply transforms, triangulate
  - normalize to the engine's model space: feet at y=0, height 1.0 (=256 in
    8.8), centred in x/z, Blender Z-up -> engine Y-up. Identical convention to
    bake_model.py, so a model's box and mesh bakes line up.
  - voxelize by ray-parity inside-test against a BVH
  - greedily extract maximal all-filled boxes, largest first, until the box cap
    or the coverage target is hit
  - emit <PREFIX>_boxes[] in cbox_t layout, plus the world height constant

The face-vertex table and per-face shading are shared engine-side (chair_face_v
/ chair_face_shade), so a box model carries no baked shade — it lights live off
its facing, same as the chair.
"""
import bpy, sys, math
from mathutils import Vector
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(argv) < 3:
    print("usage: blender --background --python tools/bake_boxes.py -- "
          "in.glb out.h PREFIX [max_boxes] [res]")
    sys.exit(1)
SRC, OUT, PREFIX = argv[0], argv[1], argv[2].upper()
MAX_BOXES = int(argv[3]) if len(argv) > 3 else 12
RES = int(argv[4]) if len(argv) > 4 else 24        # voxels along model height

# Boxes to ADD after the carve, in model units (8.8), as
# "x0,y0,z0,x1,y1,z1;x0,y0,...". The carve can only reproduce mass the source
# mesh HAS; some furniture needs a piece the download omits. The desk is the
# case in point: its knee space is completely open, so crouching in-game looked
# straight through the desk and it read as hollow. A modesty panel is one box.
# Extras are disjointness-checked against the carve exactly like carved boxes,
# so a bad hand box fails the bake instead of corrupting the painter sort.
EXTRA = __import__('os').environ.get('BAKE_EXTRA', '')

# Mirror-symmetrize the box list about the model's x centre. Most furniture is
# bilaterally symmetric, but the voxel carve is not: the desk's two pedestals
# came out 154 vs 140 wide and 231 vs 243 deep, so one presented a proper 3/4
# corner and the other read as a flat slab from the same viewpoint. Averaging
# each box with its mirror partner makes both sides render identically.
SYMMETRY = __import__('os').environ.get('BAKE_SYMMETRY', '')

# Stop carving once this much of the filled volume is covered — the tail of a
# box decomposition is always slivers, and slivers cost a full 6-face projection
# each while contributing a few pixels.
COVERAGE = float(__import__('os').environ.get('BAKE_COVERAGE', '0.92'))

# Fraction of a candidate slab that must be inside the model. Below 1.0 a box
# may swallow small voids — the difference between tracing every drawer recess
# and taking the BASIC SHAPE. 1.0 reproduces a strict volume decomposition.
SLAB_TOL = float(__import__('os').environ.get('BAKE_SLAB_TOL', '0.6'))

# ---------------------------------------------------------------- import
bpy.ops.wm.read_factory_settings(use_empty=True)
if SRC.lower().endswith((".glb", ".gltf")):
    bpy.ops.import_scene.gltf(filepath=SRC)
else:
    try:
        bpy.ops.wm.obj_import(filepath=SRC)
    except AttributeError:
        bpy.ops.import_scene.obj(filepath=SRC)

meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if not meshes:
    print("no meshes in", SRC); sys.exit(1)
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
ob = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.modifier_add(type='TRIANGULATE')
bpy.ops.object.modifier_apply(modifier=ob.modifiers[-1].name)

# ------------------------------------------------- normalize to model space
# Blender is Z-up; the engine is Y-up. Map (bx, by, bz) -> (x, y, z) = (bx, bz, by).
mn = Vector((1e9, 1e9, 1e9)); mx = Vector((-1e9, -1e9, -1e9))
for v in ob.data.vertices:
    for i in range(3):
        mn[i] = min(mn[i], v.co[i]); mx[i] = max(mx[i], v.co[i])
height = mx[2] - mn[2]
if height <= 0:
    print("degenerate model height"); sys.exit(1)
scale = 1.0 / height
cx = (mn[0] + mx[0]) * 0.5
cy = (mn[1] + mx[1]) * 0.5
for v in ob.data.vertices:
    v.co = Vector(((v.co[0] - cx) * scale,
                   (v.co[1] - cy) * scale,
                   (v.co[2] - mn[2]) * scale))
ob.data.update()

mn = Vector((1e9, 1e9, 1e9)); mx = Vector((-1e9, -1e9, -1e9))
for v in ob.data.vertices:
    for i in range(3):
        mn[i] = min(mn[i], v.co[i]); mx[i] = max(mx[i], v.co[i])
print("normalized bbox (blender xyz):",
      tuple(round(a, 3) for a in mn), tuple(round(a, 3) for a in mx))

# ------------------------------------------------------------- voxelize
# Grid spans the bbox with RES cells along height (blender z / engine y).
cell = (mx[2] - mn[2]) / RES
nx = max(1, int(math.ceil((mx[0] - mn[0]) / cell)))
ny = max(1, int(math.ceil((mx[1] - mn[1]) / cell)))
nz = RES
print(f"voxel grid {nx} x {ny} x {nz} (cell {cell:.4f})")

bvh = BVHTree.FromObject(ob, bpy.context.evaluated_depsgraph_get())

def inside(p):
    """Ray-parity inside test. Three axes voted, so a ray grazing a coplanar
    face (common on the flat slabs a desk is made of) cannot decide the cell."""
    votes = 0
    for d in (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))):
        hits, o = 0, p.copy()
        while True:
            loc, nor, idx, dist = bvh.ray_cast(o, d)
            if loc is None:
                break
            hits += 1
            o = loc + d * 1e-5
            if hits > 64:
                break
        votes += hits & 1
    return votes >= 2

filled = bytearray(nx * ny * nz)
def gi(i, j, k):
    return (k * ny + j) * nx + i

nfill = 0
for k in range(nz):
    for j in range(ny):
        for i in range(nx):
            p = Vector((mn[0] + (i + 0.5) * cell,
                        mn[1] + (j + 0.5) * cell,
                        mn[2] + (k + 0.5) * cell))
            if inside(p):
                filled[gi(i, j, k)] = 1
                nfill += 1
print(f"filled voxels: {nfill} / {nx*ny*nz} (parity)")
if nfill == 0:
    print("nothing solid — model may be open/non-manifold"); sys.exit(1)

# ------------------------------------------------------ solidify interiors
# Furniture models are SHELLS: a desk pedestal is four panels around an air gap,
# and ray parity calls that gap "outside" — which made the left pedestal bake as
# a box floating 30% off the floor. Flood-fill the empty space from the grid
# boundary; anything empty the flood never reaches is enclosed interior, so
# promote it to solid. This is what makes a hollow model behave like the solid
# object it depicts, and it is why the box fit can reach the floor at all.
OUT_ = 2
flood = bytearray(nx * ny * nz)
stack = []
for k in range(nz):
    for j in range(ny):
        for i in range(nx):
            if (i in (0, nx-1) or j in (0, ny-1) or k in (0, nz-1)):
                g = gi(i, j, k)
                if not filled[g] and not flood[g]:
                    flood[g] = OUT_; stack.append((i, j, k))
while stack:
    i, j, k = stack.pop()
    for di, dj, dk in ((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)):
        a, b, c = i+di, j+dj, k+dk
        if 0 <= a < nx and 0 <= b < ny and 0 <= c < nz:
            g = gi(a, b, c)
            if not filled[g] and not flood[g]:
                flood[g] = OUT_; stack.append((a, b, c))
sealed = 0
for g in range(nx * ny * nz):
    if not filled[g] and not flood[g]:
        filled[g] = 1; sealed += 1
nfill += sealed
print(f"solidified {sealed} enclosed cells -> {nfill} filled")

# --------------------------------------------------- greedy box extraction
claimed = bytearray(nx * ny * nz)   # cells taken by some box (voids included)

def box_ok(i0, i1, j0, j1, k0, k1):
    """A slab may be added if NO cell is already claimed by another box (hard —
    this is what keeps the decomposition disjoint) and at least SLAB_TOL of its
    cells are inside the model (soft — this is what lets a box swallow a small
    void and take the BASIC SHAPE instead of stopping at every recess).

    Returns the count of model cells gained, or -1 if the slab is rejected."""
    tot = 0; hit = 0
    for k in range(k0, k1 + 1):
        for j in range(j0, j1 + 1):
            base = (k * ny + j) * nx
            for i in range(i0, i1 + 1):
                if claimed[base + i]:
                    return -1
                tot += 1
                hit += filled[base + i]
    if tot == 0 or hit < SLAB_TOL * tot:
        return -1
    return hit

def grow(i, j, k, extent=None):
    """Greedy 6-direction expansion: repeatedly add whichever slab is legal and
    buys the most model volume. `extent` re-grows an existing box instead of
    seeding from one voxel."""
    if extent is not None:
        i0, i1, j0, j1, k0, k1 = extent
    else:
        i0 = i1 = i; j0 = j1 = j; k0 = k1 = k
    while True:
        best, bvol = None, 0
        cands = (('i0', i0 - 1, i0 - 1, j0, j1, k0, k1),
                 ('i1', i1 + 1, i1 + 1, j0, j1, k0, k1),
                 ('j0', i0, i1, j0 - 1, j0 - 1, k0, k1),
                 ('j1', i0, i1, j1 + 1, j1 + 1, k0, k1),
                 ('k0', i0, i1, j0, j1, k0 - 1, k0 - 1),
                 ('k1', i0, i1, j0, j1, k1 + 1, k1 + 1))
        for tag, a0, a1, b0, b1, c0, c1 in cands:
            if a0 < 0 or b0 < 0 or c0 < 0 or a1 >= nx or b1 >= ny or c1 >= nz:
                continue
            gain = box_ok(a0, a1, b0, b1, c0, c1)
            if gain <= 0:
                continue
            if gain > bvol:
                bvol, best = gain, tag
        if best is None:
            break
        if   best == 'i0': i0 -= 1
        elif best == 'i1': i1 += 1
        elif best == 'j0': j0 -= 1
        elif best == 'j1': j1 += 1
        elif best == 'k0': k0 -= 1
        else:              k1 += 1
    return i0, i1, j0, j1, k0, k1

def consume(i0, i1, j0, j1, k0, k1):
    """Claim every cell in the box — voids included. Claiming the voids is what
    makes overlap impossible once a box has swallowed one."""
    got = 0
    for k in range(k0, k1 + 1):
        for j in range(j0, j1 + 1):
            base = (k * ny + j) * nx
            for i in range(i0, i1 + 1):
                if not claimed[base + i]:
                    claimed[base + i] = 1
                    got += filled[base + i]
    return got

boxes = []
covered = 0
# Seed stride keeps the per-round search tractable on a fine grid while still
# sampling every region; stride 1 once the remaining volume is small.
while len(boxes) < MAX_BOXES and covered < COVERAGE * nfill:
    remaining = nfill - covered
    if remaining <= 0:
        break
    stride = 2 if remaining > 4000 else 1
    best, bvol = None, 0
    for k in range(0, nz, stride):
        for j in range(0, ny, stride):
            for i in range(0, nx, stride):
                g = gi(i, j, k)
                if claimed[g] or not filled[g]:
                    continue
                b = grow(i, j, k)
                vol = sum(filled[(kk * ny + jj) * nx + ii]
                          for kk in range(b[4], b[5] + 1)
                          for jj in range(b[2], b[3] + 1)
                          for ii in range(b[0], b[1] + 1))
                if vol > bvol:
                    bvol, best = vol, b
    if best is None or bvol == 0:
        break
    covered += consume(*best)
    boxes.append(best)
    print(f"  box {len(boxes)}: model vol {bvol}  coverage {covered/nfill*100:.1f}%")

print(f"boxes: {len(boxes)}, coverage {covered/nfill*100:.1f}%")

# --------------------------------------------------------- reclaim pass
# The carve above picks each box to maximize VOLUME, which at a tight box budget
# leaves real form unrepresented — a desk came out floating, because the greedy
# winner was the desktop slab and the pedestal bottoms were never claimed.
# Re-grow every box into whatever filled volume is still unclaimed. Growth only
# enters unclaimed cells and claims what it takes, so boxes stay disjoint. Same
# box count, same face cost, truer silhouette.
def filled_in(b):
    return sum(filled[(k * ny + j) * nx + i]
               for k in range(b[4], b[5] + 1)
               for j in range(b[2], b[3] + 1)
               for i in range(b[0], b[1] + 1))

def unclaim(b):
    for k in range(b[4], b[5] + 1):
        for j in range(b[2], b[3] + 1):
            base = (k * ny + j) * nx
            for i in range(b[0], b[1] + 1):
                claimed[base + i] = 0

for _ in range(2):
    gained = 0
    for bi, b in enumerate(boxes):
        before = filled_in(b)
        unclaim(b)                       # so grow() can re-expand from this box
        nb = grow(0, 0, 0, extent=b)
        consume(*nb)
        gained += filled_in(nb) - before
        boxes[bi] = nb
    covered += gained
    if gained == 0:
        break
print(f"after reclaim: coverage {covered/nfill*100:.1f}%")

# ------------------------------------------------------------- weld pass
# Real furniture has hairline reveals: the desk's pedestals stopped at y=230
# and its top started at y=243, so the baked model rendered as a floating
# tabletop. At voxel scale that band is a few cells of nothing, but on screen
# it reads as broken. Close any gap up to WELD cells between two boxes that
# already face each other (overlapping in the other two axes) by growing one
# to meet the other. The gap volume must be entirely unclaimed, so welding
# cannot introduce an overlap.
WELD = max(1, int(round(0.08 * RES)))
AX = ((0, 1), (2, 3), (4, 5))

def overlaps_other(A, B, axis):
    for o in range(3):
        if o == axis:
            continue
        lo, hi = AX[o]
        if not (A[lo] <= B[hi] and B[lo] <= A[hi]):
            return False
    return True

def region_unclaimed(i0, i1, j0, j1, k0, k1):
    for k in range(k0, k1 + 1):
        for j in range(j0, j1 + 1):
            base = (k * ny + j) * nx
            for i in range(i0, i1 + 1):
                if claimed[base + i]:
                    return False
    return True

welded = 0
for _ in range(2):
    for ai in range(len(boxes)):
        for axis in range(3):
            lo, hi = AX[axis]
            for sign in (+1, -1):
                best_gap = None
                for bi in range(len(boxes)):
                    if bi == ai:
                        continue
                    A, B = boxes[ai], boxes[bi]
                    if not overlaps_other(A, B, axis):
                        continue
                    gap = (B[lo] - A[hi] - 1) if sign > 0 else (A[lo] - B[hi] - 1)
                    if 0 < gap <= WELD and (best_gap is None or gap < best_gap):
                        best_gap = gap
                if best_gap is None:
                    continue
                A = list(boxes[ai])
                span = [A[0], A[1], A[2], A[3], A[4], A[5]]
                if sign > 0:
                    span[lo], span[hi] = A[hi] + 1, A[hi] + best_gap
                else:
                    span[lo], span[hi] = A[lo] - best_gap, A[lo] - 1
                if not region_unclaimed(*span):
                    continue
                consume(*span)
                A[hi if sign > 0 else lo] += best_gap * sign
                boxes[ai] = tuple(A)
                welded += 1
print(f"welded {welded} seam(s) (max {WELD} cells)")

# Disjointness is the renderer's correctness precondition, not a nicety — an
# overlap silently corrupts the painter sort (slivers painted through solids).
# Consumption should make it impossible; assert rather than trust.
for a in range(len(boxes)):
    for b in range(a + 1, len(boxes)):
        p, r = boxes[a], boxes[b]
        if (p[0] <= r[1] and r[0] <= p[1] and
            p[2] <= r[3] and r[2] <= p[3] and
            p[4] <= r[5] and r[4] <= p[5]):
            print(f"FATAL: box {a} and box {b} overlap"); sys.exit(1)
print("disjointness: OK")

# ------------------------------------------------------------- emit header
# Grid index -> model space. Engine axes: x = blender x, y = blender z (up),
# z = blender y. Model space is 8.8 with height 1.0 = 256.
def to_model(b):
    i0, i1, j0, j1, k0, k1 = b
    x0 = mn[0] + i0 * cell;        x1 = mn[0] + (i1 + 1) * cell
    z0 = mn[1] + j0 * cell;        z1 = mn[1] + (j1 + 1) * cell
    y0 = mn[2] + k0 * cell;        y1 = mn[2] + (k1 + 1) * cell
    return x0, y0, z0, x1, y1, z1

def q(v):
    return int(round(v * 256))

model_boxes = [to_model(b) for b in boxes]
n_carved = len(model_boxes)
if EXTRA.strip():
    for spec in EXTRA.split(";"):
        spec = spec.strip()
        if not spec:
            continue
        vals = [float(v) / 256.0 for v in spec.split(",")]
        if len(vals) != 6:
            print("FATAL: --extra box needs 6 values, got", spec); sys.exit(1)
        model_boxes.append(tuple(vals))
    print(f"added {len(model_boxes) - n_carved} hand box(es)")

# --------------------------------------------------------- symmetry pass
if SYMMETRY.strip().lower() == "x":
    def _c(b): return (b[0] + b[3]) * 0.5
    straddle, side = [], []
    for b in model_boxes:
        (straddle if b[0] < 0 < b[3] else side).append(b)
    # A box crossing the centre just gets a centred x span.
    fixed = []
    for b in straddle:
        h = max(abs(b[0]), abs(b[3]))
        fixed.append((-h, b[1], b[2], h, b[4], b[5]))
    # Everything else pairs with its nearest mirror partner and both become the
    # average; an unpaired box is mirrored to create its own partner.
    left  = sorted([b for b in side if _c(b) < 0], key=_c)
    right = sorted([b for b in side if _c(b) > 0], key=_c)
    used = set()
    for L in left:
        best, bd = None, None
        for i, R in enumerate(right):
            if i in used:
                continue
            d = abs(_c(L) + _c(R)) + abs((L[3]-L[0]) - (R[3]-R[0]))
            if bd is None or d < bd:
                bd, best = d, i
        if best is None:
            m = (-L[3], L[1], L[2], -L[0], L[4], L[5])
            fixed += [L, m]
            continue
        R = right[best]; used.add(best)
        x0 = (L[0] + -R[3]) * 0.5
        x1 = (L[3] + -R[0]) * 0.5
        y0 = min(L[1], R[1]); y1 = max(L[4], R[4])
        z0 = min(L[2], R[2]); z1 = max(L[5], R[5])
        fixed.append(( x0, y0, z0,  x1, y1, z1))
        fixed.append((-x1, y0, z0, -x0, y1, z1))
    for i, R in enumerate(right):
        if i not in used:
            fixed += [R, (-R[3], R[1], R[2], -R[0], R[4], R[5])]
    model_boxes = fixed
    n_carved = len(model_boxes)
    print(f"symmetrized about x -> {len(model_boxes)} box(es)")

# ------------------------------------------------------------ merge pass
# Two boxes that match exactly on two axes and abut on the third are ONE box.
# The carve produces these naturally (a slab split across a seam), and so does
# a hand box tucked against a carved one — narrowing the desk's knee added two
# fillers flush to the pedestals, which is 12 extra faces for a silhouette a
# merged box draws identically. Fewer boxes is strictly cheaper: the renderer
# projects 8 corners and up to 6 faces per box, every frame, per CPU.
def _q6(b):
    return tuple(q(v) for v in b)

merged = True
while merged:
    merged = False
    for a in range(len(model_boxes)):
        for b in range(a + 1, len(model_boxes)):
            A, B = _q6(model_boxes[a]), _q6(model_boxes[b])
            for ax in range(3):
                o1, o2 = (ax + 1) % 3, (ax + 2) % 3
                if (A[o1] != B[o1] or A[o1+3] != B[o1+3] or
                    A[o2] != B[o2] or A[o2+3] != B[o2+3]):
                    continue
                if A[ax+3] == B[ax] or B[ax+3] == A[ax]:      # touching, no overlap
                    lo = min(A[ax], B[ax]) / 256.0
                    hi = max(A[ax+3], B[ax+3]) / 256.0
                    nb = list(model_boxes[a])
                    nb[ax], nb[ax+3] = lo, hi
                    model_boxes[a] = tuple(nb)
                    del model_boxes[b]
                    if b < n_carved:
                        n_carved -= 1
                    merged = True
                    break
            if merged: break
        if merged: break
print(f"merged to {len(model_boxes)} box(es) -> {len(model_boxes)*6} faces")

# Disjointness in MODEL space over the FINAL list — the grid-space check above
# cannot see hand-added boxes, and they are exactly the ones most likely to
# overlap something.
# Compare the QUANTIZED 8.8 values, not the floats: the header ships ints, and
# a carve edge landing a hair under a hand box's edge in float space is not a
# real overlap once both round to the same 8.8 tick.
qb = [tuple(q(v) for v in mb) for mb in model_boxes]
for a in range(len(qb)):
    for b in range(a + 1, len(qb)):
        p_, r_ = qb[a], qb[b]
        if (p_[0] < r_[3] and r_[0] < p_[3] and
            p_[1] < r_[4] and r_[1] < p_[4] and
            p_[2] < r_[5] and r_[2] < p_[5]):
            print(f"FATAL: box {a} and box {b} overlap in model space"); sys.exit(1)
print("model-space disjointness: OK")

guard = f"{PREFIX}3D_H"
lines = []
lines.append(f"/* Auto-generated by tools/bake_boxes.py from "
             f"{SRC.split('/')[-1]} — do not edit.\n"
             f" * {len(boxes)} DISJOINT axis-aligned boxes covering "
             f"{covered/nfill*100:.1f}% of the model volume.\n"
             f" * Disjoint by construction (each box consumes its voxels), so the\n"
             f" * renderer's centroid painter sort is exact — see chair3d.h. */")
lines.append(f"#ifndef {guard}")
lines.append(f"#define {guard}")
lines.append("#include <stdint.h>")
lines.append("#include \"chair3d.h\"        /* cbox_t, chair_face_v */")
lines.append("")
lines.append(f"#define {PREFIX}_NBOXES {len(model_boxes)}")
lines.append("")
lines.append(f"static const cbox_t {PREFIX.lower()}_boxes[{PREFIX}_NBOXES] = {{")
for bi_, mb in enumerate(model_boxes):
    x0, y0, z0, x1, y1, z1 = mb
    lines.append(f"    BOX6({q(x0):5d}, {q(y0):5d}, {q(z0):5d}, "
                 f"{q(x1):5d}, {q(y1):5d}, {q(z1):5d}),"
                 + ("   /* hand-added */" if bi_ >= n_carved else ""))
lines.append("};")
lines.append("")
lines.append(f"#endif /* {guard} */")

with open(OUT, "w") as f:
    f.write("\n".join(lines) + "\n")
print("wrote", OUT)
