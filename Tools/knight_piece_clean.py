# Cleans one armour piece that Meshy made on its own (Art\Knight Classic\*_prop_*.glb), for the armour test (2026-11-05).
# Run through Blender: blender -b -P knight_piece_clean.py -- <piece.glb> <name> <output folder> <picture folder> [back] [strip]
#                      [rig=<rigged knight.glb>] [fit=scale,x,y,z] [slot=Chest]
#   0. with rig and fit, sets the piece on the knight in his rest pose, so the next step can test it as worn.
#   1. finds the inside walls (never smoothed). Only "strip" deletes them: a face goes if the rays straight out of its front meet the piece's own metal or the knight's body,
#      and almost all of a wide look around it does too. A band next to every opening stays, so the rims still look thick. What
#      was behind a stripped face is the back of the outer wall, and the game draws armour two-sided, so it shows dark metal, not a hole.
#   2. "back" also smooths the dents out of the back half (for pieces Meshy only saw from the front). It evens out small
#      bumps but keeps real edges: leather and metal (told apart by the texture) are never blended, plate steps stay, and
#      it fades out next to rims. The front's vertices and shading are left exactly as they were. The texture is not
#      touched, so it still fits.
#   3. with rig, copies the skeleton's weights from the body, limited to the bones the slot may follow.
#   4. writes <name>_clean.blend and pictures, and logs the triangle counts (lines start PCL).
import bpy, bmesh, sys, os, math, heapq
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index("--") + 1:]
path, name, out, pics = a[0], a[1], a[2], a[3]
SMOOTH_ALL = "all" in a[4:]   # smooth every side, for pieces whose dents go all the way round (shoulders, forearms)
SMOOTH_BACK = "back" in a[4:] or SMOOTH_ALL
STRIP = "strip" in a[4:]
opts = dict(x.split("=", 1) for x in a[4:] if "=" in x)
RIG = opts.get("rig")
ABOVE = float(opts["above"]) if "above" in opts else None   # (height on the piece) smoothing fades out below this, so a gauntlet's fingers stay as made
LEG_FIT = opts.get("fit") == "legs"   # the leg armour's own fit, below; otherwise fit=scale,x,y,z
ARM_FIT = opts.get("fit") == "arm"    # the arm harness's own fit, below
FIT = [float(v) for v in opts["fit"].split(",")] if "fit" in opts and not LEG_FIT and not ARM_FIT else None
SLOT = opts.get("slot", "Chest")
os.makedirs(out, exist_ok=True); os.makedirs(pics, exist_ok=True)
ALLOWED = {
    "Helm": ["Head", "Neck"],
    "Chest": ["Hips", "Spine", "Spine1", "Spine2", "Neck", "LeftShoulder", "RightShoulder", "LeftUpLeg", "RightUpLeg"],
    "Shoulders": ["Spine2", "LeftShoulder", "RightShoulder", "LeftArm", "RightArm"],
    "Gauntlets": ["LeftForeArm", "RightForeArm", "LeftHand", "RightHand", "LeftArm", "RightArm"],
    "Legs": ["Hips", "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg", "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase"],
}

# ------- tuning (sizes are shares of the piece's height, so they work for any piece)
CONE_DEG = 12.0      # how far around straight out each face checks is shut
OPEN_SHARE = 0.15    # a face that sees more open sky than this over its whole view is on the outside, whatever is above it
BAND = 0.02          # how far the kept band reaches in from an opening
SPECK = 0.03         # a loose patch narrower than this, kept or stripped, joins what surrounds it
BACK_FROM, BACK_TO = 0.0, 0.08   # (shares of the height, along +Y, the knight's back) where smoothing fades in
NORMAL_SIGMA = 0.3   # how different two faces may be and still get evened out; higher flattens stronger creases
ROUNDS, NORMAL_STEPS, MOVE_STEPS = 2, 8, 6
MAX_MOVE = 0.006     # no point moves further than this
RIM_FADE = 0.025     # smoothing fades in over this distance from a rim
CREASE_DEG = 40.0    # sharper than this stays a hard edge in the shading
THIGH_CAP = 0.5      # the most a chest piece's skirt may follow a thigh, at its hem
SKIRT_TOP = 0.336    # where the skirt starts (just under the belt), as a share of the chest piece's height from its hem
SKIRT_BLEND = 0.06   # the body's own weights fade into the skirt's over this
KNEE_Y, KNEE_LO, KNEE_HI, KNEE_FADE = -0.015, 0.44, 0.655, 0.03   # (metres on the knight) where the leg armour's knee plate sits
KNEE_BLUNT, KNEE_BLUNT_Y, KNEE_BLUNT_ZLO, KNEE_BLUNT_ZHI = 0.3, -0.04, 0.0, 0.42  # (numbers on the Meshy piece) how much of the depth stays, past where, and between which heights
BLUNTED = None

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=path)
o = next(x for x in bpy.data.objects if x.type == "MESH")
wm = o.matrix_world.copy(); o.parent = None; o.matrix_world = wm
me = o.data; me.transform(wm); o.matrix_world.identity()
for x in list(bpy.data.objects):
    if x is not o: bpy.data.objects.remove(x)
bpy.ops.object.select_all(action="DESELECT")
piece_images = [n.image for n in o.material_slots[0].material.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]
if opts.get("cut") == "helmetloop":
    # The thin loop under the helmet's chin is one mesh with the collar, so it is cut by where it sits, seen from the side:
    # under a line forward of the neck plate (the loop itself), and under a soft curve that takes the collar's front corner off,
    # so the plate ends in a sweep instead of a tooth. The cut edge is laid on the curve so it runs smooth. (Numbers on the Meshy piece.)
    LOOP_Y, LOOP_Z, LOOP_SLOPE = 0.0125, -0.655, 0.537
    CURVE_Y, CURVE_Z = [-0.067, -0.013, 0.053, 0.147, 0.307], [-0.576, -0.615, -0.64, -0.649, -0.649]   # (never below the back rim, or its ends poke down from the front)
    def curve_z(y): return float(np.interp(y, CURVE_Y, CURVE_Z))
    def in_loop(c):
        if c.y < LOOP_Y and c.z < LOOP_Z + LOOP_SLOPE * (c.y - LOOP_Y) + 0.012: return True
        return CURVE_Y[0] <= c.y <= CURVE_Y[-1] and c.z < curve_z(c.y)
    bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
    gone = [f for f in bm.faces if in_loop(f.calc_center_median())]
    n_before = len(bm.faces)
    touched = {v for f in gone for v in f.verts}
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    key = lambda v: tuple(round(c / 1e-5) for c in v.co)
    edge_keys = {key(v) for v in touched if v.is_valid and v.link_faces and CURVE_Y[0] <= v.co.y <= CURVE_Y[-1]}
    laid = 0
    for v in bm.verts:   # (the texture splits points along its seams: both copies move together)
        if not CURVE_Y[0] <= v.co.y <= CURVE_Y[-1]: continue
        cz = curve_z(v.co.y)
        if v.co.z < cz or (key(v) in edge_keys and v.co.z - cz < 0.05):   # nothing is left hanging under the curve, or it shows as teeth
            v.co.z = cz; laid += 1
    bm.to_mesh(me); bm.free(); me.update()
    print("PCL helmet loop cut: %d faces gone, %d edge points laid on the curve, %d faces now (from %d)" % (len(gone), laid, len(me.polygons), n_before))
if "spot" in opts:
    # Paints a stain out of the model's pictures: a ray from the front finds it, it grows over the dark pixels there, and the gap fills in from its edges.
    sx, sz = [float(v) for v in opts["spot"].split(",")]
    me.calc_loop_triangles()
    tree = BVHTree.FromPolygons([v.co[:] for v in me.vertices], [t.vertices[:] for t in me.loop_triangles])
    hit, _, fi, _ = tree.ray_cast(Vector((sx, -5.0, sz)), Vector((0, 1, 0)), 20.0)
    tri = me.loop_triangles[fi]
    vs = [me.vertices[v].co for v in tri.vertices]; uvs = [me.uv_layers.active.data[l].uv for l in tri.loops]
    from mathutils.geometry import barycentric_transform
    bary = barycentric_transform(hit, vs[0], vs[1], vs[2], Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
    uv = uvs[0] * bary[0] + uvs[1] * bary[1] + uvs[2] * bary[2]
    base = next(im for im in piece_images if "metallic" not in im.name); mrim = next(im for im in piece_images if "metallic" in im.name)
    w_, h_ = base.size
    Bp = np.empty(w_ * h_ * 4, dtype=np.float32); base.pixels.foreach_get(Bp); Bp = Bp.reshape(h_, w_, 4)
    cx, cy = int((uv.x % 1) * (w_ - 1)), int((uv.y % 1) * (h_ - 1))
    R_ = 160
    x0, x1, y0, y1 = max(cx - R_, 0), min(cx + R_, w_), max(cy - R_, 0), min(cy + R_, h_)
    win = Bp[y0:y1, x0:x1, :3].mean(2)
    px, py = cx - x0, cy - y0
    med = float(win[py, px]); dark = win < med + 0.12   # the stain is one flat dark colour on lighter metal
    mask = np.zeros_like(dark); st = [(py, px)]; mask[py, px] = True
    while st:
        yy, xx = st.pop()
        for ny, nx in ((yy + 1, xx), (yy - 1, xx), (yy, xx + 1), (yy, xx - 1)):
            if 0 <= ny < dark.shape[0] and 0 <= nx < dark.shape[1] and dark[ny, nx] and not mask[ny, nx]: mask[ny, nx] = True; st.append((ny, nx))
    grow = mask.copy()
    for _ in range(5):
        g_ = grow.copy(); g_[1:] |= grow[:-1]; g_[:-1] |= grow[1:]; g_[:, 1:] |= grow[:, :-1]; g_[:, :-1] |= grow[:, 1:]; grow = g_
    print("PCL spot: uv %.3f %.3f, %d dark pixels, painting %d (window median %.3f)" % (uv.x, uv.y, mask.sum(), grow.sum(), med))
    for im in (base, mrim):
        Ip = np.empty(w_ * h_ * 4, dtype=np.float32); im.pixels.foreach_get(Ip); Ip = Ip.reshape(h_, w_, 4)
        pt = Ip[y0:y1, x0:x1].copy(); todo = grow.copy()
        while todo.any():   # fill from the edge inward
            ngh = np.zeros_like(pt); cnt = np.zeros(todo.shape, dtype=np.float32); known = (~todo).astype(np.float32)
            for dy_, dx_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ngh += np.roll(pt * known[..., None], (dy_, dx_), (0, 1)); cnt += np.roll(known, (dy_, dx_), (0, 1))
            edge = todo & (cnt > 0)
            pt[edge] = ngh[edge] / cnt[edge][:, None]; todo[edge] = False
        for _ in range(12):   # even out the fill's streaks, inside the patch only
            even = (np.roll(pt, 1, 0) + np.roll(pt, -1, 0) + np.roll(pt, 1, 1) + np.roll(pt, -1, 1)) / 4
            pt[grow] = even[grow]
        # the faintest grain, so the patch isn't dead smooth next to the metal
        noise = np.random.default_rng(7).normal(0, 0.006, grow.shape)
        pt[..., :3][grow] = np.clip(pt[..., :3][grow] + noise[grow][:, None], 0, 1); pt[..., 3] = 1.0
        Ip[y0:y1, x0:x1] = pt
        im.pixels.foreach_set(Ip.ravel()); im.update(); im.pack()
ZPIECE = None
if FIT:
    # fit=scale,x,y,z[,height scale[,break,height scale above it,...]]: a second number makes the piece slimmer than it is tall, and its shading follows.
    # With breaks (heights on the piece, lowest first): the first goes to z, and each stretch between them has its own height scale.
    FZ = FIT[4] if len(FIT) > 4 else FIT[0]
    BR = np.array(FIT[5::2]) if len(FIT) > 6 else np.array([0.0])
    SC = np.array([FZ] + FIT[6::2]) if len(FIT) > 6 else np.array([FZ, FZ])
    ZK = FIT[3] + np.concatenate([[0.0], np.cumsum(np.diff(BR) * SC[1:-1])])   # (where each break lands)
    n0 = np.array([c.vector[:] for c in me.corner_normals])
    Lf = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get("vertex_index", Lf)
    zr = np.array([v.co.z for v in me.vertices])
    seg = np.searchsorted(BR, zr)   # (0: below the first break)
    zn = np.where(seg == 0, ZK[0] + (zr - BR[0]) * SC[0], ZK[np.maximum(seg - 1, 0)] + (zr - BR[np.maximum(seg - 1, 0)]) * SC[seg])
    for i, v in enumerate(me.vertices): v.co = Vector((v.co.x * FIT[0] + FIT[1], v.co.y * FIT[0] + FIT[2], zn[i]))
    zs = SC[seg][Lf]
    nn = np.stack([n0[:, 0] / FIT[0], n0[:, 1] / FIT[0], n0[:, 2] / zs], 1); nn /= np.maximum(np.linalg.norm(nn, axis=1), 1e-12)[:, None]
    me.update(); me.normals_split_custom_set(nn.tolist()); me.update()
if "place" in opts:
    # place=scale, piece pivot x y z, piece axis x y z, knight axis x y z, knight spot x y z[, stretch x y z[, trail, hinge]]: for a pair of pieces (gauntlets, shoulders),
    # given for the knight's left side (+X). The pivot goes on the spot and the piece's axis along his limb; the other side is the mirror image.
    # The stretch multiplies the scale along each of the piece's own axes. The trail swings what hangs below the hinge (a height on the piece) back by
    # this much per metre down, so a shoulder's plates follow the arm while its dome, above the hinge, stays exactly as round as it was made.
    # Never shear a dome to tilt it (owner, 2026-11-05: it stops being round).
    # squash=height,amount: below that height on the piece, the length is multiplied by the amount (a gauntlet's fingers).
    PL = [float(v) for v in opts["place"].split(",")]
    s_, piv_, ax_, bx_, tgt_ = PL[0], Vector(PL[1:4]), Vector(PL[4:7]).normalized(), Vector(PL[7:10]).normalized(), Vector(PL[10:13])
    str_ = np.array(PL[13:16]) if len(PL) >= 16 else np.ones(3)
    trail_, hinge_ = (PL[16], PL[17]) if len(PL) >= 18 else (0.0, 0.0)
    rot_ = ax_.rotation_difference(bx_).to_matrix()
    Pp = np.empty((len(me.vertices), 3)); me.vertices.foreach_get("co", Pp.ravel())
    ZPIECE = Pp[:, 2].copy()
    if "squash" in opts:
        zb_, k_ = [float(v) for v in opts["squash"].split(",")]
        Pp[:, 2] = np.where(Pp[:, 2] < zb_, zb_ + (Pp[:, 2] - zb_) * k_, Pp[:, 2])
    Lp = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get("vertex_index", Lp)
    n0 = np.array([c.vector[:] for c in me.corner_normals])
    Rm = np.array(rot_); side = np.where(Pp[:, 0] >= 0, 1.0, -1.0)[:, None]; flip = np.array([1.0, 0.0, 0.0])
    def mirror(p_, k_): return p_ * (1 - 2 * flip * (k_ < 0))
    Pq = mirror(Pp, side)
    if "dome" in opts:
        # dome=centre x, half width, half depth, amount, swell's origin x, z (numbers on the piece): rounds a plate that Meshy made almost straight. Seen from above, an
        # ellipse around the plate's middle swells outward, most at its centre and nothing at its edge, so the flange and the rim stay put. Shading is redone later from the shape.
        dcx, dax, day, damt, dox, doz = [float(v) for v in opts["dome"].split(",")]
        dw = np.clip(1 - ((Pq[:, 0] - dcx) / dax) ** 2 - (Pq[:, 1] / day) ** 2, 0.0, 1.0) * (Pq[:, 2] > 0.0)
        dd = Pq - np.array([dox, 0.0, doz]); dd /= np.maximum(np.linalg.norm(dd, axis=1), 1e-9)[:, None]
        Pq = Pq + dd * (dw * damt)[:, None]
        print("PCL dome swelled: %d points, the most %.3f" % ((dw > 0).sum(), dw.max() * damt))
    Pn = (Pq - np.array(piv_[:])) * s_ * str_
    # the swing below the hinge, eased in over a few centimetres so the plates don't kink (+Y is his back)
    HINGE_EASE = 0.04
    below = np.maximum((hinge_ - Pq[:, 2]) * s_ * str_[2], 0.0)
    Pn[:, 1] += trail_ * np.where(below < HINGE_EASE, below * below / (2 * HINGE_EASE), below - HINGE_EASE / 2)
    Pn = Pn @ Rm.T + np.array(tgt_[:])
    Pn = mirror(Pn, side)
    n1 = mirror(n0, side[Lp]) / str_
    n1[:, 2] += trail_ * np.minimum(below[Lp] / HINGE_EASE, 1.0) * n1[:, 1]
    nn = mirror(n1 @ Rm.T, side[Lp]); nn /= np.maximum(np.linalg.norm(nn, axis=1), 1e-12)[:, None]
    me.vertices.foreach_set("co", Pn.ravel()); me.update()
    me.normals_split_custom_set(nn.tolist()); me.update()
if LEG_FIT:
    # Meshy's legs and the knight's differ along their length, so these numbers come from measuring both (scratch measure, 2026-11-05):
    # by height on the Meshy piece, how deep to make it and how far forward to bring it. (Redone on the thinner knight, 2026-11-05.)
    LZ = [-0.95, -0.75, -0.6, -0.25, 0.16, 0.65, 0.95]
    LSY = [0.47, 0.47, 0.55, 0.55, 0.55, 0.55, 0.55]
    LYO = [0.0, 0.005, -0.017, -0.048, -0.024, -0.08, -0.085]
    LFW = [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]   # (boots can be narrowed around FOOT_X, if a thick leg ever makes clown feet)
    FOOT_X = 0.492
    LSX, LTOPIN = 0.55, 0.04
    # Heights on the piece -> heights on the knight, from the owner's reference pictures (2026-11-05): boot top, greave top, knee plate's middle and
    # top, then the thigh's top. The pictures' knee plate sits higher than the knight's real knee joint (0.547), so this one rides a little above the joint.
    ZFROM, ZTO = [-1.2, -0.95, -0.54, 0.013, 0.196, 0.36, 0.95, 1.2], [-0.163, 0.0, 0.268, 0.52, 0.62, 0.72, 0.923, 1.06]
    grid = np.linspace(-1.2, 1.2, 481); kern = np.ones(41) / 41   # no sudden bends between the numbers above
    def soft(vals):
        v_ = np.interp(grid, LZ, vals); return np.convolve(np.pad(v_, 20, mode="edge"), kern, mode="valid")
    gsy, gyo, gfw = soft(LSY), soft(LYO), soft(LFW)
    gin = np.clip((grid - 0.16) / 0.79, 0, 1) * LTOPIN
    dsy, dyo, din, dfw = np.gradient(gsy, grid), np.gradient(gyo, grid), np.gradient(gin, grid), np.gradient(gfw, grid)
    gzz = np.convolve(np.pad(np.interp(grid, ZFROM, ZTO), 20, mode="reflect", reflect_type="odd"), kern, mode="valid"); dzz = np.gradient(gzz, grid)
    Pg = np.empty((len(me.vertices), 3)); me.vertices.foreach_get("co", Pg.ravel())
    L_ = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get("vertex_index", L_)
    n0 = np.array([c.vector[:] for c in me.corner_normals])
    # blunt the knee plates (owner, 2026-11-05): the part out in front is made shallower, so the plate keeps its ridge but stops looking like a beak
    def ks(a_, b_, x_):
        t_ = np.clip((x_ - a_) / (b_ - a_), 0, 1); return t_ * t_ * (3 - 2 * t_)
    zone = ks(KNEE_BLUNT_ZLO, KNEE_BLUNT_ZLO + 0.05, Pg[:, 2]) * (1 - ks(KNEE_BLUNT_ZHI - 0.05, KNEE_BLUNT_ZHI, Pg[:, 2]))
    past = np.maximum(KNEE_BLUNT_Y - Pg[:, 1], 0.0)
    Pg[:, 1] += past * (1 - KNEE_BLUNT) * zone
    BLUNTED = (zone * past) > 1e-6
    print("PCL knee plates blunted: %d points" % BLUNTED.sum())
    Z_ = Pg[:, 2]; sign_ = np.sign(Pg[:, 0])
    absx = np.abs(Pg[:, 0])
    Pn = np.stack([sign_ * (LSX * (FOOT_X + (absx - FOOT_X) * np.interp(Z_, grid, gfw)) - np.interp(Z_, grid, gin)), Pg[:, 1] * np.interp(Z_, grid, gsy) + np.interp(Z_, grid, gyo), np.interp(Z_, grid, gzz)], 1)
    # shading follows the new shape
    pz = Z_[L_]; py = Pg[L_, 1]; sy_ = np.interp(pz, grid, gsy); sx_ = LSX * np.interp(pz, grid, gfw)
    dxdz = sign_[L_] * (LSX * (absx[L_] - FOOT_X) * np.interp(pz, grid, dfw) - np.interp(pz, grid, din))
    nz_ = n0[:, 2] - n0[:, 1] * (py * np.interp(pz, grid, dsy) + np.interp(pz, grid, dyo)) / sy_ - n0[:, 0] * dxdz / sx_
    nn = np.stack([n0[:, 0] / sx_, n0[:, 1] / sy_, nz_ / np.interp(pz, grid, dzz)], 1); nn /= np.maximum(np.linalg.norm(nn, axis=1), 1e-12)[:, None]
    me.vertices.foreach_set("co", Pn.ravel()); me.update()
    me.normals_split_custom_set(nn.tolist()); me.update()
ARM_W = None
if ARM_FIT:
    # A pair of whole arms, bent at the elbow, in one mesh (the arm harness, 2026-11-05). All numbers are for his left (+X); the other side is the mirror image.
    # Meshy made them with the elbow plate facing out and the palm back: turned to the knight's own elbow, the plate faces back and the palm in, as in the owner's pictures.
    # The upper arm and the forearm each turn as a solid onto their own bone. They bend less on the knight (21 degrees) than as made (about 40), so the difference is taken up at the elbow;
    # the elbow plate itself stays solid and round, halfway between the two (ARM_W keeps these shares: the same ones become the weights, so it also moves halfway between the two bones).
    RT, RE, RW = np.array([0.327, 0.1, 0.84]), np.array([0.6, 0.06, 0.37]), np.array([0.48, -0.045, -0.34])   # on the piece: the upper tube's top, the elbow, the wrist (the cuff's lower rim)
    KS, KE, KW = np.array([0.244, 0.036, 1.409]), np.array([0.326, 0.057, 1.156]), np.array([0.419, -0.013, 0.935])   # on the knight: shoulder, elbow, wrist joints
    RH = np.array([-0.2, -0.15, -0.97])   # on the piece: the hand's line, from the wrist down
    DISC_C, DISC_IN, DISC_OUT = np.array([0.658, 0.02, 0.356]), 0.17, 0.22   # on the piece: the elbow plate's dome, on a line straight out (+X) through this point; solid out to the first radius, blending by the second. The collar below it is the forearm's: caught in this, it twisted into claws at a full bend
    A_DISC_SHARE = 0.6    # how much of the dome follows the forearm, the rest the upper arm (the point of a real elbow belongs to the forearm)
    A_SCALE = 0.35        # the size of it all, picked against the pictures: the forearm and the elbow plate
    A_TUBE_FROM, A_TOP = 0.27, 0.154    # above this far from the elbow (on the piece) only the length changes, so the plate keeps round; the tube's top ends this far from the elbow joint (m), 3 cm inside the pauldron's plates
    A_TUBE_AIM = np.array([0.04, 0.016, 0.014])   # (m) the tube points this far off the shoulder joint: the pauldron's plates sit outward and back of the bone, and the tube has to go up inside them
    A_FORE_FROM = 0.25    # likewise below the elbow: the rest of the forearm is fitted so that the cuff lands on the wrist
    A_FORE_R, A_TUBE_R = 1.0, 0.82   # how much wider the forearm and the upper tube are than the size above (each fades in past the two numbers before; the tube narrows to go inside the plates)
    A_HAND_LEN, A_HAND_R = 0.8, 1.0   # the hand's length and width, on the size above
    A_HAND_ROLL = 40.0    # (degrees) turns the hand on its own line to hang like the knight's: palm to the hip, thumb forward
    A_ELBOW_BLEND, A_WRIST_BLEND = 0.06, 0.06   # (on the piece) over how far the upper arm becomes the forearm, either side of the elbow; and the forearm the hand, below the wrist
    A_BLUNT_R, A_BLUNT_ANGLE, A_BLUNT_OFF, A_BLUNT_ROUNDS = 0.45, 285.0, 0.012, 8   # (on the piece) how far round the elbow points are blunted; what counts as sharp: degrees all round it, how far off its neighbours; at most this many passes
    def unit_(v):
        v = np.array(v, dtype=float); return v / np.linalg.norm(v)
    def frame_(a, h):
        h = unit_(h - a * a.dot(h)); return np.stack([a, h, np.cross(a, h)], 1)
    def smooth_(t):
        t = np.clip(t, 0.0, 1.0); return t * t * (3 - 2 * t)
    Ur, Fr, Uk, Fk, Hr = unit_(RE - RT), unit_(RW - RE), unit_(KE - KS - A_TUBE_AIM), unit_(KW - KE), unit_(RH)
    hr, hk = unit_(np.cross(Ur, Fr)), unit_(np.cross(unit_(KE - KS), Fk))   # (the hinge: the real bones')
    Ru, Rf = frame_(Uk, hk) @ frame_(Ur, hr).T, frame_(Fk, hk) @ frame_(Fr, hr).T
    ang = math.radians(A_HAND_ROLL); Kx = np.array([[0, -Hr[2], Hr[1]], [Hr[2], 0, -Hr[0]], [-Hr[1], Hr[0], 0]])
    Rh = Rf @ (np.eye(3) + math.sin(ang) * Kx + (1 - math.cos(ang)) * Kx @ Kx)
    Pp = np.empty((len(me.vertices), 3)); me.vertices.foreach_get("co", Pp.ravel())
    ZPIECE = Pp[:, 2].copy()
    Lp = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get("vertex_index", Lp)
    n0 = np.array([c.vector[:] for c in me.corner_normals])
    # The sharp points round the elbow (a beak under the plate, the corners of the plates in the crook) stick out as shards at a full bend. Each is pulled onto the
    # middle of its neighbours until none is left: a point is sharp if its faces add up to far less than a full turn and it stands well off its neighbours (scratch measure, 2026-11-05).
    _, first_, Wv = np.unique(np.round(Pp / 1e-5).astype(np.int64), axis=0, return_index=True, return_inverse=True); Wv = Wv.ravel()
    Xb = Pp[first_].copy(); Tw = Wv[Lp.reshape(-1, 3)]
    Eb = np.unique(np.sort(np.concatenate([Tw[:, [0, 1]], Tw[:, [1, 2]], Tw[:, [2, 0]]]), axis=1), axis=0)
    inzone = np.linalg.norm(np.c_[np.abs(Xb[:, 0]), Xb[:, 1:]] - RE, axis=1) < A_BLUNT_R
    moved_ = np.zeros(len(Xb), bool); X0b = Xb.copy()
    for _ in range(A_BLUNT_ROUNDS):
        sum_ = np.zeros(len(Xb))
        for i_, j_, k_ in ((0, 1, 2), (1, 2, 0), (2, 0, 1)):
            u1, u2 = Xb[Tw[:, j_]] - Xb[Tw[:, i_]], Xb[Tw[:, k_]] - Xb[Tw[:, i_]]
            np.add.at(sum_, Tw[:, i_], np.arctan2(np.linalg.norm(np.cross(u1, u2), axis=1), (u1 * u2).sum(1)))
        nbs = np.zeros_like(Xb); cnt = np.zeros(len(Xb))
        np.add.at(nbs, Eb[:, 0], Xb[Eb[:, 1]]); np.add.at(nbs, Eb[:, 1], Xb[Eb[:, 0]]); np.add.at(cnt, Eb[:, 0], 1); np.add.at(cnt, Eb[:, 1], 1)
        nbs /= np.maximum(cnt, 1)[:, None]
        tip_ = inzone & (sum_ < math.radians(A_BLUNT_ANGLE)) & (np.linalg.norm(Xb - nbs, axis=1) > A_BLUNT_OFF)
        if not tip_.any(): break
        Xb[tip_] = nbs[tip_]; moved_ |= tip_
    near_ = moved_.copy(); near_[Eb[moved_[Eb[:, 1]], 0]] = True; near_[Eb[moved_[Eb[:, 0]], 1]] = True
    Pp = Xb[Wv]; BLUNTED = near_[Wv]   # (so the shading follows the new shape there)
    print("PCL arm blunted: %d points, the furthest moved %.3f on the piece" % (moved_.sum(), np.linalg.norm(Xb - X0b, axis=1).max()))
    flip = np.array([1.0, 0.0, 0.0]); side = np.where(Pp[:, 0] >= 0, 1.0, -1.0)[:, None]
    def mirror(p_, k_): return p_ * (1 - 2 * flip * (k_ < 0))
    Pq = mirror(Pp, side)
    q = Pq - RE
    # who follows what
    be = unit_(Ur + Fr); we = smooth_((q @ be) / (2 * A_ELBOW_BLEND) + 0.5)
    dq = q + RE - DISC_C; rho = np.linalg.norm(dq[:, 1:], axis=1)
    md = (1 - smooth_((rho - DISC_IN) / (DISC_OUT - DISC_IN))) * smooth_((Pq[:, 0] - 0.55) / 0.08)
    we = we * (1 - md) + A_DISC_SHARE * md
    qw = Pq - RW; wh = smooth_((qw @ Hr) / A_WRIST_BLEND)
    # the upper arm
    au = q @ -Ur; ru = q + Ur * au[:, None]   # (how far above the elbow; and what's across)
    ku = (A_TOP - A_SCALE * A_TUBE_FROM) / (np.linalg.norm(RE - RT) - A_TUBE_FROM)
    au2 = np.where(au <= A_TUBE_FROM, A_SCALE * au, A_SCALE * A_TUBE_FROM + (au - A_TUBE_FROM) * ku)
    rt = 1 + (A_TUBE_R - 1) * smooth_((au - A_TUBE_FROM + 0.05) / 0.1)
    Pu = KE + (ru * (A_SCALE * rt)[:, None] - Ur * au2[:, None]) @ Ru.T
    # the forearm
    bf = q @ Fr; rf = q - Fr * bf[:, None]
    bl = np.linalg.norm(RW - RE)
    kf = (np.linalg.norm(KW - KE) - A_SCALE * A_FORE_FROM) / (bl - A_FORE_FROM)
    bf2 = np.where(bf <= A_FORE_FROM, A_SCALE * bf, A_SCALE * A_FORE_FROM + (bf - A_FORE_FROM) * kf)
    rr = 1 + (A_FORE_R - 1) * smooth_((bf - A_FORE_FROM + 0.05) / 0.1)
    Pf = KE + (rf * (A_SCALE * rr)[:, None] + Fr * bf2[:, None]) @ Rf.T
    # the hand
    ch = qw @ Hr; rh = qw - Hr * ch[:, None]
    Ph = KW + (rh * A_SCALE * A_HAND_R + Hr[None, :] * (ch * A_SCALE * A_HAND_LEN)[:, None]) @ Rh.T
    wu = (1 - we); wf = we * (1 - wh); wh2 = we * wh
    Pn = Pu * wu[:, None] + Pf * wf[:, None] + Ph * wh2[:, None]
    me.vertices.foreach_set("co", mirror(Pn, side).ravel()); me.update()
    n1 = mirror(n0, side[Lp])
    nh = n1 - Hr[None, :] * (n1 @ Hr)[:, None]; nh = nh / A_HAND_R + Hr[None, :] * ((n1 @ Hr) / A_HAND_LEN)[:, None]
    nn = (n1 @ Ru.T) * wu[Lp, None] + (n1 @ Rf.T) * wf[Lp, None] + (nh @ Rh.T) * wh2[Lp, None]
    nn = mirror(nn, side[Lp]); nn /= np.maximum(np.linalg.norm(nn, axis=1), 1e-12)[:, None]
    me.normals_split_custom_set(nn.tolist()); me.update()
    ARM_W = np.stack([wu, wf, wh2], 1)
    print("PCL arm fit: raw bend %.0f degrees, the knight's %.0f; the elbow plate faces %s, the palm %s; forearm length scale %.3f, tube %.3f" % (math.degrees(math.acos(Ur.dot(Fr))), math.degrees(math.acos(Uk.dot(Fk))), np.round((0.5 * Ru + 0.5 * Rf) @ np.array([1.0, 0, 0]), 2), np.round(Rh @ unit_([-0.367, 0.925, -0.067]), 2), kf, ku))
arm = body = None; Vb = None; Fb = []
if RIG:
    bpy.ops.import_scene.gltf(filepath=RIG)
    for x in list(bpy.data.objects):
        if x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
    arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
    body = next(x for x in bpy.data.objects if x.type == "MESH" and x is not o)
    arm.data.pose_position = "REST"; bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get(); e = body.evaluated_get(dg); m_ = e.to_mesh()
    Vb = np.array([(body.matrix_world @ v.co)[:] for v in m_.vertices]); Fb = [list(p.vertices) for p in m_.polygons]
    e.to_mesh_clear(); body.hide_render = True
nv, nf, nl = len(me.vertices), len(me.polygons), len(me.loops)
if nl != nf * 3: raise SystemExit("PCL expected a mesh of triangles")
print("PCL start: %d triangles" % nf)

P = np.empty((nv, 3)); me.vertices.foreach_get("co", P.ravel())
LV = np.empty(nl, dtype=np.int64); me.loops.foreach_get("vertex_index", LV)
LS = np.empty(nf, dtype=np.int64); me.polygons.foreach_get("loop_start", LS)
T = np.stack([LV[LS], LV[LS + 1], LV[LS + 2]], 1)
orig_n = np.array([c.vector[:] for c in me.corner_normals])

# the texture splits vertices along its seams: weld by position for everything about the shape
_, first, W = np.unique(np.round(P / 1e-5).astype(np.int64), axis=0, return_index=True, return_inverse=True)
W = W.ravel(); X = P[first].copy(); F = W[T]; X0 = X.copy()
H = float(P[:, 2].max() - P[:, 2].min())

def face_data(X):
    e1, e2 = X[F[:, 1]] - X[F[:, 0]], X[F[:, 2]] - X[F[:, 0]]
    c = np.cross(e1, e2); l = np.linalg.norm(c, axis=1)
    return c / np.maximum(l, 1e-12)[:, None], X[F].mean(1), l / 2

N, C, A = face_data(X)
print("PCL weld: %d points, height %.3f" % (len(X), H))

# ---------------------------------------- which faces look at nothing but the piece itself
if RIG:
    bvh = BVHTree.FromPolygons(X.tolist() + Vb.tolist(), F.tolist() + [[v + len(X) for v in f] for f in Fb])
else:
    bvh = BVHTree.FromPolygons(X.tolist(), F.tolist())
eps = H * 0.0015
cc, ss = math.cos(math.radians(CONE_DEG)), math.sin(math.radians(CONE_DEG))
# a fixed even spread of directions over the half-sphere, for the wide look
WIDE = []
for k in range(40):
    zz = (k + 0.5) / 40; rr = math.sqrt(1 - zz * zz); ang = k * 2.39996
    WIDE.append((rr * math.cos(ang), rr * math.sin(ang), zz))
inner = np.zeros(nf, bool); saved = 0
for i in range(nf):
    n = Vector(N[i]); c = Vector(C[i])
    t = n.orthogonal().normalized(); b = n.cross(t)
    dirs = [n] + [(n * cc + (t * math.cos(k * math.pi / 2) + b * math.sin(k * math.pi / 2)) * ss) for k in range(4)]
    pts = [c] + [c + (Vector(X[v]) - c) * 0.7 for v in F[i]]
    shut = True
    for p in pts:
        q = p + n * eps
        for d in dirs:
            if bvh.ray_cast(q, d, H * 3)[0] is None: shut = False; break
        if not shut: break
    if shut:
        q = c + n * eps
        sky = sum(1 for w in WIDE if bvh.ray_cast(q, t * w[0] + b * w[1] + n * w[2], H * 3)[0] is None)
        if sky > OPEN_SHARE * len(WIDE): shut = False; saved += 1
    inner[i] = shut
print("PCL shut in by its own metal: %d of %d (%d kept because they see open sky)" % (inner.sum(), nf, saved))

# neighbours across edges
edge_faces = {}
for i, f in enumerate(F.tolist()):
    for j in range(3):
        k = (f[j], f[(j + 1) % 3]) if f[j] < f[(j + 1) % 3] else (f[(j + 1) % 3], f[j])
        edge_faces.setdefault(k, []).append(i)
adj = [[] for _ in range(nf)]
for fs in edge_faces.values():
    for x in fs:
        for y in fs:
            if x != y: adj[x].append(y)

def groups(mask):
    seen = np.zeros(nf, bool); res = []
    for s in np.nonzero(mask)[0]:
        if seen[s]: continue
        st = [s]; seen[s] = True; r = []
        while st:
            c = st.pop(); r.append(c)
            for g in adj[c]:
                if mask[g] and not seen[g]: seen[g] = True; st.append(g)
        res.append(r)
    return res

speck = (SPECK * H) ** 2
flipped = 0
for r in groups(~inner):   # a little open patch deep inside goes with the inside (a loose rivet, touching no inner face, stays)
    if A[r].sum() < speck and any(inner[g] for c in r for g in adj[c]): inner[r] = True; flipped += len(r)
for r in groups(inner):    # a little shut patch on the outside is not worth a hole
    if A[r].sum() < speck: inner[r] = False; flipped += len(r)

# the band by every opening
dist = np.full(nf, np.inf); pq = []
for i in np.nonzero(~inner)[0]:
    dist[i] = 0.0; pq.append((0.0, int(i)))
heapq.heapify(pq); reach = BAND * H
while pq:
    d, i = heapq.heappop(pq)
    if d > dist[i]: continue
    for g in adj[i]:
        nd = d + float(np.linalg.norm(C[i] - C[g]))
        if nd < dist[g] and nd < reach: dist[g] = nd; heapq.heappush(pq, (nd, g))
band = inner & np.isfinite(dist)
doomed = (inner & ~band) if STRIP else np.zeros(nf, bool)   # the owner did not like the stripped look (2026-11-05): off unless asked for
print("PCL patches moved %d; band %d; to strip %d (%.0f%%); keeps %d" % (flipped, band.sum(), doomed.sum(), 100 * doomed.sum() / nf, nf - doomed.sum()))

# ---------------------------------------- pictures
scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"; scene.display.shading.light = "STUDIO"
scene.display.shading.single_color = (0.62, 0.62, 0.62); scene.display.shading.show_backface_culling = False
scene.render.resolution_x = 800; scene.render.resolution_y = 800; scene.render.image_settings.file_format = "PNG"
cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.type = "ORTHO"; cam.data.clip_end = 100
cen = Vector(((P.min(0) + P.max(0)) / 2).tolist()); ext = float((P.max(0) - P.min(0)).max())
def shot(name, az, mode, scale=1.1, cut=False):
    ang = math.radians(az); dd = ext * 3
    cam.location = cen + Vector((math.sin(ang) * dd, -math.cos(ang) * dd, 0)); cam.rotation_euler = (math.pi / 2, 0, ang)
    cam.data.ortho_scale = ext * scale; cam.data.clip_start = dd if cut else 0.01
    scene.display.shading.color_type = mode
    scene.render.filepath = os.path.join(pics, name + ".png"); bpy.ops.render.render(write_still=True)

shot("before_back_clay", 180, "SINGLE"); shot("before_back_tex", 180, "TEXTURE"); shot("before_front_tex", 0, "TEXTURE"); shot("before_front_clay", 0, "SINGLE")
shot("before_cut", 90, "SINGLE", cut=True)
ca = me.color_attributes.new("what", "BYTE_COLOR", "CORNER")
cols = np.tile(np.array([0.6, 0.6, 0.6, 1.0]), (nf, 1))
cols[doomed] = (1.0, 0.0, 0.8, 1.0); cols[band] = (1.0, 0.8, 0.0, 1.0)
ca.data.foreach_set("color", np.repeat(cols, 3, axis=0).ravel())
me.color_attributes.active_color = ca
shot("map_cut", 90, "VERTEX", cut=True); shot("map_back", 180, "VERTEX")
me.color_attributes.remove(ca)

# ---------------------------------------- the back: even out the small bumps, keep the real edges
wv = np.zeros(len(X))
if SMOOTH_BACK:
    outer = ~inner
    of = np.nonzero(outer)[0]
    vfaces = [[] for _ in range(len(X))]
    for i in of:
        for v in F[i]: vfaces[v].append(int(i))
    # a smooth share for each point: its height doesn't matter, only how far back it is and which way it faces
    vn = np.zeros_like(X); np.add.at(vn, F[of].ravel(), np.repeat(N[of] * A[of][:, None], 3, axis=0))
    vn /= np.maximum(np.linalg.norm(vn, axis=1), 1e-12)[:, None]
    def ramp(a, b, x):
        t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
    facing = ramp(-0.2, 0.25, vn[:, 1])
    vpairs = np.array([k for k in edge_faces.keys()], dtype=np.int64)
    for _ in range(6):   # no patchy edges between one bump's sides
        s = np.zeros(len(X)); c = np.zeros(len(X))
        np.add.at(s, vpairs[:, 0], facing[vpairs[:, 1]]); np.add.at(s, vpairs[:, 1], facing[vpairs[:, 0]])
        np.add.at(c, vpairs[:, 0], 1); np.add.at(c, vpairs[:, 1], 1)
        facing = (facing + s) / (1 + c)
    wv = np.ones(len(X)) if SMOOTH_ALL else ramp(BACK_FROM * H, BACK_TO * H, X[:, 1] - cen.y) * ramp(0.15, 0.5, facing)
    if ABOVE is not None:
        if ZPIECE is None: wv = wv * ramp(ABOVE - 0.03 * H, ABOVE, X[:, 2])
        else: wv = wv * ramp(ABOVE - 0.056, ABOVE, ZPIECE[first])   # (a placed piece: still by height on the piece as Meshy made it)
    # the rims stay where they are, and smoothing fades in away from them so nothing tears
    rim = np.unique(F[inner].ravel())
    vd = np.full(len(X), np.inf); vd[rim] = 0.0
    vn_adj = [[] for _ in range(len(X))]
    for p, q in vpairs.tolist(): vn_adj[p].append(q); vn_adj[q].append(p)
    pq = [(0.0, int(v)) for v in rim]; heapq.heapify(pq)
    while pq:
        d0, v = heapq.heappop(pq)
        if d0 > vd[v]: continue
        for g in vn_adj[v]:
            nd = d0 + float(np.linalg.norm(X[v] - X[g]))
            if nd < vd[g] and nd < RIM_FADE * H: vd[g] = nd; heapq.heappush(pq, (nd, g))
    wv *= ramp(0.0, RIM_FADE * H, np.where(np.isfinite(vd), vd, RIM_FADE * H))
    wv[[len(f) == 0 for f in vfaces]] = 0.0
    # leather or metal, from the model's own pictures: the two are never blended, so straps keep their edges
    mr = next(im for im in piece_images if "metallic" in im.name)
    Mp = np.empty(mr.size[0] * mr.size[1] * 4, dtype=np.float32); mr.pixels.foreach_get(Mp); Mp = Mp.reshape(mr.size[1], mr.size[0], 4)
    uv = np.empty(nl * 2); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    uvc = uv[LS[:, None] + np.arange(3)]
    samples = np.concatenate([uvc, uvc.mean(1, keepdims=True), (uvc + uvc.mean(1, keepdims=True)) / 2], axis=1)
    xi = (np.mod(samples[..., 0], 1) * (mr.size[0] - 1)).astype(int); yi = (np.mod(samples[..., 1], 1) * (mr.size[1] - 1)).astype(int)
    leather = np.median(Mp[yi, xi, 2], axis=1) < 0.5
    print("PCL leather faces: %d" % leather[of].sum())
    pairs = set()
    for fs in vfaces:
        for p in fs:
            for q in fs:
                if p < q: pairs.add((p, q))
    I = np.array([p[0] for p in pairs]); J = np.array([p[1] for p in pairs])
    same = leather[I] == leather[J]
    cor_f = np.repeat(of, 3); cor_v = F[of].ravel()
    for r in range(ROUNDS):
        N, C, A = face_data(X)
        d = np.linalg.norm(C[I] - C[J], axis=1); sc = float(d.mean())
        Ws = np.exp(-d * d / (2 * sc * sc))
        nfilt = N.copy()
        for _ in range(NORMAL_STEPS):
            dn = np.linalg.norm(nfilt[I] - nfilt[J], axis=1)
            Wg = Ws * np.exp(-dn * dn / (2 * NORMAL_SIGMA ** 2)) * same
            s = nfilt * A[:, None]
            np.add.at(s, I, nfilt[J] * (A[J] * Wg)[:, None]); np.add.at(s, J, nfilt[I] * (A[I] * Wg)[:, None])
            nfilt = s / np.maximum(np.linalg.norm(s, axis=1), 1e-12)[:, None]
        for _ in range(MOVE_STEPS):
            Cn = X[F].mean(1)
            delta = (nfilt[cor_f] * ((nfilt[cor_f] * (Cn[cor_f] - X[cor_v])).sum(1))[:, None])
            s = np.zeros_like(X); c = np.zeros(len(X))
            np.add.at(s, cor_v, delta); np.add.at(c, cor_v, 1)
            X += (s / np.maximum(c, 1)[:, None]) * wv[:, None]
            off = X - X0; ln_ = np.linalg.norm(off, axis=1)
            X = X0 + off * np.minimum(1.0, MAX_MOVE * H / np.maximum(ln_, 1e-12))[:, None]
    moved = np.linalg.norm(X - X0, axis=1)
    fr = (X0[:, 1] - cen.y) <= BACK_FROM * H
    print("PCL smoothed: %d points moved, the most %.2f%% of the height; on the front half the most is %.5f%%" % ((moved > 1e-7).sum(), 100 * moved.max() / H, 100 * moved[fr].max() / H))
    me.vertices.foreach_set("co", X[W].ravel()); me.update()
    # shading follows the new shape: hard edges only where faces really turn
    N, C, A = face_data(X)
    keep_f = np.nonzero(~doomed)[0]
    vall = [[] for _ in range(len(X))]
    for i in keep_f:
        for v in F[i]: vall[v].append(int(i))
    limit = math.cos(math.radians(CREASE_DEG))
    def corner_angle(f, v):
        ids = F[f].tolist(); k = ids.index(v)
        a_, b_ = X[ids[(k + 1) % 3]] - X[v], X[ids[(k + 2) % 3]] - X[v]
        return math.atan2(np.linalg.norm(np.cross(a_, b_)), a_.dot(b_))
    new_n = orig_n.copy()
    shade = wv.copy()
    if BLUNTED is not None: shade[W[BLUNTED]] = 1.0   # the blunted knee's shape changed, so its shading must follow even on the front
    for fi in keep_f:
        for j in range(3):
            v = int(F[fi, j]); w_ = shade[v]
            if w_ <= 0: continue
            s = np.zeros(3)
            for g in vall[v]:
                if N[g].dot(N[fi]) >= limit: s += N[g] * corner_angle(g, v)
            s /= max(np.linalg.norm(s), 1e-12)
            l = LS[fi] + j
            m = orig_n[l] * (1 - w_) + s * w_
            new_n[l] = m / max(np.linalg.norm(m), 1e-12)
else:
    new_n = orig_n

# ---------------------------------------- strip
bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[bm.faces[int(i)] for i in np.nonzero(doomed)[0]], context="FACES")
bm.to_mesh(me); bm.free(); me.update()
ln = []
for i in np.nonzero(~doomed)[0]:
    for j in range(3): ln.append(new_n[LS[i] + j].tolist())
me.normals_split_custom_set(ln); me.update()
print("PCL end: %d triangles (from %d, %.0f%% gone)" % (len(me.polygons), nf, 100 * (1 - len(me.polygons) / nf)))

shot("after_back_clay", 180, "SINGLE"); shot("after_back_tex", 180, "TEXTURE"); shot("after_front_tex", 0, "TEXTURE"); shot("after_front_clay", 0, "SINGLE")
shot("after_cut", 90, "SINGLE", cut=True)
o.name = name
if RIG:
    # how much of the body sticks out through the plate, by height (the body's mail will hide under worn pieces in the game)
    tree = BVHTree.FromPolygons([v.co[:] for v in me.vertices], [list(p.vertices) for p in me.polygons])
    dg = bpy.context.evaluated_depsgraph_get(); e = body.evaluated_get(dg); m_ = e.to_mesh()
    nb = np.array([(body.matrix_world.to_3x3() @ v.normal).normalized()[:] for v in m_.vertices]); e.to_mesh_clear()
    zlo, zhi = float(X[:, 2].min()), float(X[:, 2].max())
    poke = {}
    for co, n in zip(Vb.tolist(), nb.tolist()):
        if co[2] < zlo or co[2] > zhi: continue
        co_, n_ = Vector(co), Vector(n)
        if tree.ray_cast(co_, -n_, 0.06)[0] is not None and tree.ray_cast(co_, n_, 0.09)[0] is None:
            k = round(co[2] * 10) / 10; poke[k] = poke.get(k, 0) + 1
    print("PCL body points outside the plate, by height:", sorted(poke.items()))
    # weights: each point takes those of the nearest body surface, limited to this slot's bones
    allow = {"mixamorig:" + n for n in ALLOWED[SLOT]}
    wmb = body.matrix_world
    bvb = BVHTree.FromPolygons([(wmb @ v.co)[:] for v in body.data.vertices], [list(p.vertices) for p in body.data.polygons])
    gn = {g.index: g.name for g in body.vertex_groups}
    bw = [{gn[g.group]: g.weight for g in v.groups} for v in body.data.vertices]
    bpolys = body.data.polygons; bverts = [wmb @ v.co for v in body.data.vertices]
    groups_ = {}
    def grp(n):
        if n not in groups_: groups_[n] = o.vertex_groups.new(name=n)
        return groups_[n]
    for v in me.vertices:
        loc, _, idx, _ = bvb.find_nearest(v.co)
        pv = bpolys[idx].vertices
        w_ = np.array([1.0 / max((bverts[i] - loc).length, 1e-4) for i in pv]); w_ /= w_.sum()
        acc = {}
        wrong = "mixamorig:Right" if v.co.x > 0 else "mixamorig:Left"   # the knight's left is +X
        for k, i in enumerate(pv):
            for n, wt in bw[i].items():
                if n in allow and not (SLOT == "Legs" and n.startswith(wrong)): acc[n] = acc.get(n, 0.0) + wt * w_[k]
        if not acc:
            best = min((n for n in allow if n in arm.data.bones and not (SLOT == "Legs" and n.startswith(wrong))), key=lambda n: (v.co - arm.matrix_world @ arm.data.bones[n].head_local).length)
            acc = {best: 1.0}
        top = sorted(acc.items(), key=lambda kv: -kv[1])[:4]; tot = sum(x[1] for x in top)
        top = dict((n, wt / tot) for n, wt in top)
        if SLOT == "Chest":
            # Plates don't bend, and being pulled by both thighs crumples them: the skirt hangs from the belt on the hips and
            # swings by a share of its own side's thigh that grows toward the hem. The middle plates follow only the hips.
            skirt_top = zlo + SKIRT_TOP * H
            k = min(1.0, max(0.0, (skirt_top + SKIRT_BLEND * H - v.co.z) / (SKIRT_BLEND * H)))
            if k > 0:
                def sm(t):
                    t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)
                s_ = THIGH_CAP * sm((skirt_top - v.co.z) / (skirt_top - zlo)) * sm((abs(v.co.x) - 0.05) / 0.07)
                sk = {"mixamorig:Hips": 1.0 - s_}
                if s_ > 0: sk["mixamorig:LeftUpLeg" if v.co.x > 0 else "mixamorig:RightUpLeg"] = s_
                mix = {n: top.get(n, 0.0) * (1 - k) + sk.get(n, 0.0) * k for n in set(top) | set(sk)}
                top = dict(sorted(mix.items(), key=lambda kv: -kv[1])[:4])
                tot = sum(top.values()); top = {n: wt / tot for n, wt in top.items()}
        if SLOT == "Legs":
            # the knee plate is one solid thing: an even share of thigh and shin keeps it whole at the middle of the bend.
            # The model has no separate knee part, so it is picked by where it sits, in front of the joint.
            def sm(t):
                t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)
            k = sm((KNEE_Y - v.co.y) / KNEE_FADE) * sm((v.co.z - KNEE_LO) / KNEE_FADE) * sm((KNEE_HI - v.co.z) / KNEE_FADE)
            if k > 0:
                sd = "mixamorig:Left" if v.co.x > 0 else "mixamorig:Right"
                sk = {sd + "UpLeg": 0.5, sd + "Leg": 0.5}
                mix = {n: top.get(n, 0.0) * (1 - k) + sk.get(n, 0.0) * k for n in set(top) | set(sk)}
                top = dict(sorted(mix.items(), key=lambda kv: -kv[1])[:4])
                tot = sum(top.values()); top = {n: wt / tot for n, wt in top.items()}
        if ARM_W is not None:
            # the same shares the fit used: solid on each bone, and the elbow plate half and half (it stays one piece and stays over the point of the elbow)
            sd = "mixamorig:Left" if v.co.x > 0 else "mixamorig:Right"
            top = {sd + b_: float(w_) for b_, w_ in zip(("Arm", "ForeArm", "Hand"), ARM_W[v.index]) if w_ > 1e-3}
            tot = sum(top.values()); top = {n: wt / tot for n, wt in top.items()}
        for n, wt in top.items(): grp(n).add([v.index], wt, "REPLACE")
    # into the body's own space, under the armature
    me.transform(arm.matrix_world.inverted())
    o.parent = arm; o.matrix_parent_inverse.identity(); o.matrix_basis.identity()
    md = o.modifiers.new("Armature", "ARMATURE"); md.object = arm
    body.hide_render = False
    print("PCL weights on", sorted(g.name.replace("mixamorig:", "") for g in o.vertex_groups))
    arm.data.pose_position = "POSE"
bpy.data.objects.remove(cam)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, name + "_clean.blend"))
print("PCL saved")
