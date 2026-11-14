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
import bpy, bmesh, mathutils, os, sys
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
    # A third house design (owner, 2026-11-13): the library's "Storybook cottage v2" by kaesar3d (Docs/OakHillPlan.md), reduced from
    # StorybookCottage_Detailed.glb by building_reduce.py, its glass found by building_glow.py (--mask-only --rough-max 1 --hue 195,232
    # --sat 0.45,1 --val 0.3,0.95 --grow 6), then repainted by building_palette.py "cottage". It came turned 56 degrees with its door to +X: turned so the door faces -Y like the others.
    # cm: its arched door 2.3 m tall (0.37 of the file's units), like the house's door at the house's scale.
    "StorybookCottage_300k":   dict(file="StorybookCottage_300k.glb",   cm=620.0, sink=15.0, turn=-56.0),
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
    # the storybook cottage: its three lanterns, and (owner: "remove its lanterns, the barrel and the potted plants") the barrel and flower pots too
    "StorybookCottage_300k": [
        (336.0, 414.0, -112.0, -45.0, 284.0, 386.0, dict(cyl=(375.0, 108.0, 166.0), uvat=(330.0, -40.0, 335.0))), (540.0, 600.0, 25.0, 90.0, 296.0, 380.0, dict(cyl=(375.0, 108.0, 166.0), uvat=(531.0, 165.0, 340.0))), (396.0, 440.0, -180.0, -126.0, 46.0, 134.0),   # lanterns
        (492.0, 630.0, -50.0, 105.0, -16.0, 168.0, dict(cyl=(348.0, 95.0, 190.0), uvat=(530.0, 150.0, 60.0))),                                           # the barrel (the bench stays)
        (-150.0, -30.0, -420.0, -240.0, 104.0, 170.0, dict(z=101.0)), (-350.0, -220.0, -370.0, -190.0, 104.0, 170.0, dict(z=101.0)), (-490.0, -360.0, -290.0, -90.0, 104.0, 170.0, dict(z=101.0)),
        (-630.0, -500.0, -180.0, 20.0, 104.0, 215.0, dict(z=101.0)),                                                                                         # pots on the garden wall
        (40.0, 108.0, -200.0, -60.0, 158.0, 228.0, dict(z=157.0)), (286.0, 394.0, -200.0, -60.0, 166.0, 228.0, dict(z=165.0)),                                            # pots by the door
        (16.0, 114.0, -525.0, -395.0, -16.0, 52.0, dict(z=-13.0)), (332.0, 428.0, -405.0, -265.0, -16.0, 62.0, dict(z=-13.0))],                                           # pots at the steps' foot
}
LANTERN_SCRAP = 1500   # a loose piece near a lantern with fewer faces than this is what is left of its bracket

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

def remove_lanterns(me, boxes):
    bm = bmesh.new(); bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    # Pressed flat instead of cut off: a box whose 7th entry is a dict. Where Meshy fused a piece into the wall it stands on, the wall
    # behind it was never made, so cutting leaves a hole no fill closes well. Its points are pressed onto the surface instead,
    # dict(cyl=(cx, cy, r)) onto an upright round wall (a tower), dict(z=height) down onto a flat top (a garden wall, a step), and the
    # pressed faces take one spot of the picture, that of the biggest face round them: no hole, nothing sticking out.
    squash = [b for b in boxes if len(b) > 6 and isinstance(b[6], dict)]
    boxes = [b for b in boxes if not (len(b) > 6 and isinstance(b[6], dict))]
    n_squash = 0
    for b in squash:
        inb = lambda c: b[0] <= c.x <= b[1] and b[2] <= c.y <= b[3] and b[4] <= c.z <= b[5]
        faces = [f for f in bm.faces if any(inb(v.co) for v in f.verts)]   # (any corner in it: no face left stretched from it)
        if "cyl" in b[6]:
            cx, cy, r = b[6]["cyl"]
            faces = [f for f in faces if (f.calc_center_median().x - cx) ** 2 + (f.calc_center_median().y - cy) ** 2 > (r + 3.0) ** 2]   # (the wall itself, at about r, keeps its picture)
        fs = set(faces)
        for v in {v for f in faces for v in f.verts}:
            if "cyl" in b[6]:
                dx, dy = v.co.x - cx, v.co.y - cy; d = (dx * dx + dy * dy) ** 0.5
                if d > r: v.co.x, v.co.y = cx + dx * r / d, cy + dy * r / d
            else:
                if v.co.z > b[6]["z"]: v.co.z = b[6]["z"]
        ring = {g for f in faces for v in f.verts for g in v.link_faces if g not in fs}
        if ring and uvl is not None:
            if "uvat" in b[6]:   # (the picture of the kept face nearest that point: the stone or plaster it is pressed into)
                p = mathutils.Vector(b[6]["uvat"])
                pick = min((g for g in bm.faces if g not in fs and (g.calc_center_median() - p).length < 60.0), key=lambda g: (g.calc_center_median() - p).length, default=None)
            else: pick = None
            uv = (pick or max(ring, key=lambda g: g.calc_area())).loops[0][uvl].uv.copy()
            for f in faces:
                for l in f.loops: l[uvl].uv = uv.copy()
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
    print("BUILD   holes: %d rim edges, %d left for the second fill, %d still open" % (len(rim), len(left), sum(1 for e in rim if e.is_valid and e.is_boundary)))
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
            best = max(ring, key=lambda g: g.calc_area())
            uv = best.loops[0][uvl].uv.copy() if uvl is not None else None
            around = sum((g.normal * g.calc_area() for g in ring), mathutils.Vector())
            for f in patch:
                if uv is not None:
                    for l in f.loops: l[uvl].uv = uv.copy()
                f.normal_update()
                if f.normal.dot(around) < 0: f.normal_flip()
                f.smooth = True
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
            nd, nfill = remove_lanterns(me, LANTERNS[name])
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
