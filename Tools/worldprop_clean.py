# World props from Meshy's community library (2026-11-16, the forest ruin): Art/WorldProps/Originals/<file> (git-ignored; links in Art/WorldProps/Sources.md)
# -> Saved/KnightBuild/WorldProps/<Name>.fbx + T_<Name>_Base/Normal/MR.png, imported by Tools/worldprop_import.py (Tools/BuildWorldProps.ps1).
# Run in Blender: blender -b -P Tools/worldprop_clean.py -- <Art/WorldProps> <out dir> [names]
# Per prop (WORLD below):
#   axes: where the model's own x, y, z go in the game's frame (signed letters). height: its height in cm. The origin goes to the middle of its foot, at its lowest point.
#   cut: lambda (face centre in the model's own frame, metres; (r, g, b) of the picture there; face normal) -> True to delete (dirt piles, stray pieces).
#   reduce: triangles kept (welded, collapse-decimated, fresh UVs and pictures baked from the full model: base 2048, normal and metal/roughness 1024).
#   stone: (hue, saturation, value, fine, slow): the whole picture evened to one clean stone colour, keeping 'fine' of the fine detail (the joints, chips)
#     and 'slow' of the gentle shading; the hue keeps 15% of each pixel's own.
#   hulls: (cell cm, lowest cm): the simple collision: the model cut into upright columns 'cell' wide, one convex hull (UCX_) per column round the
#     vertices in it, columns lower than 'lowest' left out (rubble he steps over). A window or arch opening in a column is filled.
#   cut_final: lambda (face centre in the game's frame, cm; normal) -> True to delete, after the cuts above (pieces the colour and 'thin' cuts miss).
#   course: a band of a round wall rebuilt as a solid stone capping course (make_course: centre, wedge of angles, z from and to, lip, cell, voxel and
#     drop, all cm or degrees in the game's frame), textured by the bake with the stone of the wall below it.
import bpy, bmesh, os, sys, math
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

def _hsv(c):
    mx, mn = max(c), min(c)
    if mx - mn < 1e-5: return 0.0, 0.0, mx
    r, g, b = c
    if mx == r: h = ((g - b) / (mx - mn)) % 6.0
    elif mx == g: h = (b - r) / (mx - mn) + 2.0
    else: h = (r - g) / (mx - mn) + 4.0
    return h * 60.0, (mx - mn) / mx, mx

WORLD = {
    # "Ruined Fortress" by eromaxi (CC0): a round stone tower with a window arch and fallen blocks. Cut: the dark earth mound under its right side
    # (owner: no dirt piles). The dark roof-like eave round its upper wall (what earlier rounds cut as the "roof slab", "thin" sheets and the "ledge")
    # is rebuilt as a solid stone capping course ('course', 2026-11-17, "fix the ruin wall top"; see make_course). With the eave gone, the cut of the
    # darkest faces (value under 0.10), 'thin' and cut_final were dropped: they only punched see-through pinholes into the stone joints and the doorway
    # (the model is a closed solid, and the repaint turns its dark crevices to grey stone anyway).
    "Ruin": dict(file="Originals/Ruin_Original.glb", axes=("Y", "-X", "Z"), height=500.0, reduce=40000,
                 cut=lambda p, c, n: p[2] < -0.50 and _hsv(c)[2] < 0.30 and _hsv(c)[1] > 0.35,
                 course=dict(centre=(10.0, 0.0), wedge=(158.0, -70.0), z=(302.0, 360.0), lip=4.0, lip_from=190.0, joint=70.0, cell=2.0, voxel=2.5, drop=62.0),
                 recalc=False, stone=(36.0, 0.13, 0.52, 0.55, 0.45), hulls=(70.0, 35.0)),
}
WORLD["Gallows"] = dict(file="Originals/Gallows_Original.glb", axes=("X", "Y", "Z"), height=400.0, reduce=6500,
                        # "Gallows Platform" by szpeylin (Meshy community, CC0): a timber gallows with a deck, stairs and four plain nooses. 6.4k triangles, kept as they are.
                        # gap: a column's points are split into separate hulls where they skip more than 'gap' cm of height (so the nooses hang free of the deck).
                        hulls=(40.0, 20.0), gap=45.0)
names = a[2:] or list(WORLD)
IDX = {"X": 0, "Y": 1, "Z": 2}

def axis_matrix(ax):
    m = Matrix.Identity(3)
    for col, s in enumerate(ax):
        sign = -1.0 if s[0] == "-" else 1.0
        for row in range(3): m[row][col] = 0.0
        m[IDX[s[-1]]][col] = sign
    return m

def tex_node(mat, sock_name):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    stack = [pr.inputs[sock_name]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(n.inputs)
    return None

def bake(o, hi, name, size=2048):
    # fresh UVs on the reduced model and its pictures baked from the full one (as Tools/prop_clean.py's bake_to_new_uvs)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
    me = o.data
    old = [l.name for l in me.uv_layers]
    uvb = me.uv_layers.new(name="Bake"); me.uv_layers.active = uvb
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")
    hmat = hi.data.materials[0]
    tb = tex_node(hmat, "Base Color"); tn = tex_node(hmat, "Normal"); tm = tex_node(hmat, "Metallic") or tex_node(hmat, "Roughness")
    lmat = bpy.data.materials.new("Baked"); lmat.use_nodes = True
    me.materials.clear(); me.materials.append(lmat)
    ln = lmat.node_tree.nodes
    tgt = ln.new("ShaderNodeTexImage")
    def img(tag, sz, noncolor):
        im = bpy.data.images.new("%s_%s" % (tag, name), sz, sz, alpha=True)
        if noncolor: im.colorspace_settings.name = "Non-Color"
        return im
    pts = [v.co for v in me.vertices]
    diag = (Vector([max(p[i] for p in pts) for i in range(3)]) - Vector([min(p[i] for p in pts) for i in range(3)])).length
    hi.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = o
    kw = dict(use_selected_to_active=True, cage_extrusion=0.006 * diag, max_ray_distance=0.02 * diag, margin=16, use_clear=True)
    got = {}
    if tn:
        im = img("Normal", size // 2, True); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw); got["Normal"] = im
    out_node = next(n for n in hmat.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
    em = hmat.node_tree.nodes.new("ShaderNodeEmission")
    hmat.node_tree.links.new(em.outputs[0], out_node.inputs["Surface"])
    for tag, tnode, sz, nc in (("Base", tb, size, False), ("MR", tm, size // 2, True)):
        if not tnode: continue
        hmat.node_tree.links.new(tnode.outputs["Color"], em.inputs["Color"])
        im = img(tag, sz, nc); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
    for n_ in old: me.uv_layers.remove(me.uv_layers[n_])
    bpy.data.objects.remove(hi, do_unlink=True)
    print("WPROP", name, "baked", ", ".join(sorted(got)))
    return got

def blur(a_, rad):
    import numpy as np
    for ax in (0, 1):
        c = np.cumsum(a_, axis=ax, dtype=np.float64)
        n = a_.shape[ax]
        hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
        t = np.take(c, hi_, axis=ax); b_v = np.take(c, np.maximum(lo_, 0), axis=ax)
        shape = [1, 1]; shape[ax] = n
        a_ = t - np.where(lo_.reshape(shape) < 0, 0.0, b_v)
    return a_

def restone(im, P):
    # one clean pale stone: hue and saturation set (15% of each pixel's own hue kept), the value from the picture's own shading and detail
    import numpy as np
    h0, s0, v0, keep_fine, keep_slow = P["stone"]
    W, H = im.size
    arr = np.array(im.pixels[:], dtype=np.float32).reshape(H, W, 4)
    rgb = arr[..., :3]
    V = rgb.max(axis=2); C = V - rgb.min(axis=2); S = C / np.maximum(V, 1e-4)
    r_, g_, b_ = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    Hd = np.where(V == r_, ((g_ - b_) / np.maximum(C, 1e-6)) % 6.0, np.where(V == g_, (b_ - r_) / np.maximum(C, 1e-6) + 2.0, (r_ - g_) / np.maximum(C, 1e-6) + 4.0)) * 60.0
    used = V > 0.01
    rad = max(4, W // 64)
    wt = blur(used.astype(np.float64), rad)
    slow = blur(np.where(used, V, 0.0), rad) / np.maximum(wt, 1.0)
    fine = np.where(used, V / np.maximum(slow, 0.02), 1.0)
    mean_ = max(float(V[used].mean()), 0.02)
    Vn = np.clip(v0 * np.power(np.maximum(slow, 0.02) / mean_, keep_slow) * np.power(fine, keep_fine), 0.0, 1.0)
    dh = ((Hd - h0 + 180.0) % 360.0) - 180.0
    Hn = (h0 + 0.15 * np.clip(dh, -40.0, 40.0)) % 360.0
    Sn = np.clip(s0 + 0.15 * (S - s0), 0.0, 1.0)
    h6 = Hn / 60.0; X = 1.0 - np.abs(h6 % 2.0 - 1.0)
    Cn = Vn * Sn; Xn = Cn * X; m = Vn - Cn
    seg = np.floor(h6).astype(np.int32) % 6
    z = 0 * Cn
    R = np.choose(seg, [Cn, Xn, z, z, Xn, Cn]); G = np.choose(seg, [Xn, Cn, Cn, Xn, z, z]); B = np.choose(seg, [z, z, Xn, Cn, Cn, Xn])
    arr[..., 0] = np.where(used, R + m, r_); arr[..., 1] = np.where(used, G + m, g_); arr[..., 2] = np.where(used, B + m, b_)
    im.pixels.foreach_set(arr.reshape(-1)); im.update()
    print("WPROP stone: value mean %.3f -> %.3f" % (mean_, float(Vn[used].mean())))

def save(im, path):
    im.filepath_raw = path; im.file_format = "PNG"; im.save()

def to_game(o, P):
    # into the game's frame: the axes, the size (cm), the origin at the middle of the foot (2026-11-17: now before the reduce, so the course
    # and the cut_final work in cm on the full model)
    me = o.data
    M3 = axis_matrix(P["axes"])
    for v in me.vertices: v.co = M3 @ v.co
    if M3.determinant() < 0:
        bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    xs = [v.co for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    S = P["height"] / (hi_.z - lo.z)
    g = Vector(((lo.x + hi_.x) * 0.5, (lo.y + hi_.y) * 0.5, lo.z))
    for v in me.vertices: v.co = (v.co - g) * S
    me.update()

def make_course(o, P, name):
    # The ruin's tower (2026-11-17, owner: "Fix the ruin wall top"). Round its upper wall ran a dark, roof-like eave: a zero-thickness skirt from the
    # lower wall's face (about 3.1 m) sloping up and in to the upper courses, which stand 40 cm further in (about 3.55 m), with a ragged lip out past
    # the wall. It was the wall's only top there, so cutting it opened a see-through band, and pulling it in ('tuck', round 3) still left a ragged
    # edge, a dark slit and a green line. Now, in the wedge of the tower it runs round (angles from 'centre', wedge[0] on round to wedge[1]):
    #   1. every face between z[0] and z[1] is deleted, from the model and from the bake source alike;
    #   2. the wall's own cross-section at z[0] (found by ray tests on a 'cell' cm grid) is stood up from just under z[0] to just over z[1] as one
    #      closed block, 'lip' cm proud of the outer face (cells further than lip_from cm from the centre; 'cell' cm on the inner face), so it reads
    #      as a solid capping course with a crisp shadow line under it; the upper courses sit back on its top, which makes a flat 40 cm stone ledge.
    #      It overlaps the cut edges of the wall above, below and at the wedge's ends, so none of them can show;
    #   3. the block is voxel-remeshed ('voxel' cm) into a smooth closed solid and joined to the model;
    #   4. its faces are copied into the bake source, textured with the stone of the same wall 'drop' cm lower (each face's corners mapped onto the
    #      wall face a level ray meets there; its underside and top unfolded down under its outer face), so the bake paints it like the row of
    #      smaller stones just under it.
    # (A voxel remesh of the whole model, to close every sheet, was tried first: the cuts leave thousands of open edges, the remesher's inside test
    # leaks through them and most of the ruin vanished.) Returns the bake source (the full model without the eave, plus the textured course).
    from mathutils.bvhtree import BVHTree
    from mathutils import geometry
    C = P["course"]
    cx, cy = C["centre"]; w0, w1 = C["wedge"]; z0, z1 = C["z"]; cell = C["cell"]
    def polar(x, y):
        return math.degrees(math.atan2(y - cy, x - cx)), math.hypot(x - cx, y - cy)
    def inwedge(x, y, wider=0.0):
        th = polar(x, y)[0]
        return th >= w0 - wider or th <= w1 + wider
    WIDER = 1.5   # (degrees: the block reaches a little past the cut, about 6 cm at the wall)
    me = o.data
    bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
    tree = BVHTree.FromBMesh(bm)
    # (2) the wall's cross-section at z0: a cell is stone when level rays from it meet the inside of the wall first, in at least 2 of 3 ways
    xs = [v.co.x for v in bm.verts]; ys = [v.co.y for v in bm.verts]
    x_lo, y_lo = min(xs) - 4 * cell, min(ys) - 4 * cell
    nx, ny = int((max(xs) - x_lo) / cell) + 8, int((max(ys) - y_lo) / cell) + 8
    zc = z0 + 0.5
    occ = set()
    for i in range(nx):
        for j in range(ny):
            x, y = x_lo + (i + 0.5) * cell, y_lo + (j + 0.5) * cell
            if not inwedge(x, y, WIDER): continue
            th, r = polar(x, y)
            if r < 60.0 or r > 330.0: continue
            R = Vector(((x - cx) / r, (y - cy) / r, 0.0)); T = Vector((-R.y, R.x, 0.0))
            p = Vector((x, y, zc)); ins = 0
            for d in (R, -R, T):
                h = tree.ray_cast(p, d, 600.0)
                if h[0] is not None and h[1].dot(d) > 0.0: ins += 1
            if ins >= 2: occ.add((i, j))
    # the lip: the outer face pushed out 'lip' cm, the inner face one cell (cells next to the stone); the lip is left out at the joints between the
    # course's stones (every 'joint' cm or so round the outer face, a groove one cell wide), so it reads as separate capping stones
    def cr(c_): return polar(x_lo + (c_[0] + 0.5) * cell, y_lo + (c_[1] + 0.5) * cell)
    joints, s_, k_ = [], 30.0, 0
    while s_ < 1200.0:
        joints.append(s_); k_ += 1; s_ += C["joint"] * (0.8 + 0.4 * ((k_ * 7919) % 13) / 12.0)
    def at_joint(c_):
        th, r = cr(c_)
        arc = math.radians((th - w0) % 360.0) * r
        return any(abs(arc - j_) < cell for j_ in joints)
    for k_ in range(max(1, int(round(C["lip"] / cell)))):
        add = set()
        for (i, j) in occ:
            for c_ in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                if c_ in occ or c_ in add: continue
                x, y = x_lo + (c_[0] + 0.5) * cell, y_lo + (c_[1] + 0.5) * cell
                if not inwedge(x, y, WIDER): continue
                outer = cr(c_)[1] > C["lip_from"]
                if (outer and not at_joint(c_)) or (k_ == 0 and not outer): add.add(c_)
        occ |= add
    # the course's outer and inner radius by angle (whole degrees), for unfolding its top onto the wall
    rout, rin = {}, {}
    for c_ in occ:
        th, r = cr(c_); k = int(math.floor(th))
        rout[k] = max(rout.get(k, 0.0), r); rin[k] = min(rin.get(k, 1e9), r)
    def ang_r(tab, th, far):
        k = int(math.floor(th))
        for d in (0, 1, -1, 2, -2, 3, -3):
            if (k + d) in tab: return tab[k + d]
        return far
    # (1) the band goes
    gone = [f for f in bm.faces if z0 < f.calc_center_median().z < z1 and inwedge(*f.calc_center_median().xy)]
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    bm.to_mesh(me); bm.free(); me.update()
    hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
    # (2) the block: cell columns from 3 cm under z0 to 3 cm over z1
    cb = bmesh.new(); vk = {}
    def V(i, j, lv):
        k = (i, j, lv)
        if k not in vk: vk[k] = cb.verts.new((x_lo + i * cell, y_lo + j * cell, (z0 - 3.0) if lv == 0 else (z1 + 3.0)))
        return vk[k]
    for (i, j) in occ:
        cb.faces.new((V(i, j, 1), V(i + 1, j, 1), V(i + 1, j + 1, 1), V(i, j + 1, 1)))
        cb.faces.new((V(i, j + 1, 0), V(i + 1, j + 1, 0), V(i + 1, j, 0), V(i, j, 0)))
        if (i + 1, j) not in occ: cb.faces.new((V(i + 1, j, 0), V(i + 1, j + 1, 0), V(i + 1, j + 1, 1), V(i + 1, j, 1)))
        if (i - 1, j) not in occ: cb.faces.new((V(i, j + 1, 0), V(i, j, 0), V(i, j, 1), V(i, j + 1, 1)))
        if (i, j + 1) not in occ: cb.faces.new((V(i + 1, j + 1, 0), V(i, j + 1, 0), V(i, j + 1, 1), V(i + 1, j + 1, 1)))
        if (i, j - 1) not in occ: cb.faces.new((V(i, j, 0), V(i + 1, j, 0), V(i + 1, j, 1), V(i, j, 1)))
    cm = bpy.data.meshes.new(name + "_Course"); cb.to_mesh(cm); cb.free()
    co = bpy.data.objects.new(cm.name, cm); bpy.context.scene.collection.objects.link(co)
    print("WPROP", name, "course: band cut", len(gone), "faces, stone cells", len(occ))
    # (3) closed, then smoothed across the ground only (the cells' 2 cm steps otherwise show as vertical flutes in a low sun)
    remesh(co, C["voxel"], name)
    bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = co; co.select_set(True)
    sm = co.modifiers.new("Sm", "SMOOTH"); sm.factor = 1.0; sm.iterations = 12; sm.use_x = True; sm.use_y = True; sm.use_z = False
    bpy.ops.object.modifier_apply(modifier=sm.name)
    # (4) the course's faces into the bake source, with the wall's stone 'drop' cm lower
    hbm = bmesh.new(); hbm.from_mesh(hi.data); hbm.faces.ensure_lookup_table()
    htree = BVHTree.FromBMesh(hbm)
    uvl = hbm.loops.layers.uv.active
    nh = len(hbm.faces)
    src = [(f.verts[0].co.copy(), f.verts[1].co.copy(), f.verts[2].co.copy(),
            f.loops[0][uvl].uv.copy(), f.loops[1][uvl].uv.copy(), f.loops[2][uvl].uv.copy()) for f in hbm.faces]
    Z = Vector((0.0, 0.0, 1.0))
    made = 0; missed = 0; last = None
    for f in co.data.polygons:
        c = f.center.copy()
        th, r = polar(c.x, c.y)
        if r < 1.0: continue
        R = Vector(((c.x - cx) / r, (c.y - cy) / r, 0.0)); T = Vector((-R.y, R.x, 0.0))
        ro, ri = ang_r(rout, th, r), ang_r(rin, th, r)
        n = f.normal
        inner = abs(n.z) < 0.6 and n.dot(R) < -0.3
        def unf(q):   # the unfolded height of a point on the course (its underside and then its top folded down under its outer face)
            if n.z > 0.6: return z0 - 2.0 * C["lip"] - (ro - math.hypot(q.x - cx, q.y - cy))
            if n.z < -0.6: return z0 - (math.hypot(q.x - cx, q.y - cy) - (ro - C["lip"]))
            return q.z
        hit = None
        for extra in (0.0, -40.0, 40.0, -80.0, 80.0):
            zs = unf(c) - C["drop"] + extra
            if inner: org, d = Vector((cx, cy, zs)) + R * 40.0, R
            else: org, d = Vector((cx, cy, zs)) + R * 450.0, -R
            h = htree.ray_cast(org, d, 600.0)
            if h[0] is None or h[1].dot(d) > 0.0: continue
            hr = math.hypot(h[0].x - cx, h[0].y - cy)
            if (inner and hr < ri + 35.0) or (not inner and hr > ro - 35.0): hit = (h, T); break
        if hit is None:
            missed += 1
            if last is None: continue
            hit = last   # (the stone the face before it used)
        last = hit
        (h, Th) = hit
        A, B, Cc, ua, ub, uc = src[h[2]]
        vs = [hbm.verts.new(co.data.vertices[i].co) for i in f.vertices]
        nf = hbm.faces.new(vs); nf.material_index = 0; nf.smooth = False
        uc_ = unf(c)
        for lp, vi in zip(nf.loops, f.vertices):
            q = co.data.vertices[vi].co
            s = h[0] + Th * (q - c).dot(T) + Z * (unf(q) - uc_)
            uv = geometry.barycentric_transform(s, A, B, Cc, Vector((ua.x, ua.y, 0.0)), Vector((ub.x, ub.y, 0.0)), Vector((uc.x, uc.y, 0.0)))
            lp[uvl].uv = (uv.x, uv.y)
        made += 1
    hbm.to_mesh(hi.data); hbm.free(); hi.data.update()
    print("WPROP", name, "course: textured faces", made, "(", missed, "took their neighbour's stone ) in the bake source of", nh, "faces")
    # the block joins the model (one mesh; not fused, it overlaps the cut edges)
    bpy.ops.object.select_all(action="DESELECT")
    co.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.object.join()
    o.select_set(False)
    return hi

def remesh(o, voxel, name):
    # a closed block made smooth: Blender's voxel remesh at 'voxel' cm, then bits under 200 vertices dropped (dust the voxels leave)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    md = o.modifiers.new("Rem", "REMESH"); md.mode = "VOXEL"; md.voxel_size = voxel; md.use_smooth_shade = False
    bpy.ops.object.modifier_apply(modifier=md.name)
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
    seen = set(); small = []
    for v in bm.verts:
        if v.index in seen: continue
        st = [v]; seen.add(v.index); comp = []
        while st:
            x = st.pop(); comp.append(x)
            for e in x.link_edges:
                w = e.other_vert(x)
                if w.index not in seen: seen.add(w.index); st.append(w)
        if len(comp) < 200: small.extend(comp)
    bmesh.ops.delete(bm, geom=small, context="VERTS")
    bm.to_mesh(o.data); bm.free(); o.data.update()
    print("WPROP", name, "remesh", voxel, "cm: tris", sum(len(p.vertices) - 2 for p in o.data.polygons), "(dropped", len(small), "dust vertices)")

for name in names:
    P = WORLD[name]
    f = os.path.join(src, P["file"])
    if not os.path.exists(f):
        print("WPROP missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
    # cut: by where a face is and the colour under it
    if "cut" in P:
        mat = me.materials[0]
        tb = tex_node(mat, "Base Color").image
        W, H = tb.size
        import numpy as np
        px = np.array(tb.pixels[:], dtype=np.float32).reshape(H, W, 4)
        uvl = me.uv_layers.active.data
        bm = bmesh.new(); bm.from_mesh(me)
        uv = bm.loops.layers.uv.active
        gone = []
        for fc in bm.faces:
            u = sum((l[uv].uv for l in fc.loops), Vector((0.0, 0.0))) / len(fc.loops)
            c = px[min(H - 1, max(0, int((u.y % 1.0) * H))), min(W - 1, max(0, int((u.x % 1.0) * W))), :3]
            if P["cut"](fc.calc_center_median(), tuple(float(x) for x in c), fc.normal): gone.append(fc)
        bmesh.ops.delete(bm, geom=gone, context="FACES")
        # (bits left floating: islands of under 40 faces, once welded: the model is split along its picture's seams)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bm.faces.ensure_lookup_table()
        seen = set(); stray = []
        for fc in bm.faces:
            if fc in seen: continue
            comp = [fc]; seen.add(fc); k = 0
            while k < len(comp):
                for e in comp[k].edges:
                    for g in e.link_faces:
                        if g not in seen: seen.add(g); comp.append(g)
                k += 1
            if len(comp) < 40: stray.extend(comp)
        bmesh.ops.delete(bm, geom=stray, context="FACES")
        # thin: faces of paper-thin sheets (what was left of the roof slab and its hanging shards, review round 2): a ray from the face into the model
        # meets the other side within 'thin' (a share of the model's height); stone blocks are far thicker. Then the strays again.
        nthin = 0
        if P.get("thin"):
            from mathutils.bvhtree import BVHTree
            bm.faces.ensure_lookup_table()
            zs = [v.co.z for v in bm.verts]; t_ = P["thin"] * (max(zs) - min(zs))
            tree = BVHTree.FromBMesh(bm)
            thin = []
            for fc in bm.faces:
                c_ = fc.calc_center_median(); n_ = fc.normal
                if n_.length < 0.5: continue
                hit = tree.ray_cast(c_ - n_ * (t_ * 0.02), -n_, t_)
                if hit[0] is not None: thin.append(fc)
            bmesh.ops.delete(bm, geom=thin, context="FACES")
            nthin = len(thin)
            bm.faces.ensure_lookup_table()
            seen = set(); stray2 = []
            for fc in bm.faces:
                if fc in seen: continue
                comp = [fc]; seen.add(fc); k = 0
                while k < len(comp):
                    for e in comp[k].edges:
                        for g in e.link_faces:
                            if g not in seen: seen.add(g); comp.append(g)
                    k += 1
                if len(comp) < 200: stray2.extend(comp)
            bmesh.ops.delete(bm, geom=stray2, context="FACES")
            stray += stray2
        bm.to_mesh(me); bm.free(); me.update()
        print("WPROP", name, "cut", len(gone), "faces, and", len(stray), "stray, and", nthin, "thin")
    # into the game's frame (cm) before the reduce (2026-11-17; it was after the bake)
    to_game(o, P)
    me = o.data
    # cut_final: faces to delete in the game's frame (cm; centre, normal), for pieces the colour cut and 'thin' miss (the ruin used it for its roof
    # slab, review round 2, until the course replaced the slab, 2026-11-17)
    if "cut_final" in P:
        bm = bmesh.new(); bm.from_mesh(me)
        gone = [f for f in bm.faces if P["cut_final"](f.calc_center_median(), f.normal)]
        bmesh.ops.delete(bm, geom=gone, context="FACES")
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        bm.to_mesh(me); bm.free(); me.update()
        print("WPROP", name, "cut_final", len(gone), "faces")
    # the full model is kept for the bake. (The old 'tuck', which pulled the eave's vertices back onto the wall, is gone: the course replaces it.)
    if "course" in P: hi = make_course(o, P, name)
    else: hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
    me = o.data
    # reduce
    bm = bmesh.new(); bm.from_mesh(me)
    ext = max((max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts)) for i in range(3))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=ext * 2e-5)
    # (recalc, 2026-11-17: off for the ruin. On its open, scanned shell Blender's outward guess flipped whole patches inward: the game culls
    # those as back faces, so a solid column read as a paper-thin panel and the wall top as hanging curved sheets; the model's own winding is right)
    if P.get("recalc", True): bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free(); me.update()
    tris_now = sum(len(p.vertices) - 2 for p in me.polygons)
    md = o.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, P["reduce"] / float(tris_now)); md.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=md.name)
    me = o.data
    try: bpy.ops.mesh.customdata_custom_splitnormals_clear()
    except Exception as e_: print("WPROP custom normals kept:", e_)
    me.shade_smooth(); me.set_sharp_from_angle(angle=math.radians(50.0))
    got = bake(o, hi, name)
    me = o.data
    print("WPROP", name, "tris from", tris0, "to", sum(len(p.vertices) - 2 for p in me.polygons))
    if "stone" in P and "Base" in got: restone(got["Base"], P)
    xs = [v.co.copy() for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    print("WPROP", name, "size cm", tuple(round(hi_[i] - lo[i]) for i in range(3)), "lo", tuple(round(x) for x in lo), "hi", tuple(round(x) for x in hi_))
    # the simple collision: one convex hull per upright column of the model
    hulls = []
    if "hulls" in P:
        cell, lowest = P["hulls"]
        cols = {}
        for c in xs: cols.setdefault((int(math.floor(c.x / cell)), int(math.floor(c.y / cell))), []).append(c)
        groups = []
        for key, ps in sorted(cols.items()):
            if P.get("gap"):
                ps = sorted(ps, key=lambda q: q.z); cur = [ps[0]]
                for q in ps[1:]:
                    if q.z - cur[-1].z > P["gap"]: groups.append((key, cur)); cur = []
                    cur.append(q)
                groups.append((key, cur))
            else: groups.append((key, ps))
        for key, ps in groups:
            if len(ps) < 8: continue
            if max(p.z for p in ps) - min(p.z for p in ps) < lowest and max(p.z for p in ps) < lowest: continue
            hb = bmesh.new()
            for p in ps: hb.verts.new(p)
            res = bmesh.ops.convex_hull(hb, input=hb.verts[:])
            loose = list({v for v in list(res.get("geom_interior", [])) + list(res.get("geom_unused", [])) if isinstance(v, bmesh.types.BMVert)})
            if loose: bmesh.ops.delete(hb, geom=loose, context="VERTS")
            if len(hb.faces) < 4: hb.free(); continue
            if len(hb.verts) > 64:   # (Unreal keeps hulls small: thin the points out)
                bmesh.ops.dissolve_limit(hb, angle_limit=math.radians(8.0), verts=hb.verts[:], edges=hb.edges[:])
            hm = bpy.data.meshes.new("UCX_%s_%02d" % (name, len(hulls)))
            hb.to_mesh(hm); hb.free()
            ho = bpy.data.objects.new(hm.name, hm); bpy.context.scene.collection.objects.link(ho)
            hulls.append(ho)
        print("WPROP", name, "collision hulls", len(hulls), "cell", cell, "cm")
    # pictures
    for tag, im in got.items():
        save(im, os.path.join(out, "T_%s_%s.png" % (name, tag)))
    # out to FBX: Blender's y -> the game's -y under the importer's scene conversion (as prop_clean.py), so y flipped and the faces turned
    o.name = name; o.data.name = name
    for ob in [o] + hulls:
        for v in ob.data.vertices: v.co.y = -v.co.y
        bm = bmesh.new(); bm.from_mesh(ob.data); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(ob.data); bm.free(); ob.data.update()
    bpy.ops.object.select_all(action="DESELECT")
    for ob in [o] + hulls: ob.select_set(True)
    bpy.context.view_layer.objects.active = o
    fbx = os.path.join(out, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("WPROP wrote", fbx, "tris", sum(len(p.vertices) - 2 for p in o.data.polygons))
