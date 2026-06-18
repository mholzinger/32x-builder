"""Render the labelled front face FLAT and SQUARE for the box3d texture.

An orthographic camera dead-on to the front wall (y=-1) frames exactly
the 2x2 face, so the output is undistorted and void-free — unlike
cropping the perspective hero render. Lit by the same scene lights, so
the label tones still match the hero across the swap.

  blender --background scripts/cardboard_box_32x.blend \
      --python scripts/gen_label_tex.py
Output: /tmp/label_tex.png  (256x256, fed to tools/bake_label.py)
"""
import bpy
import math
import os

LABEL = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "images", "splash_label.png")
OUT = "/tmp/label_tex.png"

sc = bpy.context.scene
sc.frame_set(1)
box = bpy.data.objects["CardboardBox"]
mesh = box.data

# Label material on the front face (same as genhero).
img = bpy.data.images.load(os.path.abspath(LABEL))
lbl = bpy.data.materials.new("Label")
lbl.use_nodes = True
bsdf = lbl.node_tree.nodes.get("Principled BSDF")
tex = lbl.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = img
lbl.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = 0.9
mesh.materials.append(lbl)
label_slot = len(mesh.materials) - 1

if not mesh.uv_layers:
    mesh.uv_layers.new(name="UVMap")
uvd = mesh.uv_layers.active.data
for poly in mesh.polygons:
    co = [mesh.vertices[i].co for i in poly.vertices]
    if all(abs(v.y + 1.0) < 0.01 for v in co):       # South wall
        poly.material_index = label_slot
        for li in poly.loop_indices:
            p = mesh.vertices[mesh.loops[li].vertex_index].co
            uvd[li].uv = ((p.x + 1.0) / 2.0, p.z / 2.0)
        break

# Orthographic dead-on camera: ortho_scale 2 == the 2-unit-wide face;
# placed in front of the wall (-Y) looking +Y with +Z up.
cam_data = bpy.data.cameras.new("LabelCam")
cam_data.type = 'ORTHO'
cam_data.ortho_scale = 2.0
cam = bpy.data.objects.new("LabelCam", cam_data)
sc.collection.objects.link(cam)
cam.location = (0.0, -3.0, 1.0)
cam.rotation_euler = (math.radians(90), 0.0, 0.0)
sc.camera = cam

sc.render.resolution_x = 256
sc.render.resolution_y = 256
sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print(f">>> label texture rendered to {OUT}")
