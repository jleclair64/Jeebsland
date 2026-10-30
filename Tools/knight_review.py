# Rebuilds the knight in Blender the way the game assembles him (Content/Knight/KnightParts.bin pieces on their joints,
# the painted texture, the dark body inside) in a chosen pose, and renders views to check him against the Meshy model.
# Run: blender -b -P Tools/knight_review.py -- <project dir> <out dir> <pose>      pose: stand | run | swing | crouch
import bpy, struct, sys, math, os
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
proj, out, pose = argv[0], argv[1], argv[2]
bpy.ops.wm.read_factory_settings(use_empty=True)

# joints (KnightDesign::PivotPos) and the parent of each
PIV = {0: (0, 0, 0), 1: (0, 0, -41), 2: (0, 0, 23), 6: (-3, -29, 14), 7: (-3, 29, 14), 8: (-5, -34.5, -6), 9: (-5, 34.5, -6),
       10: (0, -13, -26), 11: (0, 13, -26), 12: (-1, -13, -51), 13: (-1, 13, -51), 14: (-4, -13, -82), 15: (-4, 13, -82)}
PARENT = {0: None, 1: 0, 2: 1, 6: 1, 7: 1, 8: 6, 9: 7, 10: 0, 11: 0, 12: 10, 13: 11, 14: 12, 15: 13}
WRIST = {8: (-2, -37, -29), 9: (-2, 37, -29)}

def pitch(deg):   # Unreal pitch: + swings a hanging limb forward
    return Matrix.Rotation(math.radians(-deg), 4, 'Y')
def roll(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'X')
POSES = {
    "stand": {10: pitch(12), 11: pitch(12), 12: pitch(-24), 13: pitch(-24), 14: pitch(12), 15: pitch(12), 8: pitch(20), 9: pitch(20)},
    "run":   {10: pitch(40), 11: pitch(-25), 12: pitch(-20), 13: pitch(-72), 14: pitch(-20), 15: pitch(97), 6: pitch(-30), 7: pitch(30), 8: pitch(60), 9: pitch(60)},
    "swing": {7: pitch(150), 9: pitch(30), 10: pitch(12), 11: pitch(12), 12: pitch(-24), 13: pitch(-24), 14: pitch(12), 15: pitch(12)},
    "crouch": {10: pitch(100), 11: pitch(100), 12: pitch(-140), 13: pitch(-140), 14: pitch(40), 15: pitch(40), 1: pitch(24)},
}
P = POSES[pose]
world = {}
def joint_matrix(j):
    if j in world: return world[j]
    par = PARENT[j]
    M = joint_matrix(par) if par is not None else Matrix.Identity(4)
    p = Vector(PIV[j])
    R = P.get(j, Matrix.Identity(4))
    world[j] = M @ Matrix.Translation(p) @ R @ Matrix.Translation(-p)
    return world[j]

def to_view(v):   # game cm -> Blender metres, looking at it the right way round (x right = game -y... see note)
    return Vector((v.y / 100.0, -v.x / 100.0, v.z / 100.0))

img = bpy.data.images.load(os.path.join(proj, "Art", "Knight", "T_Knight_Base.jpg"))
mat = bpy.data.materials.new("Tex")
mat.use_nodes = True
tn = mat.node_tree.nodes.new("ShaderNodeTexImage"); tn.image = img
mat.node_tree.links.new(tn.outputs[0], mat.node_tree.nodes["Principled BSDF"].inputs[0])
dark = bpy.data.materials.new("Dark"); dark.diffuse_color = (0.035, 0.035, 0.04, 1)

data = open(os.path.join(proj, "Content", "Knight", "KnightParts.bin"), "rb").read()
ver, num = struct.unpack_from("<ii", data, 4)
pos = 12
for _ in range(num):
    pv, sl, nv, ni = struct.unpack_from("<iiii", data, pos); pos += 16
    raw = [struct.unpack_from("<12f", data, pos + 48 * i) for i in range(nv)]; pos += 48 * nv
    idx = struct.unpack_from("<%di" % ni, data, pos); pos += 4 * ni
    M = joint_matrix(pv)
    me = bpy.data.meshes.new("p%d" % pv)
    me.from_pydata([to_view(M @ Vector(r[:3])) for r in raw], [], [idx[i:i + 3] for i in range(0, ni, 3)])
    uv = me.uv_layers.new()
    for poly in me.polygons:
        for li in poly.loop_indices:
            r = raw[me.loops[li].vertex_index]
            uv.data[li].uv = (r[6], 1.0 - r[7])
    me.materials.append(mat)
    o = bpy.data.objects.new("p%d" % pv, me); bpy.context.scene.collection.objects.link(o)

# the dark body inside (KnightModel::AddCores)
def rod(a, b, r, j):
    a, b = Vector(a), Vector(b)
    M = joint_matrix(j)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(radius=r / 100, depth=d.length / 100)
    o = bpy.context.active_object
    o.matrix_world = Matrix.Translation(to_view(M @ ((a + b) / 2))) @ (M.to_3x3() @ Matrix.Identity(3)).to_4x4().inverted().inverted() @ Matrix.Identity(4)
    # orient: align local Z to the rod direction in view space
    dv = (to_view(M @ b) - to_view(M @ a)).normalized()
    o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(dv)
    o.location = to_view(M @ ((a + b) / 2))
    o.data.materials.append(dark)
def ball(c, rx, ry, rz, j):
    M = joint_matrix(j)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1, location=to_view(M @ Vector(c)))
    o = bpy.context.active_object; o.scale = (ry / 100, rx / 100, rz / 100); o.data.materials.append(dark)
ball((-3, 0, 2), 16, 15.5, 19, 1); ball((-1, 0, -26), 14, 15, 11, 0); rod((-1, 0, 32), (-1, 0, 16), 7, 2)
for sd in (-1, 1):
    A, E, H, K, N = (6, 8, 10, 12, 14) if sd < 0 else (7, 9, 11, 13, 15)
    rod(PIV[A], PIV[E], 4.5, A); ball(Vector(PIV[A]) + Vector((0, -sd * 2, 0)), 9, 8, 9, 1); ball(PIV[A], 8, 8, 8, A)
    rod(PIV[E], WRIST[E], 4.8, E); rod(PIV[H], PIV[K], 7, H); rod(PIV[K], PIV[N], 6, K)
    ball(Vector(PIV[N]) + Vector((2, 0, -4)), 9, 5.5, 5.5, N)

sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'TEXTURE'
sc.render.resolution_x = 700; sc.render.resolution_y = 1000
cd = bpy.data.cameras.new("C"); cd.type = 'ORTHO'; cd.ortho_scale = 1.8; cd.sensor_fit = 'VERTICAL'
cam = bpy.data.objects.new("C", cd); sc.collection.objects.link(cam); sc.camera = cam
# game front (+x) is view -y
views = [("front", (0, -5, -0.15), (90, 0, 0)), ("side", (5, 0, -0.15), (90, 0, 90)), ("back", (0, 5, -0.15), (90, 0, 180)),
         ("q34", (3.5, -3.5, 0.3), (80, 0, 45))]
for name, loc, rot in views:
    cam.location = loc; cam.rotation_euler = tuple(math.radians(a) for a in rot)
    sc.render.filepath = os.path.join(out, "rev_%s_%s.png" % (pose, name)); bpy.ops.render.render(write_still=True)
# close-ups of the legs from the front and side
cd.ortho_scale = 0.75
for name, loc, rot in [("knees", (0, -5, -0.62), (90, 0, 0)), ("kneeside", (5, 0, -0.62), (90, 0, 90))]:
    cam.location = loc; cam.rotation_euler = tuple(math.radians(a) for a in rot)
    sc.render.filepath = os.path.join(out, "rev_%s_%s.png" % (pose, name)); bpy.ops.render.render(write_still=True)
