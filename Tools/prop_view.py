"""blender -b --factory-startup --python viewglb.py -- in.glb out_prefix : four lit views and the bounds."""
import bpy, sys, math
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a[0])
M = [o for o in bpy.context.scene.objects if o.type == "MESH"]
V = [o.matrix_world @ v.co for o in M for v in o.data.vertices]
lo = Vector((min(c[i] for c in V) for i in range(3))); hi = Vector((max(c[i] for c in V) for i in range(3)))
print("VIEW parts", len(M), "tris", sum(len(p.vertices) - 2 for o in M for p in o.data.polygons), "lo", [round(x, 3) for x in lo], "hi", [round(x, 3) for x in hi])
mid = (lo + hi) / 2; R = (hi - lo).length * 0.5
s = bpy.context.scene
s.render.engine = "BLENDER_WORKBENCH" if False else "BLENDER_EEVEE"
s.render.resolution_x = s.render.resolution_y = 640
s.world = bpy.data.worlds.new("w"); s.world.use_nodes = True
s.world.node_tree.nodes["Background"].inputs[0].default_value = (0.8, 0.8, 0.82, 1); s.world.node_tree.nodes["Background"].inputs[1].default_value = 0.9
l = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); l.data.energy = 3; l.rotation_euler = (0.8, 0.2, 0.6); s.collection.objects.link(l)
cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); s.collection.objects.link(cam); s.camera = cam
cam.data.type = "ORTHO"; cam.data.ortho_scale = R * 2.15
for n, d in (("x", (1, 0, 0)), ("y", (0, -1, 0)), ("z", (0, 0, 1)), ("o", (0.6, -0.6, 0.55))):
    D = Vector(d).normalized(); cam.location = mid + D * R * 4
    cam.rotation_euler = D.to_track_quat("Z", "Y" if abs(D.z) < 0.99 else "X").to_euler()
    s.render.filepath = a[1] + "_" + n + ".png"
    bpy.ops.render.render(write_still=True)
