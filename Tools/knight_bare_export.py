# The knight with nothing on (2026-11-05): the whole chain mail body from his rig, for armour slots left empty. Under plate the mail is deleted from the suit
# itself (knight_suit_mail.py: it never shows through), so an empty slot needs something else to show: this mesh, cut by the same regions as the armour, one material
# per slot. FKnightAnimator::Wear shows a region only while its slot is empty. Same 24 bones, so it simply follows the suit.
# Writes <output folder>\SK_NewSuitBare.fbx. Run through Blender: blender -b -P knight_bare_export.py -- <rigged knight.glb> <output folder>
import bpy, sys, os

a = sys.argv[sys.argv.index("--") + 1:]
rig, out = a[0], a[1]
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

# (2026-11-06) The joins between pieces have regions of their own, so they can show when EITHER piece next to them is off (FKnightAnimator::ShowBare): the neck, the collar, the upper arm and the waist.
REGIONS = ["Bare_Helmet", "Bare_Neck", "Bare_Chest", "Bare_Collar", "Bare_UpperArm", "Bare_Gauntlets", "Bare_Legs", "Bare_Waist"]
def region(bone):
    n = bone.replace("mixamorig:", "")
    if n.lower().startswith("head"): return 0   # (the rig also has a "headfront" bone)
    if n == "Neck": return 1
    if n.startswith("Spine"): return 2
    if n in ("LeftShoulder", "RightShoulder"): return 3
    if n in ("LeftArm", "RightArm"): return 4
    if "Arm" in n or "Hand" in n: return 5
    return 6   # the hips and the legs

names = {g.index: g.name for g in body.vertex_groups}
base = body.material_slots[0].material
body.data.materials.clear()
for r in REGIONS:
    m = base.copy(); m.name = r
    body.data.materials.append(m)
me = body.data
vert_score = []
for v in me.vertices:
    s = [0.0] * len(REGIONS)
    for g in v.groups: s[region(names[g.group])] += g.weight
    vert_score.append(s)
counts = [0] * len(REGIONS)
bpy.context.view_layer.update()
def bone_line(a, b):
    return arm.matrix_world @ arm.data.bones["mixamorig:" + a].head_local, arm.matrix_world @ arm.data.bones["mixamorig:" + b].head_local
ARM_LINES = [bone_line("LeftArm", "LeftForeArm"), bone_line("RightArm", "RightForeArm")]
def along_arm(c):
    """How far down the upper arm this point lies (0 shoulder, 1 elbow), and how far in toward the body from the bone."""
    a, b = min(ARM_LINES, key=lambda L: abs(L[0].x - c.x))
    ab = b - a
    t = max(0.0, min(1.0, (c - a).dot(ab) / ab.dot(ab)))
    on = a + ab * t
    return t, (on.x - c.x) * (1.0 if on.x > 0 else -1.0)
UNDER_PLATE_X = 0.165  # a little inside half the chest plate's width at the armhole (0.19), so the jagged cut edge is behind the plate's rim, seen from the side too
WAIST_Z = 0.86         # the top of the leg piece's skirt
for p in me.polygons:
    s = [sum(vert_score[i][r] for i in p.vertices) for r in range(len(REGIONS))]
    r = s.index(max(s))
    c = body.matrix_world @ p.center
    # The neck goes by height, not by bone: hardly a face is weighted mostly to the neck bone. It runs from the top of the collar to under the helmet's rim, all the way round.
    if r <= 3 and 1.47 <= c.z < 1.60 and (c.x * c.x + (c.y - 0.02) * (c.y - 0.02)) ** 0.5 < 0.15: r = 1
    # The rig weights the side of the ribs to the arm bones too, and those faces are chest: arm is only what is round the bone and not far in from it. (Otherwise the chest has holes
    # at its sides with the chest plate off.) The lowest part of the upper arm goes with the gauntlets: the arm harness's sleeve is there and it pokes through.
    if r == 4:
        t, inward = along_arm(c)
        if inward > 0.06: r = 2 if c.z < 1.36 else 3
        elif t > 0.72: r = 5
    # The collar shows with the chest plate still on: only its outer top, the part clear of the plate
    if r == 3 and (c.z < 1.36 or abs(c.x) < UNDER_PLATE_X): r = 2
    # The waist, between the bottom of the chest plate and the top of the skirt, belongs to both of them
    if r == 6 and c.z >= WAIST_Z: r = 7
    p.material_index = r
    counts[r] += 1
print("BARE faces by region", dict(zip(REGIONS, counts)))

bpy.ops.object.select_all(action="DESELECT")
for x in (arm, body): x.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(out, "SK_NewSuitBare.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False, bake_anim=False,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("BARE wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
