# Puts the new suit together for Unreal (2026-11-05): the rigged chain mail knight with his clips, plus every fitted piece from
# Saved\KnightBuild\Pieces (Tools\BuildNewSuit.ps1), as one skinned mesh on his 24-bone skeleton, one material slot per piece.
# Writes Saved\KnightBuild\Suit\SK_NewSuit.fbx and each piece's two pictures (T_<Piece>_Base.png, T_<Piece>_MR.png, 2048 x 2048).
# Run through Blender: blender -b -P knight_suit_export.py -- <rigged knight.glb> <pieces folder> <output folder>
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.dont_write_bytecode = True
from knight_suit_mail import trim_mail

a = sys.argv[sys.argv.index("--") + 1:]
rig, pieces, out = a[0], a[1], a[2]
os.makedirs(out, exist_ok=True)
PIECES = [("chest", "Chest"), ("legs", "Legs"), ("helmet", "Helmet"), ("gauntlets", "Gauntlets"), ("shoulders", "Shoulders")]
TEX = 2048           # each piece has its own picture, so this is sharper than the old whole-knight 4096
# His rest pose (the clip Rest, owner 2026-11-05: hands almost touching the hips; degrees). Each is how far that part hangs from straight down.
REST_UPPER_OUT, REST_UPPER_FWD = 20, -4   # upper arm: out to his side, and a little back as in his own pose, so the pauldron stays straight over it
REST_FORE_OUT, REST_FORE_FWD = 0, 12    # forearm
REST_WRIST = 8                          # the hand turns in toward the hip from the wrist

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig)
for x in list(bpy.data.objects):
    if x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
body = next(x for x in bpy.data.objects if x.type == "MESH")
arm.name = "Armature"
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
print("SUIT clips", [(x.name, tuple(int(f) for f in x.frame_range)) for x in bpy.data.actions])

def save_pics(ob, tag):
    mat = ob.material_slots[0].material
    ims = [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]
    for im in ims:
        kind = "MR" if "metallic" in im.name else "Base"
        c = im.copy(); c.scale(TEX, TEX)
        c.filepath_raw = os.path.join(out, "T_%s_%s.png" % (tag, kind)); c.file_format = "PNG"; c.save()
        bpy.data.images.remove(c)
    mat.name = "Suit_" + tag

plates = []
for fn, tag in PIECES:
    with bpy.data.libraries.load(os.path.join(pieces, fn + "_clean.blend")) as (src, dst):
        dst.objects = [fn]
    ob = dst.objects[0]; bpy.context.scene.collection.objects.link(ob)
    ob.parent = arm; ob.matrix_parent_inverse.identity(); ob.matrix_basis.identity()
    for m in ob.modifiers:
        if m.type == "ARMATURE": m.object = arm
    save_pics(ob, tag); plates.append(ob)
save_pics(body, "Mail")
bpy.context.view_layer.update()

# ---- (2026-11-11) the gauntlets get shape keys FistR / FistL closing each hand round the grip line every weapon is held on (knight_fist.py; their own half
# closed shape is the relaxed basis), as the bare hand's do in knight_bare_export.py. Unreal imports them as morph targets, set per hand by FKnightAnimator.
import knight_fist
glv = next(p for p in plates if p.name == "gauntlets")
knight_fist.make_fists(glv, arm, bare=False, log=lambda s: print("SUIT " + s))
bpy.context.view_layer.update()

# ---- the mail: only a clean sleeve in the gap between pauldron and bracer stays, plus a dark pelvis (knight_suit_mail.py)
trees = []
for pl in plates:
    wp = pl.matrix_world
    trees.append(BVHTree.FromPolygons([(wp @ v.co)[:] for v in pl.data.vertices], [list(p.vertices) for p in pl.data.polygons]))
trim_mail(arm, body, plates, trees)
tot = len(body.data.polygons) + sum(len(p.data.polygons) for p in plates)
print("SUIT triangles", tot, {p.name: len(p.data.polygons) for p in plates})

# ---- the rest pose: one more clip, the same pose from start to end
arm.data.pose_position = "POSE"
if arm.animation_data is None: arm.animation_data_create()
keep_action = arm.animation_data.action
keep_slot = arm.animation_data.action_slot
arm.animation_data.action = None
for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()
M = arm.matrix_world; M3 = M.to_3x3().normalized(); M3i = M3.inverted()
def turn(pb, R):   # R: a rotation in world space, about the bone's own joint
    Ra = (M3i @ R @ M3).to_4x4(); h = pb.head.copy()
    pb.matrix = Matrix.Translation(h) @ Ra @ Matrix.Translation(-h) @ pb.matrix
    bpy.context.view_layer.update()
def aim(bone, child, side, out_deg, fwd_deg):   # side +1 = his left (+X); his front is -Y
    pb = arm.pose.bones["mixamorig:" + bone]
    now = (M @ arm.pose.bones["mixamorig:" + child].head - M @ pb.head).normalized()
    want = Vector((side * math.tan(math.radians(out_deg)), -math.tan(math.radians(fwd_deg)), -1)).normalized()
    turn(pb, now.rotation_difference(want).to_matrix())
for side, L in ((1, "Left"), (-1, "Right")):
    aim(L + "Arm", L + "ForeArm", side, REST_UPPER_OUT, REST_UPPER_FWD)
    aim(L + "ForeArm", L + "Hand", side, REST_FORE_OUT, REST_FORE_FWD)
    turn(arm.pose.bones["mixamorig:" + L + "Hand"], Matrix.Rotation(math.radians(REST_WRIST) * side, 3, Vector((0, 1, 0))))
# how close the gauntlets come to the chest and leg armour (those two don't move in this pose, so their trees still hold)
gl = next(p for p in plates if p.name == "gauntlets")
gnames = {g.index: g.name for g in gl.vertex_groups}
in_hand = [sum(g.weight for g in v.groups if gnames[g.group].endswith("Hand")) > 0.5 for v in gl.data.vertices]
ge = gl.evaluated_get(bpy.context.evaluated_depsgraph_get()); gm = ge.to_mesh(); gw = gl.matrix_world
near = {}
for i, v in enumerate(gm.vertices):
    co = gw @ v.co; d = min(t.find_nearest(co)[3] for t in trees[:2])   # (trees[0], [1]: chest, legs)
    key = ("L" if co.x > 0 else "R", "hand" if in_hand[i] else "cuff")
    near[key] = min(near.get(key, 9.0), d)
ge.to_mesh_clear()
print("SUIT rest gaps to the armour (cm):", {k: round(d * 100, 1) for k, d in sorted(near.items())})
rest = bpy.data.actions.new("Rest")
arm.animation_data.action = rest
for pb in arm.pose.bones:
    pb.rotation_mode = "QUATERNION"
    for f in (1, 31):
        pb.keyframe_insert("location", frame=f); pb.keyframe_insert("rotation_quaternion", frame=f); pb.keyframe_insert("scale", frame=f)
arm.animation_data.action = keep_action
if keep_action is not None and keep_slot is not None: arm.animation_data.action_slot = keep_slot
for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()
print("SUIT clips now", [x.name for x in bpy.data.actions])

# ---- the file
bpy.ops.object.select_all(action="DESELECT")
for x in [arm, body] + plates: x.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(out, "SK_NewSuit.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                         bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=True,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("SUIT wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
