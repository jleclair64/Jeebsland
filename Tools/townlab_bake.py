# TownLab buildings (2026-11-14, owner: "Make it look professional and keep the framerate good ... keep the focus the artistic
# style of those pictures", Docs/TownLab/StyleSheet.md, the Overwatch Eichenwalde streets in Docs/TownLab/Refs/ref_01-03).
# A game-ready low model of a town building, its detail baked from the finished 300k model:
#   blender -b -P Tools/townlab_bake.py -- <work> <out> <Name> <NewName> <triangles> [--size 4096] [--preview]
# <work> is Saved/KnightBuild/Buildings after building_clean.py + building_flatten.py (Tools\BuildBuildings.ps1 -CleanOnly <Name>):
# <Name>.fbx (the high model, its window marks in the vertex colour) and T_<Name>_Base/Normal/MR/Glow.png.
# Writes to <out>: <NewName>.fbx (the low model, no vertex colours), T_<NewName>_Base.png (hand-painted look, below),
# T_<NewName>_Normal.png (the high model's shape and its normal map, baked), T_<NewName>_MR.png (G roughness, B metal),
# T_<NewName>_Glow.png and T_<NewName>_Mask.png (the window marks of building_clean.py's vertex colour, as a picture: R a random
# number per window, G 1 on a window, B 1 where it never glows; M_TL_Building reads it instead of the vertex colour, so the low
# model needs none) and, with --preview, <NewName>_preview.png (old and new side by side in Blender).
#
# The painted look (Overwatch's Eichenwalde): each colour group graded warmer and a little richer (GRADE), the high model's ambient
# occlusion painted softly into the colour, warm (AO_*), bevelled edges picked out a little lighter as a painter does (EDGE_*), and
# a soft top-to-bottom gradient (lighter up top, a touch deeper and cooler at the base: GRAD_*). No grime, no dirt.
import bpy, bmesh, sys, os, math, time
import mathutils, mathutils.kdtree
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
SIZE = 4096
PREVIEW = "--preview" in a
if "--preview" in a: a.remove("--preview")
if "--size" in a:
    k = a.index("--size"); SIZE = int(a[k + 1]); del a[k:k + 2]
work, out, name, new, budget = a[0], a[1], a[2], a[3], int(a[4])
os.makedirs(out, exist_ok=True)
SMALL = SIZE // 2           # masks, glow, roughness and the occlusion bake
MARGIN = 16                 # bake bleed, texels
CAGE = 4.0                  # cm: how far out from the low model the bake rays start, and how far they look
MAX_RAY = 10.0
SHARP_DEG = 40.0            # the low model's hard edges

# ---- the painted look's numbers -------------------------------------------------------------------------------------------
CHROMA_GAIN = 1.15          # all colour a little richer
# colour groups in Lab (L lightness, C chroma, h hue degrees): (name, test, dL, da, db, chroma x)
GRADE = {
    "OakHillHouse_300k": dict(plaster=(2.0, 1.5, 6.0, 1.0),    # warm cream plaster, clearly lighter and paler than the thatch (townlab review: too close to it)
                             wood=(-4.0, 1.0, -3.0, 1.05),    # rich brown, less orange
                             thatch=(-9.0, 2.0, -2.0, 0.9),    # deeper golden-brown straw, not orange, set apart from the plaster
                             stone=(2.0, 1.0, 5.0, 1.0)),     # warm grey-beige stone
    "OakHillStore_300k": dict(plaster=(2.0, 2.0, 7.0, 1.0),
                             wood=(3.0, 1.5, -1.0, 1.1),
                             thatch=(-2.0, -2.5, 1.0, 0.95),
                             stone=(2.0, 0.5, 3.0, 1.0)),
    # build-c's stalls (2026-11-14): each keeps its own colours (the market stall's red-and-cream striped roof and goods, the potion
    # stall's dark timber), only nudged toward the town's rich brown wood and warm cream
    "GeneralStore_300k": dict(plaster=(1.0, 1.0, 3.0, 1.0),
                             wood=(-2.0, -2.5, 3.5, 1.0),     # rich brown: the red-orange plank back wall pulled toward the front timber (review)
                             thatch=(0.0, 0.0, 0.0, 1.0),
                             stone=(1.0, 0.5, 2.0, 1.0)),
    "PotionStall_300k": dict(plaster=(1.0, 1.0, 3.0, 1.0),
                             wood=(7.0, 1.5, 3.0, 1.1),       # its dark timber lifted and warmed a touch so it reads as rich brown wood, not grey-purple
                             thatch=(0.0, 0.0, 0.0, 1.0),
                             stone=(4.0, 1.5, 4.0, 1.0)),     # (its greyest timber falls in this group: warmed the same way; it has no real stone)
}
# build-b (townbuild2, 2026-11-14): the town hall, church, well and potion shop. Their roofs are red clay tiles, not thatch: an optional
# "tile" group (saturated red-brown above tile_z cm, the roof's eave height) graded to a warm terracotta, taken out of the wood group there.
GRADE.update({
    "OakHillTownHall_300k":   dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(2.0, 1.0, 5.0, 1.0), tile=(3.0, -1.0, 6.0, 0.95), tile_z=560.0),
    "OakHillChurch_300k":     dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(3.0, 1.0, 5.0, 1.0), tile=(4.0, -1.5, 7.0, 0.95), tile_z=260.0, fire_as_window=True,
                                   paint_out=[(-700.0, -150.0, -700.0, 300.0, 330.0, 800.0)]),
    "OakHillWell_150k":       dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(3.0, 1.0, 5.0, 1.0), tile=(4.0, -1.5, 7.0, 0.95), tile_z=170.0),
    "OakHillPotionShop_300k": dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(2.0, 1.0, 5.0, 1.0), tile=(3.0, -1.0, 6.0, 0.95), tile_z=500.0),
})
TILE_TARGET = (25.4, 22.7, 25.5)       # (build-b) the town's terracotta in Lab: every "tile" group is shifted to this mean (None: the fixed offsets)
MOSS_GRADE = (-6.0, 6.0, 10.0, 0.3)   # green moss: pulled most of the way into the straw (a faint olive-brown tinge: the review read brighter olive as patches)
AO_STRENGTH = 0.42          # how much of the occlusion darkens the colour
# (build-c 2026-11-14, visual review: the stalls are open boxes under a roof, so the high model's occlusion is deep everywhere inside
# and on the two-layer cloth roof; at 0.42 it turned the cream stripes peach, the lining and goods muddy and the potion stall's back
# logs near black. Per design: (how much darkens the colour, how much of it reaches the material's AO channel) and the chroma gain.)
AO_BY = {"GeneralStore_300k": (0.12, 0.4), "PotionStall_300k": (0.18, 0.5)}
CHROMA_BY = {"GeneralStore_300k": 1.0, "PotionStall_300k": 1.08}
AO_TINT = np.array([0.55, 0.38, 0.30], np.float32)   # what deep occlusion tints toward (warm brown, never black)
AO_BLUR = 2                 # texels at SMALL
EDGE_GAIN = 0.10            # convex edges this much lighter
EDGE_BLUR = 2
GRAD_TOP, GRAD_BOTTOM = 1.04, 0.90   # lightness at the top of the building and at its foot
GRAD_FOOT_TINT = np.array([0.94, 0.98, 0.92], np.float32)  # a breath of green-grey at the foot (moss, earth)

t0 = time.time()
def log(*x): print("TLBAKE", *x, flush=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
try:
    bpy.ops.import_scene.fbx(filepath=os.path.join(work, name + ".fbx"))
except Exception:
    bpy.ops.wm.fbx_import(filepath=os.path.join(work, name + ".fbx"))
hi = next(o for o in bpy.data.objects if o.type == "MESH")
bpy.context.view_layer.objects.active = hi; hi.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
co = np.empty(len(hi.data.vertices) * 3); hi.data.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
lo_b, hi_b = co.min(0), co.max(0)
U = 1.0   # cm per Blender unit, worked out from the BUILD line's height (the FBX may come in metres)
ext = hi_b - lo_b
U = 100.0 if ext.max() < 100.0 else 1.0
log(name, "high tris", sum(len(p.vertices) - 2 for p in hi.data.polygons), "extent", [round(float(x * U), 1) for x in ext], "cm/unit", U)
cage, max_ray = CAGE / U, MAX_RAY / U
# (build-c 2026-11-14: the market stall's roof is a thin two-layer cloth; the reduction sinks the low roof under its top layer, so 4 cm
# rays started under it and baked the striped underside and its full occlusion as dark red blotches. Its rays start further out.)
CAGE_BY = {"GeneralStore_300k": (12.0, 24.0)}
if name in CAGE_BY: cage, max_ray = CAGE_BY[name][0] / U, CAGE_BY[name][1] / U

def load(path, colour):
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = "sRGB" if colour else "Non-Color"
    return img

T = lambda k: os.path.join(work, "T_%s_%s.png" % (name, k))
base_img, nrm_img, mr_img = load(T("Base"), True), load(T("Normal"), False), load(T("MR"), False)
glow_img = load(T("Glow"), True) if os.path.exists(T("Glow")) else None

# the high model's bake material: an emission switch (one shader, the bakes pick what it shows) and the normal map
mh = bpy.data.materials.new("High"); mh.use_nodes = True
nt = mh.node_tree; nt.nodes.clear()
N = nt.nodes.new; L = nt.links.new
outn = N("ShaderNodeOutputMaterial")
emis = N("ShaderNodeEmission")
bsdf = N("ShaderNodeBsdfPrincipled")
add = N("ShaderNodeAddShader")
def tex(img):
    t = N("ShaderNodeTexImage"); t.image = img; t.interpolation = "Linear"; return t
tb, tn, tm = tex(base_img), tex(nrm_img), tex(mr_img)
nm = N("ShaderNodeNormalMap"); L(tn.outputs["Color"], nm.inputs["Color"]); L(nm.outputs["Normal"], bsdf.inputs["Normal"])
L(emis.outputs[0], add.inputs[0]); L(bsdf.outputs[0], add.inputs[1]); L(add.outputs[0], outn.inputs["Surface"])
bsdf.inputs["Base Color"].default_value = (0.5, 0.5, 0.5, 1)
hi.data.materials.clear(); hi.data.materials.append(mh)
vcol = N("ShaderNodeVertexColor")
vcol.layer_name = hi.data.color_attributes[0].name if len(hi.data.color_attributes) else ""
geo = N("ShaderNodeNewGeometry")
def show(sock):   # what the EMIT bakes take from the high model
    for l in list(emis.inputs["Color"].links): nt.links.remove(l)
    L(sock, emis.inputs["Color"])

# ---- the low model ----------------------------------------------------------------------------------------------------------
lo = hi.copy(); lo.data = hi.data.copy(); lo.name = new; sc.collection.objects.link(lo)
me = lo.data
while me.color_attributes: me.color_attributes.remove(me.color_attributes[0])
bm = bmesh.new(); bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.01 / U)
bm.to_mesh(me); bm.free()
tris = sum(len(p.vertices) - 2 for p in me.polygons)
bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = lo; lo.select_set(True)
# Flower boxes keep their shape (townlab review 2026-11-14: "jagged, shard-like flower boxes"): the reduction spares what sits just
# under a window (the boxes and their flowers), found from the window marks (G) of the high model's vertex colour.
keep_n = 0
if len(hi.data.color_attributes):
    hm = hi.data; at = hm.color_attributes[0]
    lc = np.empty(len(hm.loops) * 4, np.float32); at.data.foreach_get("color", lc); lc = lc.reshape(-1, 4)
    ls = np.empty(len(hm.polygons), np.int64); hm.polygons.foreach_get("loop_start", ls)
    cen = np.empty(len(hm.polygons) * 3); hm.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3) * U
    wc = cen[lc[ls, 1] > 0.5]
    if len(wc):
        kd = mathutils.kdtree.KDTree(len(wc))
        for i, c in enumerate(wc): kd.insert(c, i)
        kd.balance()
        vg = lo.vertex_groups.new(name="Keep")
        for v in me.vertices:
            p = v.co * U
            for (c, _, _) in kd.find_range(p, 120.0):
                if c.z - 75.0 < p.z < c.z - 10.0 and math.hypot(c.x - p.x, c.y - p.y) < 60.0:
                    vg.add([v.index], 0.6, "REPLACE"); keep_n += 1; break   # (0.6: reduced less, never frozen: 1.0 froze 60k vertices and the house came out at 122k)
log(new, "flower-box vertices spared", keep_n)
# (build-c 2026-11-14: the market stall's striped cloth roof is overlapping slats; collapsed with the rest it tore into holes and its
# stripes smeared. Everything above the eaves is spared like the flower boxes.)
SPARE_ABOVE = {"GeneralStore_300k": (430.0, 0.8)}
if name in SPARE_ABOVE:
    z_from, wgt = SPARE_ABOVE[name]
    vg = lo.vertex_groups.get("Keep") or lo.vertex_groups.new(name="Keep")
    n0 = keep_n
    for v in me.vertices:
        if v.co.z * U > z_from: vg.add([v.index], wgt, "REPLACE"); keep_n += 1
    log(new, "roof vertices spared", keep_n - n0)
m = lo.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = budget / tris; m.use_collapse_triangulate = True
if keep_n:
    m.vertex_group = "Keep"; m.invert_vertex_group = True; m.vertex_group_factor = 3.0   # (inverted: weight 1 = spared)
bpy.ops.object.modifier_apply(modifier=m.name)
# hard edges over SHARP_DEG, smooth elsewhere
for p in me.polygons: p.use_smooth = True
try:
    me.set_sharp_from_angle(angle=math.radians(SHARP_DEG))
except Exception:
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(SHARP_DEG))
bm = bmesh.new(); bm.from_mesh(me); open_e = sum(1 for e in bm.edges if e.is_boundary); bm.free()
low_tris = sum(len(p.vertices) - 2 for p in me.polygons)
log(new, "low tris", low_tris, "open edges", open_e)
# UVs: fresh islands, packed
while me.uv_layers: me.uv_layers.remove(me.uv_layers[0])
me.uv_layers.new(name="UV")
bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.0, area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
# (build-c 2026-11-14: the market stall's thousands of small goods came out as thousands of little islands, each with its margin, and
# the big roof got about 7% of the picture: its stripes smeared in game. UV_BOOST scales the islands of the parts that matter (their
# faces' middle above a height, cm) before packing, so they get that much more of the picture; PACK_MARGIN_BY narrows the gaps.)
UV_BOOST = {"GeneralStore_300k": (430.0, 2.6)}
PACK_MARGIN_BY = {"GeneralStore_300k": 6.0}
if name in UV_BOOST:
    bpy.ops.object.mode_set(mode="OBJECT")
    z_from, k = UV_BOOST[name]
    uvd = me.uv_layers[0].data
    nl = len(me.loops)
    luv = np.empty(nl * 2, np.float32); uvd.foreach_get("uv", luv); luv = luv.reshape(-1, 2)
    lv = np.empty(nl, np.int64); me.loops.foreach_get("vertex_index", lv)
    ls = np.empty(len(me.polygons), np.int64); me.polygons.foreach_get("loop_start", ls)
    lt = np.empty(len(me.polygons), np.int64); me.polygons.foreach_get("loop_total", lt)
    cz = np.empty(len(me.polygons) * 3); me.polygons.foreach_get("center", cz); cz = cz.reshape(-1, 3)[:, 2] * U
    # islands: faces joined where they share a vertex at the same UV spot (union-find on (vertex, rounded uv) keys)
    parent = list(range(len(me.polygons)))
    def find(a):
        while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
        return a
    seen = {}
    for f in range(len(me.polygons)):
        for li in range(ls[f], ls[f] + lt[f]):
            key = (int(lv[li]), round(float(luv[li, 0]), 5), round(float(luv[li, 1]), 5))
            g = seen.setdefault(key, f)
            ra, rb = find(f), find(g)
            if ra != rb: parent[ra] = rb
    root = np.array([find(f) for f in range(len(me.polygons))])
    up = {}
    for f in range(len(me.polygons)): up.setdefault(root[f], []).append(cz[f])
    boost = {r for r, zs in up.items() if np.median(zs) > z_from}
    lf = np.repeat(np.arange(len(me.polygons)), lt)
    sel = np.isin(root[lf], list(boost))
    luv[sel] *= k
    uvd.foreach_set("uv", luv.ravel())
    log(new, "uv islands", len(up), "boosted", len(boost), "x%.1f" % k)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.pack_islands(rotate=True, margin=PACK_MARGIN_BY.get(name, MARGIN * 0.6) / SIZE)
bpy.ops.object.mode_set(mode="OBJECT")
uv = np.empty(len(me.loops) * 2); me.uv_layers[0].data.foreach_get("uv", uv)
log("uv islands packed; uv extent", round(float(uv.max()), 3))

ml = bpy.data.materials.new("M_TL_" + new); ml.use_nodes = True
me.materials.clear(); me.materials.append(ml)
lt = ml.node_tree
bake_node = lt.nodes.new("ShaderNodeTexImage"); lt.nodes.active = bake_node; bake_node.select = True

sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 1
sc.render.bake.use_selected_to_active = True
sc.render.bake.cage_extrusion = cage
sc.render.bake.max_ray_distance = max_ray
sc.render.bake.margin = MARGIN
sc.render.bake.margin_type = "EXTEND"
sc.render.bake.use_clear = True

def bake(kind, size, colour, samples=1, selected=True, **kw):
    img = bpy.data.images.new("%s_%s" % (new, kind), size, size, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Linear Rec.709" if colour else "Non-Color"
    bake_node.image = img
    sc.cycles.samples = samples
    sc.render.bake.use_selected_to_active = selected
    for o in bpy.data.objects: o.select_set(False)
    if selected: hi.select_set(True)
    lo.select_set(True); bpy.context.view_layer.objects.active = lo
    t = time.time()
    bpy.ops.object.bake(type=kw.pop("type", "EMIT"), **kw)
    p = np.empty(size * size * 4, np.float32); img.pixels.foreach_get(p)
    log("baked", kind, size, "in %.0f s" % (time.time() - t))
    return p.reshape(size, size, 4)[..., :3].copy()

def to_srgb(x):
    x = np.clip(x, 0, 1); return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1 / 2.4) - 0.055)
def to_lin(x):
    x = np.clip(x, 0, 1); return np.where(x <= 0.04045, x / 12.92, np.power((x + 0.055) / 1.055, 2.4))
def write_png(arr, path, srgb_data):
    # arr: H x W x 3, already in the file's encoding (sRGB values for colour pictures, raw values for data)
    H, W = arr.shape[:2]
    im = bpy.data.images.new(os.path.basename(path), W, H, alpha=False, float_buffer=False)
    im.colorspace_settings.name = "Non-Color"        # (the numbers go in as they are)
    rgba = np.concatenate([np.clip(arr, 0, 1), np.ones((H, W, 1), np.float32)], 2).astype(np.float32)
    im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = path; im.file_format = "PNG"; im.save()
    bpy.data.images.remove(im)
def box_blur(x, r):
    if r <= 0: return x
    for ax in (0, 1):
        c = np.cumsum(np.pad(x, [(r + 1, r) if i == ax else (0, 0) for i in range(x.ndim)], mode="edge"), axis=ax)
        n = x.shape[ax]
        x = (np.take(c, range(2 * r + 1, 2 * r + 1 + n), axis=ax) - np.take(c, range(0, n), axis=ax)) / (2 * r + 1)
    return x
def up2(x): return np.repeat(np.repeat(x, 2, 0), 2, 1)

# the emission bakes read the pictures as linear numbers: data (normal, MR, mask) go through as they are, colour as linear light
show(tb.outputs["Color"]); base_lin = bake("Base", SIZE, True)
show(tm.outputs["Color"]); mr = bake("MR", SMALL, False)
if glow_img is not None:
    tg = tex(glow_img); show(tg.outputs["Color"]); glow_lin = bake("Glow", SMALL, True)
else:
    glow_lin = np.zeros((SMALL, SMALL, 3), np.float32)
show(vcol.outputs["Color"]); mask = bake("Mask", SMALL, False)
if GRADE.get(name, {}).get("fire_as_window"):   # (build-b: the church's stained glass came out as "fire" (0,0,0) and stayed painted orange at noon: make it windows)
    fire = (glow_lin.max(-1) > 0.05) & (mask[..., 1] < 0.5) & (mask[..., 2] < 0.5)
    mask[fire] = (0.5, 1.0, 0.0); log("fire texels made windows", int(fire.sum()))
mask[..., 2] = np.maximum(mask[..., 2], (mask[..., 1] < 0.5).astype(mask.dtype))   # (anything not a window never glows: the bake's empty background and island edges must not read as the forge's (0,0,0))
# convexity: the high model's pointiness (0.5 flat, above convex)
show(geo.outputs["Pointiness"]); point = bake("Edge", SMALL, False)[..., 0]
# the normal: the high model's shape and normal map, in the low model's tangent space
for l in list(add.inputs[0].links): nt.links.remove(l)
nrm = bake("Normal", SIZE, False, type="NORMAL", normal_space="TANGENT")
# ambient occlusion of the high model, seen by the low (the high's own shape: its eaves over the walls, the porch, the recesses)
L(emis.outputs[0], add.inputs[0])
ao = bake("AO", SMALL, False, samples=24, type="AO")[..., 0]

# ---- the painted look -------------------------------------------------------------------------------------------------------
srgb = to_srgb(base_lin)
# Lab (D65)
def rgb_to_lab(rgb):
    lin = to_lin(rgb)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], np.float32)
    xyz = lin @ M.T / np.array([0.9505, 1.0, 1.089], np.float32)
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)
def lab_to_rgb(lab):
    fy = (lab[..., 0] + 16) / 116; fx = fy + lab[..., 1] / 500; fz = fy - lab[..., 2] / 200
    f = np.stack([fx, fy, fz], -1)
    xyz = np.where(f > 0.2069, f ** 3, (f - 16 / 116) / 7.787) * np.array([0.9505, 1.0, 1.089], np.float32)
    Mi = np.array([[3.2406, -1.5372, -0.4986], [-0.9689, 1.8758, 0.0415], [0.0557, -0.2040, 1.0570]], np.float32)
    return to_srgb(xyz @ Mi.T)
lab = rgb_to_lab(srgb)
Lc, A, B = lab[..., 0], lab[..., 1], lab[..., 2]
C = np.hypot(A, B); H = (np.degrees(np.arctan2(B, A)) + 360) % 360
def soft(x, a0, a1):   # 0 below a0, 1 above a1
    return np.clip((x - a0) / (a1 - a0), 0, 1)
g = GRADE.get(name, GRADE["OakHillHouse_300k"])
w_plaster = soft(Lc, 56, 64) * (1 - soft(C, 22, 32))
w_stone = (1 - soft(C, 9, 15)) * soft(Lc, 25, 32) * (1 - soft(Lc, 72, 78)) * (1 - w_plaster)
w_wood = soft(C, 10, 15) * (1 - soft(Lc, 38, 45)) * soft(H, 25, 35) * (1 - soft(H, 70, 80))
w_thatch = soft(C, 16, 22) * soft(Lc, 38, 45) * soft(H, 62, 68) * (1 - soft(H, 95, 105)) * (1 - w_plaster)
glass_guard = np.clip(up2(mask[..., 1]) * 4, 0, 1) if mask.shape[0] * 2 == SIZE else 0
dL = np.zeros_like(Lc); dA = np.zeros_like(Lc); dB = np.zeros_like(Lc); cg = np.ones_like(Lc) * CHROMA_BY.get(name, CHROMA_GAIN)
# each texel's height, from the low model's own geometry (a position bake): the moss grade and the top-to-bottom gradient use it
show(geo.outputs["Position"])
pos = bake("Height", SMALL, False)   # (from the high model's Position, selected to active: the low model's own material has no emission, so a bake of it alone came out 0 and the gradient and foot tint fell on the whole building)
z = up2(pos[..., 2]) * U
def _grey_filter(x, r, f):   # square max (f=np.maximum) or min (np.minimum) filter, radius r texels
    for ax in (0, 1):
        p = np.pad(x, [(r, r) if i == ax else (0, 0) for i in range(2)], mode="edge"); y = np.take(p, range(0, x.shape[ax]), axis=ax)
        for s in range(1, 2 * r + 1): y = f(y, np.take(p, range(s, s + x.shape[ax]), axis=ax))
        x = y
    return x
# paint_out (build-b, 2026-11-14): Meshy painted the church's side plaster with a crack and grey drip streaks (visual review: "it still has to go").
# Inside each box (model cm: x0, x1, y0, y1, z0, z1, from the position bake) thin dark marks on plaster are closed over: a grey closing of the
# lightness (radius PO_R texels) fills anything narrower than about 2*PO_R texels and leaves the wider timber struts, stone and windows alone.
PO_R = 5
_po_boxes = GRADE.get(name, {}).get("paint_out", ())
k_po = None
if _po_boxes:
    px, py = up2(pos[..., 0]) * U, up2(pos[..., 1]) * U
    lum = np.clip(Lc, 0, 100).astype(np.float32)
    closed = _grey_filter(_grey_filter(lum, PO_R, np.maximum), PO_R, np.minimum)
    near_plaster = box_blur(w_plaster, 8)
    k = np.zeros_like(lum)
    for box in _po_boxes:
        inbox = (px >= box[0]) & (px <= box[1]) & (py >= box[2]) & (py <= box[3]) & (z >= box[4]) & (z <= box[5])
        kb = inbox * soft(closed - lum, 2.5, 7.0) * soft(near_plaster, 0.2, 0.35) * soft(closed, 50.0, 58.0) * (1 - soft(C, 22.0, 30.0))
        k = np.maximum(k, kb); log("paint_out box", box, "texels in box", int(inbox.sum()), "painted", int((kb > 0.5).sum()))
    if isinstance(glass_guard, np.ndarray): k = k * (1 - glass_guard)
    k = np.maximum(_grey_filter(k, 2, np.maximum) * 0.9, k)   # (a rim two texels wide: the mark's soft edge was left behind and the grade turned it orange, review round 3)
    k = np.maximum(k, box_blur(k, 1) * 0.8)   # (a soft rim so no outline of the old mark is left)
    wpb = np.maximum(box_blur(w_plaster, 12), 1e-3); fA = box_blur(A * w_plaster, 12) / wpb; fB = box_blur(B * w_plaster, 12) / wpb   # (the plaster's own tint round the mark: a brown crack kept its own tint and left a yellow ghost)
    target = to_lin(np.clip(lab_to_rgb(np.stack([closed, fA, fB], -1)), 0, 1))   # (set outright: a gain clipped at 8 left the darkest crack texels orange)
    base_lin = base_lin * (1 - k[..., None]) + target * k[..., None]
    k_po = np.clip(box_blur(k, 3) * 1.5, 0, 1)
    if os.environ.get("TL_PODBG"):
        dbg = srgb.copy(); dbg[k > 0.5] = (1.0, 0.0, 1.0); write_png(dbg, os.path.join(out, "PODBG_%s.png" % new), False)
if GRADE.get(name, {}).get("paint_out"):
    srgb = to_srgb(base_lin); lab = rgb_to_lab(srgb); Lc, A, B = lab[..., 0], lab[..., 1], lab[..., 2]
    C = np.hypot(A, B); H = (np.degrees(np.arctan2(B, A)) + 360) % 360
    w_plaster = soft(Lc, 56, 64) * (1 - soft(C, 22, 32))
    w_stone = (1 - soft(C, 9, 15)) * soft(Lc, 25, 32) * (1 - soft(Lc, 72, 78)) * (1 - w_plaster)
    w_wood = soft(C, 10, 15) * (1 - soft(Lc, 38, 45)) * soft(H, 25, 35) * (1 - soft(H, 70, 80))
    w_thatch = soft(C, 16, 22) * soft(Lc, 38, 45) * soft(H, 62, 68) * (1 - soft(H, 95, 105)) * (1 - w_plaster)
log("height range cm", [round(float(x), 1) for x in np.percentile(z, [1, 50, 99])], "moss-hue share by height", [round(float(((soft(C, 10, 16) * soft(H, 100, 110) * (1 - soft(H, 170, 180))) * ((z > a) & (z <= b))).mean() * 100), 2) for a, b in ((-1e9, 260), (260, 1e9))])
# the model's own painted moss (lime patches along the ridge, read as spots by the review 2026-11-14): muted to a soft olive
w_moss = soft(C, 10, 16) * soft(H, 92, 100) * (1 - soft(H, 170, 180)) * (1 - w_plaster) * soft(z, 260.0, 320.0)   # (roof only: the flower boxes' leaves keep their green, review 2026-11-14)
g = dict(g, moss=MOSS_GRADE)
groups = [(w_plaster, "plaster"), (w_wood, "wood"), (w_thatch, "thatch"), (w_stone, "stone"), (w_moss, "moss")]
if "tile" in g:   # (build-b: red clay roof tiles, above the eaves only)
    w_tile = soft(C, 14, 20) * (1 - soft(H, 52, 62)) * soft(z, g["tile_z"] - 40.0, g["tile_z"] + 40.0) * (1 - w_plaster)
    w_wood = w_wood * (1 - w_tile); groups[1] = (w_wood, "wood"); groups.append((w_tile, "tile"))
    sw = max(float(w_tile.sum()), 1.0); mL, mA, mB = (float((w_tile * x).sum()) / sw for x in (Lc, A, B))
    k = CHROMA_GAIN * g["tile"][3]
    if TILE_TARGET is not None and g.get("tile_to", True):   # one terracotta for the whole town (review 2026-11-14: the church read maroon, the well orange)
        g = dict(g, tile=(TILE_TARGET[0] - mL, TILE_TARGET[1] - mA * k, TILE_TARGET[2] - mB * k, g["tile"][3]))
    log("tile share %.0f%%, mean Lab before %.1f %.1f %.1f, after %.1f %.1f %.1f" % (100 * float(w_tile.mean()), mL, mA, mB, mL + g["tile"][0], mA * k + g["tile"][1], mB * k + g["tile"][2]))
for w, key in groups:
    l_, a_, b_, c_ = g[key]
    dL += w * l_; dA += w * a_; dB += w * b_; cg *= 1 + w * (c_ - 1)
A2 = A * cg + dA; B2 = B * cg + dB; L2 = Lc + dL
graded = lab_to_rgb(np.stack([L2, A2, B2], -1))
log("groups: plaster %.0f%% wood %.0f%% thatch %.0f%% stone %.0f%%" % tuple(100 * float(x.mean()) for x in (w_plaster, w_wood, w_thatch, w_stone)))
# occlusion and edges (baked at SMALL)
ao_s = up2(box_blur(np.clip(ao, 0, 1), AO_BLUR))
occl = (1 - ao_s)[..., None] * AO_BY.get(name, (AO_STRENGTH, 1.0))[0]
if k_po is not None: occl = occl * (1 - k_po[..., None])   # (the carved crack's occlusion left a warm ghost, review round 3)
lin = to_lin(graded)
lin = lin * (1 - occl) + lin * AO_TINT * occl
edge = up2(box_blur(np.clip((point - 0.5) * 6.0, 0, 1), EDGE_BLUR))
if k_po is not None: edge = edge * (1 - k_po)
lin = lin * (1 + EDGE_GAIN * edge[..., None])
zt = np.clip((z - 0.0) / max(float(ext[2] * U) * 0.8, 1.0), 0, 1)
gl = GRAD_BOTTOM + (GRAD_TOP - GRAD_BOTTOM) * np.sqrt(zt)
foot = np.clip(1 - z / 120.0, 0, 1)[..., None]
lin = lin * gl[..., None] * (1 - foot + foot * GRAD_FOOT_TINT)
# the glass keeps its colour (M_TL_Building turns it to dark glass anyway)
if isinstance(glass_guard, np.ndarray):
    lin = lin * (1 - glass_guard[..., None]) + base_lin * glass_guard[..., None]
base_out = to_srgb(lin)
write_png(base_out, os.path.join(out, "T_%s_Base.png" % new), True)
if k_po is not None: nrm = nrm * (1 - k_po[..., None]) + np.array([0.5, 0.5, 1.0], np.float32) * k_po[..., None]   # (the 300k model carries the crack in its shape too: flat there)
write_png(nrm, os.path.join(out, "T_%s_Normal.png" % new), False)
mr_out = np.stack([1 - (1 - np.clip(ao, 0, 1)) * AO_BY.get(name, (AO_STRENGTH, 1.0))[1], mr[..., 1], mr[..., 2]], -1)
if k_po is not None and mr_out.shape[0] * 2 == k_po.shape[0]: mr_out[..., 0] = np.maximum(mr_out[..., 0], k_po[::2, ::2])   # (no baked occlusion in a painted-out crack)
write_png(mr_out, os.path.join(out, "T_%s_MR.png" % new), False)
if glow_img is not None: write_png(to_srgb(glow_lin), os.path.join(out, "T_%s_Glow.png" % new), True)   # (none for a design with no lit windows, the stalls: the importer uses T_NoGlow)
write_png(mask, os.path.join(out, "T_%s_Mask.png" % new), False)
log("pictures written; AO mean %.2f, edge mean %.3f" % (float(ao.mean()), float(edge.mean())))

# ---- the low model out (as building_clean.py writes its models) ------------------------------------------------------------
for o in bpy.data.objects: o.select_set(False)
lo.select_set(True); bpy.context.view_layer.objects.active = lo
fbx = os.path.join(out, new + ".fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE",
                         global_scale=0.01 * U, apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
                         use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False, colors_type="NONE")
log("wrote", fbx, "tris", low_tris, "in %.0f s" % (time.time() - t0))

if PREVIEW:
    # old and new side by side in Eevee, under a sun: a quick check before Unreal
    def mat_from(img_base, img_nrm, nm_name):
        mm = bpy.data.materials.new(nm_name); mm.use_nodes = True; t = mm.node_tree
        p = t.nodes["Principled BSDF"]
        b = t.nodes.new("ShaderNodeTexImage"); b.image = img_base; t.links.new(b.outputs["Color"], p.inputs["Base Color"])
        n = t.nodes.new("ShaderNodeTexImage"); n.image = img_nrm; img_nrm.colorspace_settings.name = "Non-Color"
        q = t.nodes.new("ShaderNodeNormalMap"); t.links.new(n.outputs["Color"], q.inputs["Color"]); t.links.new(q.outputs["Normal"], p.inputs["Normal"])
        p.inputs["Roughness"].default_value = 0.8
        return mm
    hi.data.materials.clear(); hi.data.materials.append(mat_from(base_img, nrm_img, "OldLook"))
    nb = bpy.data.images.load(os.path.join(out, "T_%s_Base.png" % new)); nn = bpy.data.images.load(os.path.join(out, "T_%s_Normal.png" % new))
    lo.data.materials.clear(); lo.data.materials.append(mat_from(nb, nn, "NewLook"))
    w = float(ext[0]) * 1.15
    lo.location.x += w
    sc.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN")); sc.collection.objects.link(sun)
    sun.data.energy = 4.0; sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.65, 0.8, 1); world.node_tree.nodes["Background"].inputs[1].default_value = 0.8
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam
    cx = float(lo_b[0] + hi_b[0]) * 0.5 + w * 0.5; cz = float(ext[2]) * 0.45
    for k, (dx, dy) in enumerate(((0.0, -1.0), (0.6, 0.8))):
        d = float(ext.max()) * 2.6
        cam.location = (cx + dx * d, dy * d, cz + d * 0.35)
        dirv = np.array([cx, 0.0, cz]) - np.array(cam.location)
        cam.rotation_euler = (math.atan2(math.hypot(dirv[0], dirv[1]), -dirv[2]), 0, math.atan2(dirv[1], dirv[0]) - math.pi / 2)
        cam.data.clip_end = d * 10; cam.data.lens = 35
        sc.render.resolution_x, sc.render.resolution_y = 1600, 800
        sc.render.filepath = os.path.join(out, "%s_preview_%d.png" % (new, k))
        bpy.ops.render.render(write_still=True)
    log("previews written")
