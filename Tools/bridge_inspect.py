# Bridge model check (Blender): bounds, triangles and Workbench pictures from several sides.
#   blender -b -P Tools/bridge_inspect.py -- <in.glb> <outprefix> [views: comma list of name:azimuth:elevation]
import bpy, sys, math, mathutils
a = sys.argv[sys.argv.index("--") + 1:]
src, outp = a[0], a[1]
views = a[2] if len(a) > 2 else "front:0:20,back:180:20,side:90:15,top:0:89,under:0:-60,iso:35:28"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
ms = [o for o in bpy.data.objects if o.type == "MESH"]
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in ms)
pts = [o.matrix_world @ mathutils.Vector(c) for o in ms for c in o.bound_box]
mn = mathutils.Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
mx = mathutils.Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
print("BRINSPECT tris", tris, "min", tuple(round(v, 3) for v in mn), "max", tuple(round(v, 3) for v in mx), "meshes", len(ms))
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "TEXTURE"
sc.render.resolution_x, sc.render.resolution_y = 1000, 560
sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.35, 0.45, 0.55)
cen = (mn + mx) / 2; size = (mx - mn).length
for v in views.split(","):
    n, az, el = v.split(":"); az = math.radians(float(az)); el = math.radians(float(el))
    cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); sc.collection.objects.link(cam)
    d = size * 1.05
    # model's long axis may be X or Y; azimuth 0 looks along +Y from -Y
    cam.location = cen + mathutils.Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el))) * d
    cam.rotation_euler = (cen - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam.data.clip_end = 1000
    sc.camera = cam
    sc.render.filepath = outp + "_" + n + ".png"
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)
