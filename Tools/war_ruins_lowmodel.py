# The castle ruins skip townlab_bake.py: their 80k low model came apart into thousands of tiny UV islands (tracery, grass tufts, rubble),
# so its 2048 bake was unusable confetti. Instead the cleaned, flattened 300k model (building_clean.py + building_flatten.py) is reduced
# to BUDGET triangles keeping Meshy's own UVs and pictures, its stone warmed a little, and written as the TownLab model TL_Ruins.
# blender -b -P Tools/war_ruins_lowmodel.py -- <work Buildings> <out TownLabWar> [budget]
import bpy, sys, os, shutil
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
work, out = a[0], a[1]; budget = int(a[2]) if len(a) > 2 else 200000
name, new = "OakHillRuins_300k", "TL_Ruins"
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=os.path.join(work, name + ".fbx"))
o = next(x for x in bpy.data.objects if x.type == "MESH")
bpy.context.view_layer.objects.active = o; o.select_set(True)
me = o.data
tris = sum(len(p.vertices) - 2 for p in me.polygons)
if tris > budget:
    m = o.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = budget / tris; m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)
me = o.data
# vertex colour: no glass anywhere (G 0, B 1), as M_TL_Building reads it
while me.color_attributes: me.color_attributes.remove(me.color_attributes[0])
ca = me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
ca.data.foreach_set("color", np.tile(np.array([0, 0, 1, 1], np.float32), len(ca.data)))
print("RUINS low tris", sum(len(p.vertices) - 2 for p in me.polygons))
def load(p):
    img = bpy.data.images.load(p); w, h = img.size
    px = np.array(img.pixels[:], np.float32).reshape(h, w, 4); return img, px
def save(img, px, path):
    img.pixels[:] = px.reshape(-1); img.filepath_raw = path; img.file_format = "PNG"; img.save()
# colour: the grey (unsaturated) stone a touch warmer, as the bank's was (owner 2026-11-11), the brightness kept
img, px = load(os.path.join(work, "T_%s_Base.png" % name))
rgb = px[..., :3]; mx = rgb.max(-1); mn = rgb.min(-1); sat = (mx - mn) / np.maximum(mx, 1e-4)
grey = np.clip((0.18 - sat) / 0.10, 0, 1)[..., None]
lum0 = rgb @ np.array([0.2126, 0.7152, 0.0722], np.float32)
warm = rgb * np.array([1.05, 1.0, 0.88], np.float32)
warm *= (lum0 / np.maximum(warm @ np.array([0.2126, 0.7152, 0.0722], np.float32), 1e-4))[..., None]
px[..., :3] = np.clip(rgb * (1 - grey) + warm * grey, 0, 1)
save(img, px, os.path.join(out, "T_%s_Base.png" % new))
# MR: M_TL_Building takes R as ambient occlusion: none here (R 1), G roughness and B metal as Meshy made them
img, px = load(os.path.join(work, "T_%s_MR.png" % name))
print("RUINS MR mean", px[..., :3].reshape(-1, 3).mean(0))
px[..., 0] = 1.0
save(img, px, os.path.join(out, "T_%s_MR.png" % new))
shutil.copyfile(os.path.join(work, "T_%s_Normal.png" % name), os.path.join(out, "T_%s_Normal.png" % new))
for f in ("T_%s_Mask.png" % new, "T_%s_Glow.png" % new):
    if os.path.exists(os.path.join(out, f)): os.remove(os.path.join(out, f))
bpy.ops.export_scene.fbx(filepath=os.path.join(out, new + ".fbx"), use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False, colors_type="LINEAR")
print("RUINS wrote", new)
