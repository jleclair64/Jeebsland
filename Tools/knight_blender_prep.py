# Blender (background) step 1 for the Meshy knight: import Art/Knight/knight.glb, cut it down to a game-sized triangle
# count, stand it on the ground facing +X (Unreal forward), and render front and side preview pictures.
# Run: blender -b -P Tools/knight_blender_prep.py -- <project dir> <out dir>
import bpy, sys, math, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
proj, out = argv[0], argv[1]
target_tris = 30000

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(proj, "Art", "Knight", "knight.glb"))
obj = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# Meshy leaves the surface split along every texture seam: weld it first (the texture coordinates stay per corner), so
# the cut-down mesh has no cracks and real armour pieces can be told apart
import bmesh
bm = bmesh.new(); bm.from_mesh(obj.data)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.00002)
bm.to_mesh(obj.data); bm.free()

tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
mod = obj.modifiers.new("Dec", 'DECIMATE')
mod.ratio = min(1.0, target_tris / max(tris, 1))
bpy.ops.object.modifier_apply(modifier="Dec")
print("TRIS", tris, "->", sum(len(p.vertices) - 2 for p in obj.data.polygons))

# Bounds after import (Blender: Z up). Report which way it faces: glTF front (+Z) becomes Blender -Y.
vs = [obj.matrix_world @ v.co for v in obj.data.vertices]
mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
print("BOUNDS", tuple(mn), tuple(mx))

# Height profile: for each 5% slice of height, the width (X) and depth (Y) spans, to find neck, waist, knees
h = mx.z - mn.z
for i in range(20):
    z0, z1 = mn.z + h * i / 20, mn.z + h * (i + 1) / 20
    sl = [v for v in vs if z0 <= v.z < z1]
    if sl:
        print("SLICE %2d z %.3f-%.3f  x %.3f..%.3f  y %.3f..%.3f" % (i, z0, z1, min(v.x for v in sl), max(v.x for v in sl), min(v.y for v in sl), max(v.y for v in sl)))

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "knight_dec.blend"))

# Preview renders: orthographic, flat textured (Workbench), front (looking along +Y) and side (looking along -X)
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'TEXTURE'
scene.render.resolution_x = 700
scene.render.resolution_y = 1000
scene.world = bpy.data.worlds.new("W") if not scene.world else scene.world
cam_data = bpy.data.cameras.new("Cam")
cam_data.type = 'ORTHO'
cam_data.ortho_scale = h * 1.1
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
c = (mn + mx) / 2
for name, loc, rot in [("front", (c.x, mn.y - 5, c.z), (math.radians(90), 0, 0)),
                       ("back", (c.x, mx.y + 5, c.z), (math.radians(90), 0, math.radians(180))),
                       ("side", (mx.x + 5, c.y, c.z), (math.radians(90), 0, math.radians(90)))]:
    cam.location = loc
    cam.rotation_euler = rot
    scene.render.filepath = os.path.join(out, "knight_%s.png" % name)
    bpy.ops.render.render(write_still=True)
