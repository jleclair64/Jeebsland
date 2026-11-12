# Turns Meshy's buildings in Art/Buildings into what the game shows (2026-11-06). Run by Tools\BuildBuildings.ps1 in Blender.
# Each model is sized by its long side (along the model's X), centred on its footprint, and sunk into the ground; its shape and normals are otherwise left alone.
# Results: Saved/KnightBuild/Buildings/<Name>.fbx, T_<Name>_Base/Normal/MR.png, and a BUILD line with the walls' rectangle (for the collision box in the code).
# A model with lit windows (an emissive picture, from Tools/building_glow.py) also gives T_<Name>_Glow.png; T_NoGlow.png (black) is for the rest.
#
# BUILDINGS: file, length: the long side in cm, sink: how far the lowest point goes into the ground in cm (Meshy's porch posts reach below the stone base).
import bpy, os, sys
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

BUILDINGS = {
    "OakHillHouse_Standard": dict(file="OakHillHouse_Standard.glb", length=1200.0, sink=20.0),
    "OakHillHouse_Detailed": dict(file="OakHillHouse_Detailed.glb", length=1200.0, sink=20.0),
    "OakHillHouse_300k":     dict(file="OakHillHouse_300k.glb",     length=1200.0, sink=20.0),
    # Oak Hill's town (2026-11-11): lengths chosen so each storey matches the house's (shops about 12.5-14 m tall to the chimney,
    # the town hall's clock 18 m, the church's spire 24 m). The game reads the same footprints from JeebOakHill::Models() (OakHill.h).
    "OakHillCornerHouse_300k": dict(file="OakHillCornerHouse_300k.glb", length=900.0,  sink=20.0),   # (drawn 1.2x in the game, like the house)
    "OakHillSmith_300k":       dict(file="OakHillSmith_300k.glb",       length=1500.0, sink=20.0),
    "OakHillBank_300k":        dict(file="OakHillBank_300k.glb",        length=1340.0, sink=20.0),
    "OakHillStore_300k":       dict(file="OakHillStore_300k.glb",       length=1250.0, sink=20.0),
    "OakHillPotionShop_300k":  dict(file="OakHillPotionShop_300k.glb",  length=1200.0, sink=20.0),
    "OakHillTownHall_300k":    dict(file="OakHillTownHall_300k.glb",    length=2100.0, sink=20.0),
    "OakHillChurch_300k":      dict(file="OakHillChurch_300k.glb",      length=1330.0, sink=25.0),
    "OakHillWell_150k":        dict(file="OakHillWell_150k.glb",        length=340.0,  sink=10.0),
}
names = a[2:] or list(BUILDINGS)

def save_pic(img, path):
    im = img.copy()                 # (a packed picture only saves through a copy)
    im.scale(img.size[0], img.size[1])
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()

GLOW_KNEE = 0.15   # the GLB stores the glow as JPEG, whose blocks smear a faint amber round each window, sometimes onto a neighbouring
                   # part of the picture (a roof): anything dimmer than this goes black and the rest is stretched back to full

def save_glow(img, path):
    W, H = img.size
    p = np.empty(W * H * 4, dtype=np.float32); img.pixels.foreach_get(p); p = p.reshape(-1, 4)
    m = p[:, :3].max(1)
    k = np.clip((m - GLOW_KNEE) / (1.0 - GLOW_KNEE), 0, 1) / np.maximum(m, 1e-4)
    p[:, :3] *= k[:, None]; p[:, 3] = 1.0
    im = bpy.data.images.new(os.path.basename(path), W, H, alpha=False)
    im.pixels.foreach_set(p.ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    print("BUILD glow", os.path.basename(path), "lit texels", int((m > GLOW_KNEE).sum()))

def save_black(path):
    # T_NoGlow: what M_Building's GlowTex shows on a building without lit windows
    im = bpy.data.images.new("T_NoGlow", 8, 8, alpha=False)
    p = np.zeros(8 * 8 * 4, np.float32); p[3::4] = 1.0
    im.pixels.foreach_set(p); im.filepath_raw = path; im.file_format = "PNG"; im.save()

save_black(os.path.join(out, "T_NoGlow.png"))

def tex_of(sock):
    stack = [sock]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n.image
            stack.extend(i for i in n.inputs)
    return None

for name in names:
    B = BUILDINGS[name]
    f = os.path.join(src, B["file"])
    if not os.path.exists(f):
        print("BUILD missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    co = np.empty(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(axis=0), co.max(axis=0)
    S = B["length"] / (hi[0] - lo[0])
    co[:, 0] = (co[:, 0] - (lo[0] + hi[0]) * 0.5) * S
    co[:, 1] = (co[:, 1] - (lo[1] + hi[1]) * 0.5) * S
    co[:, 2] = (co[:, 2] - lo[2]) * S - B["sink"]
    me.vertices.foreach_set("co", co.reshape(-1).astype(np.float32))
    me.update()
    # the walls' rectangle: the stone base, without the porch posts or the odd stray point
    band = co[(co[:, 2] > 15.0) & (co[:, 2] < 45.0)]
    wx = np.percentile(band[:, 0], [1, 99]); wy = np.percentile(band[:, 1], [1, 99])
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    print("BUILD", name, "tris", tris, "size", [round(float(x), 1) for x in co.max(axis=0) - co.min(axis=0)],
          "lo", [round(float(x), 1) for x in co.min(axis=0)], "hi", [round(float(x), 1) for x in co.max(axis=0)],
          "walls x", [round(float(x), 1) for x in wx], "y", [round(float(x), 1) for x in wy])
    # the pictures
    pr = next(n for n in me.materials[0].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    base = tex_of(pr.inputs["Base Color"]); normal = tex_of(pr.inputs["Normal"]); mr = tex_of(pr.inputs["Metallic"]) or tex_of(pr.inputs["Roughness"])
    save_pic(base, os.path.join(out, "T_%s_Base.png" % name))
    if normal: save_pic(normal, os.path.join(out, "T_%s_Normal.png" % name))
    if mr: save_pic(mr, os.path.join(out, "T_%s_MR.png" % name))
    glow = tex_of(pr.inputs["Emission Color"])     # the lit windows (Tools/building_glow.py), if the model has them
    if glow: save_glow(glow, os.path.join(out, "T_%s_Glow.png" % name))
    fbx = os.path.join(out, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False)
    print("BUILD wrote", fbx, round(os.path.getsize(fbx) / 1e6, 1), "MB")
