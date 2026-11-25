# The troll's own armour (2026-11-22, owner: "work in Meshy to create a troll helmet, chest plate, gauntlets, boots, and shoulder armour").
# Fits the five Meshy pieces (Art/TrollKit/troll_<piece>.glb, decimated by Tools/troll_kit_prep.py into Saved/TrollKit/lo_<piece>.glb)
# onto the troll's own body at his bind pose (Art/People/Troll/Troll_rigged.glb), skins them to his own 24-bone rig, splits each piece
# into its iron ("<Piece>_Metal": takes the tier's colour in the game) and its leather and fur ("<Piece>_Soft"), and writes
# Saved/TrollKit/Out/SK_TrollArmour.fbx with each piece's pictures for Tools/troll_kit_import.py.
#
#   blender -b -P troll_armour_fit.py -- <Troll_rigged.glb> <lo folder> <out folder> [render]
#
# How a piece is fitted: turned to stand as worn, laid on its part of the body by the bounding box of that part's skin (the bones' own
# vertices; a limb piece is laid along its bone first), pushed out wherever it dips into the skin (Clear cm over it), then given the
# skin's own bone weights from the nearest point of the body (so it moves exactly as the skin under it), smoothed. The gauntlet and boot
# hands and toes Meshy painted green are cut off (his own hands and toes show). The single gauntlet, boot and shoulder are fitted on his
# right side and mirrored to the left. Mesh space: Blender metres, +X his left, -Y his front, +Z up.
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector, Matrix, Quaternion
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
RIG, LO, OUT = argv[0], argv[1], argv[2]
RENDER = "render" in argv[3:]
os.makedirs(OUT, exist_ok=True)
TEX = 1024

def log(s): print("TKFIT " + s)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=RIG)
for o in list(bpy.data.objects):
    if o.type == 'MESH' and not any(m.type == 'ARMATURE' for m in o.modifiers): bpy.data.objects.remove(o)   # (Meshy's stray icosphere)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
arm.name = "Armature"
body = next(o for o in bpy.data.objects if o.type == 'MESH')
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
BW = body.matrix_world
bpts = np.array([tuple(BW @ v.co) for v in body.data.vertices])
bgrp = [body.vertex_groups[max(v.groups, key=lambda g: g.weight).group].name if v.groups else "" for v in body.data.vertices]
bm_body = bmesh.new(); bm_body.from_mesh(body.data); bm_body.transform(BW)
bm_body.normal_update()
bvh = BVHTree.FromBMesh(bm_body)
_i = int(np.argmax(bpts[:, 0]))
_loc, _n, _, _ = bvh.find_nearest(Vector(bpts[_i]) + Vector((0.05, 0, 0)))
NSIGN = 1.0 if _n.x > 0 else -1.0
print("TKFIT body normals sign %d" % NSIGN)
def joint(n): return arm.matrix_world @ arm.data.bones[n].head_local

def region(groups, xsign=0):
    sel = [i for i, g in enumerate(bgrp) if g in groups and (xsign == 0 or bpts[i][0] * xsign > 0)]
    return bpts[sel]

# ---------------- pieces ----------------
# Each: the Meshy file, the slot name, how it is turned first (degrees about X, Y, Z), and how it is laid on the body.
#   box: the part of the body (bone groups) whose skin's box it is laid on, with that box grown by Grow (x, y, z) and moved by Shift (m)
#   limb: (top joint, bottom joint, groups): laid along that bone, length share Len, its cross-section fitted to the skin round it x Grow
PIECES = [
    # (boxes measured off his body in slices: head y -0.57..-0.2 at z 1.65-2.06, torso x +-0.42, y -0.23 (chest) .. 0.5 (his hunched back))
    # (2026-11-24: weld / smooth: see repair(); poseclear: the clearance kept in the test poses, see pose_clear(); taper: (from, margin), see taper_top().
    # The helmet's model is a heap of loose plates and fur strands: it is welded but left flat-shaded, smooth shading across its shards looked worse.)
    dict(name="Helmet", file="helmet", fixed=((-0.26, -0.63, 1.73), (0.26, -0.08, 2.14)), clear=0.012, rigid="Head", noconform=True)   # (2026-11-25: the clean Meshy remesh, one connected mesh, so smooth shading is fine),
    dict(name="Chest", file="chest", fixed=((-0.50, -0.33, 1.08), (0.50, 0.62, 1.86)), clear=0.015, poseclear=(0.012, 0.05, ["Hips", "Spine", "Spine01", "Spine02", "neck", "RightShoulder"]), poseonly=(lambda co: co.y > 0.2), allow=["Hips", "Spine", "Spine01", "Spine02", "neck", "RightShoulder"]),
    dict(name="Shoulders", file="shoulder", rot=(0, 0, -90), fixed=((-0.90, -0.18, 1.74), (-0.38, 0.52, 2.24)), mirror=True, clear=0.015, allow=["RightShoulder", "RightArm", "Spine02"]),
    dict(name="Arms", file="gauntlet", limb=("RightForeArm", "RightHand", ["RightForeArm"]), grow=1.18, len=1.05, cut_skin=True, clear=0.012, mirror=True, allow=["RightForeArm", "RightHand"]),
    dict(name="Legs", file="boot", limb=("RightLeg", "RightToeBase", ["RightLeg", "RightFoot"]), grow=1.15, len=1.08, cut_skin=True, clear=0.012, taper=(0.42, 0.03), poseclear=(0.006, 0.025, ["RightLeg", "RightFoot", "RightToeBase"]), mirror=True, allow=["RightLeg", "RightFoot"]),   # (not the toe bone: the boot is stiff, and the bits of it that followed his toe joint swung out under and behind his foot as he ran)
]
ONLY = os.environ.get("TK_ONLY", "")

def load_piece(fn):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(LO, "lo_%s.glb" % fn))
    new = [o for o in bpy.data.objects if o not in before]
    ob = next(o for o in new if o.type == 'MESH')
    for o in new:
        if o != ob: bpy.data.objects.remove(o)
    ob.parent = None
    ob.matrix_world = Matrix.Identity(4)
    return ob

def verts_world(ob): return np.array([tuple(ob.matrix_world @ v.co) for v in ob.data.vertices])

def apply_matrix(ob, M):
    ob.data.transform(M); ob.data.update()

def tex_kind(n):
    """Which of the piece's pictures a texture node is, by where its colour goes (Meshy's glTF names them Image_0..2)."""
    seen, todo = set(), [l.to_socket for o in n.outputs for l in o.links]
    while todo:
        sk = todo.pop()
        nd = sk.node
        if nd.type == 'NORMAL_MAP' or sk.name == "Normal": return "Normal"
        if nd.type == 'BSDF_PRINCIPLED':
            return "Base" if sk.name == "Base Color" else "MR"
        if nd in seen: continue
        seen.add(nd)
        todo += [l.to_socket for o in nd.outputs for l in o.links]
    return ""

def face_colours(ob):
    """Each face's base colour and metallic at its UV middle, from the piece's own pictures."""
    mat = ob.material_slots[0].material
    base = mr = None
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            k = tex_kind(n)
            if k == "MR": mr = n.image
            elif k == "Base": base = n.image
    def arr(im):
        if im is None: return None
        w, h = im.size
        return np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
    B, M = arr(base), arr(mr)
    uv = ob.data.uv_layers.active.data
    cols, mets = [], []
    for p in ob.data.polygons:
        u = sum((uv[l].uv for l in p.loop_indices), Vector((0, 0))) / len(p.loop_indices)
        def at(A):
            h, w = A.shape[:2]
            return A[min(h - 1, max(0, int(u.y * h))), min(w - 1, max(0, int(u.x * w)))]
        cols.append(at(B)[:3] if B is not None else (0.5, 0.5, 0.5))
        mets.append(at(M)[2] if M is not None else 0.0)
    return np.array(cols), np.array(mets), base, mr

def cut_skin(ob):
    """Meshy painted his green toes and fingers into the boot and gauntlet: those faces go (his own show)."""
    cols, _, _, _ = face_colours(ob)
    r, g, b = cols[:, 0], cols[:, 1], cols[:, 2]
    mx = cols.max(1); mn = cols.min(1); sat = (mx - mn) / np.maximum(mx, 1e-4)
    skin = (g >= r * 0.98) & (g > b * 1.12) & (sat > 0.12) & (mx > 0.08)
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.faces.ensure_lookup_table()
    gone = [bm.faces[i] for i in np.nonzero(skin)[0]]
    bmesh.ops.delete(bm, geom=gone, context='FACES')
    # (and the little islands left behind)
    bm.to_mesh(ob.data); bm.free(); ob.data.update()
    log("%s: cut %d skin faces" % (ob.name, len(gone)))

def lay_in_box(ob, lo, hi):
    P = verts_world(ob)
    plo, phi = P.min(0), P.max(0)
    s = (hi - lo) / np.maximum(phi - plo, 1e-5)
    M = Matrix.Translation(Vector((lo + hi) / 2)) @ Matrix.Diagonal((s[0], s[1], s[2], 1)) @ Matrix.Translation(-Vector((plo + phi) / 2))
    apply_matrix(ob, M)

def principal(P):
    c = P.mean(0); u, s, vt = np.linalg.svd(P - c, full_matrices=False)
    return c, Vector(vt[0])

def lay_limb(ob, top, bottom, groups, grow, length):
    """Along the bone from top to bottom: the piece's long axis (top end where it is widest of fur, i.e. +Z as Meshy made it) onto the bone."""
    A, Bj = joint(top), joint(bottom)
    P = verts_world(ob)
    c, ax = principal(P)
    if ax.z < 0: ax = -ax                                   # (Meshy stands them up: the top is +Z)
    q = ax.rotation_difference(Vector((0, 0, 1)))
    apply_matrix(ob, Matrix.Translation(-Vector(c)))
    apply_matrix(ob, q.to_matrix().to_4x4())                # long axis on +Z
    # now +Z (top) onto the bone pointing up (bottom to top), keeping the front ahead (-Y)
    up = (A - Bj).normalized()
    q2 = Vector((0, 0, 1)).rotation_difference(up)
    apply_matrix(ob, q2.to_matrix().to_4x4())
    # sizes: the skin round the bone in the bone's frame
    R = np.array([tuple(p) for p in region(groups, -1 if top.startswith("Right") else 1)])
    side = up.cross(Vector((0, -1, 0))).normalized(); front = side.cross(up).normalized()
    def ext(P, a): v = P @ np.array(tuple(a)); return v.min(), v.max()
    P = verts_world(ob)
    mid = (A + Bj) / 2
    L = (A - Bj).length * length
    want = {}
    for nm, a in (("side", side), ("front", front)):
        lo, hi = ext(R, a); want[nm] = ((hi - lo) * grow, (hi + lo) / 2)
    out = Matrix.Identity(4)
    S = []
    for nm, a in (("side", side), ("front", front), ("up", up)):
        lo, hi = ext(P, a)
        tgt = L if nm == "up" else want[nm][0]
        S.append(tgt / max(hi - lo, 1e-5))
    # scale in the bone frame
    F = Matrix((tuple(side), tuple(front), tuple(up))).transposed()   # columns: the frame's axes
    Sm = F @ Matrix.Diagonal(S) @ F.transposed()
    P = verts_world(ob); c0 = Vector(P.mean(0))
    apply_matrix(ob, Matrix.Translation(c0) @ Sm.to_4x4() @ Matrix.Translation(-c0))
    # place: the middle along the bone, centred on the skin's middle across it
    P = verts_world(ob)
    cur = Vector(P.mean(0))
    tgt = mid + side * (want["side"][1] - mid.dot(side)) + front * (want["front"][1] - mid.dot(front))
    apply_matrix(ob, Matrix.Translation(tgt - cur))

def conform(ob, clear):
    """Wherever the piece dips into the skin or comes within Clear of it, it is pushed out along the skin's normal."""
    me = ob.data; n = 0
    for v in me.vertices:
        p = Vector(v.co)
        loc, nrm, idx, d = bvh.find_nearest(p)
        if loc is None: continue
        nrm = nrm * NSIGN
        s = (p - loc).dot(nrm)
        if s < clear and d < 0.05:
            v.co = loc + nrm * clear; n += 1
    me.update()
    return n

def inside_frac(ob, clear):
    n = k = 0
    for v in ob.data.vertices:
        loc, nrm, idx, d = bvh.find_nearest(v.co)
        if loc is None or d > 0.2: continue
        n += 1
        if (v.co - loc).dot(nrm * NSIGN) < clear * 0.5: k += 1
    return k / max(n, 1)

def inflate(ob, clear, axis=None, limit=1.3):
    """Grown as a whole (across its axis) until almost none of it is in the skin; the last few points are pushed out after."""
    P = verts_world(ob); c = Vector(P.mean(0)); g = 1.0
    while g < limit:
        f = inside_frac(ob, clear)
        if f < 0.04: break
        if axis is None: S = Matrix.Diagonal((1.04, 1.04, 1.0, 1.0))
        else:
            a = axis.normalized(); S = (Matrix.Identity(3) * 1.04 - (0.04) * Matrix(((a.x*a.x, a.x*a.y, a.x*a.z), (a.y*a.x, a.y*a.y, a.y*a.z), (a.z*a.x, a.z*a.y, a.z*a.z)))).to_4x4()
        apply_matrix(ob, Matrix.Translation(c) @ S @ Matrix.Translation(-c)); g *= 1.04
    log("%s: grown x%.2f, %.0f%% still in the skin" % (ob.name, g, 100 * inside_frac(ob, clear)))

def keep_bones(ob, allow):
    """Only the bones the piece is worn on keep weights (a gauntlet near his hip at the bind pose must not follow the hip); a point left with none
    takes the nearest allowed bone. Allowed names are given for his right side; Left is added."""
    names = set(allow) | {a.replace("Right", "Left") for a in allow}
    idx = {g.index: g.name for g in ob.vertex_groups}
    segs = [(n, joint(n), arm.matrix_world @ arm.data.bones[n].tail_local) for n in names]
    for v in ob.data.vertices:
        ws = [(idx[g.group], g.weight) for g in v.groups]
        keep = [(n, w) for n, w in ws if n in names and w > 0]
        for n, w in ws:
            if n not in names: ob.vertex_groups[n].remove([v.index])
        tot = sum(w for _, w in keep)
        if tot <= 1e-4:
            p = ob.matrix_world @ v.co
            best = min(segs, key=lambda s_: (p - (s_[1] + (s_[2] - s_[1]) * max(0, min(1, (p - s_[1]).dot(s_[2] - s_[1]) / max((s_[2] - s_[1]).length_squared, 1e-8))))).length)
            ob.vertex_groups[best[0]].add([v.index], 1.0, 'REPLACE')
        else:
            for n, w in keep: ob.vertex_groups[n].add([v.index], w / tot, 'REPLACE')

def skin_to_body(ob, rigid=None, allow=None):
    ob.parent = arm
    ob.matrix_parent_inverse = arm.matrix_world.inverted()
    for g in list(ob.vertex_groups): ob.vertex_groups.remove(g)
    for b in arm.data.bones: ob.vertex_groups.new(name=b.name)
    if rigid:
        ob.vertex_groups[rigid].add(range(len(ob.data.vertices)), 1.0, 'REPLACE')
    else:
        dt = ob.modifiers.new("W", 'DATA_TRANSFER'); dt.object = body
        dt.use_vert_data = True; dt.data_types_verts = {'VGROUP_WEIGHTS'}; dt.vert_mapping = 'POLYINTERP_NEAREST'
        dt.layers_vgroup_select_src = 'ALL'; dt.layers_vgroup_select_dst = 'NAME'
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier="W")
        # smooth so the shell bends as one piece, not pinched along the skin's own folds
        bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
        bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
        bpy.ops.object.vertex_group_smooth(group_select_mode='ALL', factor=0.6, repeat=6)
        bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
        bpy.ops.object.vertex_group_normalize_all(lock_active=False)
        bpy.ops.object.mode_set(mode='OBJECT')
    if allow: keep_bones(ob, allow)
    m = ob.modifiers.new("Armature", 'ARMATURE'); m.object = arm

def split_metal(ob, name):
    """Two slots: the iron (Meshy's metallic over a half) and the leather and fur."""
    cols, mets, base, mr = face_colours(ob)
    src = ob.material_slots[0].material
    metal = src.copy(); metal.name = name + "_Metal"
    soft = src.copy(); soft.name = name + "_Soft"
    ob.data.materials.clear(); ob.data.materials.append(metal); ob.data.materials.append(soft)
    for p, m in zip(ob.data.polygons, mets): p.material_index = 0 if m > 0.45 else 1
    log("%s: %d iron faces, %d leather and fur" % (name, int((mets > 0.45).sum()), int((mets <= 0.45).sum())))
    # the pictures, for the importer
    for im, kind in ((base, "Base"), (mr, "MR")):
        if im is None: continue
        c = im.copy(); c.scale(TEX, TEX); c.filepath_raw = os.path.join(OUT, "T_TK_%s_%s.png" % (name, kind)); c.file_format = 'PNG'; c.save(); bpy.data.images.remove(c)
    for n in src.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image and tex_kind(n) == "Normal":
            c = n.image.copy(); c.scale(TEX, TEX); c.filepath_raw = os.path.join(OUT, "T_TK_%s_Normal.png" % name); c.file_format = 'PNG'; c.save(); bpy.data.images.remove(c)

def mirror_copy(ob):
    """The right piece mirrored to his left, joined into one object."""
    c = ob.copy(); c.data = ob.data.copy(); bpy.context.scene.collection.objects.link(c)
    c.data.transform(Matrix.Diagonal((-1, 1, 1, 1))); c.data.flip_normals(); c.data.update()
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); c.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.join()
    return ob

# ---------------- 2026-11-24: repairs (owner: armour "looks cracked and sinks into his body", "boot tops too wide", chest "sinks into his back when he runs") ----------------
def repair(ob, weld, smooth):
    """Meshy's pieces come as loose triangles (the chest: 16000 triangles in 595 separate bits, most edges open), so every later step that
    moved points one by one pulled the triangles apart and his skin showed through the cracks. The points are welded first (Weld, in the
    model's own units of about 2 across), so neighbours share their corners and move together; with Smooth the faces are then shaded
    smooth below 50 degrees instead of every triangle flat."""
    bm = bmesh.new(); bm.from_mesh(ob.data)
    n0 = len(bm.verts); e0 = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=weld)
    e1 = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    bm.to_mesh(ob.data); bm.free(); ob.data.update()
    if smooth:
        for p in ob.data.polygons: p.use_smooth = True
        ob.data.set_sharp_from_angle(angle=math.radians(50))
    log("%s: welded %d -> %d points, open edges %d -> %d" % (ob.name, n0, len(ob.data.vertices), e0, e1))

def taper_top(ob, top, bottom, groups, start, margin):
    """The boot's top fitted to his calf (owner: "boot tops too wide: fit them to the calves; the lower half of the boot is fine"). The
    boot was grown as a whole until the foot cleared his skin, which left the top a wide ring far off the calf. From Start (share of the
    way up from the ankle to the knee) the boot is drawn in toward the middle of the calf, more with height, until at the top its inner
    wall stands Margin off the calf's skin; below Start nothing moves."""
    A, Bj = joint(top), joint(bottom)
    up = (A - Bj).normalized(); L = (A - Bj).length
    R = region(groups, -1 if top.startswith("Right") else 1)
    upn = np.array(tuple(up)); b0 = np.array(tuple(Bj))
    def frame(P):
        t = (P - b0) @ upn / L
        return t
    ts = frame(R)
    band = R[(ts > 0.72) & (ts < 1.0)]
    if len(band) < 10: log("%s: no calf skin found for the taper" % ob.name); return
    def radial(P, c):
        d = P - c; d = d - np.outer(d @ upn, upn); return d
    c_top = band.mean(0)
    low = R[(ts > start - 0.1) & (ts < start + 0.1)]
    c_low = low.mean(0) if len(low) > 10 else c_top
    rs = np.linalg.norm(radial(band, c_top), axis=1)
    P = verts_world(ob); tp = frame(P)
    topv = P[(tp > 0.72) & (tp < 1.0)]
    if len(topv) < 10: log("%s: no boot top found for the taper" % ob.name); return
    rb = np.linalg.norm(radial(topv, c_top), axis=1)
    Rs, Rb = float(np.percentile(rs, 90)), float(np.percentile(rb, 15))
    k = max(0.5, min(1.0, (Rs + margin) / max(Rb, 1e-4)))
    for v in ob.data.vertices:
        p = np.array(tuple(ob.matrix_world @ v.co)); t = float((p - b0) @ upn / L)
        if t <= start: continue
        w = min(1.0, max(0.0, (t - start) / max(0.85 - start, 1e-3))); w = w * w * (3 - 2 * w)
        c = c_low + (c_top - c_low) * min(1.0, max(0.0, (t - start) / max(0.86 - start, 1e-3)))
        along = c + upn * float((p - c) @ upn)
        q = along + (p - along) * (1 + (k - 1) * w)
        v.co = ob.matrix_world.inverted() @ Vector(q)
    ob.data.update()
    log("%s: top drawn in x%.2f (calf %.1f cm, the boot's inner wall was %.1f cm from its middle)" % (ob.name, k, Rs * 100, Rb * 100))

# Test poses for the armour (world axes: +X his left, -Y his front, +Z up; degrees about each bone's own joint, parents first). A lean
# forward is + about X; an arm swung forward is - about X; his right arm lifted sideways is + about Y, his left - about Y.
POSES = {
    "runA": [("Spine", 'X', 10), ("Spine01", 'X', 10), ("Spine02", 'X', 10), ("Spine02", 'Z', 10), ("RightArm", 'X', -40), ("LeftArm", 'X', 30), ("RightUpLeg", 'X', -35), ("LeftUpLeg", 'X', 25), ("RightLeg", 'X', 40), ("LeftLeg", 'X', 55), ("RightFoot", 'X', -20), ("LeftFoot", 'X', 25)],
    "runB": [("Spine", 'X', 10), ("Spine01", 'X', 10), ("Spine02", 'X', 10), ("Spine02", 'Z', -10), ("RightArm", 'X', 30), ("LeftArm", 'X', -40), ("RightUpLeg", 'X', 25), ("LeftUpLeg", 'X', -35), ("RightLeg", 'X', 55), ("LeftLeg", 'X', 40), ("RightFoot", 'X', 25), ("LeftFoot", 'X', -20)],
    "hunch": [("Spine", 'X', 14), ("Spine01", 'X', 14), ("Spine02", 'X', 12), ("RightShoulder", 'Z', -14), ("LeftShoulder", 'Z', 14), ("RightArm", 'X', -55), ("LeftArm", 'X', -55), ("neck", 'X', -15), ("Head", 'X', -12)],
    "back": [("Spine", 'X', -8), ("Spine01", 'X', -8), ("Spine02", 'X', -8), ("RightArm", 'X', 35), ("LeftArm", 'X', 35), ("Head", 'X', 10)],
    "armsup": [("RightArm", 'Y', 65), ("LeftArm", 'Y', -65), ("RightForeArm", 'X', -40), ("LeftForeArm", 'X', -40)],
    "wind": [("Spine01", 'Z', -18), ("Spine02", 'Z', -18), ("RightArm", 'Y', 80), ("RightArm", 'X', 30), ("LeftArm", 'X', -60)],
    "twistL": [("Spine01", 'Z', 18), ("Spine02", 'Z', 18), ("RightArm", 'X', -70), ("RightForeArm", 'X', -30)],
}

def set_pose(pose):
    arm.data.pose_position = "POSE"
    for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    Aw = arm.matrix_world
    for name, axis, deg in (pose or []):
        pb = arm.pose.bones.get(name)
        if pb is None: continue
        Mw = Aw @ pb.matrix
        h = Mw.to_translation()
        pb.matrix = Aw.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ Mw)
        bpy.context.view_layer.update()

def posed_points(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    e = ob.evaluated_get(dg); me = e.to_mesh()
    P = [e.matrix_world @ v.co for v in me.vertices]
    e.to_mesh_clear()
    return P

def posed_bvh(ob, groups=None):
    """His skin as posed now; with Groups, only the part of it that mostly follows those bones (a chest plate is not to be pushed about by
    an arm swinging through its armhole)."""
    dg = bpy.context.evaluated_depsgraph_get()
    e = ob.evaluated_get(dg); me = e.to_mesh()
    bm = bmesh.new(); bm.from_mesh(me); bm.transform(e.matrix_world)
    if groups:
        bm.verts.ensure_lookup_table()
        gone = [f for f in bm.faces if sum(1 for v in f.verts if bgrp[v.index] in groups) * 2 < len(f.verts)]
        bmesh.ops.delete(bm, geom=gone, context='FACES')
    bm.normal_update()
    t = BVHTree.FromBMesh(bm)
    e.to_mesh_clear()
    return t, bm

def pose_clear(ob, clear, rounds=3, most=0.07, groups=None, only=None):
    """The piece checked on his body in the test poses above (a run both ways, a hunch, leaning back, arms up, a wind-up, a twist): wherever
    one of its points ends up under his skin, or within Clear of it, that point is moved out by what is missing along the skin's normal at
    the bind pose, with the amount spread to its neighbours so the shell swells smoothly instead of spiking; repeated a few times."""
    me = ob.data
    nb = [[] for _ in me.vertices]
    for e in me.edges: a, b = e.vertices; nb[a].append(b); nb[b].append(a)
    report = []
    if groups: groups = set(groups) | {g.replace("Right", "Left") for g in groups}
    set_pose(None); arm.data.pose_position = "REST"; bpy.context.view_layer.update()
    rest_tree, rest_keep = posed_bvh(body, groups)
    for rnd in range(rounds):
        need = np.zeros(len(me.vertices)); worst = {}
        for pname, pose in POSES.items():
            set_pose(pose)
            tree, keep = posed_bvh(body, groups)
            P = posed_points(ob)
            k = 0
            for i, p in enumerate(P):
                loc, nrm, idx, d = tree.find_nearest(p)
                if loc is None or d > 0.06: continue
                if only is not None and not only(me.vertices[i].co): continue
                s = (p - loc).dot(nrm * NSIGN)
                if s < clear:
                    need[i] = max(need[i], min(most, clear - s))
                    if s < 0: k += 1
            keep.free()
            worst[pname] = k
        set_pose(None); arm.data.pose_position = "REST"; bpy.context.view_layer.update()
        report.append(worst)
        if need.max() <= 1e-5: break
        # (first pictures: spread only along edges, the amount stopped at every crack the weld had not closed and the plate's front broke up
        # into shards. It is spread through space instead: every point within 9 cm of a point that must move goes with it, less with distance.)
        from mathutils import kdtree
        kd = kdtree.KDTree(len(me.vertices))
        for i, v in enumerate(me.vertices): kd.insert(v.co, i)
        kd.balance()
        sm = need.copy()
        for i in np.nonzero(need > 1e-5)[0]:
            for co, j, dist in kd.find_range(me.vertices[i].co, 0.09):
                w = need[i] * (1 - dist / 0.09) ** 0.5
                if w > sm[j]: sm[j] = w
        need = sm
        Wm = ob.matrix_world; Wi = Wm.inverted()
        for i, v in enumerate(me.vertices):
            if need[i] <= 1e-5: continue
            p = Wm @ v.co
            loc, nrm, idx, d = rest_tree.find_nearest(p)
            if loc is None: continue
            n = nrm * NSIGN
            if d > 1e-4 and (p - loc).dot(n) > 0: n = ((p - loc).normalized() + n).normalized()   # (out, away from the skin)
            v.co = Wi @ (p + n * float(need[i]) * 1.05)
        me.update()
    rest_keep.free()
    log("%s: points under the skin by pose, each round: %s" % (ob.name, "; ".join(" ".join("%s %d" % (k, v) for k, v in w.items()) for w in report)))

made = []
for P in PIECES:
    if ONLY and P["name"] not in ONLY.split(","): continue
    ob = load_piece(P["file"]); ob.name = P["name"]
    repair(ob, P.get("weld", 0.004), P.get("smooth", True))
    if len(ob.data.polygons) > 30000:              # (a piece the prep left whole)
        dm = ob.modifiers.new("D", 'DECIMATE'); dm.ratio = 24000 / len(ob.data.polygons)
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.modifier_apply(modifier="D")
    rx, ry, rz = (math.radians(a) for a in P.get("rot", (0, 0, 0)))
    apply_matrix(ob, Matrix.Rotation(rz, 4, 'Z') @ Matrix.Rotation(ry, 4, 'Y') @ Matrix.Rotation(rx, 4, 'X'))
    if P.get("cut_skin"): cut_skin(ob)
    if "fixed" in P:
        lay_in_box(ob, np.array(P["fixed"][0]), np.array(P["fixed"][1]))
    elif "box" in P:
        groups, grow, shift = P["box"]
        R = region(groups)
        lo, hi = R.min(0), R.max(0)
        if "zrange" in P: lo[2], hi[2] = P["zrange"]
        if P["name"] != "Helmet": w = max(-lo[0], hi[0]); lo[0], hi[0] = -w, w
        if "zfrom" in P: lo[2] = lo[2] + (hi[2] - lo[2]) * P["zfrom"]   # (the helmet: the upper part of the head, brow up)
        c = (lo + hi) / 2; h = (hi - lo) / 2 * np.array(grow)
        lay_in_box(ob, c - h + np.array(shift), c + h + np.array(shift))
    elif "limb" in P:
        top, bottom, groups = P["limb"]
        lay_limb(ob, top, bottom, groups, P["grow"], P["len"])
    elif P.get("shoulder"):
        # on top of his right shoulder: centred over the shoulder joint, as wide as the deltoid's skin, capping it
        Sj = joint("RightArm")
        R = region(["RightShoulder", "RightArm"], -1)
        R = R[(R[:, 2] > Sj.z - 0.22)]
        lo, hi = R.min(0), R.max(0)
        lo[2] = Sj.z - 0.10; hi[2] = R[:, 2].max() + 0.12   # (the horns stand above it)
        lo[0] = Sj.x - 0.20; hi[0] = Sj.x + 0.10
        cy = (lo[1] + hi[1]) / 2; lo[1], hi[1] = cy - 0.16, cy + 0.16
        lay_in_box(ob, lo, hi)
    if "limb" in P: inflate(ob, P["clear"], joint(P["limb"][0]) - joint(P["limb"][1]))
    if "taper" in P: taper_top(ob, P["limb"][0], "RightFoot", ["RightLeg"], P["taper"][0], P["taper"][1])
    elif not P.get("shoulder") and "fixed" not in P: inflate(ob, P["clear"])
    n = 0 if P.get("noconform") else conform(ob, P["clear"])
    log("%s: %d points pushed out of the skin" % (P["name"], n))
    if P.get("mirror") or P.get("shoulder"): mirror_copy(ob)
    split_metal(ob, P["name"])
    skin_to_body(ob, P.get("rigid"), P.get("allow"))
    if P.get("poseclear"): pose_clear(ob, P["poseclear"][0], 3, P["poseclear"][1], P["poseclear"][2], P.get("poseonly"))
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    Pw = verts_world(ob)
    log("%s: %d triangles, box %s .. %s" % (P["name"], tris, np.round(Pw.min(0), 3), np.round(Pw.max(0), 3)))
    made.append(ob)

if RENDER:
    for n in body.material_slots[0].material.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image and n.image.name == "texture_0": body.material_slots[0].material.node_tree.nodes.active = n
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'; sc.display.shading.color_type = 'TEXTURE'; sc.display.shading.light = 'STUDIO'
    sc.render.resolution_x, sc.render.resolution_y = 1000, 1400
    cam = bpy.data.objects.new("C", bpy.data.cameras.new("C")); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 2.6
    c = Vector((0, 0, 1.15))
    for nm, d in (("front", Vector((0, -1, 0))), ("side", Vector((-1, 0, 0))), ("back", Vector((0, 1, 0))), ("q", Vector((-0.7, -0.7, 0.25)).normalized())):
        cam.location = c + d * 6
        cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(OUT, "fit_%s.png" % nm)
        bpy.ops.render.render(write_still=True)

if os.environ.get("TK_NOEXPORT") != "1" and not ONLY:
    bpy.data.objects.remove(body)
    bpy.ops.object.select_all(action='DESELECT')
    for o in [arm] + made: o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SK_TrollArmour.fbx"), use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=False, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                             use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True, path_mode='STRIP')
    log("wrote SK_TrollArmour.fbx")
log("done")
