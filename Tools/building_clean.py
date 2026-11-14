# Turns Meshy's buildings in Art/Buildings into what the game shows (2026-11-06). Run by Tools\BuildBuildings.ps1 in Blender.
# Each model is sized by its long side (along the model's X), centred on its footprint, and sunk into the ground.
# Results: Saved/KnightBuild/Buildings/<Name>.fbx, T_<Name>_Base/Normal/MR.png, and a BUILD line with the walls' rectangle (the
# grass and the stones at the walls use it: OakHill.h WX0..WY1).
# A model with lit windows (an emissive picture, from Tools/building_glow.py) also gives T_<Name>_Glow.png; T_NoGlow.png (black) is for the rest.
#
# Since 2026-11-13 (owner's feedback on the town) it also:
#  - welds the model (a glTF comes split along every texture seam);
#  - takes off the lanterns Meshy built into the walls (LANTERNS: boxes round each, in this script's cm; the hole where one hung is
#    filled), so the separate Meshy lanterns can go up instead (owner: "take off all the lanterns from the houses");
#  - smooths the shading of the white plaster (PLASTER_*): Meshy's plaster panels are crumpled, which showed as dark creases and spikes
#    in the sun. Their normals are evened out across each panel (the shape is kept) and the normal map is flattened there;
#  - marks every lit window in the vertex colour so M_Building can switch each on and off on its own (see WINDOWS below);
#  - repaints the glow picture a deep warm amber, even across each pane (the yellow was lemony and mottled);
#  - writes <Name>_Col.fbx, a reduced copy of the model (COL_TRIS) that is its collision in the game (building_import.py makes it
#    SM_<Name>_Col, complex as simple; OakHillTown.cpp puts it on every machine): the knight walks up the steps, onto porches and
#    under awnings, and the walls stop him at the stone.
#
# BUILDINGS: file, length: the long side in cm, sink: how far the lowest point goes into the ground in cm (Meshy's porch posts reach below the stone base).
import bpy, bmesh, math, mathutils, os, sys
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

BUILDINGS = {
    "OakHillHouse_Standard": dict(file="OakHillHouse_Standard.glb", length=1200.0, sink=20.0),
    "OakHillHouse_Detailed": dict(file="OakHillHouse_Detailed.glb", length=1200.0, sink=20.0),
    "OakHillHouse_300k":     dict(file="OakHillHouse_300k.glb",     length=1200.0, sink=20.0),
    # Oak Hill's town (2026-11-11): lengths chosen so each storey matches the house's (shops about 12.5-14 m tall to the chimney,
    # the town hall's clock 18 m, the church's spire 24 m). The game reads the same footprints from JeebOakHill::Models() (OakHill.h).
    "OakHillCornerHouse_300k": dict(file="OakHillCornerHouse_300k.glb", length=900.0,  sink=20.0),   # (drawn 1.2x in the game, like the house)
    "OakHillSmith_300k":       dict(file="OakHillSmith_300k.glb",       length=1500.0, sink=20.0),
    "OakHillBank_300k":        dict(file="OakHillBank_300k.glb",        length=1340.0, sink=20.0),
    "OakHillStore_300k":       dict(file="OakHillStore_300k.glb",       length=1250.0, sink=20.0),
    "OakHillPotionShop_300k":  dict(file="OakHillPotionShop_300k.glb",  length=1200.0, sink=20.0),
    "OakHillTownHall_300k":    dict(file="OakHillTownHall_300k.glb",    length=2100.0, sink=20.0),
    "OakHillChurch_300k":      dict(file="OakHillChurch_300k.glb",      length=1330.0, sink=25.0),
    "OakHillWell_150k":        dict(file="OakHillWell_150k.glb",        length=340.0,  sink=10.0),
    # The general store (owner chose candidate B, 2026-11-12; Meshy task 01a1d25f-9e40-732b-8d8b-028ff97f9972): a timber trading post with an
    # open front (its -Y in Blender), 290k triangles as it came, its own colours kept (the palette would flatten its goods)
    "GeneralStore_300k":       dict(file="GeneralStore_B.glb",          length=1100.0, sink=15.0),
    # Two more market stalls (owner, 2026-11-12). The bank: Bank_A.glb repainted to the town (building_palette.py "bankstall") as
    # BankStall_A_town.glb, open front +Y in Blender. The potion shop: PotionShop_A.glb as it came (its own colours), stretched
    # deeper (ys) and lower (zs) because Meshy made it shallow and tall
    "BankStall_300k":          dict(file="BankStall_A_town.glb",        length=1100.0, sink=15.0),
    "PotionStall_300k":        dict(file="PotionShop_A.glb",            length=1100.0, sink=15.0, ys=1.5, zs=0.75),
    # A third house design (owner, 2026-11-13): like the library's "Storybook cottage v2" by kaesar3d (Docs/OakHillPlan.md), but remade
    # without its lanterns, pots, barrel and bench (2026-11-14): two views of the original rendered in Blender, the props painted out by
    # Meshy image-to-image, then Meshy multi-image-to-3D (303k triangles, door to -Y). Glass by building_glow.py (--mask-only --rough-max 1
    # --hue 195,232 --sat 0.45,1 --val 0.3,0.95 --grow 6), repainted by building_palette.py "cottage". cm: the same 12.4 m height as before.
    "StorybookCottage_300k":   dict(file="StorybookCottage_300k.glb",   cm=667.0, sink=15.0),
}
names = a[2:] or list(BUILDINGS)
TEST_HOUSES = {"OakHillHouse_Standard", "OakHillHouse_Detailed"}   # MoveLab's side-by-side of triangle counts: left as Meshy made them

# The lanterns built into the models (2026-11-13): boxes (x0, x1, y0, y1, z0, z1) in this script's cm, Blender's frame (Unreal's Y is
# this -Y). Every face whose middle is inside a box goes; the hole it leaves where the bracket met a post or wall is filled. Measured on
# straight orthographic renders and the glow of each lantern's flame; the stalls' goods (a lantern on a shelf) are wares, not lamps.
LANTERNS = {
    "OakHillHouse_300k":  [(-110.0, -68.0, -452.0, -409.5, 146.0, 230.0), (160.0, 204.0, -452.0, -409.5, 146.0, 230.0)],   # the porch posts
    "OakHillStore_300k":  [(238.0, 292.0, -394.0, -322.0, 183.0, 300.0)],                                                  # by the lean-to
    "OakHillChurch_300k": [(-212.0, -128.0, -695.0, -621.0, 182.0, 312.0)],                                                # left of the door
}
LANTERN_SCRAP = 1500   # a loose piece near a lantern with fewer faces than this is what is left of its bracket
# Where a cut-off pot leaves a flat patch on a wall top, the picture it takes: that of the kept face nearest the closest of these points
# (this script's cm; within 1.5 m of the patch), the wall-top stone, instead of what is left of the pot round the hole
PATCH_AT = {}

# The white plaster: what counts as plaster in the colour picture (CIE Lab of the painted cream: lightness and how colourful)
PLASTER_L, PLASTER_CHROMA = 72.0, 26.0
PLASTER_ROUNDS = 120        # how far its normals are evened out (rounds of averaging with the neighbours: about 40-60 cm on a 300k model)
PLASTER_CORNER = 0.6        # neighbours whose faces turn more than this apart (cosine, about 53 degrees) are a corner and stay apart
PLASTER_NORMAL_FLAT = 0.9   # how much of the normal map's ripple goes on plaster

# Lit windows (M_Building, building_import.py). Each window's glass is found on the model (faces whose glow lights up, joined in
# WINDOW_CELL cm cells, so the panes of one window are one window), then marked in the vertex colour:
#   R: a random number per window: when, in its house's turn, it lights (and whether it stays dark in that house)
#   G: 1 for a window, 0 for anything else
#   B: 1 = never glows (all faces that aren't glass or fire, and specks of glow on a wall), 0 = may glow
# The forge's fire (glow redder than HUE_FIRE) is G 0, B 0: it glows always, by FireLevel.
WINDOW_CELL = 18.0
SPECK_CM = 14.0             # a patch of glow no bigger than this either way is paint on a wall, not a window
HUE_FIRE = 30.0             # degrees: the windows' amber is 35-42, the forge's fire 22-24
# The glow picture's new colour (sRGB): a deep warm amber, from the edge of a pane to its middle; only the pane's own light and dark
# (blurred over GLOW_BLUR texels) are kept, at GLOW_VARY
GLOW_EDGE, GLOW_MID = np.array([1.0, 0.46, 0.13], np.float32), np.array([1.0, 0.60, 0.22], np.float32)
GLOW_BLUR, GLOW_VARY = 4, 0.35

COL_TRIS = {"OakHillWell_150k": 8000}   # collision triangles per design (default COL_DEFAULT)
COL_DEFAULT = 20000

def save_pic(img, path):
    im = img.copy()                 # (a packed picture only saves through a copy)
    im.scale(img.size[0], img.size[1])
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()

def pixels(img):
    W, H = img.size
    p = np.empty(W * H * 4, dtype=np.float32); img.pixels.foreach_get(p)
    return p.reshape(H, W, 4)

def write_png(arr, path):
    H, W = arr.shape[:2]
    im = bpy.data.images.new(os.path.basename(path), W, H, alpha=False)
    im.pixels.foreach_set(arr.astype(np.float32).ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    bpy.data.images.remove(im)

GLOW_KNEE = 0.15   # the GLB stores the glow as JPEG, whose blocks smear a faint amber round each window, sometimes onto a neighbouring
                   # part of the picture (a roof): anything dimmer than this goes black and the rest is stretched back to full

def hue_of(c):
    mx = c.max(-1); mn = c.min(-1); d = np.maximum(mx - mn, 1e-5)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60.0
    return np.where(mx - mn > 1e-4, h, 0.0)

def box_blur(x, r):
    if r <= 0: return x
    k = 2 * r + 1
    for ax in (0, 1):
        c = np.cumsum(np.pad(x, [(r + 1, r) if i == ax else (0, 0) for i in range(x.ndim)], mode="edge"), axis=ax)
        x = (np.take(c, range(k, c.shape[ax]), axis=ax) - np.take(c, range(0, c.shape[ax] - k), axis=ax)) / k
    return x

def save_glow(img, path):
    p = pixels(img).copy()
    m = p[..., :3].max(-1)
    k = np.clip((m - GLOW_KNEE) / (1.0 - GLOW_KNEE), 0, 1) / np.maximum(m, 1e-4)
    p[..., :3] *= k[..., None]
    lit = p[..., :3].max(-1) > 0.02
    fire = lit & (hue_of(p[..., :3]) < HUE_FIRE)
    win = lit & ~fire
    # the windows: one warm amber, a little lighter in each pane's middle, the pane's own soft light and dark kept faintly
    lum = p[..., :3].max(-1)
    soft = box_blur(np.where(win, lum, 0.0), GLOW_BLUR) / np.maximum(box_blur(win.astype(np.float32), GLOW_BLUR), 1e-3)
    inner = box_blur(win.astype(np.float32), GLOW_BLUR * 2)                         # 1 in a pane's middle, lower at its edge
    t = np.clip((inner - 0.5) * 2.0, 0, 1)[..., None]
    amber = GLOW_EDGE * (1 - t) + GLOW_MID * t
    bright = (1.0 - GLOW_VARY) + GLOW_VARY * np.clip(soft / max(float(np.percentile(soft[win], 90)) if win.any() else 1.0, 1e-3), 0, 1)
    edge = np.clip(lum / 0.6, 0, 1)                                                 # (keep the soft rim of each pane soft)
    rgb = np.where(win[..., None], amber * (bright * edge)[..., None], p[..., :3])
    out_ = np.concatenate([rgb, np.ones_like(rgb[..., :1])], -1)
    write_png(out_, path)
    print("BUILD glow", os.path.basename(path), "window texels", int(win.sum()), "fire texels", int(fire.sum()))
    return p[..., :3]          # (the knee'd glow, for finding the windows on the model)

def save_black(path):
    # T_NoGlow: what M_Building's GlowTex shows on a building without lit windows
    im = bpy.data.images.new("T_NoGlow", 8, 8, alpha=False)
    p = np.zeros(8 * 8 * 4, np.float32); p[3::4] = 1.0
    im.pixels.foreach_set(p); im.filepath_raw = path; im.file_format = "PNG"; im.save()

save_black(os.path.join(out, "T_NoGlow.png"))

def tex_of(sock):
    stack = [sock]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n.image
            stack.extend(i for i in n.inputs)
    return None

# ---------- the model ----------
SHARP_COS = np.cos(np.radians(50.0))   # creases sharper than 50 degrees keep a hard edge in the shading (as building_reduce.py)

def weld(me, dist):
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    for f in bm.faces: f.smooth = True
    for e in bm.edges:
        e.smooth = not (e.is_manifold and e.link_faces[0].normal.dot(e.link_faces[1].normal) < SHARP_COS)
    bm.to_mesh(me); bm.free()

# A patch's picture (2026-11-14, visual review: a patch in one spot of the picture reads as a flat grey or green shape): a piece of the
# surface nearby that faces the same way (DONOR_COS) and is one unbroken stretch of the picture, at least as big as the patch, is laid
# over the patch at the picture's own size (a straight-line fit from position to picture over that stretch), so the patch shows
# real stone or plaster with its painted strokes instead of one colour.
DONOR_COS = 0.8         # a donor face turns at most about 37 degrees from the patch
DONOR_REACH = 450.0     # cm: how far from the patch the donor's first face may be
DONOR_SEEDS = 300       # how many stretches are tried, nearest first
DONOR_FIT = 0.02        # the fit may miss by at most this share of the stretch's picture size
DONOR_SHRINK = 3.0      # a stretch smaller than the patch may be laid on at most this much smaller (stones a third the size)
DONOR_COVER = 0.92      # this share of the patch's corners must land on the stretch (the rest just past its edge, in the picture's margin)

def donor_uvs(bm, uvl, verts, facing, centre, exclude, avoid, shrink_max=DONOR_SHRINK, cover_min=DONOR_COVER, near=None, first=False):
    seek = mathutils.Vector(near) if near is not None else centre   # (where to look first: a spot of the surface the patch should match)
    n = facing.normalized()
    ax = (mathutils.Vector((0.0, 0.0, 1.0)).cross(n) if abs(n.z) < 0.9 else mathutils.Vector((1.0, 0.0, 0.0)))
    ax.normalize(); ay = n.cross(ax).normalized()
    pts0 = np.array([[(v.co - centre).dot(ax), (v.co - centre).dot(ay)] for v in verts])
    pts0 = pts0 - (pts0.max(0) + pts0.min(0)) * 0.5
    turns = [pts0, np.c_[-pts0[:, 1], pts0[:, 0]]]   # (the patch as it is, and turned a quarter: a long patch fits a long stretch either way)
    def loop_at(f, v):
        return next(l for l in f.loops if l.vert == v)
    def ok(f):
        return f not in exclude and f.normal.dot(n) > DONOR_COS and not avoid(f.calc_center_median())
    cands = sorted((f for f in bm.faces if ok(f) and (f.calc_center_median() - seek).length < DONOR_REACH),
                   key=lambda f: (f.calc_center_median() - seek).length)
    tried, best = set(), None
    why = dict(small=0, fit=0, cover=0, maxext=[0.0, 0.0], bestcov=0.0)
    for seed in cands:
        if why["small"] + why["fit"] + why["cover"] >= DONOR_SEEDS: break
        if seed in tried: continue
        region, stack, seen = [seed], [seed], {seed}
        while stack and len(region) < 2500:
            f = stack.pop()
            for e in f.edges:
                for g in e.link_faces:
                    if g in seen or not ok(g): continue
                    if any((loop_at(f, v)[uvl].uv - loop_at(g, v)[uvl].uv).length > 1e-4 for v in e.verts): continue   # (a seam in the picture)
                    seen.add(g); region.append(g); stack.append(g)
        tried |= seen
        rows, uvs, tri = [], [], []
        for f in region:
            k0 = len(rows)
            for l in f.loops:
                d = l.vert.co - centre
                rows.append((d.dot(ax), d.dot(ay), 1.0)); uvs.append((l[uvl].uv.x, l[uvl].uv.y))
            tri += [(k0, k0 + i, k0 + i + 1) for i in range(1, len(f.loops) - 1)]
        A, U = np.array(rows), np.array(uvs)
        ext = A[:, :2].max(0) - A[:, :2].min(0)
        why["maxext"] = [max(why["maxext"][0], float(ext[0])), max(why["maxext"][1], float(ext[1]))]
        # (a stretch smaller than the patch: its picture is laid on smaller, by the turn that needs the least)
        shr = [max(1.0, float(((P.max(0) - P.min(0)) * 1.15 / np.maximum(ext, 1e-3)).max())) for P in turns]
        if min(shr) > (best[0] if best else shrink_max): why["small"] += 1; continue
        M = np.linalg.lstsq(A, U, rcond=None)[0]
        uvext = float((U.max(0) - U.min(0)).max()) or 1.0
        if np.sqrt(((A @ M - U) ** 2).sum(1).mean()) > DONOR_FIT * uvext: why["fit"] += 1; continue
        # where on the stretch the patch goes: its middle on a face of the stretch such that its corners land on the stretch
        T = np.array(tri); a, b, c = A[T[:, 0], :2], A[T[:, 1], :2], A[T[:, 2], :2]
        den = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
        good = np.abs(den) > 1e-9
        a, b, c, den = a[good], b[good], c[good], den[good]
        def covered(q):
            w1 = ((b[:, 1] - c[:, 1]) * (q[:, None, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (q[:, None, 1] - c[:, 1])) / den
            w2 = ((c[:, 1] - a[:, 1]) * (q[:, None, 0] - c[:, 0]) + (a[:, 0] - c[:, 0]) * (q[:, None, 1] - c[:, 1])) / den
            return ((w1 >= -1e-3) & (w2 >= -1e-3) & (1 - w1 - w2 >= -1e-3)).any(1).mean()
        def inside_mask(q):
            w1 = ((b[:, 1] - c[:, 1]) * (q[:, None, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (q[:, None, 1] - c[:, 1])) / den
            w2 = ((c[:, 1] - a[:, 1]) * (q[:, None, 0] - c[:, 0]) + (a[:, 0] - c[:, 0]) * (q[:, None, 1] - c[:, 1])) / den
            return ((w1 >= -1e-3) & (w2 >= -1e-3) & (1 - w1 - w2 >= -1e-3)).any(1)
        def clamp_in(q):
            # (the few corners past the stretch's edge are drawn back onto it, a little inside: past it lies whatever else the picture
            # holds there, a window's dark wood or a roof, which showed as dark spikes)
            q = q.copy(); out = ~inside_mask(q)
            if not out.any(): return q
            cen3 = (a + b + c) / 3.0
            for j in np.nonzero(out)[0]:
                bestd, bestp = None, None
                for s0, s1 in ((a, b), (b, c), (c, a)):
                    d = s1 - s0; L = (d * d).sum(1); L[L < 1e-12] = 1e-12
                    t = np.clip(((q[j] - s0) * d).sum(1) / L, 0.0, 1.0)
                    pp = s0 + d * t[:, None]; dd = ((pp - q[j]) ** 2).sum(1); k = int(dd.argmin())
                    if bestd is None or dd[k] < bestd: bestd, bestp = dd[k], pp[k] + (cen3[k] - pp[k]) * 0.08
                q[j] = bestp
            return q
        mids = np.array([A[T[i]].mean(0)[:2] for i in range(len(T))])
        cen = A[:, :2].mean(0)
        order = np.argsort(((mids - cen) ** 2).sum(1))[:40]
        res = None
        for k in (1.0, 1.25, 1.6):
            for P, s in sorted(zip(turns, shr), key=lambda t: t[1]):
                if s * k > shrink_max: continue
                for i in order:
                    q = P / (s * k) + mids[i]
                    cv = covered(q); why["bestcov"] = max(why["bestcov"], float(cv))
                    if cv >= cover_min:
                        out_ = np.c_[clamp_in(q), np.ones(len(q))] @ M
                        res = ({v: mathutils.Vector((float(u[0]), float(u[1]))) for v, u in zip(verts, out_)}, len(region), s * k)
                        break
                if res: break
            if res: break
        if not res: why["cover"] += 1; continue
        if res[2] <= 1.0 or first: return res   # (first=True, a fill patch with a spot named: the nearest stretch that fits, even laid on smaller, not a better fit further off)
        if best is None or res[2] < best[0]: best = (res[2], res)
    if not best and shrink_max == DONOR_SHRINK:   # (a second, looser try: smaller stones, a little more past the stretch's edge)
        return donor_uvs(bm, uvl, verts, facing, centre, exclude, avoid, 4.0, 0.85, near, first)
    if not best and len(verts) >= 15:
        print("BUILD     (no stretch: patch %.0f x %.0f cm, %d candidate faces, stretches too small %d, not flat %d, not covering %d, largest %.0f x %.0f, best cover %.2f)"
              % (np.ptp(pts0[:, 0]), np.ptp(pts0[:, 1]), len(cands), why["small"], why["fit"], why["cover"], why["maxext"][0], why["maxext"][1], why["bestcov"]))
    return best[1] if best else (None, 0, 0.0)

def remove_lanterns(me, boxes, patch_at=()):
    bm = bmesh.new(); bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    allboxes = list(boxes)
    def avoid(co, m=20.0):   # (no donor picture from what is left of a removed piece)
        return any(b[0] - m <= co.x <= b[1] + m and b[2] - m <= co.y <= b[3] + m and b[4] - m <= co.z <= b[5] + m for b in allboxes)
    n_donor = n_flat = 0
    # Pressed flat instead of cut off: a box whose 7th entry is a dict. Where Meshy fused a piece into the wall it stands on, the wall
    # behind it was never made, so cutting leaves a hole no fill closes well. Its points are pressed onto the surface instead,
    # dict(cyl=(cx, cy, r)) onto an upright round wall (a tower), dict(z=height) down onto a flat top (a garden wall, a step), and the
    # pressed faces take one spot of the picture, that of the biggest face round them: no hole, nothing sticking out.
    retex = [b for b in boxes if len(b) > 6 and isinstance(b[6], dict) and "retex" in b[6]]   # (done last, below)
    squash = [b for b in boxes if len(b) > 6 and isinstance(b[6], dict) and "retex" not in b[6]]
    boxes = [b for b in boxes if not (len(b) > 6 and isinstance(b[6], dict))]
    n_squash = 0
    for b in squash:
        d6 = b[6]
        if "disc" in d6:   # (a round footprint instead of the box's: a pot standing half on a wall top and half in the bed behind it)
            dx_, dy_, dr = d6["disc"]
            inb = lambda c: (c.x - dx_) ** 2 + (c.y - dy_) ** 2 <= dr * dr and b[4] <= c.z <= b[5]
        else:
            inb = lambda c: b[0] <= c.x <= b[1] and b[2] <= c.y <= b[3] and b[4] <= c.z <= b[5]
        faces = [f for f in bm.faces if any(inb(v.co) for v in f.verts)]   # (any corner in it: no face left stretched from it)
        if "cyl" in d6:
            cx, cy, r = d6["cyl"]
            if not d6.get("wall"): faces = [f for f in faces if (f.calc_center_median().x - cx) ** 2 + (f.calc_center_median().y - cy) ** 2 > (r + 3.0) ** 2]   # (the wall itself, at about r, keeps its picture; wall=True: the wall in the box takes the same picture as what is pressed onto it, so the two layers show one surface, not the painted lantern and spikes fighting through)
        fs = set(faces)
        only_in = "disc" in d6 or "y" in d6 or "x" in d6   # (only the points inside move: the faces reaching out from it are stretched, not the wall round it)
        for v in {v for f in faces for v in f.verts}:
            if only_in and not inb(v.co): continue
            if "cyl" in d6:
                dx, dy = v.co.x - cx, v.co.y - cy; d = (dx * dx + dy * dy) ** 0.5
                if d > r - 2.0: v.co.x, v.co.y = cx + dx * (r - 2.0) / d, cy + dy * (r - 2.0) / d   # (2 cm inside the wall: where the wall is, it shows, not the pressed layers fighting it)
            elif "x" in d6:   # (pressed back onto an upright face looking along +X: a pot beside the steps' wall)
                if v.co.x <= d6["x"]: continue
                v.co.x = d6["x"]
            elif "y" in d6:   # (pressed back onto an upright face looking along -Y: a pot in front of a pier at the steps' foot)
                if v.co.y >= d6["y"]: continue
                v.co.y = d6["y"]
            else:
                if v.co.z <= d6["z"]: continue   # (the wall itself stays where it is)
                v.co.z = d6["z"]
                if "shrink" in b[6]:   # (what is pressed is drawn in towards the middle of the box too, so it stays within the wall top)
                    k = b[6]["shrink"]; mx, my = (b[0] + b[1]) * 0.5, (b[2] + b[3]) * 0.5
                    v.co.x, v.co.y = mx + (v.co.x - mx) * k, my + (v.co.y - my) * k
        if "cyl" in d6 and d6.get("wall"):
            # (pressed onto the tower, the piece's sides are slivers and its back faces look into the wall: those showed as thin dark
            # lines and a shaded outline of the lantern; only what faces out, the wall and the piece's front, is kept)
            drop = []
            for f in faces:
                f.normal_update(); c = f.calc_center_median()
                rad = mathutils.Vector((c.x - cx, c.y - cy, 0.0)).normalized()
                if all(inb(v.co) for v in f.verts) and (f.calc_area() < 1.0 or f.normal.dot(rad) < 0.6): drop.append(f)   # (whole in the box: a stone or window frame at its edge keeps its sides)
            if drop: bmesh.ops.delete(bm, geom=drop, context="FACES")
            faces = [f for f in faces if f.is_valid]; fs = set(faces)
            print("BUILD   pressed onto the tower: %d sliver or inward faces dropped" % len(drop))
        ring ={g for f in faces for v in f.verts for g in v.link_faces if g not in fs}
        got = None
        if faces and uvl is not None:
            for f in faces: f.normal_update()
            vs = list({v for f in faces for v in f.verts})
            pc = sum((v.co for v in vs), mathutils.Vector()) / len(vs)
            facing = (mathutils.Vector((pc.x - cx, pc.y - cy, 0.0)) if "cyl" in d6 else mathutils.Vector((1.0, 0.0, 0.0)) if "x" in d6 else mathutils.Vector((0.0, -1.0, 0.0)) if "y" in d6 else mathutils.Vector((0.0, 0.0, 1.0)))
            got, nreg, sk = donor_uvs(bm, uvl, vs, facing, pc, fs, avoid, near=d6.get("uvat"))   # (uvat: a spot of the surface it is pressed into)
            print("BUILD   pressed patch at", [round(x) for x in pc], "picture from", ("a stretch of %d faces, x1/%.2f" % (nreg, sk)) if got else "ONE SPOT (no stretch found)")
        if got:
            n_donor += 1
            for f in faces:
                for l in f.loops: l[uvl].uv = got[l.vert].copy()
        elif ring and uvl is not None:
            n_flat += 1
            if "uvat" in b[6]:   # (the picture of the kept face nearest that point: the stone or plaster it is pressed into)
                p = mathutils.Vector(b[6]["uvat"])
                pick = min((g for g in bm.faces if g not in fs and (g.calc_center_median() - p).length < 60.0), key=lambda g: (g.calc_center_median() - p).length, default=None)
            else: pick = None
            uv = (pick or max(ring, key=lambda g: g.calc_area())).loops[0][uvl].uv.copy()
            for f in faces:
                for l in f.loops: l[uvl].uv = uv.copy()
        if "cyl" not in d6:
            # (pressed into a plane, the layers lie on each other: each shaded flat, so they show one face, not dark smears)
            for f in faces: f.normal_update(); f.smooth = False
        if "sides" in d6 and uvl is not None:
            # what is left standing of the piece below the press (a pot's foot in the bed): its upright sides take the picture of an
            # upright stretch nearby that faces the same way, a quarter of the compass at a time
            zt = d6["z"]
            side = [f for f in bm.faces if inb(f.calc_center_median()) or ((f.calc_center_median().x - dx_) ** 2 + (f.calc_center_median().y - dy_) ** 2 <= dr * dr and b[4] - 30.0 <= f.calc_center_median().z <= zt)]
            side = [f for f in side if (f.normal_update() or True) and abs(f.normal.z) < 0.6]
            for q in range(4):
                ang = math.pi * 0.5 * q
                dirq = mathutils.Vector((math.cos(ang), math.sin(ang), 0.0))
                grp = [f for f in side if mathutils.Vector((f.normal.x, f.normal.y, 0.0)).normalized().dot(dirq) > math.cos(math.pi / 4) - 1e-6]
                if not grp: continue
                gv = list({v for f in grp for v in f.verts})
                gc = sum((v.co for v in gv), mathutils.Vector()) / len(gv)
                got2, nreg2, sk2 = donor_uvs(bm, uvl, gv, dirq, gc, fs | set(side), avoid)
                print("BUILD   pressed patch's sides (%d faces) toward %d deg: picture from" % (len(grp), q * 90), ("a stretch of %d faces, x1/%.2f" % (nreg2, sk2)) if got2 else "nothing (kept)")
                if got2:
                    for f in grp:
                        for l in f.loops: l[uvl].uv = got2[l.vert].copy()
        n_squash += len(faces)
    if squash: print("BUILD   %d faces pressed flat" % n_squash)
    def inside(co):
        # (a 7th entry (cx, cy, r) keeps whatever is within r of that upright axis: a round tower the lantern or barrel stands against)
        return any(b[0] <= co.x <= b[1] and b[2] <= co.y <= b[3] and b[4] <= co.z <= b[5]
                   and (len(b) < 7 or (co.x - b[6][0]) ** 2 + (co.y - b[6][1]) ** 2 > b[6][2] ** 2) for b in boxes)
    dead = [f for f in bm.faces if inside(f.calc_center_median())]   # (by its middle: the bracket plates reach into the post)
    deadset = set(dead)
    rim_verts = {v for f in dead for v in f.verts if any(g not in deadset for g in v.link_faces)}
    bmesh.ops.delete(bm, geom=dead, context="FACES")
    # what is left of a bracket or collar round a post is a small loose piece now: those go too (the building itself is one piece)
    def near(co, m=15.0):
        return any(b[0] - m <= co.x <= b[1] + m and b[2] - m <= co.y <= b[3] + m and b[4] - m <= co.z <= b[5] + m for b in boxes)
    seen, scraps = set(), []
    for f0 in [f for f in bm.faces if near(f.calc_center_median())]:
        if f0 in seen: continue
        comp, stack, big = [f0], [f0], False
        seen.add(f0)
        while stack and not big:
            f = stack.pop()
            for e in f.edges:
                for g in e.link_faces:
                    if g not in seen:
                        seen.add(g); comp.append(g); stack.append(g)
            big = len(comp) > LANTERN_SCRAP
        if not big and all(near(f.calc_center_median()) for f in comp): scraps.extend(comp)   # (not a barrel or crate standing close by)
    if scraps:
        rim_verts |= {v for f in scraps for v in f.verts}
        bmesh.ops.delete(bm, geom=scraps, context="FACES")
    rim = [e for v in rim_verts if v.is_valid for e in v.link_edges if e.is_boundary]
    rim = list(set(rim))
    filled = bmesh.ops.holes_fill(bm, edges=rim, sides=0)["faces"] if rim else []
    # (a hole whose rim touches itself, where a pot sat on a wall's edge or a barrel against a tower, isn't filled by that: those are
    # triangulated across instead)
    left = [e for e in rim if e.is_valid and e.is_boundary]
    if left:
        filled = list(filled) + list(bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=left)["geom"])
        filled = [f for f in filled if isinstance(f, bmesh.types.BMFace)]
    # (and what neither closes, a rim that twists or doubles back, is closed by a fan from the middle of each open loop)
    still = [e for e in rim if e.is_valid and e.is_boundary]
    fans = 0
    while still:
        loop, todo = set(), [still[0]]
        while todo:
            e = todo.pop()
            if e in loop: continue
            loop.add(e)
            todo.extend(g for v in e.verts for g in v.link_edges if g.is_boundary and g not in loop)
        still = [e for e in still if e not in loop]
        if len(loop) < 3 or len(loop) > 800: continue
        mid = bm.verts.new(sum((v.co for e in loop for v in e.verts), mathutils.Vector()) / (2 * len(loop)))
        for e in loop:
            try: filled = list(filled) + [bm.faces.new((e.verts[0], e.verts[1], mid))]; fans += 1
            except ValueError: pass
    print("BUILD   holes: %d rim edges, %d left for the second fill, %d closed by fans, %d still open" % (len(rim), len(left), fans, sum(1 for e in rim if e.is_valid and e.is_boundary)))
    if filled:
        filled = bmesh.ops.triangulate(bm, faces=filled)["faces"]
        new = set(filled)
        # Each patch (the fill of one hole) takes one spot of the picture: that of the biggest old face on its rim, the wall or step it
        # closes (corner by corner the picture smeared across whatever stood round the hole: wood, a pot's rim). And it faces the same
        # way as the old faces round it (a fill can come out facing in, which shows as a hole from outside).
        seen_f = set()
        for f0 in filled:
            if f0 in seen_f: continue
            patch, stack = [f0], [f0]; seen_f.add(f0)
            while stack:
                f = stack.pop()
                for e in f.edges:
                    for g in e.link_faces:
                        if g in new and g not in seen_f: seen_f.add(g); patch.append(g); stack.append(g)
            ring = {o.face for f in patch for l in f.loops for o in l.vert.link_loops if o.face not in new}
            if not ring: continue
            # (a flat patch above the ground, where a pot stood on a wall top or a landing, faces up and takes the picture of the top round
            # it: what is left of the pot round the hole can face any way)
            for f in patch: f.normal_update()
            area = sum(f.calc_area() for f in patch) or 1.0
            pc = sum((f.calc_center_median() for f in patch), mathutils.Vector()) / len(patch)
            p = min((mathutils.Vector(q) for q in patch_at), key=lambda q: (q - pc).length) if patch_at else None
            flat = sum(abs(f.normal.z) * f.calc_area() for f in patch) / area
            near_at = p is not None and (p - pc).length < 150.0 and abs(p.z - pc.z) < 35.0 and flat > 0.3
            top = pc.z > 30.0 and (near_at or flat > 0.8)   # (faces of a fill can come out either way; 30 cm: a bench top is a top, the ground is not)
            ups = [g for g in ring if g.normal.z > 0.7]
            best = max(ups if top and ups else ring, key=lambda g: g.calc_area())
            if top and near_at:
                    best = min((g for g in bm.faces if g not in new and (g.calc_center_median() - p).length < 60.0), key=lambda g: (g.calc_center_median() - p).length, default=best)
            uv = best.loops[0][uvl].uv.copy() if uvl is not None else None
            around = mathutils.Vector((0.0, 0.0, 1.0)) if top else sum((g.normal * g.calc_area() for g in ring), mathutils.Vector())
            got = None
            if uvl is not None and around.length > 1e-6:
                vs = list({v for f in patch for v in f.verts})
                got, nreg, sk = donor_uvs(bm, uvl, vs, around, pc, new, avoid, near=(p if top and near_at else None), first=bool(top and near_at))
                print("BUILD   patch at", [round(x) for x in pc], "%d faces" % len(patch), "picture from", ("a stretch of %d faces, x1/%.2f" % (nreg, sk)) if got else "ONE SPOT (no stretch found)")
            if got: n_donor += 1
            else: n_flat += 1
            for f in patch:
                if got:
                    for l in f.loops: l[uvl].uv = got[l.vert].copy()
                elif uv is not None:
                    for l in f.loops: l[uvl].uv = uv.copy()
                f.normal_update()
                if f.normal.dot(around) < 0: f.normal_flip()
                f.smooth = True
            # (a patch round a corner, a pier's front and side, can't face every way at once: the back of each face is drawn too, so no
            # part of it shows as a hole from either side)
            dup = [g for g in bmesh.ops.duplicate(bm, geom=patch)["geom"] if isinstance(g, bmesh.types.BMFace)]
            bmesh.ops.reverse_faces(bm, faces=dup, flip_multires=False)
    # Re-pictured, nothing moved (dict(retex=...)): what is left standing of a removed piece where it was fused to a wall or a bed (a
    # pot's foot in the bed, the back of a pot against the wall) takes the picture of a stretch of nearby surface facing the same way,
    # so it reads as the stone or plaster round it. retex="all": every face in the box, by which way it faces (up, or a quarter of the
    # compass); or one way, (x, y, z). at=(x, y, z): where the up-facing ones look first (the wall-top stone, not the bed's grass).
    if uvl is not None:
        for b in retex:
            d6 = b[6]
            cand = [f for f in bm.faces if b[0] <= f.calc_center_median().x <= b[1] and b[2] <= f.calc_center_median().y <= b[3] and b[4] <= f.calc_center_median().z <= b[5]]
            for f in cand: f.normal_update()
            dirs = [mathutils.Vector((0.0, 0.0, 1.0))] + [mathutils.Vector((math.cos(math.pi * 0.5 * q), math.sin(math.pi * 0.5 * q), 0.0)) for q in range(4)]
            used, cs = set(), set(cand)
            for dv in dirs:
                if d6["retex"] != "all" and dv.dot(mathutils.Vector(d6["retex"]).normalized()) < 0.99: continue
                grp = [f for f in cand if f not in used and f.normal.dot(dv) > (0.7 if dv.z > 0.5 else 0.5)]
                if not grp: continue
                used |= set(grp)
                gv = list({v for f in grp for v in f.verts})
                gc = sum((v.co for v in gv), mathutils.Vector()) / len(gv)
                got3, nreg3, sk3 = donor_uvs(bm, uvl, gv, dv, gc, cs, avoid, near=(d6.get("near") or (d6.get("at") if dv.z > 0.5 else None)))
                print("BUILD   re-pictured %d faces facing %s at %s: picture from" % (len(grp), tuple(round(x) for x in dv), [round(x) for x in gc]),
                      ("a stretch of %d faces, x1/%.2f" % (nreg3, sk3)) if got3 else "nothing (kept)")
                if got3:
                    for f in grp:
                        for l in f.loops: l[uvl].uv = got3[l.vert].copy()
    print("BUILD   patches: %d take a stretch of nearby surface, %d one spot" % (n_donor, n_flat))
    bm.to_mesh(me); bm.free()
    return len(dead), len(filled)

def face_samples(me, img_px):
    # for each face: the brightest of seven points of a picture inside it, and its average colour
    H, W = img_px.shape[:2]
    uv = np.empty(len(me.loops) * 2); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    ls = np.empty(len(me.polygons), np.int64); me.polygons.foreach_get("loop_start", ls)
    best = np.zeros(len(ls)); col = np.zeros((len(ls), 3)); avg = np.zeros((len(ls), 3))
    W7 = ((1/3, 1/3, 1/3), (0.6, 0.2, 0.2), (0.2, 0.6, 0.2), (0.2, 0.2, 0.6), (0.45, 0.45, 0.1), (0.1, 0.45, 0.45), (0.45, 0.1, 0.45))
    for w in W7:
        u = uv[ls] * w[0] + uv[ls + 1] * w[1] + uv[ls + 2] * w[2]
        x = np.clip((u[:, 0] % 1.0) * W, 0, W - 1).astype(int); y = np.clip((u[:, 1] % 1.0) * H, 0, H - 1).astype(int)
        c = img_px[y, x, :3]; m = c.max(1)
        upd = m > best; best = np.where(upd, m, best); col[upd] = c[upd]; avg += c / len(W7)
    return best, col, avg

def to_lab(rgb):
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((np.maximum(rgb, 0) + 0.055) / 1.055) ** 2.4)
    xyz = c @ M.T / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)

def mark_windows(me, glow_px, name):
    nf = len(me.polygons)
    vc = np.zeros((nf, 3), np.float32); vc[:, 2] = 1.0           # default: never glows
    if glow_px is not None:
        best, col, _ = face_samples(me, glow_px)
        lit = best > 0.02
        cen = np.empty(nf * 3); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3)
        idx = np.nonzero(lit)[0]
        keys = {}
        for i in idx: keys.setdefault(tuple((cen[i] // WINDOW_CELL).astype(int)), []).append(i)
        lab, clusters = {}, []
        for k in keys:
            if k in lab: continue
            stack = [k]; lab[k] = len(clusters); mem = []
            while stack:
                q = stack.pop(); mem.extend(keys[q])
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            r = (q[0] + dx, q[1] + dy, q[2] + dz)
                            if r in keys and r not in lab: lab[r] = lab[k]; stack.append(r)
            clusters.append(np.array(mem))
        rng = np.random.default_rng(sum(ord(ch) for ch in name) * 7919 + nf)   # (fixed per model)
        nwin = nfire = nspeck = 0
        for mem in clusters:
            size = cen[mem].max(0) - cen[mem].min(0)
            hue = float(hue_of(col[mem].mean(0)))
            if hue < HUE_FIRE and col[mem].mean(0).max() > 0.2:
                vc[mem] = (0.0, 0.0, 0.0); nfire += 1
            elif np.sort(size)[1] <= SPECK_CM:
                nspeck += 1                                          # (a speck: stays dark, left as painted)
            else:
                vc[mem] = (rng.random(), 1.0, 0.0); nwin += 1
        print("BUILD %s lit faces %d: %d windows, %d fire, %d specks" % (name, len(idx), nwin, nfire, nspeck))
    attr = me.color_attributes.new("WindowMask", "FLOAT_COLOR", "CORNER")
    ls = np.empty(nf, np.int64); me.polygons.foreach_get("loop_start", ls)
    lt = np.empty(nf, np.int64); me.polygons.foreach_get("loop_total", lt)
    per_loop = np.repeat(np.concatenate([vc, np.ones((nf, 1), np.float32)], 1), lt, axis=0)
    attr.data.foreach_set("color", per_loop.ravel())
    me.color_attributes.active_color = attr

def smooth_plaster(me, base_px, name):
    nf = len(me.polygons)
    _, _, avg = face_samples(me, base_px)
    L = to_lab(avg)
    plaster = (L[:, 0] > PLASTER_L) & (np.hypot(L[:, 1], L[:, 2]) < PLASTER_CHROMA)
    fn = np.empty(nf * 3); me.polygons.foreach_get("normal", fn); fn = fn.reshape(-1, 3)
    area = np.empty(nf); me.polygons.foreach_get("area", area)
    ls = np.empty(nf, np.int64); me.polygons.foreach_get("loop_start", ls)
    lt = np.empty(nf, np.int64); me.polygons.foreach_get("loop_total", lt)
    vi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", vi)
    lf = np.repeat(np.arange(nf), lt)                                # each corner's face
    nv = len(me.vertices)
    pl = plaster[lf]
    n = np.zeros((nv, 3))
    np.add.at(n, vi[pl], fn[lf[pl]] * area[lf[pl], None])
    isp = np.linalg.norm(n, axis=1) > 0
    n[isp] /= np.linalg.norm(n[isp], axis=1)[:, None]
    # the plaster's edges (both ends plaster, inside a plaster face)
    e0, e1 = [], []
    for k in range(3):
        a_ = vi[ls + k][plaster]; b_ = vi[ls + (k + 1) % 3][plaster]
        e0.append(a_); e1.append(b_)
    e0 = np.concatenate(e0); e1 = np.concatenate(e1)
    for _ in range(PLASTER_ROUNDS):
        keep = (n[e0] * n[e1]).sum(1) > PLASTER_CORNER
        acc = n.copy()
        for d in range(3):
            acc[:, d] += np.bincount(e0[keep], weights=n[e1[keep], d], minlength=nv) + np.bincount(e1[keep], weights=n[e0[keep], d], minlength=nv)
        ln = np.linalg.norm(acc, axis=1)
        n = np.where((ln > 0)[:, None] & isp[:, None], acc / np.maximum(ln, 1e-9)[:, None], n)
    cn = np.empty(len(me.loops) * 3); me.corner_normals.foreach_get("vector", cn); cn = cn.reshape(-1, 3)
    cn[pl] = n[vi[pl]]
    me.normals_split_custom_set(cn.tolist())
    print("BUILD %s plaster faces %d (%.0f%%), normals evened over %d rounds" % (name, int(plaster.sum()), 100.0 * plaster.mean(), PLASTER_ROUNDS))

def flatten_plaster_normals(nrm_path, base_px):
    # the normal map flattened where the colour picture is plaster (Meshy's plaster ripple), the timber's grain and the lines kept
    im = bpy.data.images.load(nrm_path); nm = pixels(im).copy(); bpy.data.images.remove(im)
    if nm.shape[:2] != base_px.shape[:2]:
        print("BUILD normal map %s is %s, the colour %s: left alone" % (os.path.basename(nrm_path), nm.shape[:2], base_px.shape[:2])); return
    L = to_lab(base_px[..., :3])
    m = ((L[..., 0] > PLASTER_L) & (np.hypot(L[..., 1], L[..., 2]) < PLASTER_CHROMA)).astype(np.float32)
    m = np.clip(box_blur(m, 2) * 1.5 - 0.25, 0, 1) * PLASTER_NORMAL_FLAT
    flat = np.array([0.5, 0.5, 1.0], np.float32)
    nm[..., :3] = nm[..., :3] * (1 - m[..., None]) + flat * m[..., None]
    write_png(nm, nrm_path)
    print("BUILD plaster normal map flattened on %.1f%% of texels" % (100.0 * (m > 0.5).mean()))

def collision_copy(o, name, path):
    # a reduced copy of the finished model: its own triangles are the building's collision in the game
    c = o.copy(); c.data = o.data.copy(); bpy.context.scene.collection.objects.link(c)
    me = c.data
    while me.color_attributes: me.color_attributes.remove(me.color_attributes[0])
    target = COL_TRIS.get(name, COL_DEFAULT)
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = c; c.select_set(True)
    if tris > target:
        m = c.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = target / tris; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False,
                             colors_type="NONE")
    print("BUILD collision", os.path.basename(path), "tris", sum(len(p.vertices) - 2 for p in me.polygons))
    bpy.data.objects.remove(c, do_unlink=True)
    o.select_set(True); bpy.context.view_layer.objects.active = o

for name in names:
    B = BUILDINGS[name]
    f = os.path.join(src, B["file"])
    if not os.path.exists(f):
        print("BUILD missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    co = np.empty(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    if "turn" in B:   # (degrees about the up axis, counter-clockwise from above: a model that came turned, its door to go to -Y)
        t = np.radians(B["turn"]); cx, cy = co[:, 0].copy(), co[:, 1].copy()
        co[:, 0] = cx * np.cos(t) - cy * np.sin(t); co[:, 1] = cx * np.sin(t) + cy * np.cos(t)
    lo, hi = co.min(axis=0), co.max(axis=0)
    S = B["cm"] if "cm" in B else B["length"] / (hi[0] - lo[0])   # (cm: centimetres per unit of the file, instead of a length)
    co[:, 0] = (co[:, 0] - (lo[0] + hi[0]) * 0.5) * S
    co[:, 1] = (co[:, 1] - (lo[1] + hi[1]) * 0.5) * S * B.get("ys", 1.0)   # (ys, zs: stretch a model that came out the wrong shape)
    co[:, 2] = (co[:, 2] - lo[2]) * S * B.get("zs", 1.0) - B["sink"]
    me.vertices.foreach_set("co", co.reshape(-1).astype(np.float32))
    me.update()
    town = name not in TEST_HOUSES
    if town:
        if me.has_custom_normals:
            bpy.ops.mesh.customdata_custom_splitnormals_clear()   # (worked out again from the welded shape, then the plaster's evened)
        weld(me, S * (hi[0] - lo[0]) * 1.0e-5)
        if name in LANTERNS:
            nd, nfill = remove_lanterns(me, LANTERNS[name], PATCH_AT.get(name, ()))
            print("BUILD %s lanterns: %d faces taken off, %d faces fill the holes" % (name, nd, nfill))
        me.update()
        co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    # the walls' rectangle: the stone base, without the porch posts or the odd stray point
    band = co[(co[:, 2] > 15.0) & (co[:, 2] < 45.0)]
    wx = np.percentile(band[:, 0], [1, 99]); wy = np.percentile(band[:, 1], [1, 99])
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    print("BUILD", name, "tris", tris, "size", [round(float(x), 1) for x in co.max(axis=0) - co.min(axis=0)],
          "lo", [round(float(x), 1) for x in co.min(axis=0)], "hi", [round(float(x), 1) for x in co.max(axis=0)],
          "walls x", [round(float(x), 1) for x in wx], "y", [round(float(x), 1) for x in wy])
    # the pictures
    pr = next(n for n in me.materials[0].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    base = tex_of(pr.inputs["Base Color"]); normal = tex_of(pr.inputs["Normal"]); mr = tex_of(pr.inputs["Metallic"]) or tex_of(pr.inputs["Roughness"])
    save_pic(base, os.path.join(out, "T_%s_Base.png" % name))
    if normal: save_pic(normal, os.path.join(out, "T_%s_Normal.png" % name))
    if mr: save_pic(mr, os.path.join(out, "T_%s_MR.png" % name))
    glow = tex_of(pr.inputs["Emission Color"])     # the lit windows (Tools/building_glow.py), if the model has them
    glow_px = save_glow(glow, os.path.join(out, "T_%s_Glow.png" % name)) if glow else None
    if town:
        base_px = pixels(base)
        smooth_plaster(me, base_px, name)
        if normal: flatten_plaster_normals(os.path.join(out, "T_%s_Normal.png" % name), base_px)
        mark_windows(me, glow_px, name)
    fbx = os.path.join(out, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False,
                             colors_type="LINEAR" if town else "NONE")
    print("BUILD wrote", fbx, round(os.path.getsize(fbx) / 1e6, 1), "MB")
    if town: collision_copy(o, name, os.path.join(out, name + "_Col.fbx"))
