# Blender (background): the horse's saddle (2026-11-22, agent horse).
#   blender -b --factory-startup -P Tools/saddle_prep.py -- <saddle .glb> <out folder>
# The CC0 library saddle (Art/Animals/Sources.md) is reduced to about 8k triangles, turned so its length runs along +X (the pommel at +X),
# scaled to 52 cm long, its origin where it sits on the horse's back (70% of the way up from the stirrups, the inside of the arch), and written as SM_Saddle.fbx with its colour picture
# T_Saddle_Base.png, for Tools/deer_import.py. (Which end is the pommel: the higher end of the seat; printed as SADDLE lines.)
import bpy, sys, os, bmesh
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = a[0], a[1]
LENGTH = 0.52
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
for o in bpy.data.objects: o.select_set(o in meshes)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
ob = bpy.context.view_layer.objects.active
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
print("SADDLE source: %d triangles, %d materials" % (tris, len(ob.data.materials)))
if tris > 8000:
    m = ob.modifiers.new("dec", "DECIMATE"); m.ratio = 8000.0 / tris
    bpy.ops.object.modifier_apply(modifier="dec")
vs = [v.co.copy() for v in ob.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
size = hi - lo
print("SADDLE size %.3f %.3f %.3f" % tuple(size))
# (this library saddle: its length runs along +Y with the horn at +Y, checked on pictures; turned so the horn points +X)
ob.data.transform(Matrix.Rotation(-1.5708, 4, "Z"))
vs = [v.co.copy() for v in ob.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
s = LENGTH / max(1e-4, hi.x - lo.x)
ob.data.transform(Matrix.Scale(s, 4) @ Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -(lo.z + 0.70 * (hi.z - lo.z))))))
ob.name = "SM_Saddle"
tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
print("SADDLE out: %d triangles, %.3f x %.3f x %.3f m" % (tris, (hi.x - lo.x) * s, (hi.y - lo.y) * s, (hi.z - lo.z) * s))
# the colour picture: the first image the first material uses
img = None
for mat in ob.data.materials:
    if mat and mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == "TEX_IMAGE" and n.image:
                img = n.image; break
    if img: break
os.makedirs(OUT, exist_ok=True)
if img:
    img.filepath_raw = os.path.join(OUT, "T_Saddle_Base.png"); img.file_format = "PNG"; img.save()
    print("SADDLE texture %dx%d" % tuple(img.size))
else: print("SADDLE no texture found")
for o in bpy.data.objects: o.select_set(o == ob)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_Saddle.fbx"), use_selection=True, apply_unit_scale=True, global_scale=1.0, mesh_smooth_type="FACE", path_mode="STRIP", bake_anim=False)
print("SADDLE wrote", os.path.join(OUT, "SM_Saddle.fbx"))
