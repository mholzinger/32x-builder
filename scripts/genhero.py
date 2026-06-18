"""Render the high-res static HERO frame: the closed cardboard box from
the title's start camera, with the SEGA CORE label mapped onto the front
face. This is the crisp "attic box" splash that holds before we swap to
the live low-res 3D box for the open + dive.

Run headless against the box scene:
  blender --background scripts/cardboard_box_32x.blend \
      --python scripts/genhero.py
"""
import bpy
import os
import mathutils

LABEL = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "images", "splash_label.png")
OUT = "/tmp/hero.png"

sc = bpy.context.scene
sc.frame_set(1)                      # frame 1 == box fully closed

box = bpy.data.objects["CardboardBox"]
mesh = box.data

# --- Label material on the front face --------------------------------
img = bpy.data.images.load(os.path.abspath(LABEL))
lbl = bpy.data.materials.new("Label")
lbl.use_nodes = True
nt = lbl.node_tree
bsdf = nt.nodes.get("Principled BSDF")
tex = nt.nodes.new("ShaderNodeTexImage")
tex.image = img
nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = 0.9   # matte paper

mesh.materials.append(lbl)            # cardboard is slot 0, label is slot 1
label_slot = len(mesh.materials) - 1

if not mesh.uv_layers:
    mesh.uv_layers.new(name="UVMap")
uvd = mesh.uv_layers.active.data

# Front face = South wall: the body quad whose 4 verts all sit at y=-1.
# (Flap verts never all share y=-1, so this uniquely picks the wall.)
for poly in mesh.polygons:
    co = [mesh.vertices[i].co for i in poly.vertices]
    if all(abs(v.y + 1.0) < 0.01 for v in co):
        poly.material_index = label_slot
        for li in poly.loop_indices:
            p = mesh.vertices[mesh.loops[li].vertex_index].co
            uvd[li].uv = ((p.x + 1.0) / 2.0, p.z / 2.0)   # x->u, z->v
        break

# The hero renders from the scene's frame-1 camera, which genbox.py now
# sets to the 3/4 product-shot pose — so the splash and the live 3D box's
# first frame are pose-identical (no jump at the swap).

# --- Render high quality (2x for AA; the bake downscales to 320x224) --
sc.render.resolution_x = 640
sc.render.resolution_y = 448
sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print(f">>> hero rendered to {OUT}")
