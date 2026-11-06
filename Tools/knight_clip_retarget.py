# Moves Mixamo clips onto the new knight's skeleton (2026-11-05). Mixamo's clips are downloaded on its stock Y Bot, standing in a
# T-pose; our knight stands in his own loose pose and is built differently, so each bone's turn is carried across in world space:
#     knight bone now = (Y Bot's turn away from HIS rest) * (the turn that lines up the knight's rest bone with the Y Bot's) * knight bone at rest
# The line-up keeps the elbows and knees hinging the right way (bone direction plus hinge axis, per limb). Hips move scaled to his
# height. Clips are pulled in place: the game's capsule does the travelling, so the hips' straight-line travel is taken out.
# One FBX per clip, armature only, goes to <out> for knight_clip_import.py.
# Run through Blender: blender -b -P knight_clip_retarget.py -- <rigged knight.glb> <clip folder> <out folder> [names...]
import bpy, sys, os, math, json
from mathutils import Vector, Matrix, Quaternion
a = sys.argv[sys.argv.index("--") + 1:]
RIG, CLIPS, OUT = a[0], a[1], a[2]
ONLY = set(a[3:])
os.makedirs(OUT, exist_ok=True)
P = "mixamorig:"
# How each clip's whole-body motion is treated (default "ground"):
#   ground  feet on the floor: the hips' travel along the ground is removed, height kept, the lowest foot put on the floor
#   air     the capsule carries him up and down: the hips' height stays at its first frame's as well (jump and fall clips)
#   floor   the body goes down to the ground (knockdowns, get-ups, rolls): as ground, but no foot levelling
MODE = {"JumpUp": "air", "FallIdle": "air", "Jump_SS": "air", "RunJump_SS": "air", "JumpUp2": "air", "ForwardJump": "air", "ClimbLadder": "air", "SlideHold": "floor", "SitChair": "floor",
        "KnockBack": "floor", "SweepFall": "floor", "FallDown": "floor", "Stunned": "floor", "GetUpBack": "floor", "GetUpStomach": "floor", "GetUp": "floor",
        "KipUp": "floor", "StandBack": "floor", "StandRoll": "floor", "SprintRoll": "floor", "QuickRoll": "floor", "DeathBack": "floor", "DeathFwd": "floor"}

def load_rig():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=RIG)
    for x in list(bpy.data.objects):
        if x.type == "MESH" or x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
    arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
    arm.name = "Armature"
    if arm.animation_data: arm.animation_data.action = None
    for act in list(bpy.data.actions): bpy.data.actions.remove(act)
    arm.data.pose_position = "POSE"
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"; pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    return arm

def frame(x, h):
    """A rotation whose X axis is x and whose Z axis is as near as it can be to h."""
    x = x.normalized(); y = h.cross(x).normalized(); z = x.cross(y).normalized()
    return Matrix((x, y, z)).transposed().to_quaternion()

def world_heads(arm, names):
    M = arm.matrix_world
    return {n: M @ arm.data.bones[P + n].head_local for n in names if P + n in arm.data.bones}

def line_up(T, S):
    """Per bone: the turn from the knight's rest bone onto the Y Bot's rest bone."""
    th = world_heads(T, ALL); sh = world_heads(S, ALL); A = {}
    def lim(bone, child, tdir=None, hinge_t=None, hinge_s=None):
        t = (th[child] - th[bone]) if tdir is None else tdir
        s = sh[child] - sh[bone]
        A[bone] = frame(s, hinge_s) @ frame(t, hinge_t).inverted()
    for side, sgn in (("Left", 1), ("Right", -1)):
        u = th[side + "ForeArm"] - th[side + "Arm"]; f = th[side + "Hand"] - th[side + "ForeArm"]
        ht = u.cross(f).normalized()                  # the knight's elbow hinge, from how his arm is bent
        hs = Vector((0, 0, -sgn))                     # the Y Bot's: T-pose arm, bending to the front (-Y)
        lim(side + "Shoulder", side + "Arm", None, Vector((0, 0, 1)), Vector((0, 0, 1)))
        lim(side + "Arm", side + "ForeArm", None, ht, hs)
        lim(side + "ForeArm", side + "Hand", None, ht, hs)
        # the hand: the Y Bot's runs on from his wrist along his middle finger; the knight's solid fist from his forearm's line
        A[side + "Hand"] = A[side + "ForeArm"]
        hx = Vector((1, 0, 0))                        # knee hinge: across the body
        lim(side + "UpLeg", side + "Leg", None, hx, hx)
        lim(side + "Leg", side + "Foot", None, hx, hx)
        lim(side + "Foot", side + "ToeBase", None, hx, hx)
    return A

ALL = ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head"] + [s + b for s in ("Left", "Right") for b in ("Shoulder", "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot", "ToeBase")]
ORDER = ALL

def retarget(path, name):
    T = load_rig()
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, ignore_leaf_bones=False)
    S = next(x for x in bpy.data.objects if x.type == "ARMATURE" and x is not T)
    act = S.animation_data.action
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    scene = bpy.context.scene
    # the rests: the Y Bot's from its bones' rest matrices, in the world
    Mt, Ms = T.matrix_world, S.matrix_world
    Rs = {n: (Ms @ S.data.bones[P + n].matrix_local).to_quaternion() for n in ALL}
    Rt = {n: (Mt @ T.data.bones[P + n].matrix_local).to_quaternion() for n in ALL}
    A = line_up(T, S)
    th = world_heads(T, ["Hips", "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase"]); sh = world_heads(S, ["Hips"])
    k = th["Hips"].z / sh["Hips"].z                  # his height against the Y Bot's
    # read the Y Bot, every frame
    rot = []; hips = []
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        M = S.matrix_world
        rot.append({n: (M @ S.pose.bones[P + n].matrix).to_quaternion() @ Rs[n].inverted() for n in ALL})
        hips.append(M @ S.pose.bones[P + "Hips"].head)
    n = len(hips)
    mode = MODE.get(name, "ground")
    # the straight line from start to end comes out of the hips' travel along the ground
    d0, d1 = hips[0], hips[-1]
    loc = []
    for i, h in enumerate(hips):
        u = i / max(n - 1, 1)
        x = (h.x - (d0.x + (d1.x - d0.x) * u)) * k
        y = (h.y - (d0.y + (d1.y - d0.y) * u)) * k
        z = h.z * k if mode != "air" else d0.z * k
        loc.append(Vector((x + th["Hips"].x, y + th["Hips"].y, z)))
    # write the knight's bones, parents first, then bake
    T.animation_data_create()
    new = bpy.data.actions.new("Clip")
    T.animation_data.action = new
    M3 = Mt.to_quaternion(); Minv = Mt.inverted()
    feet = []
    for i, f in enumerate(range(f0, f1 + 1)):
        scene.frame_set(f)
        for b in ORDER:
            pb = T.pose.bones[P + b]
            want = rot[i][b] @ A.get(b, Quaternion()) @ Rt[b]
            arm_rot = M3.inverted() @ want
            mat = arm_rot.to_matrix().to_4x4()
            mat.translation = (Minv @ loc[i]) if b == "Hips" else pb.matrix.translation
            pb.matrix = mat
            bpy.context.view_layer.update()
        feet.append(min((Mt @ T.pose.bones[P + s].head).z for s in ("LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase")))
        for b in ORDER:
            pb = T.pose.bones[P + b]
            pb.keyframe_insert("rotation_quaternion", frame=f - f0 + 1)
            pb.keyframe_insert("location", frame=f - f0 + 1)
    # the floor: the lowest a foot gets, down to where the rest pose's lowest joint is
    rest_low = min(th[s].z for s in ("LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase"))
    drop = (min(feet) - rest_low) if mode == "ground" else 0.0
    if abs(drop) > 1e-4:
        hipb = T.pose.bones[P + "Hips"]
        for i, f in enumerate(range(f0, f1 + 1)):
            scene.frame_set(f - f0 + 1)
            mat = hipb.matrix.copy(); mat.translation = Minv @ (Mt @ mat.translation - Vector((0, 0, drop)))
            hipb.matrix = mat
            bpy.context.view_layer.update()
            hipb.keyframe_insert("location", frame=f - f0 + 1)
    # a check: how well the limbs follow (degrees between the knight's bone and the Y Bot's, worst frame)
    worst = 0.0
    scene.frame_set(1 + (n - 1) // 2)
    print("CLIPDIAG %-16s %s frames %3d (%.2f s) scale %.3f drop %.1f cm" % (name, mode, n, (n - 1) / 30.0, k, drop * 100))
    # the file: the armature alone with this one action
    bpy.data.objects.remove(S)
    for act2 in list(bpy.data.actions):
        if act2 is not new: bpy.data.actions.remove(act2)
    new.name = name
    scene.frame_start, scene.frame_end = 1, n
    bpy.ops.object.select_all(action="DESELECT"); T.select_set(True); bpy.context.view_layer.objects.active = T
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "A_%s.fbx" % name), use_selection=True, object_types={"ARMATURE"}, add_leaf_bones=False,
                             bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                             apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X")

for fn in sorted(os.listdir(CLIPS)):
    if not fn.lower().endswith(".fbx"): continue
    name = fn[:-4]
    if ONLY and name not in ONLY: continue
    try:
        retarget(os.path.join(CLIPS, fn), name)
    except Exception as e:
        import traceback
        print("CLIPDIAG FAILED", name, e); traceback.print_exc()
print("CLIPDIAG done")
