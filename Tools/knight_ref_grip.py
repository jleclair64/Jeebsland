# Measures the Mixamo paladin's real sword and shield against his hand and forearm, and says what that means on our knight's bones.
import bpy, sys, os, json
from mathutils import Vector, Matrix, Quaternion
a = sys.argv[sys.argv.index("--") + 1:]
PAL, RIG, YBOT = a[0], a[1], a[2]
P = "mixamorig:"
ALL = ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head"] + [s + b for s in ("Left", "Right") for b in ("Shoulder", "Arm", "ForeArm", "Hand", "UpLeg", "Leg", "Foot", "ToeBase")]

def frame(x, h):
    x = x.normalized(); y = h.cross(x).normalized(); z = x.cross(y).normalized()
    return Matrix((x, y, z)).transposed().to_quaternion()
def world_heads(arm, names):
    M = arm.matrix_world
    return {n: M @ arm.data.bones[P + n].head_local for n in names if P + n in arm.data.bones}
def line_up(T, S):
    th = world_heads(T, ALL); sh = world_heads(S, ALL); A = {}
    def lim(bone, child, tdir=None, hinge_t=None, hinge_s=None):
        t = (th[child] - th[bone]) if tdir is None else tdir
        s = sh[child] - sh[bone]
        A[bone] = frame(s, hinge_s) @ frame(t, hinge_t).inverted()
    for side, sgn in (("Left", 1), ("Right", -1)):
        u = th[side + "ForeArm"] - th[side + "Arm"]; f = th[side + "Hand"] - th[side + "ForeArm"]
        ht = u.cross(f).normalized()
        hs = Vector((0, 0, -sgn))
        lim(side + "Arm", side + "ForeArm", None, ht, hs)
        lim(side + "ForeArm", side + "Hand", None, ht, hs)
        A[side + "Hand"] = A[side + "ForeArm"]
    return A

def pca(pts):
    c = sum(pts, Vector()) / len(pts)
    C = [[0]*3 for _ in range(3)]
    for p in pts:
        d = p - c
        for i in range(3):
            for j in range(3): C[i][j] += d[i]*d[j]
    v = Vector((1, 0.3, 0.2)).normalized()
    m = Matrix(C)
    for _ in range(60): v = (m @ v).normalized()
    # smallest axis too: power iteration on (trace I - C)
    t = C[0][0] + C[1][1] + C[2][2]
    m2 = Matrix.Identity(3) * t - m
    n = Vector((0.2, 1, 0.3)).normalized()
    for _ in range(200): n = (m2 @ n).normalized()
    return c, v, n

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=PAL, automatic_bone_orientation=False, ignore_leaf_bones=False)
S = next(x for x in bpy.data.objects if x.type == "ARMATURE")
S.name = "Pal"
S.data.pose_position = "REST"
bpy.context.view_layer.update()
res = {}
parts = {}
for o in list(bpy.data.objects):
    if o.type != "MESH" or len(o.vertex_groups) != 1: continue
    g = o.vertex_groups[0].name.replace(P, "")
    pts = [o.matrix_world @ v.co for v in o.data.vertices]
    parts[g] = pts
    lo = [min(p[i] for p in pts) for i in range(3)]; hi = [max(p[i] for p in pts) for i in range(3)]
    print("GRIP part", o.name, len(pts), "on", g, "lo", [round(x, 3) for x in lo], "hi", [round(x, 3) for x in hi])
# load the knight's rig and work out the line-up the clips use
bpy.ops.import_scene.gltf(filepath=RIG)
T = next(x for x in bpy.data.objects if x.type == "ARMATURE" and x is not S)
T.data.pose_position = "REST"
bpy.context.view_layer.update()
A = line_up(T, S)
Ms = S.matrix_world
Mt = T.matrix_world
Rs = {n: (Ms @ S.data.bones[P + n].matrix_local).to_quaternion() for n in ALL}
for g0, pts in parts.items():
    if "joint" not in g0: continue
    b0 = S.data.bones.get(P + g0) or S.data.bones.get(g0)
    print("GRIP joint", g0, "parent", b0.parent.name, "head", Ms @ b0.head_local, "tail", Ms @ b0.tail_local)
    g = b0.parent.name.replace(P, "")
    head = Ms @ S.data.bones[P + g].head_local
    c, ax, nrm = pca(pts)
    print("GRIP", g, "head", head, "centre", c, "from head", c - head, "long axis", ax, "flat normal", nrm)
    far = max(pts, key=lambda p: (p - head).length)
    near = min(pts, key=lambda p: (p - head).length)
    print("GRIP   farthest", far - head, (far - head).length, "nearest", near - head, (near - head).length)
    # along the long axis: which end has the tip
    if (far - head).dot(ax) < 0: ax = -ax
    Ainv = A[g].inverted()
    # into the knight's bind pose: world (Blender) -> as if his hand stood at its rest
    d = Ainv @ ax; n = Ainv @ nrm; off = Ainv @ (c - head)
    print("GRIP   knight bind: axis", d, "normal", n, "centre off (m)", off)
    res[g0] = {"parent": g, "axis_ue": [d.x, -d.y, d.z], "normal_ue": [n.x, -n.y, n.z], "off_ue_cm": [off.x * 100, -off.y * 100, off.z * 100],
              "far_ue_cm": list(((Ainv @ (far - head)) * 100)), "len": (far - head).length * 100}
print("GRIP result", json.dumps(res))
json.dump(res, open(a[3], "w"), indent=1)
