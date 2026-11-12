# Makes a building's windows glow a warm lamp-lit yellow (2026-11-11, owner: "windows that slightly glow yellow").
# Blender: blender -b -P Tools/building_glow.py -- <in.glb> <out.glb> [options]
#
# How it works: window glass is found in the base colour picture by its colour (and, when the model has one, by being smoother than the
# walls in the metal-roughness picture: Meshy paints glass glossy). Cold glass (blue or grey) is repainted a warm amber, keeping each
# pane's own light and dark; glass that is already painted lamp-lit (pale yellow, amber) is only pulled a little towards that amber.
# An emissive picture is written with the glass in its new colour and everything else black, and stored as the GLB's glTF emissive
# texture, so building_clean.py saves it as T_<Name>_Glow and M_Building lights it (GlowTex x GlowStrength). Glass is told from walls
# of the same colour by size (specks are dropped), by growing only from what was found, and optionally by 3D shape (--plane-r).
#
# Options (colour windows are on the picture's own sRGB values; hue in degrees, sat and val 0..1; LO,HI pairs):
#  Cold glass (repainted amber):
#   --hue LO,HI       glass hue (default 190,228: blue). --no-blue turns cold glass off (for buildings whose glass is already warm).
#   --sat LO,HI       glass saturation (default 0.10,1)
#   --val LO,HI       glass brightness (HSV value, default 0.25,0.75)
#   --rough-max X     only texels smoother than X (metal-roughness G); 1 turns it off (default 0.40)
#   --grow N          grow the glass up to N texels (at 2048) into a looser window of the same colour, taking in the panes' pale
#                     highlights and soft edges, but only where joined to glass already found (default 10; 0 = off)
#   --grow-hue --grow-sat --grow-val --grow-rough-max   that looser window (defaults: hue 15 lower/12 higher, sat 0.03..1, val 0.15..0.97,
#                     rough-max +0.12)
#   --plane-r X       3D grow (off by default; 7 on the bank): texels of a looser grey-or-blue colour (--plane-sat, --plane-val, defaults
#                     0..0.2 and 0.38..0.95) join the glass when they lie on the same flat pane as glass already found (within --plane-tol,
#                     default 2, of its plane, facing the same way) and within X of it; the window frame, wider than X, keeps walls out.
#                     Lengths are per mille of the model's longest side (on a 12 m building 1 = 1.2 cm). --plane-rounds N (default 3).
#  Warm glass (already painted lamp-lit):
#   --lit-hue LO,HI --lit-sat LO,HI --lit-val LO,HI --lit-rough-max X   its colour window (off unless --lit-hue is given;
#                     --lit-rough-max defaults to --rough-max)
#   --lit-warm X      how far that glass is pulled towards the amber below, 0..1 (default 0.35; pale glass moves most, as it is furthest away)
#   --lit-grow N      grow it like --grow, into hue +-8, sat 0.05..1, val from 0.15 lower, rough-max +0.12 (default 4)
#  Fire (emissive only, colour kept): --fire HLO,HHI,SLO,SHI,VLO,VHI and --fire-rough-max X, e.g. a forge's opening.
#  Shared:
#   --feather X       how soft the colour windows' edges are, as a share of each range (default 0.12)
#   --min-area N      drop specks smaller than N texels, counted at 2048 (scaled to the picture's size; default 24)
#   --dark R,G,B      amber for the darkest glass (sRGB, default 0.95,0.62,0.20)
#   --light R,G,B     amber for the lightest glass (default 1.0,0.78,0.35)
#   --centre X        how much the middle of a pane is lightened, 0..1 (default 0.5)
#   --glow X          brightness of the emissive picture, 0..1 (default 1.0)
#   --strength X      glTF emissive strength written to the file (default 1.0; the game uses its own GlowStrength)
#   --old-emissive keep|drop   a model that already has an emissive picture: keep it under ours (default) or drop it
#   --mask-only       write the emissive picture but leave the base colour as it is
#   --debug PREFIX    also write PREFIX_mask.png (full size) and PREFIX_overlay.png (1400 px: the picture dimmed; cold glass yellow,
#                     warm glass magenta, fire red)
# Every picture in the GLB is written as JPEG quality 92 at its own size (4K at most).
import bpy, sys, os
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
src, dst = a[0], a[1]
opt = {}
i = 2
while i < len(a):
    k = a[i][2:]
    if k in ("mask-only", "no-blue"):
        opt[k] = True; i += 1
    else:
        opt[k] = a[i + 1]; i += 2

def tup(k, d): return tuple(float(x) for x in opt[k].split(",")) if k in opt else d
def num(k, d): return float(opt[k]) if k in opt else d

BLUE = not opt.get("no-blue")
HUE = tup("hue", (190.0, 228.0)); SAT = tup("sat", (0.10, 1.0)); VAL = tup("val", (0.25, 0.75))
ROUGH = num("rough-max", 0.40); FEATHER = num("feather", 0.12); MINAREA = num("min-area", 24.0)
DARK = np.array(tup("dark", (0.95, 0.62, 0.20)), np.float32); LIGHT = np.array(tup("light", (1.0, 0.78, 0.35)), np.float32)
CENTRE = num("centre", 0.5); GLOW = num("glow", 1.0); STRENGTH = num("strength", 1.0)
LIT = "lit-hue" in opt; FIRE = "fire" in opt

# ---------- load ----------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
mats = {m for o in meshes for m in o.data.materials if m and m.node_tree}
if len(mats) != 1: print("GLOW warning: %d materials, using the first" % len(mats))
mat = sorted(mats, key=lambda m: m.name)[0]
nt = mat.node_tree
pr = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")

def tex_of(sock):
    stack = [sock]
    while stack:
        s_ = stack.pop()
        for l in s_.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(i for i in n.inputs)
    return None

base_node = tex_of(pr.inputs["Base Color"])
base = base_node.image
mr_node = tex_of(pr.inputs["Roughness"]) or tex_of(pr.inputs["Metallic"])
old_node = tex_of(pr.inputs["Emission Color"])
W, H = base.size
print("GLOW", os.path.basename(src), "base", W, H, "mr", mr_node.image.size[:] if mr_node else None,
      "old emissive", old_node.image.size[:] if old_node else None)

def pixels(img):
    p = np.empty(img.size[0] * img.size[1] * 4, np.float32); img.pixels.foreach_get(p)
    return p.reshape(img.size[1], img.size[0], 4)

def to_base_size(m):   # nearest-neighbour resample to the base picture's size
    if m.shape[:2] == (H, W): return m
    yi = (np.arange(H) * m.shape[0] // H); xi = (np.arange(W) * m.shape[1] // W)
    return m[yi][:, xi]

px = pixels(base)
rgb = px[..., :3].copy()
rough = to_base_size(pixels(mr_node.image)[..., 1]) if mr_node else None

# ---------- colour ----------
def hsv(c):
    mx = c.max(-1); mn = c.min(-1); d = mx - mn
    sat = np.where(mx > 1e-4, d / np.maximum(mx, 1e-4), 0.0)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    dd = np.maximum(d, 1e-5)
    h = np.where(mx == r, ((g - b) / dd) % 6, np.where(mx == g, (b - r) / dd + 2, (r - g) / dd + 4)) * 60.0
    h = np.where(d < 1e-5, -999.0, h)
    return h, sat, mx

def soft_box(x, lo, hi, f):
    w = max((hi - lo) * f, 1e-4)
    return np.clip((x - lo) / w + 0.5, 0, 1) * np.clip((hi - x) / w + 0.5, 0, 1)

def window(hr, sr, vr, rmax):
    if hr[0] <= hr[1]:
        wh = soft_box(h, hr[0], hr[1], FEATHER)
    else:   # wraps round red
        wh = np.maximum(soft_box(h, hr[0], 360.0 + 30, FEATHER), soft_box(h, -30, hr[1], FEATHER))
    w = wh * soft_box(s, sr[0], sr[1], FEATHER) * soft_box(v, vr[0], vr[1], FEATHER)
    if rough is not None and rmax < 1.0:
        w *= np.clip((rmax - rough) / 0.08 + 0.5, 0, 1)
    return w.astype(np.float32)

h, s, v = hsv(rgb)
zero = np.zeros((H, W), np.float32)

# ---------- morphology (no scipy in Blender's Python) ----------
def shift(m, dy, dx, fill):
    o = np.full_like(m, fill)
    ys = slice(max(dy, 0), m.shape[0] + min(dy, 0)); yd = slice(max(-dy, 0), m.shape[0] + min(-dy, 0))
    xs = slice(max(dx, 0), m.shape[1] + min(dx, 0)); xd = slice(max(-dx, 0), m.shape[1] + min(-dx, 0))
    o[yd, xd] = m[ys, xs]
    return o

def dilate(m, r=1):
    for _ in range(r):
        m = m | shift(m, 1, 0, False) | shift(m, -1, 0, False) | shift(m, 0, 1, False) | shift(m, 0, -1, False)
    return m

def erode(m, r=1):
    for _ in range(r):
        m = m & shift(m, 1, 0, False) & shift(m, -1, 0, False) & shift(m, 0, 1, False) & shift(m, 0, -1, False)
    return m

def components(m):
    # labels by min-propagation with pointer jumping
    BIG = np.int64(m.size + 1)
    L = np.where(m, np.arange(m.size, dtype=np.int64).reshape(m.shape), BIG)
    for it in range(400):
        old = L
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            L = np.where(m, np.minimum(L, shift(L, dy, dx, BIG)), BIG)
        f = L.ravel().copy(); sel = np.flatnonzero(m.ravel())
        for _ in range(6): f[sel] = f[f[sel]]
        L = f.reshape(m.shape)
        if np.array_equal(L, old): break
    return L

AREA = MINAREA * (W * H) / (2048.0 * 2048.0)
def keep_big(w, name):
    core = w > 0.5
    if not core.any(): return w
    L = components(core)
    labs, counts = np.unique(L[core], return_counts=True)
    good = labs[counts >= AREA]
    keep = np.isin(L, good) & core
    print("GLOW", name, "patches", len(labs), "kept", len(good), "texels", int(keep.sum()))
    keep = dilate(keep, 2)          # keep the soft edge round each kept patch
    return np.where(keep, w, 0.0).astype(np.float32)

def grow(w, n, gw, name):
    # join texels of the looser window gw to w, up to n texels away, through gw only
    n = int(round(n * W / 2048.0))
    if n <= 0 or not (w > 0.5).any(): return w
    weak = gw > 0.5; reg = w > 0.5
    for _ in range(n):
        reg = reg | (dilate(reg) & weak)
    before = int((w > 0.5).sum())
    w = np.maximum(w, np.where(dilate(reg, 1), gw, 0.0)).astype(np.float32)
    print("GLOW", name, "grew", before, "->", int((w > 0.5).sum()))
    return w

# ---------- cold glass ----------
w_glass = zero
if BLUE:
    w_glass = keep_big(window(HUE, SAT, VAL, ROUGH), "cold glass")
    w_glass = grow(w_glass, num("grow", 10.0), window(tup("grow-hue", (HUE[0] - 15, HUE[1] + 12)), tup("grow-sat", (0.03, 1.0)),
                                                       tup("grow-val", (0.15, 0.97)), num("grow-rough-max", min(1.0, ROUGH + 0.12))), "cold glass")

# ---------- plane grow (3D) ----------
def bake_points():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.samples = 1; sc.cycles.device = "CPU"
    sc.render.bake.margin = 1; sc.render.bake.use_clear = True
    res = []
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    old = [l.from_socket for l in out.inputs["Surface"].links]
    geo = nt.nodes.new("ShaderNodeNewGeometry"); em = nt.nodes.new("ShaderNodeEmission")
    tgt = nt.nodes.new("ShaderNodeTexImage")
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    for sock in ("Position", "Normal"):
        img = bpy.data.images.new("bake_" + sock, W, H, alpha=True, float_buffer=True)
        img.colorspace_settings.name = "Non-Color"
        img.pixels.foreach_set(np.zeros(W * H * 4, np.float32))
        tgt.image = img; nt.nodes.active = tgt
        nt.links.new(geo.outputs[sock], em.inputs["Color"]); nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
        bpy.ops.object.bake(type="EMIT")
        res.append(pixels(img))
        bpy.data.images.remove(img)
    for n in (geo, em, tgt): nt.nodes.remove(n)
    for s_ in old: nt.links.new(s_, out.inputs["Surface"])
    P, N = res
    return P[..., :3], N[..., :3], P[..., 3] > 0.5     # (alpha: texels the bake reached)

PLANE = num("plane-r", 0.0)
if PLANE > 0 and (w_glass > 0.5).any():
    from mathutils.kdtree import KDTree
    co = []
    for o in meshes:
        c_ = np.empty(len(o.data.vertices) * 3, np.float32); o.data.vertices.foreach_get("co", c_)
        c_ = c_.reshape(-1, 3); M = np.array(o.matrix_world)
        co.append(c_ @ M[:3, :3].T + M[:3, 3])
    co = np.concatenate(co)
    L3 = float((co.max(0) - co.min(0)).max())
    R = PLANE * L3 / 1000.0; TOL = num("plane-tol", 2.0) * L3 / 1000.0
    P, N, valid = bake_points()
    pw = np.maximum(window((0.0, 360.0), tup("plane-sat", (0.0, 0.2)), tup("plane-val", (0.38, 0.95)), 1.0),
                    window((HUE[0] - 15, HUE[1] + 12), (0.03, 1.0), tup("plane-val", (0.38, 0.95)), 1.0))
    cand = (pw > 0.5) & valid
    seed = (w_glass > 0.5) & valid
    added = np.zeros_like(seed)
    key = lambda q: np.floor(q / R).astype(np.int64) + 100000
    enc = lambda k: (k[..., 0] * 200003 + k[..., 1]) * 200003 + k[..., 2]
    offs = np.array([(a_, b_, c_) for a_ in (-1, 0, 1) for b_ in (-1, 0, 1) for c_ in (-1, 0, 1)])
    for rnd in range(int(num("plane-rounds", 3))):
        sy, sx = np.nonzero(seed | added)
        sp = P[sy, sx]; sn = N[sy, sx]
        allk = np.unique(enc((key(sp)[:, None, :] + offs[None]).reshape(-1, 3)))   # voxels next to glass
        cy, cx = np.nonzero(cand & ~seed & ~added)
        ok = np.isin(enc(key(P[cy, cx])), allk)
        cy, cx = cy[ok], cx[ok]
        if len(cy) == 0: break
        cp = P[cy, cx]; cn = N[cy, cx]
        kd = KDTree(len(sp))
        for i_, c_ in enumerate(sp): kd.insert(c_, i_)
        kd.balance()
        acc = np.zeros(len(cy), bool)
        for j in range(len(cy)):
            _, idx, dist = kd.find(cp[j])
            if dist > R: continue
            n0 = sn[idx]
            if abs(float(np.dot(cp[j] - sp[idx], n0))) < TOL and float(np.dot(cn[j], n0)) > 0.9:
                acc[j] = True
        added[cy[acc], cx[acc]] = True
        print("GLOW plane round", rnd, "checked", len(cy), "added", int(acc.sum()))
        if not acc.any(): break
    ad = dilate(added, 1)
    w_glass = np.maximum(w_glass, np.where(ad, pw, 0.0)).astype(np.float32)
    print("GLOW plane total added", int(added.sum()))

# ---------- warm glass and fire ----------
w_lit = zero
if LIT:
    LH = tup("lit-hue", (35, 60)); LS = tup("lit-sat", (0.25, 0.8)); LV = tup("lit-val", (0.65, 1.0)); LR = num("lit-rough-max", ROUGH)
    w_lit = keep_big(window(LH, LS, LV, LR), "warm glass")
    w_lit = grow(w_lit, num("lit-grow", 4.0), window((LH[0] - 8, LH[1] + 8), (0.05, 1.0), (max(0.0, LV[0] - 0.15), 1.0), min(1.0, LR + 0.12)), "warm glass")
    w_lit = (w_lit * (1 - w_glass)).astype(np.float32)
w_fire = zero
if FIRE:
    f = tup("fire", (0, 25, 0.6, 1, 0.45, 1))
    w_fire = keep_big(window(f[0:2], f[2:4], f[4:6], num("fire-rough-max", 1.0)), "fire")
    w_fire = (w_fire * (1 - w_glass) * (1 - w_lit)).astype(np.float32)

# ---------- repaint ----------
lum = rgb @ np.array([0.2126, 0.7152, 0.0722], np.float32)
def amber_of(core):
    lo, hi = np.percentile(lum[core], [5, 95])
    t = np.clip((lum - lo) / max(hi - lo, 1e-3), 0, 1)
    if CENTRE > 0:   # lighter in the middle of a pane: steps in from the glass's edge, up to R texels
        R_ = max(2, int(round(6 * W / 2048)))
        d = np.zeros(core.shape, np.float32); m = core.copy()
        for _ in range(R_):
            m = erode(m); d += m
        t = np.clip(t * (1 - CENTRE * 0.5) + CENTRE * 0.5 * (d / R_), 0, 1)
    return DARK[None, None, :] * (1 - t[..., None]) + LIGHT[None, None, :] * t[..., None]

out = rgb.copy()
emis = np.zeros_like(rgb)
for w, k, name in ((w_glass, 1.0, "cold"), (w_lit, num("lit-warm", 0.35), "warm")):
    core = w > 0.5
    if not core.any(): continue
    am = amber_of(core)
    painted = rgb * (1 - k) + am * k
    wk = w[..., None]
    out = out * (1 - wk) + painted * wk
    emis += painted * wk * GLOW
if FIRE:
    emis += rgb * w_fire[..., None] * GLOW
if old_node is not None and opt.get("old-emissive", "keep") == "keep":
    emis = np.maximum(emis, to_base_size(pixels(old_node.image))[..., :3])
emis = np.clip(emis, 0, 1)
if opt.get("mask-only"): out = rgb
print("GLOW texels: cold glass %d, warm glass %d, fire %d (%.3f%% of the picture)" % (
      int((w_glass > 0.5).sum()), int((w_lit > 0.5).sum()), int((w_fire > 0.5).sum()), 100.0 * float(((w_glass + w_lit + w_fire) > 0.5).mean())))

# ---------- debug pictures ----------
def save_rgb(arr, path, w, h_):
    im = bpy.data.images.new(os.path.basename(path), w, h_, alpha=False)
    p = np.ones((h_, w, 4), np.float32); p[..., :3] = arr
    im.pixels.foreach_set(p.ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    bpy.data.images.remove(im)

if "debug" in opt:
    dp = opt["debug"]
    msk = np.maximum(np.maximum(w_glass, w_lit), w_fire)
    save_rgb(np.repeat(msk[..., None], 3, -1), dp + "_mask.png", W, H)
    S = 1400 / max(W, H); nw, nh = int(W * S), int(H * S)
    yi = (np.arange(nh) / S).astype(int); xi = (np.arange(nw) / S).astype(int)
    small = rgb[yi][:, xi] * 0.35
    for w, col in ((w_glass, (1.0, 0.85, 0.1)), (w_lit, (1.0, 0.2, 0.9)), (w_fire, (1.0, 0.1, 0.05))):
        mg = w[yi][:, xi][..., None]; small = small * (1 - mg) + np.array(col) * mg
    save_rgb(small, dp + "_overlay.png", nw, nh)

# ---------- write ----------
if not opt.get("mask-only"):
    px[..., :3] = out
    base.pixels.foreach_set(px.ravel()); base.update(); base.pack()
gimg = bpy.data.images.new(mat.name + "_Glow", W, H, alpha=False)
gp = np.ones((H, W, 4), np.float32); gp[..., :3] = emis
gimg.pixels.foreach_set(gp.ravel()); gimg.update(); gimg.pack()
if old_node is not None:
    for l in list(pr.inputs["Emission Color"].links): nt.links.remove(l)
    nt.nodes.remove(old_node)
gn = nt.nodes.new("ShaderNodeTexImage"); gn.image = gimg; gn.location = (base_node.location.x, base_node.location.y - 900)
for l in base_node.inputs["Vector"].links:      # the base picture's UV map, if it names one
    nt.links.new(l.from_socket, gn.inputs["Vector"])
nt.links.new(gn.outputs["Color"], pr.inputs["Emission Color"])
pr.inputs["Emission Color"].default_value = (1, 1, 1, 1)
pr.inputs["Emission Strength"].default_value = STRENGTH
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_image_format="JPEG", export_jpeg_quality=92)
print("GLOW wrote", dst, round(os.path.getsize(dst) / 1e6, 1), "MB")
