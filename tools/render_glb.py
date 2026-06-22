#!/usr/bin/env python3
"""Render a GLB/glTF model to flat PNGs from N orthographic views, for baking
into a 2D palette texture. Headless Blender:

    blender --background --python tools/render_glb.py -- \
        models/foo.glb out_prefix [W H]

Writes <out_prefix>_front.png / _back.png / _left.png / _right.png (transparent
background). Use the one that shows the face we want, then bake it.
"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
glb = argv[0]
prefix = argv[1]
W = int(argv[2]) if len(argv) > 2 else 512
H = int(argv[3]) if len(argv) > 3 else 512

# Empty scene, import the model.
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)

# World bbox over all meshes.
mn = Vector((1e9, 1e9, 1e9))
mx = Vector((-1e9, -1e9, -1e9))
for o in bpy.context.scene.objects:
    if o.type == 'MESH':
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            mn = Vector((min(mn.x, w.x), min(mn.y, w.y), min(mn.z, w.z)))
            mx = Vector((max(mx.x, w.x), max(mx.y, w.y), max(mx.z, w.z)))
center = (mn + mx) / 2
size = mx - mn
span = max(size.x, size.y, size.z)
print("BBOX size=%.3f,%.3f,%.3f span=%.3f" % (size.x, size.y, size.z, span))

scene = bpy.context.scene
# Render engine: EEVEE under whatever name this Blender uses.
for eng in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'BLENDER_WORKBENCH'):
    try:
        scene.render.engine = eng
        break
    except Exception:
        continue
scene.render.film_transparent = True
scene.render.resolution_x = W
scene.render.resolution_y = H

# Low ambient + a hard key light so the metal door's frame/handle/hinges cast
# shadow and the slab gets form instead of baking flat. Weak fill keeps the
# shadow side from going pure black.
world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[1].default_value = 0.22

kd = bpy.data.lights.new("key", 'SUN')
kd.energy = 5.0
kd.angle = math.radians(8)          # sharper shadows than the default soft sun
key = bpy.data.objects.new("key", kd)
scene.collection.objects.link(key)
key.rotation_euler = (math.radians(58), 0, math.radians(42))

fd = bpy.data.lights.new("fill", 'SUN')
fd.energy = 1.2
fill = bpy.data.objects.new("fill", fd)
scene.collection.objects.link(fill)
fill.rotation_euler = (math.radians(70), 0, math.radians(-50))

# Ortho camera.
cd = bpy.data.cameras.new("cam")
cd.type = 'ORTHO'
cd.ortho_scale = span * 1.15
cam = bpy.data.objects.new("cam", cd)
scene.collection.objects.link(cam)
scene.camera = cam

views = {
    "front": Vector((0, -1, 0)),
    "back":  Vector((0,  1, 0)),
    "right": Vector((1,  0, 0)),
    "left":  Vector((-1, 0, 0)),
}
for name, d in views.items():
    cam.location = center + d * span * 3
    look = (center - cam.location).normalized()
    cam.rotation_euler = look.to_track_quat('-Z', 'Z').to_euler()
    scene.render.filepath = "%s_%s.png" % (prefix, name)
    bpy.ops.render.render(write_still=True)
    print("wrote %s_%s.png" % (prefix, name))
