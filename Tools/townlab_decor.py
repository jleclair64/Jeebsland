# TownLab decorations (2026-11-14): the nature and clutter that make Overwatch's Eichenwalde streets feel alive (Docs/TownLab/
# StyleSheet.md: "ivy up the walls, moss on ledges and roof edges, flowers in pots and boxes, weeds at the base ... a few crates,
# planks, sacks, stones at wall bases"). Separate pieces, never part of a building (owner rule). Blender, nothing in Unreal:
#   blender -b -P Tools/townlab_decor.py -- <project folder>
# Makes, in Saved/KnightBuild/TownLab/Decor (Tools/townlab_import.py brings them into /Game/TownLab/Buildings):
#   T_TL_Foliage.png    one painted 2048 atlas with alpha: three ivy patches (climbing, wide, hanging) and a fringe of moss
#   TL_IvyClimb / TL_IvyWide / TL_IvyHang / TL_MossFringe .fbx   cards on that atlas (two layers, a little bent; about 100 triangles)
#   TL_MossClump / TL_Stones .fbx   small solid pieces painted in their vertex colour (a few hundred triangles each)
# and writes Source/Jeebsland/TownLabDecor.h: where every piece goes on each building, planned on the real model (raycasts against
# the finished 300k model in Saved/KnightBuild/Buildings: walls, windows, eaves, roof), plus a few of Oak Hill's filler props
# (/Game/Filler, the Meshy library pieces in Art/Filler/Clean) against the walls and the lanterns building_clean.py took off.
import bpy, bmesh, sys, os, math, random
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

proj = sys.argv[sys.argv.index("--") + 1]
out = os.path.join(proj, "Saved", "KnightBuild", "TownLab", "Decor")
work = os.path.join(proj, "Saved", "KnightBuild", "Buildings")
os.makedirs(out, exist_ok=True)
def log(*x): print("TLDECOR", *x, flush=True)

# ============================================================================================================================
# 1. The painted atlas (sRGB colours; 2048, four 1024 tiles: 0 climbing ivy, 1 wide ivy, 2 hanging ivy, 3 moss fringe)
# ============================================================================================================================
S = 2048; Tl = 1024
img = np.zeros((S, S, 4), np.float32)       # rows top to bottom (flipped to Blender's bottom-up when saved)
rng = np.random.default_rng(1405)
LEAF_GREENS = np.array([[0.17, 0.31, 0.09], [0.23, 0.38, 0.11], [0.30, 0.44, 0.14], [0.13, 0.25, 0.08], [0.22, 0.34, 0.15], [0.33, 0.42, 0.16], [0.19, 0.33, 0.14], [0.36, 0.50, 0.18], [0.42, 0.52, 0.22]], np.float32)   # (deep natural ivy greens, a few olive and a few cooler: townlab review "over-saturated")
VINE = np.array([0.36, 0.25, 0.15], np.float32)
MOSS = np.array([[0.42, 0.55, 0.16], [0.52, 0.62, 0.19], [0.34, 0.47, 0.15], [0.58, 0.64, 0.24]], np.float32)

def stamp(cx, cy, size, ang, col, shade_top, tile_xy, kind="leaf"):
    # one ivy leaf (three rounded lobes, a lighter top-left, a darker rim and a vein) or a moss blob, painted over what is there
    ox, oy = tile_xy
    r = int(size * 0.75) + 2
    x0, x1 = int(cx) - r, int(cx) + r + 1; y0, y1 = int(cy) - r, int(cy) + r + 1
    if x0 < 2 or y0 < 2 or x1 > Tl - 2 or y1 > Tl - 2: return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    dx, dy = (xx - cx) / size, (yy - cy) / size
    c, s = math.cos(ang), math.sin(ang)
    u, v = dx * c + dy * s, -dx * s + dy * c        # u along the leaf (tip at +u), v across
    if kind == "leaf":
        th = np.arctan2(v, u); rr = np.hypot(u, v)
        edge = 0.42 + 0.13 * np.cos(3 * th) + 0.06 * np.cos(th)      # three lobes, a little longer at the tip
        inside = rr < edge
        soft = np.clip((edge - rr) * size * 0.9, 0, 1)
        rim = np.clip(1 - (edge - rr) * size / 2.5, 0, 1) * inside
        light = np.clip(0.5 - 0.6 * (dx * 0.6 + dy * 0.8), 0, 1)      # light from the upper left
        vein = np.exp(-(v * size / 1.2) ** 2) * (u > -0.05) * (u < edge) * 0.25
        k = (0.78 + 0.42 * light)[..., None] * shade_top
        colr = col[None, None, :] * k * (1 - 0.35 * rim[..., None]) * (1 - vein[..., None])
        colr += np.array([0.06, 0.06, 0.0], np.float32) * (light ** 3)[..., None]    # a painted highlight
    else:
        rr = np.hypot(u, v * 1.15); inside = rr < 0.5
        soft = np.clip((0.5 - rr) * size * 0.8, 0, 1)
        light = np.clip(0.55 - 0.9 * dy, 0, 1)
        colr = col[None, None, :] * (0.75 + 0.45 * light)[..., None] * shade_top
    a = soft[..., None]
    reg = img[oy + y0:oy + y1, ox + x0:ox + x1]
    reg[..., :3] = reg[..., :3] * (1 - a) + colr * a
    reg[..., 3:] = np.maximum(reg[..., 3:], a)

def vine(points, tile_xy, width=2.2):
    ox, oy = tile_xy
    for (x0, y0), (x1, y1) in zip(points[:-1], points[1:]):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) / 1.5) + 1
        for t in np.linspace(0, 1, n):
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            xi, yi = int(x), int(y)
            if 3 < xi < Tl - 3 and 3 < yi < Tl - 3:
                w = int(width)
                reg = img[oy + yi - w:oy + yi + w + 1, ox + xi - w:ox + xi + w + 1]
                reg[..., :3] = VINE; reg[..., 3] = 1.0

def walk(x, y, steps, step, drift, up=True):
    pts = [(x, y)]; ang = -math.pi / 2 if up else math.pi / 2
    for _ in range(steps):
        ang += rng.normal(0, drift)
        ang = ang * 0.85 + (-math.pi / 2 if up else math.pi / 2) * 0.15
        x += math.cos(ang) * step; y += math.sin(ang) * step
        pts.append((x, y))
    return pts

def ivy(tile_xy, shape):
    # shape(x, y) in 0..1 of the tile -> how likely a leaf is there; vines first, leaves along them, bottom layers darker
    strands = []
    if shape == "climb":
        for i in range(22):
            x = Tl * (0.10 + 0.80 * rng.random()); strands.append(walk(x, Tl - 10, 60, 16, 0.6))   # (wide: the review found 20-30 cm strips)
    elif shape == "wide":
        for i in range(20):
            x = Tl * (0.08 + 0.84 * rng.random()); strands.append(walk(x, Tl - 10 - rng.random() * 200, 42, 15, 0.5))
    else:   # hanging from the top edge
        for i in range(22):
            x = Tl * (0.05 + 0.9 * rng.random()); strands.append(walk(x, 8, int(20 + 40 * rng.random()), 15, 0.25, up=False))
    for p in strands: vine(p, tile_xy)
    leaves = []
    for p in strands:
        for (x, y) in p[1:]:
            for _ in range(7):
                lx, ly = x + rng.normal(0, 22), y + rng.normal(0, 22)
                if not (40 < lx < Tl - 40 and 40 < ly < Tl - 40): continue
                fx, fy = lx / Tl, ly / Tl
                if shape == "climb":   # a tall blob: full at the bottom, thinning and narrowing up top
                    keep = (1.1 - abs(fx - 0.5) * 2 / (0.85 + 0.15 * fy)) * (0.45 + 0.55 * fy)
                elif shape == "wide":
                    keep = 1 - ((fx - 0.5) / 0.48) ** 2 - ((fy - 0.62) / 0.42) ** 2
                else:
                    keep = 1 - fy * 0.9
                if rng.random() < min(1.0, keep * 1.15):
                    leaves.append((lx, ly))
    rng.shuffle(leaves)
    for k, (lx, ly) in enumerate(leaves):
        layer = k / max(len(leaves), 1)          # later leaves sit on top: lighter
        col = LEAF_GREENS[rng.integers(len(LEAF_GREENS))] * (0.9 + 0.2 * rng.random())
        size = rng.uniform(30, 50)
        ang = rng.normal(math.pi / 2 if shape == "hang" else -math.pi / 2, 0.9)
        stamp(lx, ly, size, ang, col, 0.62 + 0.55 * layer, tile_xy)   # (deep underneath, lit on top: layered)
    return len(leaves)

def moss(tile_xy):
    # a fringe: dense along the top third, ragged tufts hanging below
    n = 0
    for _ in range(9000):
        x = rng.uniform(30, Tl - 30)
        hang = 0.25 + 0.45 * (0.5 + 0.5 * math.sin(x * 0.021)) * (0.5 + 0.5 * math.sin(x * 0.0071 + 1.3))
        y = Tl * rng.uniform(0.08, 0.08 + hang * rng.random() ** 0.6)
        depth = (y / Tl - 0.08) / max(hang, 0.05)
        stamp(x, y, rng.uniform(10, 22), rng.uniform(0, 6.3), MOSS[rng.integers(len(MOSS))], 1.05 - 0.35 * depth, tile_xy, kind="blob")
        n += 1
    return n

tiles = {"climb": (0, 0), "wide": (Tl, 0), "hang": (0, Tl), "moss": (Tl, Tl)}   # (x, y) of each tile's top-left in the picture
for k in ("climb", "wide", "hang"): log("ivy", k, "leaves", ivy(tiles[k], k))
log("moss blobs", moss(tiles["moss"]))
# colour bleed under the transparent texels (no dark halo when mipmapped): spread the painted colour outward
rgb, al = img[..., :3].copy(), img[..., 3].copy()
filled = al > 0.02
for _ in range(24):
    acc = np.zeros_like(rgb); cnt = np.zeros(al.shape, np.float32)
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        sh = np.roll(np.roll(rgb, dy, 0), dx, 1); sf = np.roll(np.roll(filled, dy, 0), dx, 1)
        acc += sh * sf[..., None]; cnt += sf
    new = (~filled) & (cnt > 0)
    rgb[new] = acc[new] / cnt[new][..., None]; filled |= new
img[..., :3] = rgb
pic = bpy.data.images.new("T_TL_Foliage", S, S, alpha=True)
pic.colorspace_settings.name = "Non-Color"
pic.pixels.foreach_set(np.ascontiguousarray(img[::-1]).ravel())
pic.filepath_raw = os.path.join(out, "T_TL_Foliage.png"); pic.file_format = "PNG"; pic.save()
log("atlas written", "alpha cover %.0f%%" % (100 * float((img[..., 3] > 0.5).mean())))

# ============================================================================================================================
# 2. The pieces (built in cm, exported as building_clean.py exports: Unreal reads the same numbers in cm, its Y = -Blender Y).
#    Every piece: its pivot at the bottom middle where it meets the wall or the ground, its face out toward Blender -Y (Unreal +Y).
# ============================================================================================================================
def export(obj, name, colours=False):
    for o in bpy.data.objects: o.select_set(False)
    obj.select_set(True); bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=os.path.join(out, name + ".fbx"), use_selection=True, object_types={"MESH"},
                             apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True, axis_forward="-Y", axis_up="Z",
                             mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, embed_textures=False,
                             colors_type="SRGB" if colours else "NONE")
    log("piece", name, "tris", sum(len(p.vertices) - 2 for p in obj.data.polygons))

def new_obj(name, bm):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    if len(me.color_attributes):   # (the FBX carries the active colour layer: the moss and stones are painted in it)
        me.color_attributes.active_color = me.color_attributes[0]; me.color_attributes.render_color_index = 0
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    for p in me.polygons: p.use_smooth = True
    return o

def card(name, tile, w, h, hang=False):
    # two layers of a gently bulged grid on one tile: the back one 2 cm off the wall, a smaller front one 7 cm off, mirrored
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new("UV")
    tx, ty = tiles[tile]
    for layer, (off, sc, mir) in enumerate(((2.0, 1.0, False), (7.0, 0.8, True), (12.0, 0.6, False))):   # (three layers: fuller, townlab review)
        nx, nz = 4, 6
        grid = []
        for j in range(nz + 1):
            row = []
            for i in range(nx + 1):
                fu, fv = i / nx, j / nz
                x = (fu - 0.5) * w * sc
                z = (h - fv * h * sc) if hang else fv * h * sc
                bulge = 4.0 * math.sin(math.pi * fu) * math.sin(math.pi * fv)
                row.append((bm.verts.new((x, -(off + bulge), z)), fu, fv))
            grid.append(row)
        for j in range(nz):
            for i in range(nx):
                q = [grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]]
                f = bm.faces.new([v for v, _, _ in q])
                for loop, (_, fu, fv) in zip(f.loops, q):
                    u = 1 - fu if mir else fu
                    # tile rows: the picture's top row is v = 1; a climbing tile's bottom (fv 0) is its picture bottom
                    py = (1 - fv) if not hang else fv                      # 0 = top of the tile, 1 = bottom (in picture rows)
                    loop[uvl].uv = ((tx + u * Tl) / S, 1 - (ty + py * Tl) / S)
    bm.normal_update()
    for f in bm.faces:
        if f.normal.y > 0: f.normal_flip()
    return new_obj(name, bm)

CARDS = {"TL_IvyClimb": ("climb", 150.0, 260.0, False), "TL_IvyWide": ("wide", 220.0, 170.0, False),
         "TL_IvyHang": ("hang", 170.0, 140.0, True), "TL_MossFringe": ("moss", 120.0, 40.0, True)}
for n, (tile, w, h, hang) in CARDS.items(): export(card(n, tile, w, h, hang), n)

def blob(name, count, size, flat, cols, seed, spread):
    # a few rounded lumps (moss cushions, or a little pile of stones) painted per lump in the vertex colour, darker underneath
    r = random.Random(seed)
    bm = bmesh.new(); cl = bm.loops.layers.color.new("Col")
    for k in range(count):
        s = size * r.uniform(0.6, 1.0)
        res = bmesh.ops.create_icosphere(bm, subdivisions=2, radius=s)
        cx, cy = r.uniform(-spread, spread), r.uniform(-spread * 0.5, spread * 0.5)
        col = cols[r.randrange(len(cols))]
        for v in res["verts"]:
            n = v.co.normalized()
            v.co.x = v.co.x * r.uniform(0.95, 1.05) + cx
            v.co.y = v.co.y * r.uniform(0.95, 1.05) + cy
            v.co.z = max(v.co.z * flat + s * flat * 0.55, 0.0)
        for f in {f for v in res["verts"] for f in v.link_faces}:
            for loop in f.loops:
                up = loop.vert.normal.z if loop.vert.normal.length > 0 else 0.0
                k2 = 0.72 + 0.32 * max(up, 0.0)
                loop[cl] = (col[0] * k2, col[1] * k2, col[2] * k2, 1.0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)   # (the squashed lumps must face out, or the sun lights their insides)
    return new_obj(name, bm)

export(blob("TL_MossClump", 9, 18.0, 0.22, [(0.44, 0.57, 0.17), (0.52, 0.63, 0.20), (0.36, 0.50, 0.15)], 7, 36.0), "TL_MossClump", True)

def weeds(name):
    # a leafy clump at the foot of a wall: three crossed cards on the wide ivy tile, 45 cm tall (36 triangles)
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new("UV")
    tx, ty = tiles["wide"]
    w, h = 60.0, 45.0
    for k in range(3):
        a = math.radians(k * 60.0 + 10.0); ca, sa = math.cos(a), math.sin(a)
        vs = []
        for (fu, fv) in ((0, 0), (1, 0), (1, 1), (0, 1)):
            x = (fu - 0.5) * w
            vs.append((bm.verts.new((x * ca, x * sa, fv * h + (6.0 if fv else 0.0) * 0)), fu, fv))
        f = bm.faces.new([v for v, _, _ in vs])
        for loop, (_, fu, fv) in zip(f.loops, vs):
            loop[uvl].uv = ((tx + fu * Tl) / S, 1 - (ty + (1 - fv) * Tl * 0.95) / S)
    bm.normal_update()
    return new_obj(name, bm)
export(weeds("TL_Weeds"), "TL_Weeds")
export(blob("TL_Stones", 6, 13.0, 0.6, [(0.62, 0.58, 0.52), (0.55, 0.52, 0.48), (0.68, 0.63, 0.55), (0.50, 0.50, 0.47)], 11, 22.0), "TL_Stones", True)

# ============================================================================================================================
# 3. Where they go: planned on each finished model
# ============================================================================================================================
BUILD = {   # walls rectangle (Blender frame, cm) from building_clean.py's BUILD line; front = Blender -Y (Unreal +Y), the door side
    "TL_House": dict(src="OakHillHouse_300k", wx=(-597.1, 563.0), wy=(-293.5, 399.0), door=(-150.0, 260.0), seed=3),
    "TL_Store": dict(src="OakHillStore_300k", wx=(-610.6, 561.2), wy=(-583.1, 543.5), door=(-480.0, 450.0), seed=5, corners_only=True),
}
BUILD.update({   # build-b (townbuild2, 2026-11-14); walls from building_clean.py's BUILD lines / OakHill.h's FModel rows
    "TL_TownHall": dict(src="OakHillTownHall_300k", wx=(-989.1, 985.8), wy=(-580.0, 562.0), door=(-300.0, 180.0), seed=7, ivy_min=(150.0, 220.0), no_hang=True),
    "TL_Church": dict(src="OakHillChurch_300k", wx=(-591.5, 573.5), wy=(-709.2, 723.0), door=(-130.0, 130.0), seed=9, ivy_min=(150.0, 220.0)),
    "TL_Well": dict(src="OakHillWell_150k", wx=(-152.7, 123.0), wy=(-118.6, 118.5), door=(9999.0, 9999.0), seed=11, no_ivy=True, feet=1),
    "TL_PotionShop": dict(src="OakHillPotionShop_300k", wx=(-464.4, 526.7), wy=(-408.8, 404.3), door=(-200.0, 0.0), seed=13, ivy_min=(120.0, 200.0)),
})
# The filler props measured on their files (Art/Filler/Clean: bottom centre, front Blender -Y; sizes in cm as drawn at scale 1)
FILLER = ["Crate", "CrateNoLid", "GrainSack", "WheatSack", "Barrel", "Firewood", "LogStackSmall", "PotRound", "PotSmall", "PlantClump",
          "WateringCan", "Pail", "LanternBlack", "LanternOil", "Basket", "ShrubLow", "BushYellowFlowers"]
fsize = {}
for f in FILLER:
    path = os.path.join(proj, "Art", "Filler", "Clean", f + ".glb")
    if not os.path.exists(path): log("missing filler", f); continue
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    pts = np.array([list(o.matrix_world @ v.co) for o in new for v in o.data.vertices]) * 100.0
    fsize[f] = pts.max(0) - pts.min(0)
    for o in [o for o in bpy.data.objects if o not in before]: bpy.data.objects.remove(o, do_unlink=True)
log("filler sizes", {k: [int(x) for x in v] for k, v in fsize.items()})

def to_ue(p): return (round(float(p[0]), 1), round(float(-p[1]), 1), round(float(p[2]), 1))
def yaw_for(out_b):
    # the yaw (Unreal degrees) that turns a piece's front (Unreal local +Y) to face along out_b (a Blender-frame direction)
    nx, ny = out_b[0], -out_b[1]
    return round(math.degrees(math.atan2(-nx, ny)), 1)

plans = {}
for bname, B in BUILD.items():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.join(work, B["src"] + ".fbx"))
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    if (co.max(0) - co.min(0)).max() < 100: co *= 100.0; me.vertices.foreach_set("co", co.ravel()); me.update()
    top = float(co[:, 2].max())
    bvh = BVHTree.FromObject(o, bpy.context.evaluated_depsgraph_get())
    # windows: faces marked G = 1 by building_clean.py, joined into rectangles per wall side
    wins = []
    if me.color_attributes:
        attr = me.color_attributes[0]
        lc = np.empty(len(me.loops) * 4, np.float32); attr.data.foreach_get("color", lc); lc = lc.reshape(-1, 4)
        ls = np.empty(len(me.polygons), np.int64); me.polygons.foreach_get("loop_start", ls)
        cen = np.empty(len(me.polygons) * 3); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3)
        wf = cen[lc[ls, 1] > 0.5]
        cells = {}
        for c in wf: cells.setdefault(tuple((c // 40).astype(int)), []).append(c)
        seen = set()
        for k in cells:
            if k in seen: continue
            stack, mem = [k], []
            seen.add(k)
            while stack:
                q = stack.pop(); mem += cells[q]
                for d in [(a, b, cc) for a in (-1, 0, 1) for b in (-1, 0, 1) for cc in (-1, 0, 1)]:
                    r2 = (q[0] + d[0], q[1] + d[1], q[2] + d[2])
                    if r2 in cells and r2 not in seen: seen.add(r2); stack.append(r2)
            m = np.array(mem); wins.append((m.min(0) - 45.0, m.max(0) + 45.0))   # (with the frame and shutters round the glass)
    log(bname, "windows", len(wins), "top", round(top))
    rnd = random.Random(B["seed"])
    items = []
    x0, x1 = B["wx"]; y0, y1 = B["wy"]
    # the four walls: (name, start, along, out); u runs along the wall from its left end as seen from outside
    sides = [("front", Vector((x0, y0, 0)), Vector((1, 0, 0)), Vector((0, -1, 0)), x1 - x0),
             ("back", Vector((x1, y1, 0)), Vector((-1, 0, 0)), Vector((0, 1, 0)), x1 - x0),
             ("left", Vector((x0, y1, 0)), Vector((0, -1, 0)), Vector((-1, 0, 0)), y1 - y0),
             ("right", Vector((x1, y0, 0)), Vector((0, 1, 0)), Vector((1, 0, 0)), y1 - y0)]
    def surface(p_line, outv, z):
        # the outermost surface of the building at this spot and height, cast in from 4 m out (None: nothing there)
        hit = bvh.ray_cast(p_line + outv * 400.0 + Vector((0, 0, z)), -outv, 800.0)
        return None if hit[0] is None else hit[0]
    def wall_profile(p_line, outv):
        # the wall's face (median depth out from the line over 60-250 cm) and how high it runs clear before the eaves stick out
        ds = []
        for z in range(60, 260, 20):
            h = surface(p_line, outv, z)
            if h is not None: ds.append((h - p_line).dot(outv))
        if len(ds) < 4: return None, 0
        face = float(np.median(ds))
        clear = 60
        for z in range(60, int(top), 10):
            h = surface(p_line, outv, z)
            if h is None or (h - p_line).dot(outv) > face + 22.0: break
            clear = z
        return face, clear
    def free_rect(side, u0, u1, z0, z1):
        sname, start, along, outv, L = side
        for lo_, hi_ in wins:
            pa = start + along * u0; pb = start + along * u1
            ax0, ax1 = min(pa.x, pb.x) - 5, max(pa.x, pb.x) + 5; ay0, ay1 = min(pa.y, pb.y) - 5, max(pa.y, pb.y) + 5
            # the window must be on this wall (within 1 m of its line) and overlap the rectangle
            near = abs((Vector(((lo_[0] + hi_[0]) / 2, (lo_[1] + hi_[1]) / 2, 0)) - start).dot(outv)) < 100.0
            if near and lo_[0] < ax1 and hi_[0] > ax0 and lo_[1] < ay1 and hi_[1] > ay0 and lo_[2] < z1 and hi_[2] > z0: return False
        return True
    def flat(side, u, w, h, face):
        # the wall under a card must be one face: no door or window opening sunk into it, nothing sticking far out (a sign, a lean-to)
        sname, start, along, outv, L = side
        for uu in np.linspace(u - w / 2 + 8, u + w / 2 - 8, 6):
            p0 = start + along * float(uu)
            for z in range(30, int(h), 25):
                hit = surface(p0, outv, float(z))
                if hit is None: return False
                d = (hit - p0).dot(outv)
                if d < face - 12.0 or d > face + 30.0: return False
        return True
    def add(kind, mesh, pos_b, out_b, scale, with_building, light=False, roll=0.0, pitch=0.0, normal=None):
        items.append(dict(kind=kind, mesh=mesh, pos=to_ue(pos_b), yaw=yaw_for(out_b), pitch=pitch, roll=roll, scale=round(scale, 2),
                          wb=with_building, light=light, n=(0.0, 0.0, 0.0) if normal is None else (round(normal.x, 3), round(-normal.y, 3), round(normal.z, 3))))
    # --- ivy: at corners and on blank stretches, climbing from the ground as high as the wall runs clear
    n_ivy = 0
    for side in sides:
        sname, start, along, outv, L = side
        cands = [] if B.get("no_ivy") else [0.55 * 100, L - 0.55 * 100] + ([] if B.get("corners_only") else [rnd.uniform(150, L - 150) for _ in range(6)])
        rnd.shuffle(cands)
        placed = []
        for u in cands:
            if any(abs(u - p) < 170 for p in placed): continue
            pl = start + along * u
            if sname == "front" and B["door"][0] - 80 < pl.x < B["door"][1] + 80: continue
            face, clear = wall_profile(pl, outv)
            if face is None or clear < 160: continue
            for w in [x for x in (190.0, 150.0, 120.0, 95.0) if x >= B.get("ivy_min", (0.0, 140.0))[0]]:   # (ivy_min: (width, height) the smallest ivy a building takes)
                h = min(clear - 10.0, 260.0)
                while h >= B.get("ivy_min", (0.0, 140.0))[1] and not (free_rect(side, u - w / 2, u + w / 2, 0, h) and flat(side, u, w, h, face)): h -= 20
                if h >= B.get("ivy_min", (0.0, 140.0))[1]: break
            if h < B.get("ivy_min", (0.0, 140.0))[1]: continue
            mesh = "TL_IvyClimb"
            add("ivy", mesh, pl + outv * face, outv, round(min(w / 150.0, h / 260.0) * 1.0, 2) if h < 255 else w / 150.0, True)
            items[-1]["scale3"] = (round(w / 150.0, 2), 1.0, round(h / 260.0, 2))
            placed.append(u); n_ivy += 1
            if len(placed) >= (3 if sname in ("left", "right") else 2): break
        # a hanging tress from the eaves over one clear spot per long wall
        if sname in ("front", "back") and not B.get("no_ivy") and not B.get("no_hang"):
            for _ in range(8):
                u = rnd.uniform(120, L - 120); pl = start + along * u
                if sname == "front" and B["door"][0] - 60 < pl.x < B["door"][1] + 60: continue
                face, clear = wall_profile(pl, outv)
                if face is None or clear < 200: continue
                if not free_rect(side, u - 90, u + 90, clear - 140, clear + 20): continue
                add("ivy", "TL_IvyHang", pl + outv * face + Vector((0, 0, clear - 140.0)), outv, 1.0, True)
                break
    # --- moss: cushions along the roof just above each long wall's eave line, and fringes hanging off the eaves' edge
    # (off, 2026-11-14: in game the clumps read as round green stickers on the thatch and landed on the store's awning; the
    #  baked colour already carries the model's own moss along the ridge. Set moss=True on a building to try them again.)
    n_moss = 0
    for side in (sides if B.get("moss", False) else []):
        sname, start, along, outv, L = side
        u = rnd.uniform(40, 140)
        while u < L - 40:
            pl = start + along * u
            for outk in (40.0, 15.0, -10.0):
                hit = bvh.ray_cast(pl + outv * outk + Vector((0, 0, top + 200)), Vector((0, 0, -1)), top + 400)
                if hit[0] is not None and hit[0].z > 180 and hit[1].z > 0.25: break
            if hit[0] is not None and hit[0].z > 180 and hit[1].z > 0.25:
                nrm = hit[1]
                pitch = -math.degrees(math.acos(max(min(nrm.z, 1.0), -1.0)))   # tilted with the roof, front edge down the slope
                down = Vector((nrm.x, nrm.y, 0)).normalized() if Vector((nrm.x, nrm.y)).length > 0.05 else outv
                add("moss", "TL_MossClump", hit[0] - Vector((0, 0, 2.0)), down, rnd.uniform(0.7, 1.3), True, normal=nrm)
                n_moss += 1
            u += rnd.uniform(110, 210)
    # --- stones and weeds at the foot of the walls, mostly near the corners
    for side in sides:
        sname, start, along, outv, L = side
        for u in (rnd.uniform(20, 90), L - rnd.uniform(20, 90), rnd.uniform(150, L - 150))[:B.get("feet", 3)]:   # (feet: how many spots per wall)
            pl = start + along * u
            if sname == "front" and B["door"][0] - 60 < pl.x < B["door"][1] + 60: continue
            h = surface(pl, outv, 15.0)
            face = (h - pl).dot(outv) if h is not None else 0.0
            add("stones", "TL_Stones", pl + outv * (face + 12.0), outv, rnd.uniform(0.7, 1.2), False)
            if rnd.random() < 0.6:
                add("weeds", "TL_Weeds", pl + outv * (face + 22.0) + along * rnd.uniform(-40, 40), outv, rnd.uniform(0.8, 1.2), False)
    plans[bname] = (items, bvh, sides, B, top, surface)
    log(bname, "ivy", n_ivy, "moss", n_moss, "items so far", len(items))

    # --- props against the walls (filler pieces, drawn at their own size: about real size next to the knight)
    def prop(sname, u, mesh, scale=0.85, gap=6.0, turn=0.0, dz=0.0, sidestep=0.0):
        side = next(s for s in sides if s[0] == sname)
        _, start, along, outv, L = side
        pl = start + along * u
        face = None
        for z in (10.0, 40.0, 80.0):
            h = surface(pl, outv, z)
            if h is not None:
                d = (h - pl).dot(outv); face = d if face is None else max(face, d)
        depth = fsize.get(mesh, np.array([60.0, 60.0, 60.0]))
        off = (face or 0.0) + gap + float(max(depth[0], depth[1])) * 0.5 * scale
        p = pl + outv * off + along * sidestep + Vector((0, 0, dz))
        out_t = outv.copy(); out_t.rotate(__import__("mathutils").Euler((0, 0, math.radians(turn))))
        items.append(dict(kind="prop", mesh="/Game/Filler/SM_" + mesh, pos=to_ue(p), yaw=yaw_for(out_t), pitch=0.0, roll=0.0,
                          scale=scale, wb=False, light=False))
        return p
    if bname == "TL_House":
        prop("left", 120, "Firewood", 0.9, turn=90)
        prop("back", 140, "WateringCan", 0.8, turn=-30)
        prop("back", 210, "PotRound", 0.9); prop("back", 250, "PotSmall", 0.9, sidestep=12)
        prop("right", 520, "Pail", 0.85, turn=20)
        # the porch lanterns building_clean.py took off (LANTERNS: its two porch posts), hung back as separate lights
        for (bx, by, bz) in ((-89.0, -430.0, 146.0), (182.0, -430.0, 146.0)):
            items.append(dict(kind="lantern", mesh="/Game/Filler/SM_LanternBlack", pos=to_ue((bx, by, bz)), yaw=0.0, pitch=0.0, roll=0.0,
                              scale=0.8, wb=True, light=True))
    elif bname == "TL_Store":
        prop("left", 160, "Crate", 0.85, turn=8)
        prop("left", 160, "CrateNoLid", 0.75, dz=fsize.get("Crate", np.array([0, 0, 72.0]))[2] * 0.85, turn=-12)
        prop("left", 270, "GrainSack", 0.85, turn=25); prop("left", 330, "WheatSack", 0.8, turn=-15)
        prop("back", 200, "Barrel", 0.85); prop("back", 290, "Basket", 0.8, turn=40)
        prop("right", 260, "WateringCan", 0.8, turn=-40)
        items.append(dict(kind="lantern", mesh="/Game/Filler/SM_LanternOil", pos=to_ue((265.0, -358.0, 183.0)), yaw=0.0, pitch=0.0, roll=0.0,
                          scale=0.85, wb=True, light=True))
    # build-b (townbuild2, 2026-11-14): the town hall, church, well and potion shop
    elif bname == "TL_TownHall":
        prop("left", 260, "Barrel", 0.85); prop("left", 360, "Crate", 0.85, turn=10)
        prop("back", 600, "LogStackSmall", 0.9, turn=90); prop("back", 1300, "PotRound", 0.9)
        prop("right", 300, "BushYellowFlowers", 0.9); prop("right", 820, "PotSmall", 0.9)
    elif bname == "TL_Church":
        prop("left", 400, "ShrubLow", 0.9); prop("right", 900, "ShrubLow", 0.9, turn=40)
        prop("back", 300, "PotRound", 0.9); prop("back", 820, "BushYellowFlowers", 0.9)
        # the lantern building_clean.py took off left of the door (LANTERNS), hung back as a light
        items.append(dict(kind="lantern", mesh="/Game/Filler/SM_LanternBlack", pos=to_ue((-170.0, -658.0, 182.0)), yaw=0.0, pitch=0.0, roll=0.0,
                          scale=0.8, wb=True, light=True))
    elif bname == "TL_Well":
        prop("right", 120, "Pail", 0.8, turn=25)
    elif bname == "TL_PotionShop":
        prop("left", 200, "PlantClump", 0.9); prop("left", 330, "PotSmall", 0.9, turn=20)
        prop("back", 250, "Crate", 0.8, turn=-10); prop("back", 360, "Basket", 0.8, turn=30)
        prop("right", 420, "PotRound", 0.9)
    log(bname, "pieces", len(items))

# ============================================================================================================================
# 4. The header
# ============================================================================================================================
H = []
H.append("// TownLab decorations per building (generated by Tools/townlab_decor.py, 2026-11-14; edit that script, not this file).")
H.append("// Every piece sits in its building's own frame: X along its length, Y with the door side at +Y, Z up, cm, the model's")
H.append("// floor at Z 0, as /Game/TownLab/Buildings/SM_TL_<Name> is drawn before its FModel Scale (OakHill.h). Place():")
H.append("//   bScaleWithBuilding (ivy, moss, the lanterns): the building's whole transform, Scale included, as if attached to it;")
H.append("//   otherwise (props, stones, weeds) the spot moves with the building's Scale but the piece keeps its own Scale (real size).")
H.append("// Mesh: a full asset path, or a name in /Game/TownLab/Buildings. bLight: a spot for a real light (lanterns, about 25 cm below).")
H.append("// bCastShadow: false for the small pieces and the ivy and moss cards (speed rules, Docs/TownLab/StyleSheet.md).")
H.append("#pragma once")
H.append("#include \"CoreMinimal.h\"")
H.append("")
H.append("namespace JeebTownLabDecor")
H.append("{")
H.append("\tstruct FDecor { const TCHAR* Mesh; float X, Y, Z, Yaw, Pitch, Roll; float SX, SY, SZ; bool bScaleWithBuilding; bool bLight; bool bCastShadow; float NX = 0.f, NY = 0.f, NZ = 0.f; };")
H.append("\tstruct FBuildingDecor { const TCHAR* Building; const FDecor* Pieces; int32 Num; };")
H.append("")
for bname, (items, *_r) in plans.items():
    H.append("\tinline const FDecor %sDecor[] = {" % bname[3:])
    for it in items:
        mesh = it["mesh"] if it["mesh"].startswith("/Game") else "/Game/TownLab/Buildings/SM_" + it["mesh"]
        mesh = mesh + "." + mesh.rsplit("/", 1)[1]
        s3 = it.get("scale3", (it["scale"],) * 3)
        if "scale3" not in it: s3 = (it["scale"],) * 3
        shadow = it["kind"] in ("prop", "lantern")
        H.append("\t\t{ TEXT(\"%s\"), %.1ff, %.1ff, %.1ff, %.1ff, %.1ff, %.1ff, %.2ff, %.2ff, %.2ff, %s, %s, %s%s },   // %s"
                 % (mesh, it["pos"][0], it["pos"][1], it["pos"][2], it["yaw"], it["pitch"], it["roll"], s3[0], s3[1], s3[2],
                    "true" if it["wb"] else "false", "true" if it["light"] else "false", "true" if shadow else "false", "" if not any(it.get("n", (0, 0, 0))) else ", %.3ff, %.3ff, %.3ff" % it["n"], it["kind"]))
    H.append("\t};")
H.append("\tinline const FBuildingDecor Buildings[] = {")
for bname in plans: H.append("\t\t{ TEXT(\"%s\"), %sDecor, UE_ARRAY_COUNT(%sDecor) }," % (bname, bname[3:], bname[3:]))
H.append("\t};")
H.append("")
H.append("\t// Where a piece goes in the world, given its building's transform (location, turn, and Scale as the building is drawn)")
H.append("\tinline FTransform Place(const FTransform& Building, const FDecor& D)")
H.append("\t{")
H.append("\t\tconst FVector BS = Building.GetScale3D();")
H.append("\t\tconst FVector Spot = Building.TransformPosition(FVector(D.X, D.Y, D.Z));")
H.append("\t\t// (NX..NZ, when set: the surface the piece sits on, its up axis turned onto it, its front kept as near Yaw as the surface allows)")
H.append("\t\tconst FQuat Local = (D.NX == 0.f && D.NY == 0.f && D.NZ == 0.f) ? FRotator(D.Pitch, D.Yaw, D.Roll).Quaternion()")
H.append("\t\t\t: FRotationMatrix::MakeFromZY(FVector(D.NX, D.NY, D.NZ), FRotator(0.f, D.Yaw, 0.f).RotateVector(FVector::YAxisVector)).ToQuat();")
H.append("\t\tconst FQuat Turn = Building.GetRotation() * Local;")
H.append("\t\tconst FVector Own(D.SX, D.SY, D.SZ);")
H.append("\t\treturn FTransform(Turn, Spot, D.bScaleWithBuilding ? Own * BS : Own);")
H.append("\t}")
H.append("\tinline const FBuildingDecor* For(const TCHAR* Building)   // \"TL_House\" or \"TL_Store\"")
H.append("\t{")
H.append("\t\tfor (const FBuildingDecor& B : Buildings) if (FCString::Stricmp(B.Building, Building) == 0) return &B;")
H.append("\t\treturn nullptr;")
H.append("\t}")
H.append("}")
with open(os.path.join(proj, "Source", "Jeebsland", "TownLabDecor.h"), "w", newline="\n") as f: f.write("\n".join(H) + "\n")
log("header written", sum(len(p[0]) for p in plans.values()), "pieces")
