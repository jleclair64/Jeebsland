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
import grass_field       # noqa: E402  (the field colour shared with the grass blades, M_Grass)
import palette           # noqa: E402  (2026-11-15: the world palette, Content/Data/Palette.ini; Docs/Palette.md)

GRASS_DESATURATION = grass_field.GRASS_DESATURATION   # the grass and the ground under it pulled this far toward grey (one number, grass_field.py)
GRASS_TO_MEADOW = palette.rgb("GroundGrass")   # T_GrassGround times this reads like the meadow tufts seen from afar (no line where the grass ends);
                                         # was (0.85, 1, 0.85) until M_Grass's tint went from 0.47, 0.58, 0.38 to 0.64, 0.52, 0.25 (2026-11-13: olive, not lime), then 0.60, 0.47, 0.21 and 0.63, 0.44, 0.16 (2026-11-14): 0.85 x new R / 0.47, new G / 0.58, 0.85 x new B / 0.38
GRASS_AVERAGE = (0.0705, 0.0916, 0.0083)   # T_GrassGround v2's average (linear) after GRASS_TO_MEADOW: what it fades to far away
ROCK_AVERAGE = palette.rgb("RockFar")   # what the stone fades to far away: a darker neutral grey (lighter averages, with the haze over
                                       # them, read lavender or pink-beige against the green hills)
ROCK_PHOTO_GAIN = (2.2, 2.1, 2.0)   # (rock_face is dark rust: desaturated 70% then brightened to about the old stone's level)
ROCK_TINT = (1.0, 1.0, 1.0)   # the RockTint parameter's default
EARTH_BALANCE = palette.rgb("Earth")   # T_Dirt (average linear 0.069, 0.036, 0.016: dark rust) times this averages a light packed tan
                                     # (0.14, 0.10, 0.062): roads and worn ground never read as dark stains beside cobbles and grass
EARTH_AVERAGE = (0.14, 0.10, 0.062)   # T_Dirt x EARTH_BALANCE's average (linear)

# (the averages are the pictures' own times the palette's colour: kept here as the picture's part, so a palette change moves both)
# (the pictures' own averages: GRASS_AVERAGE and EARTH_AVERAGE above divided by the multipliers they were measured with)
GRASS_PICTURE_AVERAGE = (0.0705 / 1.139, 0.0916 / 0.759, 0.0083 / 0.358)
EARTH_PICTURE_AVERAGE = (0.14 / 2.02, 0.10 / 2.79, 0.062 / 3.95)

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


def rock_picture(g, rock_tex, uv):
    """The photo rock (Poly Haven rock_face, CC0, Art/WorldRock): a rusty brown picture pulled toward a natural grey-brown and
    brightened to the old painted stone's level (2026-11-21, owner: wanted the path's photo detail)"""
    c = g.mask(g.tex(rock_tex, uv), True, True, True)
    return g.muln(g.desat(c, 0.8), g.c3(ROCK_PHOTO_GAIN))


def rock_untiled(g, rock_tex, pos2, patches, size):
    """The photo rock without a visible repeat (2026-11-21, owner: the slopes' stone looked like a repeated pattern): one sampling
    at `size` cm and a second turned 37 degrees, 1.43x larger and offset, swapped by a broad soft noise (about 25 m), then lighter
    and darker in 50 m patches. Two texture reads plus two of the small T_Patches: still cheap."""
    a = rock_picture(g, rock_tex, g.mul(pos2, 1.0 / size))
    u = g.mask(pos2, True, False); v = g.mask(pos2, False, True)
    ru = g.subn(g.mul(u, 0.8), g.mul(v, 0.6)); rv = g.addn(g.mul(u, 0.6), g.mul(v, 0.8))
    b = rock_picture(g, rock_tex, g.mul(g.add(g.append(ru, rv), 913.0), 1.0 / (size * 1.43)))
    swap = g.mask(g.tex(patches, g.mul(pos2, 1.0 / (size * 5.5))), True, False)
    col = g.lerp(a, b, g.ramp(swap, 0.38, 0.62))
    macro = g.mask(g.tex(patches, g.mul(g.add(pos2, 3171.0), 1.0 / (size * 11.0))), True, False)
    return g.muln(col, g.lerpc(0.8, 1.15, macro))


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
        # (2026-11-14: a failed link used to pass silently, leaving the input at its default 0: the near grass asked a Lerp for an
        # "RGB" pin it doesn't have, so bare ground within about 15 m of the camera painted black. Now it stops the build.)
        if not lib.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("WORLDDIAG link failed: %s.%s -> %s.%s" % (a.get_class().get_name(), out, b.get_class().get_name(), pin))

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
    fn = grass_field.noises(g, xy, patches)   # (10 m, 17 m, 45 m, broad swathes and the dry patches: the same as the blades', M_Grass)
    med, medm = fn["med"], fn["medm"]

    # Grass: the painted picture, fading to its average far away (no tiling, no shimmer), lighter and darker in soft patches
    # two samplings at unrelated sizes, swapped in 10 m patches, so no 3 m repeat lines up into a grid
    near_a = g.tex(grass_tex, g.mul(xy, 1.0 / 300.0))
    near_b = g.tex(grass_tex, g.mul(g.add(xy, 1777.0), 1.0 / 430.0))
    near = g.muln(g.lerp(near_a, near_b, g.ramp(medm, 0.42, 0.58)), palette.colour(mat, "GroundGrass"))   # (no "RGB": a Lerp's one output is unnamed; asking for "RGB" left the near ground black)
    # past the grass blades (40 m since 2026-11-14) the ground carries the look: a broad sampling of the same picture keeps some
    # texture out to about 80 m before it settles to the average (as the rebuilt town's lawn, M_TL_Ground)
    mid = g.muln(g.tex(grass_tex, g.mul(g.add(xy, 4321.0), 1.0 / 1400.0)), palette.colour(mat, "GroundGrass"))
    near = g.lerp(near, mid, g.ramp(depth, 1500.0, 4500.0))
    far = g.ramp(depth, 8000.0, 16000.0)
    grass = g.lerp(near, g.muln(g.c3(GRASS_PICTURE_AVERAGE), palette.colour(mat, "GroundGrass")), far)
    grass = grass_field.before_grey(g, grass, fn)   # lightness and hue patches and the dry patches, exactly as the blades
    grass = g.desat(g.muln(grass, g.vector("Tint", (1.0, 1.0, 1.0))), GRASS_DESATURATION)
    grass = palette.grade_plant_extra(mat, grass)   # the palette's season and Greenness on the grass only (World on the whole, below)
    beyond = grass_field.beyond_blades(g, depth)   # (0 among the grass blades near the camera, 1 past them; grass_field.py, 2026-11-15)
    grass = grass_field.under_grass_floor(g, grass, beyond)   # the meadow's shaded floor among the blades at the low sun

    # Soil (packed earth) for worn ground, mud and the fringe of rock
    soil = g.muln(g.mask(g.tex(dirt_tex, g.mul(xy, 1.0 / 350.0)), True, True, True), palette.colour(mat, "Earth"))   # (light packed tan: M_Dirt's earth is the same)
    soil = g.lerp(soil, g.muln(g.c3(EARTH_PICTURE_AVERAGE), palette.colour(mat, "Earth")), g.c(0.45))   # its darkest specks pulled up: worn ground at walls never reads as a black stain

    # Worn: first trampled grass (paler, a little earth showing through), then packed earth, in ragged patches
    worn = g.mask(vc, True, False)
    trampled = g.lerp(g.muln(grass, g.c3((1.07, 1.04, 0.93))), soil, g.c(0.22))
    t_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.7), g.mul(g.add(rag, -0.5), 0.45)), -0.32), 2.2))   # (centred noise: nothing where worn is 0)
    d_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.45), g.mul(g.add(rag, -0.5), 1.3)), -1.0), 1.2))   # (a soft edge into the grass; walls and aprons trampled grass, bare earth only on roads and doorsteps: bare earth in a building's shadow reads black)
    col = g.lerp(grass, trampled, t_amt)
    col = g.lerp(col, soil, d_amt)

    # Rock: painted stone from above, and from the side where the ground is steeper than about 55 degrees (cliffs, later)
    rock_m = g.mask(vc, False, True)
    top = rock_untiled(g, rock_tex, xy, patches, 450.0)
    nx = g.one(unreal.MaterialExpressionAbs, g.mask(nrm, True, False))
    ny = g.one(unreal.MaterialExpressionAbs, g.mask(g.mask(nrm, False, True), True, False))
    nz = g.mask(g.mask(nrm, False, False, True), True, False)
    wz = g.mask(g.mask(wp, False, False, True), True, False)
    side_uv = g.lerp(g.append(g.mask(wp, True, False), wz), g.append(g.mask(g.mask(wp, False, True), True, False), wz), g.ramp(g.subn(nx, ny), -0.05, 0.05))
    side = rock_untiled(g, rock_tex, side_uv, patches, 450.0)
    rock = g.lerp(top, side, g.ramp(g.addn(g.one(unreal.MaterialExpressionOneMinus, nz), g.mul(rag, 0.1)), 0.42, 0.58))
    rock = g.lerp(rock, palette.colour(mat, "RockFar"), g.ramp(depth, 2500.0, 9000.0))   # far away the stone's pattern fades (no tiled paving on hillsides)
    rock = g.muln(rock, g.vector("RockTint", ROCK_TINT))
    # Where stone shows (2026-11-21, owner: the slopes' stone patches repeated as the same blobs): the 10 m noise alone used to
    # cut them; now three samplings at unrelated sizes, one turned 37 degrees, plus 45 m patches (patch sizes vary), a little of
    # the ragged 1.5 m noise for natural edges, and more stone the steeper the slope.
    xu = g.mask(xy, True, False); xv = g.mask(xy, False, True)
    rot = g.append(g.subn(g.mul(xu, 0.8), g.mul(xv, 0.6)), g.addn(g.mul(xu, 0.6), g.mul(xv, 0.8)))
    med3 = g.mask(g.tex(patches, g.mul(g.add(rot, 2711.0), 1.0 / 2300.0)), True, False)
    mix = g.addn(g.addn(g.mul(med, 0.35), g.mul(fn["med2"], 0.3)), g.mul(med3, 0.35))
    mix = g.addn(mix, g.mul(g.add(fn["patchy"], -0.5), 0.45))
    mix = g.addn(mix, g.mul(g.add(rag, -0.5), 0.18))
    steep = g.ramp(g.one(unreal.MaterialExpressionOneMinus, nz), 0.12, 0.45)
    r_amt = g.sat(g.mul(g.add(g.addn(g.addn(g.mul(rock_m, 1.5), g.mul(mix, 0.55)), g.mul(steep, 0.3)), -0.68), 2.4))
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
    s_amt = g.sat(g.mul(g.add(g.addn(g.mul(sand_m, 1.4), g.mul(g.add(rag, -0.5), 0.4)), -0.25), 1.6))   # (2026-11-19: 1.6, was 2.6: a soft blend into the grass, not a hard edge)
    sand = g.muln(palette.colour(mat, "Sand"), g.lerpc(0.86, 1.08, g.mul(g.lum(soil), 7.5)))
    col = g.lerp(col, sand, s_amt)

    # Snow by height on gentle ground, off until the mountains are built (SnowFrom 60 m: nothing is that high yet)
    snow_from, snow_to = g.scalar("SnowFrom", 6000.0), g.scalar("SnowTo", 6800.0)
    up = g.subn(wz, snow_from)
    span = g.subn(snow_to, snow_from)
    div = g.node(unreal.MaterialExpressionDivide); g.link(up, div, "A"); g.link(span, div, "B")
    snow_amt = g.muln(g.sat(g.addn(div, g.mul(g.add(rag, -0.5), 0.4))), g.ramp(nz, 0.55, 0.75))
    col = g.lerp(col, palette.colour(mat, "Snow"), snow_amt)

    # (2026-11-14: the swathes are now in grass_field.before_grey, on the grass only, the same as the blades and the town's lawn; flat ground is a little brighter than the blades, which are lit through)
    col = grass_field.ground_lift(g, col)
    col = palette.grade_world(mat, col)   # the palette's Warmth, Saturation and Brightness (identity at the defaults)

    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(g.c(0.95), g.c(0.62), m_amt), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.15), unreal.MaterialProperty.MP_SPECULAR)
    # The low sun's fill (SkyFill, set by AWorldTerrain from ADayNight::Dusk; jeeb.Ground.SkyFill 0.2 at the lowest sun): the grass
    # blades are lit through and stay bright at the low sun, flat ground goes near-black where they stop (from the air, and past the
    # town's 40 m of grass). A small warm light on the grass only: an emissive floor on bare earth went white by the paths at the
    # low sun, when the exposure rises (2026-11-12), so packed earth and mud take none of it.
    bare = g.one(unreal.MaterialExpressionOneMinus, g.sat(g.addn(g.addn(d_amt, m_amt), s_amt)))   # (2026-11-19: and sand, whose fill made the beaches glow at the low sun)
    # (2026-11-15: emissive ignores shadow, so among the blades it lit the ground under trees at dusk above the shaded blades, owner:
    # "seems to glow". Now it comes in only past the blades: grass_field.under_grass_fill.)
    fill = g.muln(g.muln(g.muln(col, g.c3((1.12, 0.97, 0.8))), g.scalar("SkyFill", 0.0)), bare)
    g.out(grass_field.under_grass_fill(g, fill, beyond), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Ground")


def build_dirt(dirt_tex, cobble_tex, cobble_n, patches, grass_tex):
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
    earth = g.muln(g.muln(g.muln(near, var, "RGB"), g.vector("Tint", (1.0, 1.0, 1.0))), palette.colour(mat, "Earth"))
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

    # The packed-earth track (Blackrock's paths and the forest's roads, 2026-11-16). Until now its outline came from T_Dirt's red
    # channel, a dark rust picture (about 0.07 on average once read as sRGB), while the sums assumed noise round 0.5: the "grit"
    # term came out near -0.56, so the track's middle summed to about 0.44, under the 0.5 cut, and nearly all of the ribbon was
    # cut away; only the cobbles (always solid) and a few specks showed. Now the outline uses T_Patches' noise, read back to its
    # painted 0..1 (it is stored as sRGB: a sample is about the painted value squared, the square root undoes that).
    def n01(scale, off):
        return g.one(unreal.MaterialExpressionSquareRoot, g.mask(g.tex(patches, g.mul(g.add(xy, off), 1.0 / scale)), True, False))
    wander = g.add(g.mul(g.addn(n01(700.0, 211.0), n01(1150.0, 5077.0)), 0.5), -0.5)   # about -0.5..0.5, a few metres long: the track widens and narrows
    crumb = g.add(g.mul(g.addn(n01(90.0, 77.0), n01(140.0, 1931.0)), 0.5), -0.5)       # under a metre: small bites and tufts at the edge
    reach = g.addn(g.addn(edge, g.mul(wander, 0.32)), g.mul(crumb, 0.16))   # how far into the track (0 the ribbon's side, 1 its middle; the road's own edge is near 0.29)
    # a smoother, lighter worn middle where feet and wheels go; toward the edges the earth takes the meadow ground's own colour
    # (T_GrassGround as M_Ground lays it) in patches, so the dithered edge melts into the grass instead of drawing tan specks on green
    middle = g.sat(g.mul(g.add(reach, -0.6), 3.0))
    calm = g.desat(g.lerp(earth, g.addn(g.mul(g.muln(g.c3(EARTH_PICTURE_AVERAGE), palette.colour(mat, "Earth")), 0.9), g.c3((0.024, 0.019, 0.013))), g.c(0.3)), 0.15)   # (the pebbles a little quieter and the earth a touch less orange: under the low sun it read rust-red)
    trodden = g.muln(calm, g.lerpc(1.0, 1.1, middle))
    meadow = g.muln(g.tex(grass_tex, g.mul(xy, 1.0 / 300.0)), palette.colour(mat, "GroundGrass"))
    fringe = g.sat(g.mul(g.addn(g.mul(g.add(reach, -0.62), -3.2), g.mul(crumb, 0.8)), 1.0))   # 1 at the outer edge, 0 from about 0.6 in
    track = g.lerp(trodden, g.lerp(meadow, earth, g.c(0.3)), g.mul(fringe, 0.8))
    g.out(palette.grade_world(mat, g.lerp(track, stones, cob)), unreal.MaterialProperty.MP_BASE_COLOR)
    flat = g.c3((0.0, 0.0, 1.0))
    bumps = g.mask(g.tex(cobble_n, g.mul(xy, 1.0 / 250.0), normal=True), True, True, True)
    g.out(g.lerp(flat, bumps, cob), unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerp(g.c(0.92), g.c(0.78), cob), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.2), unreal.MaterialProperty.MP_SPECULAR)
    # The edge: the track's reach (above, ragged in world space: each noise two samplings at unrelated sizes, so no bite repeats)
    # fades out over a soft band, broken by fine crumbs of earth and grass a few centimetres across (a world-space noise, so they
    # stay put as the camera moves; Python can't make a DitherTemporalAA node) instead of cutting a line. Solid from about 0.45 in,
    # gone by about 0.17 (the road's own edge, 0.29, is about half); the fringe's colour (above) is close to the ground's there.
    soft = g.mul(g.add(reach, -0.17), 1.0 / 0.28)   # (not clamped: well inside it stays far over the cut, so the crumbs never hole the middle)
    fine = g.add(g.mul(g.addn(n01(23.0, 4111.0), n01(37.0, 733.0)), 0.5), -0.5)
    g.out(g.addn(g.addn(g.addn(soft, g.mul(fine, 1.1)), g.mul(cob, 2.0)), g.mul(band, 3.0)), unreal.MaterialProperty.MP_OPACITY_MASK)   # (stones and the street's own band are always solid: no holes to the ground below)
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
    top = rock_picture(g, rock_tex, g.mul(g.mask(wp, True, True), 1.0 / 160.0))
    sx = rock_picture(g, rock_tex, g.mul(g.append(g.mask(g.mask(wp, False, True), True, False), wz), 1.0 / 160.0))
    sy = rock_picture(g, rock_tex, g.mul(g.append(g.mask(wp, True, False), wz), 1.0 / 160.0))
    side = g.lerp(sy, sx, g.ramp(g.subn(nx, ny), -0.15, 0.15))
    col = g.lerp(side, top, g.ramp(nz, 0.45, 0.75))
    col = g.muln(col, g.vector("Tint", (1.0, 1.0, 1.0)))
    moss = g.mul(g.muln(g.ramp(nz, 0.6, 0.9), g.mask(g.tex(patches, g.mul(g.mask(wp, True, True), 1.0 / 90.0)), True, False)), 0.45)
    col = g.lerp(col, palette.grade_plant_extra(mat, palette.colour(mat, "Moss")), moss)
    g.out(palette.grade_world(mat, col), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.c(0.85), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.25), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, "M_Stone")


def mesh_textures(mi_path):
    mi = unreal.EditorAssetLibrary.load_asset(mi_path)
    MEL = unreal.MaterialEditingLibrary
    out = {}
    for p in MEL.get_texture_parameter_names(mi):
        out[str(p)] = MEL.get_material_instance_texture_parameter_value(mi, p)
    unreal.log("WORLDDIAG %s pictures %s" % (mi_path, sorted(out.keys())))
    return out


def pick(texs, *keys):
    for k, t in texs.items():
        if t is not None and any(x.lower() in k.lower() for x in keys):
            return t
    return None


def build_boulder(rock_tex, patches):
    # The forest's boulders (2026-11-21, owner: "make the rocks look less cartoony", as the bushes were): the photo rock laid on them
    # in world space from three sides (no stretched UVs), the model's own painted shading kept only as light and dark, its normal
    # map for the shape, and a soft, darker, broken moss on the tops instead of the flat lime cap.
    texs = mesh_textures("/Game/TownDecor/MI_BoulderMossy")
    base_t, nrm_t = pick(texs, "base", "color", "albedo"), pick(texs, "normal")
    mat = fresh_material("M_Boulder")
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    nx = g.one(unreal.MaterialExpressionAbs, g.mask(nrm, True, False))
    ny = g.one(unreal.MaterialExpressionAbs, g.mask(g.mask(nrm, False, True), True, False))
    nz = g.mask(g.mask(nrm, False, False, True), True, False)
    wz = g.mask(g.mask(wp, False, False, True), True, False)
    S = 150.0
    top = rock_picture(g, rock_tex, g.mul(g.mask(wp, True, True), 1.0 / S))
    sx = rock_picture(g, rock_tex, g.mul(g.append(g.mask(g.mask(wp, False, True), True, False), wz), 1.0 / S))
    sy = rock_picture(g, rock_tex, g.mul(g.append(g.mask(wp, True, False), wz), 1.0 / S))
    col = g.lerp(sy, sx, g.ramp(g.subn(nx, ny), -0.2, 0.2))
    col = g.lerp(col, top, g.ramp(g.one(unreal.MaterialExpressionAbs, nz), 0.5, 0.8))
    if base_t is not None:   # the painted model's light and dark (cracks, hollows), not its colours
        shade = g.lum(g.tex(base_t, uv))
        col = g.muln(col, g.lerpc(0.55, 1.25, g.ramp(shade, 0.08, 0.6)))
    col = g.muln(col, g.vector("Tint", (0.88, 0.88, 0.88)))
    # Moss (2026-11-21, owner: the moss was a camouflage of same-size blotches; that was the 23 cm noise): now mostly on the tops
    # and in the model's dark hollows, in patches from three noises at unrelated sizes (one turned), soft ragged edges, a few
    # flecks, and each boulder its own amount (per instance random).
    p3 = g.mask(wp, True, True, True)
    def nz3(size, off):   # a soft noise read from above and from two sides, blended by facing (no stretched patches on the sides)
        t = g.mask(g.tex(patches, g.mul(g.add(g.mask(p3, True, True), off), 1.0 / size)), True, False)
        a = g.mask(g.tex(patches, g.mul(g.add(g.append(g.mask(p3, True, False), wz), off), 1.0 / size)), True, False)
        b = g.mask(g.tex(patches, g.mul(g.add(g.append(g.mask(g.mask(p3, False, True), True, False), wz), off), 1.0 / size)), True, False)
        return g.lerp(g.lerp(a, b, g.ramp(g.subn(nx, ny), -0.2, 0.2)), t, g.ramp(g.one(unreal.MaterialExpressionAbs, nz), 0.4, 0.8))
    big = nz3(330.0, 1234.0)
    mid = nz3(127.0, 517.0)
    fine = nz3(41.0, 2903.0)
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    amount = g.lerpc(-0.35, 0.25, rnd)   # some boulders nearly bare, some mossy
    hollow = g.c(0.0)
    if base_t is not None:
        hollow = g.one(unreal.MaterialExpressionOneMinus, g.ramp(shade, 0.1, 0.35))   # the model's dark cracks and hollows
    up = g.ramp(nz, 0.05, 0.85)
    field = g.addn(g.addn(g.mul(big, 0.55), g.mul(mid, 0.35)), g.mul(g.add(fine, -0.5), 0.22))
    field = g.addn(g.addn(field, g.mul(up, 0.75)), g.addn(g.mul(hollow, 0.25), amount))
    moss_a = g.ramp(field, 1.0, 1.28)
    fleck = g.muln(g.ramp(fine, 0.72, 0.8), g.mul(up, 0.35))   # small scattered flecks beyond the patches
    moss_a = g.sat(g.addn(moss_a, fleck))
    n2 = mid
    moss_c = g.lerp(g.c3((0.06, 0.07, 0.045)), g.c3((0.1, 0.115, 0.065)), fine)   # dark, muted olive with lighter tufts
    col = g.lerp(col, palette.grade_plant_extra(mat, moss_c), g.mul(moss_a, 0.75))
    g.out(palette.grade_world(mat, col), unreal.MaterialProperty.MP_BASE_COLOR)
    if nrm_t is not None:
        g.out(g.tex(nrm_t, uv, normal=True), unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerpc(0.82, 0.95, moss_a), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.3), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, "M_Boulder")
    sm = unreal.EditorAssetLibrary.load_asset("/Game/TownDecor/SM_BoulderMossy")
    sm.set_material(0, mat)
    unreal.EditorAssetLibrary.save_asset("/Game/TownDecor/SM_BoulderMossy")


STUMP_TINT = (0.42, 0.37, 0.33)   # the pack trees' grey-brown bark (owner 2026-11-21: the stump read orange-tan beside them)


def build_stumps():
    # One plain parent (picture, normal, roughness from the MR picture's green) with the colour desaturated and tinted to the bark,
    # then one instance per stump kind, set on each stump model's slots that used the old instance.
    mat = fresh_material("M_StumpBark")
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    g = Graph(mat)
    base = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Base")
    nrm = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Normal", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mr = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="MR", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    c = g.mask(base, True, True, True)
    lum = g.lum(c)
    col = g.lerp(g.muln(c, g.c3((0.75, 0.75, 0.75))), g.muln(g.mul(lum, 1.6), g.vector("Tint", STUMP_TINT)), g.scalar("Recolour", 0.85))
    g.out(palette.grade_world(mat, col), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(nrm, unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerpc(0.75, 1.0, g.mask(mr, False, True)), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.25), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, "M_StumpBark")
    MEL = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for kind in ("Stump", "StumpRooty", "StumpMushroom", "StumpGrass"):
        texs = mesh_textures("/Game/TownDecor/MI_" + kind)
        path = "/Game/TownDecor/MI_%sBark" % kind
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            mi = unreal.EditorAssetLibrary.load_asset(path)
        else:
            mi = tools.create_asset("MI_%sBark" % kind, "/Game/TownDecor", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, mat)
        for pn, keys in (("Base", ("base", "color", "albedo")), ("Normal", ("normal",)), ("MR", ("mr", "rough", "metal"))):
            t = pick(texs, *keys)
            if t is not None:
                MEL.set_material_instance_texture_parameter_value(mi, pn, t)
        unreal.EditorAssetLibrary.save_asset(path)
        sm = unreal.EditorAssetLibrary.load_asset("/Game/TownDecor/SM_" + kind)
        for i, sl in enumerate(sm.static_materials):
            if sl.material_interface and sl.material_interface.get_name() in ("MI_" + kind, "MI_%sBark" % kind):
                sm.set_material(i, mi)
        unreal.EditorAssetLibrary.save_asset("/Game/TownDecor/SM_" + kind)
        unreal.log("WORLDDIAG stump %s recoloured" % kind)


if os.environ.get("JEEB_WORLD_ONLY") == "dirt":
    # (2026-11-16: M_Dirt alone, from the textures already in /Game, leaving M_Ground, M_Stone and the pictures untouched:
    # set JEEB_WORLD_ONLY=dirt before Tools\BuildWorldMaterial.ps1)
    load = unreal.EditorAssetLibrary.load_asset
    build_dirt(load("/Game/T_Dirt"), load("/Game/T_Cobble"), load("/Game/T_CobbleN"), load("/Game/T_Patches"), load("/Game/T_GrassGround"))
    unreal.log("WORLDDIAG world ground material built")
else:

    palette.ensure_mpc()
    grass_tex = import_texture(ground_textures.grass_ground(), "T_GrassGround")
    rock_tex = import_texture(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Art", "WorldRock", "rock_face_diff_2k.jpg"), "T_Rock")
    cobble_tex = import_texture(ground_textures.cobble(), "T_Cobble", alpha=True)
    cobble_n = import_texture(ground_textures.cobble_normal(), "T_CobbleN", normal=True)
    dirt_tex = import_texture(gen_textures.dirt(), "T_Dirt")
    patches = unreal.EditorAssetLibrary.load_asset("/Game/T_Patches")
    if patches is None:
        patches = import_texture(gen_textures.patches(), "T_Patches")
    build_ground(grass_tex, dirt_tex, rock_tex, patches)
    build_dirt(dirt_tex, cobble_tex, cobble_n, patches, grass_tex)
    build_stone(rock_tex, patches)
    build_boulder(rock_tex, patches)
    build_stumps()
    for old in ("M_WorldGround", "T_Ground"):   # the old flat world material and the old ground picture: replaced
        if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + old):
            unreal.EditorAssetLibrary.delete_asset("/Game/" + old)
    unreal.log("WORLDDIAG world ground material built")
