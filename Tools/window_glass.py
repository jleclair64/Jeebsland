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
# corner. GLASS IN FRONT (owner, 2026-11-15, after panes laid at the glass failed two reviews: the low model's decimated muntins and frames
# came through them as dark wedges): the panes of one opening become one flat sheet, laid GLASS_LIFT in front of all of the low model inside
# the panes' outline (up to WALL_PROUD past the wall round the window), glass where the panes are and one even texel of the building's own
# painted frame for the bars between them and a FRAME_BAND rim, so nothing of the model can come through the glass.
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
FRAME_BAND = 2.0     # cm: the sheet runs this far past its panes all round, painted frame (its edge straight, over the model's ragged frame)
WALL_IN, WALL_OUT = 6.0, 16.0   # cm: the wall round a window, this far out from its sheet's edge
WALL_PROUD = 12.0    # cm: the sheet may come this far out past that wall (a frame standing proud of it), never further
JOIN_GAP = 20.0      # cm: openings of one window closer than this are one sheet (the panes of one window between thick muntins came
                     # out as sheets at different depths; sheets over each other would fight; a lattice split by a mullion)
UNEVEN = 1.25        # box sizes within this of each other are one size; a sheet of more than two widths or heights is uneven: one glass
ROUND_FIT = 0.85     # a round opening's outline must match a round or arched light this well (overlap over union), else it is a box
NO_GLASS_ABOVE = {"TL_Church": 1300.0}   # cm: no glass above this (the church tower's belfry louvres are marked as windows; lit, they showed as
                     # an amber sliver at the tower top)
LATTICE = 12         # an opening of more panes than this (a lattice, the potion shop's shop window) is one glass
FIRE_NEAR = 30.0     # cm: the low model's faces this near the 300k model's fire (the forge) are marked fire (0, 0, 0), the only faces that may burn
BAR_MIN = 0.8        # cm: panes closer than this have no bar between them (or overlap): one pane
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
    res = []; nwin = 0; nwhole = 0; kinds = {}; nmerged = 0; npanes = 0; nhidden = 0; nround = 0; nlattice = 0; nuneven = 0; nparts = 0
    for wid in np.unique(rid[win]):
        left = win & (rid == wid)
        R = float(fc[np.nonzero(left)[0][0], 0])
        nwin += 1; w_first = len(res)
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
            proj = float((fa[part] * np.abs(fn[part] @ n)).sum())
            if base is None and M.sum() * PX * PX < 0.4 * proj:
                # (a window whose glass sits behind something, a shop window's goods or a lattice: most of its marked glass is hidden,
                # leaving scattered flecks. Its marks are drawn on their own; the sheet goes in front of what stood before them)
                gi = np.nonzero(part[tp[idx]])[0]
                w2, _, _ = draw(T2[gi], ((P[idx] - c0) @ n)[gi], W, H)
                M = w2 >= 0; nhidden += 1
                log("hidden glass", TAG, "window", nwin, "marks drawn alone %.0f cm2 of %.0f" % (float(M.sum()) * PX * PX, proj))
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
            # Glass in front (owner, 2026-11-15): the panes of one opening become one flat sheet laid in front of everything there,
            # glass where the panes are and painted frame between them and round their edge, so nothing of the model comes through
            groups = {}
            for (poly, kind, _) in found_here:
                kinds[kind if kind else "outline"] = kinds.get(kind if kind else "outline", 0) + 1
                cx_, cy_ = np.mean([q[0] for q in poly]), np.mean([q[1] for q in poly])
                k = olab[min(H - 1, max(0, int(cy_))), min(W - 1, max(0, int(cx_)))]
                groups.setdefault(int(k) if k >= 0 else -1 - len(groups), []).append(([(x0 + px * PX, y0 + py * PX) for (px, py) in poly], kind))
            # (openings whose sheets would overlap are one sheet: two sheets over each other fight, and a lattice split by a
            # mullion came out as a big glass and a column of little ones)
            items = [([k], gp) for k, gp in groups.items()]
            again = True
            while again:
                again = False
                for i in range(len(items)):
                    for j in range(i + 1, len(items)):
                        ra = hull([q for (p, _) in items[i][1] for q in p]); rb = hull([q for (p, _) in items[j][1] for q in p])
                        if not apart(ra, rb, JOIN_GAP):
                            items[i] = (items[i][0] + items[j][0], items[i][1] + items[j][1]); del items[j]; again = True; break
                    if again: break
            polys = []
            for ks, gp in items:
                om_ = np.isin(olab, [k for k in ks if k >= 0]) & raw
                ys_, xs_ = np.nonzero(om_)
                if len(xs_) * PX * PX >= MIN_PANE:
                    # (a round window, the church's rose: its lights between the tracery came out as small boxes, and the box round
                    # them overlapped the stone ring; a round opening is one round glass, the outline round its marks)
                    cp = np.concatenate([np.stack([xs_, ys_], 1) + np.array(d_) for d_ in ((0, 0), (1, 0), (0, 1), (1, 1))])
                    hk = hull(cp)
                    bx0, bx1, by0, by1 = xs_.min(), xs_.max() + 1, ys_.min(), ys_.max() + 1
                    if len(hk) >= 3 and area(hk) / float((bx1 - bx0) * (by1 - by0)) < 0.86:
                        # (its clean shape: the round light or the arched one that matches the outline best; an outline neither
                        # matches, the potion shop's tilted blob of marks in a square opening, is the box round it)
                        mx, my, w2, a = (bx0 + bx1) / 2, (by0 + by1) / 2, (bx1 - bx0) / 2, min((bx1 - bx0) / 2, (by1 - by0) * 0.5)
                        tgt = inside(hk, H, W); best, bi = [(bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)], 0.0
                        for cand in (ellipse(mx, my, w2, (by1 - by0) / 2, 0, 2 * math.pi, 40)[:-1], [(bx0, by0), (bx1, by0)] + ellipse(mx, by1 - a, w2, a, 0, math.pi, 20)):
                            m = inside(cand, H, W); iou = (m & tgt).sum() / max((m | tgt).sum(), 1)
                            if iou > bi: bi, best = iou, cand
                        if bi < ROUND_FIT: best = [(bx0, by0), (bx1, by0), (bx1, by1), (bx0, by1)]
                        gp = [([(x0 + px * PX, y0 + py * PX) for (px, py) in best], "round" if bi >= ROUND_FIT else "box")]; nround += 1
                if len(gp) > LATTICE:
                    lp = [q for (p, _) in gp for q in p]
                    if len(xs_):   # (the whole opening's marks: its lights found may miss a row or a column)
                        lp += [(x0 + xs_.min() * PX, y0 + ys_.min() * PX), (x0 + (xs_.max() + 1) * PX, y0 + (ys_.max() + 1) * PX)]
                    gp = [(bbox(lp), "box")]; nlattice += 1   # (a lattice of many small lights, uneven in the model: one glass, lit whole)
                if any(kd == "box" for (_, kd) in gp): gp = [(p, kd) if kd else (bbox(p), "box") for (p, kd) in gp]   # (among boxes, a pane of no clean shape is its box)
                gp, nm = merge_touching(gp); nmerged += nm
                if len(gp) >= 3 and all(upright(p) for p in gp):
                    # (boxes of uneven sizes, the smithy's blotchy marks: no straight rows of bars to keep, one glass, lit whole)
                    wd = [p[2][0] - p[0][0] for p in map(bbox, gp)]; ht = [p[2][1] - p[0][1] for p in map(bbox, gp)]
                    nx = sizes([p[0][0] + 100.0 for p in map(bbox, gp)], 3.0); ny = sizes([p[0][1] + 100.0 for p in map(bbox, gp)], 3.0)   # (the rows and columns the panes start on)
                    if sizes(wd) > 2 or sizes(ht) > 2 or len(gp) < 0.75 * nx * ny:   # (or panes that fill no grid: the store back's scattered little lights)
                        gp = [bbox([q for p in gp for q in p])]; nuneven += 1
                ring = hull([(x + dx, y + dy) for p in gp for (x, y) in p for dx in (-FRAME_BAND, FRAME_BAND) for dy in (-FRAME_BAND, FRAME_BAND)])
                if all(upright(p) for p in gp): ring = ccw(bbox(ring))   # (boxes: a box round them, laid as a grid)
                res.append(dict(R=R, n=n, c0=c0, h=h, v=v, ring=ring, panes=[ccw(p) for p in gp]))
                npanes += len(gp)
                polys += [[((x - x0) / PX, (y - y0) / PX) for (x, y) in p] for p in gp + [ring]]
            if DEBUG: debug_pic("%s_%03d_%03d" % (TAG, nwin, len(res)), raw, M, polys)
        # (one window's second flat part facing the same way gave a second sheet over the first, and its bars cut thin dark lines
        # through the first one's glass, 2026-11-15: sheets of one window facing alike that overlap are one sheet)
        mine = list(range(w_first, len(res))); again = True
        while again:
            again = False
            for i in mine:
                for j in mine:
                    if j <= i or res[i] is None or res[j] is None: continue
                    A, B = res[i], res[j]
                    if float(np.dot(A["n"], B["n"])) < 0.95: continue
                    to_a = lambda pts: [(float((B["c0"] + B["h"] * x + B["v"] * y - A["c0"]) @ A["h"]), float((B["c0"] + B["h"] * x + B["v"] * y - A["c0"]) @ A["v"])) for (x, y) in pts]
                    if apart(A["ring"], hull(to_a(B["ring"])), JOIN_GAP): continue
                    bp = [hull(to_a(p)) for p in B["panes"]]
                    if any(upright(p) for p in A["panes"]): bp = [bbox(p) for p in bp]
                    gp = [(p, "box" if upright(p) else False) for p in A["panes"]] + [(p, "box" if upright(p) else False) for p in bp if all(apart(p, q, BAR_MIN) for q in A["panes"])]   # (the second part's panes over the first's are the same panes)
                    if any(kd == "box" for (_, kd) in gp): gp = [(p, kd) if kd else (bbox(p), "box") for (p, kd) in gp]
                    gp, nm = merge_touching(gp); nmerged += nm
                    ring = hull([(x + dx, y + dy) for p in gp for (x, y) in p for dx in (-FRAME_BAND, FRAME_BAND) for dy in (-FRAME_BAND, FRAME_BAND)])
                    if all(upright(p) for p in gp): ring = ccw(bbox(ring))
                    res[i] = dict(A, ring=ring, panes=[ccw(p) for p in gp]); res[j] = None; nparts += 1; again = True
        res[:] = [r for r in res if r is not None]
    log("windows", nwin, "sheets", len(res), "panes", npanes, kinds, "whole openings", nwhole, "touching panes merged", nmerged, "hidden glass drawn alone", nhidden, "round openings", nround, "lattices", nlattice, "uneven made whole", nuneven, "parts joined", nparts)
    return res

def sizes(xs, gap=None):   # how many distinct sizes (each within UNEVEN times, or gap cm, of the last)
    xs = sorted(xs); return 1 + sum(1 for a, b in zip(xs, xs[1:]) if (b - a > gap if gap else b > UNEVEN * max(a, 1e-6)))

def upright(p):   # (an axis-aligned box)
    return len(p) == 4 and all(abs(p[i][0] - p[(i + 1) % 4][0]) < 1e-6 or abs(p[i][1] - p[(i + 1) % 4][1]) < 1e-6 for i in range(4))

def ccw(p):
    s = sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))
    return list(p) if s > 0 else list(reversed(p))

def apart(a, b, gap):
    """Two convex outlines at least gap apart along one of their edges' normals (separating axis)."""
    for poly in (a, b):
        for i in range(len(poly)):
            (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
            nx, ny = by - ay, ax - bx; l = math.hypot(nx, ny)
            if l < 1e-9: continue
            pa = [(x * nx + y * ny) / l for (x, y) in a]; pb = [(x * nx + y * ny) / l for (x, y) in b]
            if min(pb) - max(pa) > gap or min(pa) - max(pb) > gap: return True
    return False

def bbox(p):
    xs = [q[0] for q in p]; ys = [q[1] for q in p]
    return [(min(xs), min(ys)), (max(xs), min(ys)), (max(xs), max(ys)), (min(xs), max(ys))]

def merge_touching(gp):
    """Panes closer than BAR_MIN (overlapping box snaps, or no bar between them) become one pane: two boxes the box round both,
    else the outline round both. Returns the outlines."""
    gp = [(hull(p), kd) for (p, kd) in gp]; merged = 0; again = True
    while again:
        again = False
        for i in range(len(gp)):
            for j in range(i + 1, len(gp)):
                if not apart(gp[i][0], gp[j][0], BAR_MIN):
                    both = gp[i][0] + gp[j][0]
                    gp[i] = (bbox(both), "box") if gp[i][1] == gp[j][1] == "box" else (hull(both), False); del gp[j]; merged += 1; again = True; break
            if again: break
    return [p for (p, _) in gp], merged

def in_convex(poly, x, y, m):
    """(x, y) inside a counter-clockwise convex outline grown by -m cm (m > 0: at least m inside it)."""
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        l = math.hypot(bx - ax, by - ay)
        if l > 1e-9 and ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) / l < m: return False
    return True

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
    """One texel of the building's own painted frame round a sheet, for all of its bars: the middle shade of that frame, where the
    picture is even and its normal flat (every bar points at it: one even colour, no smudge, no tilted light)."""
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

def tri_area(a, b, c):
    return abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2

def add_glass(o, found, keep_marks, base=None, nrm=None, fire_pts=None):
    """Lay each window's sheet on o (world metres): its panes glass (R, 1, 0), its bars and rim the building's own frame colour (one
    texel of it, NOT), the rest NOT (keeping fire, B 0 G 0, if keep_marks). The sheet sits GLASS_LIFT in front of all of the low model
    inside it, so no decimated triangle of a muntin or frame can come through the glass."""
    from mathutils.geometry import tessellate_polygon
    me = o.data
    cn_old = [tuple(c.vector) for c in me.corner_normals]
    nf0 = len(me.polygons)
    old = marks(me, np.array([p.loop_start for p in me.polygons], np.int64)) if keep_marks else None
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3) * 100.0   # (cm)
    bvh = BVHTree.FromPolygons([Vector(c) for c in co], [tuple(p.vertices) for p in me.polygons], all_triangles=False)
    uvl = me.uv_layers[0].data
    bm = bmesh.new(); bm.from_mesh(me)
    uvb = bm.loops.layers.uv[0]
    pl_ = bm.faces.layers.int.new("pane")      # (0 the building, -1 a sheet's bars and rim, i + 1 the i-th pane: triangulation copies it)
    Rs = []; skipped = 0; capped = 0; spreads = []; nbar = 0; badfill = 0; ngrid = 0
    for g in found:
        n = np.array(g["n"]); nn = Vector(n); c0 = np.array(g["c0"]); h = np.array(g["h"]); v = np.array(g["v"]); ring = g["ring"]; pn = g["panes"]
        if TAG in NO_GLASS_ABOVE and (c0 + h * np.mean([p[0] for p in ring]) + v * np.mean([p[1] for p in ring]))[2] > NO_GLASS_ABOVE[TAG]: skipped += 1; continue
        xs = [p[0] for p in ring]; ys = [p[1] for p in ring]
        gx, gy = np.meshgrid(np.arange(min(xs) - WALL_OUT, max(xs) + WALL_OUT + 1e-6, 2.0), np.arange(min(ys) - WALL_OUT, max(ys) + WALL_OUT + 1e-6, 2.0))
        depth = []; fuv = []; anyuv = None; wall = []
        for x, y in list(zip(gx.ravel(), gy.ravel())) + [tuple(p) for p in ring]:
            on = in_convex(ring, x, y, 0.0)
            if not on and not (in_convex(ring, x, y, -WALL_OUT) and not in_convex(ring, x, y, -WALL_IN)): continue
            hit = bvh.ray_cast(Vector(c0 + h * x + v * y) + nn * 80.0, -nn, 160.0)
            if hit[0] is None: continue
            hp = np.array(hit[0]); z = (hp - c0) @ n
            if not on: wall.append(z); continue   # (the wall round the window)
            if in_convex(ring, x, y, FRAME_BAND - 0.5): depth.append(z)   # (inside the panes' outline: the rim may tuck under a deep frame or sill, the glass never)
            if anyuv is None: anyuv = hit_uv(me, uvl, co, hit[2], hp)
            if not any(in_convex(p, x, y, -1.0) for p in pn): fuv.append(hit_uv(me, uvl, co, hit[2], hp))   # (a bar or the rim, 1 cm clear of glass)
        if len(depth) < 3: skipped += 1; continue
        dd = np.array(depth); med = float(np.median(dd))
        # The sheet goes in front of every frame, muntin and sill inside the window, which can stand out to the wall's face (a deep-set
        # window: the house's glass is 30 cm in); anything further out than WALL_PROUD past the wall round it stands in front of the
        # window (an awning, a beam) and is left to hide the glass as it would
        top = (float(np.median(wall)) if len(wall) >= 8 else med) + WALL_PROUD
        keep = dd[dd <= max(top, med)]
        mx = float(keep.max()) if len(keep) else med; spreads.append(mx - med)
        capped += bool((dd > max(top, med)).any()); d = mx + GLASS_LIFT
        if DEBUG: log("sheet", TAG, "R %.3f" % g["R"], "glass median %.1f wall %.1f laid at %.1f, %d of %d samples further out (to %.1f), centre %s" % (med, top - WALL_PROUD, d, (dd > max(top, med)).sum(), len(dd), dd.max(), np.round(c0 + h * np.mean([p[0] for p in ring]) + v * np.mean([p[1] for p in ring])).astype(int)))
        uv = frame_uv(fuv, base, nrm) if fuv else anyuv
        new = []
        if all(upright(p) for p in pn + [ring]):
            # (boxes in a box: a grid through every pane edge, each cell glass or bar, every cell sharing its corners. The polygon fill
            # left sliver triangles across the panes where many edges line up, thin dark lines through lit glass)
            gxs = sorted({round(x, 3) for p in pn + [ring] for (x, _) in p}); gys = sorted({round(y, 3) for p in pn + [ring] for (_, y) in p})
            vg = {(x, y): bm.verts.new(Vector((c0 + h * x + v * y + n * d) / 100.0)) for x in gxs for y in gys}
            first = len(Rs); Rs += [g["R"]] * len(pn)
            for i in range(len(gxs) - 1):
                for j in range(len(gys) - 1):
                    cx_, cy_ = (gxs[i] + gxs[i + 1]) / 2, (gys[j] + gys[j + 1]) / 2
                    if not in_convex(ring, cx_, cy_, 0.0): continue
                    k = next((t for t, p in enumerate(pn) if in_convex(p, cx_, cy_, 0.0)), -1)
                    f = bm.faces.new((vg[(gxs[i], gys[j])], vg[(gxs[i + 1], gys[j])], vg[(gxs[i + 1], gys[j + 1])], vg[(gxs[i], gys[j + 1])]))
                    f[pl_] = first + k + 1 if k >= 0 else -1; new.append(f); nbar += k < 0
            ngrid += 1; pn = []
        P2 = [tuple(p) for p in ring] + [tuple(q) for p in pn for q in p]
        allv = [bm.verts.new(Vector((c0 + h * x + v * y + n * d) / 100.0)) for (x, y) in P2] if pn else []
        tris = [] if not pn else tessellate_polygon([[Vector((x, y, 0.0)) for (x, y) in ring]] + [[Vector((x, y, 0.0)) for (x, y) in p] for p in pn])
        fill = sum(tri_area(P2[a], P2[b], P2[c]) for (a, b, c) in tris)
        want = area(ring) - sum(area(p) for p in pn)
        if pn and abs(fill - want) > 0.02 * area(ring) + 4.0: badfill += 1
        for (a, b, c) in tris:
            if len({a, b, c}) < 3: continue
            try: f = bm.faces.new((allv[a], allv[b], allv[c]))
            except ValueError: continue
            f[pl_] = -1; new.append(f); nbar += 1
        k = len(ring)
        for p in pn:
            f = bm.faces.new(allv[k:k + len(p)]); k += len(p); Rs.append(g["R"]); f[pl_] = len(Rs); new.append(f)
        for f in new:
            f.normal_update()
            if f.normal.dot(nn) < 0: f.normal_flip()
            for l in f.loops: l[uvb].uv = uv
    nq = len(Rs)
    added = [f for f in bm.faces if f[pl_] != 0]
    bmesh.ops.triangulate(bm, faces=added)
    bm.to_mesh(me); bm.free(); me.update()
    nf = len(me.polygons)
    pane = np.empty(nf, np.int32); me.attributes["pane"].data.foreach_get("value", pane); me.attributes.remove(me.attributes["pane"])
    assert (pane[:nf0] == 0).all() and (pane[nf0:] != 0).all(), "the building's own faces moved"
    cols = np.zeros((nf, 3), np.float32); cols[:] = NOT
    if old is not None:
        fire = (old[:, 1] < 0.5) & (old[:, 2] < 0.5)
        cols[:nf0][fire] = (0.0, 0.0, 0.0)
    if fire_pts is not None and len(fire_pts):
        from mathutils.kdtree import KDTree
        kd = KDTree(len(fire_pts))
        for i, p in enumerate(fire_pts): kd.insert(Vector(p), i)
        kd.balance()
        cen = np.empty(nf * 3); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3)[:nf0] * 100.0
        near = np.array([kd.find(Vector(c))[2] < FIRE_NEAR for c in cen])
        cols[:nf0][near] = (0.0, 0.0, 0.0); log("fire faces", int(near.sum()), "from", len(fire_pts), "fire triangles")
    Ra = np.array([0.0] + Rs, np.float32)
    gl = pane > 0
    cols[gl, 0] = Ra[pane[gl]]; cols[gl, 1] = 1.0; cols[gl, 2] = 0.0
    for at in list(me.color_attributes): me.color_attributes.remove(at)
    attr = me.color_attributes.new("WindowMask", "FLOAT_COLOR", "CORNER")
    lt = np.array([p.loop_total for p in me.polygons]); pl = np.repeat(np.concatenate([cols, np.ones((nf, 1), np.float32)], 1), lt, axis=0)
    attr.data.foreach_set("color", pl.ravel()); me.color_attributes.active_color = attr
    me.normals_split_custom_set(cn_old + [tuple(me.polygons[pi].normal) for pi in range(nf0, nf) for _ in me.polygons[pi].loop_indices]); me.update()
    sp = np.array(spreads) if spreads else np.zeros(1)
    log("sheets laid", len(found) - skipped, "skipped", skipped, "panes", nq, "bar pieces", nbar, "grids", ngrid, "fill off", badfill, "capped", capped,
        "front spread cm median %.1f p90 %.1f max %.1f" % (np.median(sp), np.percentile(sp, 90), sp.max()), "triangles added", nf - nf0)
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
def pics(folder, name):   # (the painted picture and its normals, for the bars' frame colour)
    p = [os.path.join(folder, "T_%s_%s.png" % (name, k)) for k in ("Base", "Normal")]
    return [picture(x) if os.path.exists(x) else None for x in p]
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
        add_glass(o, found, True, *pics(work, name))
        export(o, src_path); done(src_path, stamp)
        log(name, "written", src_path)
    else:
        low = os.path.join(out, new + ".fbx")
        orig, stamp = fresh(low)
        found = []; fire_pts = None
        if not os.path.exists(src_path): log(new, "no source model", src_path, "(an open stall: no glass)")
        else:
            pre = os.path.join(work, "PreGlass", name + ".fbx")
            ours = os.path.exists(pre + ".glass") and open(pre + ".glass").read().strip() == "%.3f" % os.path.getmtime(src_path)
            src = load(pre if ours else src_path)   # (an old model already given its own glass: its own marks are in the copy)
            fc = marks(src.data, np.array([p.loop_start for p in src.data.polygons], np.int64))
            if fc is not None:
                found = panes(src, fc)
                fi_ = np.nonzero((fc[:, 0] < 0.5) & (fc[:, 1] < 0.5) & (fc[:, 2] < 0.5))[0]
                if len(fi_):   # (the forge: its fire stays where it was, and nowhere else can burn, the window edges' baked fleck included)
                    ar_ = arrays(src.data); fire_pts = ar_[1][np.isin(ar_[2], fi_)].mean(1)
                    if new in NO_GLASS_ABOVE: fire_pts = fire_pts[fire_pts[:, 2] <= NO_GLASS_ABOVE[new]]   # (the church belfry's painted slot read as fire: a lit amber sliver at dusk)
        o = load(orig)
        add_glass(o, found, False, *pics(out, new), fire_pts=fire_pts)
        export(o, low); done(low, stamp)
        log(new, "written")
