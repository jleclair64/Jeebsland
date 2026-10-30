# Blender (background) step 2 for the Meshy knight: take the cut-down model (knight_dec.blend from knight_blender_prep.py),
# move it into the game's knight space (cm; feet at Z = -96, forward +X, right +Y), cut it into the pieces that hang off
# the knight's joints (KnightDesign::PivotPos) and write them to Content/Knight/KnightParts.bin for the game to load.
# It also renders front and side pictures with each piece in its own colour, to check the cuts.
# Run: blender -b -P Tools/knight_blender_export.py -- <project dir> <dir holding knight_dec.blend>
#
# KnightParts.bin (little endian): 'JKNT', int32 version (2), int32 part count; per part: int32 pivot, int32 slot,
# int32 vertex count, int32 index count, then per vertex 12 floats (position xyz, normal xyz, uv, tangent xyz, tangent-y sign:
# -1 means Unreal must flip its tangent Y), then the indices.
import bpy, sys, os, struct, math
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
proj, work = argv[0], argv[1]
bpy.ops.wm.open_mainfile(filepath=os.path.join(work, "knight_dec.blend"))
obj = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
me = obj.data

# ---- Into knight space. The model faces -Y with Z up; its right hand is at -X. ----
HEIGHT, FEET = 162.5, -96.0
zs = [v.co.z for v in me.vertices]
zmin, zmax = min(zs), max(zs)
S = HEIGHT / (zmax - zmin)
def to_game(p):  return Vector((-p.y * S, -p.x * S, (p.z - zmin) * S + FEET))
def nrm_game(n): return Vector((-n.y, -n.x, n.z)).normalized()

# ---- The joints (must match KnightDesign::PivotPos) ----
P_BODY, P_UPPER, P_HEAD, P_ARML, P_ARMR, P_ELBL, P_ELBR, P_HIPL, P_HIPR, P_KNEEL, P_KNEER, P_ANKL, P_ANKR = 0, 1, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
S_HELM, S_BODY, S_SHOULDERS, S_BOOTS, S_GLOVES = 0, 1, 2, 3, 5
HIP  = lambda sd: Vector((0.0, sd * 13.0, -26.0))
KNEE = lambda sd: Vector((2.0, sd * 14.0, -51.0))

def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length

def classify(c):
    sd = 1 if c.y > 0 else -1
    ay = abs(c.y)
    # the helmet sits above a sloping cut: low under the chin, higher at the back of the neck, so the tall collar stays
    # with the body
    if c.z > 27.5 - 0.12 * c.x and ay < 19.0:
        return (P_HEAD, S_HELM)
    # the arms, pauldrons included (they turn with the arm about the shoulder)
    is_arm = (5.0 <= c.z <= 34.0 and ay > 19.0) or (-20.0 <= c.z < 5.0 and ay > 21.5) or (-48.0 < c.z < -20.0 and ay > 27.5)
    if is_arm:
        if c.z > -3.5:
            return (P_ARMR if sd > 0 else P_ARML, S_SHOULDERS)
        return (P_ELBR if sd > 0 else P_ELBL, S_GLOVES)
    if c.z >= -20.0:
        return (P_UPPER, S_BODY)
    if c.z > -45.0:
        # inside the fauld: the thigh if close to its line, otherwise the plates and skirt hanging from the belt
        if c.z > -43.0 and seg_dist(c, HIP(sd), KNEE(sd)) > 11.0:
            return (P_UPPER, S_BODY)
        return (P_HIPR if sd > 0 else P_HIPL, S_BODY)
    if c.z > -82.0:
        return (P_KNEER if sd > 0 else P_KNEEL, S_BOOTS)
    return (P_ANKR if sd > 0 else P_ANKL, S_BOOTS)

# ---- Crisp plate edges: faces meeting at more than 40 degrees get their own normals ----
me.set_sharp_from_angle(angle=math.radians(40.0))

# ---- Gather triangles per piece ----
me.calc_loop_triangles()
me.calc_tangents(uvmap=me.uv_layers.active.name)
uvl = me.uv_layers.active.data
cn = me.corner_normals
parts = {}
gpos = [to_game(v.co) for v in me.vertices]
for lt in me.loop_triangles:
    c = (gpos[lt.vertices[0]] + gpos[lt.vertices[1]] + gpos[lt.vertices[2]]) / 3.0
    key = classify(c)
    P = parts.setdefault(key, {"v": [], "map": {}, "i": []})
    tri = []
    for li in lt.loops:
        vi = me.loops[li].vertex_index
        p = gpos[vi]
        n = nrm_game(cn[li].vector)
        uv = uvl[li].uv
        lp = me.loops[li]
        tg = nrm_game(lp.tangent)
        # the model is mirrored on its way into Unreal's axes, so the texture's up direction needs Unreal's tangent Y
        # flipped exactly when Blender's bitangent sign is +1 (the normal map stays in its own OpenGL form)
        fy = -1.0 if lp.bitangent_sign > 0 else 1.0
        k = (vi, round(uv.x, 5), round(uv.y, 5), round(n.x, 3), round(n.y, 3), round(n.z, 3), round(tg.x, 2), round(tg.y, 2), round(tg.z, 2), fy)
        idx = P["map"].get(k)
        if idx is None:
            idx = len(P["v"])
            P["map"][k] = idx
            P["v"].append((p.x, p.y, p.z, n.x, n.y, n.z, uv.x, 1.0 - uv.y, tg.x, tg.y, tg.z, fy))   # V flipped back to the texture's own (top-left) origin
        tri.append(idx)
    P["i"].extend(tri)

out_dir = os.path.join(proj, "Content", "Knight")
os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "KnightParts.bin"), "wb") as f:
    f.write(b"JKNT")
    f.write(struct.pack("<ii", 2, len(parts)))
    for (pivot, slot), P in sorted(parts.items()):
        f.write(struct.pack("<iiii", pivot, slot, len(P["v"]), len(P["i"])))
        for v in P["v"]:
            f.write(struct.pack("<12f", *v))
        f.write(struct.pack("<%di" % len(P["i"]), *P["i"]))
        print("PART pivot %2d slot %d: %6d verts %6d tris" % (pivot, slot, len(P["v"]), len(P["i"]) // 3))

# ---- Check picture: each piece in its own colour ----
cols = {}
palette = [(0.9, 0.2, 0.2), (0.2, 0.8, 0.2), (0.2, 0.4, 0.95), (0.95, 0.85, 0.2), (0.85, 0.3, 0.9), (0.2, 0.9, 0.9), (1.0, 0.55, 0.1), (0.6, 0.6, 0.6)]
attr = me.color_attributes.new("PartCol", 'FLOAT_COLOR', 'CORNER')
keys = sorted(parts.keys())
for lt in me.loop_triangles:
    c = (gpos[lt.vertices[0]] + gpos[lt.vertices[1]] + gpos[lt.vertices[2]]) / 3.0
    col = palette[keys.index(classify(c)) % len(palette)]
    for li in lt.loops:
        attr.data[li].color = (*col, 1.0)
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'VERTEX'
scene.render.resolution_x = 800
scene.render.resolution_y = 1000
cam_data = bpy.data.cameras.new("Cam"); cam_data.type = 'ORTHO'; cam_data.ortho_scale = 2.0; cam_data.sensor_fit = 'VERTICAL'
cam = bpy.data.objects.new("Cam", cam_data); scene.collection.objects.link(cam); scene.camera = cam
for name, loc, rot in [("front", (0, -5, 0), (math.radians(90), 0, 0)), ("side", (5, 0, 0), (math.radians(90), 0, math.radians(90))), ("back", (0, 5, 0), (math.radians(90), 0, math.radians(180)))]:
    cam.location = loc; cam.rotation_euler = rot
    scene.render.filepath = os.path.join(work, "parts_%s.png" % name)
    bpy.ops.render.render(write_still=True)
