# Blender (background): the eagle's two still bodies (2026-11-26, agent animals; Docs/AnimalsPlan2.md).
#   blender -b --factory-startup -P Tools/eagle_prep.py -- <EagleFly_clean.glb> <EaglePerch_clean.glb> <out folder>
# Both library models face -Y; each is turned to face +X, its origin put at the body's centre (flying) or under its claws (perched), and
# written as SM_EagleFly.fbx / SM_EaglePerch.fbx with T_EagleFly_Base.png / T_EaglePerch_Base.png for Tools/deer_import.py.
# Flying: 2.0 m wingspan; its grey-white paint is repainted brown (the perched bird's browns), the brightest feathers (head, tail) kept cream.
# Perched: 0.8 m tall; the rock it stands on is cut away (everything below the claws).
import bpy, sys, os
import numpy as np
from mathutils import Vector, Matrix
a = sys.argv[sys.argv.index("--") + 1:]
FLY, PERCH, OUT = a[0], a[1], a[2]
os.makedirs(OUT, exist_ok=True)

def load(p):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=p)
    ms = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in bpy.data.objects: o.select_set(o in ms)
    bpy.context.view_layer.objects.active = ms[0]
    if len(ms) > 1: bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.data.transform(Matrix.Rotation(1.5708, 4, "Z"))
    return ob

def bounds(ob):
    vs = np.array([v.co[:] for v in ob.data.vertices])
    return Vector(vs.min(0)), Vector(vs.max(0))

def image(ob):
    for mat in ob.data.materials:
        if mat and mat.use_nodes:
            for n in mat.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image: return n.image

def write(ob, name, img):
    img.filepath_raw = os.path.join(OUT, "T_%s_Base.png" % name); img.file_format = "PNG"; img.save()
    ob.name = "SM_" + name
    for o in bpy.data.objects: o.select_set(o == ob)
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_%s.fbx" % name), use_selection=True, apply_unit_scale=True, global_scale=1.0,
                             mesh_smooth_type="FACE", path_mode="STRIP", bake_anim=False)
    lo, hi = bounds(ob)
    print("EAGLE %s: %d triangles, %.2f x %.2f x %.2f m, texture %dx%d" % (name, sum(len(p.vertices) - 2 for p in ob.data.polygons),
          *(hi - lo), img.size[0], img.size[1]))

# --- flying
ob = load(FLY)
lo, hi = bounds(ob)
s = 2.0 / (hi.y - lo.y)   # wingspan now runs along Y
ob.data.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-(lo + hi) * 0.5))
img = image(ob)
px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)
lum = px[:, :3] @ np.array([0.3, 0.59, 0.11], dtype=np.float32)
dark = np.array([0.16, 0.09, 0.045]); mid = np.array([0.40, 0.24, 0.11]); cream = np.array([0.93, 0.86, 0.72])
t = np.clip(lum / 0.75, 0, 1)[:, None]
col = dark + (mid - dark) * t
w = np.clip((lum - 0.80) / 0.15, 0, 1)[:, None]
col = col * (1 - w) + cream * w
px[:, :3] = col
img.pixels[:] = px.ravel()
write(ob, "EagleFly", img)

# --- perched: cut the rock (below the claws: the lowest 13% of the height)
ob = load(PERCH)
lo, hi = bounds(ob)
cut = lo.z + 0.13 * (hi.z - lo.z)
import bmesh
bm = bmesh.new(); bm.from_mesh(ob.data)
bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < cut], context="VERTS")
bm.to_mesh(ob.data); bm.free()
lo, hi = bounds(ob)
s = 0.8 / (hi.z - lo.z)
ob.data.transform(Matrix.Scale(s, 4) @ Matrix.Translation(Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))))
write(ob, "EaglePerch", image(ob))
