# The chain mail that is always under the armour (owner 2026-11-06: plates over nothing look hollow, seen up the sleeve or through the visor).
# His whole mail body from the rig, without hands and feet, pulled in a little. It can't come through the plates when he moves, for two reasons: every point of it takes its bone weights
# from the nearest point of the armour, so it moves with the plate over it, and its material moves it a few centimetres away from the camera as it draws (M_SuitUnder).
# Writes <output folder>\SK_NewSuitUnder.fbx. Run through Blender: blender -b -P knight_under_export.py -- <rigged knight.glb> <SK_NewSuit.fbx> <output folder>
import bpy, bmesh, sys, os
from mathutils import kdtree

INSET = 0.015        # (m) pulled in along its normals
TARGET_FACES = 6000  # his 10k body is more than something mostly hidden needs

a = sys.argv[sys.argv.index("--") + 1:]
rig, suit_fbx, out = a[0], a[1], a[2]
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig)
for x in list(bpy.data.objects):
    if x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
body = next(x for x in bpy.data.objects if x.type == "MESH")
arm.name = "Armature"
arm.data.pose_position = "REST"
for act in list(bpy.data.actions): bpy.data.actions.remove(act)
bpy.context.view_layer.update()
wb = body.matrix_world
local_inset = INSET / wb.to_scale()[0]

# ---- hands and feet go: the gauntlets and boots are closed, and fingers are too thin to pull in
names = {g.index: g.name.split(":")[-1] for g in body.vertex_groups}
me = body.data
ends = [sum(g.weight for g in v.groups if any(k in names[g.group] for k in ("Hand", "Foot", "Toe"))) > 0.5 for v in me.vertices]
bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(ends[v.index] for v in f.verts) * 2 > len(f.verts)], context="FACES")
for v in [v for v in bm.verts if not v.link_faces]: bm.verts.remove(v)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=2e-5 / wb.to_scale()[0])
bm.to_mesh(me); bm.free(); me.update()
print("UNDER after hands and feet: %d faces" % len(me.polygons))

# ---- fewer faces
bpy.context.view_layer.objects.active = body
body.select_set(True)
if len(me.polygons) > TARGET_FACES:
    d = body.modifiers.new("less", "DECIMATE"); d.ratio = TARGET_FACES / len(me.polygons)
    bpy.ops.object.modifier_apply(modifier=d.name)
me = body.data
print("UNDER decimated: %d faces" % len(me.polygons))

# ---- pulled in
if "custom_normal" in me.attributes: me.attributes.remove(me.attributes["custom_normal"])
me.update()
moved = [v.co - v.normal * local_inset for v in me.vertices]
for v, c in zip(me.vertices, moved): v.co = c
me.update()

# ---- each point moves with the plate over it
keep = set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=suit_fbx, automatic_bone_orientation=False)
new = [x for x in bpy.data.objects if x not in keep]
bpy.context.view_layer.update()
pts, pt_w = [], []   # (the suit comes back as one mesh per piece)
for suit in (x for x in new if x.type == "MESH"):
    ws = suit.matrix_world
    sn = {g.index: g.name.split(":")[-1] for g in suit.vertex_groups}
    for v in suit.data.vertices:
        pts.append(ws @ v.co)
        pt_w.append([(sn[g.group], g.weight) for g in v.groups if g.weight > 0.001])
pts_b = [wb @ v.co for v in me.vertices]
def box(p): return tuple(round(min(c[i] for c in p), 3) for i in range(3)), tuple(round(max(c[i] for c in p), 3) for i in range(3))
print("UNDER suit box", box(pts), "body box", box(pts_b))
tree = kdtree.KDTree(len(pts))
for i, p in enumerate(pts): tree.insert(p, i)
tree.balance()
rig_group = {g.name.split(":")[-1]: g for g in body.vertex_groups}
dists = []
weights = []
for p in pts_b:
    _, i, d = tree.find(p)
    dists.append(d)
    weights.append(pt_w[i])
dists.sort()
print("UNDER nearest plate: median %.3f m, worst %.3f m" % (dists[len(dists) // 2], dists[-1]))
if dists[len(dists) // 2] > 0.1: raise RuntimeError("UNDER the suit and the body don't line up")
for g in rig_group.values(): g.remove(list(range(len(me.vertices))))
for i, w in enumerate(weights):
    for n, x in w:
        if n in rig_group: rig_group[n].add([i], x, "REPLACE")
for x in new: bpy.data.objects.remove(x)

# ---- one slot (M_SuitUnder, set on import)
base = me.materials[0] if len(me.materials) else bpy.data.materials.new("Suit_UnderBody")
me.materials.clear()
m = base.copy(); m.name = "Suit_UnderBody"
me.materials.append(m)
for p in me.polygons: p.material_index = 0
me.shade_smooth(); me.update()

bpy.ops.object.select_all(action="DESELECT")
for x in (arm, body): x.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(out, "SK_NewSuitUnder.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False, bake_anim=False,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("UNDER wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
