"""Headless Blender: render a top-down orthographic view of the loaded
.blend file so we can visually compare against the extracted floor plan.

Usage:
    blender --background <.blend> --python topdown_render.py -- --output out.png
"""
import sys
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = "/tmp/topdown.png"
for i, a in enumerate(argv):
    if a == "--output" and i + 1 < len(argv):
        OUT = argv[i + 1]

# Bounding box across all meshes
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
xs, ys, zs = [], [], []
for o in meshes:
    for v in o.bound_box:
        wv = o.matrix_world @ Vector(v)
        xs.append(wv.x); ys.append(wv.y); zs.append(wv.z)
min_x, max_x = min(xs), max(xs)
min_y, max_y = min(ys), max(ys)
min_z, max_z = min(zs), max(zs)
cx = (min_x + max_x) / 2
cy = (min_y + max_y) / 2
span = max(max_x - min_x, max_y - min_y) * 1.05

# Make a top-down orthographic camera looking straight down
cam_data = bpy.data.cameras.new("TopdownCam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = span
cam = bpy.data.objects.new("TopdownCam", cam_data)
cam.location = (cx, cy, max_z + 10)
cam.rotation_euler = (0, 0, 0)         # look straight down (-Z)
bpy.context.scene.collection.objects.link(cam)
bpy.context.scene.camera = cam

# Sun light from above so we get something visible
light_data = bpy.data.lights.new("Sun", "SUN")
light_data.energy = 3
light = bpy.data.objects.new("Sun", light_data)
light.location = (cx, cy, max_z + 20)
bpy.context.scene.collection.objects.link(light)

# Render settings — square, fast
scene = bpy.context.scene
scene.render.resolution_x = 512
scene.render.resolution_y = 512
scene.render.filepath = OUT
scene.render.image_settings.file_format = "PNG"
scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in {
    e.identifier for e in type(scene.render).bl_rna.properties["engine"].enum_items
} else "BLENDER_EEVEE"
bpy.ops.render.render(write_still=True)
print(f"[topdown] saved {OUT}", file=sys.stderr)
