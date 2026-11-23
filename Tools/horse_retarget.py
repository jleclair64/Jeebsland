# Blender (background): the horse's clips (2026-11-22, agent horse).
#   blender -b --factory-startup -P Tools/horse_retarget.py -- <horse rig .glb> <stag rig with horse clips .glb> <out .glb>
# Why: Meshy rigged the library horse four times (Horse_A with and without its tail, the Riding Horse) and every time answered that the
# four-legged skeleton "was not recognized" for animation (a horse's long legs, neck and tail land on a different bone layout from its
# template; 20 credits spent finding that out). The stag's rig IS recognized, and any four-legged library clip plays on it, so Meshy put
# the horse clips (Horse idle x2, walk, canter, sprint, backward, Fawn sniff for grazing, Cow death) on the stag's rig, and this script
# copies them onto the horse's own (skinned, unrecognized) skeleton:
#  - legs: each horse leg segment is turned to point the way the matching stag segment points in that frame (absolute directions, so the
#    library horse's lifted foreleg is straightened by the clips);
#  - body: the horse's root turns by the stag body's turn from its rest pose (hips to chest) and moves up and down by its root's change,
#    scaled to the horse's size;
#  - neck and head: each segment turns by the matching stag neck segment's turn from its rest (the stag's head bone points up between its
#    antlers, so absolute directions would point the horse's nose at the sky);
#  - tail and ears: carried along unchanged.
# The horse's bones are renamed where deer_export.py needs them (root and the four toes). Writes one .glb holding the skinned horse and
# all eight actions, named as Meshy names them (horse_Idle ...), which Tools/BuildDeer.ps1 reads like the stag's clips file.
import bpy, sys
from mathutils import Vector, Matrix, Quaternion

a = sys.argv[sys.argv.index("--") + 1:]
HORSE, CLIPS, OUT = a[0], a[1], a[2]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=CLIPS)
S = next(o for o in bpy.data.objects if o.type == "ARMATURE"); S.name = "Src"
for o in [o for o in bpy.data.objects if o.type == "MESH"]: bpy.data.objects.remove(o)
src_actions = list(bpy.data.actions)
bpy.ops.import_scene.gltf(filepath=HORSE)
T = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o != S); T.name = "Horse"
for o in [o for o in bpy.data.objects if o.type == "MESH" and not any(m.type == "ARMATURE" for m in o.modifiers)]: bpy.data.objects.remove(o)
for act in list(bpy.data.actions):
    if act not in src_actions: bpy.data.actions.remove(act)
if T.animation_data: T.animation_data.action = None

def j(n): return "joint_%02d" % n
CH = {   # target bones (each pointing at the next point), target points, source points
    "FL": ([j(i) for i in (42, 41, 40, 39, 38, 37)], [j(i) for i in (41, 40, 39, 38, 37, 36)],
           ["clavicle.L", "frontUpr.L", "frontLwr.L", "frontLeg.L_04", "frontHorselink.L", "frontToe.L"]),
    "FR": ([j(i) for i in (49, 48, 47, 46, 45, 44)], [j(i) for i in (48, 47, 46, 45, 44, 43)],
           ["clavicle.R", "frontUpr.R", "frontLwr.R", "frontHorselink.R", "frontFoot.R", "frontToe.R"]),
    "RL": ([j(i) for i in (28, 27, 26, 25, 24, 23)], [j(i) for i in (27, 26, 25, 24, 23, 22)],
           ["hips", "rearUpr.L", "rearLwr.L", "rearHorselink.L", "rearFoot.L", "rearToe.L"]),
    "RR": ([j(i) for i in (21, 20, 19, 18, 17, 16)], [j(i) for i in (20, 19, 18, 17, 16, 15)],
           ["hips", "rearUpr.R", "rearLwr.R", "rearHorselink.R", "rearFoot.R", "rearToe.R"]),
    "NK": ([j(i) for i in (35, 34, 33, 32, 31, 30)], [j(i) for i in (34, 33, 32, 31, 30, 29)],
           ["neck_01", "neck_02", "neck1", "neck2", "head"]),
}
DELTA = {"NK"}
ROOT = j(1)

# rest data, in each armature's world
Sw, Tw = S.matrix_world.copy(), T.matrix_world.copy()
srest = {b.name: Sw @ b.head_local for b in S.data.bones}
trest = {b.name: Tw @ b.head_local for b in T.data.bones}
# the stag file faces its own way: turn its directions about Z onto the horse's forward
def flat(v): return Vector((v.x, v.y, 0)).normalized()
fs = flat(srest["chest"] - srest["hips"]); ft = flat(trest[j(9)] - trest[ROOT])
C = fs.rotation_difference(ft)
print("HORSE source forward %s, horse forward %s" % (tuple(round(x, 2) for x in fs), tuple(round(x, 2) for x in ft)))
scale = (trest[ROOT].z - min(p.z for p in trest.values())) / max(1e-3, srest["root"].z - min(p.z for p in srest.values()))
print("HORSE size ratio %.2f" % scale)

ctrl = {}   # bone -> (chain, index)
for key, (bones, nxt, sp) in CH.items():
    for i, b in enumerate(bones): ctrl[b] = (key, i)
order = sorted(T.data.bones, key=lambda b: len(b.parent_recursive))
Tinv = Tw.inverted()

def seg_src(key, i, P, n_t):
    sp = CH[key][2]; m = len(sp) - 1
    if key in ("FL", "FR", "RL", "RR"): k = (0, 1, 2, 3, 4, 4)[i]   # shoulder/hip, upper, lower, cannon, pastern; the hoof follows the pastern
    else: k = min(m - 1, int(round(i * (m - 1) / max(1, n_t - 1))))
    return sp[k], sp[k + 1]

for act in src_actions:
    S.animation_data.action = act
    try:
        if act.slots: S.animation_data.action_slot = act.slots[0]
    except Exception: pass
    f0, f1 = int(act.frame_range[0]), int(round(act.frame_range[1]))
    new = bpy.data.actions.new(act.name)
    T.animation_data_create(); T.animation_data.action = new
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f)
        P = {pb.name: C @ (Sw @ pb.head) for pb in S.pose.bones}   # (world, turned onto the horse's forward)
        R = {n: C @ v for n, v in srest.items()}
        pose = {}   # bone -> posed matrix in horse armature space
        for b in order:
            pb = T.pose.bones[b.name]
            rest = b.matrix_local
            if b.parent is None:
                inh = rest.copy()
            else:
                inh = pose[b.parent.name] @ b.parent.matrix_local.inverted() @ rest
            M = inh
            if b.name == ROOT:
                q = (R["chest"] - R["hips"]).rotation_difference(P["chest"] - P["hips"])
                q = (Tinv.to_quaternion() @ q @ Tw.to_quaternion())
                dz = (P["root"].z - R["root"].z) * scale
                M = Matrix.Translation(rest.translation + Tinv.to_3x3() @ Vector((0, 0, dz))) @ q.to_matrix().to_4x4() @ Matrix.Translation(-rest.translation) @ rest
            elif b.name in ctrl:
                key, i = ctrl[b.name]
                nxt = CH[key][1][i]
                seg_rest = T.data.bones[nxt].head_local - b.head_local
                cur = inh.to_3x3() @ (rest.to_3x3().inverted() @ seg_rest)
                a_, b_ = seg_src(key, i, P, len(CH[key][0]))
                if key in DELTA:
                    qd = (R[b_] - R[a_]).rotation_difference(P[b_] - P[a_])
                    want = Tinv.to_3x3() @ (qd @ (Tw.to_3x3() @ cur))
                else:
                    want = Tinv.to_3x3() @ (P[b_] - P[a_])
                q = cur.rotation_difference(want)
                t = inh.translation.copy()
                M = Matrix.Translation(t) @ q.to_matrix().to_4x4() @ Matrix.Translation(-t) @ inh
            pose[b.name] = M
            basis = inh.inverted() @ M
            loc, rot, _ = basis.decompose()
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = rot
            pb.location = loc if b.name == ROOT else Vector()
            pb.keyframe_insert("rotation_quaternion", frame=f)
            if b.name == ROOT: pb.keyframe_insert("location", frame=f)
    tr = T.animation_data.nla_tracks.new(); tr.name = act.name
    st = tr.strips.new(act.name, f0, new); tr.mute = True
    T.animation_data.action = None
    print("HORSE clip %s frames %d-%d" % (act.name, f0, f1))

# names deer_export.py looks for
for old, nm in ((ROOT, "root"), (j(36), "frontToe.L"), (j(43), "frontToe.R"), (j(22), "rearToe.L"), (j(15), "rearToe.R")):
    T.data.bones[old].name = nm
for o in [S]: bpy.data.objects.remove(o)
for act in src_actions: bpy.data.actions.remove(act)
for pb in T.pose.bones:
    pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
bpy.ops.object.select_all(action="DESELECT")
for o in bpy.data.objects: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_animation_mode="NLA_TRACKS", export_image_format="AUTO")
print("HORSE wrote", OUT)
