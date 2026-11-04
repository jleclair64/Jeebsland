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
# (2026-11-03) Armour sets: each is its own Meshy model cut the same way into the five slots and written as its own skinned
# mesh on the SAME skeleton (SK_Knight_<Set>.fbx; Iron keeps SK_Knight.fbx and alone writes KnightParts.bin). Run with
# -- <project dir> <work dir> <set> <metal/roughness picture>. Mail is the chain mail worn under every set: it is shown
# whole, always, pulled MAIL_INSET cm inside along its normals so the plate sits over it (it fills any gap where pieces of
# two different sets meet). Tin is the crusader tunic: its long skirt stays with the chest piece, hanging from the hips.
SET = argv[2] if len(argv) > 2 else "Heavy"
MR_PATH = argv[3] if len(argv) > 3 else os.path.join(proj, "Art", "Knight", "T_Knight_MR.jpg")
TAG = "" if SET == "Heavy" else "_" + SET
MAIL_INSET = 2.0
MAIL_LEG_EXTRA = 2.0
OVERLAP_RINGS = 2   # each piece reaches this many triangles into its neighbours, so two sets meeting at a cut overlap
bpy.ops.wm.open_mainfile(filepath=os.path.join(work, "knight_dec%s.blend" % TAG))
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
P_WRISTR = 16   # (2026-10-31) the sword hand: the right fist hangs off its own wrist joint, so animation can turn it
WRIST_CUT_Z = -28.5   # the cut, just under the rim of the gauntlet cuff (which hides the seam when the wrist turns)
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

SKIRT_GAP = 11.0   # the tunic: cloth further than this from the thigh's axis (hip to knee) is the skirt, not the trousers
SKIRT_BOTTOM = -56.0   # (just under the hem: lower, bits of the leg mail above the knee were taken for skirt)
def is_skirt(c):
    if c.z >= -20.0 or c.z < SKIRT_BOTTOM: return False
    sd = 1 if c.y > 0 else -1
    knee = KNEE(sd) + Vector((0.0, 0.0, -12.0))   # (the axis runs on a little past the knee, under the hem)
    return seg_dist(c, HIP(sd), knee) > SKIRT_GAP

# where each set's helmet ends (game cm at the middle of the neck; owner 2026-11-03: the new sets' helmets took in their
# collars and gorgets, which belong to the chest piece). Measured on Saved/KnightBuild height-line pictures. The mail's hood
# ends high too, so its neck stays with its body: hidden under a helmet, the hood would otherwise leave the neck open.
HEAD_CUT = {"Heavy": (31.7, 0.32), "Classic": (35.5, 0.12), "Light": (36.5, 0.12), "Mail": (37.0, 0.12)}   # (height, slope: lower at the chin)
# owner 2026-11-03: the elbow armour belongs to the gloves (the shoulder piece ends above the elbow cop), and the plates that
# hang from the belt are body armour all the way down (their low ends went to the boots and showed on bare legs)
ELBOW_CUT_Z, HANG_LOW = 3.0, -50.0
def classify(c, metal=1.0):
    sd = 1 if c.y > 0 else -1
    ay = abs(c.y)
    # the helmet sits above a sloping cut: low under the chin, higher at the back of the neck, so the tall collar stays
    # with the body
    hc, hs = HEAD_CUT.get(SET, (27.5, 0.12))
    if c.z > hc - hs * c.x and ay < 19.0:
        return (P_HEAD, S_HELM)
    # the arms, pauldrons included (they turn with the arm about the shoulder)
    is_arm = (5.0 <= c.z <= 34.0 and ay > 19.0) or (-20.0 <= c.z < 5.0 and ay > 21.5) or (-48.0 < c.z < -20.0 and ay > 27.5)
    if is_arm:
        if c.z > ELBOW_CUT_Z:
            return (P_ARMR if sd > 0 else P_ARML, S_SHOULDERS)
        if c.z > -3.5:   # (the elbow cop: in the gloves, but turning with the upper arm like before)
            return (P_ARMR if sd > 0 else P_ARML, S_GLOVES)
        if sd > 0 and c.z < WRIST_CUT_Z: return (P_WRISTR, S_GLOVES)
        return (P_ELBR if sd > 0 else P_ELBL, S_GLOVES)
    if c.z >= -20.0:
        return (P_UPPER, S_BODY)
    if SET == "Light" and is_skirt(c):
        return (P_BODY, S_BODY)
    if c.z > -44.0:
        # under the belt: the steel plates hanging from it (front plate, tassets, back plates) stay with the body; the dark
        # cloth of the trousers goes with the thigh. The model's own metal map tells them apart (by position alone the
        # two were mixed up and the trousers tore open above the knees).
        if metal > 0.5 and seg_dist(c, HIP(sd), KNEE(sd)) > 8.0:
            return (P_UPPER, S_BODY)
        return (P_HIPR if sd > 0 else P_HIPL, S_BODY)
    if SET != "Mail" and c.z > HANG_LOW and metal > 0.5 and seg_dist(c, HIP(sd), KNEE(sd)) > 9.0 and (c - KNEE(sd)).length > 14.0:
        return (P_UPPER, S_BODY)   # (the low ends of plates hanging from the belt: body armour, not boots)
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

# ---- The arm fit (2026-11-03). The skeleton, the sword grip and every pose were made on the Heavy model, whose long
# gauntlets put the fists about 13 cm lower than on the Classic, Light and Mail models (their elbows are at the same height).
# Each of those is fitted: the hand is moved whole onto the Heavy model's fist and the forearm stretches from the elbow to
# meet it, so a held sword sits in the glove and a piece of one set lines up with its neighbours from another.
# FIST: the centre of the lowest 9 cm of arm surface well outside the body (|y| > 30) below the elbow.
HEAVY_FIST = Vector((0.2, 37.8, -37.8))   # (measured by this same routine on the Heavy model; the left fist is its mirror)
ELBOW_Z, ARM_Y = -10.0, 21.0
def fist_of(sd):
    pts = [p for p in gpos if p.y * sd > 30.0 and -50.0 < p.z < -12.0]
    if not pts: return None
    bot = min(p.z for p in pts)
    low = [p for p in pts if p.z < bot + 9.0]
    return sum(low, Vector()) / len(low)
for sd in (1, -1):
    f = fist_of(sd)
    print("FIST side %d at %.1f %.1f %.1f" % (sd, f.x, f.y, f.z) if f else "FIST side %d not found" % sd)
    if SET == "Heavy" or f is None: continue
    target = Vector((HEAVY_FIST.x, sd * HEAVY_FIST.y, HEAVY_FIST.z))
    move = target - f
    # the forearm and hand: everything joined to the fist through the mesh's edges, staying below the elbow and outside the body
    near = lambda i: gpos[i].z < ELBOW_Z and gpos[i].y * sd > ARM_Y
    seed = min((i for i in range(len(gpos)) if near(i)), key=lambda i: (gpos[i] - f).length)
    nb = [[] for _ in gpos]
    for e in me.edges:
        a, b = e.vertices
        nb[a].append(b); nb[b].append(a)
    arm, todo = {seed}, [seed]
    while todo:
        for j in nb[todo.pop()]:
            if j not in arm and near(j): arm.add(j); todo.append(j)
    wrist_z = f.z + 6.0   # (the top of the hand: from here down it moves whole)
    for i in arm:
        p = gpos[i]
        t = max(0.0, min(1.0, (ELBOW_Z - p.z) / (ELBOW_Z - wrist_z))); t = t * t * (3.0 - 2.0 * t)
        gpos[i] = p + move * t
    print("ARMFIT side %d: %d vertices, hand moved %.1f %.1f %.1f" % (sd, len(arm), move.x, move.y, move.z))

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
    if pv in (P_ELBL, P_ELBR, P_WRISTR):
        sd = -1 if pv == P_ELBL else 1
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
    if key[0] in (P_ELBL, P_ELBR, P_WRISTR):
        return rot_y(v, fist_angle(p))
    return v

# ---- Which piece each triangle belongs to; then tidy up: small scraps cut off from the rest of their piece (they float
# when that joint moves) join the piece around them ----
class _Tri:   # (plain copies: Blender's own triangle list goes stale once the check picture adds a colour layer, and reading it then crashes)
    __slots__ = ("vertices", "loops")
    def __init__(self, lt): self.vertices = tuple(lt.vertices); self.loops = tuple(lt.loops)
tris = [_Tri(lt) for lt in me.loop_triangles]
# how metal each triangle is, from the model's metal/roughness picture (blue = metal)
import numpy as np
mr_img = bpy.data.images.load(MR_PATH)
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

# The tunic (Light) has no shoulder piece (owner, 2026-11-03: a cut shoulder never fitted): everything on its arms above
# the gloves belongs to the chest piece, so the whole tunic comes on and off with the chest. (It still bends with the arm bones.)
if SET == "Light":
    keys = [(k[0], S_BODY) if k[1] == S_SHOULDERS else k for k in keys]
    print("TUNIC shoulders joined to the chest piece")

# The overlap (skinned sets only): triangles within OVERLAP_RINGS of another slot are also drawn in that slot, so where
# a piece of one set meets a piece of another, each reaches a little under the other instead of leaving a crack
extra = []   # (triangle, slot) pairs drawn a second time
if SET != "Mail":
    ring = {ti: set() for ti in range(len(tris))}
    for ti in range(len(tris)):
        for tj in nbrs[ti]:
            if keys[tj][1] != keys[ti][1]: ring[ti].add(keys[tj][1])
    for _r in range(OVERLAP_RINGS - 1):
        grow = {ti: set(s) for ti, s in ring.items()}
        for ti in range(len(tris)):
            for tj in nbrs[ti]: grow[ti] |= {s for s in ring[tj] if s != keys[ti][1]}
        ring = grow
    extra = [(ti, s) for ti, ss in ring.items() for s in ss]
    print("OVERLAP %d triangles drawn twice" % len(extra))

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
with open(os.path.join(out_dir, "KnightParts.bin") if SET == "Heavy" else os.path.join(work, "unused_parts%s.bin" % TAG), "wb") as f:   # (the rigid-piece fallback is the Heavy set's)
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
    scene.render.filepath = os.path.join(work, "parts%s_%s.png" % (TAG, name))
    bpy.ops.render.render(write_still=True)

# ---- The skinned knight (2026-11-01): the same model as ONE mesh on a skeleton, written to Saved\KnightBuild\SK_Knight.fbx
# for knight_unreal_skin.py. The bones sit on the knight's joints (KnightDesign::PivotPos, named by KnightModel::BoneName);
# the game copies its joint pivots onto them every frame, so every pose and move carries over. Each vertex starts weighted to
# the piece its triangles were cut into above, then the weights are blended a few centimetres across every cut, so a bending
# elbow or knee stretches smoothly instead of opening a gap between rigid pieces.
SKIN_SMOOTH = 6   # blending passes across each cut (about 1.5 cm each)
BONES = {P_BODY: "Body", P_UPPER: "Upper", P_HEAD: "Head", P_ARML: "ArmL", P_ARMR: "ArmR", P_ELBL: "ElbowL", P_ELBR: "ElbowR",
         P_WRISTR: "WristR", P_HIPL: "HipL", P_HIPR: "HipR", P_KNEEL: "KneeL", P_KNEER: "KneeR", P_ANKL: "AnkleL", P_ANKR: "AnkleR"}
PARENT = {"Upper": "Body", "Head": "Upper", "ArmL": "Upper", "ArmR": "Upper", "ElbowL": "ArmL", "ElbowR": "ArmR", "WristR": "ElbowR",
          "HipL": "Body", "HipR": "Body", "KneeL": "HipL", "KneeR": "HipR", "AnkleL": "KneeL", "AnkleR": "KneeR"}
PIVOT_POS = {P_BODY: (0, 0, 0), P_UPPER: (0, 0, -41), P_HEAD: (0, 0, 23), P_ARML: (-3, -29, 14), P_ARMR: (-3, 29, 14),
             P_ELBL: (-5, -34.5, -6), P_ELBR: (-5, 34.5, -6), P_WRISTR: (-2, 37, -29), P_HIPL: (0, -13, -26), P_HIPR: (0, 13, -26),
             P_KNEEL: (-1, -13, -51), P_KNEER: (-1, 13, -51), P_ANKL: (-4, -13, -82), P_ANKR: (-4, 13, -82)}
SLOT_MAT = {S_HELM: "Helm", S_BODY: "Body", S_SHOULDERS: "Shoulders", S_BOOTS: "Boots", S_GLOVES: "Gloves"}
FOOT_SCALE = 0.7   # KnightDesign::FootScale: the feet are drawn 30% narrower, easing in over 8 cm under the ankle

nv = len(me.vertices)
acc = [Vector((0.0, 0.0, 0.0)) for _ in range(nv)]
cnt = [0] * nv
W = [dict() for _ in range(nv)]
for ti, lt in enumerate(tris):
    k = keys[ti]
    for vi in lt.vertices:
        acc[vi] += place(gpos[vi], k)   # (where a vertex sits on two pieces, between the two placings: the seam closes)
        cnt[vi] += 1
        W[vi][k[0]] = W[vi].get(k[0], 0.0) + 1.0
pos = [acc[i] / cnt[i] if cnt[i] else gpos[i] for i in range(nv)]
W = [{p: c / sum(d.values()) for p, c in d.items()} if d else {P_BODY: 1.0} for d in W]
adj = [[] for _ in range(nv)]
for e in me.edges:
    a, b = e.vertices
    adj[a].append(b); adj[b].append(a)
for _pass in range(SKIN_SMOOTH):
    new = []
    for i in range(nv):
        if not adj[i]: new.append(W[i]); continue
        m = {}
        for j in adj[i]:
            for p, w in W[j].items(): m[p] = m.get(p, 0.0) + w
        n = float(len(adj[i]))
        new.append({p: 0.5 * W[i].get(p, 0.0) + 0.5 * m.get(p, 0.0) / n for p in set(W[i]) | set(m)})
    W = new
for i in range(nv):   # at most four bones per vertex, none under 3%
    top = sorted(W[i].items(), key=lambda kv: -kv[1])[:4]
    top = [(p, w) for p, w in top if w >= 0.03] or top[:1]
    s = sum(w for _, w in top)
    W[i] = {p: w / s for p, w in top}
    # the narrower feet, by how much the vertex follows an ankle
    for ap in (P_ANKL, P_ANKR):
        wa = W[i].get(ap, 0.0)
        if wa > 0.0:
            az = PIVOT_POS[ap][2]
            t = max(0.0, min(1.0, (az + 2.0 - pos[i].z) / 8.0)); t = t * t * (3.0 - 2.0 * t)
            sc = 1.0 + (FOOT_SCALE - 1.0) * t * wa
            pos[i] = Vector((pos[i].x, PIVOT_POS[ap][1] + (pos[i].y - PIVOT_POS[ap][1]) * sc, pos[i].z))

# The tunic's skirt hangs from the hips: down its length it follows the thigh on its own side more and more (half way at
# the hem, the front and back centre sharing both thighs), so a stride or a kick swings it without tearing it from the belt
if SET == "Light":
    skirt_v = set()
    for ti, lt in enumerate(tris):
        if keys[ti] == (P_BODY, S_BODY): skirt_v.update(lt.vertices)
    for i in skirt_v:
        z = pos[i].z
        t = max(0.0, min(1.0, (-22.0 - z) / (-22.0 - SKIRT_BOTTOM))); t = t * t * (3.0 - 2.0 * t)
        right = max(0.0, min(1.0, 0.5 + pos[i].y / 14.0))
        follow = 0.55 * t
        W[i] = {p: w for p, w in {P_BODY: 1.0 - follow, P_HIPR: follow * right, P_HIPL: follow * (1.0 - right)}.items() if w >= 0.03}
        s = sum(W[i].values()); W[i] = {p: w / s for p, w in W[i].items()}
    print("SKIRT %d vertices hang from the hips" % len(skirt_v))

# ---- The size fit (2026-11-03, owner: the new sets looked smaller than the original, the helmet most of all and the
# shoulders thinner). Around each joint, how far the armour reaches (95th percentile of each axis, vertices that mostly follow
# that bone) is measured; the Heavy set writes its numbers to size_heavy.json, and every other set (not the mail) is scaled
# per bone and per axis to reach as far, blended by the skin weights so the pieces stay joined. Limbs and the body grow
# only in girth (their length stays on the skeleton); the helmet and pauldrons grow in all three directions.
import json
SIZE_FILE = os.path.join(work, "size_heavy.json")
# What is measured, per bone: (centre, which vertices, axes compared, uniform?). The helmet is measured above the neck only
# (a mail hood under it must not count) and grows evenly, so its shape stays; the chest in a band round the ribs; the
# pauldrons evenly; limbs and feet only in girth.
SIZE_MIN, SIZE_MAX = 0.92, 1.3
# set by eye against the Heavy model in the side-by-side picture (knight_blender_compare.py), where the measure falls short
SIZE_SET = {}
# sets remodelled in Meshy to the Heavy knight's build (2026-11-03): only measured (the SIZE lines), never reshaped
MEASURE_ONLY = {"Classic", "Light"}
def size_rule(b):
    c = Vector(PIVOT_POS[b])
    if b == P_HEAD: return (c + Vector((0.0, 0.0, 6.0)), lambda p: p.z > c.z + 6.0, (0, 1), True)
    if b == P_UPPER: return (Vector((c.x, 0.0, 0.0)), lambda p: -15.0 < p.z < 22.0, (0, 1), False)
    if b == P_BODY: return (Vector((c.x, 0.0, c.z)), lambda p: True, (0, 1), False)
    if b in (P_ARML, P_ARMR): return (c, lambda p: True, (0, 1, 2), True)
    return (c, lambda p: True, (0, 1), False)
def reach(b):
    c, keep, axes, _ = size_rule(b)
    pts = [pos[i] - c for i in range(nv) if W[i].get(b, 0.0) > 0.6 and keep(pos[i])]
    if len(pts) < 30: return None
    out = []
    for a in range(3):
        v = sorted(abs(p[a]) for p in pts)
        out.append(v[int(0.95 * (len(v) - 1))])
    return out
if SET == "Heavy":
    json.dump({str(b): reach(b) for b in BONES}, open(SIZE_FILE, "w"))
    print("SIZE written %s" % SIZE_FILE)
elif SET != "Mail" and os.path.exists(SIZE_FILE):
    heavy = {int(k): v for k, v in json.load(open(SIZE_FILE)).items()}
    scale, centre = {}, {}
    for b in BONES:
        mine, ref = reach(b), heavy.get(b)
        if not mine or not ref: continue
        c, _, axes, even = size_rule(b)
        r = [max(SIZE_MIN, min(SIZE_MAX, ref[a] / max(mine[a], 0.5))) for a in axes]
        if even: s = SIZE_SET.get(SET, {}).get(b, sum(r) / len(r)); scale[b] = [s, s, s]
        else: scale[b] = [r[axes.index(a)] if a in axes else 1.0 for a in range(3)]
        centre[b] = c
        print("SIZE bone %-7s x%.2f y%.2f z%.2f%s" % (BONES[b], *scale[b], " (measured only)" if SET in MEASURE_ONLY else ""))
        if SET in MEASURE_ONLY: scale[b] = [1.0, 1.0, 1.0]
    for i in range(nv):
        d = Vector((0.0, 0.0, 0.0))
        for b, w in W[i].items():
            if b not in scale: continue
            r = pos[i] - centre[b]
            d += w * Vector((r.x * (scale[b][0] - 1.0), r.y * (scale[b][1] - 1.0), r.z * (scale[b][2] - 1.0)))
        pos[i] = pos[i] + d

# The chain mail sits just inside every set's plate
# The mail's legs were cartoonishly thin bare (owner, 2026-11-03): they are thickened round each leg bone. Under worn boots
# and gloves the game hides the mail's lower legs and forearms (KnightModel::Wear), so thicker mail never pokes through them.
MAIL_GIRTH = {P_HIPL: 1.3, P_HIPR: 1.3, P_KNEEL: 1.2, P_KNEER: 1.2, P_ANKL: 1.1, P_ANKR: 1.1}
if SET == "Mail":
    for i in range(nv):
        d = Vector((0.0, 0.0, 0.0))
        for b, g in MAIL_GIRTH.items():
            w = W[i].get(b, 0.0)
            if w <= 0.0: continue
            c = PIVOT_POS[b]
            d += w * Vector(((pos[i].x - c[0]) * (g - 1.0), (pos[i].y - c[1]) * (g - 1.0), 0.0))
        pos[i] = pos[i] + d - nrm_game(me.vertices[i].normal) * MAIL_INSET

def to_blend(p): return Vector((p[0] * 0.01, -p[1] * 0.01, p[2] * 0.01))   # (game cm -> Blender metres; Unreal flips Y back on import)

# ---- Smooth slot edges (2026-11-03, owner: the cut edges looked jagged, e.g. the Heavy chest's arm holes with the
# shoulders off). Each triangle belongs to one slot, so a cut followed the triangles' saw-tooth. Instead each vertex gets a
# share of every slot (how many of its triangles are in it), blurred over the mesh a few times; every slot keeps only the
# surface where its share is at least CUT_AT, and triangles crossing that line are cut along it. The line is a smooth curve,
# and with CUT_AT under a half both neighbours reach a little past it, so pieces from different sets still overlap.
# The share is measured in SPACE, not along the mesh: the slots of every triangle within CUT_RADIUS of the vertex, by area,
# nearer ones counting more. Plates here are several shells thick (outer face, inner face, rims); blurring along the mesh
# cut each shell at a different place and left torn layers, while a share in space cuts every shell along the same line.
CUT_RADIUS, CUT_AT = 7.0, 0.35
tri_c = np.array([[*((pos[t.vertices[0]] + pos[t.vertices[1]] + pos[t.vertices[2]]) / 3.0)] for t in tris])
tri_a = np.array([max(1e-4, ((pos[t.vertices[1]] - pos[t.vertices[0]]).cross(pos[t.vertices[2]] - pos[t.vertices[0]])).length * 0.5) for t in tris])
slot_list = sorted(SLOT_MAT.keys())
tri_s = np.array([slot_list.index(keys[ti][1]) for ti in range(len(tris))])
cells = {}
for ti, c in enumerate(tri_c):
    cells.setdefault(tuple((c // CUT_RADIUS).astype(int)), []).append(ti)
share = []
for i in range(nv):
    p = np.array([pos[i].x, pos[i].y, pos[i].z])
    k = (p // CUT_RADIUS).astype(int)
    cand = [ti for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1) for ti in cells.get((k[0] + dx, k[1] + dy, k[2] + dz), ())]
    if not cand: share.append({}); continue
    cand = np.array(cand)
    d2 = ((tri_c[cand] - p) ** 2).sum(axis=1)
    w = tri_a[cand] * np.clip(1.0 - d2 / (CUT_RADIUS * CUT_RADIUS), 0.0, None) ** 2
    tot = w.sum()
    if tot <= 0.0: share.append({}); continue
    sums = np.bincount(tri_s[cand], weights=w, minlength=len(slot_list))
    share.append({slot_list[j]: float(sums[j] / tot) for j in range(len(slot_list)) if sums[j] > 0.0})
for ti, t in enumerate(tris):   # (a vertex with nothing near keeps its own triangles' slot)
    for vi in t.vertices:
        if not share[vi]: share[vi] = {keys[ti][1]: 1.0}
out_pos, out_w = list(pos), list(W)
out_tris = []   # (three output vertex indices, three uvs, slot)
cut_vert = {}
def cut_point(a, b, s, ua, ub):
    k = (min(a, b), max(a, b), s)
    ga, gb = share[a].get(s, 0.0) - CUT_AT, share[b].get(s, 0.0) - CUT_AT
    t = ga / (ga - gb)
    if k not in cut_vert:
        cut_vert[k] = len(out_pos)
        out_pos.append(pos[a].lerp(pos[b], t))
        wa, wb = W[a], W[b]
        w = {p: wa.get(p, 0.0) * (1.0 - t) + wb.get(p, 0.0) * t for p in set(wa) | set(wb)}
        top = sorted(w.items(), key=lambda kv: -kv[1])[:4]
        sw = sum(x for _, x in top)
        out_w.append({p: x / sw for p, x in top if x > 0.0})
    return cut_vert[k], ua.lerp(ub, t)
dropped = cut = 0
for ti, s in [(ti, keys[ti][1]) for ti in range(len(tris))] + extra:
    vs = tris[ti].vertices
    us = [Vector(uvl[li].uv) for li in tris[ti].loops]
    inside = [share[v].get(s, 0.0) >= CUT_AT for v in vs]
    if all(inside): out_tris.append((tuple(vs), us, s)); continue
    if not any(inside): dropped += 1; continue
    cut += 1
    poly = []   # (vertex, uv) round the part of the triangle that is inside
    for c in range(3):
        a, b = vs[c], vs[(c + 1) % 3]
        if inside[c]: poly.append((a, us[c]))
        if inside[c] != inside[(c + 1) % 3]: poly.append(cut_point(a, b, s, us[c], us[(c + 1) % 3]))
    for c in range(1, len(poly) - 1):
        out_tris.append(((poly[0][0], poly[c][0], poly[c + 1][0]), [poly[0][1], poly[c][1], poly[c + 1][1]], s))
# Shards: small loose bits left at the cut line (a sliver of an inner shell or a rim) are dropped. Only bits that touch the
# line go; a small part that was always loose (a rivet, a buckle) stays.
first_cut = nv
by_slot = {}
for oi, (t, _, s) in enumerate(out_tris): by_slot.setdefault(s, []).append(oi)
shard_ids = set()
for s, ois in by_slot.items():
    vt = {}
    for oi in ois:
        for v in out_tris[oi][0]: vt.setdefault(v, []).append(oi)
    seen = set()
    for start in ois:
        if start in seen: continue
        comp, todo = [], [start]; seen.add(start)
        while todo:
            oi = todo.pop(); comp.append(oi)
            for v in out_tris[oi][0]:
                for oj in vt[v]:
                    if oj not in seen: seen.add(oj); todo.append(oj)
        if len(comp) < 40 and any(v >= first_cut for oi in comp for v in out_tris[oi][0]): shard_ids.update(comp)
out_tris = [t for oi, t in enumerate(out_tris) if oi not in shard_ids]
print("CUT smooth edges: %d triangles cut along the line, %d dropped, %d new vertices, %d shard triangles removed" % (cut, dropped, len(out_pos) - nv, len(shard_ids)))
pos, W = out_pos, out_w
nv = len(pos)

skin_me = bpy.data.meshes.new("SK_Knight")
faces = [(None, s) for _, _, s in out_tris]
skin_me.from_pydata([to_blend(p) for p in pos], [], [t for t, _, _ in out_tris])
uvs = skin_me.uv_layers.new(name="UVMap")
for fi, (_, us, _) in enumerate(out_tris):
    poly = skin_me.polygons[fi]
    for c in range(3):
        uvs.data[poly.loop_start + c].uv = (us[c].x, us[c].y)
slot_order = sorted(SLOT_MAT.keys())
for s in slot_order:
    skin_me.materials.append(bpy.data.materials.get(SLOT_MAT[s]) or bpy.data.materials.new(SLOT_MAT[s]))
for fi, (_, s) in enumerate(faces):
    skin_me.polygons[fi].material_index = slot_order.index(s)
skin_me.update()
skin_me.set_sharp_from_angle(angle=math.radians(40.0))
skin_obj = bpy.data.objects.new("SK_Knight", skin_me)
scene.collection.objects.link(skin_obj)

rig = bpy.data.armatures.new("KnightRig")
arm = bpy.data.objects.new("Armature", rig)
scene.collection.objects.link(arm)
for o in bpy.context.view_layer.objects: o.select_set(False)
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for pv, name in BONES.items():
    b = rig.edit_bones.new(name)
    b.head = to_blend(PIVOT_POS[pv])
    b.tail = b.head + Vector((0.0, 0.0, 0.06))
for name, par in PARENT.items():
    rig.edit_bones[name].parent = rig.edit_bones[par]
bpy.ops.object.mode_set(mode='OBJECT')
skin_obj.parent = arm
mod = skin_obj.modifiers.new("Armature", 'ARMATURE')
mod.object = arm
groups = {pv: skin_obj.vertex_groups.new(name=name) for pv, name in BONES.items()}
for i in range(nv):
    for p, w in W[i].items(): groups[p].add([i], w, 'REPLACE')
inf = [len(W[i]) for i in range(nv)]
print("SKIN %d verts, %d tris; vertices with 1/2/3/4 bones: %d %d %d %d" % (nv, len(tris), inf.count(1), inf.count(2), inf.count(3), inf.count(4)))

for o in bpy.context.view_layer.objects: o.select_set(False)
arm.select_set(True); skin_obj.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(work, "SK_Knight%s.fbx" % TAG)
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False, bake_anim=False,
                         apply_scale_options='FBX_SCALE_ALL', axis_forward='-Y', axis_up='Z', mesh_smooth_type='OFF', colors_type='NONE',
                         use_armature_deform_only=True, primary_bone_axis='Y', secondary_bone_axis='X')
print("SKIN written %s" % fbx)
