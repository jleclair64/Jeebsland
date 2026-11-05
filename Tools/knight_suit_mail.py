# What is left of the chain mail under the new suit (2026-11-05). Used by knight_suit_export.py inside Blender.
# Owner: chain mail never shows through the armour, as though it's covered. So everything under plate is deleted, not set deeper (it follows different
# weights and would come back through when he moves), and only two parts stay:
#  - a sleeve on each upper arm, for the bare gap between the pauldron and the bracer. The mail's own arm is lumpy, jagged and too fat for that gap, so it is reshaped into a clean
#    even tube: cut with straight edges inside both plates, centred in them, and it follows the arm bone, then the bracer's own weights at its lower end, so it can't slide out from under.
#  - a dark pelvis (the second material), so there's no hole through him under the skirt, narrow enough that the thighs' plates don't swing through it.
# Measured on the plates (scratch, 2026-11-05): numbers along the arm are 0 at the shoulder, 1 at the elbow. Directions round the left arm: 0 degrees out to his side, 90 forward.
# The pauldron's plates end between 0.5 and 0.62; the bracer's rim is slanted, 0.92 at the back of the elbow and 1.08 in its crook; both are centred about 1.7 cm off the bone, toward 135 degrees.
import bpy, bmesh, heapq, math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

SLEEVE_ROUND = 0.055     # (m) the tube's radius: the owner's pictures have a sleeve about 11 cm across, well inside the plates
SLEEVE_SHIFT, SHIFT_TURN = 0.017, 135.0   # (m, degrees) the tube's middle, off the bone: the middle of both plates
SLEEVE_TOP = 0.42        # along the arm: where it ends, 2 cm inside the pauldron's plates
RIM_ALONG, RIM_SLANT, RIM_TURN = 0.99, 0.33, 120.0   # the bracer's rim, as a plane: where it crosses the bone, how much it slants, and the way it runs down
SLEEVE_PAST_RIM = 0.08   # along the arm: how far past the rim the sleeve ends (2 cm)
TUCK_GAP, TUCK_SLOPE = 0.008, 0.5   # (m) how far behind a plate's wall it stays; how fast it eases back above that
WEIGHT_FADE = 0.035      # (m) above the rim, the sleeve fades from the arm bone's weights to the bracer's
SMOOTH_STEPS = 12        # evens out the jagged mesh before it is laid on the tube
PELVIS_X, PELVIS_Y = 0.095, 0.85   # (m, share) the dark pelvis no wider than this, near the hip joints, where a thigh plate doesn't sweep through it; and its depth
PELVIS_EDGE_STEPS = 12   # smooths its saw-toothed edge


def trim_mail(arm, body, plates, trees):
    tree = {p.name: t for p, t in zip(plates, trees)}
    Mw = arm.matrix_world
    wb = body.matrix_world; wbi = wb.inverted(); wb3t = wb.to_3x3().transposed()
    names = {g.index: g.name.replace("mixamorig:", "") for g in body.vertex_groups}

    def turned(u, w, deg):
        return u * math.cos(math.radians(deg)) + w * math.sin(math.radians(deg))
    frames = {}
    a_ = Mw @ arm.data.bones["mixamorig:LeftArm"].head_local; b_ = Mw @ arm.data.bones["mixamorig:LeftForeArm"].head_local
    axl = (b_ - a_).normalized()
    ul = axl.cross(Vector((0, 1, 0))).normalized(); wl = axl.cross(ul).normalized()   # (0 degrees: out; 90: forward)
    mirror = lambda d: Vector((-d.x, d.y, d.z))
    for L, m in (("Left", lambda d: d), ("Right", mirror)):
        a_ = Mw @ arm.data.bones["mixamorig:" + L + "Arm"].head_local; b_ = Mw @ arm.data.bones["mixamorig:" + L + "ForeArm"].head_local
        ln = (b_ - a_).length; ax = (b_ - a_).normalized()
        frames[L] = dict(a=a_, ax=ax, ln=ln, rim=m(turned(ul, wl, RIM_TURN)), shift=m(turned(ul, wl, SHIFT_TURN)) * SLEEVE_SHIFT)
    side_of = lambda co: "Left" if co.x > 0 else "Right"
    def along(co):
        f = frames[side_of(co)]; return (co - f["a"]).dot(f["ax"]) / f["ln"]
    def rim_q(co):   # metres past the bracer's rim (negative: above it)
        f = frames[side_of(co)]; return (co - f["a"]).dot(f["ax"] - f["rim"] * RIM_SLANT) - RIM_ALONG * f["ln"]

    # ---- which faces stay: a generous stretch of arm for now (the clean cuts come next), and the pelvis
    arm_sel = []; pelvis = []
    for v in body.data.vertices:
        co = wb @ v.co; L = side_of(co)
        ws = {names[g.group]: g.weight for g in v.groups}
        s = along(co); f = frames[L]; d = co - f["a"]
        arm_sel.append(ws.get(L + "Arm", 0.0) + ws.get(L + "ForeArm", 0.0) > 0.5 and SLEEVE_TOP - 0.12 <= s <= RIM_ALONG + SLEEVE_PAST_RIM + 0.3 and (d - f["ax"] * d.dot(f["ax"])).length < 0.16)
        n_ = (wb.to_3x3() @ v.normal).normalized()
        pelvis.append(ws.get("Hips", 0.0) > 0.3 and co.z < 1.02 and tree["legs"].ray_cast(co, -n_, 0.06)[0] is None)   # (a point already outside the leg plates goes)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    def most(f, flags):
        idx = [v.index for v in f.verts]
        return sum(flags[i] for i in idx) * 2 > len(idx)
    under = bpy.data.materials.new("Suit_Under"); under.diffuse_color = (0.02, 0.02, 0.02, 1.0); body.data.materials.append(under)
    gone = []
    for f in bm.faces:
        if most(f, pelvis): f.material_index = 1
        elif most(f, arm_sel): f.material_index = 0
        else: gone.append(f)
    n_before = len(bm.faces)
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    # (the model's points are split along its texture's seams: weld them for the shape, or the arm is many loose pieces; the texture's own seams stay)
    bmesh.ops.remove_doubles(bm, verts=[v for v in bm.verts if v.link_faces], dist=2e-5 / wb.to_scale()[0])

    # ---- the cuts: straight edges across the arm, inside each plate
    def sleeve_geom(L):
        sgn = 1 if L == "Left" else -1
        fs = [f for f in bm.faces if f.material_index == 0 and (wb @ f.calc_center_median()).x * sgn > 0]
        return fs + list({e for f in fs for e in f.edges}) + list({v for f in fs for v in f.verts})
    for L, f in frames.items():
        bmesh.ops.bisect_plane(bm, geom=sleeve_geom(L), dist=1e-6, plane_co=wbi @ (f["a"] + f["ax"] * (SLEEVE_TOP * f["ln"])), plane_no=(wb3t @ -f["ax"]).normalized(), clear_outer=True)
        bmesh.ops.bisect_plane(bm, geom=sleeve_geom(L), dist=1e-6, plane_co=wbi @ (f["a"] + f["ax"] * ((RIM_ALONG + SLEEVE_PAST_RIM) * f["ln"])), plane_no=(wb3t @ (f["ax"] - f["rim"] * RIM_SLANT)).normalized(), clear_outer=True)
    # scraps cut off from the arm go
    for L in frames:
        fs = [x for x in sleeve_geom(L) if isinstance(x, bmesh.types.BMFace)]
        seen = set(); parts = []
        for f0 in fs:
            if f0 in seen: continue
            seen.add(f0); st = [f0]; part = []
            while st:
                c = st.pop(); part.append(c)
                for e in c.edges:
                    for g in e.link_faces:
                        if g not in seen and g.material_index == 0: seen.add(g); st.append(g)
            parts.append(part)
        parts.sort(key=len, reverse=True)
        scraps = [f0 for p in parts[1:] for f0 in p]
        if scraps: bmesh.ops.delete(bm, geom=scraps, context="FACES")
    for v in [v for v in bm.verts if not v.link_faces]: bm.verts.remove(v)
    bm.verts.ensure_lookup_table()

    # ---- the tube
    sleeve_v = [v for v in bm.verts if any(f.material_index == 0 for f in v.link_faces)]
    inner = [v for v in sleeve_v if not v.is_boundary]
    for _ in range(SMOOTH_STEPS):   # (the cut edges stay on their planes)
        moved = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.6)) for v in inner]
        for v, c in moved: v.co = c
    def on_line(co):
        f = frames[side_of(co)]
        return f["a"] + f["ax"] * (co - f["a"]).dot(f["ax"]) + f["shift"]
    spoke = {}
    for v in sleeve_v:
        co = wb @ v.co; ap = on_line(co); d = co - ap
        spoke[v] = (ap, d.normalized() if d.length > 1e-5 else (wb.to_3x3() @ v.normal).normalized())
    pull = {v: 0.0 for v in sleeve_v}; need = 0
    for v in sleeve_v:   # behind each plate's wall, even where a plate comes in close
        ap, d = spoke[v]
        for t in (tree["gauntlets"], tree["shoulders"]):
            h = t.ray_cast(ap, d, SLEEVE_ROUND + 0.03)
            if h[0] is not None and h[3] - TUCK_GAP < SLEEVE_ROUND:
                pull[v] = max(pull[v], SLEEVE_ROUND - (h[3] - TUCK_GAP))
        if pull[v] > 0: need += 1
    heap = [(-p, i, v) for i, (v, p) in enumerate(pull.items()) if p > 0]; heapq.heapify(heap); tie = len(heap)
    while heap:
        p, _, v = heapq.heappop(heap); p = -p
        if p < pull[v]: continue
        for e in v.link_edges:
            o = e.other_vert(v)
            if o not in pull: continue
            q = p - TUCK_SLOPE * (spoke[v][0] + spoke[v][1] * SLEEVE_ROUND - spoke[o][0] - spoke[o][1] * SLEEVE_ROUND).length
            if q > pull[o]: pull[o] = q; tie += 1; heapq.heappush(heap, (-q, tie, o))
    for v in sleeve_v:
        ap, d = spoke[v]
        v.co = wbi @ (ap + d * max(SLEEVE_ROUND - pull[v], 0.02))
    print("SUIT sleeve: %d points, %d close to a plate's wall, %d eased in, the most %.1f cm" % (len(sleeve_v), need, sum(1 for p in pull.values() if p > 0), 100 * max(pull.values())))
    # A triangle facing the arm's middle is left over from the model's inner shell. Laid on the tube, it shows as a dark fleck (the armour draws both sides, and a back is lit as a back).
    backward = []
    for f in bm.faces:
        if f.material_index != 0: continue
        f.normal_update()
        c = wb @ f.calc_center_median()
        if (wb.to_3x3() @ f.normal).dot(c - on_line(c)) <= 0: backward.append(f)
    n_back = len(backward)
    bmesh.ops.delete(bm, geom=backward, context="FACES")
    for v in [v for v in bm.verts if not v.link_faces]: bm.verts.remove(v)
    sleeve_v = [v for v in bm.verts if any(f.material_index == 0 for f in v.link_faces)]
    # no hole may be left in the bare stretch between the plates: rays from the tube's middle, all the way round
    bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    fs_ = [f for f in bm.faces if f.material_index == 0]
    idx_ = {v: i for i, v in enumerate(sleeve_v)}
    tube = BVHTree.FromPolygons([(wb @ v.co)[:] for v in sleeve_v], [[idx_[v] for v in f.verts] for f in fs_])
    misses = 0; tries = 0
    for L, f in frames.items():
        for k in range(0, 21):
            s = 0.55 + 0.4 * k / 20
            for th in range(0, 360, 10):
                d = (f["rim"] * math.cos(math.radians(th)) + f["ax"].cross(f["rim"]) * math.sin(math.radians(th))).normalized()
                tries += 1
                if tube.ray_cast(f["a"] + f["ax"] * (s * f["ln"]) + f["shift"], d, 0.2)[0] is None: misses += 1
    print("SUIT sleeve: %d backward faces deleted, %d faces left; %d of %d rays round the bare stretch went through a hole" % (n_back, len(fs_), misses, tries))

    # ---- its weights: the arm bone, fading to the bracer's own at the rim, so the two move as one
    glove = next(p for p in plates if p.name == "gauntlets")
    gw = glove.matrix_world; gnames = {g.index: g.name for g in glove.vertex_groups}
    kd = KDTree(len(glove.data.vertices))
    for i, gv in enumerate(glove.data.vertices): kd.insert(gw @ gv.co, i)
    kd.balance()
    def group(n):
        return (body.vertex_groups.get(n) or body.vertex_groups.new(name=n)).index
    dl = bm.verts.layers.deform.verify()
    for v in sleeve_v:
        co = wb @ v.co; L = side_of(co)
        t = min(1.0, max(0.0, 1.0 + rim_q(co) / WEIGHT_FADE)); t = t * t * (3 - 2 * t)
        ws = {"mixamorig:" + L + "Arm": 1.0 - t}
        if t > 0:
            gi = kd.find(co)[1]
            for g in glove.data.vertices[gi].groups:
                nm = gnames[g.group]; ws[nm] = ws.get(nm, 0.0) + t * g.weight
        tot = sum(ws.values())
        dv = v[dl]; dv.clear()
        for nm, wt in ws.items():
            if wt > 1e-4: dv[group(nm)] = wt / tot

    # ---- the pelvis: narrower, with a clean edge
    pelvis_v = [v for v in bm.verts if any(f.material_index == 1 for f in v.link_faces)]
    ys = [(wb @ v.co).y for v in pelvis_v]; y_mid = (max(ys) + min(ys)) / 2
    for v in pelvis_v:
        co = wb @ v.co
        co.x = max(-PELVIS_X, min(PELVIS_X, co.x)); co.y = y_mid + (co.y - y_mid) * PELVIS_Y
        v.co = wbi @ co
    for _ in range(PELVIS_EDGE_STEPS):
        moved = []
        for v in pelvis_v:
            if v.is_boundary:
                ends = [e.other_vert(v) for e in v.link_edges if e.is_boundary]
                if len(ends) == 2: moved.append((v, v.co.lerp((ends[0].co + ends[1].co) / 2, 0.6)))
            elif _ < 4 and v.link_edges:
                moved.append((v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5)))
        for v, c in moved: v.co = c
    pts = [wb @ v.co for v in pelvis_v]
    print("SUIT pelvis: %d points, x %.3f..%.3f, y %.3f..%.3f, z %.3f..%.3f" % (len(pts), min(p.x for p in pts), max(p.x for p in pts), min(p.y for p in pts), max(p.y for p in pts), min(p.z for p in pts), max(p.z for p in pts)))

    me = body.data
    bm.to_mesh(me); bm.free(); me.update()
    if "custom_normal" in me.attributes: me.attributes.remove(me.attributes["custom_normal"])   # (the model's own shading would no longer fit this shape)
    me.shade_smooth(); me.update()
    print("SUIT mail: %d faces hidden, %d left" % (n_before - len(me.polygons), len(me.polygons)))
