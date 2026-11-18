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
# The window glass is NOT decided here any more (2026-11-15): Tools/window_inserts.py runs after this and townlab_panes.py
# (BuildTownLabBuildings.ps1) and lays every pane of the high model as its own flat glass on the low model, with straight edges, marked
# in the low model's vertex colour (G 1 glass, R the window's number, B 1 everything else); M_TL_Building lights only that glass and
# reads the mask picture just for the forge's fire. Bake the low model, then always run window_inserts.py on it.
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
    # build-a's four (2026-11-14): the house/store palette; the bank stall keeps its own (building_palette.py "bankstall": clay tile
    # roof, warm stone, brass), so it is graded only gently
    "OakHillBank_300k":        dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-6.0, 2.0, -2.0, 0.9), stone=(2.0, 1.0, 5.0, 1.0)),
    "BankStall_300k":          dict(plaster=(5.0, 0.5, 3.0, 1.0), wood=(-2.0, 0.5, -1.5, 1.0), thatch=(-2.0, 0.5, -0.5, 1.0), stone=(5.0, 0.5, 3.0, 1.0)),   # (its arcade wall lifted: review read it as grey concrete)
    "OakHillSmith_300k":       dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-4.0, 1.0, -3.0, 1.05), thatch=(-8.0, 2.0, -2.0, 0.9), stone=(2.0, 1.0, 4.0, 1.0)),
    # the castle ruins (war buildings 2026-11-18): pale old stone, no plaster; its pale stone must not turn to the houses' butter cream
    "OakHillRuins_300k":       dict(plaster=(0.0, 0.5, 2.5, 1.0), wood=(-4.0, 1.0, -3.0, 1.05), thatch=(-3.0, 0.0, 0.0, 1.0), stone=(1.0, 0.5, 3.0, 1.0)),
    "OakHillCornerHouse_300k": dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-4.0, 1.0, -3.0, 1.05), thatch=(-9.0, 2.0, -2.0, 0.9), stone=(2.0, 1.0, 5.0, 1.0)),
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
                                   # the -X side wall's retouch (see "Retouch" further down): the two rows under the dormer, the big panel left of them
                                   wall_fill=[(-330.0, -280.0, -352.0, 218.0, 545.0, 760.0, 0, 3.5),   # (depth_x here: cm proud of the wall)
                                              (-400.0, -240.0, 250.0, 720.0, 380.0, 525.0, 3, -1e9)],
                                   paint_fill=[(-400.0, -240.0, -230.0, -180.0, 653.0, 721.0), (-400.0, -240.0, -135.0, -82.0, 653.0, 721.0),
                                               (-400.0, -240.0, -38.0, 5.0, 653.0, 721.0), (-400.0, -240.0, 58.0, 105.0, 653.0, 721.0)],   # the four stumps
                                   flat_out=[(-380.0, -280.0, -350.0, 215.0, 657.0, 722.0, -301.0)],
                                   flat_shade=[(-330.0, -280.0, -352.0, 218.0, 545.0, 725.0), (-400.0, -240.0, 250.0, 720.0, 380.0, 525.0)],
                                   fill_lab=(79.0, 3.0, 16.0), quads=True,
                                   quad_rects=[(-302.5, -352.0, 214.0, 650.0, 750.0), (-306.0, -352.0, -190.0, 540.0, 636.0), (-304.0, -170.0, 18.0, 540.0, 636.0), (-303.0, 46.0, 214.0, 540.0, 636.0), (-300.0, 278.0, 679.0, 372.0, 504.0, 9.0)], binarize=True,
                                   frame_bars=[(-354.0, 216.0, 634.0, 653.0), (-192.0, -168.0, 540.0, 636.0), (16.0, 48.0, 540.0, 636.0), (-380.0, -350.0, 540.0, 752.0), (212.0, 240.0, 540.0, 752.0)],
                                   sheet_lin=(0.70, 0.60, 0.42), sheet_ao=0.85,
                                   paint_keep=[(-400.0, -240.0, -360.0, 225.0, 628.0, 652.0)]),
    "OakHillWell_150k":       dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(3.0, 1.0, 5.0, 1.0), tile=(4.0, -1.5, 7.0, 0.95), tile_z=170.0),
    "OakHillPotionShop_300k": dict(plaster=(2.0, 1.5, 6.0, 1.0), wood=(-3.0, 1.0, -3.0, 1.05), thatch=(-4.0, 0.0, 0.0, 0.95),
                                   stone=(2.0, 1.0, 5.0, 1.0), tile=(3.0, -1.0, 6.0, 0.95), tile_z=500.0),
})
TILE_TARGET = (25.4, 22.7, 25.5)       # (build-b) the town's terracotta in Lab: every "tile" group is shifted to this mean (None: the fixed offsets)
MOSS_GRADE = (-6.0, 6.0, 10.0, 0.3)   # green moss: pulled most of the way into the straw (a faint olive-brown tinge: the review read brighter olive as patches)
# Buildings with a fire that burns all day (the forge): building_clean.py marks its faces (0,0,0) in the vertex colour, which the bake's
# empty background also reads as, so their mask is baked offset (0.2 + 0.8 x colour: a real face is never 0) and decoded.
FIRE = {"OakHillSmith_300k"}
AO_STRENGTH = 0.42          # how much of the occlusion darkens the colour
# (build-c 2026-11-14, visual review: the stalls are open boxes under a roof, so the high model's occlusion is deep everywhere inside
# and on the two-layer cloth roof; at 0.42 it turned the cream stripes peach, the lining and goods muddy and the potion stall's back
# logs near black. Per design: (how much darkens the colour, how much of it reaches the material's AO channel) and the chroma gain.)
AO_BY = {"GeneralStore_300k": (0.12, 0.4), "PotionStall_300k": (0.18, 0.5), "BankStall_300k": (0.18, 0.18 / AO_STRENGTH)}   # (build-a: the bank stall's open arcade sits under its roof, so full occlusion greyed its back wall)
CHROMA_BY = {"GeneralStore_300k": 1.0, "PotionStall_300k": 1.08}
# (war buildings 2026-11-18: the castle ruins are open walls with no roof, two faces and deep window reveals, and their occlusion bake came
# out dark and blotchy (mean 0.17): at the town's 0.42 it painted brown-yellow blotches over the stone. Barely any of it here)
AO_BY["OakHillRuins_300k"] = (0.08, 0.3)
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
# flat_out (build-b, 2026-11-14): boxes (model cm: x0, x1, y0, y1, z0, z1, plane) where the low model's relief standing out past a wall
# plane at x = plane is pressed back onto it. The church's four timber stumps hanging mid-panel under the dormer are carved in the 300k
# model; painted over as plaster they still showed as pale ghosts (review round 3), so their shape goes too (paint_fill does the colour).
for (x0, x1, y0, y1, z0, z1, plane, *deep) in GRADE.get(name, {}).get("flat_out", ()):   # (deep: the plane for the lowest 6 cm, the face of the beam they stand on)
    nflat = 0
    for v in me.vertices:
        x, y, zz = v.co.x * U, v.co.y * U, v.co.z * U
        p = deep[0] if deep and zz < z0 + 6.0 else plane   # (the stumps' feet only back to the beam's face, so no slope runs down onto the beam)
        if x0 <= x <= x1 and y0 <= y <= y1 and z0 <= zz <= z1 and x < p: v.co.x = p / U; nflat += 1
    me.update(); log(new, "flat_out vertices pressed back", nflat)
    bmf = bmesh.new(); bmf.from_mesh(me); nb = len(bmf.faces)   # (the stumps' sides are now slivers of no width: dissolved, or their smoothed normals drew pale outlines, review round 4)
    inb = lambda co: x0 - 20 <= co.x * U <= x1 + 20 and y0 - 20 <= co.y * U <= y1 + 20 and z0 - 20 <= co.z * U <= z1 + 20
    bmesh.ops.dissolve_degenerate(bmf, dist=3.0 / U, edges=[e for e in bmf.edges if inb(e.verts[0].co) and inb(e.verts[1].co)]); bmf.to_mesh(me); log(new, "flat_out faces dissolved", nb - len(bmf.faces)); bmf.free(); me.update()
# hard edges over SHARP_DEG, smooth elsewhere
for p in me.polygons: p.use_smooth = True
try:
    me.set_sharp_from_angle(angle=math.radians(SHARP_DEG))
except Exception:
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(SHARP_DEG))
# flat_shade (build-b, 2026-11-14, review rounds 5-7): boxes (model cm x0, x1, y0, y1, z0, z1) of plaster wall facing -X. Smoothed
# normals bent toward the beams' bevels and shaded the painted fill in streaks; flat faces showed the decimated wall's facets. Every
# corner of the wall's faces there gets the same normal, straight out (-X), so the fill shades as one even sheet (geometry and UVs kept).
_fs = GRADE.get(name, {}).get("flat_shade", ())
if _fs:
    cn = [tuple(c.vector) for c in me.corner_normals]; nfs = 0
    for p in me.polygons:
        c = p.center * U
        if (p.normal.x < -0.8 or any(b[0] <= c.x <= b[1] and b[2] <= c.y <= b[3] and b[4] <= c.z <= b[5] for b in GRADE.get(name, {}).get("flat_out", ()))) and any(x0 <= c.x <= x1 and y0 <= c.y <= y1 and z0 <= c.z <= z1 for (x0, x1, y0, y1, z0, z1) in _fs):
            for li in p.loop_indices: cn[li] = (-1.0, 0.0, 0.0)
            nfs += 1
    me.normals_split_custom_set(cn); me.update(); log(new, "flat_shade faces given one normal", nfs)
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
_base_raw = base_lin.copy() if name in GRADE and GRADE[name].get("quads") else np.zeros((1, 1, 3), np.float32)   # (plaster quads: where the bake wrote nothing)
show(tm.outputs["Color"]); mr = bake("MR", SMALL, False)
if glow_img is not None:
    tg = tex(glow_img); show(tg.outputs["Color"]); glow_lin = bake("Glow", SMALL, True)
else:
    glow_lin = np.zeros((SMALL, SMALL, 3), np.float32)
if name in FIRE:
    off = N("ShaderNodeMix"); off.data_type = "RGBA"; off.inputs["Factor"].default_value = 0.8
    off.inputs[6].default_value = (1.0, 1.0, 1.0, 1.0); L(vcol.outputs["Color"], off.inputs[7])
    show(off.outputs[2]); raw = bake("Mask", SMALL, False)
    surf = raw.max(-1) > 0.1                    # (a real face of the model; the background stays 0)
    mask = np.clip((raw - 0.2) / 0.8, 0, 1)
    fire_t = surf & (mask.max(-1) < 0.15)       # the forge: (0,0,0) on the model
    mask[fire_t] = 0.0
    mask[..., 2] = np.where(fire_t, 0.0, np.maximum(mask[..., 2], (mask[..., 1] < 0.5).astype(mask.dtype)))
    log(new, "fire texels", int(fire_t.sum()), "window texels", int((mask[..., 1] > 0.5).sum()))
else:
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
_rt = GRADE.get(name, {})
k_po = None
_struts = []   # (build-b, review round 12: the left panel's struts, rebuilt as wood geometry over its plaster sheet)
if _rt.get("wall_fill") or _rt.get("paint_fill"):
    # Retouch (build-b, 2026-11-14; the church's side wall under the dormer: a painted crack, drip streaks and grime, review rounds 2-4).
    # wall_fill boxes (model cm x0, x1, y0, y1, z0, z1, open_r, depth_x): the plaster is everything in the box that is not timber, and it
    # is painted one flat colour (fill_lab). Timber = the dark texels (lightness under 55 or wood-brown) that survive an opening of
    # open_r texels (so marks and stains narrower than 2*open_r+1 texels go, struts and beams stay), plus any dark texel standing out
    # past depth_x (the beams and posts are carved: their own shape keeps them, however thin). A hard mask traced on the timber's edges,
    # not soft per-patch fills (review round 4: those left patch edges, grime and ragged beam edges).
    px, py = up2(pos[..., 0]) * U, up2(pos[..., 1]) * U
    lum = np.clip(Lc, 0, 100).astype(np.float32)
    dark = ((lum < 55.0) | (w_wood > 0.5)).astype(np.float32)
    kf = np.zeros_like(lum)
    for (x0, x1, y0, y1, z0, z1, open_r, depth_x) in _rt.get("wall_fill", ()):
        inbox = (px >= x0) & (px <= x1) & (py >= y0) & (py <= y1) & (z >= z0) & (z <= z1)
        if open_r == 0:   # (the wall is not quite flat: each texel is measured against the most recessed surface within about 40 cm of it in the picture)
            wall = _grey_filter(np.where(inbox, px, -1e9).astype(np.float32), 15, np.maximum)
        timber = _grey_filter(_grey_filter((px < wall - depth_x).astype(np.float32), 3, np.minimum), 3, np.maximum) if open_r == 0 else np.maximum(_grey_filter(_grey_filter(dark, open_r, np.minimum), open_r, np.maximum), dark * (px < depth_x))   # (open_r 0: by depth alone, every carved timber face of 7 texels or more kept (5 left lumps along the middle beam's top, review round 7), the plaster painted up to its edge, grime included)
        timber = _grey_filter(timber, 1, np.maximum)   # (the timber keeps its own soft edge texel)
        fb = (inbox & (timber < 0.5) & (C < 24.0)).astype(np.float32)   # (C: never the red roof tiles)
        tb = inbox & (timber >= 0.5) & (C < 24.0)   # (pale chips of the old plaster left on the beams' edges, review round 5: painted the beams' own wood)
        if (tb & (lum < 45.0)).sum() > 50:
            wood_lin = np.median(base_lin[tb & (lum < 45.0)], axis=0)
            kc = (tb & (lum > 48.0)).astype(np.float32); base_lin = base_lin * (1 - kc[..., None]) + wood_lin * kc[..., None]; log("wall_fill chips painted wood", int(kc.sum()))
        if open_r > 0 and _rt.get("binarize"):
            # (review rounds 9-11: the left panel's struts are painted, not carved, and their grime halos read as smudges. The two
            # struts are found as straight lines in the dark texels of the panel (the angle whose projection piles them up most
            # sharply, then its two peaks and their widths), and the panel is repainted: wood within each strut, plaster between,
            # with a 1-texel soft edge.)
            plq = (fb > 0.5) & inbox
            if plq.sum() > 500:
                ry0, ry1 = np.percentile(py[plq], [1.5, 98.5]); rz0, rz1 = np.percentile(z[plq], [1.5, 98.5])
                inr = inbox & (C < 24.0) & (py >= ry0) & (py <= ry1) & (z >= rz0) & (z <= rz1)
                D_ = inr & (lum < 40.0); dy_, dz_ = py[D_], z[D_]
                best = None
                for th in list(range(-80, -9)) + list(range(10, 81)):
                    t_ = math.radians(th); sv = dy_ * math.cos(t_) + dz_ * math.sin(t_)
                    h_, e_ = np.histogram(sv, bins=np.arange(sv.min() - 2, sv.max() + 4, 2.0))
                    sc_ = float((h_.astype(np.float64) ** 2).sum())
                    if best is None or sc_ > best[0]: best = (sc_, th, h_, e_)
                _, th, h_, e_ = best
                hs = np.convolve(h_, np.ones(3) / 3, mode="same"); pk = []
                for k in np.argsort(-hs):
                    if hs[k] < 0.3 * hs.max(): break
                    if all(abs(k - q) > 12 for q in pk): pk.append(int(k))
                    if len(pk) == 2: break
                t_ = math.radians(th); S_ = py * math.cos(t_) + z * math.sin(t_)
                aw = np.zeros_like(lum)
                for k in pk:
                    lo_ = k
                    while lo_ > 0 and hs[lo_ - 1] > 0.5 * hs[k]: lo_ -= 1
                    hi_ = k
                    while hi_ < len(hs) - 1 and hs[hi_ + 1] > 0.5 * hs[k]: hi_ += 1
                    c_ = (e_[lo_] + e_[hi_ + 1]) / 2.0
                    band = inr & (np.abs(S_ - c_) < 15.0); w_ = 0.85 * 30.0 * float((band & (lum < 40.0)).sum()) / max(1.0, float(band.sum()))   # (the dark share of a 30 cm band across it, less its grime halo)
                    aw = np.maximum(aw, np.clip((w_ / 2.0 - np.abs(S_ - c_)) / 1.2 + 0.5, 0, 1))
                    log("strut line: angle", th, "centre %.1f width %.1f cm" % (c_, w_)); _struts.append((th, c_, w_, ry0, ry1, rz0, rz1))
                aw = aw * inr
                wl_ = np.median(base_lin[D_ & (lum < 30.0)], axis=0) if (D_ & (lum < 30.0)).sum() > 50 else np.median(base_lin[D_], axis=0)
                base_lin = base_lin * (1 - aw[..., None]) + wl_ * aw[..., None]
                fb = np.where(inr, 1.0 - aw, fb).astype(np.float32)
                log("strut panel rect y %.0f..%.0f z %.0f..%.0f, strut texels %d" % (ry0, ry1, rz0, rz1, int((aw > 0.5).sum())))
        if open_r == 0 and _rt.get("quad_rects"):
            # (review round 11: pale plaster texels left on the beams just outside the sheets: wood there, no plaster outside a sheet)
            inq = np.zeros(lum.shape, bool)
            for qr2 in _rt["quad_rects"]: inq |= (py >= qr2[1] + 3.0) & (py <= qr2[2] - 3.0) & (z >= qr2[3] + 3.0) & (z <= qr2[4] - 3.0)   # (3 cm in: the beams' edges inside a sheet's margin show in front of it)
            out_ = inbox & ~inq & (C < 24.0)
            wsrc = out_ & (lum < 35.0)
            if wsrc.sum() > 50:
                wl2 = np.median(base_lin[wsrc], axis=0); pc_ = (out_ & (lum > 42.0)).astype(np.float32)
                base_lin = base_lin * (1 - pc_[..., None]) + wl2 * pc_[..., None]; fb = fb * (~out_)
                log("pale texels outside the sheets painted wood", int(pc_.sum()))
        kf = np.maximum(kf, fb); log("wall_fill box", (x0, x1, y0, y1, z0, z1), "plaster texels", int(fb.sum()), "of", int(inbox.sum()))
    for box in _rt.get("paint_fill", ()):   # (boxes painted outright: the church's four timber stumps hanging mid-panel, carved in the 300k model too; flat_out presses their shape back)
        fb = ((px >= box[0]) & (px <= box[1]) & (py >= box[2]) & (py <= box[3]) & (z >= box[4]) & (z <= box[5])).astype(np.float32)
        kf = np.maximum(kf, fb); log("paint_fill box", box, "texels", int(fb.sum()))
    kf_ao = kf.copy()   # (no baked occlusion where the stumps were, the beam under them included: they shaded it)
    for box in _rt.get("paint_keep", ()):   # (boxes never painted: the top of the middle beam under the stumps)
        kb2 = ((px >= box[0]) & (px <= box[1]) & (py >= box[2]) & (py <= box[3]) & (z >= box[4]) & (z <= box[5])).astype(np.float32)
        kf = kf * (1 - kb2); log("paint_keep box", box, "texels", int(kb2.sum()))
    if isinstance(glass_guard, np.ndarray): kf = kf * (1 - glass_guard)
    fl = _rt.get("fill_lab", (78.0, 1.0, 10.0))   # (one clean warm off-white; the plaster grade then lifts it like the town's other plaster)
    target = to_lin(np.clip(lab_to_rgb(np.broadcast_to(np.array(fl, np.float32), lum.shape + (3,)).copy()), 0, 1))
    kfs = np.maximum(kf, box_blur(kf, 1) * (1 - np.maximum(kf_ao - kf, 0)))   # (one texel of soft edge: a hard texel mask drew stair-steps along the beams, review round 6)
    base_lin = base_lin * (1 - kfs[..., None]) + target * kfs[..., None]
    k_po = np.clip(box_blur(np.maximum(kf, kf_ao), 2) * 1.5, 0, 1)   # (no baked occlusion, edge or ripple on the painted plaster)
    if os.environ.get("TL_PODBG"):
        dbg = srgb.copy(); dbg[kf > 0.5] = (0.0, 1.0, 0.0); write_png(dbg, os.path.join(out, "PODBG_%s.png" % new), False)
if k_po is not None:
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
# Panes Meshy painted lit yellow that the window marks missed (no glow there, so M_TL_Building leaves them yellow all day: the bank's
# back panes, review 2026-11-14): bright saturated yellow becomes dark glass in the picture itself, on the buildings listed
PANE_FIX = {"OakHillBank_300k", "OakHillSmith_300k", "OakHillCornerHouse_300k"}
if name in PANE_FIX:
    w_pane = soft(C, 34, 44) * soft(Lc, 62, 70) * soft(H, 60, 68) * (1 - soft(H, 100, 108))
    # warm painted leading and stained glass inside the window marks (gold cames read as a lit window at noon, review 2026-11-14):
    # inside the marks, anything saturated becomes the same dark glass; the frames outside the marks keep their wood
    win_big = up2(np.clip(box_blur(mask[..., 1], 2) * 3, 0, 1)) * (1 - up2(mask[..., 2]))
    w_lead = win_big * soft(C, 14, 24) * soft(Lc, 30, 40) * (1 - w_pane)
    w_all = np.maximum(w_pane, w_lead)
    lin = lin * (1 - w_all[..., None]) + np.array([0.035, 0.04, 0.05], np.float32) * w_all[..., None]
    log(new, "yellow pane texels darkened %.2f%%, warm leading %.2f%%" % (100 * float((w_pane > 0.5).mean()), 100 * float((w_lead > 0.5).mean())))
    # the yellow panes the window marks missed become window too (lit at dusk with the rest; half-marked panes, review 2026-11-14)
    wp_s = w_pane.reshape(SMALL, 2, SMALL, 2).mean((1, 3))
    add_win = (wp_s > 0.4) & (mask[..., 1] < 0.5) & (mask[..., 2] > 0.5)
    if add_win.any():
        rr = mask[..., 0] * (mask[..., 1] > 0.5)
        for _ in range(6):   # each new pane texel takes a neighbouring window's random number, so a pane lights as one
            rr = np.maximum(rr, np.maximum(np.maximum(np.roll(rr, 2, 0), np.roll(rr, -2, 0)), np.maximum(np.roll(rr, 2, 1), np.roll(rr, -2, 1))))
        lit = glow_lin.max(-1) > 0.05
        gcol = np.median(glow_lin[lit], 0) if lit.any() else np.array([1.0, 0.55, 0.2], np.float32)
        mask[add_win, 0] = np.where(rr[add_win] > 0, rr[add_win], 0.5)
        mask[add_win, 1] = 1.0; mask[add_win, 2] = 0.0
        glow_lin[add_win] = np.maximum(glow_lin[add_win], gcol)
        log(new, "pane texels added to the window marks", int(add_win.sum()))
# Windows whose painted pane the marks cover only in part (the bank's two ground-floor back windows: Meshy painted a dark
# rectangle across each pane, outside the marks, so at dusk they lit only round one edge, review 2026-11-14). Growing the marks
# by colour can't tell that dark paint from the frame's dark wood, so these panes are boxed by hand (model cm, the bake's own
# positions) and kept as dark windows all the time: no glow, dark glass by day, like the 30% of windows that never light
DARK_BOXES = {"OakHillBank_300k": [(-420.0, -310.0, 540.0, 700.0, 165.0, 295.0), (310.0, 420.0, 540.0, 700.0, 165.0, 295.0)]}   # (wide of the frames: the stone round them has no marks to lose)
if name in DARK_BOXES:
    P = pos * U; box = np.zeros(P.shape[:2], bool)
    for x0, x1, y0, y1, z0, z1 in DARK_BOXES[name]:
        box |= (P[..., 0] > x0) & (P[..., 0] < x1) & (P[..., 1] > y0) & (P[..., 1] < y1) & (P[..., 2] > z0) & (P[..., 2] < z1)
    was_win = box & (mask[..., 1] > 0.5)
    mask[box, 1] = 0.0; mask[box, 2] = 1.0; glow_lin[box] = 0.0
    wb = up2(was_win.astype(np.float32))
    lin = lin * (1 - wb[..., None]) + np.array([0.035, 0.04, 0.05], np.float32) * wb[..., None]
    log(new, "texels in hand-boxed dark windows", int(box.sum()), "of them window marks turned to dark glass", int(was_win.sum()))
# ---- plaster quads (build-b, 2026-11-14, review rounds 5-9) -------------------------------------------------------------------
# The church's -X side wall under the dormer: Meshy painted cracks, drips and grime on its plaster, and however the texels were
# retouched (rounds 2-9) the panels' edges along the beams stayed ragged at this texture's density (about 0.8 texel a cm). So each
# panel the depth mask finds (a rectangle of plaster between carved beams) gets a flat quad of its own, just in front of the wall and
# behind the beams, whose corners all sample one block of the fill colour painted into an empty corner of the picture: the panel is
# then one even colour at every mip, and its edges are the beams' own geometry, straight and crisp.
_quads = []
_quv = None
_wuv = None
if _rt.get("quad_rects"):
    # (model cm: x, y0, y1, z0, z1; the panels measured from the depth mask's plaster, 2 cm into the beams round them. A search for
    # them by connected plaster joined the upper row to a lower panel through the beam's gaps, so they are given by hand.)
    vco = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", vco); vco = vco.reshape(-1, 3) * U
    for qr_ in _rt["quad_rects"]:
        qx, qy0, qy1, qz0, qz1 = qr_[:5]
        inn = (vco[:, 0] > qx - 25) & (vco[:, 0] < qx + 25) & (vco[:, 1] > qy0 + 4) & (vco[:, 1] < qy1 - 4) & (vco[:, 2] > qz0 + 4) & (vco[:, 2] < qz1 - 4)
        log(new, "plaster quad x %.1f y %.0f..%.0f z %.0f..%.0f; low verts inside %d, their x percentiles 0/5/50/95/100 %s" % (qx, qy0, qy1, qz0, qz1, int(inn.sum()),
            [round(float(v), 1) for v in np.percentile(vco[inn, 0], [0, 5, 50, 95, 100])] if inn.any() else None))
        _quads.append(tuple(qr_))
if k_po is not None and _rt.get("quads"):
    # (review round 11: the fill read grey at noon and glowing orange at low sun beside the town's other plaster: it is now the
    # median of this building's own graded plaster, outside the retouched boxes, so it lights like the rest)
    wp_ = (w_plaster > 0.7) & (kf < 0.05) & (soft(Lc, 70, 75) > 0.5) & (C < 14.0)
    if wp_.sum() > 1000:
        P_ = np.median(lin[wp_], axis=0)
        P_ = np.array(_rt.get("sheet_lin", P_), np.float32)   # (review round 17: the church's own plaster median read grey in the shade beside the town's cream: a set cream instead)
        lin = lin * (1 - kfs[..., None]) + P_ * kfs[..., None]
        log(new, "fill set to the building's own plaster (linear)", [round(float(q), 3) for q in P_], "from texels", int(wp_.sum()))
if _quads:
    Hh, Ww = lin.shape[:2]; N = 64
    # an empty block: texels the base bake never wrote (black far from every island) in a block of N x N
    blank = (np.abs(_base_raw).sum(-1) == 0.0).astype(np.float32)
    ok = _grey_filter(blank, N // 2 + 2, np.minimum) > 0.5
    rr, cc = np.nonzero(ok[N:-N, N:-N]) if ok[N:-N, N:-N].any() else (np.array([]), np.array([]))
    deep = _grey_filter(kf, 12, np.minimum) > 0.5
    if isinstance(mask, np.ndarray): deep &= up2(mask[..., 1]) < 0.05
    dr, dc = np.nonzero(deep)
    if len(dr) and len(rr):
        pr, pc = int(dr[len(dr) // 2]), int(dc[len(dc) // 2])
        r0, c0 = int(rr[0]) + N - N // 2, int(cc[0]) + N - N // 2
        lin[r0:r0 + N, c0:c0 + N] = lin[pr, pc]
        if os.environ.get("TL_QDBG"): lin[r0:r0 + N, c0:c0 + N] = (1.0, 0.0, 0.0)
        nrm[r0:r0 + N, c0:c0 + N] = (0.5, 0.5, 1.0)
        mask[r0 // 2:(r0 + N) // 2, c0 // 2:(c0 + N) // 2] = mask[pr // 2, pc // 2]
        if glow_img is not None: glow_lin[r0 // 2:(r0 + N) // 2, c0 // 2:(c0 + N) // 2] = 0.0
        _qblk = (r0, c0, N, pr, pc)
        _quv = ((c0 + N / 2) / Ww, (r0 + N / 2) / Hh)
        log(new, "plaster quads", len(_quads), "fill block at texel", (r0, c0), "colour from", (pr, pc))
        _wuv = None
        if _struts or _rt.get("frame_bars"):
            far = [(int(a) + N - N // 2, int(b) + N - N // 2) for a, b in zip(rr, cc) if abs(int(a) + N - N // 2 - r0) > 2 * N or abs(int(b) + N - N // 2 - c0) > 2 * N]
            ws_ = (aw > 0.95) if _struts else (w_wood > 0.6)
            if far and ws_.any():
                wr, wc = far[0]; sr, sc2 = [int(q[len(q) // 2]) for q in np.nonzero(ws_)]
                wsel_ = (w_wood > 0.6) & (kf < 0.05)
                lin[wr:wr + N, wc:wc + N] = np.median(lin[wsel_], axis=0) if wsel_.sum() > 1000 else lin[sr, sc2]; nrm[wr:wr + N, wc:wc + N] = (0.5, 0.5, 1.0)   # (the building's own timber colour, review round 15)
                mask[wr // 2:(wr + N) // 2, wc // 2:(wc + N) // 2] = mask[sr // 2, sc2 // 2]
                if glow_img is not None: glow_lin[wr // 2:(wr + N) // 2, wc // 2:(wc + N) // 2] = 0.0
                _wblk = (wr, wc, sr, sc2); _wuv = ((wc + N / 2) / Ww, (wr + N / 2) / Hh)
                log(new, "strut wood block at texel", (wr, wc), "colour from", (sr, sc2))
    else:
        log(new, "plaster quads: NO empty block or deep fill texel found, quads skipped", len(rr), len(dr)); _quads = []
base_out = to_srgb(lin)
write_png(base_out, os.path.join(out, "T_%s_Base.png" % new), True)
if k_po is not None: nrm = nrm * (1 - k_po[..., None]) + np.array([0.5, 0.5, 1.0], np.float32) * k_po[..., None]   # (the 300k model carries the crack in its shape too: flat there)
write_png(nrm, os.path.join(out, "T_%s_Normal.png" % new), False)
mr_out = np.stack([1 - (1 - np.clip(ao, 0, 1)) * AO_BY.get(name, (AO_STRENGTH, 1.0))[1], mr[..., 1], mr[..., 2]], -1)
if k_po is not None and mr_out.shape[0] * 2 == k_po.shape[0]: mr_out[..., 0] = np.maximum(mr_out[..., 0], k_po[::2, ::2])   # (no baked occlusion in a painted-out crack)
if _quv is not None:
    mr_out[_qblk[0] // 2:(_qblk[0] + _qblk[2]) // 2, _qblk[1] // 2:(_qblk[1] + _qblk[2]) // 2] = mr_out[_qblk[3] // 2, _qblk[4] // 2]
    mr_out[_qblk[0] // 2:(_qblk[0] + _qblk[2]) // 2, _qblk[1] // 2:(_qblk[1] + _qblk[2]) // 2, 0] = _rt.get("sheet_ao", 1.0)   # (a little shade on the sheets; the plaster median, 0.46, turned them grey, round 18)   # (review rounds 11-17: unshaded sheets glowed at low sun)
if _quv is not None and _wuv is not None: mr_out[_wblk[0] // 2:(_wblk[0] + 64) // 2, _wblk[1] // 2:(_wblk[1] + 64) // 2] = mr_out[_wblk[2] // 2, _wblk[3] // 2]
write_png(mr_out, os.path.join(out, "T_%s_MR.png" % new), False)
if glow_img is not None: write_png(to_srgb(glow_lin), os.path.join(out, "T_%s_Glow.png" % new), True)   # (none for a design with no lit windows, the stalls: the importer uses T_NoGlow)
write_png(mask, os.path.join(out, "T_%s_Mask.png" % new), False)
log("pictures written; AO mean %.2f, edge mean %.3f" % (float(ao.mean()), float(edge.mean())))

if _quv is not None:
    cn_old = [tuple(c.vector) for c in me.corner_normals]
    bq = bmesh.new(); bq.from_mesh(me); uvl = bq.loops.layers.uv[0]
    import mathutils.bvhtree
    tree = mathutils.bvhtree.BVHTree.FromBMesh(bq)
    nq0 = len(bq.faces)
    for qr_ in _quads:
        qx, qy0, qy1, qz0, qz1 = qr_[:5]; qoff = qr_[5] if len(qr_) > 5 else 1.0
        # a grid of about 10 cm whose points sit 1 cm in front of the low wall, found by a ray along +X; the wall bulges out up to
        # 8 cm toward the -Y end (one flat quad sank behind it there, debug round 10). Hits on the beams and roof in front of the
        # panel are outliers of a plane fitted to the hits and give way to the plane, so the quad stays behind them.
        ny_, nz_ = max(2, int(math.ceil((qy1 - qy0) / 10.0)) + 1), max(2, int(math.ceil((qz1 - qz0) / 10.0)) + 1)
        Y, Zg = np.meshgrid(np.linspace(qy0, qy1, ny_), np.linspace(qz0, qz1, nz_), indexing="ij")
        hit = np.full(Y.shape, np.nan)
        for a_ in range(ny_):
            for b_ in range(nz_):
                yy = min(max(Y[a_, b_], qy0 + 4.0), qy1 - 4.0); zz = min(max(Zg[a_, b_], qz0 + 4.0), qz1 - 4.0)   # (edge points probe 4 cm inside)
                r = tree.ray_cast(mathutils.Vector(((qx - 60.0) / U, yy / U, zz / U)), mathutils.Vector((1.0, 0.0, 0.0)), 120.0 / U)
                if r[0] is not None: hit[a_, b_] = r[0].x * U
        ok_ = ~np.isnan(hit)
        A_ = np.c_[np.ones(ok_.sum()), Y[ok_], Zg[ok_]]; hv = hit[ok_]; inl = np.ones(len(hv), bool)
        for _it in range(4):
            coef = np.linalg.lstsq(A_[inl], hv[inl], rcond=None)[0] if inl.sum() >= 3 else np.array([qx + 1.0, 0.0, 0.0])
            fit_ = A_ @ coef; inl = (hv > fit_ - 5.0) & (hv < fit_ + 6.0)
        fitg = coef[0] + coef[1] * Y + coef[2] * Zg
        xg = fitg.copy(); hh = np.where(ok_, hit, np.inf)
        good = ok_ & (hh > fitg - 5.0)
        good[0, :] = good[-1, :] = False; good[:, 0] = good[:, -1] = False   # (edges on the plane: behind the beams round the panel, never over them, review round 12)
        if qoff > 3.0: good[:] = False   # (a sheet standing well out lies on its plane alone: following the hits it bulged over the old carved strut, round 19)
        xg[good] = np.minimum(fitg[good], hh[good])
        xg -= qoff   # (in front of the wall: 1 cm, or more on a bumpy wall that poked through between the grid's points, review round 13)
        log(new, "plaster quad grid y %.0f..%.0f z %.0f..%.0f: %dx%d, hits %d, inliers %d, x %.1f..%.1f" % (qy0, qy1, qz0, qz1, ny_, nz_, int(ok_.sum()), int(inl.sum()), float(xg.min()), float(xg.max())))
        vg = [[bq.verts.new((xg[a_, b_] / U, Y[a_, b_] / U, Zg[a_, b_] / U)) for b_ in range(nz_)] for a_ in range(ny_)]
        for a_ in range(ny_ - 1):
            for b_ in range(nz_ - 1):
                f = bq.faces.new((vg[a_][b_], vg[a_][b_ + 1], vg[a_ + 1][b_ + 1], vg[a_ + 1][b_])); f.normal_update()
                if f.normal.x > 0: f.normal_flip()
                f.smooth = False
                for lp in f.loops: lp[uvl].uv = _quv
    def _clip(poly, ax, v, keep_ge):
        o = []
        for k in range(len(poly)):
            a, b = poly[k], poly[(k + 1) % len(poly)]
            ina, inb = (a[ax] >= v) == keep_ge, (b[ax] >= v) == keep_ge
            if ina: o.append(a)
            if ina != inb:
                t = (v - a[ax]) / (b[ax] - a[ax]); o.append((a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])))
        return o
    if _wuv is not None:
        for (th, c_, w_, ry0, ry1, rz0, rz1) in _struts:
            t_ = math.radians(th); n_ = (math.cos(t_), math.sin(t_)); d_ = (-math.sin(t_), math.cos(t_)); L = 1000.0
            wg_ = w_ / 0.85 + 12.0   # (the old struts are partly carved: the new ones are wide enough to cover them, review round 14)
            poly = [(c_ * n_[0] + sg * wg_ / 2 * n_[0] + tt * d_[0], c_ * n_[1] + sg * wg_ / 2 * n_[1] + tt * d_[1]) for (sg, tt) in ((-1, -L), (-1, L), (1, L), (1, -L))]
            sr_ = [q for q in _quads if len(q) > 5 and q[1] <= (ry0 + ry1) / 2 <= q[2]]
            if sr_: ry0, ry1, rz0, rz1 = sr_[0][1] + 2.0, sr_[0][2] - 2.0, sr_[0][3] + 2.0, sr_[0][4] - 2.0   # (the struts run the sheet's whole span, into the frame round it)
            for ax, v, ge in ((0, ry0 - 2.0, True), (0, ry1 + 2.0, False), (1, rz0 - 2.0, True), (1, rz1 + 2.0, False)): poly = _clip(poly, ax, v, ge)
            if len(poly) < 3: continue
            cy, cz = sum(q[0] for q in poly) / len(poly), sum(q[1] for q in poly) / len(poly)
            r = tree.ray_cast(mathutils.Vector((-450.0 / U, cy / U, cz / U)), mathutils.Vector((1.0, 0.0, 0.0)), 400.0 / U)
            wx = r[0].x * U if r[0] is not None else -300.0
            xf, xb = wx - 13.0, wx + 0.5   # (4 cm in front of its sheet, which stands 9 cm out to hide the old carved struts)
            fr = [bq.verts.new((xf / U, q[0] / U, q[1] / U)) for q in poly]; bk = [bq.verts.new((xb / U, q[0] / U, q[1] / U)) for q in poly]
            fl = [bq.faces.new(fr)] + [bq.faces.new((fr[k], fr[(k + 1) % len(fr)], bk[(k + 1) % len(fr)], bk[k])) for k in range(len(fr))]
            for f in fl:
                f.normal_update(); cen = f.calc_center_median()
                out_dir = mathutils.Vector(((xf - xb) / U if f is fl[0] else 0.0, 0.0, 0.0)) if f is fl[0] else (cen - mathutils.Vector(((xf + xb) / 2 / U, cy / U, cz / U)))
                if f.normal.dot(out_dir) < 0: f.normal_flip()
                f.smooth = False
                for lp in f.loops: lp[uvl].uv = _wuv
            log(new, "strut built: angle %d, %d corners, wall x %.1f" % (th, len(poly), wx))
        # frame bars (review round 17: the rail and posts round the sheets had wavy painted edges): straight wood bars over them,
        # 3 cm in front of whatever the ray meets at their middle (the old beam or the sheet), overlapping each sheet's edge by 2 cm
        for (by0, by1, bz0, bz1) in _rt.get("frame_bars", ()):
            poly = [(by0, bz0), (by0, bz1), (by1, bz1), (by1, bz0)]; cy, cz = (by0 + by1) / 2, (bz0 + bz1) / 2
            hx = []
            for (yy, zz) in ((cy, cz), (by0 + 2, cz), (by1 - 2, cz), (cy, bz0 + 2), (cy, bz1 - 2)):
                r = tree.ray_cast(mathutils.Vector((-450.0 / U, yy / U, zz / U)), mathutils.Vector((1.0, 0.0, 0.0)), 400.0 / U)
                if r[0] is not None and r[0].x * U > -330.0: hx.append(r[0].x * U)   # (not the side roof in front of the -Y post)
            wx = min(hx) if hx else -305.0
            xf, xb = wx - 4.0, wx + 1.0
            fr = [bq.verts.new((xf / U, q[0] / U, q[1] / U)) for q in poly]; bk = [bq.verts.new((xb / U, q[0] / U, q[1] / U)) for q in poly]
            fl = [bq.faces.new(fr)] + [bq.faces.new((fr[k], fr[(k + 1) % 4], bk[(k + 1) % 4], bk[k])) for k in range(4)]
            for f in fl:
                f.normal_update(); cen = f.calc_center_median()
                out_dir = mathutils.Vector((-1.0, 0.0, 0.0)) if f is fl[0] else (cen - mathutils.Vector(((xf + xb) / 2 / U, cy / U, cz / U)))
                if f.normal.dot(out_dir) < 0: f.normal_flip()
                f.smooth = False
                for lp in f.loops: lp[uvl].uv = _wuv
            log(new, "frame bar y %.0f..%.0f z %.0f..%.0f front x %.1f" % (by0, by1, bz0, bz1, xf))
    nqf = len(bq.faces) - nq0
    bq.to_mesh(me); bq.free(); me.update()
    me.normals_split_custom_set(cn_old + [tuple(me.polygons[pi].normal) for pi in range(len(me.polygons) - nqf, len(me.polygons)) for _ in me.polygons[pi].loop_indices]); me.update()
    low_tris = sum(len(p.vertices) - 2 for p in me.polygons)
    log(new, "plaster quads added", len(_quads), "low tris now", low_tris)
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
