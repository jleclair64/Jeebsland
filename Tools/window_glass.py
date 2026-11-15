# Window glass from the model's own shape (owner, 2026-11-15: lit windows with no black smudges or ragged orange edges, "Please find
# a solution"). Every pane becomes its own flat piece of glass with straight edges, laid a few millimetres over the building where its
# pane is, and marked in the vertex colour as glass with its window's random number. The materials (M_TL_Building, and M_Building with
# GeomGlass 1) light only that glass: one even amber, the whole window at once; by day one even dark glass. Everything else on the
# building keeps its painted picture (the frame and the muntins stay painted wood between the panes), and no picture decides glass.
#
#   TownLab:  blender -b -P Tools/window_glass.py -- <work> <out> <Name>:<NewName> [...]
#             <work>/<Name>.fbx is the 300k model the bake started from (its window marks), <out>/<NewName>.fbx the baked low model,
#             rewritten with the glass added and a "WindowMask" colour on every face (BuildTownLabBuildings.ps1, after the bake).
#   Old /Game/Buildings model:  blender -b -P Tools/window_glass.py -- <work> - <Name> [...]
#             the model is its own source; its old window faces stop being glass (BuildBuildings.ps1 does the church).
#
# How the panes are found: building_clean.py marked every face of the 300k model that the glow picture lights (G 1, R a number per
# window). Those faces follow Meshy's triangles, so their edge is ragged, and the dark painted shapes inside a pane aren't marked at all.
# Per window (and per flat part of it, if it bends), the faces in front are drawn flat at 1 cm a texel in the window's own plane; small
# gaps (leading) are closed, slivers and spikes opened away, and each piece left is one pane. A pane that fills its box is that box (the
# usual case: straight sides, square corners); any other (round, arched) is the outline round it (convex hull), straight from corner to
# corner. The pane's glass sits GLASS_LIFT in front of the building's surface there (traced on the low model), facing out.
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index("--") + 1:]
work, out, pairs = a[0], a[1], a[2:]
OLD = out == "-"

PX = 1.0             # cm per texel of a window's flat drawing
CLOSE_R = 1          # texels: gaps this narrow inside a pane (leading, a dark painted line) are closed
OPEN_R = 2           # texels: slivers and spikes this thin are opened away (a ragged triangle reaching onto a bar)
MIN_PANE = 120.0     # cm2: smaller pieces are dropped (a stray marked triangle, a lit speck on a roof)
SHAPE_IOU = 0.78     # a pane matching a box, a round or an arched light this well (overlap over union) takes that clean shape
ROUND_MARGIN = 0.06  # a round or arched light must match this much better than the box
OPEN_CLOSE = 6       # texels: a window's opening is its glass with gaps this narrow closed (muntins, leading)
COVER = 0.8          # panes covering less of the opening's glass than this (slivers between leading): the whole opening is one glass
CONVEX = 0.88        # a pane filling less of its outline than this is two joined (or has a bar through it): split if it parts
PLANE_DOT = 0.85     # faces of one window within this of its main facing are one flat part
NEAR_CM = 25.0       # faces this far in front of or behind a window's plane can hide or be its panes
GLASS_LIFT = 0.5     # cm in front of the building's surface
NEAR_MARK = 6        # texels: (an old model by its colour) glass within this of the window marks
GLASS_SRGB = (0.16, 0.17, 0.19)   # (townlab_panes.py's GLASS_SRGB: the pane colour it paints)
GLASS_TOL = 0.035    # how close to it a texel is glass
DEPTH_CAP = 8.0      # cm: the glass sits in front of the furthest-out point of the low model inside the pane, at most this past the middle one
MIN_SIDE = 6.0       # cm: a pane narrower than this either way is a sliver, dropped
GLASS = (0.0, 1.0, 0.0)   # the mark: G 1 glass, B 0 (R the window's number)
NOT = (0.0, 0.0, 1.0)     # every other face: never glass (B 1)

def log(*x): print("WGLASS", *x, flush=True)

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
    uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers[0].data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    return co, co[lv[tl]], tp, fn, fa, ls, uv[tl]

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

def draw(T2, Z, W, H):   # front-most triangle per texel (T2 in texel units): its index, -1 where none
    best = np.full((H, W), -np.inf); who = np.full((H, W), -1, np.int64); B0 = np.zeros((H, W), np.float32); B1 = np.zeros((H, W), np.float32)
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
            who[yy[win], xx[win]] = s[ti[win]]; B0[yy[win], xx[win]] = w0[ti, pi][win]; B1[yy[win], xx[win]] = w1[ti, pi][win]
    return who, B0, B1

TAG = ""
DEBUG = os.environ.get("WGLASS_DEBUG", "")   # a folder: a picture of each window part's marks (grey), panes (white) and glass (red outline)
def debug_pic(tag, raw, clean, polys):
    H, W = raw.shape
    img = np.zeros((H, W, 4), np.float32); img[..., 3] = 1
    img[raw, :3] = 0.35; img[clean, :3] = 0.85
    for poly in polys:
        for i in range(len(poly)):
            (xa, ya), (xb, yb) = poly[i], poly[(i + 1) % len(poly)]
            for t in np.linspace(0, 1, int(max(abs(xb - xa), abs(yb - ya))) * 2 + 2):
                x, y = int(min(W - 1, max(0, xa + (xb - xa) * t))), int(min(H - 1, max(0, ya + (yb - ya) * t)))
                img[y, x, :3] = (1, 0, 0)
    img = np.repeat(np.repeat(img, 4, 0), 4, 1); im = bpy.data.images.new("dbg", W * 4, H * 4, alpha=True); im.pixels.foreach_set(img.ravel())
    im.filepath_raw = os.path.join(DEBUG, tag + ".png"); im.file_format = "PNG"; im.save(); bpy.data.images.remove(im)

def area(poly):
    return 0.5 * abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly))))

def inside(poly, H, W):
    """Texels whose centres lie inside a convex counter-clockwise polygon."""
    yy, xx = np.mgrid[0:H, 0:W]; X = xx + 0.5; Y = yy + 0.5
    m = np.ones((H, W), bool)
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        m &= (bx - ax) * (Y - ay) - (by - ay) * (X - ax) >= -1e-6
    return m

def ellipse(cx, cy, rx, ry, a0, a1, n=24):
    return [(cx + rx * math.cos(a), cy + ry * math.sin(a)) for a in np.linspace(a0, a1, n)]

def fit(cm):
    """A pane's glass as a clean shape: the box (straight sides, square corners), the round light (an ellipse) or the arched light
    (a box with a round top), whichever matches the texels best; failing all three, the outline round them. Also how much of that
    outline the texels fill (low: two panes joined, or a pane with a bar through it)."""
    ys, xs = np.nonzero(cm); H, W = cm.shape
    cx = np.bincount(xs - xs.min()); cy = np.bincount(ys - ys.min())
    fx = np.median(cx[len(cx) // 4: max(len(cx) * 3 // 4, len(cx) // 4 + 1)]); fy = np.median(cy[len(cy) // 4: max(len(cy) * 3 // 4, len(cy) // 4 + 1)])
    gx = np.nonzero(cx >= 0.5 * fx)[0]; gy = np.nonzero(cy >= 0.5 * fy)[0]
    bx0, bx1 = xs.min() + gx.min(), xs.min() + gx.max() + 1
    gy0 = np.nonzero(cy >= 0.15 * fy)[0]          # (the top of a round head is narrow: its rows count from a little)
    by0, by1 = ys.min() + gy.min(), ys.min() + max(gy.max(), gy0.max()) + 1
    pts = np.concatenate([np.stack([xs, ys], 1), np.stack([xs + 1, ys], 1), np.stack([xs, ys + 1], 1), np.stack([xs + 1, ys + 1], 1)])
    hl = hull(pts)
    ratio = len(ys) / max(area(hl), 1.0) if len(hl) >= 3 else 1.0
    w, h = bx1 - bx0, by1 - by0
    if w < 2 or h < 2: return hl, False, ratio
    if min(w, h) * PX < MIN_SIDE: return [], False, ratio   # (a sliver: a mark run along a strut, not a pane)
    roundish = len(hl) >= 3 and area(hl) / (w * h) < 0.86   # (the outline has no corners: a round light, never a box)
    mx, my = (bx0 + bx1) / 2, (by0 + by1) / 2
    cands = [("box", [(bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)]),
             ("round", ellipse(mx, my, w / 2, h / 2, 0, 2 * math.pi, 32)[:-1])]
    a = min(w / 2, h * 0.5)
    cands.append(("arch", [(bx0, by0), (bx1, by0)] + ellipse(mx, by1 - a, w / 2, a, 0, math.pi, 16)))
    tgt = inside(hl, H, W) if len(hl) >= 3 else cm   # (matched against the outline round the texels: a painted blotch or a notch out of a pane doesn't count)
    best, bi = None, -1.0
    for kind, poly in cands:
        if roundish and kind == "box": continue
        m = inside(poly, H, W)
        iou = (m & tgt).sum() / max((m | tgt).sum(), 1) - (0.0 if kind == "box" else ROUND_MARGIN)   # (a box unless round is clearly better)
        if iou > bi: bi, best = iou, (kind, poly)
    if bi >= SHAPE_IOU: return best[1], best[0], ratio
    fit.box = [(bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)]   # (kept for a pane among boxes: see panes)
    return hl, False, ratio

def split(cm):
    """Panes joined by a thin neck or a half-painted bar: shrink until they come apart, then grow each back over its own part."""
    for r in range(2, 9):
        lab, nl = parts(erode(cm, r))
        big = [k for k in range(nl) if (lab == k).sum() * PX * PX >= MIN_PANE / 4]
        if len(big) < 2: continue
        own = np.full(cm.shape, -1, np.int64)
        for i, k in enumerate(big): own[lab == k] = i
        for _ in range(r + 2):   # (grow each seed inside the pane, the first to reach a texel keeps it)
            for i in range(len(big)):
                g = dilate(own == i, 1) & cm & (own < 0)
                own[g] = i
        return [own == i for i in range(len(big))]
    return None

def panes(src, fc, base=None):
    """The panes of every window of the source model: a list of (R, n, 3D corner points in cm)."""
    me = src.data
    co, P, tp, fn, fa, ls, UVt = arrays(me)
    win = (fc[:, 1] > 0.5) & (fc[:, 2] < 0.5)
    rid = np.round(fc[:, 0] * 1e6).astype(np.int64)
    cen = P.mean(1)
    res = []; nwin = 0; nwhole = 0; kinds = {}
    for wid in np.unique(rid[win]):
        left = win & (rid == wid)
        R = float(fc[np.nonzero(left)[0][0], 0])
        nwin += 1
        while left.any() and fa[left].sum() > MIN_PANE:
            n = (fn[left] * fa[left, None]).sum(0); n /= max(np.linalg.norm(n), 1e-9)
            part = left & ((fn @ n) > PLANE_DOT)
            if fa[part].sum() < MIN_PANE: break
            left = left & ~part
            h = np.cross([0.0, 0.0, 1.0], n)
            h = h / np.linalg.norm(h) if np.linalg.norm(h) > 0.3 else np.array([1.0, 0.0, 0.0])
            v = np.cross(n, h)
            c0 = (cen[np.isin(tp, np.nonzero(part)[0])] * 1.0).mean(0)
            mine = part[tp]
            Hm = (P[mine] - c0) @ h; Vm = (P[mine] - c0) @ v
            x0, x1, y0, y1 = Hm.min() - 6, Hm.max() + 6, Vm.min() - 6, Vm.max() + 6
            ch, cv, cd = (cen - c0) @ h, (cen - c0) @ v, (cen - c0) @ n
            near = (np.abs(cd) < NEAR_CM) & (ch > x0 - 30) & (ch < x1 + 30) & (cv > y0 - 30) & (cv < y1 + 30) & ((fn[tp] @ n) > 0.0)
            idx = np.nonzero(near)[0]
            W, H = int(math.ceil((x1 - x0) / PX)), int(math.ceil((y1 - y0) / PX))
            T2 = np.stack([((P[idx] - c0) @ h - x0) / PX, ((P[idx] - c0) @ v - y0) / PX], -1)
            who, B0, B1 = draw(T2, (P[idx] - c0) @ n, W, H)
            M = np.zeros((H, W), bool); ok = who >= 0
            M[ok] = part[tp[idx[who[ok]]]]
            if base is not None:
                # (a model whose colour picture townlab_panes.py painted: its panes are one flat glass colour there, crisper than the
                # marks, which run onto the tracery and join the lights of a lancet; glass is that colour near the window's marks)
                t = idx[np.where(ok, who, 0)]
                uvp = UVt[t, 0] * B0[..., None] + UVt[t, 1] * B1[..., None] + UVt[t, 2] * (1 - B0 - B1)[..., None]
                Sy, Sx = base.shape[:2]
                px_ = base[np.clip((uvp[..., 1] * Sy).astype(np.int64), 0, Sy - 1), np.clip((uvp[..., 0] * Sx).astype(np.int64), 0, Sx - 1), :3]
                M = ok & dilate(M, NEAR_MARK) & (np.abs(px_ - np.array(GLASS_SRGB)).max(-1) < GLASS_TOL)
            raw = M.copy()
            M = erode(dilate(M, CLOSE_R), CLOSE_R)          # (close the leading)
            M = dilate(erode(M, OPEN_R), OPEN_R) & M         # (open the spikes)
            lab, nl = parts(M); found_here = []
            todo = [(lab == k, 0) for k in range(nl)]
            while todo:
                cm, depth = todo.pop()
                if cm.sum() * PX * PX < MIN_PANE: continue
                poly, kind, ratio = fit(cm)
                if ratio < CONVEX and depth < 3:
                    sub = split(cm)
                    if sub: todo += [(s, depth + 1) for s in sub]; continue
                if len(poly) >= 3: found_here.append((poly, kind, None if kind else fit.box))
            # A leaded or latticed window (the corner house, the church) comes apart into slivers between its painted leading, which
            # the opening drops: where the panes found miss much of the window's glass, the whole opening is one glass instead.
            op = erode(dilate(raw, OPEN_CLOSE), OPEN_CLOSE)
            olab, onl = parts(op)
            for k in range(onl):
                om = olab == k
                if (om & raw).sum() * PX * PX < MIN_PANE: continue
                mine_ = [(p, kd, bx) for (p, kd, bx) in found_here if om[min(H - 1, max(0, int(np.mean([q[1] for q in p])))), min(W - 1, max(0, int(np.mean([q[0] for q in p]))))]]
                cov = np.zeros((H, W), bool)
                for (p, kd, bx) in mine_: cov |= inside(p, H, W)
                share = (cov & raw & om).sum() / max((raw & om).sum(), 1)
                if share < COVER or sum(1 for x in mine_ if not x[1]) > sum(1 for x in mine_ if x[1] == "box"):   # (or a pane that is no clean shape: the opening, whole, is)
                    poly, kind, _ = fit(om & dilate(raw, 1))
                    found_here = [x for x in found_here if not any(x is y for y in mine_)] + ([(poly, kind, None)] if len(poly) >= 3 else [])
                    nwhole += 1
            # a pane that is no clean shape among two or more boxes of its window (a blotch cut a corner off it) takes its own box
            if sum(1 for x in found_here if x[1] == "box") >= 2: found_here = [(x[2], "box", None) if (not x[1] and x[2]) else x for x in found_here]
            polys = []
            for (poly, kind, _) in found_here:
                kinds[kind if kind else "outline"] = kinds.get(kind if kind else "outline", 0) + 1
                pts3 = [c0 + h * (x0 + px * PX) + v * (y0 + py * PX) for (px, py) in poly]
                res.append((R, n, pts3)); polys.append(poly)
            if DEBUG: debug_pic("%s_%03d_%03d" % (TAG, nwin, len(res)), raw, M, polys)
    log("windows", nwin, "panes", len(res), kinds, "whole openings", nwhole)
    return res

def add_glass(o, found, keep_marks):
    """Lay the panes on o (world metres) and colour every face: the glass (R, 1, 0), the rest NOT (keeping fire, B 0 G 0, if keep_marks)."""
    me = o.data
    cn_old = [tuple(c.vector) for c in me.corner_normals]
    nf0 = len(me.polygons)
    old = marks(me, np.array([p.loop_start for p in me.polygons], np.int64)) if keep_marks else None
    # trace the building's surface (in cm)
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3) * 100.0
    bvh = BVHTree.FromPolygons([Vector(c) for c in co], [tuple(p.vertices) for p in me.polygons], all_triangles=False)
    uvl = me.uv_layers[0].data
    bm = bmesh.new(); bm.from_mesh(me)
    uvb = bm.loops.layers.uv[0]
    pl_ = bm.faces.layers.int.new("pane")      # (0 the building, i + 1 the i-th pane laid: triangulation copies it)
    Rs = []; skipped = 0; capped = 0
    for (R, n, pts) in found:
        nn = Vector(n); c = sum(pts, np.zeros(3)) / len(pts)
        # every 2 cm over the pane, a little in from its edge (the low model's decimated triangles cross a pane at a slant: a sparse
        # sample missed them and they came through the glass as dark diagonal cracks)
        P2 = np.array(pts); e1 = P2[1] - P2[0]; e1 /= max(np.linalg.norm(e1), 1e-9); e2 = np.cross(n, e1)
        u_, v_ = (P2 - c) @ e1, (P2 - c) @ e2
        gu, gv = np.meshgrid(np.arange(u_.min() + 1.0, u_.max() - 1.0, 2.0), np.arange(v_.min() + 1.0, v_.max() - 1.0, 2.0))
        cand = c + gu.ravel()[:, None] * e1 + gv.ravel()[:, None] * e2
        polyuv = list(zip(u_, v_))
        def _in(px, py):
            s = [(bx - ax) * (py - ay) - (by - ay) * (px - ax) for (ax, ay), (bx, by) in zip(polyuv, polyuv[1:] + polyuv[:1])]
            return all(x >= 0 for x in s) or all(x <= 0 for x in s)
        samp = [c] + [p for p, a_, b_ in zip(cand, gu.ravel(), gv.ravel()) if _in(a_, b_)]
        depth = []; uv = None
        for s in samp:
            hit = bvh.ray_cast(Vector(s) + nn * 40.0, -nn, 80.0)
            if hit[0] is None: continue
            depth.append((np.array(hit[0]) - c) @ n)
            if uv is None: uv = tuple(uvl[me.polygons[hit[2]].loop_start].uv)
        if len(depth) < max(2, len(samp) // 3): skipped += 1; continue
        med = float(np.median(depth)); capped += max(depth) > med + DEPTH_CAP; d = min(max(depth), med + DEPTH_CAP) + GLASS_LIFT   # (in front of all of the low model inside the pane: its decimated surface wanders a few cm and tore the glass where it came through)
        vs = [bm.verts.new(Vector((np.array(p) + np.array(n) * d) / 100.0)) for p in pts]
        f = bm.faces.new(vs)
        f.normal_update()
        if f.normal.dot(nn) < 0: f.normal_flip()
        for l in f.loops: l[uvb].uv = uv
        Rs.append(R); f[pl_] = len(Rs)
    nq = len(Rs)
    added = [f for f in bm.faces if f[pl_] > 0]
    bmesh.ops.triangulate(bm, faces=added)
    bm.to_mesh(me); bm.free(); me.update()
    nf = len(me.polygons)
    pane = np.empty(nf, np.int32); me.attributes["pane"].data.foreach_get("value", pane); me.attributes.remove(me.attributes["pane"])
    assert (pane[:nf0] == 0).all() and (pane[nf0:] > 0).all(), "the building's own faces moved"
    cols = np.zeros((nf, 3), np.float32); cols[:] = NOT
    if old is not None:
        fire = (old[:, 1] < 0.5) & (old[:, 2] < 0.5)
        cols[:nf0][fire] = (0.0, 0.0, 0.0)
    Ra = np.array([0.0] + Rs, np.float32)
    cols[nf0:, 0] = Ra[pane[nf0:]]; cols[nf0:, 1] = 1.0; cols[nf0:, 2] = 0.0
    for at in list(me.color_attributes): me.color_attributes.remove(at)
    attr = me.color_attributes.new("WindowMask", "FLOAT_COLOR", "CORNER")
    lt = np.array([p.loop_total for p in me.polygons]); pl = np.repeat(np.concatenate([cols, np.ones((nf, 1), np.float32)], 1), lt, axis=0)
    attr.data.foreach_set("color", pl.ravel()); me.color_attributes.active_color = attr
    me.normals_split_custom_set(cn_old + [tuple(me.polygons[pi].normal) for pi in range(nf0, nf) for _ in me.polygons[pi].loop_indices]); me.update()
    log("glass laid", nq, "skipped", skipped, "capped", capped, "triangles added", nf - nf0)
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

import shutil
def fresh(path):
    """The model as its maker wrote it: the first run keeps a copy in PreGlass next to it, and every later run starts from that copy
    (never from glass laid before), until the maker writes the model again (its time then no longer matches the stamp left here)."""
    keep = os.path.join(os.path.dirname(path), "PreGlass"); os.makedirs(keep, exist_ok=True)
    orig = os.path.join(keep, os.path.basename(path)); stamp = orig + ".glass"
    ours = os.path.exists(stamp) and open(stamp).read().strip() == "%.3f" % os.path.getmtime(path)
    if not ours or not os.path.exists(orig): shutil.copy2(path, orig)
    return orig, stamp
def done(path, stamp):
    open(stamp, "w").write("%.3f" % os.path.getmtime(path))

for pr in pairs:
    name, new = (pr.split(":") + [None])[:2]; TAG = new or name
    src_path = os.path.join(work, name + ".fbx")
    if OLD:
        orig, stamp = fresh(src_path)
        o = load(orig)
        fc = marks(o.data, np.array([p.loop_start for p in o.data.polygons], np.int64))
        bp = os.path.join(work, "T_%s_Base.png" % name)
        found = panes(o, fc, picture(bp) if os.path.exists(os.path.join(work, "PrePanes", "T_%s_Base.png" % name)) else None)
        add_glass(o, found, True)
        export(o, src_path); done(src_path, stamp)
        log(name, "written", src_path)
    else:
        low = os.path.join(out, new + ".fbx")
        orig, stamp = fresh(low)
        found = []
        if not os.path.exists(src_path): log(new, "no source model", src_path, "(an open stall: no glass)")
        else:
            pre = os.path.join(work, "PreGlass", name + ".fbx")
            ours = os.path.exists(pre + ".glass") and open(pre + ".glass").read().strip() == "%.3f" % os.path.getmtime(src_path)
            src = load(pre if ours else src_path)   # (an old model already given its own glass: its own marks are in the copy)
            fc = marks(src.data, np.array([p.loop_start for p in src.data.polygons], np.int64))
            if fc is not None: found = panes(src, fc)
        o = load(orig)
        add_glass(o, found, False)
        export(o, low); done(low, stamp)
        log(new, "written")
