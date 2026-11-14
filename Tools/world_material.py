# Builds the ground's materials and pictures (2026-11-12: one painted, Breath of the Wild ground for the whole world):
#   M_Ground  the land everywhere: the original square (GrassField), Oak Hill's patch and the world's 8 m chunks (WorldTerrain).
#             The meshes write what the ground is into their vertex colours (Source/Jeebsland/GroundMask.h: R worn, G rock,
#             B mud, A sand); this paints the layers in world space, so they run straight across the meshes' borders.
#   M_Dirt    the road ribbons (Paths.cpp): packed earth fraying into the grass; Oak Hill's streets (vertex red 1) cobbled
#             down the middle.
#   M_Stone   the loose stones at building foundations (GroundDecor.cpp).
# Pictures: Tools/ground_textures.py (T_GrassGround, T_Rock, T_Cobble, T_CobbleN) and gen_textures.py (T_Dirt, T_Patches).
# Run by Tools\BuildWorldMaterial.ps1 inside a windowless editor (the editor must be closed). M_WorldGround (the old flat,
# vertex-coloured world material) is deleted.
#
# The look (CLAUDE.md, art direction; owner dislikes flat-colour ground, hay-coloured grass, contour lines): painted grass at
# two scales, the same broad lusher-to-lighter patches as the meadow's tufts (T_Patches at 45 m, as M_Grass has them) so the
# ground and the grass on it shift together, lightness only at the biggest scale (never toward yellow), and far away the fine
# picture fades to its own average so it never tiles or shimmers. Every edge between layers is broken by soft noise.
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_textures      # noqa: E402
import ground_textures   # noqa: E402

GRASS_DESATURATION = 0.45   # as build_materials.py's: the grass and the ground under it pulled this far toward grey
GRASS_TO_MEADOW = (1.139, 0.759, 0.358)   # T_GrassGround times this reads like the meadow tufts seen from afar (no line where the grass ends);
                                         # was (0.85, 1, 0.85) until M_Grass's tint went from 0.47, 0.58, 0.38 to 0.64, 0.52, 0.25 (2026-11-13: olive, not lime), then 0.60, 0.47, 0.21 and 0.63, 0.44, 0.16 (2026-11-14): 0.85 x new R / 0.47, new G / 0.58, 0.85 x new B / 0.38
GRASS_AVERAGE = (0.0705, 0.0916, 0.0083)   # T_GrassGround v2's average (linear) after GRASS_TO_MEADOW: what it fades to far away
ROCK_AVERAGE = (0.100, 0.096, 0.084)   # what the stone fades to far away: a darker neutral grey (lighter averages, with the haze over
                                       # them, read lavender or pink-beige against the green hills)
ROCK_TINT = (1.0, 1.0, 1.0)   # the RockTint parameter's default
EARTH_BALANCE = (2.02, 2.79, 3.95)   # T_Dirt (average linear 0.069, 0.036, 0.016: dark rust) times this averages a light packed tan
                                     # (0.14, 0.10, 0.062): roads and worn ground never read as dark stains beside cobbles and grass
EARTH_AVERAGE = (0.14, 0.10, 0.062)   # T_Dirt x EARTH_BALANCE's average (linear)

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def fresh_material(name):
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + name):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + name)
    return tools.create_asset(name, "/Game", unreal.Material, unreal.MaterialFactoryNew())


def import_texture(png, name, normal=False, alpha=False):
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = "/Game"
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset("/Game/" + name)
    if tex is None:
        raise RuntimeError("WORLDDIAG the picture " + name + " did not import")
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_no_alpha", not alpha)   # (T_Cobble keeps its alpha: each stone's own number)
        tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    return tex


class Graph:
    """Small helpers for wiring material nodes (as build_materials.py's)"""

    def __init__(self, mat):
        self.mat = mat

    def node(self, cls, **props):
        n = lib.create_material_expression(self.mat, cls, 0, 0)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        lib.connect_material_expressions(a, out, b, pin)

    def c(self, v):
        return self.node(unreal.MaterialExpressionConstant, r=v)

    def c3(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def mul(self, a, k, out=""):
        n = self.node(unreal.MaterialExpressionMultiply, const_b=k); self.link(a, n, "A", out); return n

    def add(self, a, k, out=""):
        n = self.node(unreal.MaterialExpressionAdd, const_b=k); self.link(a, n, "A", out); return n

    def muln(self, a, b, out_a="", out_b=""):
        n = self.node(unreal.MaterialExpressionMultiply); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

    def addn(self, a, b, out_a="", out_b=""):
        n = self.node(unreal.MaterialExpressionAdd); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

    def subn(self, a, b, out_a="", out_b=""):
        n = self.node(unreal.MaterialExpressionSubtract); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

    def one(self, cls, a, out=""):
        n = self.node(cls); self.link(a, n, "", out); return n

    def sat(self, a):
        return self.one(unreal.MaterialExpressionSaturate, a)

    def lerp(self, a, b, alpha, out_alpha=""):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, n, "A"); self.link(b, n, "B"); self.link(alpha, n, "Alpha", out_alpha)
        return n

    def lerpc(self, a_const, b_const, alpha, out_alpha=""):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, const_a=a_const, const_b=b_const)
        self.link(alpha, n, "Alpha", out_alpha)
        return n

    def ramp(self, a, lo, hi, out=""):
        """saturate((a - lo) / (hi - lo))"""
        return self.sat(self.mul(self.add(a, -lo, out), 1.0 / (hi - lo)))

    def mask(self, a, r, g, b=False, alpha=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=alpha); self.link(a, n); return n

    def append(self, a, b):
        n = self.node(unreal.MaterialExpressionAppendVector); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def tex(self, texture, uv, normal=False):
        n = self.node(unreal.MaterialExpressionTextureSample, texture=texture)
        if normal:
            n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        self.link(uv, n, "UVs")
        return n

    def desat(self, a, amount):
        n = self.node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
        self.link(a, n, "")
        self.link(self.c(amount), n, "Fraction")
        return n

    def lum(self, a):
        return self.mask(self.desat(a, 1.0), True, False)

    def vector(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def out(self, a, prop, pin=""):
        lib.connect_material_property(a, pin, prop)


def finish(mat, name):
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    unreal.log("WORLDDIAG built " + name)


def build_ground(grass_tex, dirt_tex, rock_tex, patches):
    mat = fresh_material("M_Ground")
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    xy = g.mask(wp, True, True)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    depth = g.node(unreal.MaterialExpressionPixelDepth)

    # soft noise at a few scales (the small T_Patches picture: cheap), shared by every layer's edge
    rag = g.mask(g.tex(patches, g.mul(xy, 1.0 / 170.0)), True, False)            # ragged edges, about 1.5 m
    med = g.mask(g.tex(patches, g.mul(g.add(xy, 3100.0), 1.0 / 1100.0)), True, False)   # 10 m patches
    med2 = g.mask(g.tex(patches, g.mul(g.add(xy, -5300.0), 1.0 / 1730.0)), True, False)   # the same at an unrelated size:
    medm = g.mul(g.addn(med, med2), 0.5)                                                  # averaged, neither one's repeat shows from the air
    patchy = g.mask(g.tex(patches, g.mul(xy, 1.0 / 4500.0)), True, False)         # the meadow tufts' own patches (M_Grass)
    big = g.mask(g.tex(patches, g.mul(g.add(xy, -7700.0), 1.0 / 26000.0)), True, False)   # broad swathes, a few hundred metres

    # Grass: the painted picture, fading to its average far away (no tiling, no shimmer), lighter and darker in soft patches
    # two samplings at unrelated sizes, swapped in 10 m patches, so no 3 m repeat lines up into a grid
    near_a = g.tex(grass_tex, g.mul(xy, 1.0 / 300.0))
    near_b = g.tex(grass_tex, g.mul(g.add(xy, 1777.0), 1.0 / 430.0))
    near = g.muln(g.lerp(near_a, near_b, g.ramp(medm, 0.42, 0.58)), g.c3(GRASS_TO_MEADOW), "RGB")
    far = g.ramp(depth, 1500.0, 6000.0)
    grass = g.lerp(near, g.c3(GRASS_AVERAGE), far)
    grass = g.muln(grass, g.lerpc(0.91, 1.08, medm))
    shade = g.lerp(g.c3((0.84, 0.97, 0.86)), g.c3((1.02, 1.02, 0.90)), patchy)        # deeper to lighter green, exactly as the tufts
    grass = g.muln(grass, shade)
    grass = g.muln(grass, g.lerpc(0.82, 1.10, big))                               # broad lightness swathes, so the far land isn't flat
    grass = g.muln(grass, g.lerp(g.c3((0.95, 1.0, 1.03)), g.c3((1.03, 1.0, 0.92)), med2))   # and a gentle hue wander, cooler to warmer green (never yellow)
    grass = g.desat(g.muln(grass, g.vector("Tint", (1.0, 1.0, 1.0))), GRASS_DESATURATION)

    # Soil (packed earth) for worn ground, mud and the fringe of rock
    soil = g.muln(g.mask(g.tex(dirt_tex, g.mul(xy, 1.0 / 350.0)), True, True, True), g.c3(EARTH_BALANCE))   # (light packed tan: M_Dirt's earth is the same)
    soil = g.lerp(soil, g.c3(EARTH_AVERAGE), g.c(0.45))   # its darkest specks pulled up: worn ground at walls never reads as a black stain

    # Worn: first trampled grass (paler, a little earth showing through), then packed earth, in ragged patches
    worn = g.mask(vc, True, False)
    trampled = g.lerp(g.muln(grass, g.c3((1.07, 1.04, 0.93))), soil, g.c(0.22))
    t_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.7), g.mul(g.add(rag, -0.5), 0.45)), -0.32), 2.2))   # (centred noise: nothing where worn is 0)
    d_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.45), g.mul(g.add(rag, -0.5), 1.3)), -1.0), 1.2))   # (a soft edge into the grass; walls and aprons trampled grass, bare earth only on roads and doorsteps: bare earth in a building's shadow reads black)
    col = g.lerp(grass, trampled, t_amt)
    col = g.lerp(col, soil, d_amt)

    # Rock: painted stone from above, and from the side where the ground is steeper than about 55 degrees (cliffs, later)
    rock_m = g.mask(vc, False, True)
    top = g.mask(g.tex(rock_tex, g.mul(xy, 1.0 / 450.0)), True, True, True)
    nx = g.one(unreal.MaterialExpressionAbs, g.mask(nrm, True, False))
    ny = g.one(unreal.MaterialExpressionAbs, g.mask(g.mask(nrm, False, True), True, False))
    nz = g.mask(g.mask(nrm, False, False, True), True, False)
    wz = g.mask(g.mask(wp, False, False, True), True, False)
    side_uv = g.lerp(g.append(g.mask(wp, True, False), wz), g.append(g.mask(g.mask(wp, False, True), True, False), wz), g.ramp(g.subn(nx, ny), -0.05, 0.05))
    side = g.mask(g.tex(rock_tex, g.mul(side_uv, 1.0 / 450.0)), True, True, True)
    rock = g.lerp(top, side, g.ramp(g.addn(g.one(unreal.MaterialExpressionOneMinus, nz), g.mul(rag, 0.1)), 0.42, 0.58))
    rock = g.lerp(rock, g.c3(ROCK_AVERAGE), g.ramp(depth, 2500.0, 9000.0))   # far away the stone's pattern fades (no tiled paving on hillsides)
    rock = g.muln(rock, g.vector("RockTint", ROCK_TINT))
    r_amt = g.sat(g.mul(g.add(g.addn(g.mul(rock_m, 1.5), g.mul(med, 0.5)), -0.5), 2.6))
    far_rock = g.ramp(depth, 30000.0, 90000.0)   # (hills a kilometre off: less stone, no earth halo, so no pasted-on blobs)
    r_amt = g.muln(r_amt, g.lerpc(1.0, 0.55, far_rock))
    fringe = g.mul(g.sat(g.mul(g.add(g.addn(g.mul(rock_m, 1.6), g.mul(g.add(rag, -0.5), 0.5)), -0.3), 2.0)), 0.25)   # a little earth between grass and stone
    fringe = g.muln(fringe, g.one(unreal.MaterialExpressionOneMinus, g.ramp(depth, 6000.0, 20000.0)))
    col = g.lerp(col, soil, g.muln(fringe, g.one(unreal.MaterialExpressionOneMinus, r_amt)))
    col = g.lerp(col, rock, r_amt)

    # Mud: river beds and their wet banks, darker earth
    mud_m = g.mask(g.mask(vc, False, False, True), True, False)
    m_amt = g.sat(g.mul(g.add(g.addn(g.mul(mud_m, 1.4), g.mul(g.add(rag, -0.5), 0.4)), -0.25), 2.6))
    col = g.lerp(col, g.muln(soil, g.c3((0.42, 0.40, 0.42))), m_amt)   # (wet: darker than the packed tan)

    # Sand: the beach, warm and pale, with the earth picture's grain in it
    sand_m = g.mul(vc, 1.0, "A")   # (the vertex colour's plain output is RGB only: alpha comes from its own pin)
    s_amt = g.sat(g.mul(g.add(g.addn(g.mul(sand_m, 1.4), g.mul(g.add(rag, -0.5), 0.4)), -0.25), 2.6))
    sand = g.muln(g.c3((0.40, 0.33, 0.21)), g.lerpc(0.86, 1.08, g.mul(g.lum(soil), 7.5)))
    col = g.lerp(col, sand, s_amt)

    # Snow by height on gentle ground, off until the mountains are built (SnowFrom 60 m: nothing is that high yet)
    snow_from, snow_to = g.scalar("SnowFrom", 6000.0), g.scalar("SnowTo", 6800.0)
    up = g.subn(wz, snow_from)
    span = g.subn(snow_to, snow_from)
    div = g.node(unreal.MaterialExpressionDivide); g.link(up, div, "A"); g.link(span, div, "B")
    snow_amt = g.muln(g.sat(g.addn(div, g.mul(g.add(rag, -0.5), 0.4))), g.ramp(nz, 0.55, 0.75))
    col = g.lerp(col, g.c3((0.82, 0.85, 0.90)), snow_amt)

    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(g.c(0.95), g.c(0.62), m_amt), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.15), unreal.MaterialProperty.MP_SPECULAR)
    # (no glow of its own: an emissive floor on bare earth went white by the paths at the low sun, when the exposure rises.
    # Worn ground in shade stays mid brown through the soil's lerp toward EARTH_AVERAGE above, which is lit like everything else.)
    finish(mat, "M_Ground")


def build_dirt(dirt_tex, cobble_tex, cobble_n, patches):
    # Packed earth laid in world space (no stretching along curves); a second, much larger sampling breaks up the tiling.
    # Oak Hill's streets (vertex red 1): cobbles down the middle, fraying into the earth toward the sides.
    mat = fresh_material("M_Dirt")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    g = Graph(mat)
    xy = g.mask(g.node(unreal.MaterialExpressionWorldPosition), True, True)
    near = g.tex(dirt_tex, g.mul(xy, 1.0 / 350.0))
    far = g.tex(dirt_tex, g.mul(xy, 1.0 / 1700.0))
    var = g.lerpc(0.86, 1.14, far, "G")   # (was 0.72-1.22: near-black blotches where the cobbles fray out)
    earth = g.muln(g.muln(g.muln(near, var, "RGB"), g.vector("Tint", (1.0, 1.0, 1.0))), g.c3(EARTH_BALANCE))
    earth = g.addn(g.mul(earth, 0.9), g.c3((0.024, 0.019, 0.013)))   # the darkest earth lifted a little (second review: near-black in shade beside the stones)
    uv =g.node(unreal.MaterialExpressionTextureCoordinate)
    edge = g.one(unreal.MaterialExpressionOneMinus, g.one(unreal.MaterialExpressionAbs, g.mul(g.add(g.mask(uv, True, False), -0.5), 2.0)))   # 0 at the sides, 1 down the middle
    # the cobbles: across nearly the whole street (2026-11-13, owner: the dirt came too far onto the path and looked
    # copy-pasted), earth washing softly over the outer stones instead of biting in. The road's own edge is at edge 0.29 (the
    # ribbon is 1.4x the road): stones fade in from there and are whole from about three quarters of the way out. The wobble
    # is two samplings at unrelated sizes averaged, so no 2.4 m repeat shows along the street.
    cob_on = g.mask(g.node(unreal.MaterialExpressionVertexColor), True, False)
    ragc_a = g.mask(g.tex(patches, g.mul(g.add(xy, 913.0), 1.0 / 370.0)), True, False)
    ragc_b = g.mask(g.tex(patches, g.mul(g.add(xy, -2671.0), 1.0 / 610.0)), True, False)
    ragc = g.mul(g.addn(ragc_a, ragc_b), 0.5)
    # (after the review: the stones' outline also drifts wider and narrower along the street (slow), and fine earth flecks
    # (speck) wash over the outer stones, so the edge frays softly instead of drawing one crisp repeated lobe)
    slow = g.mul(g.addn(g.mask(g.tex(patches, g.mul(xy, 1.0 / 650.0)), True, False), g.mask(g.tex(patches, g.mul(g.add(xy, 4409.0), 1.0 / 1130.0)), True, False)), 0.5)
    speck = g.mul(g.addn(g.mask(g.tex(dirt_tex, g.mul(xy, 1.0 / 32.0)), True, False), g.mask(g.tex(dirt_tex, g.mul(g.add(xy, 377.0), 1.0 / 53.0)), True, False)), 0.5)
    # (second review, 2026-11-14: the stones still ended in a crisp, crunchy cut-out. Now a wider band that starts just outside
    # the road, in which the stones drop out WHOLE, one by one (each stone's own number in T_Cobble's alpha against how far into
    # the band it lies), so the street thins into scattered stray stones with earth washed over them instead of a cut edge)
    cob_edge = g.addn(g.addn(g.add(edge, -0.22), g.mul(g.add(ragc, -0.5), 0.30)), g.mul(g.add(slow, -0.5), 0.18))
    band = g.muln(cob_on, g.sat(g.addn(g.mul(cob_edge, 2.4), g.mul(g.add(speck, -0.5), 0.25))))   # 0 just outside the road's edge, 1 from about two thirds of the way in (1.6 from -0.33 left the side streets half bare)
    ctex = g.tex(cobble_tex, g.mul(xy, 1.0 / 250.0))
    stones = g.muln(g.mask(ctex, True, True, True), g.lerpc(0.9, 1.08, far, "G"))
    pick = g.mul(ctex, 1.0, "A")   # each stone's own number (0.04 to 1; 0 in the joints)
    cob = g.sat(g.mul(g.add(g.addn(pick, g.mul(band, 1.4)), -1.0), 7.0))   # (1.4: most stones stay until the band is well under half) a stone stays if its number beats what the band leaves
    stones = g.lerp(stones, earth, g.mul(g.one(unreal.MaterialExpressionOneMinus, band), 0.3))   # earth washed over the outer stones
    g.out(g.lerp(earth, stones, cob), unreal.MaterialProperty.MP_BASE_COLOR)
    flat = g.c3((0.0, 0.0, 1.0))
    bumps = g.mask(g.tex(cobble_n, g.mul(xy, 1.0 / 250.0), normal=True), True, True, True)
    g.out(g.lerp(flat, bumps, cob), unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerp(g.c(0.92), g.c(0.78), cob), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.2), unreal.MaterialProperty.MP_SPECULAR)
    # The edge: ragged in world space and dithered over a soft band, so the dirt frays into the grass instead of stopping at a line
    # (each noise is two samplings at unrelated sizes averaged: one alone repeated its bites every few metres down the street)
    rag = g.mul(g.addn(g.mask(g.tex(dirt_tex, g.mul(xy, 1.0 / 260.0)), True, False), g.mask(g.tex(dirt_tex, g.mul(g.add(xy, 1531.0), 1.0 / 410.0)), True, False)), 0.5)
    noise = g.sat(g.mul(g.add(rag, -0.15), 3.5))
    # (slow: the wander a metre or two long, made above, so the edge bulges and bites in)
    alpha = g.sat(g.mul(g.addn(g.addn(g.add(edge, -0.12), g.mul(noise, -0.16)), g.mul(slow, -0.22)), 2.4))   # a broad, gentle band
    # (speck, made above: fine grit breaks the soft band into scattered specks of dirt and grass)
    grit = g.addn(g.mul(g.add(g.mul(speck, 3.5), -1.0), 0.40), g.mul(g.add(speck, -0.5), 0.6))   # (0.55 and 0.9 until the second review: the crumbs read pixel-noisy at 60% render scale)   # (the second, centred term since the review: the outline breaks into crumbs over a wider band, the edge's average place unchanged)
    g.out(g.addn(g.addn(g.addn(alpha, grit), g.mul(cob, 2.0)), g.mul(band, 3.0)), unreal.MaterialProperty.MP_OPACITY_MASK)   # (stones and the street's own band are always solid: no holes to the ground below)
    finish(mat, "M_Dirt")


def build_stone(rock_tex, patches):
    # Loose stones at building foundations: the painted rock, from above and from the side by the surface's facing, with a
    # touch of moss on their tops
    mat = fresh_material("M_Stone")
    for flag in ("used_with_instanced_static_meshes",):
        mat.set_editor_property(flag, True)
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    nx = g.one(unreal.MaterialExpressionAbs, g.mask(nrm, True, False))
    ny = g.one(unreal.MaterialExpressionAbs, g.mask(g.mask(nrm, False, True), True, False))
    nz = g.mask(g.mask(nrm, False, False, True), True, False)
    wz = g.mask(g.mask(wp, False, False, True), True, False)
    top = g.mask(g.tex(rock_tex, g.mul(g.mask(wp, True, True), 1.0 / 160.0)), True, True, True)
    sx = g.mask(g.tex(rock_tex, g.mul(g.append(g.mask(g.mask(wp, False, True), True, False), wz), 1.0 / 160.0)), True, True, True)
    sy = g.mask(g.tex(rock_tex, g.mul(g.append(g.mask(wp, True, False), wz), 1.0 / 160.0)), True, True, True)
    side = g.lerp(sy, sx, g.ramp(g.subn(nx, ny), -0.15, 0.15))
    col = g.lerp(side, top, g.ramp(nz, 0.45, 0.75))
    col = g.muln(col, g.vector("Tint", (1.0, 1.0, 1.0)))
    moss = g.mul(g.muln(g.ramp(nz, 0.6, 0.9), g.mask(g.tex(patches, g.mul(g.mask(wp, True, True), 1.0 / 90.0)), True, False)), 0.45)
    col = g.lerp(col, g.c3((0.07, 0.11, 0.035)), moss)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.c(0.85), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.25), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, "M_Stone")


grass_tex = import_texture(ground_textures.grass_ground(), "T_GrassGround")
rock_tex = import_texture(ground_textures.rock(), "T_Rock")
cobble_tex = import_texture(ground_textures.cobble(), "T_Cobble", alpha=True)
cobble_n = import_texture(ground_textures.cobble_normal(), "T_CobbleN", normal=True)
dirt_tex = import_texture(gen_textures.dirt(), "T_Dirt")
patches = unreal.EditorAssetLibrary.load_asset("/Game/T_Patches")
if patches is None:
    patches = import_texture(gen_textures.patches(), "T_Patches")
build_ground(grass_tex, dirt_tex, rock_tex, patches)
build_dirt(dirt_tex, cobble_tex, cobble_n, patches)
build_stone(rock_tex, patches)
for old in ("M_WorldGround", "T_Ground"):   # the old flat world material and the old ground picture: replaced
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + old):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + old)
unreal.log("WORLDDIAG world ground material built")
