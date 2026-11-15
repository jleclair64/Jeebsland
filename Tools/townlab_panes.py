# Even window panes (owner, 2026-11-15: "repaint the windows so they are all dark and then all bright/orange when they light up ...
# I don't like the mixed orange/black look. It looks sloppy.")
#   blender -b -P Tools/townlab_panes.py -- <dir> <NewName> [<NewName> ...]
# Also for an old /Game/Buildings model (no T_<Name>_Mask.png: <dir> Saved/KnightBuild/Buildings, the church OakHillChurch_300k):
# only its colour and glow pictures are evened, its window marks stay in the model's vertex colour.
# Run on townlab_bake.py's pictures in <dir> (Saved/KnightBuild/TownLab), after the bake (BuildTownLabBuildings.ps1 does it). It
# rewrites T_<NewName>_Base/Glow/Mask.png in place; the first run keeps the originals in <dir>/PrePanes (later runs start from those,
# so it can be run again with new numbers without a rebake).
#
# What was wrong: a lit window's glass is baked from Meshy's 300k model, whose panes were painted as blotches of lit orange and dark
# curtain/muntin shapes, in both the colour and the glow picture. M_TL_Building turns the colour to dark glass only where the glow
# picture is bright (GlassMask), and its emissive is the glow picture itself, so by day a pane was dark glass mixed with the painted
# orange and brown, and at dusk orange with black patches.
# What this does, per texel at the mask's size. The window marks are per face of the 300k model, so the painted bars (muntins,
# leading) are gaps between them. A pane is a window mark lit in the glow picture, plus the dark inside the mark that lit pane closes in
# on all four sides (Meshy's blotches); dark reaching a mark's rim is the frame. Then:
#   - the pane's colour becomes one flat dark glass (GLASS_SRGB), and its glow one flat amber (GLOW_SRGB);
#   - the bars between panes are painted one dark wood (BAR_SRGB: some were painted gold and read as lit by day), and warm lit-pane
#     paint left on the rim round a pane is that wood too;
#   - the pane's mask is a window (G 1, B 0) with its window's random number (R), one per window on the model (grouped by 3D spot on
#     the low model <dir>/<NewName>.fbx, since its UVs cut windows apart), carried round it (see the note at R_SPREAD's use);
#   - the bake's fill just off the model's faces round a pane is pane too (filtering at a UV seam through a window mixed it in as cracks);
#   - window-marked texels that are not pane (frame the marks caught) stop being windows (G 0, B 1, no glow).
import bpy, sys, os, shutil
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
folder, names = a[0], a[1:]

PANE_GLOW = 0.20           # glow (sRGB, max channel) above this is a lit pane
BAR_W = 3                  # texels at the mask's size: a gap between panes up to about twice this wide is a bar (painted dark wood)
GLASS_SRGB = (0.16, 0.17, 0.19)   # the pane's colour in the colour picture (sRGB): a dark, very slightly cool glass
BAR_SRGB = (0.30, 0.19, 0.11)     # a painted bar (muntin, leading) between panes: rich brown wood (darker read as black against a lit pane)
GLOW_SRGB = (0.90, 0.46, 0.14)    # the pane's glow (sRGB): the old glow pictures' lit amber (median 0.80-0.91, 0.40-0.46, 0.12-0.15), one even tone
R_SPREAD = 24              # texels each window's random number is carried out past its pane (see below)
WIN_CELLS = 100           # a window's glass parts closer than the model's size / this (about 20 cm on a house, a transom's width) are one window
BOX_PAD = 0.04            # (m) round a window's box, for the unmarked lit paint below
PLANE_TOL = 0.15          # (m) texels this close to a window's glass plane belong to it: the glass and its muntins (they stand a few cm proud of it)
STONE_R = 20              # (texels, the old church) how far round a pane red tracery paint is made stone
STONE_TRACERY = {"OakHillChurch_300k", "TL_Church"}   # (the churches: stone tracery round stained glass)
OUTLINE_BIN = 0.08        # (m) rows and columns of a window's outline (a texel is about 10 cm on the town hall)
ENCLOSE = 14               # texels: dark inside a window mark with lit pane this close on all four sides is a blotch (made pane)
CRACK = 2                  # texels: a gap this narrow (or less) with pane on both sides is a crack through the pane, filled
CRACK_GLOW = 0.08          # (and only where the glow picture had some light: the bars Meshy painted are dark there)

OLD_FORGE = {"OakHillSmith_300k"}   # (old /Game/Buildings models whose forge glows: building_clean.py's amber is the fire's hue too)

def log(*x): print("TLPANES", *x, flush=True)

def read(path):
    img = bpy.data.images.load(path); img.colorspace_settings.name = "Non-Color"
    w, h = img.size; p = np.empty(w * h * 4, np.float32); img.pixels.foreach_get(p)
    bpy.data.images.remove(img)
    return p.reshape(h, w, 4)[..., :3].copy()
def write(arr, path):
    h, w = arr.shape[:2]
    im = bpy.data.images.new(os.path.basename(path), w, h, alpha=False, float_buffer=False)
    im.colorspace_settings.name = "Non-Color"
    rgba = np.concatenate([np.clip(arr, 0, 1), np.ones((h, w, 1), np.float32)], 2).astype(np.float32)
    im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    bpy.data.images.remove(im)
def box_sum(x, r):
    x = x.astype(np.float32)
    for ax in (0, 1):
        c = np.cumsum(np.pad(x, [(r + 1, r) if i == ax else (0, 0) for i in range(x.ndim)]), axis=ax)
        n = x.shape[ax]
        x = np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=ax) - np.take(c, range(0, n), axis=ax)
    return x
def dilate(m, r): return box_sum(m, r) > 0.5 if r > 0 else m
def parts(m):   # 8-connected parts of m: a label per texel (-1 outside m)
    ys, xs = np.nonzero(m)
    H, W = m.shape
    lab = np.full(m.shape, -1, np.int64); lab[ys, xs] = ys * W + xs
    cur = lab[ys, xs].copy()
    for _ in range(2000):
        best = cur.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    nb = lab[np.clip(ys + dy, 0, H - 1), np.clip(xs + dx, 0, W - 1)]
                    best = np.where((nb >= 0) & (nb < best), nb, best)
        best = np.minimum(best, lab.ravel()[best])   # (jump along the chain of labels)
        if np.array_equal(best, cur): break
        cur = best; lab[ys, xs] = cur
    return lab
def erode(m, r): return box_sum(m, r) > (2 * r + 1) ** 2 - 0.5 if r > 0 else m
def to_lin(x):
    x = np.clip(x, 0, 1); return np.where(x <= 0.04045, x / 12.92, np.power((x + 0.055) / 1.055, 2.4))
def lch(rgb):
    lin = to_lin(rgb)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], np.float32)
    xyz = lin @ M.T / np.array([0.9505, 1.0, 1.089], np.float32)
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    A = 500 * (f[..., 0] - f[..., 1]); B = 200 * (f[..., 1] - f[..., 2])
    return 116 * f[..., 1] - 16, np.hypot(A, B), (np.degrees(np.arctan2(B, A)) + 360) % 360

def surface(fbx, S):   # where the low model's faces lie on the picture: each texel's spot on the model (NaN off every face)
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=fbx)
    pos = np.full((S, S, 3), np.nan, np.float32)
    for o in [o for o in bpy.data.objects if o.type == "MESH"]:
        me = o.data; me.calc_loop_triangles()
        tl = np.empty(len(me.loop_triangles) * 3, np.int64); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(-1, 3)
        uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers[0].data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
        lv = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lv)
        co = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
        Mw = np.array(o.matrix_world, np.float32); co = co @ Mw[:3, :3].T + Mw[:3, 3]
        T = uv[tl].astype(np.float64) * S - 0.5; Pt = co[lv[tl]]   # (in texel-centre units)
        lo = np.floor(T.min(1)).astype(np.int64); ext = (np.ceil(T.max(1)).astype(np.int64) - lo).max(1) + 1
        Ks = np.array([2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]); bucket = np.searchsorted(Ks, ext)
        for bi, K in enumerate(Ks):
            sel = np.nonzero(bucket == bi)[0]
            step = max(1, 2000000 // (K * K))
            oy, ox = np.mgrid[0:K, 0:K]; ox = ox.ravel()[None]; oy = oy.ravel()[None]
            for c0 in range(0, len(sel), step):
                s = sel[c0:c0 + step]
                X = lo[s, 0, None] + ox; Y = lo[s, 1, None] + oy
                a, b, c = T[s, 0, :, None], T[s, 1, :, None], T[s, 2, :, None]
                d = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
                d = np.where(np.abs(d) < 1e-12, 1e-12, d)
                w0 = ((b[:, 1] - c[:, 1]) * (X - c[:, 0]) + (c[:, 0] - b[:, 0]) * (Y - c[:, 1])) / d
                w1 = ((c[:, 1] - a[:, 1]) * (X - c[:, 0]) + (a[:, 0] - c[:, 0]) * (Y - c[:, 1])) / d
                w2 = 1 - w0 - w1
                ok = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4) & (X >= 0) & (Y >= 0) & (X < S) & (Y < S)
                ti, pi = np.nonzero(ok)
                if not len(ti): continue
                p = Pt[s[ti], 0] * w0[ti, pi, None] + Pt[s[ti], 1] * w1[ti, pi, None] + Pt[s[ti], 2] * w2[ti, pi, None]
                pos[Y[ti, pi], X[ti, pi]] = p
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    return pos
def groups3d(pts, cell):   # parts of a cloud of spots on the model, joined within about a cell (26-connected cells)
    v = np.floor(pts / cell).astype(np.int64); v -= v.min(0)
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

keep = os.path.join(folder, "PrePanes"); os.makedirs(keep, exist_ok=True)
for n in names:
    P = lambda k: os.path.join(folder, "T_%s_%s.png" % (n, k))
    if not os.path.exists(P("Glow")): log(n, "no glow picture: no lit windows, left alone"); continue
    if n in OLD_FORGE: log(n, "an old model with a forge: its fire can't be told from the windows in the glow picture, left alone"); continue
    stamp = os.path.join(keep, "%s.done" % n)   # (the sizes this tool wrote last: a picture that differs is a fresh bake and replaces the kept original)
    done = open(stamp).read().split() if os.path.exists(stamp) else []
    kinds = ("Base", "Glow", "Mask") if os.path.exists(P("Mask")) else ("Base", "Glow")
    old = len(kinds) == 2   # (an old /Game/Buildings model: its window marks are in the model's vertex colour, M_Building reads them)
    for i, k in enumerate(kinds):
        kept = os.path.join(keep, os.path.basename(P(k)))
        if not os.path.exists(kept) or len(done) != len(kinds) or str(os.path.getsize(P(k))) != done[i]:
            shutil.copy2(P(k), kept)
    base = read(os.path.join(keep, os.path.basename(P("Base"))))
    glow = read(os.path.join(keep, os.path.basename(P("Glow"))))
    if old:   # the marks are where the glow picture is lit (the vertex colour marks the faces; the picture says where on them)
        mask = np.zeros_like(glow); mask[..., 1] = dilate(glow.max(-1) > 0.02, 1); mask[..., 2] = 1.0 - mask[..., 1]
    else: mask = read(os.path.join(keep, os.path.basename(P("Mask"))))
    fire = (mask[..., 1] < 0.5) & (mask[..., 2] < 0.5)   # (the forge, (0,0,0) in the mask: burns all day, kept as it is)
    S = mask.shape[0]; k = base.shape[0] // S
    b2 = base.reshape(S, k, S, k, 3).mean((1, 3))
    L, C, H = lch(b2)
    win = (mask[..., 1] > 0.5) & (mask[..., 2] < 0.5)
    # The window marks are per face of the 300k model, so the painted bars (muntins, leading) that were their own faces are already
    # gaps between the marks: those are the window's lines, kept as they are (never lit). Inside the marks, every texel is pane:
    # Meshy's dark blotches (curtains, smudges, the bake's soft wedges) and the glow's soft dips are filled to one even pane, except
    # dark that reaches the mark's rim (the frame, which a mark caught along a window's edge): that stays frame.
    def within(m, ax, sgn, n):   # m within n texels along one direction
        out = np.zeros_like(m)
        for t in range(1, n + 1): out |= np.roll(m, sgn * t, ax)
        return out
    lit = win & (glow.max(-1) > PANE_GLOW)
    e = ENCLOSE * (2 if old else 1)
    enclosed = within(lit, 0, 1, e) & within(lit, 0, -1, e) & within(lit, 1, 1, e) & within(lit, 1, -1, e)
    pane = lit | (win & enclosed)
    hole = ~pane & win & within(pane, 0, 1, 1) & within(pane, 0, -1, 1) & within(pane, 1, 1, 1) & within(pane, 1, -1, 1)
    # and hairline cracks through a pane (a bake ray that caught a frame face, a mark one face short): a gap at most CRACK texels wide
    # with pane on both sides, lit in the glow picture, drew a thin dark line across lit windows (2026-11-15); bars (dark in the glow picture) stay
    if not old: hole |= ~pane & (glow.max(-1) > CRACK_GLOW) & ((within(pane, 0, 1, CRACK) & within(pane, 0, -1, CRACK)) | (within(pane, 1, 1, CRACK) & within(pane, 1, -1, CRACK)))
    pane |= hole
    # each pane texel takes its window's random number: grown out from the marked texels
    r = np.where(win, mask[..., 0], -1.0)
    for _ in range(4):
        grow = r < 0
        if not (grow & pane).any(): break
        nb = np.max(np.stack([np.roll(r, s, ax) for ax in (0, 1) for s in (-1, 1)]), 0)
        r = np.where(grow & (nb >= 0), nb, r)
    # One number per window: building_clean.py groups a window's glass faces in 18 cm cells, so a big window or one whose bars cut
    # its glass apart could come out as two or three groups with their own numbers, lighting in parts (2026-11-15: orange slivers in
    # dark windows). Panes joined across their bars are one window and all take the number most of it has.
    if not old:
        lab = parts(dilate(pane, BAR_W) & (b2.max(-1) > 0.02))
        sel = pane & (lab >= 0)
        q = np.round(np.clip(r[sel], 0, 1) * 31.0).astype(np.int64)
        _, comp = np.unique(lab[sel], return_inverse=True)
        votes = np.zeros((comp.max() + 1, 32), np.int64); np.add.at(votes, (comp, q), 1)
        r[sel] = votes.argmax(1)[comp] / 31.0
        log(n, "windows (panes joined across their bars): %d" % (comp.max() + 1))
    # and on the model itself: the low model's UVs cut many windows into two or more islands (smart_project), whose parts lie apart on
    # the picture, so the step above could not join them and one half lit while the other stayed dark (2026-11-15). Pane texels whose
    # spots on the model lie within a cell of each other are one window and take one number.
    margin = np.zeros_like(pane); onface = np.ones_like(pane); force_bar = np.zeros_like(pane); glow_only = np.zeros_like(pane)
    fbx = os.path.join(folder, "%s.fbx" % n)
    if not old and os.path.exists(fbx):
        pos = surface(fbx, S); onface = ~np.isnan(pos[..., 0])
        sel = pane & onface
        if sel.any():
            pts = pos[sel]; cell = float(np.linalg.norm(np.nanmax(pos.reshape(-1, 3), 0) - np.nanmin(pos.reshape(-1, 3), 0))) / WIN_CELLS
            g = groups3d(pts, cell)
            q = np.round(np.clip(r[sel], 0, 1) * 31.0).astype(np.int64)
            votes = np.zeros((g.max() + 1, 32), np.int64); np.add.at(votes, (g, q), 1)
            r[sel] = votes.argmax(1)[g] / 31.0
            # Meshy's lit-pane paint where the bake left no window mark (part of a pane whose 300k faces weren't marked): orange glass
            # by day that never lit (2026-11-15, the smithy's back windows). Warm paint on the model inside a window's own box is pane.
            gu = np.unique(g); lo3 = np.full((g.max() + 1, 3), np.inf, np.float32); hi3 = np.full((g.max() + 1, 3), -np.inf, np.float32)
            np.minimum.at(lo3, g, pts); np.maximum.at(hi3, g, pts)
            cand = onface & ~pane & ~fire & (win | ((L > 45) & (C > 25) & (H > 45) & (H < 95)))   # (and marked glass left dark: black cuts)
            cy, cx = np.nonzero(cand); cp = pos[cy, cx]; added = 0
            for gi in gu:
                inb = np.all((cp >= lo3[gi] - BOX_PAD) & (cp <= hi3[gi] + BOX_PAD), 1)
                if inb.any():
                    pane[cy[inb], cx[inb]] = True; r[cy[inb], cx[inb]] = votes[gi].argmax() / 31.0; added += int(inb.sum())
            log(n, "unmarked lit paint or dark marked glass in a window made pane: %d texels" % added)
            log(n, "windows on the model: %d (cell %.3f)" % (len(np.unique(g)), cell))
            # Each window filled whole in its own plane (2026-11-15, the town hall's cuts: one-texel strips of frame paint across the glass
            # read as thick black slashes when lit, since a texel here is about 10 cm): inside the glass's outline, every texel within
            # PLANE_TOL of the glass's plane is pane, its muntins too: a lit window is one flat orange (the reviewer, 2026-11-15: bars at
            # this texel size came out torn and chipped against the glow), and by day the muntins are painted one wood over dark glass.
            for pass_ in range(2):   # (twice: a window whose panes were two groups apart is one group once its bars are filled)
                if pass_:
                    sel = pane & onface; pts = pos[sel]; g = groups3d(pts, cell)
                    q = np.round(np.clip(r[sel], 0, 1) * 31.0).astype(np.int64)
                    votes = np.zeros((g.max() + 1, 32), np.int64); np.add.at(votes, (g, q), 1); r[sel] = votes.argmax(1)[g] / 31.0
                    gu = np.unique(g); lo3 = np.full((g.max() + 1, 3), np.inf, np.float32); hi3 = np.full((g.max() + 1, 3), -np.inf, np.float32)
                    np.minimum.at(lo3, g, pts); np.maximum.at(hi3, g, pts)
                sy, sx = np.nonzero(sel)
                oy, ox = np.nonzero(onface & ~fire); op = pos[oy, ox]
                nfix = nbar = 0 if not pass_ else nfix
                for gi in gu:
                    gp = pts[g == gi]
                    if len(gp) < 40: continue
                    c0 = gp.mean(0); ev, evec = np.linalg.eigh(np.cov((gp - c0).T))
                    nrm = evec[:, 0]; uax = np.cross([0.0, 0.0, 1.0], nrm)
                    if np.linalg.norm(uax) < 0.3: continue   # (a skylight: left as it is)
                    uax /= np.linalg.norm(uax); vax = np.cross(nrm, uax)
                    inb = np.all((op >= lo3[gi] - BOX_PAD) & (op <= hi3[gi] + BOX_PAD), 1) & (np.abs((op - c0) @ nrm) < PLANE_TOL)
                    if inb.sum() < 40: continue
                    ty, tx = oy[inb], ox[inb]; PP = op[inb] - c0
                    u = PP @ uax; v = PP @ vax
                    gu_ = (gp - c0) @ uax; gv_ = (gp - c0) @ vax
                    bu = np.floor(u / OUTLINE_BIN).astype(int); bv = np.floor(v / OUTLINE_BIN).astype(int)
                    gbu = np.floor(gu_ / OUTLINE_BIN).astype(int); gbv = np.floor(gv_ / OUTLINE_BIN).astype(int)
                    ou, ov = bu.min(), bv.min(); bu -= ou; bv -= ov; gbu = np.clip(gbu - ou, 0, bu.max()); gbv = np.clip(gbv - ov, 0, bv.max())
                    NU, NV = bu.max() + 1, bv.max() + 1
                    # the glass's outline: per row its span along u, per column its span along v (arched tops kept)
                    rlo = np.full(NV, 10**9); rhi = np.full(NV, -1); np.minimum.at(rlo, gbv, gbu); np.maximum.at(rhi, gbv, gbu)
                    clo = np.full(NU, 10**9); chi = np.full(NU, -1); np.minimum.at(clo, gbu, gbv); np.maximum.at(chi, gbu, gbv)
                    # (a row or column that is all bar, a muntin, has no glass: it takes the span of the glass rows either side of it)
                    def bridge(lo_, hi_):
                        has = hi_ >= 0; idx = np.nonzero(has)[0]
                        if len(idx) < 2: return lo_, hi_
                        for i in range(idx[0], idx[-1] + 1):
                            if not has[i]:
                                a_ = idx[idx < i][-1]; b_ = idx[idx > i][0]
                                lo_[i] = min(lo_[a_], lo_[b_]); hi_[i] = max(hi_[a_], hi_[b_])
                        return lo_, hi_
                    rlo, rhi = bridge(rlo, rhi); clo, chi = bridge(clo, chi)
                    ins = (bu >= rlo[bv]) & (bu <= rhi[bv]) & (bv >= clo[bu]) & (bv <= chi[bu])
                    if ins.sum() < 40: continue
                    isbar = ~pane[ty, tx]
                    lines = np.zeros(len(u), bool)   # (in the glass's own plane there are no real bars at this picture's size: about 10 cm a texel on the town hall; the muntins stand in front of it and are not touched)
                    newbar = ins & lines; newpane = ins & ~lines
                    nfix += int((newpane & isbar).sum()); nbar += int(newbar.sum())
                    was = pane[ty, tx] | win[ty, tx]
                    pane[ty[newpane], tx[newpane]] = True; r[ty[newpane], tx[newpane]] = votes[gi].argmax() / 31.0
                    glow_only[ty[newpane & ~was], tx[newpane & ~was]] = True   # (a muntin: lit with the pane, painted wood by day)
            log(n, "windows filled in their own plane: %d texels of cut or blotch made pane" % nfix)
        # Off the model's faces round a pane (the island's edge): pane too. A texel there is never shown itself, but filtering and mips
        # mix it into the pane's edge texels, and as frame it drew dark cracks along every UV seam through a lit window (2026-11-15).
        margin = dilate(pane, 3) & ~onface & ~fire & ~dilate(onface & ~pane, 1)
    else:
        if not old: log(n, "no low model %s: windows joined on the picture only" % fbx)
    # Then out past the pane, so every texel round a window carries that window's number. M_TL_Building switches each window on
    # where Frac(R x 7.13 + ...) passes a hard threshold, so a texel whose R was filtered with the frame's 0 (bilinear filtering, mips) or
    # rounded by the block compression (the mask is DXT1: R has 5 bits, and a block mixing two values interpolates) got another
    # random and lit on its own: orange slivers along the rims of dark windows and dark slivers in lit ones (2026-11-15). With the
    # number constant round each pane and set to one of the 32 values DXT1 stores exactly, the filtering only ever mixes equals.
    r = np.where(pane & onface, r, -1.0)   # (seeded from texels on the model's faces: the rest of the picture is the bake's fill between islands)
    for _ in range(R_SPREAD):
        grow = r < 0
        if not grow.any(): break
        nb = np.max(np.stack([np.roll(r, s, ax) for ax in (0, 1) for s in (-1, 1)]), 0)
        r = np.where(grow & (nb >= 0), nb, r)
    r = np.where(r < 0, 0.5, r)
    r = np.round(np.clip(r, 0, 1) * 31.0) / 31.0
    pane |= margin
    pane &= ~fire
    new_mask = np.zeros_like(mask); new_mask[..., 2] = 1.0; new_mask[..., 0] = r
    new_mask[fire] = mask[fire]
    new_mask[pane] = np.stack([r[pane], np.ones(int(pane.sum())), np.zeros(int(pane.sum()))], -1)
    # (an old model keeps its glow outside the panes: its vertex colour, not this picture, says which faces may glow)
    new_glow = glow.copy() if old else np.where(fire[..., None], glow, 0.0)
    new_glow[win & ~pane] = 0.0   # (bars and frame: never lit)
    new_glow[pane] = GLOW_SRGB
    # the colour picture at its own size: the pane, nearest-up and one texel wider so the glass meets the frame cleanly
    # the bars between panes (pane on both sides along one axis) are painted one dark wood: Meshy painted some gold, which read as
    # a lit window by day; the rest of the window's rim keeps its paint (the frame)
    bw = BAR_W * (2 if old else 1)
    bar = ~pane & ~fire & (b2.max(-1) > 0.02) & ((within(pane, 0, 1, bw) & within(pane, 0, -1, bw)) | (within(pane, 1, 1, bw) & within(pane, 1, -1, bw)))
    bar |= force_bar & ~pane
    up = lambda m: np.repeat(np.repeat(m, k, 0), k, 1)
    pane_b = up(pane & ~glow_only); bar_b = up(bar | glow_only)
    pane_b |= dilate(pane_b, 1) & up(dilate(pane, 1) & win) & ~bar_b
    new_base = base.copy(); new_base[pane_b] = GLASS_SRGB; new_base[bar_b] = BAR_SRGB
    # and the rest of the rim, where Meshy's lit-pane paint (warm, bright) spilled onto the frame, is that wood too: no orange left by day
    rim_b = up(dilate(pane, 3) & ~pane & ~fire) & ~pane_b & ~bar_b
    if rim_b.any():
        Lr, Cr, Hr = lch(base[rim_b])
        warm = (Lr > 45) & (Cr > 25) & (Hr > 45) & (Hr < 95)
        idx = np.nonzero(rim_b); new_base[idx[0][warm], idx[1][warm]] = BAR_SRGB
        log(n, "warm paint on the rim made wood: %d texels" % int(warm.sum()))
    # The old church's tracery (the stone round its rose and lancets) was painted red and orange like stained glass: by day the window
    # read red round dark glass (2026-11-15). Saturated warm paint round its panes becomes the stone the tracery is cut from (the median
    # of the unsaturated paint there).
    if n in STONE_TRACERY:
        ring = up(dilate(pane, STONE_R) & ~pane & ~fire) & ~pane_b
        if ring.any():
            Ls, Cs, Hs = lch(base[ring])
            red = ((Cs > 40) & ((Hs < 55) | (Hs > 330))) | ((Ls > 55) & (Cs > 40) & (Hs < 95)) | bar_b[ring] | np.all(new_base[ring] == np.array(BAR_SRGB, np.float32), -1)   # (and the bars: stone here, not wood)
            grey = (Cs < 12) & (Ls > 25)   # (unsaturated, and not the black fill between islands)
            stone = np.median(base[ring][grey], 0) if grey.sum() > 50 else np.array([0.45, 0.44, 0.42])
            idx = np.nonzero(ring); new_base[idx[0][red], idx[1][red]] = stone
            log(n, "red tracery paint made stone %s: %d texels" % (np.round(stone, 2), int(red.sum())))
    dropped = win & ~pane
    log(n, "window texels %d, pane %d, bars %d, forge %d, holes filled %d, seam margin %d, frame left unlit %d"
        % (int(win.sum()), int(pane.sum()), int(bar.sum()), int(fire.sum()), int(hole.sum()), int(margin.sum()), int(dropped.sum())))
    write(new_base, P("Base")); write(new_glow, P("Glow"))
    if not old: write(new_mask, P("Mask"))
    open(stamp, "w").write(" ".join(str(os.path.getsize(P(k))) for k in kinds))
log("done")
