# The stone bridge (2026-11-18): Art/Bridge/StoneBridge_Detailed.glb (Meshy, task ids in Art/Bridge/Sources.md) -> Saved/KnightBuild/Bridge/Bridge.fbx
# + T_Bridge_Base/Normal/MR.png, imported by Tools/bridge_import.py (Tools/BuildBridge.ps1). Run in Blender:
#   blender -b -P Tools/bridge_clean.py -- <in.glb> <out dir> [scale cm per model unit, default 1400] [triangles, default 280000]
# The model is welded, collapse-decimated (UVs and pictures kept), scaled to cm (its long side along X, deck 4.6 m wide at 1400), its origin put
# at the middle of the span at the pier bottoms (z 0), and its stone warmed a little to sit with Oak Hill's buildings. Pictures: base 2048,
# normal 2048, metal/roughness 1024 (the game caps textures at 2048).
import bpy, bmesh, sys, os, math
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
S = float(a[2]) if len(a) > 2 else 1400.0
budget = int(a[3]) if len(a) > 3 else 280000
TINT = (1.05, 1.0, 0.90)   # red, green, blue of the stone picture (sRGB values), as the buildings' warmer stone
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
o = [x for x in bpy.data.objects if x.type == "MESH"][0]
me = o.data
mat = o.material_slots[0].material

def tex_image(sock):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    stack = [pr.inputs[sock]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n.image
            stack.extend(n.inputs)
    return None
base_im, rough_im, nrm_im = tex_image("Base Color"), tex_image("Roughness"), tex_image("Normal")
print("BRCLEAN pictures", base_im and tuple(base_im.size), rough_im and rough_im.name, nrm_im and tuple(nrm_im.size))

# into cm, origin at the span's middle and the lowest point
for v in me.vertices: v.co = o.matrix_world @ v.co
o.matrix_world = o.matrix_world.__class__.Identity(4)
xs = np.array([v.co[:] for v in me.vertices])
lo, hi = xs.min(0), xs.max(0)
g = np.array([(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, lo[2]])
for v in me.vertices: v.co = ((np.array(v.co[:]) - g) * S).tolist()
me.update()

# weld, reduce, smooth
tris = lambda: sum(len(p.vertices) - 2 for p in me.polygons)
before = tris()
bm = bmesh.new(); bm.from_mesh(me)
co = np.array([v.co[:] for v in bm.verts])
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=float((co.max(0) - co.min(0)).max()) * 1.0e-5)
bm.to_mesh(me); bm.free()
if before > budget:
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = budget / before; m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)
if me.has_custom_normals:
    bpy.context.view_layer.objects.active = o
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
bm = bmesh.new(); bm.from_mesh(me)
for f in bm.faces: f.smooth = True
lim = math.radians(50.0)
for e in bm.edges: e.smooth = not (e.is_manifold and e.calc_face_angle(0.0) > lim)
bm.to_mesh(me); bm.free()
print("BRCLEAN triangles", before, "->", tris())
xs = np.array([v.co[:] for v in me.vertices]); print("BRCLEAN bounds cm", xs.min(0).round(1), xs.max(0).round(1))

def px(im):
    _ = len(im.pixels)
    arr = np.empty(len(im.pixels), dtype=np.float32); im.pixels.foreach_get(arr); return arr.reshape(im.size[1], im.size[0], -1)
def save(arr, name, size, tint=None):
    h, w = arr.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=False)
    if arr.shape[2] == 3: arr = np.concatenate([arr, np.ones((h, w, 1), np.float32)], 2)
    im.pixels.foreach_set(np.clip(arr, 0, 1).ravel()); im.update()
    if max(w, h) > size: im.scale(w * size // max(w, h), h * size // max(w, h))
    im.filepath_raw = os.path.join(out, name + ".png"); im.file_format = "PNG"; im.save()
    print("BRCLEAN wrote", name, tuple(im.size))
b = px(base_im)[:, :, :3].copy()
# the picture holds sRGB values (Blender gives them linear): tint in sRGB terms so the numbers read the same as for the buildings
def to_srgb(c): return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - 0.055)
def to_lin(c): return np.where(c <= 0.04045, c / 12.92, np.power((c + 0.055) / 1.055, 2.4))
if base_im.colorspace_settings.name == "sRGB":
    # (Blender's pixel buffer for an sRGB picture holds the values as stored, already 0..1 of the file)
    pass
b = np.clip(b * np.array(TINT, np.float32), 0, 1)
save(b, "T_Bridge_Base", 2048)
n = px(nrm_im)[:, :, :3]; save(n, "T_Bridge_Normal", 2048)
r = px(rough_im)[:, :, :3]; save(r, "T_Bridge_MR", 1024)

# out to FBX (y flipped for the importer's scene conversion, faces turned: as worldprop_clean.py)
o.name = "Bridge"; me.name = "Bridge"
for v in me.vertices: v.co.y = -v.co.y
bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(me); bm.free(); me.update()
bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
fbx = os.path.join(out, "Bridge.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
print("BRCLEAN wrote", fbx)
