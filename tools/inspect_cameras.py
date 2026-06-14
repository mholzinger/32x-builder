"""List all cameras in the loaded .blend file with their world-space
positions and rotations. Useful for finding where a reference render
was shot from inside the model.
"""
import sys
import math
import bpy
from mathutils import Vector

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
xs, ys, zs = [], [], []
for o in meshes:
    for v in o.bound_box:
        wv = o.matrix_world @ Vector(v)
        xs.append(wv.x); ys.append(wv.y); zs.append(wv.z)
min_x, max_x = min(xs), max(xs)
min_y, max_y = min(ys), max(ys)
min_z, max_z = min(zs), max(zs)

print(f"[inspect] bbox X[{min_x:.2f}..{max_x:.2f}] "
      f"Y[{min_y:.2f}..{max_y:.2f}] "
      f"Z[{min_z:.2f}..{max_z:.2f}]", file=sys.stderr)

cams = [o for o in bpy.context.scene.objects if o.type == "CAMERA"]
print(f"[inspect] {len(cams)} cameras in scene", file=sys.stderr)
for c in cams:
    loc = c.location
    rot = c.rotation_euler
    print(f"  {c.name}:", file=sys.stderr)
    print(f"    pos = ({loc.x:.2f}, {loc.y:.2f}, {loc.z:.2f})", file=sys.stderr)
    print(f"    rot_deg = ({math.degrees(rot.x):.1f}, {math.degrees(rot.y):.1f}, {math.degrees(rot.z):.1f})", file=sys.stderr)
    # Forward vector — Blender camera looks down -Z in local space
    fwd = c.matrix_world.to_3x3() @ Vector((0, 0, -1))
    print(f"    forward = ({fwd.x:.2f}, {fwd.y:.2f}, {fwd.z:.2f})", file=sys.stderr)

# Also list cameras stored in .blend data (not necessarily in scene)
print(f"[inspect] cameras in .blend data: {len(bpy.data.cameras)}", file=sys.stderr)
for cd in bpy.data.cameras:
    print(f"  data: {cd.name}", file=sys.stderr)
