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
            # (2026-11-18: the Troll's first image node was a metallic/roughness map, so he came out black: prefer the node feeding Base Color)
            for n in m.node_tree.nodes:
                if n.type == 'BSDF_PRINCIPLED' and n.inputs['Base Color'].links:
                    src = n.inputs['Base Color'].links[0].from_node
                    if src.type == 'TEX_IMAGE' and src.image: img = src.image
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
if NAME == "Troll": key = key * 0.0   # (the troll wears no dyed cloth: his own skin and hide colours stay as painted)
if NAME == "Witch": key = ((hue > 270) & (hue < 350) & (sat > 0.15) & (mx > 0.04)).astype(np.float32)   # (2026-11-26: her plum dress is the dyed garment)
if NAME == "Goblin": key = key * 0.0   # (2026-11-26: the goblins keep their own green skin and brown rags)
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

# ---- The troll's skin weights (2026-11-22, owner: "his belt and cloth warps and wobbles a ton when he sprints ... his face gets all
# squished"). Meshy's auto weights put his face 30% on the two shoulder bones (and the jaw also on the spine), and his belt and
# loincloth about 90% on the two thighs and only 4-8% on the hips: every arm pump pulled his face and every stride tore the belt
# and cloth between the legs. Each move below is a share of the weight a vertex already has, so the result stays as smooth as before.
def troll_reweight(o):
    vg = {g.name: g for g in o.vertex_groups}
    idx = {g.index: g.name for g in o.vertex_groups}
    me = o.data
    uv = me.uv_layers.active.data if me.uv_layers.active else None
    col = np.zeros((len(me.vertices), 3), dtype=np.float32); cnt = np.zeros(len(me.vertices), dtype=np.float32)
    if uv is not None:
        for loop in me.loops:
            u, v = uv[loop.index].uv
            x = min(w - 1, max(0, int((u % 1.0) * w))); y = min(h - 1, max(0, int((v % 1.0) * h)))
            col[loop.vertex_index] += rgb[y, x]; cnt[loop.vertex_index] += 1
    col /= np.maximum(cnt, 1)[:, None]
    def ss(a, b, x): t = min(1.0, max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    moved_face = moved_belt = 0
    mw = o.matrix_world
    for v in me.vertices:
        ws = {idx[g.group]: g.weight for g in v.groups}
        if not ws: continue
        p = mw @ v.co
        # the head: as much of the shoulder and spine weight as the vertex is already head (headfront + Head) goes to the Head
        hd = ws.get("Head", 0.0) + ws.get("headfront", 0.0)
        f = ss(0.12, 0.45, hd)
        if f > 0.0:
            take = 0.0
            for b in ("LeftShoulder", "RightShoulder", "Spine", "Spine01", "Spine02", "LeftArm", "RightArm"):
                if b in ws and ws[b] > 0.0:
                    t = ws[b] * f; ws[b] -= t; take += t
            if take > 1e-4: ws["Head"] = ws.get("Head", 0.0) + take; moved_face += 1
        # the belt and the cloth below it: leg weight to the hips, all of it at the belt, less down the loincloth; bare skin of the
        # thighs keeps its legs below the belt (his own green-grey skin: low saturation, not the brown/orange leather)
        c = col[v.index]; mx_ = float(c.max()); mn_ = float(c.min()); sat_ = (mx_ - mn_) / max(mx_, 1e-4)
        cloth = c[0] >= c[1] and c[0] >= c[2] and sat_ > 0.25   # (leather: red the strongest channel)
        z = p.z
        if cloth: f = (0.55 + 0.45 * ss(0.80, 1.00, z)) if z > 0.35 else 0.0
        else: f = ss(0.98, 1.12, z)
        legw = ws.get("LeftUpLeg", 0.0) + ws.get("RightUpLeg", 0.0)
        if f > 0.0 and legw > 1e-4 and z < 1.35:
            take = 0.0
            for b in ("LeftUpLeg", "RightUpLeg"):
                if b in ws: t = ws[b] * f; ws[b] -= t; take += t
            ws["Hips"] = ws.get("Hips", 0.0) + take; moved_belt += 1
        tot = sum(ws.values())
        for b, wt in ws.items():
            if b not in vg: vg[b] = o.vertex_groups.new(name=b); idx[vg[b].index] = b
            if wt <= 1e-5: vg[b].remove([v.index])
            else: vg[b].add([v.index], wt / tot, 'REPLACE')
    print("PEOPLE %s reweighted: %d face vertices to the Head, %d belt/cloth vertices toward the Hips" % (NAME, moved_face, moved_belt))
if NAME == "Troll" and os.environ.get("TROLL_OLD_WEIGHTS") != "1":
    for o in meshes: troll_reweight(o)
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
