# Blender: builds the knight's animation rig, an armature that matches the game's joints exactly, with the game's own
# armour pieces (Content/Knight/KnightParts.bin) hung on its bones, and saves it to Art/Knight/knight_rig.blend. Animation
# clips are made on this rig and exported with knight_clip_export.py.
#
# The C++ is the source of truth; nothing here is typed in by hand:
#   joint positions   KnightDesign::PivotPos (Source/Jeebsland/KnightDesign.h)
#   joint parenting   the MakePivot lines in AKnightCharacter's constructor (Source/Jeebsland/KnightCharacter.cpp)
#   piece -> joint    the Pivots[...] table in AKnightCharacter::BuildBody (Source/Jeebsland/KnightCharacterBody.cpp)
#
# COORDINATES. The game is left-handed: X forward, Y right, Z up, in centimetres, and every joint has zero rotation at rest
# (its axes are the body's). Blender is right-handed, so the rig is built with Y flipped: Blender (x, y, z) = game (X, -Y, Z),
# 1 Blender unit = 1 cm. The knight faces Blender +X, his right side is Blender -Y. A rotation keeps its meaning across the
# flip once its quaternion becomes (x, y, z, w)_game = (-x, y, -z, w)_blender (knight_clip_export.py does this).
#
# Run: blender -b -P Tools/knight_rig.py -- <project dir> [<picture dir>]
#   Also renders front / side / back pictures of the rig at rest and in a test pose into the picture dir.
import bpy, sys, os, re, struct, math
from mathutils import Vector, Matrix, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
proj = argv[0]
pics = argv[1] if len(argv) > 1 else os.path.join(proj, "Saved", "KnightRig")
os.makedirs(pics, exist_ok=True)
src = os.path.join(proj, "Source", "Jeebsland")
read = lambda f: open(os.path.join(src, f), encoding="utf-8").read()

# ---- The joints, read from the C++ ----
design = read("KnightDesign.h")
enum_body = re.search(r"enum\s+EPivot[^{]*\{([^}]*)\}", design) or re.search(r"\{\s*(P_Body[^}]*P_Count)", design)
pivot_ids = [n.strip() for n in re.sub(r"//[^\n]*", "", enum_body.group(1)).split(",") if n.strip()]
pivot_ids = [n for n in pivot_ids if n != "P_Count"]
pivot_index = {n: i for i, n in enumerate(pivot_ids)}
pivot_pos = {m.group(1): Vector((float(m.group(2)), float(m.group(3)), float(m.group(4))))
             for m in re.finditer(r"case\s+(P_\w+):\s*return\s+FVector\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)", design)}

ctor = read("KnightCharacter.cpp")
joints = {}     # component variable -> (joint name, parent variable or None, game position)
for m in re.finditer(r"(\w+)\s*=\s*MakePivot\(TEXT\(\"(\w+)\"\),\s*(\w+),\s*(.+?)\);", ctor):
    var, name, parent, pos = m.group(1), m.group(2), m.group(3), m.group(4)
    pm = re.search(r"PivotPos\(\s*(?:KnightDesign::)?(P_\w+)\s*\)", pos)
    if pm: p = pivot_pos[pm.group(1)]
    else:
        fm = re.search(r"FVector\(\s*([-\d.]+)f?\s*,\s*([-\d.]+)f?\s*,\s*([-\d.]+)f?\s*\)", pos)
        p = Vector((float(fm.group(1)), float(fm.group(2)), float(fm.group(3))))
    joints[var] = (name, None if parent == "nullptr" else parent, p)

body = read("KnightCharacterBody.cpp")
var_of_pivot = {pivot_index[m.group(1)]: m.group(2) for m in re.finditer(r"Pivots\[KnightDesign::(P_\w+)\]\s*=\s*(\w+);", body)}
print("RIG %d joints, %d pivots mapped" % (len(joints), len(var_of_pivot)))

def to_blender(v): return Vector((v.x, -v.y, v.z))

# ---- A clean scene in centimetres ----
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.01
scene.render.fps = 30

# ---- The armature: one bone per joint, at the joint, pointing at its first child (or down, if it has none) ----
arm_data = bpy.data.armatures.new("KnightRig")
arm = bpy.data.objects.new("KnightRig", arm_data)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
children = {}
for var, (name, parent, p) in joints.items():
    children.setdefault(parent, []).append(var)
for var, (name, parent, p) in joints.items():
    eb = arm_data.edit_bones.new(name)
    head = to_blender(p)
    kids = [joints[c][2] for c in children.get(var, [])]
    tail = to_blender(kids[0]) if kids and (to_blender(kids[0]) - head).length > 2.0 else head + Vector((0.0, 0.0, -8.0 if var not in ("BodyRoot", "UpperPivot", "HeadPivot", "PlumePivot") else 8.0))
    eb.head, eb.tail, eb.roll = head, tail, 0.0
for var, (name, parent, p) in joints.items():
    if parent: arm_data.edit_bones[name].parent = arm_data.edit_bones[joints[parent][0]]
bpy.ops.object.mode_set(mode='OBJECT')
for b in arm.pose.bones: b.rotation_mode = 'QUATERNION'

# ---- The game's armour pieces, each hung (rigidly) on its joint's bone ----
data = open(os.path.join(proj, "Content", "Knight", "KnightParts.bin"), "rb").read()
assert data[:4] == b"JKNT"
ver, count = struct.unpack_from("<ii", data, 4)
off = 12
cols = [(0.62, 0.64, 0.70), (0.45, 0.47, 0.53), (0.78, 0.66, 0.40), (0.55, 0.58, 0.66)]
for k in range(count):
    pivot, slot, nv, ni = struct.unpack_from("<iiii", data, off); off += 16
    verts = []
    for i in range(nv):
        f = struct.unpack_from("<12f", data, off); off += 48
        verts.append(to_blender(Vector(f[0:3])))
    idx = struct.unpack_from("<%di" % ni, data, off); off += 4 * ni
    faces = [(idx[t], idx[t + 2], idx[t + 1]) for t in range(0, ni, 3)]   # (the Y flip mirrors, so the winding turns over)
    me = bpy.data.meshes.new("Piece_%d_%d" % (pivot, slot))
    me.from_pydata([tuple(v) for v in verts], [], faces)
    ob = bpy.data.objects.new(me.name, me)
    scene.collection.objects.link(ob)
    mat = bpy.data.materials.new(me.name); mat.diffuse_color = (*cols[slot % len(cols)], 1.0); me.materials.append(mat)
    var = var_of_pivot.get(pivot)
    if var is None: print("RIG piece on unmapped pivot %d" % pivot); continue
    ob.parent = arm; ob.parent_type = 'BONE'; ob.parent_bone = joints[var][0]
    ob.matrix_world = Matrix.Identity(4)   # the vertices are already where they belong at rest
print("RIG %d pieces hung on the bones" % count)

# A plain sword in the right fist, so clips can be judged by the blade (the grip and blade direction from KnightDesign.h)
grip = re.search(r"GripPos\(bool bLeft\)\s*\{\s*return FVector\(([-\d.]+),\s*bLeft \? [-\d.]+ : ([-\d.]+),\s*([-\d.]+)\)", design)
blade = re.search(r"BladeDir\(int32 Side\)\s*\{\s*return FVector\(([-\d.]+),\s*-Side \* ([-\d.]+),\s*([-\d.]+)\)", design)
if grip and blade:
    g = Vector((float(grip.group(1)), float(grip.group(2)), float(grip.group(3))))
    d = Vector((float(blade.group(1)), -float(blade.group(2)), float(blade.group(3)))).normalized()
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    sw = bpy.context.active_object; sw.name = "Sword"
    sw.scale = (2.0, 0.6, 1.0)   # (stretched along the blade below)
    look = to_blender(d).to_track_quat('X', 'Z')
    sw.matrix_world = Matrix.Translation(to_blender(g + d * 40.0)) @ look.to_matrix().to_4x4() @ Matrix.Diagonal((80.0, 1.2, 5.0, 1.0))
    wr = joints.get("WristR")
    if wr:
        mw = sw.matrix_world.copy()
        sw.parent = arm; sw.parent_type = 'BONE'; sw.parent_bone = wr[0]; sw.matrix_world = mw
    m = bpy.data.materials.new("Blade"); m.diffuse_color = (0.85, 0.88, 0.95, 1.0); sw.data.materials.append(m)

# ---- Pictures: workbench, front / side / back, at rest and in a test pose ----
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.render.resolution_x, scene.render.resolution_y = 600, 800
cam_data = bpy.data.cameras.new("Cam"); cam_data.type = 'ORTHO'; cam_data.ortho_scale = 230.0; cam_data.clip_end = 5000.0
cam = bpy.data.objects.new("Cam", cam_data); scene.collection.objects.link(cam); scene.camera = cam
def shoot(name, yaw_deg):
    a = math.radians(yaw_deg)
    pos = Vector((math.cos(a) * 600.0, math.sin(a) * 600.0, -10.0))
    cam.location = pos
    cam.rotation_euler = (Vector((0.0, 0.0, -10.0)) - pos).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(pics, name + ".png")
    bpy.ops.render.render(write_still=True)
for nm, yaw in (("front", 0.0), ("side", -90.0), ("back", 180.0)):   # (the knight faces +X; his right is -Y, so -90 looks at his right side)
    shoot("rig_rest_" + nm, yaw)

# Test pose: sword arm out to the side, elbow bent, wrist cocked; the chest turned. Every joint should carry its pieces.
def pose(bone, axis, deg):
    pb = arm.pose.bones[bone]; pb.rotation_quaternion = Quaternion(axis, math.radians(deg))
names = {v[0] for v in joints.values()}
if "ArmPivotR" in names: pose("ArmPivotR", (1.0, 0.0, 0.0), -70.0)   # (his right is Blender -Y: a negative turn about X lifts the arm out)
if "ElbowR" in names: pose("ElbowR", (1.0, 0.0, 0.0), 50.0)
if "WristR" in names: pose("WristR", (0.0, 1.0, 0.0), 40.0)
if "UpperPivot" in names: pose("UpperPivot", (0.0, 1.0, 0.0), 25.0)
bpy.context.view_layer.update()
for nm, yaw in (("front", 0.0), ("side", -90.0)):
    shoot("rig_test_" + nm, yaw)
for b in arm.pose.bones: b.rotation_quaternion = Quaternion()

out = os.path.join(proj, "Art", "Knight", "knight_rig.blend")
bpy.ops.wm.save_as_mainfile(filepath=out)
print("RIG saved %s" % out)
