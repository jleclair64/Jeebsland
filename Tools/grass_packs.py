# Sets up the owner's realistic Fab grass packs for the meadow (Meadow.cpp, jeeb.Grass.Pack; 2026-11-15, owner: "lean towards realism
# with the grass"). Run with Tools\BuildGrassPacks.ps1 (editor closed) once on every machine after the packs are added to Content from
# the owner's Fab library (they are git-ignored: Content/PN_GrassLibrary, PN_MeadowFlowers, PN_OpenWorldFoliage), and again after
# changing anything here. Safe to run again.
#
# For each pack's master material it makes our copy in /Game/GrassReal with:
#   - our field colour (grass_field.py: the patches, swathes and dry patches the painted ground under the grass has), mixed in by
#     JeebField (0..1), a colour multiplier JeebTint and a pull toward grey JeebDesat (all set live by Meadow.cpp from jeeb.Grass.*)
#   - cooler blades at the low sun (Dusk, as M_Grass)
#   - sinking into the ground with distance (FadeStart..FadeEnd, as M_Grass), the pack's own wind kept and eased out as it sinks
# and points the pack's own material instances (one per mesh: they carry each mesh's pivot-painter textures) at our copy. The pack's
# original masters are left as they were.
#
# It also moves Open World Foliage's ground material and textures (unused, about 140 MB) into its Unused folder, which is never cooked
# (Config/DefaultGame.ini), and logs the parameter collections the packs' wind reads.
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import grass_field   # noqa: E402
import palette       # noqa: E402  (2026-11-15: the world palette's knobs, Docs/Palette.md)

lib = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
OUT = "/Game/GrassReal"

# (pack master, our copy's name, where the pack's material instances live, flowers?)
PACKS = [
    ("/Game/PN_GrassLibrary/Materials/grassMaterials/MA_Grass", "M_GrassLibrary", "/Game/PN_GrassLibrary/Materials/grassMaterials", False),
    ("/Game/PN_MeadowFlowers/Materials/flowerMaterials/MA_Flowers", "M_MeadowFlowers", "/Game/PN_MeadowFlowers/Materials/flowerMaterials", True),
    ("/Game/PN_OpenWorldFoliage/Materials/MA_OpenWorldFoliage", "M_OpenWorldFoliage", "/Game/PN_OpenWorldFoliage/Materials", False),
]


def instances_in(folder):
    res = []
    for a in AR.get_assets_by_path(folder, recursive=True):
        if str(a.asset_class_path.asset_name) == "MaterialInstanceConstant":
            res.append(unreal.load_asset(str(a.package_name)))
    return res


def sink(g, start, end):
    """as build_materials.grass_sink: (keep, offset) for a clump sinking into the ground between FadeStart and FadeEnd"""
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    obj = g.node(unreal.MaterialExpressionObjectPositionWS)
    cam = g.node(unreal.MaterialExpressionCameraPositionWS)
    dist = g.node(unreal.MaterialExpressionDistance); g.link(obj, dist, "A"); g.link(cam, dist, "B")
    span = g.subn(g.scalar("FadeEnd", end), g.scalar("FadeStart", start))
    # each clump sinks at its own distance, up to Ragged sooner than FadeStart (one fixed random per clump), so the realistic
    # grass thins out raggedly toward the painted ground instead of ending in a straight line
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    near = g.subn(dist, g.subn(g.scalar("FadeStart", start), g.muln(rnd, g.scalar("Ragged", 900.0))))
    div = g.node(unreal.MaterialExpressionDivide); g.link(near, div, "A"); g.link(span, div, "B")
    gone = g.sat(div)
    gone = g.muln(gone, gone)
    keep = g.one(unreal.MaterialExpressionOneMinus, gone)
    up = g.subn(g.mask(wp, False, False, True), g.mask(obj, False, False, True))
    down = g.muln(g.c3((0.0, 0.0, -1.0)), g.muln(up, gone))
    # (2026-11-15, for speed: the pack's wind eases out between WindStill - 10 m and WindStill; nobody sees far blades sway)
    calm = g.sat(g.mul(g.subn(g.scalar("WindStill", 3000.0), dist), 0.001))
    return keep, down, calm


def greener(g, c, flowers):
    """the pack's straw-yellow stems and seed heads pulled to the green blades' colour at the same brightness (owner, 2026-11-15:
    'recolor the yellow blades ... they stand out too much'). Yellow is where red comes near green; JeebGreenDry 0 turns it off."""
    if flowers:
        return c
    red = g.mask(c, True, False)
    grn = g.mask(c, False, True)
    yellow = g.sat(g.mul(g.subn(red, g.mul(grn, 0.72)), 6.0))
    green = g.muln(g.lum(c), g.vector("JeebDryGreen", (0.66, 1.12, 0.42)))
    return g.lerp(c, green, g.muln(yellow, g.scalar("JeebGreenDry", 1.0)))


def rewire(mat, flowers):
    g = grass_field.FieldGraph(mat)
    MP = unreal.MaterialProperty
    base = lib.get_material_property_input_node(mat, MP.MP_BASE_COLOR)
    base_out = lib.get_material_property_input_node_output_name(mat, MP.MP_BASE_COLOR)
    wpo = lib.get_material_property_input_node(mat, MP.MP_WORLD_POSITION_OFFSET)
    wpo_out = lib.get_material_property_input_node_output_name(mat, MP.MP_WORLD_POSITION_OFFSET)
    sss = lib.get_material_property_input_node(mat, MP.MP_SUBSURFACE_COLOR)
    sss_out = lib.get_material_property_input_node_output_name(mat, MP.MP_SUBSURFACE_COLOR)
    if not base:
        raise RuntimeError("MATDIAG %s has no base colour input" % mat.get_name())
    patches = EAL.load_asset("/Game/T_Patches")
    # the field colour at the clump's own spot, once per vertex (its patches are 10 m and more: a clump is one colour)
    xy = g.mask(g.node(unreal.MaterialExpressionObjectPositionWS), True, True)
    fn = grass_field.noises(g, xy, patches)
    field = g.node(unreal.MaterialExpressionVertexInterpolator)
    g.link(grass_field.before_grey(g, g.c3((1.0, 1.0, 1.0)), fn), field)
    fieldmix = g.lerp(g.c3((1.0, 1.0, 1.0)), field, g.scalar("JeebField", 1.0))
    tint = g.vector("JeebTint", (1.0, 1.0, 1.0))
    col = g.muln(g.muln(greener(g, g.muln(base, g.c3((1.0, 1.0, 1.0)), out_a=base_out), flowers), fieldmix), tint)
    d = g.node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
    g.link(col, d, "")
    g.link(g.scalar("JeebDesat", 0.15), d, "Fraction")
    col = d
    # cooler at the low sun (as M_Grass: the warm light turned blades mustard); flowers only a little
    col = g.lerp(col, g.muln(col, g.c3((0.80, 1.04, 1.30))), g.mul(g.scalar("Dusk", 0.0), 0.4 if flowers else 0.85))
    keep, down, calm = sink(g, 4200.0, 5000.0)
    # as it sinks, the clump's contrast eases toward the painted ground beyond
    col = g.lerp(col, g.lum(col), g.mul(g.one(unreal.MaterialExpressionOneMinus, keep), 0.3))
    col = palette.grade_world(mat, col) if flowers else palette.grade_plant(mat, col)   # the palette's knobs (identity at the defaults)
    g.out(col, MP.MP_BASE_COLOR)
    if sss:
        # the light through the blades, cooled and dimmed at the low sun too (backlit, it alone turned the Grass Library to hay)
        scol = g.muln(g.muln(greener(g, g.muln(sss, g.c3((1.0, 1.0, 1.0)), out_a=sss_out), flowers), fieldmix), tint)
        scol = g.lerp(scol, g.muln(scol, g.c3((0.40, 0.62, 0.80))), g.mul(g.scalar("Dusk", 0.0), 0.3 if flowers else 0.9))
        scol = palette.grade_world(mat, scol) if flowers else palette.grade_plant(mat, scol)
        g.out(scol, MP.MP_SUBSURFACE_COLOR)
    if wpo:
        moved = g.muln(g.muln(wpo, keep, out_a=wpo_out), calm)          # (the pack's wind and bending, easing out as it sinks)
        g.out(g.addn(moved, down), MP.MP_WORLD_POSITION_OFFSET)
    else:
        g.out(down, MP.MP_WORLD_POSITION_OFFSET)
    lib.recompile_material(mat)


def build(orig_path, name, mi_folder, flowers):
    orig = EAL.load_asset(orig_path)
    if not orig:
        unreal.log_warning("MATDIAG %s is not in Content: skipped (add the pack from the owner's Fab library)" % orig_path)
        return
    ours = OUT + "/" + name
    mis = instances_in(mi_folder)
    # back to the pack's master first, so our old copy can go
    for mi in mis:
        if mi.get_editor_property("parent") and mi.get_editor_property("parent").get_path_name().split(".")[0] == ours:
            lib.set_material_instance_parent(mi, orig)
    if EAL.does_asset_exist(ours):
        EAL.delete_asset(ours)
    mat = EAL.duplicate_asset(orig_path, ours)
    rewire(mat, flowers)
    EAL.save_asset(ours)
    n = 0
    for mi in mis:
        p = mi.get_editor_property("parent")
        if p and p.get_path_name().split(".")[0] == orig_path:
            lib.set_material_instance_parent(mi, mat)
            lib.update_material_instance(mi)
            EAL.save_loaded_asset(mi)
            n += 1
    unreal.log("MATDIAG built %s from %s, %d material instances moved onto it" % (ours, orig_path, n))


def move_unused_owf():
    dst = "/Game/PN_OpenWorldFoliage/Unused"
    for src in ("/Game/PN_OpenWorldFoliage/Textures/ground_Albedo", "/Game/PN_OpenWorldFoliage/Textures/ground_Normal",
                "/Game/PN_OpenWorldFoliage/Textures/ground_AO", "/Game/PN_OpenWorldFoliage/Textures/ground_Height",
                "/Game/PN_OpenWorldFoliage/Materials/MA_OpenWorldGround", "/Game/PN_OpenWorldFoliage/Materials/MI_OpenWorldGround"):
        if EAL.does_asset_exist(src):
            to = dst + "/" + src.split("/")[-1]
            if EAL.rename_asset(src, to):
                unreal.log("MATDIAG moved %s to %s (never cooked)" % (src, to))


def log_collections():
    for path in ("/Game/PN_GrassLibrary/Materials/PN_WindParameters", "/Game/PN_MeadowFlowers/Materials/PN_WindParameters",
                 "/Game/PN_OpenWorldFoliage/Materials/MaterialCollection"):
        c = EAL.load_asset(path)
        if not c:
            continue
        for kind in ("scalar_parameters", "vector_parameters"):
            for p in c.get_editor_property(kind):
                try:
                    nm = p.get_editor_property("parameter_name")
                except Exception:
                    nm = "?"
                try:
                    dv = p.get_editor_property("default_value")
                except Exception:
                    dv = "?"
                unreal.log("MATDIAG collection %s %s %s = %s" % (path.split("/")[2], kind, nm, dv))


palette.ensure_mpc()
log_collections()
move_unused_owf()
for args in PACKS:
    build(*args)
unreal.log("MATDIAG all grass packs built")
