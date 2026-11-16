# Windows built into the model (owner, 2026-11-15, third round: "The panes are not flush up against the window and appear to be
# floating in front of the window", "needs to be 1 piece with dividers, not a bunch of shapes", "doesn't fit the same shape as the
# window frame", "see if there is a better solution other than these cards sitting in front of the windows").
#
#   blender -b -P Tools/window_inserts.py -- <work> <out> <Name>:<NewName> [...]
#   <work>/<Name>.fbx is the 300k model the bake started from (its window marks and its real frames), <out>/<NewName>.fbx the baked
#   low model, rewritten with its windows rebuilt (BuildTownLabBuildings.ps1, after the bake and townlab_panes.py).
#
# What it does, per window opening:
#   1. THE OPENING comes from the 300k model's own frame, not from the old pane fragments: the model is drawn as a depth picture in the
#      window's plane (1 cm a texel); the opening is everything at the glass's depth joined to the window marks, lights a bar or
#      mullion apart taken as one window, and it takes the clean shape that fits it best: a box, a round arch, a pointed arch, a
#      round light, or failing those the outline round it. (The church: from the stone moulding round each window, BY_FRAME.)
#
#   2. THE OLD WINDOW IS CUT OUT of the low model: its faces are cut along the opening's outline and everything inside it from behind
#      the glass outward is deleted (the decimated glass, muntins, mullions and the ragged inner edge of the frame). Things that stand
#      well in front of the glass in the 300k model (the bank's iron bars, the houses' flower boxes) are kept; the church's tracery
#      and the corner house's mullions, too close to the glass to keep cleanly, go with the rest (NOKEEP).
#   3. A NEW WINDOW IS BUILT IN at the glass's own depth, at the back of the recess: one flat glass fitted to the opening, a lining
#      from every cut edge straight back to the glass (so the recess has clean straight sides and nothing is open), and real muntin
#      bars standing 1.5 cm proud of the glass in the building's own frame colour: a grid sized to the window (2 x 3 on a house
#      window), a bar at the springing of an arch, spokes and a hub on a round light. A window that keeps its own bars or tracery in
#      front (the bank's barred windows) gets no second set.
#   Only the glass is marked in the vertex colour (G 1, B 0, R the window's number): M_TL_Building turns it dark by day and lights it
#   one even amber at dusk, the whole window at once. One mesh, one material: no new draw calls.
#
# WINS_DEBUG=<folder> writes a sheet per building: each opening's depth picture with the outline found (red) and what is kept (blue).
import bpy, bmesh, sys, os, math, shutil
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index("--") + 1:]
work, out, pairs = os.path.abspath(a[0]), os.path.abspath(a[1]), a[2:]

PX = 1.0             # cm per texel of a window's depth picture
CELL = 20.0          # cm: window marks closer than about this are one window (its lights share a number: they light together)
PLANE_DOT = 0.85     # faces of one window within this of its main facing are one flat part
MARGIN = 40          # texels drawn round a window's marks
MIN_MARKS = 300.0    # cm2: fewer marks than this are no window (a stray lit triangle)
GL_TOL = 3.5         # cm: the 300k model's surface this close to the glass's depth is glass
GL_BACK = 8.0        # cm: (and no further behind it)
CLOSE_R = 6          # texels: (a window found from its marks alone) gaps up to twice this wide between them are closed
JOIN = 24            # texels: lights this close are one window opening (the bars or mullions between them inside it)
MIN_LIGHT = 100.0    # cm2: glass-depth patches smaller than this are no light of a window
MIN_OPEN = 350.0     # cm2: smaller openings are left alone
MIN_SIDE = 14.0      # cm: (and narrower ones)
LEAK = 45            # texels: glass depth running this far past the marks is the wall itself (a window with no frame step)
SHAPE_IOU = 0.80     # a clean shape must match the opening this well (overlap over union), else the outline round it
BOX_SLACK, BOX_SHARE = 40.0, 0.015   # texels, and share of the opening: a box that misses the opening by this little more than the best shape wins
KEEP_FROM = 12.0     # cm in front of the glass: marks further out than this are not on the glass
STRONG = 16.0        # cm in front of the glass: what reaches this far out inside the opening (iron bars, tracery, flowers) is kept,
WEAK = 8.0           # cm: all of it down to here (so a thing is kept whole or goes whole)
UNDER = 5.0          # cm: under a kept thing, only what is this close to the glass goes (the old glass, a muntin)
KEEP_IN = 3          # texels: (if it reaches this far into the opening: the frame's own inner edge is not such a thing)
TOP_OVER = 3.0       # cm past the frame's face: everything inside the opening up to here goes, unless kept above
BACK = 20.0          # cm behind the glass: (and from here)
FLANGE = 2.0         # cm: the glass runs on this far behind the frame, a touch deeper, so no crack shows round its edge

PANE_W, PANE_H = 30.0, 32.0   # cm: the size of pane the muntin grid aims for
BAR_HEIGHT = 1.5     # cm: how far the muntins stand proud of the glass
NO_GLASS_ABOVE = {"TL_Church": 1300.0}   # cm: no window above this (the church tower's belfry louvres are marked as windows)
FIRE_NEAR = 30.0     # cm: the low model's faces this near the 300k model's fire (the forge) are marked fire (0, 0, 0)
GLASS_SRGB = (0.16, 0.17, 0.19)   # (townlab_panes.py's painted pane colour: never picked as the frame's colour)
GLASS_TOL = 0.035
NOT = (0.0, 0.0, 1.0)     # every face that is not glass (B 1)
NOKEEP = {"TL_Church", "TL_CornerHouse"}   # buildings whose windows keep nothing of the old model inside the opening: their tracery and
                     # mullions sit too close to the glass to be kept cleanly (kept, the decimated stone came out as shards round the glass)
ROUND_BY_HAND = {"TL_Church": [(20, -621, 654, 120)]}   # (x, y, z, radius cm): the rose over the church door
BY_FRAME = {"TL_Church"}   # buildings whose openings are found from the moulding round them (see find_openings)
ROSE_GROW = 12.0     # cm: a rose is cut this far into its ring, past the ragged painted inner edge of the tracery
GROW = 1.5           # cm: every outline is cut this far into its frame, whose inner edge on the low model is ragged and often carries
GROW_BY = {"TL_Church": 3.0}   #   the old glass paint (a strip of pane colour along a sill; old red window paint on the church)
SHAPE_BY_HAND = {   # (x, y, z, radius cm, shape, width, height cm, which side of the box found stays: L or R; its foot always does):
    # three windows whose marks cover only part of the opening, measured off the WINS_DEBUG sheet
    "TL_CornerHouse": [(-367, 276, 489, 45, "arch", 69, 80, "L"), (43, -337, 138, 45, "arch", 40, 70, "R")],
    "TL_Church": [(-263, -75, 899, 45, "arch", 52, 76, "R")]}

def log(*x): print("WINS", *x, flush=True)
TAG = ""
DEBUG = os.environ.get("WINS_DEBUG", "")
SKIP = {"TL_PotionShop": [(239, -382, 527, 80), (-106, 27, 1221, 150)]}   # openings left alone by hand, (x, y, z, radius cm): the potion shop's little faceted oriel (found only as a sliver) and stray marks inside its roof lantern

def load(path):
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes): bpy.data.meshes.remove(m)
    bpy.ops.import_scene.fbx(filepath=path)
    o = next(o for o in bpy.data.objects if o.type == "MESH")
    for x in list(bpy.data.objects):
        if x is not o: bpy.data.objects.remove(x, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)   # (world metres in the vertices)
    return o

def arrays(me):
    nf = len(me.polygons)
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3) * 100.0   # cm
    me.calc_loop_triangles()
    tl = np.empty(len(me.loop_triangles) * 3, np.int64); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(-1, 3)
    tp = np.empty(len(me.loop_triangles), np.int64); me.loop_triangles.foreach_get("polygon_index", tp)
    lv = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lv)
    fn = np.empty(nf * 3); me.polygons.foreach_get("normal", fn); fn = fn.reshape(-1, 3)
    fa = np.empty(nf); me.polygons.foreach_get("area", fa); fa *= 1e4
    ls = np.empty(nf, np.int64); me.polygons.foreach_get("loop_start", ls)
    return co, co[lv[tl]], tp, fn, fa, ls

def marks(me, ls):
    at = me.color_attributes.get("WindowMask") or (me.color_attributes[0] if len(me.color_attributes) else None)
    if at is None: return None
    lc = np.empty(len(me.loops) * 4, np.float32); at.data.foreach_get("color", lc)
    return lc.reshape(-1, 4)[ls, :3]

def box_sum(x, r):
    p = np.pad(x.astype(np.float32), r); c = p.cumsum(0).cumsum(1); c = np.pad(c, ((1, 0), (1, 0)))
    k = 2 * r + 1
    return c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]
def dilate(m, r): return box_sum(m, r) > 0.5 if r > 0 else m
def erode(m, r): return box_sum(m, r) > (2 * r + 1) ** 2 - 0.5 if r > 0 else m

def parts(m):   # 4-connected parts: a label per texel (-1 outside)
    lab = np.full(m.shape, -1, np.int64); n = 0
    H, W = m.shape
    for y0, x0 in zip(*np.nonzero(m)):
        if lab[y0, x0] >= 0: continue
        st = [(y0, x0)]; lab[y0, x0] = n
        while st:
            y, x = st.pop()
            for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= yy < H and 0 <= xx < W and m[yy, xx] and lab[yy, xx] < 0: lab[yy, xx] = n; st.append((yy, xx))
        n += 1
    return lab, n

def fill_holes(m):
    p = np.pad(~m, 1, constant_values=True)
    lab, _ = parts(p)
    return ~(p & (lab == lab[0, 0]))[1:-1, 1:-1]

def hull(pts):   # convex hull, counter-clockwise (monotone chain)
    pts = sorted(set(map(tuple, pts)))
    if len(pts) < 3: return pts
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0: up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]

def draw(T2, Z, W, H):   # the front-most triangle per texel (T2 in texel units): its index (-1 where none) and its depth
    best = np.full((H, W), -np.inf); who = np.full((H, W), -1, np.int64)
    if not len(T2): return who, best
    lo = np.floor(T2.min(1)).astype(np.int64); hi = np.ceil(T2.max(1)).astype(np.int64)
    ext = (hi - lo).max(1) + 1
    for K in (4, 16, 64, 256, 1024):
        sel = np.nonzero((ext <= K) & (ext > K // 4 if K > 4 else ext <= K))[0]
        if not len(sel): continue
        oy, ox = np.mgrid[0:K, 0:K]; ox = ox.ravel()[None]; oy = oy.ravel()[None]
        step = max(1, 4000000 // (K * K))
        for c0 in range(0, len(sel), step):
            s = sel[c0:c0 + step]
            X = lo[s, 0, None] + ox; Y = lo[s, 1, None] + oy
            a_, b_, c_ = T2[s, 0, :, None], T2[s, 1, :, None], T2[s, 2, :, None]
            d = (b_[:, 1] - c_[:, 1]) * (a_[:, 0] - c_[:, 0]) + (c_[:, 0] - b_[:, 0]) * (a_[:, 1] - c_[:, 1])
            d = np.where(np.abs(d) < 1e-9, 1e-9, d)
            w0 = ((b_[:, 1] - c_[:, 1]) * (X + 0.5 - c_[:, 0]) + (c_[:, 0] - b_[:, 0]) * (Y + 0.5 - c_[:, 1])) / d
            w1 = ((c_[:, 1] - a_[:, 1]) * (X + 0.5 - c_[:, 0]) + (a_[:, 0] - c_[:, 0]) * (Y + 0.5 - c_[:, 1])) / d
            w2 = 1 - w0 - w1
            ok = (w0 >= -1e-3) & (w1 >= -1e-3) & (w2 >= -1e-3) & (X >= 0) & (Y >= 0) & (X < W) & (Y < H)
            ti, pi = np.nonzero(ok)
            if not len(ti): continue
            z = Z[s[ti], 0] * w0[ti, pi] + Z[s[ti], 1] * w1[ti, pi] + Z[s[ti], 2] * w2[ti, pi]
            yy, xx = Y[ti, pi], X[ti, pi]
            np.maximum.at(best, (yy, xx), z)
            win = z >= best[yy, xx] - 1e-6
            who[yy[win], xx[win]] = s[ti[win]]
    return who, best

def clusters(cen, cell):   # points joined within about a cell (26-connected cells): a label per point
    v = np.floor(cen / cell).astype(np.int64); v -= v.min(0)
    D = v.max(0) + 3; key = ((v[:, 0] + 1) * D[1] + v[:, 1] + 1) * D[2] + v[:, 2] + 1
    uk, inv = np.unique(key, return_inverse=True)
    nbi = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                if not (dx or dy or dz): continue
                o = (dx * D[1] + dy) * D[2] + dz
                j = np.minimum(np.searchsorted(uk, uk + o), len(uk) - 1)
                nbi.append(np.where(uk[j] == uk + o, j, np.arange(len(uk))))
    lab = np.arange(len(uk))
    for _ in range(10000):
        new = lab.copy()
        for j in nbi: new = np.minimum(new, lab[j])
        new = new[new]
        if np.array_equal(new, lab): break
        lab = new
    return lab[inv]

def area(poly):
    return 0.5 * abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly))))

def ccw(p):
    s = sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))
    return list(p) if s > 0 else list(reversed(p))

def inside(poly, H, W):
    """Texels whose centres lie inside a convex counter-clockwise polygon."""
    yy, xx = np.mgrid[0:H, 0:W]; X = xx + 0.5; Y = yy + 0.5
    m = np.ones((H, W), bool)
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        m &= (bx - ax) * (Y - ay) - (by - ay) * (X - ax) >= -1e-6
    return m

def in_convex(poly, x, y, m=0.0):
    """(x, y) at least m inside a counter-clockwise convex outline (m < 0: within -m outside it)."""
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        l = math.hypot(bx - ax, by - ay)
        if l > 1e-9 and ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) / l < m: return False
    return True

def edge_dist(poly, x, y):   # how far (x, y) is from a convex outline's edge
    best = 1e9
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        dx, dy = bx - ax, by - ay; l2 = dx * dx + dy * dy
        t = 0.0 if l2 < 1e-12 else min(1.0, max(0.0, ((x - ax) * dx + (y - ay) * dy) / l2))
        best = min(best, math.hypot(x - ax - t * dx, y - ay - t * dy))
    return best

def clip(subject, clipper):
    """A polygon cut to a convex counter-clockwise one (Sutherland-Hodgman)."""
    outp = list(subject)
    for i in range(len(clipper)):
        (ax, ay), (bx, by) = clipper[i], clipper[(i + 1) % len(clipper)]
        inp, outp = outp, []
        if not inp: break
        def side(p): return (bx - ax) * (p[1] - ay) - (by - ay) * (p[0] - ax)
        for j in range(len(inp)):
            p, q = inp[j], inp[(j + 1) % len(inp)]
            sp, sq = side(p), side(q)
            if sp >= 0: outp.append(p)
            if (sp >= 0) != (sq >= 0):
                t = sp / (sp - sq); outp.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    res = []
    for p in outp:   # (no doubled corners)
        if not res or math.hypot(p[0] - res[-1][0], p[1] - res[-1][1]) > 1e-4: res.append(p)
    if len(res) > 1 and math.hypot(res[0][0] - res[-1][0], res[0][1] - res[-1][1]) < 1e-4: res.pop()
    return res

def grow(poly, d):   # a convex counter-clockwise outline, every edge moved d outward
    n = len(poly); res = []
    for i in range(n):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % n]
        def nrm(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]; l = math.hypot(dx, dy) or 1.0
            return (dy / l, -dx / l)
        n1, n2 = nrm(p0, p1), nrm(p1, p2)
        bx, by = n1[0] + n2[0], n1[1] + n2[1]; k = 1.0 + n1[0] * n2[0] + n1[1] * n2[1]
        k = max(k, 0.3)
        res.append((p1[0] + d * bx / k, p1[1] + d * by / k))
    return res

def simplify(poly, tol):   # corners of a convex outline that stand less than tol off the line between their neighbours go
    poly = list(poly); again = True
    while again and len(poly) > 4:
        again = False
        for i in range(len(poly)):
            a_, b_, c_ = poly[i - 1], poly[i], poly[(i + 1) % len(poly)]
            l = math.hypot(c_[0] - a_[0], c_[1] - a_[1])
            if l < 1e-9 or abs((c_[0] - a_[0]) * (b_[1] - a_[1]) - (c_[1] - a_[1]) * (b_[0] - a_[0])) / l < tol:
                del poly[i]; again = True; break
    return poly

def shapes(bx0, bx1, by0, by1):
    """The clean shapes a window opening can be, in its box: (kind, outline, springing height)."""
    w, h = bx1 - bx0, by1 - by0; mx = (bx0 + bx1) / 2
    res = [("box", [(bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)], by1)]
    for k in (0.12, 0.2, 0.28, 0.36, 0.44, 0.5):       # a round (or flattened) arch: half an ellipse on a box
        a_ = k * w
        if a_ > 0.85 * h: continue
        top = [(mx + w / 2 * math.cos(t), by1 - a_ + a_ * math.sin(t)) for t in np.linspace(0, math.pi, 15)]
        res.append(("arch", [(bx0, by0), (bx1, by0)] + top, by1 - a_))
    for k in (0.6, 0.75, 0.9, 1.05, 1.2, 1.4):         # a pointed arch: two arcs meeting at the top
        a_ = k * w
        if a_ > 0.92 * h: continue
        c = (a_ * a_ - w * w / 4) / w; R = c + w / 2
        ts = np.linspace(0, a_, 9)
        right = [(mx + math.sqrt(max(R * R - t * t, 0.0)) - c, by1 - a_ + t) for t in ts]
        left = [(2 * mx - x, y) for (x, y) in reversed(right[:-1])]
        res.append(("pointed", [(bx0, by0), (bx1, by0)] + right + left, by1 - a_))
    for k in (0.1, 0.16, 0.22, 0.3):                   # a flat head with rounded corners (the bank's small windows)
        r = k * w
        if r > 0.6 * h: continue
        rc = [(bx1 - r + r * math.cos(t), by1 - r + r * math.sin(t)) for t in np.linspace(0, math.pi / 2, 6)]
        lc = [(2 * mx - x, y) for (x, y) in reversed(rc)]
        res.append(("arch", [(bx0, by0), (bx1, by0)] + rc + lc, by1 - r))
    if 0.75 < w / max(h, 1e-6) < 1.34:
        res.append(("round", [(mx + w / 2 * math.cos(t), (by0 + by1) / 2 + h / 2 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 25)[:-1]], by1))
    return res

def fit(om):
    """An opening's texels as a clean outline: the best of shapes(), or the outline round the texels. (kind, outline, springing)."""
    H, W = om.shape
    ys, xs = np.nonzero(om)
    pts = np.concatenate([np.stack([xs, ys], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])
    hl = hull(pts)
    tgt = inside(hl, H, W)
    # (the box the opening fills: where its rows and columns are at least a quarter as long as the longest, so a spike doesn't count)
    cx = tgt.sum(0); cy = tgt.sum(1)
    gx = np.nonzero(cx >= 0.25 * cx.max())[0]; gy = np.nonzero(cy >= 0.25 * cy.max())[0]
    bx0, bx1, by0, by1 = float(gx.min()), float(gx.max() + 1), float(gy.min()), float(gy.max() + 1)
    res = []
    for kind, poly, spring in shapes(bx0, bx1, by0, by1):
        m = inside(ccw(poly), H, W)
        res.append((int((m ^ tgt).sum()), (m & tgt).sum() / max((m | tgt).sum(), 1), kind, ccw(poly), spring))
    best = min(res, key=lambda r: r[0]); box = res[0]
    if box[0] <= best[0] + max(BOX_SLACK, BOX_SHARE * tgt.sum()): best = box    # (a box unless another shape is clearly closer)
    if best[1] >= SHAPE_IOU: return best[2], best[3], best[4], best[1]
    return ("outline", ccw(simplify(hl, 1.5)), float(ys.max() + 1), best[1])

def shut_all(lab, n, edge):   # which parts of a labelling do not touch the edge of the picture searched
    s = np.ones(max(n, 1), bool); s[np.unique(lab[edge & (lab >= 0)])] = False; return s

def find_openings(src, fc):
    """Every window opening of the 300k model: its plane, its clean outline (cm in that plane), the glass's depth, the frame's, and
    what stands in front of the glass that is to be kept."""
    me = src.data
    co, P, tp, fn, fa, ls = arrays(me)
    nf = len(fn)
    win = (fc[:, 1] > 0.5) & (fc[:, 2] < 0.5)
    cen = P.mean(1)
    fcen = np.zeros((nf, 3)); cnt = np.zeros(nf); np.add.at(fcen, tp, cen); np.add.at(cnt, tp, 1); fcen /= np.maximum(cnt, 1)[:, None]
    wi = np.nonzero(win)[0]
    if not len(wi): return []
    lab = clusters(fcen[wi], CELL)
    cands = []
    for L in np.unique(lab):
        left = np.zeros(nf, bool); left[wi[lab == L]] = True
        while left.any() and fa[left].sum() > MIN_MARKS:
            n = (fn[left] * fa[left, None]).sum(0); n /= max(np.linalg.norm(n), 1e-9)
            part = left & ((fn @ n) > PLANE_DOT)
            if fa[part].sum() < MIN_MARKS: break
            n = (fn[part] * fa[part, None]).sum(0); n /= max(np.linalg.norm(n), 1e-9)
            part = left & ((fn @ n) > PLANE_DOT)
            if not part.any(): break
            left &= ~part
            if abs(n[2]) > 0.25 or fa[part].sum() < MIN_MARKS: continue   # (a sill or a muntin's top: no window faces up)
            cands.append((float(fa[part].sum()), n, np.nonzero(part)[0]))
    cands.sort(key=lambda c: -c[0])
    found = []; tiles = []; njunk = 0; nsmall = 0
    for (ar, n, pidx) in cands:
        # (marks on the sides of a window's own muntins and frame make "windows" facing sideways inside it: skipped)
        junk = False
        for g in found:
            d = fcen[pidx] - g["c0"]; x = d @ g["h"]; y = d @ g["v"]; z = d @ g["n"]
            bx = [p[0] for p in g["poly"]]; by = [p[1] for p in g["poly"]]
            ins = (x > min(bx) - 8) & (x < max(bx) + 8) & (y > min(by) - 8) & (y < max(by) + 8) & (z > g["zg"] - 25) & (z < g["zg"] + 50)
            if fa[pidx][ins].sum() > 0.5 * ar: junk = True; break
        if junk: njunk += 1; continue
        part = np.zeros(nf, bool); part[pidx] = True
        # (the plane of the glass from where its marked faces lie, not from which way they face: Meshy's speckled marks face
        # every way, and a plane a few degrees off puts half the glass "in front" of itself)
        pc_ = fcen[pidx]; wt_ = fa[pidx]; sel_ = np.ones(len(pidx), bool)
        for _ in range(2):
            cm_ = (pc_[sel_] * wt_[sel_, None]).sum(0) / wt_[sel_].sum()
            q_ = (pc_[sel_] - cm_) * np.sqrt(wt_[sel_, None])
            ev_, evec_ = np.linalg.eigh(q_.T @ q_)
            nr_ = evec_[:, 0] * (1.0 if evec_[:, 0] @ n > 0 else -1.0)
            if ev_[0] > 0.1 * ev_[1] or nr_ @ n < 0.9 or abs(nr_[2]) > 0.25: break     # (marks in a strip, or not flat: the faces' own facing stays)
            n = nr_
            s2 = np.abs((pc_ - cm_) @ n) < 3.0
            if wt_[s2].sum() < 0.5 * wt_.sum(): break
            sel_ = s2
        h = np.cross([0.0, 0.0, 1.0], n); h /= np.linalg.norm(h); v = np.cross(n, h)
        mine = part[tp]
        c0 = cen[mine].mean(0)
        Hm = (P[mine] - c0) @ h; Vm = (P[mine] - c0) @ v
        x0, x1, y0, y1 = Hm.min() - MARGIN, Hm.max() + MARGIN, Vm.min() - MARGIN, Vm.max() + MARGIN
        ch, cv, cd = (cen - c0) @ h, (cen - c0) @ v, (cen - c0) @ n
        near = (cd > -50) & (cd < 100) & (ch > x0 - 30) & (ch < x1 + 30) & (cv > y0 - 30) & (cv < y1 + 30) & ((fn[tp] @ n) > 0.0)
        idx = np.nonzero(near)[0]
        W, H = int(math.ceil((x1 - x0) / PX)), int(math.ceil((y1 - y0) / PX))
        T2 = np.stack([((P[idx] - c0) @ h - x0) / PX, ((P[idx] - c0) @ v - y0) / PX], -1)
        Zt = (P[idx] - c0) @ n
        who, D = draw(T2, Zt, W, H); ok = who >= 0
        gi = np.nonzero(part[tp[idx]])[0]
        w2, Dm = draw(T2[gi], Zt[gi], W, H); alone = w2 >= 0
        if alone.sum() * PX * PX < MIN_MARKS: nsmall += 1; continue
        zg = float(np.percentile(Dm[alone], 15)) + 0.5        # (the glass: the deeper of the marks; some lie on a muntin or on tracery)
        alone &= Dm < zg + KEEP_FROM
        GL = ok & (D <= zg + GL_TOL) & (D >= zg - GL_BACK)
        ys, xs = np.nonzero(alone)
        if not len(xs): nsmall += 1; continue
        mb = (xs.min(), xs.max() + 1, ys.min(), ys.max() + 1)
        def boxmask(g_):
            m = np.zeros((H, W), bool); m[max(0, mb[2] - g_):mb[3] + g_, max(0, mb[0] - g_):mb[1] + g_] = True; return m
        # the opening: glass depth joined to the marks, its muntins closed over; if that runs on past the marks (no frame step
        # there: the wall is at the glass's depth), only what lies within a hand of the marks; failing that the marks themselves
        how = "depth"
        S0 = (alone | GL) & boxmask(LEAK + 6)
        S0 = dilate(erode(S0, 1), 1) & S0
        lab2, nl = parts(S0)
        size_ = np.bincount(lab2[lab2 >= 0], minlength=max(nl, 1)); mark_ = np.bincount(lab2[alone & (lab2 >= 0)], minlength=max(nl, 1))
        is_seed = (size_ * PX * PX >= MIN_LIGHT) & (mark_ >= np.maximum(0.1 * size_, 40))
        usual = np.median(size_[is_seed]) if is_seed.any() else 0
        sb_ = boxmask(LEAK + 6); edge_ = sb_ & ~erode(sb_, 1)
        shut = np.ones(max(nl, 1), bool); shut[np.unique(lab2[edge_ & (lab2 >= 0)])] = False    # (glass depth running off the picture is wall, not a light)
        light = np.isin(lab2, np.nonzero(is_seed | (shut & (size_ >= max(MIN_LIGHT / (PX * PX), 0.3 * usual)) & (size_ <= 3 * max(usual, 1))))[0])
        seed = np.isin(lab2, np.nonzero(is_seed)[0])
        # (a patch the marks missed is a light of this window only if what lies between it and the marked lights is lower than
        # the frame: a bar or a mullion, not the sill or the frame itself with bare wall beyond it)
        if seed.any() and (light & ~seed).any():
            sy_, sx_ = np.nonzero(seed)
            sh_ = inside(hull(np.concatenate([np.stack([sx_, sy_], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])), H, W)
            rg_ = dilate(sh_, 6) & ~dilate(sh_, 1) & ok & (D < zg + 60)
            zf0 = float(np.clip(np.percentile(D[rg_], 75), zg + 2, zg + 45)) if rg_.sum() > 20 else zg + 2
            near_seed = dilate(seed, JOIN)
            for q in np.unique(lab2[light & ~seed]):
                um = lab2 == q
                br = dilate(um, JOIN) & near_seed & ~um & ~seed & ok
                uy_, ux_ = np.nonzero(um); th_ = 0.1 * (sy_.max() - sy_.min() + 1); tw_ = 0.1 * (sx_.max() - sx_.min() + 1)
                if (abs(uy_.min() - sy_.min()) <= th_ and abs(uy_.max() - sy_.max()) <= th_) or (abs(ux_.min() - sx_.min()) <= tw_ and abs(ux_.max() - sx_.max()) <= tw_): continue   # (a light just like the marked ones, beside or above them)
                if br.sum() < 10 or np.median(D[br]) >= zf0 - 3.0: light &= ~um
        # (the lights of one window, a hand or two of bar or mullion between them, are one opening: the four panes of the store
        # in their board, the three lights of the bank behind its bars; lights the marks missed count too)
        glab, gn = parts(dilate(light, JOIN // 2))
        keep = np.zeros((H, W), bool)
        for k in range(gn):
            gm = (glab == k) & light
            if (gm & seed).any(): keep |= erode(dilate(gm, JOIN // 2 + 1), JOIN // 2 + 1)
        ky, kx = np.nonzero(keep)
        if not len(kx) or kx.min() < mb[0] - LEAK or kx.max() + 1 > mb[1] + LEAK or ky.min() < mb[2] - LEAK or ky.max() + 1 > mb[3] + LEAK:
            how = "near"
            band = boxmask(8) & ~boxmask(1) & ok
            if band.any() and GL[band].mean() > 0.6: how = "marks"
            S = (alone | (GL & boxmask(8))) if how == "near" else alone
            keep = erode(dilate(S, CLOSE_R), CLOSE_R)
        if TAG in BY_FRAME and keep.any():
            # (a church window: its lights sit in stone tracery nearly level with the wall, and the opening to rebuild is the whole
            # arch inside the moulding round them, not just the lights: everything lower than that moulding, joined to the lights)
            ky_, kx_ = np.nonzero(keep)
            hm_ = inside(hull(np.concatenate([np.stack([kx_, ky_], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])), H, W)
            far_ = dilate(hm_, 25) & ~hm_ & ok & (D < zg + 45)
            if far_.sum() > 50:
                lvl = max(zg + WEAK, float(np.percentile(D[far_], 90)) - 4.0)
                ll_, ln_ = parts((~ok | (D < lvl)) & sb_)
                pick = np.unique(ll_[keep & (ll_ >= 0)])
                reg = np.isin(ll_, pick)
                if reg.any() and not (reg & edge_).any():
                    keep = fill_holes(erode(dilate(reg, CLOSE_R), CLOSE_R)); how = "frame"
        keep = fill_holes(keep)
        keep = dilate(erode(keep, 2), 2) & keep
        lab2, nl = parts(keep)
        tile_polys = []
        for k in range(nl):
            om = lab2 == k
            if om.sum() * PX * PX < MIN_OPEN or (om & alone).sum() < 0.08 * om.sum(): continue
            oy_, ox_ = np.nonzero(om)
            tgt = inside(hull(np.concatenate([np.stack([ox_, oy_], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])), H, W)
            if how == "marks" and (om & alone).sum() < 0.6 * tgt.sum(): continue   # (lit specks on a bare wall: no window)
            # (where the frame itself covers the edge of the marked glass, the opening stops at the frame: an arched frame over
            # glass marked square; what hangs well in front of the wall, a flower box or a cloth, hides glass that is still there)
            ring = dilate(tgt, 6) & ~dilate(tgt, 1) & ok & (D < zg + 60)
            zf = float(np.clip(np.percentile(D[ring], 75), zg + 2, zg + 45)) if ring.sum() > 20 else zg + 2
            cov = ok & (D > max(zg + WEAK, zf - 3.0)) & (D <= zf + TOP_OVER)
            vis_t = om & ~cov
            if vis_t.sum() < 0.3 * om.sum(): vis_t = om
            kind, poly, spring, iou = fit(vis_t)
            cw_ = c0 + h * (x0 + ox_.mean() * PX) + v * (y0 + oy_.mean() * PX) + n * zg
            if any(np.linalg.norm(cw_ - np.array(s[:3])) < s[3] for s in ROUND_BY_HAND.get(TAG, [])):
                # (a rose: one round glass in the stone ring, as big as the ring round every hole of its tracery)
                ry, rx = np.nonzero(om)     # (the whole opening inside the ring: see BY_FRAME)
                hp = np.array(hull(np.stack([rx, ry], 1)), float) + 0.5
                cc = hp.mean(0)
                for it in range(200):
                    far = hp[np.argmax(np.linalg.norm(hp - cc, axis=1))]; cc = cc + (far - cc) / (it + 2)
                rr = float(np.linalg.norm(hp - cc, axis=1).max()) + ROSE_GROW / PX
                poly = [(cc[0] + rr * math.cos(t), cc[1] + rr * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 29)[:-1]]
                kind, spring, iou = "round", cc[1] + rr, 1.0
            for s in SHAPE_BY_HAND.get(TAG, []):
                if np.linalg.norm(cw_ - np.array(s[:3])) < s[3]:
                    qx = [p[0] for p in poly]; qy = [p[1] for p in poly]; ww, hh = s[5] / PX, s[6] / PX
                    nx0 = min(qx) if "L" in s[7] else max(qx) - ww if "R" in s[7] else (min(qx) + max(qx) - ww) / 2
                    ny0 = min(qy)
                    vy_, vx_ = np.nonzero(vis_t)
                    tg_ = inside(hull(np.concatenate([np.stack([vx_, vy_], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])), H, W)
                    cand = [(int((inside(ccw(c[1]), H, W) ^ tg_).sum()), c) for c in shapes(nx0, nx0 + ww, ny0, ny0 + hh) if c[0] == s[4]]
                    if s[4] == "arch":   # (a true round head, rising about a third of its width: the marks are no guide to it here)
                        cand = [(abs((ny0 + hh - c[2]) / ww - 0.32) + (1.0 if len(c[1]) < 17 else 0.0), c) for (_, c) in cand]
                    kind, poly, spring = min(cand, key=lambda c: c[0])[1]; poly = ccw(poly); iou = 1.0
            poly = ccw(grow(poly, GROW_BY.get(TAG, GROW) / PX))     # (a little into the frame: see GROW)
            px_ = [p[0] for p in poly]; py_ = [p[1] for p in poly]
            w_, h_ = (max(px_) - min(px_)) * PX, (max(py_) - min(py_)) * PX
            if min(w_, h_) < MIN_SIDE: continue
            if kind == "outline" and (how != "depth" or area(poly) * PX * PX < 1500): continue    # (no clean shape and no frame: no window)
            if how == "marks" and iou < 0.9: continue
            inm = inside(poly, H, W)
            ring = dilate(inm, 6) & ~dilate(inm, 1) & ok & (D < zg + 60)
            ztop = float(np.clip(np.percentile(D[ring], 75), zg + 2, zg + 45)) if ring.sum() > 20 else zg + 2
            if ztop - zg < 3.0 and (inm & alone).sum() < 0.5 * inm.sum(): continue          # (no frame and mostly unmarked: the wall)
            # what stands well in front of the glass inside the opening is kept whole (iron bars, tracery, a flower box): anything
            # reaching STRONG out, followed down to WEAK; the rest (muntins, leading, a slim mullion, the edge of the frame) goes
            weak = ok & inm & (D > zg + WEAK)
            wl, wn = parts(weak)
            strong = weak & (D > zg + STRONG) & erode(inm, KEEP_IN) & (TAG not in NOKEEP)
            kept = (np.isin(wl, np.unique(wl[strong])) & weak) if strong.any() else np.zeros((H, W), bool)
            # (all that is not kept goes, however far out it stands: the edge of the frame, a knot or hinge poking in over the glass;
            # left in, such stubs showed as dark notches in lit glass and muntin ends ran on under them, 2026-11-15)
            top_ = zg + 60.0
            tdel = np.where(dilate(kept, 2), zg + UNDER, top_).astype(np.float32)
            # the lights the muntin grids go in: the whole opening, or if kept things part it, each part big enough to want dividing
            free = inm & ~dilate(kept, 1)
            fl_, fn_ = parts(free); lights = None
            if fn_:
                fs = np.bincount(fl_[fl_ >= 0], minlength=fn_); big = int(np.argmax(fs))
                by_, bx_ = np.nonzero(fl_ == big)
                if (bx_.max() + 1 - bx_.min()) * (by_.max() + 1 - by_.min()) < 0.6 * (max(px_) - min(px_)) * (max(py_) - min(py_)):
                    lights = []
                    for q in range(fn_):
                        if fs[q] * PX * PX < 400: continue
                        qy, qx = np.nonzero(fl_ == q)
                        lights.append((x0 + qx.min() * PX, x0 + (qx.max() + 1) * PX, y0 + qy.min() * PX, y0 + (qy.max() + 1) * PX))
            fr_ = D[ok & inm & (D > zg + GL_TOL) & (D < zg + 60)] - zg
            stats = "in front %.2f of it at %s cm" % (len(fr_) / max(inm.sum(), 1), np.round(np.percentile(fr_, [25, 50, 75, 90]), 1) if len(fr_) else "-")
            own = float(kept.sum()) / max(inm.sum(), 1)
            R = float(np.median(fc[pidx, 0]))
            cmx, cmy = np.mean(px_), np.mean(py_)
            centre = c0 + h * (x0 + cmx * PX) + v * (y0 + cmy * PX) + n * zg
            g = dict(n=n, c0=c0, h=h, v=v, zg=zg, ztop=ztop, R=R, kind=kind, own=own, how=how, centre=centre, lights=lights,
                     poly=[(x0 + p[0] * PX, y0 + p[1] * PX) for p in poly], spring=y0 + spring * PX,
                     tdel=tdel, org=(x0, y0), iou=iou)
            if TAG in NO_GLASS_ABOVE and centre[2] > NO_GLASS_ABOVE[TAG]: log("left alone (too high)", TAG, np.round(centre).astype(int)); continue
            if any(np.linalg.norm(centre - q["centre"]) < 15.0 and q["n"] @ n > 0.9 for q in found): continue   # (found already)
            if any(np.linalg.norm(centre - np.array(s[:3])) < s[3] for s in SKIP.get(TAG, [])): log("left alone (by hand)", TAG, np.round(centre).astype(int)); continue
            found.append(g); tile_polys.append((poly, kept))
            log("opening", TAG, len(found) - 1, kind, "%.0fx%.0f cm" % (w_, h_), "at", np.round(centre).astype(int), "facing", np.round(n, 2),
                "fit %.2f" % iou, "by", how, "frame %.1f cm proud" % (ztop - zg), "kept %.2f" % own, "lights", "whole" if lights is None else len(lights), stats, "R %.3f" % R)
        if DEBUG and tile_polys:
            img = np.zeros((H, W, 3), np.float32)
            gimg = np.clip((D - zg + 10) / 50.0, 0, 1); gimg[~ok] = 0
            img[:] = gimg[..., None]
            vis = np.zeros((H, W), bool); vis[ok] = part[tp[idx[who[ok]]]]
            img[vis, 1] = np.clip(img[vis, 1] + 0.3, 0, 1)
            for poly, kept in tile_polys:
                img[kept, 2] = np.clip(img[kept, 2] + 0.5, 0, 1)
                for i in range(len(poly)):
                    (xa, ya), (xb, yb) = poly[i], poly[(i + 1) % len(poly)]
                    for t in np.linspace(0, 1, int(max(abs(xb - xa), abs(yb - ya))) * 2 + 2):
                        x, y = int(min(W - 1, max(0, xa + (xb - xa) * t))), int(min(H - 1, max(0, ya + (yb - ya) * t)))
                        img[y, x] = (1, 0, 0)
            tiles.append(img)
    if DEBUG and tiles:
        cols = 6; ch = max(t.shape[0] for t in tiles); cw = max(t.shape[1] for t in tiles); rows = (len(tiles) + cols - 1) // cols
        sheet = np.zeros((rows * (ch + 4), cols * (cw + 4), 4), np.float32); sheet[..., 3] = 1; sheet[..., 2] = 0.25
        for i, t in enumerate(tiles):
            r = rows - 1 - i // cols; c = i % cols
            sheet[r * (ch + 4): r * (ch + 4) + t.shape[0], c * (cw + 4): c * (cw + 4) + t.shape[1], :3] = t
        sheet = np.repeat(np.repeat(sheet, 2, 0), 2, 1)
        im = bpy.data.images.new("sheet", sheet.shape[1], sheet.shape[0], alpha=True); im.pixels.foreach_set(sheet.ravel())
        im.filepath_raw = os.path.join(DEBUG, "%s_openings.png" % TAG); im.file_format = "PNG"; im.save(); bpy.data.images.remove(im)
    log("windows", TAG, "marked parts", len(cands), "inside another window", njunk, "too small", nsmall, "openings", len(found),
        {k: sum(1 for g in found if g["kind"] == k) for k in ("box", "arch", "pointed", "round", "outline")})
    return found

def hit_uv(me, uvl, co, fi, p):
    """The building's UV where a trace hit face fi at p (cm)."""
    pg = me.polygons[fi]
    if pg.loop_total != 3: return tuple(uvl[pg.loop_start].uv)
    A, B, C = (co[me.loops[pg.loop_start + k].vertex_index] for k in range(3))
    v0, v1, v2 = B - A, C - A, p - A
    d00, d01, d11, d20, d21 = v0 @ v0, v0 @ v1, v1 @ v1, v2 @ v0, v2 @ v1
    den = d00 * d11 - d01 * d01
    if abs(den) < 1e-12: return tuple(uvl[pg.loop_start].uv)
    b1 = (d11 * d20 - d01 * d21) / den; b2 = (d00 * d21 - d01 * d20) / den; b0 = 1 - b1 - b2
    return tuple(b0 * np.array(uvl[pg.loop_start].uv) + b1 * np.array(uvl[pg.loop_start + 1].uv) + b2 * np.array(uvl[pg.loop_start + 2].uv))

def frame_uv(cands, base, nrm):
    """One texel of the building's own painted frame round a window, for its lining and its muntins: the middle shade of that frame,
    where the picture is even and its normal flat (everything new points at it: one even colour, no smudge, no tilted light)."""
    U = np.array(cands, np.float64)
    if base is None: return tuple(U[0])
    Sy, Sx = base.shape[:2]
    ix = np.clip((U[:, 0] % 1.0 * Sx).astype(np.int64), 1, Sx - 2); iy = np.clip((U[:, 1] % 1.0 * Sy).astype(np.int64), 1, Sy - 2)
    col = base[iy, ix, :3]; lum = col @ np.array([0.3, 0.59, 0.11])
    ok = np.abs(col - np.array(GLASS_SRGB)).max(-1) > 2 * GLASS_TOL   # (never the painted pane colour)
    if not ok.any(): ok[:] = True
    med = np.median(lum[ok])
    spread = np.max([np.abs(base[iy + dy, ix + dx, :3] - col).max(-1) for dy in (-1, 0, 1) for dx in (-1, 0, 1)], 0)
    score = np.abs(lum - med) + spread + np.where(ok, 0.0, 10.0)
    if nrm is not None:
        Ny, Nx = nrm.shape[:2]
        score = score + np.abs(nrm[np.clip((U[:, 1] % 1.0 * Ny).astype(np.int64), 0, Ny - 1), np.clip((U[:, 0] % 1.0 * Nx).astype(np.int64), 0, Nx - 1), :2] - 0.5).sum(-1)
    i = int(np.argmin(score))
    return ((ix[i] + 0.5) / Sx, (iy[i] + 0.5) / Sy)

def grid(bx0, bx1, by0, by1, bw, spring=None, over=5.0):
    """The bars of a muntin grid in a box (an arch springing at spring gets a bar there and its uprights run on into the head)."""
    w, h = bx1 - bx0, by1 - by0
    head = (by1 - spring) if spring is not None else 0.0
    arch = head >= 12
    body = (spring - by0) if arch else h
    # (bigger windows get bigger panes: the potion shop front at 30 cm a pane was forty little squares)
    cols = max(2 if w >= 36 else 1, int(round(w / (PANE_W * max(1.0, math.sqrt(w / 120.0))))))
    rows = max(2 if body >= 40 else 1, int(round(body / (PANE_H * max(1.0, math.sqrt(body / 120.0))))))
    if cols == 1 and rows == 1 and not arch:
        if h >= w: rows = 2
        else: cols = 2
    bars = []
    for i in range(1, cols):
        x = bx0 + w * i / cols
        bars.append(([(x - bw / 2, by0 - over), (x + bw / 2, by0 - over), (x + bw / 2, by1 + over), (x - bw / 2, by1 + over)], BAR_HEIGHT))
    for j in range(1, rows + (1 if arch else 0)):
        y = by0 + body * j / rows
        bars.append(([(bx0 - over, y - bw / 2), (bx1 + over, y - bw / 2), (bx1 + over, y + bw / 2), (bx0 - over, y + bw / 2)], BAR_HEIGHT - 0.2))
    return bars

def muntins(g):
    """The bars of a window, each (outline in the plane of the window, height proud of the glass): a grid sized to the window, spokes
    and a hub on a round light; where the window keeps bars or tracery of its own, a grid only in the parts still big enough to want one."""
    poly = g["poly"]; xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
    bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys); w, h = bx1 - bx0, by1 - by0
    bw = min(4.0, max(2.4, 0.045 * min(w, h)))
    if g["lights"] is not None:
        bars = []
        for (lx0, lx1, ly0, ly1) in g["lights"]:
            if (lx1 - lx0 < 34 and ly1 - ly0 < 40) or min(lx1 - lx0, ly1 - ly0) < 30: continue
            bars += grid(lx0, lx1, ly0, ly1, bw, None, 2.0)
        return bars
    if g["kind"] == "round":
        mx, my = (bx0 + bx1) / 2, (by0 + by1) / 2; r = max(w, h); bars = []
        nsp = 2 if min(w, h) < 70 else 4
        for k in range(nsp):
            t = math.pi * k / nsp
            dx, dy = math.cos(t), math.sin(t); ox, oy = -dy * bw / 2, dx * bw / 2
            bars.append(([(mx - dx * r + ox, my - dy * r + oy), (mx - dx * r - ox, my - dy * r - oy), (mx + dx * r - ox, my + dy * r - oy), (mx + dx * r + ox, my + dy * r + oy)], BAR_HEIGHT - 0.12 * k))
        if nsp == 4:
            hr = max(0.1 * min(w, h), 1.6 * bw)
            bars.append(([(mx + hr * math.cos(t), my + hr * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 13)[:-1]], BAR_HEIGHT + 0.3))
        return bars
    return grid(bx0, bx1, by0, by1, bw, min(g["spring"], by1))

def rebuild(o, found, base=None, nrm=None, fire_pts=None):
    """Cut each old window out of the low model o (world metres) and build the new one in: see the top of the file."""
    me = o.data
    nf0 = len(me.polygons)
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3) * 100.0   # (cm)
    bvh = BVHTree.FromPolygons([Vector(c) for c in co], [tuple(p.vertices) for p in me.polygons], all_triangles=False)
    bvh_m = BVHTree.FromPolygons([Vector(c / 100.0) for c in co], [tuple(p.vertices) for p in me.polygons], all_triangles=False)   # (in metres, for the open check)
    uvl = me.uv_layers[0].data
    # (the model's own shading normals ride along through the cuts as a corner attribute)
    cn = np.empty(len(me.loops) * 3, np.float32); me.corner_normals.foreach_get("vector", cn)
    at = me.attributes.new("cn_keep", "FLOAT_VECTOR", "CORNER"); at.data.foreach_set("vector", cn)
    # the frame's colour for every window, found on the untouched model
    for g in found:
        n = g["n"]; nn = Vector(n); c0 = g["c0"]; h = g["h"]; v = g["v"]; poly = g["poly"]
        fuv = []; anyuv = None
        for ring in (grow(poly, 2.0), grow(poly, 4.0)):
            for i in range(len(ring)):
                (xa, ya), (xb, yb) = ring[i], ring[(i + 1) % len(ring)]
                for t in np.linspace(0, 1, max(2, int(math.hypot(xb - xa, yb - ya) / 3.0)), endpoint=False):
                    x, y = xa + (xb - xa) * t, ya + (yb - ya) * t
                    hit = bvh.ray_cast(Vector(c0 + h * x + v * y + n * (g["ztop"] + 8.0)), -nn, 60.0)
                    if hit[0] is None: continue
                    hp = np.array(hit[0]); uv_ = hit_uv(me, uvl, co, hit[2], hp)
                    if anyuv is None: anyuv = uv_
                    if (hp - c0) @ n > g["zg"] + 1.0: fuv.append(uv_)
        g["uv"] = frame_uv(fuv, base, nrm) if fuv else (anyuv or (0.5, 0.5))
    bm = bmesh.new(); bm.from_mesh(me)
    uvb = bm.loops.layers.uv[0]
    pl_ = bm.faces.layers.int.new("pane")      # (0 the building, -1 a new face that is not glass, i + 1 the i-th window's glass)
    Rs = []; tot = dict(deleted=0, lining=0, bars=0, cut=0, nomunt=0, repaint=0)
    for g in found:
        n = np.array(g["n"]); nn = Vector(n); c0 = np.array(g["c0"]); h = np.array(g["h"]); v = np.array(g["v"]); poly = g["poly"]
        zg = g["zg"]; tdel = g["tdel"]; ox, oy = g["org"]; uv = g["uv"]
        xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
        bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
        zmax = float(tdel.max())
        def W3(x, y, z): return Vector((c0 + h * x + v * y + n * z) / 100.0)
        def to2(p):
            d = np.array(p) * 100.0 - c0
            return float(d @ h), float(d @ v), float(d @ n)
        # the faces round the opening
        bm.verts.ensure_lookup_table()
        vc = np.array([vv.co[:] for vv in bm.verts]) * 100.0 - c0
        X, Y, Z = vc @ h, vc @ v, vc @ n
        vs = np.nonzero((X > bx0 - 60) & (X < bx1 + 60) & (Y > by0 - 60) & (Y < by1 + 60) & (Z > zg - BACK - 60) & (Z < zmax + 60))[0]
        cur = set()
        for i in vs:
            for f in bm.verts[i].link_faces: cur.add(f)
        near = set()
        for f in cur:
            pts = [to2(vv.co) for vv in f.verts]
            if max(p[0] for p in pts) < bx0 - 0.5 or min(p[0] for p in pts) > bx1 + 0.5 or max(p[1] for p in pts) < by0 - 0.5 or min(p[1] for p in pts) > by1 + 0.5: continue
            if max(p[2] for p in pts) < zg - BACK or min(p[2] for p in pts) > zmax: continue
            near.add(f)
        around = cur; cur = near
        # cut them along every edge of the outline
        for i in range(len(poly)):
            (xa, ya), (xb, yb) = poly[i], poly[(i + 1) % len(poly)]
            ex, ey = xb - xa, yb - ya; L = math.hypot(ex, ey)
            if L < 1e-6: continue
            ex, ey = ex / L, ey / L; nx, ny = ey, -ex     # (outward of a counter-clockwise outline)
            geo = []
            for f in cur:
                if not f.is_valid: continue
                pts = [to2(vv.co) for vv in f.verts]
                d = [(p[0] - xa) * nx + (p[1] - ya) * ny for p in pts]
                if max(d) < 0.01 or min(d) > -0.01: continue
                s = [(p[0] - xa) * ex + (p[1] - ya) * ey for p in pts]
                if max(s) < -0.5 or min(s) > L + 0.5: continue
                geo.append(f)
            if not geo: continue
            ge = set(geo)
            for f in geo:
                ge.update(f.edges); ge.update(f.verts)
            res = bmesh.ops.bisect_plane(bm, geom=list(ge), dist=1e-6, plane_co=W3(xa, ya, zg), plane_no=Vector(h * nx + v * ny), clear_inner=False, clear_outer=False)
            cur = set(f for f in cur if f.is_valid) | set(e for e in res["geom"] if isinstance(e, bmesh.types.BMFace) and e.is_valid)
            tot["cut"] += len(geo)
        # what lies inside the outline, from behind the glass out to the frame's face (less under the things kept): deleted
        dele = set(); TH, TW = tdel.shape
        for f in cur:
            if not f.is_valid: continue
            x, y, z = to2(f.calc_center_median())
            if not in_convex(poly, x, y, 0.0): continue
            lim = float(tdel[min(TH - 1, max(0, int((y - oy) / PX))), min(TW - 1, max(0, int((x - ox) / PX)))])
            if zg - BACK <= z <= lim: dele.add(f)
        edges = []; seen = set()
        for f in dele:
            for e in f.edges:
                if e in seen: continue
                kept = [k for k in e.link_faces if k not in dele]
                if not kept: continue
                seen.add(e)
                lk = next(l for l in kept[0].loops if l.edge == e)     # (the kept face runs along the edge from lk.vert to the next)
                uvs = {l.vert: tuple(l[uvb].uv) for l in kept[0].loops if l.vert in e.verts}
                edges.append((lk.vert, lk.link_loop_next.vert, uvs))
        bmesh.ops.delete(bm, geom=list(dele), context="FACES"); tot["deleted"] += len(dele)
        # What is left of the old glass just outside the outline (the low model's opening is a few cm bigger here and there, and its
        # glass carries the pane paint townlab_panes.py gave it: a strip of dark glass along a sill, 2026-11-15) is painted the frame's
        # colour, as the back of the recess. Only faces that ARE pane-painted, near the glass's depth, within a hand of the outline.
        if base is not None:
            Sy_, Sx_ = base.shape[:2]
            def pane_paint(u_, v_):
                c_ = base[min(Sy_ - 1, max(0, int((v_ % 1.0) * Sy_))), min(Sx_ - 1, max(0, int((u_ % 1.0) * Sx_))), :3]
                return bool(np.abs(c_ - np.array(GLASS_SRGB)).max() < GLASS_TOL)
            for f in set(around) | set(cur):
                if not f.is_valid or f[pl_] != 0 or f.normal.dot(nn) < -0.2: continue
                pts = [to2(vv.co) for vv in f.verts]
                if not all(abs(p[2] - zg) < 8.0 and in_convex(poly, p[0], p[1], -12.0) and not in_convex(poly, p[0], p[1], 0.3) for p in pts): continue
                us = [tuple(l[uvb].uv) for l in f.loops]
                mid_ = (sum(q[0] for q in us) / len(us), sum(q[1] for q in us) / len(us))
                smp = [mid_] + [((q[0] + mid_[0]) / 2, (q[1] + mid_[1]) / 2) for q in us]
                if sum(pane_paint(*q) for q in smp) * 2 > len(smp):
                    for l in f.loops: l[uvb].uv = uv
                    f[pl_] = -1; tot["repaint"] += 1
        def face(vl, uvs, want, kind):
            """A new face on vl, turned so its normal points along want (None: as wound), its corners' UVs from uvs (a list, or
            one UV for all)."""
            try: f = bm.faces.new(vl)
            except ValueError: return None
            f.normal_update()
            um = {vv: (uvs[k] if isinstance(uvs, list) else uvs) for k, vv in enumerate(vl)}
            if want is not None and f.normal.dot(want) < 0: f.normal_flip()
            for l in f.loops: l[uvb].uv = um[l.vert]
            f[pl_] = kind
            return f
        # the lining: from every cut edge straight back to the glass, wound as the face it replaces was (the other way along the
        # edge from the kept face), so it faces the open air as that face did
        foot = {}
        def down(vv):
            if vv not in foot:
                x, y, z = to2(vv.co); foot[vv] = bm.verts.new(W3(x, y, zg))
            return foot[vv]
        for (va, vb, uvs) in edges:
            if not (va.is_valid and vb.is_valid): continue
            pa, pb = to2(va.co), to2(vb.co)
            ua, ub = pa[2] > zg + 0.05, pb[2] > zg + 0.05
            if not (ua or ub): continue
            if math.hypot(pa[0] - pb[0], pa[1] - pb[1]) < 0.02: continue   # (an edge running straight back: nothing to hang from it)
            onrim = edge_dist(poly, pa[0], pa[1]) < 0.3 and edge_dist(poly, pb[0], pb[1]) < 0.3
            vl = [vb, va] + ([down(va)] if ua else []) + ([down(vb)] if ub else [])
            ul = [uvs.get(vb, uv), uvs.get(va, uv)] + ([uvs.get(va, uv)] if ua else []) + ([uvs.get(vb, uv)] if ub else [])
            inward = None      # (round the rim the opening itself is the open air: the lining there faces its middle)
            if onrim: inward = Vector(h * ((bx0 + bx1) / 2 - (pa[0] + pb[0]) / 2) + v * ((by0 + by1) / 2 - (pa[1] + pb[1]) / 2))
            if face(vl, uv if onrim else ul, inward, -1): tot["lining"] += 1
        # the glass, and its edge running on a little behind the frame
        gv = [bm.verts.new(W3(x, y, zg)) for (x, y) in poly]
        Rs.append(g["R"]); face(gv, uv, nn, len(Rs))
        fl = [bm.verts.new(W3(x, y, zg - 0.6)) for (x, y) in grow(poly, FLANGE)]
        for i in range(len(poly)):
            j = (i + 1) % len(poly)
            face([gv[i], fl[i], fl[j], gv[j]], uv, nn, -1)
        # the muntins
        bars = muntins(g)
        if bars:

            for (bp, bh) in bars:
                cp = clip(ccw(bp), poly)
                if len(cp) < 3 or area(cp) < 1.0: continue
                tv = [bm.verts.new(W3(x, y, zg + bh)) for (x, y) in cp]; bv = [bm.verts.new(W3(x, y, zg)) for (x, y) in cp]
                face(tv, uv, nn, -1)
                for i in range(len(cp)):
                    j = (i + 1) % len(cp)
                    ex, ey = cp[j][0] - cp[i][0], cp[j][1] - cp[i][1]
                    face([tv[i], bv[i], bv[j], tv[j]], uv, Vector(h * ey - v * ex), -1)   # (outward of the counter-clockwise bar)
                tot["bars"] += 1
        else: tot["nomunt"] += 1
    # A check that nothing is left open: rays into every window, straight on and from four slants, must end on a face that looks
    # at them (faces seen from behind are not drawn in the game); counted on the untouched model too, which has gaps of its own
    bm.faces.ensure_lookup_table(); bm.normal_update()
    after = BVHTree.FromBMesh(bm)
    def through(tree, org, dr, reach):
        o_ = org
        for _ in range(60):
            hit = tree.ray_cast(o_, dr, reach)
            if hit[0] is None: return True
            if hit[1].dot(dr) < 0: return False
            reach -= hit[3] + 0.001; o_ = hit[0] + dr * 0.001
            if reach <= 0: return True
        return False
    for g in found:
        n = np.array(g["n"]); c0 = np.array(g["c0"]); h = np.array(g["h"]); v = np.array(g["v"]); poly = grow(g["poly"], 4.0)
        xs = [p[0] for p in poly]; ys = [p[1] for p in poly]; lk0 = lk1 = nr = 0
        for sx, sy in ((0, 0), (0.6, 0), (-0.6, 0), (0, 0.6), (0, -0.6), (1.2, 0), (-1.2, 0), (0, 1.2), (0, -1.2), (0.9, 0.9), (-0.9, 0.9), (0.9, -0.9), (-0.9, -0.9)):
            dr = -(n + h * sx + v * sy); dr /= np.linalg.norm(dr)
            for x in np.arange(min(xs), max(xs) + 0.1, 3.0):
                for y in np.arange(min(ys), max(ys) + 0.1, 3.0):
                    if not in_convex(poly, x, y): continue
                    tgt = (c0 + h * x + v * y + n * g["zg"]) / 100.0
                    org = Vector(tgt - dr * 1.5)
                    a0 = through(bvh_m, org, Vector(dr), 1.9); a1 = through(after, org, Vector(dr), 1.9); nr += 1; lk0 += a0; lk1 += a1
                    if DEBUG and a1 and not a0 and lk1 - lk0 < 6:
                        log("  ray through", TAG, "at %.0f %.0f of box %.0f..%.0f %.0f..%.0f slant %.1f %.1f" % (x, y, min(xs), max(xs), min(ys), max(ys), sx, sy))
                        if os.environ.get("WINS_TRACE"):
                            o2 = org; rc = 1.9
                            for _ in range(60):
                                ht = after.ray_cast(o2, Vector(dr), rc)
                                if ht[0] is None: break
                                q = np.array(ht[0]) * 100.0 - c0
                                log("      hit at %.1f %.1f depth %.1f, facing %.2f, kind %d" % (q @ h, q @ v, q @ n - g["zg"], -ht[1].dot(Vector(dr)), bm.faces[ht[2]][pl_]))
                                rc -= ht[3] + 0.001; o2 = ht[0] + Vector(dr) * 0.001
        g["leak"] = (lk0, lk1, nr)
        if lk1 > lk0 + 2: log("OPEN", TAG, "window at", np.round(g["centre"]).astype(int), g["kind"], "rays through: %d before, %d after, of %d" % (lk0, lk1, nr))
    log("open check", TAG, "rays through all windows: %d before, %d after, of %d" % (sum(g["leak"][0] for g in found), sum(g["leak"][1] for g in found), sum(g["leak"][2] for g in found)))
    added = [f for f in bm.faces if f[pl_] != 0]
    bmesh.ops.triangulate(bm, faces=added)
    big = [f for f in bm.faces if len(f.verts) > 4]
    if big: bmesh.ops.triangulate(bm, faces=big)
    bm.to_mesh(me); bm.free(); me.update()
    nf = len(me.polygons)
    pane = np.empty(nf, np.int32); me.attributes["pane"].data.foreach_get("value", pane); me.attributes.remove(me.attributes["pane"])
    # the shading normals: the model's own where it is the model, flat on everything new
    nl = len(me.loops)
    cn = np.empty(nl * 3, np.float32); me.attributes["cn_keep"].data.foreach_get("vector", cn); cn = cn.reshape(-1, 3)
    me.attributes.remove(me.attributes["cn_keep"])
    pn = np.empty(nf * 3, np.float32); me.polygons.foreach_get("normal", pn); pn = pn.reshape(-1, 3)
    lt = np.empty(nf, np.int32); me.polygons.foreach_get("loop_total", lt)
    lpoly = np.repeat(np.arange(nf), lt)
    ln = np.linalg.norm(cn, axis=1)
    flat = (pane[lpoly] != 0) | (ln < 0.5)
    cn = np.where(flat[:, None], pn[lpoly], cn / np.maximum(ln, 1e-9)[:, None])
    cols = np.zeros((nf, 3), np.float32); cols[:] = NOT
    if fire_pts is not None and len(fire_pts):
        from mathutils.kdtree import KDTree
        kd = KDTree(len(fire_pts))
        for i, p in enumerate(fire_pts): kd.insert(Vector(p), i)
        kd.balance()
        cen = np.empty(nf * 3); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3) * 100.0
        nearf = np.array([kd.find(Vector(c))[2] < FIRE_NEAR for c in cen]) & (pane == 0)
        cols[nearf] = (0.0, 0.0, 0.0); log("fire faces", int(nearf.sum()), "from", len(fire_pts), "fire triangles")
    Ra = np.array([0.0] + Rs, np.float32)
    gl = pane > 0
    cols[gl, 0] = Ra[pane[gl]]; cols[gl, 1] = 1.0; cols[gl, 2] = 0.0
    for at in list(me.color_attributes): me.color_attributes.remove(at)
    attr = me.color_attributes.new("WindowMask", "FLOAT_COLOR", "CORNER")
    pl = np.repeat(np.concatenate([cols, np.ones((nf, 1), np.float32)], 1), lt, axis=0)
    attr.data.foreach_set("color", pl.ravel()); me.color_attributes.active_color = attr
    me.normals_split_custom_set([tuple(x) for x in cn.tolist()]); me.update()
    log("rebuilt", TAG, "windows", len(found), "cut faces", tot["cut"], "old faces deleted", tot["deleted"], "lining faces", tot["lining"],
        "muntin bars", tot["bars"], "windows keeping their own bars", tot["nomunt"], "old glass scraps repainted", tot["repaint"], "triangles %d -> %d" % (nf0, nf))
    return nf - nf0

def export(o, path):
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE",
                             global_scale=1.0, apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
                             use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False, colors_type="LINEAR")

def picture(path):
    im = bpy.data.images.load(path); w, h = im.size
    px = np.empty(w * h * 4, np.float32); im.pixels.foreach_get(px); bpy.data.images.remove(im)
    return px.reshape(h, w, 4)   # (bottom row first, as UV v runs)

def fresh(path):
    """The model as its maker wrote it: the first run keeps a copy in PreGlass next to it, and every later run starts from that copy
    (never from windows built before), until the maker writes the model again (its time then no longer matches the stamp left here).
    (The same folder and stamp as window_glass.py, the second round's tool.)"""
    keep = os.path.join(os.path.dirname(path), "PreGlass"); os.makedirs(keep, exist_ok=True)
    orig = os.path.join(keep, os.path.basename(path)); stamp = orig + ".glass"
    ours = os.path.exists(stamp) and open(stamp).read().strip() == "%.3f" % os.path.getmtime(path)
    if not ours or not os.path.exists(orig): shutil.copy2(path, orig)
    return orig, stamp
def pics(folder, name):   # (the painted picture and its normals, for the frame colour)
    p = [os.path.join(folder, "T_%s_%s.png" % (name, k)) for k in ("Base", "Normal")]
    return [picture(x) if os.path.exists(x) else None for x in p]

for pr in pairs:
    name, new = pr.split(":"); TAG = new
    src_path = os.path.join(work, name + ".fbx")
    low = os.path.join(out, new + ".fbx")
    found = []; fire_pts = None
    if not os.path.exists(src_path): log(new, "no source model", src_path, "(an open stall: no windows)")
    else:
        pre = os.path.join(work, "PreGlass", name + ".fbx")
        ours = os.path.exists(pre + ".glass") and open(pre + ".glass").read().strip() == "%.3f" % os.path.getmtime(src_path)
        src = load(pre if ours else src_path)   # (an old model window_glass.py gave its own glass: its own marks are in the copy)
        fc = marks(src.data, np.array([p.loop_start for p in src.data.polygons], np.int64))
        if fc is not None:
            found = find_openings(src, fc)
            fi_ = np.nonzero((fc[:, 0] < 0.5) & (fc[:, 1] < 0.5) & (fc[:, 2] < 0.5))[0]
            if len(fi_):   # (the forge: its fire stays where it was, and nowhere else can burn)
                ar_ = arrays(src.data); fire_pts = ar_[1][np.isin(ar_[2], fi_)].mean(1)
                if new in NO_GLASS_ABOVE: fire_pts = fire_pts[fire_pts[:, 2] <= NO_GLASS_ABOVE[new]]
    if os.environ.get("WINS_FIND_ONLY"): continue
    orig, stamp = fresh(low)
    o = load(orig)
    rebuild(o, found, *pics(out, new), fire_pts=fire_pts)
    export(o, low); open(stamp, "w").write("%.3f" % os.path.getmtime(low))
    log(new, "written")
