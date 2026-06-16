import bpy
import math
import os
import mathutils

# Clear existing objects to start fresh
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)

# 1. Define the Unified Mesh Coordinates (The Base Box + 4 Attached Flaps)
# Vertices 0-7: The standard main box container
# Vertices 8-15: Outer flap edges attached to the top rim seams
verts = [
    # Main Box Base (Bottom Rim, Z=0)
    (-1.0, -1.0, 0.0), ( 1.0, -1.0, 0.0), ( 1.0,  1.0, 0.0), (-1.0,  1.0, 0.0),
    # Main Box Top Rim (Z=2)
    (-1.0, -1.0, 2.0), ( 1.0, -1.0, 2.0), ( 1.0,  1.0, 2.0), (-1.0,  1.0, 2.0),
    # Outer Flap Projection Coordinates (Wide Open State)
    (-1.0,  2.0, 2.0), ( 1.0,  2.0, 2.0),  # North Flap Outer (Verts 8, 9)
    ( 1.0, -2.0, 2.0), (-1.0, -2.0, 2.0),  # South Flap Outer (Verts 10, 11)
    ( 2.0, -1.0, 2.0), ( 2.0,  1.0, 2.0),  # East Flap Outer  (Verts 12, 13)
    (-2.0,  1.0, 2.0), (-2.0, -1.0, 2.0)   # West Flap Outer  (Verts 14, 15)
]

# Structural Face Matrix binding the indices together safely
faces = [
    (0, 3, 2, 1),  # Box Bottom
    (0, 1, 5, 4),  # Box South Wall
    (1, 2, 6, 5),  # Box East Wall
    (2, 3, 7, 6),  # Box North Wall
    (3, 0, 4, 7),  # Box West Wall
    (7, 6, 9, 8),  # North Flap (Connected to top rim 7, 6)
    (4, 5, 10, 11),# South Flap (Connected to top rim 4, 5)
    (5, 6, 13, 12),# East Flap  (Connected to top rim 5, 6)
    (7, 4, 15, 14) # West Flap  (Connected to west rim 7, 4)
]

# Create the Mesh Container
mesh_data = bpy.data.meshes.new("CardboardBoxMesh")
mesh_data.from_pydata(verts, [], faces)
mesh_data.update()

# Instantiate Object into Scene
box = bpy.data.objects.new("CardboardBox", mesh_data)
bpy.context.collection.objects.link(box)
bpy.context.view_layer.objects.active = box

# 2. Set up Shape Keys for Morph Animation
shape_basis = box.shape_key_add(name="Basis", from_mix=False)
shape_closed = box.shape_key_add(name="Flaps_Closed", from_mix=False)

# Morph target translations: Flatten outer flap vertices to the box center line to fold it shut
# North & South slide to Y=0; East & West slide to X=0 over the top opening
for v in shape_closed.data:
    # North Flap outer vertices (indices 8 and 9)
    if v.co.y > 1.1: 
        v.co.y = 0.0
    # South Flap outer vertices (indices 10 and 11)
    elif v.co.y < -1.1: 
        v.co.y = 0.0
    # East Flap outer vertices (indices 12 and 13)
    elif v.co.x > 1.1: 
        v.co.x = 0.0
    # West Flap outer vertices (indices 14 and 15)
    elif v.co.x < -1.1: 
        v.co.x = 0.0

# 3. Liminal Space Environment Setup (Pure Black)
bpy.context.scene.world.use_nodes = True
bg_node = bpy.context.scene.world.node_tree.nodes.get("Background")
if bg_node:
    bg_node.inputs[0].default_value = (0, 0, 0, 1)

# 4. Camera Setup — frame 1 starts at the HERO splash pose (3/4 front
# product shot) so the high-res splash and the live 3D box are pose-
# identical at the swap. Same look-at as scripts/genhero.py.
hero_loc = mathutils.Vector((0.0, -5.5, 1.0))
hero_tgt = mathutils.Vector((0.0, 0.0, 1.0))
hero_rot = (hero_tgt - hero_loc).to_track_quat('-Z', 'Y').to_euler()
bpy.ops.object.camera_add(location=hero_loc, rotation=hero_rot)
cam = bpy.context.active_object
bpy.context.scene.camera = cam

# Frame timelines
bpy.context.scene.frame_start = 1
bpy.context.scene.frame_end = 120

# Keyframe Camera - Start position (Looking at closed box)
cam.keyframe_insert(data_path="location", frame=1)
cam.keyframe_insert(data_path="rotation_euler", frame=1)

# Animate the Shape Key morph (Frames 15 to 45)
shape_closed.value = 1.0  # Starts folded closed
shape_closed.keyframe_insert(data_path="value", frame=15)

shape_closed.value = 0.0  # Morphs open
shape_closed.keyframe_insert(data_path="value", frame=45)

# Keyframe Camera - Move directly over and look inside
cam.location = (0, 0, 5)
cam.rotation_euler = (0, 0, 0)
cam.keyframe_insert(data_path="location", frame=65)
cam.keyframe_insert(data_path="rotation_euler", frame=65)

# Dive directly into the darkness inside the bottom box structure
cam.location = (0, 0, 0.5)
cam.keyframe_insert(data_path="location", frame=90)

# 4b. Cardboard material (tan, rough) on the welded box+flap mesh
mat = bpy.data.materials.get("Material") or bpy.data.materials.new("Material")
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get("Principled BSDF")
bsdf.inputs["Base Color"].default_value = (0.46, 0.30, 0.16, 1.0)  # cardboard tan
bsdf.inputs["Roughness"].default_value = 0.92
if box.data.materials:
    box.data.materials[0] = mat
else:
    box.data.materials.append(mat)

# 4c. Lighting — warm key + cool fill over the pure-black liminal void
key_data = bpy.data.lights.new("KeyLight", 'AREA')
key_data.energy = 900
key_data.size = 4
key_obj = bpy.data.objects.new("KeyLight", key_data)
bpy.context.scene.collection.objects.link(key_obj)
key_obj.location = (-3, -4, 6)
key_obj.rotation_euler = (math.radians(40), math.radians(-15), 0)

fill_data = bpy.data.lights.new("FillLight", 'AREA')
fill_data.energy = 250
fill_data.size = 6
fill_data.color = (0.7, 0.8, 1.0)
fill_obj = bpy.data.objects.new("FillLight", fill_data)
bpy.context.scene.collection.objects.link(fill_obj)
fill_obj.location = (4, -3, 3)
fill_obj.rotation_euler = (math.radians(60), math.radians(20), 0)

# 5. Retro Hardware Core Render Profiles (320x240 4:3 NTSC standard)
bpy.context.scene.render.resolution_x = 320
bpy.context.scene.render.resolution_y = 240
bpy.context.scene.render.pixel_aspect_x = 1
bpy.context.scene.render.pixel_aspect_y = 1

# 6. Save out the clean blend pipeline asset
blend_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cardboard_box_32x.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"\n>>> Success! Fully welded 32X Box Model Generated: {blend_path}\n")
