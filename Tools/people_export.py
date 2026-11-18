# Blender (background): one townsperson from Meshy (Docs/TownLife/People.md) -> FBX files for Unreal (people_import.py).
# Args after "--": <name> <rigged .glb> <out dir> [<clip key>=<animation .glb>:<action name> ...]
# Writes <out>/SK_<name>.fbx (mesh + 24-bone rig), T_<name>_Base.png (the colour texture), T_<name>_Mask.png (red = the main
# garment, keyed on the blue-green cloth every prompt asked for, so it can be dyed per person), A_<name>_<key>.fbx per clip,
# and prints PEOPLE lines: triangles, height, the garment's mean brightness and each walk clip's speed (planted toe).
import bpy, sys, os, math, colorsys
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
NAME, RIG, OUT = argv[0], argv[1], argv[2]
CLIPS = argv[3:]
os.makedirs(OUT, exist_ok=True)


def fresh(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    for o in list(bpy.data.objects):
        if o.type == 'MESH' and not any(m.type == 'ARMATURE' for m in o.modifiers):
            bpy.data.objects.remove(o)   # (Meshy's stray icosphere)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.name = "Armature"   # (Unreal drops a root node with this name)
    return arm


def export(path, objs, anim):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=anim, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                             apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                             use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True, path_mode='STRIP')


# ---- The body
arm = fresh(RIG)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in meshes:
    print("PEOPLE %s %s custom normals %s, smooth faces %d of %d" % (NAME, o.name, o.data.has_custom_normals, sum(p.use_smooth for p in o.data.polygons), len(o.data.polygons)))
    if not o.data.has_custom_normals: o.data.shade_smooth()
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
zs = [(o.matrix_world @ v.co).z for o in meshes for v in o.data.vertices]
print("PEOPLE %s mesh tris %d height %.3f m" % (NAME, tris, max(zs) - min(zs)))
img = None
for o in meshes:
    for m in o.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and img is None: img = n.image
        if m: m.name = "M_" + NAME
if img is None: raise RuntimeError("PEOPLE no texture")
w, h = img.size
px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
rgb = px[..., :3]
img.filepath_raw = os.path.join(OUT, "T_%s_Base.png" % NAME); img.file_format = 'PNG'; img.save()

# Garment key: hue in the blue-green band, some saturation, not near-black
mx = rgb.max(-1); mn = rgb.min(-1); sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0)
r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
d = np.maximum(mx - mn, 1e-5)
hue = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60.0
key = ((hue > 150) & (hue < 235) & (sat > 0.11) & (mx > 0.04)).astype(np.float32)
# close small holes (stains and dark folds the key missed): grow the key 6 px, then shrink it back
def grow(a, op, n=6):
    for _ in range(n):
        b = a.copy()
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            b = op(b, np.roll(np.roll(a, dy, 0), dx, 1))
        a = b
    return a
key = grow(grow(key, np.minimum, 2), np.maximum, 2)   # (first drop specks: the mail's faint blue)
key = grow(grow(key, np.maximum), np.minimum)
# soften: two box blurs (3 px) so the dye fades over the cloth's edge instead of a hard line
for _ in range(2):
    k = np.zeros_like(key)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            k += np.roll(np.roll(key, dy, 0), dx, 1)
    key = k / 9.0
lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
sel = key > 0.5
glum = float(lum[sel].mean()) if sel.any() else 0.2
lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4) @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
llum = float(lin[sel].mean()) if sel.any() else 0.05
print("PEOPLE %s GarmentLum %.4f (linear, for People.csv)" % (NAME, llum))
print("PEOPLE %s texture %dx%d garment share %.3f mean lum %.4f (gamma-space)" % (NAME, w, h, float(sel.mean()), glum))
mask = np.zeros((h, w, 4), dtype=np.float32); mask[..., 0] = key; mask[..., 3] = 1.0
mi = bpy.data.images.new("Mask", w, h, alpha=False); mi.pixels = mask.ravel()
mi.filepath_raw = os.path.join(OUT, "T_%s_Mask.png" % NAME); mi.file_format = 'PNG'; mi.save()
export(os.path.join(OUT, "SK_%s.fbx" % NAME), [arm] + meshes, False)

# ---- The clips
for c in CLIPS:
    key, rest = c.split("=", 1)
    path, action = rest.rsplit(":", 1)
    arm = fresh(path)
    for o in [o for o in bpy.data.objects if o.type == 'MESH']: bpy.data.objects.remove(o)
    act = bpy.data.actions.get(action) or next((a for a in bpy.data.actions if action in a.name.split("|")), None)
    if act is None: raise RuntimeError("PEOPLE no action %s in %s: %s" % (action, path, [a.name for a in bpy.data.actions]))
    arm.animation_data_create(); arm.animation_data.action = act
    try:
        if act.slots: arm.animation_data.action_slot = act.slots[0]
    except Exception: pass
    for a in list(bpy.data.actions):
        if a != act: bpy.data.actions.remove(a)
    sc = bpy.context.scene
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    sc.frame_start, sc.frame_end = f0, f1
    fps = sc.render.fps / sc.render.fps_base
    toe = {s: [] for s in ("LeftToeBase", "RightToeBase")}
    hips = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        for s in toe: toe[s].append(np.array(arm.matrix_world @ arm.pose.bones[s].head))
        hips.append(np.array(arm.matrix_world @ arm.pose.bones["Hips"].head))
    L, R = np.array(toe["LeftToeBase"]), np.array(toe["RightToeBase"])
    v = [np.linalg.norm(((L if L[i, 2] < R[i, 2] else R)[i] - (L if L[i, 2] < R[i, 2] else R)[i - 1])[:2]) * fps for i in range(1, len(L))]
    hips = np.array(hips)
    print("PEOPLE %s clip %s (%s) %.2f s, planted toe speed %.1f cm/s, hips moved %.1f cm, lowest toe %.1f cm" % (
        NAME, key, action, (f1 - f0) / fps, 100 * float(np.median(v)), 100 * float(np.linalg.norm((hips[-1] - hips[0])[:2])), 100 * float(min(L[:, 2].min(), R[:, 2].min()))))
    act.name = "A_%s_%s" % (NAME, key)
    export(os.path.join(OUT, "A_%s_%s.fbx" % (NAME, key)), [arm], True)
print("PEOPLE %s done" % NAME)
