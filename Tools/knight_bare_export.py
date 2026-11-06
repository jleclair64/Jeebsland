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

REGIONS = ["Bare_Helmet", "Bare_Chest", "Bare_Gauntlets", "Bare_Legs"]
def region(bone):
    n = bone.replace("mixamorig:", "")
    if n in ("Head", "Neck") or n.startswith("HeadTop"): return 0
    if n.startswith(("Spine", "LeftShoulder", "RightShoulder")): return 1
    if "Arm" in n or "Hand" in n: return 2
    return 3   # the hips and the legs

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
for p in me.polygons:
    s = [sum(vert_score[i][r] for i in p.vertices) for r in range(len(REGIONS))]
    p.material_index = s.index(max(s))
    counts[p.material_index] += 1
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
