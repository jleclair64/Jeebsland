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
KNEE = lambda sd: Vector((-1.0, sd * 17.5, -51.0))   # (where the model's knee is before the legs are straightened)
ANKLE_Z = -82.0

# The model stands with its legs angled out (knees 4.5 cm, ankles 8 cm wider than the hips) and its toes turned out: that
# read as a waddle once the legs swing. The leg pieces are slid in so each leg hangs straight under its hip, and each foot
# is turned to point straight ahead about its ankle.
LEG_Y = 13.0
def leg_out(z):   # how far the model's leg centre stands outside the hip line at height z
    if z >= -26.0: return 0.0
    if z >= -51.0: return 4.5 * (-26.0 - z) / 25.0
    if z >= ANKLE_Z: return 4.5 + 3.5 * (-51.0 - z) / 31.0
    return 8.0
LEG_PIVOTS = (P_HIPL, P_HIPR, P_KNEEL, P_KNEER, P_ANKL, P_ANKR)

def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length

def classify(c, metal=1.0):
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
    if c.z > -44.0:
        # under the belt: the steel plates hanging from it (front plate, tassets, back plates) stay with the body; the dark
        # cloth of the trousers goes with the thigh. The model's own metal map tells them apart (by position alone the
        # two were mixed up and the trousers tore open above the knees).
        if metal > 0.5 and seg_dist(c, HIP(sd), KNEE(sd)) > 8.0:
            return (P_UPPER, S_BODY)
        return (P_HIPR if sd > 0 else P_HIPL, S_BODY)
    if c.z > -78.0:   # (the cut sits on the seam where the greave meets the boot cuff, which turns with the foot)
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

# each foot's toe-out: the long axis of the sole (principal direction of the lowest points), measured per side
foot_yaw = {}
for sd in (-1, 1):
    pts = [p for p in gpos if p.z < -90.0 and p.y * sd > 4.0]
    mx = sum(p.x for p in pts) / len(pts); my = sum(p.y for p in pts) / len(pts)
    sxx = sum((p.x - mx) ** 2 for p in pts); syy = sum((p.y - my) ** 2 for p in pts); sxy = sum((p.x - mx) * (p.y - my) for p in pts)
    ang = 0.5 * math.atan2(2.0 * sxy, sxx - syy)   # angle of the long axis from +X
    foot_yaw[sd] = ang
    print("FOOT side %d toe-out %.1f degrees" % (sd, math.degrees(ang)))

def rot_z(v, a):
    c, s = math.cos(a), math.sin(a)
    return Vector((v.x * c - v.y * s, v.x * s + v.y * c, v.z))

FIST_LEAN = math.radians(25.0)   # the fists tip 25 degrees about the wrist, thumb end down, so a sword gripped through them (front to back) carries forward and down
def rot_y(v, a):
    c, s = math.cos(a), math.sin(a)
    return Vector((v.x * c + v.z * s, v.y, -v.x * s + v.z * c))
def fist_angle(p):   # full lean below the cuff, easing in over 4 cm so the glove does not tear from the vambrace
    return FIST_LEAN * max(0.0, min(1.0, (-27.0 - p.z) / 4.0))

def place(p, key):
    pv = key[0]
    if pv in (P_ELBL, P_ELBR):
        sd = 1 if pv == P_ELBR else -1
        w = Vector((-2.0, sd * 37.0, -29.0))   # the wrist (KnightDesign::WristPos)
        return w + rot_y(p - w, fist_angle(p))
    if pv == P_UPPER and p.z < -26.0:
        # the plates and skirt round the thighs follow the straightened legs (easing to nothing at the middle)
        sd = 1 if p.y > 0 else -1
        return Vector((p.x, p.y - sd * leg_out(p.z) * min(1.0, abs(p.y) / 8.0), p.z))
    if pv not in LEG_PIVOTS: return p
    sd = 1 if pv in (P_HIPR, P_KNEER, P_ANKR) else -1
    q = Vector((p.x, p.y - sd * leg_out(p.z), p.z))
    if pv in (P_ANKL, P_ANKR):   # turn the foot about its ankle so the toes point ahead
        a = Vector((-4.0, sd * LEG_Y, ANKLE_Z))
        q = a + rot_z(q - a, -foot_yaw[sd])
    return q

def turn(v, p, key):
    if key[0] in (P_ANKL, P_ANKR):
        sd = 1 if key[0] == P_ANKR else -1
        return rot_z(v, -foot_yaw[sd])
    if key[0] in (P_ELBL, P_ELBR):
        return rot_y(v, fist_angle(p))
    return v

# ---- Which piece each triangle belongs to; then tidy up: small scraps cut off from the rest of their piece (they float
# when that joint moves) join the piece around them ----
tris = list(me.loop_triangles)
# how metal each triangle is, from the model's metal/roughness picture (blue = metal)
import numpy as np
mr_img = bpy.data.images.load(os.path.join(proj, "Art", "Knight", "T_Knight_MR.jpg"))
mw, mh = mr_img.size
mr_px = np.empty(mw * mh * 4, dtype=np.float32); mr_img.pixels.foreach_get(mr_px); mr_px = mr_px.reshape(mh, mw, 4)
def metal_at(uv):
    x = min(mw - 1, max(0, int(uv.x * mw))); y = min(mh - 1, max(0, int(uv.y * mh)))
    return float(mr_px[y, x, 2])
tri_metal = [sum(metal_at(uvl[li].uv) for li in lt.loops) / 3.0 for lt in tris]
keys = [classify((gpos[lt.vertices[0]] + gpos[lt.vertices[1]] + gpos[lt.vertices[2]]) / 3.0, tri_metal[ti]) for ti, lt in enumerate(tris)]
vert_tris = [[] for _ in me.vertices]
for ti, lt in enumerate(tris):
    for vi in lt.vertices: vert_tris[vi].append(ti)
# Smooth the cut lines: a triangle whose edge neighbours mostly belong to another piece joins it (no saw-tooth edges)
edge_tris = {}
for ti, lt in enumerate(tris):
    v = lt.vertices
    for a, b in ((v[0], v[1]), (v[1], v[2]), (v[2], v[0])):
        edge_tris.setdefault((min(a, b), max(a, b)), []).append(ti)
nbrs = [[] for _ in tris]
for ts in edge_tris.values():
    for a in ts:
        for b in ts:
            if a != b: nbrs[a].append(b)
for _pass in range(4):
    changed = 0
    new = list(keys)
    for ti in range(len(tris)):
        votes = {}
        for tj in nbrs[ti]: votes[keys[tj]] = votes.get(keys[tj], 0) + 1
        if votes:
            best = max(votes, key=votes.get)
            if best != keys[ti] and votes[best] >= 2: new[ti] = best; changed += 1
    keys = new
    print("SMOOTH pass %d: %d triangles changed" % (_pass, changed))
for _pass in range(3):
    seen = [False] * len(tris)
    moved = 0
    for start in range(len(tris)):
        if seen[start]: continue
        k = keys[start]; comp = [start]; seen[start] = True; i = 0
        while i < len(comp):
            for vi in tris[comp[i]].vertices:
                for tj in vert_tris[vi]:
                    if not seen[tj] and keys[tj] == k: seen[tj] = True; comp.append(tj)
            i += 1
        if len(comp) >= 80: continue
        votes = {}
        cs = set(comp)
        for ti in comp:
            for vi in tris[ti].vertices:
                for tj in vert_tris[vi]:
                    if tj not in cs: votes[keys[tj]] = votes.get(keys[tj], 0) + 1
        if votes:
            best = max(votes, key=votes.get)
            for ti in comp: keys[ti] = best
            moved += len(comp)
    print("SCRAPS pass %d: %d triangles moved" % (_pass, moved))

for ti, lt in enumerate(tris):
    key = keys[ti]
    P = parts.setdefault(key, {"v": [], "map": {}, "i": []})
    tri = []
    for li in lt.loops:
        vi = me.loops[li].vertex_index
        p = place(gpos[vi], key)
        n = turn(nrm_game(cn[li].vector), gpos[vi], key)
        uv = uvl[li].uv
        lp = me.loops[li]
        tg = turn(nrm_game(lp.tangent), gpos[vi], key)
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
final_keys = keys
for ti, lt in enumerate(tris):
    col = palette[sorted(parts.keys()).index(final_keys[ti]) % len(palette)]
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
