# Flat, hand-painted colour for the town's buildings (2026-11-12, owner: "flat, painted colour ... lighting does the shading").
# Blender, after building_clean.py has written a building's pictures (Tools\BuildBuildings.ps1 runs it between the two halves):
#   blender -b -P Tools/building_flatten.py -- <work folder> [Name ...] [--option value ...]
# <work folder> is Saved/KnightBuild/Buildings: <Name>.fbx (for its UV islands), T_<Name>_Base.png, T_<Name>_Normal.png and,
# if the building has lit windows, T_<Name>_Glow.png. With no names it does every building in the folder.
#
# The pictures are changed in place. The untouched ones are first copied to <work>/Raw/, and a run that finds its own last output
# (T_<Name>_Base.flat remembers it) starts again from Raw/, so it can be run again with other settings without building_clean.py.
# The models in Art/Buildings are never touched. To go back: run BuildBuildings.ps1 with -NoFlatten.
#
# What it does to the colour (in CIE Lab, every number below at a 2048 picture and scaled with the picture's size):
#  1. Broad patches. First a rolling guidance filter melts painted texture finer than --texture-scale texels (straw, wood grain,
#     AI speckle), however strong, and rebuilds only the edges that outlast that blur (stone and tile outlines, material borders),
#     --rolls times. Then an edge-preserving smoothing (the domain transform's recursive filter, a fast bilateral) paints over
#     smudges and blotches into patches of one colour; steps bigger than --edge (Lab units) stay crisp, so wood against plaster,
#     roof tiles and stone blocks keep clean lines. Neither ever smooths across a UV island's edge (islands touch in Meshy's
#     atlas but are far apart on the model).
#  2. A little painted variation: what step 1 took out is put back softened (blurred over --variation-size texels) at --variation
#     (lightness) and --variation-colour, and a trace of the finest grain at --grain. That reads as brush strokes, not noise.
#  3. Lighting does the shading: within each surface (one UV island, one colour group: k-means of the picture into --classes groups)
#     the slow light-and-dark across it (Meshy's baked ambient occlusion and painted highlights) is cut by --shade (its colour
#     only by --shade-colour of that: 0, so only the light and dark go; taking it out of the colour turned stone cold grey); each surface's
#     average is then moved --even (lightness) and --even-colour (colour) of the way to its colour group's average, so every wall,
#     roof and beam sits cleanly on its palette colour. The palette itself (building_palette.py) is not changed.
#  4. --crisp sharpens the remaining lines (mortar, tile edges) a little so they read as painted lines, and --antialias puts back
#     the soft half-way texels along borders that the filters pushed to one side (otherwise a staircase shows up close).
#     Then --lines puts back the dark lines steps 1 and 4 melted (mortar, block and plank gaps: narrow dark dips that run on
#     along a line, so a lone speck stays melted), each as deep as it was; the normal map keeps its grooves there too.
#     Without it walls read soft and pale (visual review 2026-11-12: about 60% of the dark lines had gone).
#  3b. --keep-chroma: the colour (hue and saturation) stays as painted, speckle aside; steps 1-3 flatten the lightness only.
#  5. --keep-palette puts each colour group's average colour back where it was, so the palette never drifts.
#  Lit windows and the forge (wherever T_<Name>_Glow is lit, grown by --glow-guard texels) keep their original colour exactly.
# The normal map: inside each island its fine ripples (wood grain, straw, AI noise) are smoothed over --normal-smooth texels and only
# --normal-detail of them kept; the big forms (stone bulges, tile rows, planks) stay. --normal-strength scales what is left.
#
# The look in short (for anything else made to sit beside the buildings): each surface one clean colour from the palette, with
# soft brush-like variation of a few per cent and no speckle, grit or photo detail; clean dark lines only where real shapes meet
# (mortar, tile edges, planks); no baked shadow or highlight in the lightness (the painted warmth of a block's colour stays),
# M_Building's sun and sky light and its faint warm rim do the shading. Judged on the pilot (OakHillHouse_300k) in Docs/BuildingLook/.
#
# Options (defaults in DEFAULTS below; per-building changes in PER_BUILDING):
#   --texture-scale X    finest painted texture melted, texels (3)       --rolls N      its rebuilding rounds (4)
#   --edge X             Lab step kept as an edge (default 10)           --smooth X     patch size in texels (default 10)
#   --group-soft X       how softly a surface picks its colour group, Lab units (6)
#   --passes N           smoothing passes (default 3)                    --variation X  painted lightness variation kept (0.45)
#   --variation-colour X painted colour variation kept (0.2)             --variation-size X  its softness in texels (2.5)
#   --grain X            finest grain kept (0.08)                        --shade X      baked light and shade removed (0.6)
#   --shade-size X       what counts as slow light across a surface, texels (40)    --shade-colour X  its share on colour (0)
#   --even X             surfaces' lightness moved to their group's (0.35)    --even-colour X  their colour (0.1)
#   --keep-chroma X      how much of the painted colour (a, b) is kept as painted, only speckle melted (0.8): lightness is flattened, colour isn't
#   --classes N          colour groups (10)                              --crisp X      line sharpening (0.2)
#   --antialias X        how much of a border's anti-aliasing is put back (0.7)
#   --lines X            how much of each lost dark line's depth is put back (1)    --line-width X  widest line, texels (2)
#   --keep-palette X     how exactly each colour group's average colour is kept as it was (1 = exactly)
#   --normal-smooth X    texels (3)    --normal-detail X  fine normal ripples kept (0.3)    --normal-strength X  (1.0)
#   --glow-guard X       texels round the glow kept exactly (3)
#   --out DIR            write the results to DIR instead of in place (pilots); Raw/ is not used then
#   --debug PREFIX       also writes PREFIX_<Name>_islands.png and PREFIX_<Name>_classes.png
import bpy, sys, os, json, time, shutil
import numpy as np

DEFAULTS = dict(texture_scale=3.0, rolls=4, group_soft=6.0, edge=10.0, smooth=10.0, passes=3, variation=0.45, variation_colour=0.2, variation_size=2.5, grain=0.08,
                shade=0.6, shade_colour=0.0, shade_size=40.0, even=0.35, even_colour=0.1, classes=10, crisp=0.2, antialias=0.7,
                keep_chroma=0.8, lines=1.0, line_width=2.0, keep_palette=1.0,
                normal_smooth=3.0, normal_detail=0.3, normal_strength=1.0, glow_guard=3.0)
PER_BUILDING = {
    # GeneralStore_300k and PotionStall_300k keep their own colours (goods, bottles): gentler, so the goods stay readable and the
    # shelves at the back don't go dark and grey (they were painted lighter than the stall's other wood)
    "GeneralStore_300k": dict(even=0.15, variation_colour=0.3),
    "PotionStall_300k": dict(even=0.15, variation_colour=0.3),
}
KEYS = {k.replace("_", "-"): k for k in DEFAULTS}

a = sys.argv[sys.argv.index("--") + 1:]
work = a[0]
names, cli, out_dir, debug = [], {}, None, None
i = 1
while i < len(a):
    if a[i].startswith("--"):
        k = a[i][2:]
        if k == "out": out_dir = a[i + 1]
        elif k == "debug": debug = a[i + 1]
        elif k in KEYS: cli[KEYS[k]] = float(a[i + 1])
        else: raise SystemExit("FLAT unknown option --" + k)
        i += 2
    else:
        names.append(a[i]); i += 1
# MoveLab's three test houses (Standard, Reduced, Detailed) are a side-by-side comparison of triangle counts: left as Meshy made them
SKIP = {"OakHillHouse_Standard", "OakHillHouse_Detailed"}
if not names:
    names = sorted(n[:-4] for n in os.listdir(work) if n.endswith(".fbx") and n[:-4] not in SKIP and os.path.exists(os.path.join(work, "T_%s_Base.png" % n[:-4])))

# ---------- colour ----------
M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], dtype=np.float32)
WHITE = np.array([0.9505, 1.0, 1.089], dtype=np.float32)
MI = np.linalg.inv(M).astype(np.float32)

def to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((np.maximum(rgb, 0) + 0.055) / 1.055) ** 2.4)
    xyz = c @ M.T / WHITE
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1).astype(np.float32)

def to_rgb(lab):
    fy = (lab[..., 0] + 16) / 116; fx = fy + lab[..., 1] / 500; fz = fy - lab[..., 2] / 200
    f = np.stack([fx, fy, fz], -1)
    xyz = np.where(f > 0.206893, f ** 3, (f - 16 / 116) / 7.787) * WHITE
    c = np.clip(xyz @ MI.T, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055).astype(np.float32)

# ---------- pictures ----------
def load(path):
    im = bpy.data.images.load(path)
    W, H = im.size
    p = np.empty(W * H * 4, np.float32); im.pixels.foreach_get(p)
    bpy.data.images.remove(im)
    return p.reshape(H, W, 4)

def save(arr, path):
    H, W = arr.shape[:2]
    im = bpy.data.images.new(os.path.basename(path), W, H, alpha=False)
    p = np.ones((H, W, 4), np.float32); p[..., :3] = np.clip(arr[..., :3], 0, 1)
    im.pixels.foreach_set(p.ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    bpy.data.images.remove(im)

# ---------- edge-aware filtering: the domain transform's recursive filter (Gastal and Oliveira 2011) ----------
# F is (H, W, C). dx[y, x] is the step from texel x-1 to x along a row, dy[y, x] from row y-1 to y (the first column / row unused);
# STOP marks a step never crossed. The filter is a running weighted average forwards and back along every row, then every column.
STOP = 1e4

def _sweep(F, V):
    # F: (N, M, C), V: (N, M); runs along axis 0 both ways (contiguous rows keep the Python loop fast)
    for j in range(1, F.shape[0]):
        F[j] += V[j][:, None] * (F[j - 1] - F[j])
    for j in range(F.shape[0] - 2, -1, -1):
        F[j] += V[j + 1][:, None] * (F[j + 1] - F[j])

def rf(F, dx, dy, sigma_s, passes):
    F = F.astype(np.float32).copy()
    dxT = np.ascontiguousarray(dx.T); passes = int(passes)
    for k in range(passes):
        sig = sigma_s * np.sqrt(3.0) * 2 ** (passes - k - 1) / np.sqrt(4 ** passes - 1)
        A = np.float32(np.exp(-np.sqrt(2.0) / sig))
        G = np.ascontiguousarray(F.transpose(1, 0, 2)); _sweep(G, A ** dxT); F = np.ascontiguousarray(G.transpose(1, 0, 2))
        _sweep(F, A ** dy)
    return F

def steps(F, scale, same):
    # the domain transform's step lengths: 1 + scale x (sum of the channels' differences); STOP where `same` is False
    dx = np.ones(F.shape[:2], np.float32); dy = np.ones(F.shape[:2], np.float32)
    if scale:
        dx[:, 1:] += scale * np.abs(F[:, 1:] - F[:, :-1]).sum(-1)
        dy[1:] += scale * np.abs(F[1:] - F[:-1]).sum(-1)
    sx, sy = same
    dx[:, 1:][~sx] = STOP; dy[1:][~sy] = STOP
    return dx, dy

def same_of(lbl):
    return lbl[:, 1:] == lbl[:, :-1], lbl[1:] == lbl[:-1]

def grow(mask, n):
    m = mask.copy()
    for _ in range(int(round(n))):
        g = m.copy()
        g[1:] |= m[:-1]; g[:-1] |= m[1:]; g[:, 1:] |= m[:, :-1]; g[:, :-1] |= m[:, 1:]
        m = g
    return m

def box3(F):
    P = np.pad(F, ((1, 1), (1, 1), (0, 0)), mode="edge")
    return sum(P[y:y + F.shape[0], x:x + F.shape[1]] for y in range(3) for x in range(3)) / 9.0

def tophat(L, r):
    # black top-hat: how far each texel sits below the closing (grow the light by r, then shrink it back), in L units
    def spread(A, f):
        for _ in range(r):
            B = A.copy()
            B[1:] = f(B[1:], A[:-1]); B[:-1] = f(B[:-1], A[1:]); B[:, 1:] = f(B[:, 1:], A[:, :-1]); B[:, :-1] = f(B[:, :-1], A[:, 1:])
            A = B
        return A
    return spread(spread(L, np.maximum), np.minimum) - L

def lines_of(L, r, sc):
    # dark dips that run on along a line: the top-hat averaged over 5 texels (scaled) along each of four directions, the
    # strongest kept, and never more than the texel's own dip (a lone speck averages away to a fifth)
    T = tophat(L, r); s = max(1, int(round(sc)))
    best = np.zeros_like(T)
    for dy, dx in ((0, 1), (1, 0), (1, 1), (1, -1)):
        best = np.maximum(best, sum(np.roll(T, (k * s * dy, k * s * dx), (0, 1)) for k in range(-2, 3)) / 5.0)
    return np.minimum(T, best)

# ---------- UV islands ----------
def islands(fbx, W, H, sc):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=fbx)
    objs = [o for o in bpy.data.objects if o.type == "MESH"]
    tri_uv = []
    for o in objs:
        me = o.data; me.calc_loop_triangles()
        uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
        lt = np.empty(len(me.loop_triangles) * 3, np.int64); me.loop_triangles.foreach_get("loops", lt)
        tri_uv.append(uv[lt.reshape(-1, 3)])
    tuv = np.concatenate(tri_uv)                       # (T, 3, 2)
    T = len(tuv)
    # islands: triangles joined through shared UV corners (labels spread to the smallest triangle number, with pointer jumping)
    key = np.round(tuv.reshape(-1, 2) * np.array([W * 8, H * 8])).astype(np.int64)
    _, kid = np.unique(key[:, 0] * (H * 64) + key[:, 1], return_inverse=True); kid = kid.reshape(T, 3)
    lab = np.arange(T, dtype=np.int64)
    while True:
        kl = np.full(kid.max() + 1, T, np.int64)
        np.minimum.at(kl, kid.ravel(), np.repeat(lab, 3))
        new = np.minimum(lab, kl[kid].min(1)); new = new[new]
        if np.array_equal(new, lab): break
        lab = new
    _, isl = np.unique(lab, return_inverse=True)
    # draw them into the picture (texel centres inside the triangle), small triangles in batches
    ids = np.full((H, W), -1, np.int32)
    P = tuv * np.array([W, H], np.float32)             # pixel units; row = v x H (Blender's pictures run bottom-up, like UV v)
    x0 = np.ceil(P[..., 0].min(1) - 0.5).astype(np.int64); x1 = np.floor(P[..., 0].max(1) - 0.5).astype(np.int64)
    y0 = np.ceil(P[..., 1].min(1) - 0.5).astype(np.int64); y1 = np.floor(P[..., 1].max(1) - 0.5).astype(np.int64)
    size = np.maximum(x1 - x0 + 1, y1 - y0 + 1)
    ok = size > 0
    lo = 0
    for s in [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096, 8192]:
        sel = np.nonzero(ok & (size > lo) & (size <= s))[0]; lo = s
        step = max(1, 4_000_000 // (s * s))
        for c in range(0, len(sel), step):
            t = sel[c:c + step]
            gx = x0[t][:, None] + np.arange(s)[None]; gy = y0[t][:, None] + np.arange(s)[None]
            px = np.broadcast_to(gx[:, None, :] + 0.5, (len(t), s, s)); py = np.broadcast_to(gy[:, :, None] + 0.5, (len(t), s, s))
            A, B, C = P[t, 0], P[t, 1], P[t, 2]
            def edge(p, q):
                return (q[:, 0, None, None] - p[:, 0, None, None]) * (py - p[:, 1, None, None]) - (q[:, 1, None, None] - p[:, 1, None, None]) * (px - p[:, 0, None, None])
            e0, e1, e2 = edge(A, B), edge(B, C), edge(C, A)
            inside = ((e0 >= -1e-6) & (e1 >= -1e-6) & (e2 >= -1e-6)) | ((e0 <= 1e-6) & (e1 <= 1e-6) & (e2 <= 1e-6))
            inside &= (gx[:, None, :] >= 0) & (gx[:, None, :] < W) & (gy[:, :, None] >= 0) & (gy[:, :, None] < H)
            tt, yy, xx = np.nonzero(inside)
            ids[gy[tt, yy], gx[tt, xx]] = isl[t[tt]]
    # texels no triangle centre fell in (slivers, gutters) take a neighbour's island
    for _ in range(int(12 * sc)):
        miss = ids < 0
        if not miss.any(): break
        n = ids.copy()
        for sh in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            r = np.roll(ids, sh, (0, 1)); take = (n < 0) & (r >= 0); n[take] = r[take]
        ids = n
    print("FLAT islands %d, triangles %d, texels outside islands %.2f%%" % (isl.max() + 1, T, 100.0 * (ids < 0).mean()))
    return ids

# ---------- colour groups ----------
def kmeans(X, k, iters=20, seed=1):
    rng = np.random.default_rng(seed)
    S = X[rng.choice(len(X), min(len(X), 200_000), replace=False)]
    C = S[rng.choice(len(S), k, replace=False)].copy()
    for _ in range(iters):
        l = ((S[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
        for j in range(k):
            if (l == j).any(): C[j] = S[l == j].mean(0)
    return C

def assign(X, C):
    out = np.empty(len(X), np.int32)
    for c in range(0, len(X), 1 << 20):
        out[c:c + (1 << 20)] = ((X[c:c + (1 << 20), None, :] - C[None]) ** 2).sum(2).argmin(1)
    return out

# ---------- one building ----------
def flatten(name):
    t0 = time.time()
    o = dict(DEFAULTS); o.update(PER_BUILDING.get(name, {})); o.update(cli)
    paths = {k: os.path.join(work, "T_%s_%s.png" % (name, k)) for k in ("Base", "Normal", "Glow")}
    src = dict(paths)
    if out_dir is None:
        raw = os.path.join(work, "Raw"); os.makedirs(raw, exist_ok=True)
        mark = os.path.join(work, "T_%s_Base.flat" % name)
        st = os.stat(paths["Base"])
        done = os.path.exists(mark) and json.load(open(mark)).get("stamp") == [st.st_size, st.st_mtime_ns]
        for k in ("Base", "Normal"):
            rp = os.path.join(raw, os.path.basename(paths[k]))
            if not os.path.exists(paths[k]): continue
            if not done: shutil.copy2(paths[k], rp)       # a fresh picture from building_clean.py: keep it as the raw copy
            src[k] = rp
        dst = paths
    else:
        os.makedirs(out_dir, exist_ok=True)
        dst = {k: os.path.join(out_dir, os.path.basename(p)) for k, p in paths.items()}
    base = load(src["Base"]); H, W = base.shape[:2]; sc = W / 2048.0
    ids = islands(os.path.join(work, name + ".fbx"), W, H, sc)
    glow = load(paths["Glow"])[..., :3].max(-1) > 0.02 if os.path.exists(paths["Glow"]) else np.zeros((H, W), bool)
    if glow.shape != (H, W):   # (a glow picture of another size, the well's: picked texel by texel)
        glow = glow[(np.arange(H) * glow.shape[0] // H)[:, None], (np.arange(W) * glow.shape[1] // W)[None]]
    keep = grow(glow, o["glow_guard"] * sc)
    lab = to_lab(base[..., :3])
    same = same_of(ids)

    # 1a. melt the finest painted texture (straw, grain, AI speckle) whatever its contrast: a rolling guidance filter (Zhang et al.
    #     2014) starts from a blur and then rebuilds only the edges that survive it (block and tile outlines, material borders)
    ts = o["texture_scale"] * sc
    dxs, dys = steps(lab, 0, same)
    G = rf(lab, dxs, dys, ts, 2)
    for _ in range(int(o["rolls"])):
        dx, dy = steps(G, ts / o["edge"], same)
        G = rf(lab, dx, dy, ts, 2)
    # 1b. broad patches: smudges and soft blotches smaller than --edge are painted over
    dx, dy = steps(G, o["smooth"] * sc / o["edge"], same)
    P = rf(G, dx, dy, o["smooth"] * sc, o["passes"])
    # 2. a little painted variation back, softened, and a trace of the grain
    R = lab - P
    V = rf(R, dxs, dys, o["variation_size"] * sc, 2)
    out = P + V * np.array([o["variation"], o["variation_colour"], o["variation_colour"]], np.float32) + (R - V) * o["grain"]

    # 3. colour groups (on the smoothed picture: lightness counts a third, so shade and light of one material stay one group)
    X = (P * np.array([0.35, 1, 1], np.float32)).reshape(-1, 3)
    C = kmeans(X[~keep.ravel()] if (~keep).any() else X, int(o["classes"]))
    cls = assign(X, C).reshape(H, W)
    # tidy the groups: a texel whose four neighbours mostly agree on another group takes it (twice)
    for _ in range(2):
        nb = np.stack([np.roll(cls, s, (0, 1)) for s in ((1, 0), (-1, 0), (0, 1), (0, -1))])
        agree = (nb == nb[0]).sum(0) >= 3
        cls = np.where(agree & (nb[0] != cls), nb[0], cls)
    region = ids.astype(np.int64) * 64 + cls
    rsame = same_of(region)
    # slow light and dark across each surface
    dxr, dyr = steps(lab, 0, rsame)
    Bk = rf(out, dxr, dyr, o["shade_size"] * sc, 2)
    _, rid = np.unique(region.ravel(), return_inverse=True)
    n = np.bincount(rid).astype(np.float64)
    flat = out.reshape(-1, 3)
    RM = np.stack([np.bincount(rid, flat[:, c]) / n for c in range(3)], 1)       # each surface's average
    cls_r = np.zeros(len(n), np.int64); cls_r[rid] = cls.ravel()
    K = int(o["classes"])
    CM = np.stack([np.bincount(cls_r, RM[:, c] * n, minlength=K) / np.maximum(np.bincount(cls_r, n, minlength=K), 1)
                   for c in range(3)], 1)                                        # each group's average
    # each surface heads for a soft mix of the groups' averages, weighted by how close its own average is to each: a surface
    # halfway between two groups (a wall's lit and shaded plaster) stays put instead of being pushed to one side
    scl = np.array([0.35, 1, 1]); d2 = ((((RM[:, None, :] - CM[None]) * scl) ** 2).sum(2))
    wk = np.exp(-(d2 - d2.min(1, keepdims=True)) / (2 * o["group_soft"] ** 2)); wk /= wk.sum(1, keepdims=True)
    TG = wk @ CM
    rm = RM[rid].reshape(H, W, 3).astype(np.float32); cm = TG[rid].reshape(H, W, 3).astype(np.float32)
    out = out - o["shade"] * np.array([1, o["shade_colour"], o["shade_colour"]], np.float32) * (Bk - rm) \
              - np.array([o["even"], o["even_colour"], o["even_colour"]], np.float32) * (rm - cm)

    # 3b. the colour stays as painted: only the lightness is flattened into broad patches. The colour (a, b) is taken back
    #     --keep-chroma of the way to step 1a's (fine speckle melted, but a stone's warm top and cool foot kept), with the
    #     surface's own colour shift from step 3 (--even-colour) on top. Without it stone read cooler and paler than before
    #     (visual review 2026-11-12): the painted warm light on each block had gone, though every group's average was kept.
    kc = o["keep_chroma"]
    out[..., 1:] = out[..., 1:] * (1 - kc) + (G[..., 1:] - o["even_colour"] * (rm - cm)[..., 1:]) * kc

    # 4. crisper painted lines (lightness only), then the edges' anti-aliasing back: edge-preserving filters push the half-way
    #    texels along a border to one side, which shows as a staircase up close (the door's arch against the plaster)
    L = out[..., :1]
    out[..., 0] = np.clip((L + o["crisp"] * (L - box3(L)))[..., 0], 0, 100)
    gx = np.zeros((H, W), np.float32); gy = np.zeros((H, W), np.float32)
    gx[:, 1:] = np.abs(P[:, 1:] - P[:, :-1]).sum(-1); gy[1:] = np.abs(P[1:] - P[:-1]).sum(-1)
    g = np.maximum(np.maximum(gx, np.roll(gx, -1, 1)), np.maximum(gy, np.roll(gy, -1, 0)))
    em = (np.clip((g - o["edge"]) / o["edge"], 0, 1) * o["antialias"])[..., None]
    out = out * (1 - em) + box3(out) * em
    # 4b. the dark lines back: steps 1 and 4 melt thin dark lines (mortar, block and plank gaps) with the speckle, and walls then
    #     read soft and pale (visual review, 2026-11-12). A line is a narrow dark dip (black top-hat over --line-width texels) that
    #     runs on along some direction; a speckle does not, so it stays melted. Each line texel gets back the depth it had.
    lw = max(1, int(round(o["line_width"] * sc)))
    lines_lab = lines_of(lab[..., 0], lw, sc)
    # only real lines: a dip of 0.3 x --edge (3 Lab units) or less is painted texture and stays melted; from 0.8 x --edge it
    # is a line and comes back whole (the church's rough stucco is all shallow dips: restoring those kept its noise)
    real = np.clip((lines_lab - o["edge"] * 0.3) / (o["edge"] * 0.5), 0, 1)
    lost = np.maximum(0, lines_lab - tophat(out[..., 0], lw)) * real
    lost[keep] = 0
    out[..., 0] -= o["lines"] * lost
    line_mask = grow(lines_lab > o["edge"], max(1, round(sc)))     # the normal map keeps its grooves only along strong lines
    print("FLAT %s lines: %.1f%% of texels, %.1f%% put back deeper than 2" % (name, 100.0 * line_mask.mean(), 100.0 * (lost > 2).mean()))
    # 5. the palette stays where it was: each colour group's average colour is put back exactly where Meshy's (and
    #    building_palette.py's) picture had it. Taking the baked shade out lifts the average, so without this wood came out
    #    paler and greyer (the market stall's counter, 2026-11-12).
    fl, fo = lab.reshape(-1, 3), out.reshape(-1, 3); cf = cls.ravel(); kf = ~keep.ravel()
    cnt = np.maximum(np.bincount(cf[kf], minlength=K), 1)
    shift = np.stack([(np.bincount(cf[kf], fl[kf, c], minlength=K) - np.bincount(cf[kf], fo[kf, c], minlength=K)) / cnt for c in range(3)], 1)
    out = out + o["keep_palette"] * shift[cls].astype(np.float32)
    rgb = to_rgb(out)
    k = keep[..., None].astype(np.float32)
    rgb = rgb * (1 - k) + base[..., :3] * k
    save(rgb, dst["Base"])
    dl = out[..., 0] - lab[..., 0]
    print("FLAT %s base %dx%d: groups %d, surfaces %d, lightness change mean %.1f, 95%% %.1f; glow kept %.2f%%" % (
        name, W, H, int(o["classes"]), len(n), float(np.abs(dl).mean()), float(np.percentile(np.abs(dl), 95)), 100.0 * keep.mean()))

    # the normal map
    if os.path.exists(src["Normal"]):
        nm = load(src["Normal"])
        if nm.shape[:2] != (H, W):
            print("FLAT %s normal map is %s, the base %s: normal map left alone" % (name, nm.shape[:2], (H, W)))
        else:
            v = nm[..., :2] * 2 - 1
            # never across islands or the colour edges kept in step 1 (plank edges, tile lines)
            dxn, dyn = steps(P, o["normal_smooth"] * sc / (o["edge"] * 3), same)
            lo = rf(v, dxn, dyn, o["normal_smooth"] * sc, 2)
            v = lo + o["normal_detail"] * (v - lo) + line_mask[..., None] * (1 - o["normal_detail"]) * (v - lo)   # grooves kept
            v = v * o["normal_strength"]
            l2 = (v ** 2).sum(-1)
            v = np.where((l2 > 0.98)[..., None], v * np.sqrt(0.98 / np.maximum(l2, 1e-6))[..., None], v)
            z = np.sqrt(np.clip(1 - (v ** 2).sum(-1), 0, 1))
            res = np.concatenate([v * 0.5 + 0.5, z[..., None] * 0.5 + 0.5], -1)
            save(res, dst["Normal"])
            print("FLAT %s normal: ripple kept %.2f" % (name, o["normal_detail"]))
    if out_dir is None:
        st = os.stat(dst["Base"])
        json.dump({"stamp": [st.st_size, st.st_mtime_ns], "settings": o}, open(os.path.join(work, "T_%s_Base.flat" % name), "w"))
    if debug:
        rng = np.random.default_rng(3)
        pal = rng.random((ids.max() + 2, 3)).astype(np.float32)
        save(pal[ids + 1], "%s_%s_islands.png" % (debug, name))
        save(to_rgb(C / np.array([0.35, 1, 1], np.float32))[cls], "%s_%s_classes.png" % (debug, name))
    print("FLAT %s done in %.0f s" % (name, time.time() - t0))

for nm_ in names:
    flatten(nm_)
print("FLAT all done")
