"""Headless Blender script: rasterize a .blend file's geometry into a 2D
floor-plan grid by ray-casting straight down through the model.

Usage:
    blender --background <path-to-.blend> --python extract_floorplan.py -- \
        --resolution 64 --output floorplan.txt

The resolution controls grid cells per world unit. We sample on a regular
XY grid covering the model's bounding box, fire a ray straight down at each
cell, and mark it WALL (1) if any face above floor height is hit, FLOOR (0)
otherwise. Output: ASCII grid + optional C array for raycast.h.

Robust to whatever the .blend contains — we don't rely on object names.
"""

import sys
import os

# Blender API
import bpy
from mathutils import Vector

# --- arg parsing (after the -- separator) ---
argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
RES = 64
OUT = "/tmp/floorplan.txt"
for i, a in enumerate(argv):
    if a == "--resolution" and i + 1 < len(argv):
        RES = int(argv[i + 1])
    if a == "--output" and i + 1 < len(argv):
        OUT = argv[i + 1]

# --- collect all mesh geometry into one combined bounding box ---
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    print("[extract] no mesh objects in scene", file=sys.stderr)
    sys.exit(1)

depsgraph = bpy.context.evaluated_depsgraph_get()

# bounding box across all meshes (in world coords)
xs, ys, zs = [], [], []
for o in meshes:
    for v in o.bound_box:
        wv = o.matrix_world @ Vector(v)
        xs.append(wv.x); ys.append(wv.y); zs.append(wv.z)
min_x, max_x = min(xs), max(xs)
min_y, max_y = min(ys), max(ys)
min_z, max_z = min(zs), max(zs)

print(f"[extract] meshes={len(meshes)} bbox X[{min_x:.2f}..{max_x:.2f}] "
      f"Y[{min_y:.2f}..{max_y:.2f}] Z[{min_z:.2f}..{max_z:.2f}]", file=sys.stderr)

# Sample on a grid. We fire rays from above max_z, straight down.
# A "wall" hit means we hit geometry well above the floor.
# Floor height is min_z; ceiling around max_z. A wall extends from min_z
# to max_z. So we ray-cast from max_z + epsilon downward.
W = RES
H = RES
dx = (max_x - min_x) / W
dy = (max_y - min_y) / H

# Walk every mesh face. A face is a "wall" if its normal is roughly horizontal
# (|Nz| < 0.5) AND it spans some portion of the wall height band. For each such
# face, rasterize its XY bounding rectangle into the grid as walls. This finds
# vertical wall faces reliably regardless of where the grid sample points land.
floor_z = min_z
height = max_z - min_z
wall_band_lo = floor_z + height * 0.20
wall_band_hi = floor_z + height * 0.80

grid = [[0] * W for _ in range(H)]
total_wall_faces = 0

for obj in meshes:
    mesh = obj.data
    M = obj.matrix_world
    M3 = M.to_3x3()
    for poly in mesh.polygons:
        # World-space normal
        wn = M3 @ poly.normal
        if abs(wn.z) > 0.5:                                # roughly horizontal face = floor or ceiling
            continue
        # World-space vertex positions
        verts = [M @ mesh.vertices[v].co for v in poly.vertices]
        zs = [v.z for v in verts]
        # Does this face cross the wall band at all?
        if max(zs) < wall_band_lo or min(zs) > wall_band_hi:
            continue
        total_wall_faces += 1
        xs = [v.x for v in verts]
        ys = [v.y for v in verts]
        ix0 = max(0, int((min(xs) - min_x) / dx))
        ix1 = min(W - 1, int((max(xs) - min_x) / dx))
        iy0 = max(0, int((min(ys) - min_y) / dy))
        iy1 = min(H - 1, int((max(ys) - min_y) / dy))
        for j in range(iy0, iy1 + 1):
            for i in range(ix0, ix1 + 1):
                grid[j][i] = 1

print(f"[extract] rasterized {total_wall_faces} wall faces", file=sys.stderr)

# --- write output ---
with open(OUT, "w") as f:
    f.write(f"# Floor plan {W}x{H} from {bpy.data.filepath}\n")
    f.write(f"# bbox X[{min_x:.2f}..{max_x:.2f}] Y[{min_y:.2f}..{max_y:.2f}]\n")
    f.write(f"# 1 = wall, 0 = floor\n\n")
    for row in grid:
        f.write("".join("#" if c else "." for c in row) + "\n")
    f.write("\n# C array for raycast world_map:\n")
    f.write(f"const uint8_t world_map[{H}][{W}] = {{\n")
    for row in grid:
        f.write("    {" + ",".join(str(c) for c in row) + "},\n")
    f.write("};\n")

print(f"[extract] wrote {OUT} ({W}x{H} grid)", file=sys.stderr)
