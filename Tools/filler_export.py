# Oak Hill's cleaned filler props (Art/Filler/Clean/*.glb) -> Saved/KnightBuild/Filler/<Piece>.fbx + T_<Piece>_Base/Normal/MR.png, for Tools/filler_import.py (2026-11-13).
# Run by Tools\BuildFillerLineup.ps1 in Blender:  blender -b -P Tools/filler_export.py -- <out dir> [Piece ...]
# The cleaned GLBs are in real metres x 1.2 (WORLD in filler_clean.py), origin at the bottom centre; here they become centimetres (x100), so they draw at scale 1.
# Material slots keep their order and names: slot 0 is the body, "Glow" (lantern glass, flames) and "Cloth" (two-sided cloth) follow when a piece has them.
# One line per piece:  FILLERX <Piece> tris=<n> size=<x>x<y>x<z>cm slots=<names> twosided=<0|1>
import bpy, os, sys
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
out = a[0]
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJ, "Art", "Filler", "Clean")
names = a[1:] or sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".glb"))
os.makedirs(out, exist_ok=True)

def tex_of(sock):
    stack = [sock]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n.image
            stack.extend(i for i in n.inputs)
    return None

def save_pic(img, path):
    im = img.copy()
    im.scale(img.size[0], img.size[1])
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()

for name in names:
    f = os.path.join(SRC, name + ".glb")
    if not os.path.exists(f): print("FILLERX missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1: bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    o.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    co = np.empty(len(me.vertices) * 3, dtype=np.float64); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3) * 100.0
    me.vertices.foreach_set("co", co.reshape(-1).astype(np.float32)); me.update()
    d = co.max(0) - co.min(0)
    mats = [m for m in me.materials]
    body = mats[0]
    pr = next(n for n in body.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    base = tex_of(pr.inputs["Base Color"]); normal = tex_of(pr.inputs["Normal"]); mr = tex_of(pr.inputs["Metallic"]) or tex_of(pr.inputs["Roughness"])
    save_pic(base, os.path.join(out, "T_%s_Base.png" % name))
    if normal: save_pic(normal, os.path.join(out, "T_%s_Normal.png" % name))
    if mr: save_pic(mr, os.path.join(out, "T_%s_MR.png" % name))
    two = int(not body.use_backface_culling) if hasattr(body, "use_backface_culling") else 0
    for m in mats:
        if m.name.startswith("Glow"): m.name = "Glow"
        elif m.name.startswith("Cloth"): m.name = "Cloth"
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    if two: open(os.path.join(out, name + ".two"), "w").close()
    print("FILLERX", name, "tris=%d" % tris, "size=%.0fx%.0fx%.0fcm" % tuple(d), "slots=" + ",".join(m.name for m in me.materials), "twosided=%d" % two)
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True)
    bpy.ops.export_scene.fbx(filepath=os.path.join(out, name + ".fbx"), use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01,
                             apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False,
                             path_mode="AUTO", embed_textures=False)
