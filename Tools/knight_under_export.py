# The chain mail that is always under the armour (owner 2026-11-06: plates over nothing look hollow, seen up the sleeve or through the visor).
# His whole mail body from the rig, without hands and feet, pulled in a little. It can't come through the plates when he moves, for two reasons: every point of it takes its bone weights
# from the nearest point of the armour, so it moves with the plate over it, and its material moves it a few centimetres away from the camera as it draws (M_SuitUnder).
# Since 2026-11-17 its legs are also pulled inside the leg plates (they hug the thigh closer than his body), its hips tucked inside the dark cloth under the skirt,
# and its leg points move with the leg armour, not a tasset (owner: mail through the front of the thighs in the bow draw).
# Writes <output folder>\SK_NewSuitUnder.fbx. Run through Blender: blender -b -P knight_under_export.py -- <rigged knight.glb> <SK_NewSuit.fbx> <output folder>
import bpy, bmesh, sys, os
from mathutils import kdtree, Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import intersect_point_line

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
pts, pt_w, pt_leg = [], [], []   # (the suit comes back as one mesh per piece)
for suit in (x for x in new if x.type == "MESH"):
    ws = suit.matrix_world
    sn = {g.index: g.name.split(":")[-1] for g in suit.vertex_groups}
    legs = any(m and "Legs" in m.name for m in suit.data.materials)
    for v in suit.data.vertices:
        pts.append(ws @ v.co)
        pt_w.append([(sn[g.group], g.weight) for g in v.groups if g.weight > 0.001])
        pt_leg.append(legs)
pts_b = [wb @ v.co for v in me.vertices]
# The thighs (owner 2026-11-17: mail through the front of the thighs in the bow draw): the nearest plate to the front of a thigh is often a tasset of the
# skirt, which hangs from the hips, so that mail stayed with the hips while the thigh plate under the skirt turned away with the leg and the mail came out over it.
# A point of his body that belongs mostly to a leg takes its weights from the nearest point of the leg armour (cuisse, knee, greave) instead: it lies under that plate.
LEG_BONES = ("LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg")
body_names = {g.index: g.name.split(":")[-1] for g in body.vertex_groups}
leg_share = [sum(g.weight for g in v.groups if body_names[g.group] in LEG_BONES) / max(sum(g.weight for g in v.groups), 1e-6) for v in me.vertices]
# The leg plates hug the thighs closer than his mail body does (Meshy's body is fuller than the plate in places, round the inner and back of the thigh),
# so 1.5 cm in was still outside the plate there and the mail showed through it, worst when the draw's stance turns the thigh to the camera (test: with
# the mail pushed away, every red patch of -ShotUnderRed was solid plate, not a gap). Each leg point is moved in toward the bone until it lies at least
# LEG_MARGIN inside the leg plate along the line out from the bone. Where that line meets no plate (a real gap) it stays where it is.
LEG_MARGIN = 0.012   # (m)
LEG_SHARE_IN = 0.3   # (the upper inner thigh and the groin belong partly to the hips)
leg_objs = [x for x in new if x.type == "MESH" and any(m and "Legs" in m.name for m in x.data.materials)]
lv, lf = [], []
for o in leg_objs:
    b0 = len(lv); lv += [o.matrix_world @ v.co for v in o.data.vertices]; lf += [tuple(b0 + i for i in p.vertices) for p in o.data.polygons]
legs_bvh = BVHTree.FromPolygons(lv, lf)
bone_at = {b.name.split(":")[-1]: arm.matrix_world @ b.head_local for b in arm.data.bones}
segs = {"LeftUpLeg": ("LeftUpLeg", "LeftLeg"), "LeftLeg": ("LeftLeg", "LeftFoot"), "RightUpLeg": ("RightUpLeg", "RightLeg"), "RightLeg": ("RightLeg", "RightFoot")}
wbi = wb.inverted()
pulled, pulled_cm = 0, 0.0
for i, v in enumerate(me.vertices):
    if leg_share[i] <= LEG_SHARE_IN: continue
    g = max((g for g in v.groups if body_names[g.group] in LEG_BONES), key=lambda g: g.weight, default=None)
    if g is None: continue
    s0, s1 = (bone_at[n] for n in segs[body_names[g.group]])
    p = pts_b[i]
    c, t = intersect_point_line(p, s0, s1)
    if t < 0: c = s0
    elif t > 1: c = s1
    out_dir = p - c; r = out_dir.length
    if r < 1e-4: continue
    out_dir /= r
    hit, _, _, h = legs_bvh.ray_cast(c, out_dir, r + 0.08)
    if hit is None or r <= h - LEG_MARGIN: continue
    r2 = max(h - LEG_MARGIN, 0.3 * r)
    pulled += 1; pulled_cm += (r - r2) * 100
    pts_b[i] = c + out_dir * r2
    v.co = wbi @ pts_b[i]
me.update()
print("UNDER leg points pulled inside the leg plate: %d, by %.1f cm on average" % (pulled, pulled_cm / max(pulled, 1)))
# The waist between the skirt's tassets and the top of the thigh plates is a real gap, filled by the suit's dark cloth under the skirt (knight_suit_mail.py's pelvis).
# The owner saw bright mail there in the draw; the mail round the hips goes just inside that cloth, so the gap shows the dark cloth and the mail is still behind it.
PELVIS_MARGIN = 0.008   # (m)
pel_objs = [x for x in new if x.type == "MESH" and any(m and m.name.startswith("Suit_Under") for m in x.data.materials)]
pv, pf = [], []
for o in pel_objs:
    mats = o.data.materials
    b0 = len(pv); pv += [o.matrix_world @ v.co for v in o.data.vertices]
    pf += [tuple(b0 + i for i in p.vertices) for p in o.data.polygons if mats[p.material_index] and mats[p.material_index].name.startswith("Suit_Under")]
tucked = 0
if pf:
    pel_bvh = BVHTree.FromPolygons(pv, pf)
    pz = [pv[i].z for f in pf for i in f]; z_lo, z_hi = min(pz), max(pz)
    hip = bone_at["Hips"]
    for i, v in enumerate(me.vertices):
        p = pts_b[i]
        if leg_share[i] > LEG_SHARE_IN or not (z_lo <= p.z <= z_hi): continue
        c = Vector((hip.x, hip.y, p.z)); out_dir = p - c; r = out_dir.length
        if r < 1e-4: continue
        out_dir /= r
        hit, _, _, h = pel_bvh.ray_cast(c, out_dir, r + 0.05)
        if hit is None or r <= h - PELVIS_MARGIN: continue
        pts_b[i] = c + out_dir * max(h - PELVIS_MARGIN, 0.3 * r); v.co = wbi @ pts_b[i]; tucked += 1
    me.update()
print("UNDER hip points tucked inside the dark cloth under the skirt: %d (cloth z %.2f-%.2f)" % (tucked, z_lo if pf else 0, z_hi if pf else 0))
def box(p): return tuple(round(min(c[i] for c in p), 3) for i in range(3)), tuple(round(max(c[i] for c in p), 3) for i in range(3))
print("UNDER suit box", box(pts), "body box", box(pts_b))
tree = kdtree.KDTree(len(pts))
for i, p in enumerate(pts): tree.insert(p, i)
tree.balance()
rig_group = {g.name.split(":")[-1]: g for g in body.vertex_groups}
leg_idx = [i for i, x in enumerate(pt_leg) if x]
leg_tree = kdtree.KDTree(len(leg_idx))
for j, i in enumerate(leg_idx): leg_tree.insert(pts[i], j)
leg_tree.balance()
LEG_MIN = 0.5   # (his body's own leg share above which a point follows the leg armour)
dists = []
weights = []
moved_to_leg = 0
for p, ls in zip(pts_b, leg_share):
    _, i, d = tree.find(p)
    if ls > LEG_MIN and leg_idx:
        _, j, d2 = leg_tree.find(p)
        if not pt_leg[i]: moved_to_leg += 1
        i, d = leg_idx[j], d2
    dists.append(d)
    weights.append(pt_w[i])
print("UNDER leg points following the leg armour instead of another plate: %d of %d leg points" % (moved_to_leg, sum(1 for x in leg_share if x > LEG_MIN)))
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
