# Repaints a shield's pictures from its own shape (2026-11-21, the kite; owner: "The paint on this shield looks horrible. It looks all smudged and the
# line where gold stops and black starts isn't defined. Please repaint."). Used by prop_clean.py's 'repaint' option.
# The library model's own picture is photo-like grime, and every way of sorting its pixels by colour left blotches and a ragged edge. Its SHAPE is clean:
# the cross, its border, the rim and the rivets all stand up from the face. So the full model is measured from the front and from the back (a height
# picture each, ray by ray), the parts are found by height, and a new clean picture is drawn from them: one flat charcoal face, the cross and rim in
# clean steel that takes the tier's colour, a thin dark line where they meet the face, a light bevel on their edges. prop_clean.py then lays these
# pictures on the full model from the front and back and bakes them like the old ones, so paint and relief line up exactly.
import bpy, math
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def height_fields(me, ai, rows):
    # (the full model seen along its thin axis ai from both sides: heights above the far side of its box in model units, 0 where nothing is hit.
    # Returns front (the low side of ai looks at you), back, and the frame: lo, ext, ui, vi, columns)
    co = [v.co.copy() for v in me.vertices]
    lo = [min(c[i] for c in co) for i in range(3)]; hi = [max(c[i] for c in co) for i in range(3)]
    ext = [hi[i] - lo[i] for i in range(3)]
    ui, vi = [i for i in range(3) if i != ai]
    cols = int(round(rows * ext[ui] / ext[vi]))
    me.calc_loop_triangles()
    tree = BVHTree.FromPolygons(co, [tuple(t.vertices) for t in me.loop_triangles])
    out = []
    for sgn in (-1.0, 1.0):   # (front: rays from below lo toward +ai; back: from above hi toward -ai)
        h = np.zeros((rows, cols), np.float32)
        d = Vector((0.0, 0.0, 0.0)); d[ai] = -sgn
        p = Vector((0.0, 0.0, 0.0)); p[ai] = (lo[ai] - ext[ai]) if sgn < 0 else (hi[ai] + ext[ai])
        for y in range(rows):
            p[vi] = lo[vi] + (y + 0.5) / rows * ext[vi]
            for x in range(cols):
                p[ui] = lo[ui] + (x + 0.5) / cols * ext[ui]
                loc = tree.ray_cast(p, d)[0]
                if loc is not None: h[y, x] = (hi[ai] - loc[ai]) if sgn < 0 else (loc[ai] - lo[ai])
        out.append(h)
    return out[0], out[1], dict(lo=lo, ext=ext, ui=ui, vi=vi, ai=ai, rows=rows, cols=cols)

def _box(a_, rad):
    a_ = a_.astype(np.float64)
    for ax in (0, 1):
        c = np.cumsum(a_, axis=ax); n = a_.shape[ax]
        hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
        t = np.take(c, hi_, axis=ax); b = np.take(c, np.maximum(lo_, 0), axis=ax)
        sh = [1, 1]; sh[ax] = n
        a_ = t - np.where(lo_.reshape(sh) < 0, 0.0, b)
    return a_
def _soft(a_, rad, times=3):   # (a box blur, three times over: near enough a Gaussian; the mean, edges clamped)
    n = _box(np.ones(a_.shape), rad)
    for _ in range(times): a_ = _box(a_, rad) / n
    return a_
def _grow(m, k):   # (k pixels wider, an octagon: 4 and 8 neighbours in turn)
    for i in range(k):
        n4 = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
        if i % 2: n4 = n4 | np.roll(np.roll(m, 1, 0), 1, 1) | np.roll(np.roll(m, 1, 0), -1, 1) | np.roll(np.roll(m, -1, 0), 1, 1) | np.roll(np.roll(m, -1, 0), -1, 1)
        m = n4
    return m
def _shrink(m, k): return ~_grow(~m, k)
def _maxf(a_, k):   # (the highest value within about k pixels, a square)
    for ax in (0, 1):
        out = a_.copy(); step = 1
        while step <= k:
            out = np.maximum(out, np.maximum(np.roll(out, step, ax), np.roll(out, -step, ax))); step *= 2
        a_ = out
    return a_
def _flood(seed, allowed):   # (everything in 'allowed' that joins on to 'seed')
    ys, xs = np.nonzero(allowed)
    if not len(ys): return seed & allowed
    y0, y1, x0, x1 = max(0, ys.min() - 1), ys.max() + 2, max(0, xs.min() - 1), xs.max() + 2
    al = allowed[y0:y1, x0:x1]; m = seed[y0:y1, x0:x1] & al; n = -1
    while True:
        for _ in range(12):
            g = m.copy(); g[1:] |= m[:-1]; g[:-1] |= m[1:]; g[:, 1:] |= m[:, :-1]; g[:, :-1] |= m[:, 1:]
            m = g & al
        c = int(m.sum())
        if c == n: break
        n = c
    out = np.zeros_like(seed); out[y0:y1, x0:x1] = m
    return out
def _face(H, S, start, ring, cm_px, name, o, deg=6):
    # (the face's own gentle dome, and which pixels are the face. A polynomial through the middle of the face first (leaving out, round after round,
    # whatever stands off it); then the face is everything that joins on to that middle without climbing a wall: the steep sides of the rim and of
    # the cross stop it)
    rows, cols = H.shape
    yy, xx = np.mgrid[0:rows, 0:cols]
    X = xx / (cols - 1.0) * 2.0 - 1.0; Y = yy / (rows - 1.0) * 2.0 - 1.0
    terms = [(i, j) for i in range(deg + 1) for j in range(deg + 1 - i)]
    def basis(xs, ys): return np.stack([xs ** i * ys ** j for i, j in terms], axis=-1)
    w = start.copy(); P = None
    for thr in (1.0, 0.5, 0.25, 0.15, 0.1, 0.1):
        sub = np.zeros_like(w); sub[::4, ::4] = True; pick = w & sub
        coef = np.linalg.lstsq(basis(X[pick], Y[pick]), H[pick].astype(np.float64), rcond=None)[0]
        P = np.zeros(H.shape)
        for (i, j), c in zip(terms, coef): P += c * X ** i * Y ** j
        w = start & (np.abs(H - P) < thr)
    D = np.where(S, H - P, 0.0)
    G = _soft(np.where(S, D, 0.0), max(1, int(0.04 / cm_px)), 2)
    gy, gx = np.gradient(G, cm_px)
    slope = np.hypot(gx, gy)
    wall = _grow(slope > o["wall"], 1)
    face = _flood(_shrink(w & ring, 2), _shrink(S, 2) & ~wall)   # (from the ring of face between the rim and the cross)
    print("PROP %s repaint: %.0f%% of the outline is face" % (name, 100.0 * face.sum() / max(1, S.sum())))
    return D, face, slope
def _srgb(L):
    L = np.clip(L, 0.0, 1.0)
    return np.where(L <= 0.0031308, L * 12.92, 1.055 * np.power(L, 1.0 / 2.4) - 0.055)

def design(front, back, fr, length_cm, name, O=None):
    # The new pictures, in the height pictures' own frame. Returns for the front and for the back: base (rows x cols x 3, sRGB values) and aux (tier
    # mask, roughness, metal). O: any of the numbers below (colours as hue, saturation, value; the steel's brightness under the tier's colour 0..1).
    o = dict(field=(220.0, 0.14, 0.115), raise_cm=0.12, edge_cm=4.6, line_cm=0.11, frame=0.55, panel=0.02, scroll=0.42, rim=0.40, drop=0.14, scroll_cm=0.03, wall=1.0, foot=0.3, foot_cm=0.9, light=(-0.6, 0.8),
             shade=0.9, bevel=1.6)
    o.update(O or {})
    cm = length_cm / fr["ext"][fr["vi"]]; rows, cols = front.shape; px = length_cm / rows   # (cm per model unit; cm per pixel)
    h6 = o["field"][0] / 60.0; c_ = o["field"][2] * o["field"][1]; x_ = c_ * (1.0 - abs(h6 % 2.0 - 1.0)); m_ = o["field"][2] - c_
    fcol = np.array([(c_, x_, 0), (x_, c_, 0), (0, c_, x_), (0, x_, c_), (x_, 0, c_), (c_, 0, x_)][int(h6) % 6]) + m_
    yy, xx = np.mgrid[0:rows, 0:cols]
    out = []
    for side, Hm in (("front", front), ("back", back)):
        H = Hm.astype(np.float64) * cm; S = Hm > 0
        k_edge = int(o["edge_cm"] / px)
        inner = _box(S, k_edge) >= (2 * k_edge + 1) ** 2 - 0.5
        k_in = int((o["edge_cm"] - 0.9) / px)
        ring = (_box(S, k_in) >= (2 * k_in + 1) ** 2 - 0.5) & ~inner
        R, face, slope = _face(H, S, inner | ring, ring, px, name + " " + side, o)
        up = S & ~face
        k_s = max(2, int(0.2 / px))
        cross = np.zeros_like(S)
        if side == "front":
            cand = up & inner
            mid_ = np.zeros_like(S); mid_[int(rows * 0.4):int(rows * 0.7), int(cols * 0.4):int(cols * 0.6)] = True
            cross = _flood(_shrink(cand, k_s) & mid_, cand)   # (the one piece in the middle: a dent's rim elsewhere on the face is not a cross)
            k_c = int(0.6 / px); cross = _shrink(_grow(cross, k_c), k_c)   # (its sunk panel is the cross too)
            q = 4; rq, cq = rows // q * q, cols // q * q   # (and whatever else the face outside can't reach, on a coarse grid)
            coarse = cross[:rq, :cq].reshape(rq // q, q, cq // q, q).any(axis=(1, 3))
            seed = np.zeros_like(coarse); seed[0, :] = True; seed[-1, :] = True; seed[:, 0] = True; seed[:, -1] = True
            outside = _flood(seed, ~coarse)
            hole = np.zeros_like(S); hole[:rq, :cq] = np.repeat(np.repeat(~outside & ~coarse, q, 0), q, 1)
            core = cross | hole
            # (the cross stands on a low sloping foot, too gentle to count as a wall: out to the bottom of that slope, but no further than 'foot_cm')
            foot = _flood(core, core | (inner & (slope > o["foot"]) & (R > -0.03))) & _grow(core, int(o["foot_cm"] / px))
            cross = _soft(core | foot, 2, 2) > 0.5   # (a clean line round it)
        near_edge = S & ~_shrink(S, int(1.0 / px))
        rim = _flood(up & near_edge, up & ~cross & ~inner) | (S & ~_shrink(S, 3))
        k_r = int(0.7 / px); rim = S & (_soft(rim | ~S, k_r, 2) > 0.5)   # (notches and splinters along its inner edge evened out)
        # shading from the shape itself: light from the upper left on the slopes, light on the outward edges, dark in the creases
        G = _soft(np.where(S, H, H[S].min()), max(1, int(0.05 / px)))
        gy, gx = np.gradient(G, px)
        lit = -(gx * o["light"][0] + gy * o["light"][1])
        G2 = _soft(G, max(1, int(0.06 / px)))
        lap = (np.roll(G2, 1, 0) + np.roll(G2, -1, 0) + np.roll(G2, 1, 1) + np.roll(G2, -1, 1) - 4.0 * G2) / (px * px)
        shade = np.clip(1.0 + o["shade"] * 0.35 * np.clip(lit, -1.5, 1.5) - o["bevel"] * 0.012 * np.clip(lap, -40.0, 40.0), 0.25, 2.2)
        L = np.zeros((rows, cols)); L[:] = o["rim"]   # (past the outline: the rim's steel, for the rim's outer wall)
        if cross.any():
            # the cross's parts: a border of one width all round (as wide as where its ridge runs), inside it the sunk panel, and the scrollwork
            # that stands up from the panel
            depth = np.zeros((rows, cols)); m = core.copy(); kmax = int(1.4 / px)
            for k in range(kmax):
                m = _shrink(m, 1); depth += m
            Rc = np.where(core, R, -9.0)
            ridge = core & (Rc > _maxf(Rc, int(1.3 / px)) - o["drop"]) & (depth < kmax - 1)
            wf = float(np.percentile(depth[ridge], 85)) + 1.0 if ridge.any() else 0.45 / px
            panel = core & (depth > wf)
            # (where two arms meet, an even width cuts the inside corner off at a slant, and the ridge there runs square into the corner: near those
            # corners the border is measured straight across and straight up only)
            kf = int(round(wf)); r_ = max(3, int(wf * 0.6))
            sharp = core & ~_shrink(core, 1) & (_box(core, r_) / float((2 * r_ + 1) ** 2) > 0.68)
            plus = core.copy()
            for d in range(1, kf + 1): plus &= np.roll(core, d, 0) & np.roll(core, -d, 0) & np.roll(core, d, 1) & np.roll(core, -d, 1)
            panel = panel | (plus & _grow(sharp, int(1.7 * wf)))
            panel = (_soft(panel & cross, 1, 2) > 0.5) & cross; frame = cross & ~panel
            wt = np.maximum(_box(_box(panel, int(0.5 / px)), int(0.5 / px)), 1e-9)
            floor = _box(_box(np.where(panel, R, 0.0), int(0.5 / px)), int(0.5 / px)) / wt
            scroll = panel & (R - floor > o["scroll_cm"]) & ~_grow(sharp, int(1.7 * wf)); scroll = _grow(_shrink(scroll, 2), 2) & panel; scroll = _soft(scroll, 1, 2) > 0.5
            Lc = np.where(frame, o["frame"], np.where(scroll, o["scroll"], o["panel"]))
            L = np.where(cross, Lc * np.where(frame, shade, 1.0), L)
            print("PROP %s repaint: cross %.1f%% of the front, border %.2f cm wide (%.0f%% of it), scrollwork %.0f%%" % (name, 100.0 * cross.sum() / S.sum(), wf * px, 100.0 * frame.sum() / cross.sum(), 100.0 * scroll.sum() / cross.sum()))
        L = np.where(rim | ~S, o["rim"] * shade, L)
        metal = cross | rim | ~S
        fieldm = S & ~metal
        line = fieldm & _grow(metal, max(1, int(round(o["line_cm"] / px))))
        cy, cx = rows * 0.52, cols * 0.5
        glow = 1.0 + 0.22 * np.clip(1.0 - (((xx - cx) / (cols * 0.5)) ** 2 + ((yy - cy) / (rows * 0.5)) ** 2), 0.0, 1.0)
        base = np.zeros((rows, cols, 3)); aux = np.zeros((rows, cols, 3))
        g = _srgb(np.clip(L, 0.0, 1.0))
        for c in range(3):
            base[..., c] = np.where(metal, g, np.where(line, fcol[c] * 0.3, fcol[c] * glow))
        aux[..., 0] = metal; aux[..., 1] = np.where(metal, 0.35, np.where(line, 0.75, 0.58)); aux[..., 2] = metal
        print("PROP %s repaint %s: rim %.0f%%, tier colour on %.0f%%" % (name, side, 100.0 * (rim & S).sum() / S.sum(), 100.0 * (metal & S).sum() / S.sum()))
        out.append((base.astype(np.float32), aux.astype(np.float32)))
    return out[0], out[1]

def lay_on(mat, F, B, front, back, fr, name):
    # The new pictures laid on the full model straight through it, front picture on its front, back picture on its back (a point is on the back if it
    # lies beyond the height half way between the two faces). Returns what to bake: the colour, the metal/roughness and the tier mask.
    ext, lo, ui, vi, ai = fr["ext"], fr["lo"], fr["ui"], fr["vi"], fr["ai"]
    both = (front > 0) & (back > 0)
    mid = np.where(both, 0.5 * ((ext[ai] - front) + back) / ext[ai], 0.5).astype(np.float32)   # (front heights run from the high side of the box)
    nt = mat.node_tree; N = nt.nodes; L = nt.links
    def img(tag, arr, colour, flt=False):
        rows, cols = arr.shape[:2]
        if arr.ndim == 2: arr = np.dstack([arr, arr, arr])
        im = bpy.data.images.new("Repaint%s_%s" % (tag, name), cols, rows, alpha=True, float_buffer=flt)
        if not colour: im.colorspace_settings.name = "Non-Color"
        im.pixels.foreach_set(np.dstack([arr, np.ones((rows, cols), np.float32)]).astype(np.float32).reshape(-1)); im.update()
        return im
    tc = N.new("ShaderNodeTexCoord"); mp = N.new("ShaderNodeMapping"); mp.vector_type = "POINT"
    mp.inputs["Scale"].default_value = [1.0 / e for e in ext]; mp.inputs["Location"].default_value = [-l / e for l, e in zip(lo, ext)]
    L.new(tc.outputs["Object"], mp.inputs["Vector"])
    sp = N.new("ShaderNodeSeparateXYZ"); L.new(mp.outputs["Vector"], sp.inputs[0])
    cb = N.new("ShaderNodeCombineXYZ"); L.new(sp.outputs[ui], cb.inputs[0]); L.new(sp.outputs[vi], cb.inputs[1])
    def tex(im):
        n = N.new("ShaderNodeTexImage"); n.image = im; n.extension = "EXTEND"; n.interpolation = "Linear"
        L.new(cb.outputs[0], n.inputs["Vector"]); return n
    gt = N.new("ShaderNodeMath"); gt.operation = "GREATER_THAN"
    L.new(sp.outputs[ai], gt.inputs[0]); L.new(tex(img("Mid", mid, False, True)).outputs["Color"], gt.inputs[1])
    def pick(a_, b_):
        mx = N.new("ShaderNodeMix"); mx.data_type = "RGBA"
        L.new(gt.outputs[0], mx.inputs[0]); L.new(a_.outputs["Color"], mx.inputs[6]); L.new(b_.outputs["Color"], mx.inputs[7])
        return mx.outputs[2]
    base = pick(tex(img("BaseF", F[0], True)), tex(img("BaseB", B[0], True)))
    aux = pick(tex(img("AuxF", F[1], False)), tex(img("AuxB", B[1], False)))
    sx = N.new("ShaderNodeSeparateColor"); L.new(aux, sx.inputs[0])
    mk = N.new("ShaderNodeCombineColor"); mr = N.new("ShaderNodeCombineColor")
    for i in range(3): L.new(sx.outputs[0], mk.inputs[i])
    mr.inputs[0].default_value = 0.0; L.new(sx.outputs[1], mr.inputs[1]); L.new(sx.outputs[2], mr.inputs[2])
    return {"Base": base, "MR": mr.outputs[0], "Mask": mk.outputs[0]}
