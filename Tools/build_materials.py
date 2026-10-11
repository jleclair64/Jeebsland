# Rebuilds the foliage and bark materials (M_Grass, M_Bush, M_TreeLeaf, M_BarkTint) from the textures already in /Game.
# Run with Tools\BuildMaterials.ps1 (the editor must be closed). The textures were painted by MakeGrassCards.bat.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def fresh_material(name):
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + name):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + name)
    return tools.create_asset(name, "/Game", unreal.Material, unreal.MaterialFactoryNew())


def set_flags(mat, flags):
    for flag in flags:
        try:
            mat.set_editor_property(flag, True)
        except Exception as e:
            unreal.log("MATDIAG flag " + flag + ": " + str(e))


def add_wind(mat, strength, tip_only, lift):
    """A gentle breeze: broad gusts sweep slowly across the land from one side, rising and fading,
    with a faint flutter on top. strength = how far a tip leans (cm) in a full gust."""
    def node(cls, **props):
        n = lib.create_material_expression(mat, cls, 0, 600)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(a, b, pin=""):
        lib.connect_material_expressions(a, "", b, pin)

    def mul(a, c):
        n = node(unreal.MaterialExpressionMultiply, const_b=c); link(a, n, "A"); return n

    def add(a, c):
        n = node(unreal.MaterialExpressionAdd, const_b=c); link(a, n, "A"); return n

    def muln(a, b):
        n = node(unreal.MaterialExpressionMultiply); link(a, n, "A"); link(b, n, "B"); return n

    def addn(a, b):
        n = node(unreal.MaterialExpressionAdd); link(a, n, "A"); link(b, n, "B"); return n

    def sine(a):
        n = node(unreal.MaterialExpressionSine, period=1.0); link(a, n); return n

    def mask(src, r, g, b):
        n = node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=False); link(src, n); return n

    t = node(unreal.MaterialExpressionTime)
    wp = node(unreal.MaterialExpressionWorldPosition)
    wx = mask(wp, True, False, False)
    wy = mask(wp, False, True, False)
    rnd = node(unreal.MaterialExpressionPerInstanceRandom)

    # Gust bands: two long waves travelling downwind (roughly +X) at different speeds and slightly different angles.
    # Where their crests meet a gust passes; because the speeds don't divide evenly the pattern never quite repeats.
    # (Sine has period 1, so inputs are in cycles: position / wavelength - time * speed / wavelength.)
    s1 = sine(addn(addn(mul(wx, 0.000152), mul(wy, 0.000068)), mul(t, -0.058)))   # 60 m bands, 3.5 m/s
    s2 = sine(addn(addn(mul(wx, 0.000216), mul(wy, 0.000162)), mul(t, -0.076)))   # 37 m bands, 2.8 m/s
    s3 = sine(addn(addn(mul(wx, -0.000027), mul(wy, 0.000061)), mul(t, 0.008)))   # a very slow swell: some minutes breezier than others
    u1 = add(mul(s1, 0.5), 0.5)
    u2 = add(mul(s2, 0.5), 0.5)
    u3 = add(mul(s3, 0.35), 0.65)
    gust = node(unreal.MaterialExpressionSaturate)
    link(mul(add(muln(u1, u2), -0.15), 1.9), gust)
    gust2 = muln(muln(gust, gust), u3)

    # a faint flutter on each instance's own beat: barely there in calm air, livelier inside a gust
    flutter = sine(addn(mul(t, 0.6), addn(rnd, mul(wx, 0.002))))
    flutter_amp = add(mul(gust2, 0.35), 0.15)
    total = addn(gust2, muln(flutter, flutter_amp))           # lean downwind as a gust passes, plus the flutter

    if tip_only:
        uv = node(unreal.MaterialExpressionTextureCoordinate)
        tip = node(unreal.MaterialExpressionOneMinus); link(mask(uv, False, True, False), tip)   # V = 0 at the top of the card
        total = muln(total, tip)

    dirv = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(1.0, 0.45, lift, 0.0))
    final = muln(mul(total, strength), dirv)
    lib.connect_material_property(final, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def soft_edges(mat, ts):
    """Opacity through a temporal dither, so cut-out edges blend softly with anti-aliasing instead of stair-stepping"""
    try:
        cls = unreal.load_class(None, "/Script/Engine.MaterialExpressionDitherTemporalAA")
        if cls is None:
            raise RuntimeError("no DitherTemporalAA class")
        d = lib.create_material_expression(mat, cls, -300, 500)
        lib.connect_material_expressions(ts, "A", d, "")
        lib.connect_material_property(d, "", unreal.MaterialProperty.MP_OPACITY_MASK)
        mat.set_editor_property("opacity_mask_clip_value", 0.35)
    except Exception as e:
        unreal.log("MATDIAG dither unavailable, plain cut-out: " + str(e))
        lib.connect_material_property(ts, "A", unreal.MaterialProperty.MP_OPACITY_MASK)


def fluffy_normal(mat, amount=0.8):
    """Light every card as if it faced mostly upward, so a canopy or a lawn shades as one soft mass instead of
    thousands of cards each catching the sun differently (the sparkly, sharp look)"""
    try:
        up = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 700)
        up.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
        tr = lib.create_material_expression(mat, unreal.MaterialExpressionTransform, -350, 700)
        tr.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
        tr.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
        lib.connect_material_expressions(up, "", tr, "")
        flat = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 800)
        flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
        mix = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, 700)
        mix.set_editor_property("const_alpha", amount)
        lib.connect_material_expressions(flat, "", mix, "A")
        lib.connect_material_expressions(tr, "", mix, "B")
        lib.connect_material_property(mix, "", unreal.MaterialProperty.MP_NORMAL)
    except Exception as e:
        unreal.log("MATDIAG fluffy normal skipped: " + str(e))


def patchy(mat, col):
    """Big soft patches across the land, some lusher, some drier, and each tuft a shade lighter or darker than its neighbour"""
    patches = unreal.EditorAssetLibrary.load_asset("/Game/T_Patches")
    if patches is None:
        return col
    g = Graph(mat)
    xy = g.mask(g.node(unreal.MaterialExpressionWorldPosition), True, True)
    p = g.tex(patches, g.mul(xy, 1.0 / 4500.0))
    shade = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.const3((0.86, 1.0, 0.86)), shade, "A"); g.link(g.const3((1.04, 1.0, 0.84)), shade, "B"); g.link(p, shade, "Alpha", "R")   # deeper to lighter green
    each = g.add(g.mul(g.node(unreal.MaterialExpressionPerInstanceRandom), 0.24), 0.88)
    return g.muln(g.muln(col, shade), each)


def build_foliage(name, tex_name, tint_rgb, strength, tip_only, lift, param=False, gain=1.0, wind=True, vary=False, glow_amt=0.05, crown_shade=False):
    tex = unreal.EditorAssetLibrary.load_asset("/Game/" + tex_name)
    if tex is None:
        raise RuntimeError("MATDIAG missing texture " + tex_name + ": run MakeGrassCards.bat once")
    mat = fresh_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("opacity_mask_clip_value", 0.4)
    set_flags(mat, ("used_with_instanced_static_meshes", "used_with_static_lighting"))

    ts = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 0)
    ts.set_editor_property("texture", tex)
    if param:
        tint = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, -200)
        tint.set_editor_property("parameter_name", "Tint")
        tint.set_editor_property("default_value", unreal.LinearColor(*tint_rgb))
    else:
        tint = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, -200)
        tint.set_editor_property("constant", unreal.LinearColor(*tint_rgb))
    col = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, -100)
    lib.connect_material_expressions(ts, "RGB", col, "A")
    lib.connect_material_expressions(tint, "", col, "B")
    if gain != 1.0:
        boosted = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -100)   # the pale leaf picture is darker than a plain colour, so lift it
        lib.connect_material_expressions(col, "", boosted, "A")
        boosted.set_editor_property("const_b", gain)
        col = boosted
    if vary:
        col = patchy(mat, col)
    if crown_shade:
        # each leaf cluster carries how deep in the crown it sits (per-instance data 0): darken it that much
        try:
            cd = lib.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -500, -300)
            cd.set_editor_property("data_index", 0)
            lit = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -350, -300)
            lib.connect_material_expressions(cd, "", lit, "")
            shaded = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, -300)
            lib.connect_material_expressions(col, "", shaded, "A")
            lib.connect_material_expressions(lit, "", shaded, "B")
            col = shaded
        except Exception as e:
            unreal.log("MATDIAG crown shade skipped: " + str(e))
    lib.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(col, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, -100)   # a faint glow so deep shade is never pitch black
    lib.connect_material_expressions(col, "", glow, "A")
    glow.set_editor_property("const_b", glow_amt)
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 300)
    rough.set_editor_property("r", 1.0)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 400)
    spec.set_editor_property("r", 0.0)
    lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    soft_edges(mat, ts)
    fluffy_normal(mat)

    if wind:
        add_wind(mat, strength, tip_only, lift)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    unreal.log("MATDIAG built " + name)


def build_bark(tex):
    bark = fresh_material("M_BarkTint")
    bark.set_editor_property("two_sided", True)
    set_flags(bark, ("used_with_instanced_static_meshes", "used_with_static_lighting"))
    tint = lib.create_material_expression(bark, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.16, 0.09, 0.05, 1.0))
    # the bark picture (plates and fissures, about mid-grey) coloured by the tint
    ts = lib.create_material_expression(bark, unreal.MaterialExpressionTextureSample, -700, 200)
    ts.set_editor_property("texture", tex)
    col = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -350, 0)
    lib.connect_material_expressions(ts, "RGB", col, "A")
    lib.connect_material_expressions(tint, "", col, "B")
    col2 = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -250, 0)
    lib.connect_material_expressions(col, "", col2, "A")
    col2.set_editor_property("const_b", 2.0)
    lib.connect_material_property(col2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -250, 100)   # trunks in deep shade stay a readable brown
    lib.connect_material_expressions(col2, "", glow, "A")
    glow.set_editor_property("const_b", 0.04)
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # The trunk and branch meshes are wound so Unreal sees their outside as the back face, and a two-sided material
    # flips the normal on back faces: the bark was lit as if facing inward (sunny side dark, shadows invisible).
    # Multiplying the normal by the two-sided sign cancels that flip, so the bark uses its outward normal on either face.
    up = lib.create_material_expression(bark, unreal.MaterialExpressionConstant3Vector, -500, 450)
    up.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    sign = lib.create_material_expression(bark, unreal.MaterialExpressionTwoSidedSign, -500, 550)
    signed = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -300, 500)
    lib.connect_material_expressions(up, "", signed, "A")
    lib.connect_material_expressions(sign, "", signed, "B")
    lib.connect_material_property(signed, "", unreal.MaterialProperty.MP_NORMAL)
    rough = lib.create_material_expression(bark, unreal.MaterialExpressionConstant, -500, 250)
    rough.set_editor_property("r", 1.0)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lib.create_material_expression(bark, unreal.MaterialExpressionConstant, -500, 350)
    spec.set_editor_property("r", 0.0)
    lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    lib.recompile_material(bark)
    unreal.EditorAssetLibrary.save_asset("/Game/M_BarkTint")
    unreal.log("MATDIAG built M_BarkTint")


def import_texture(png, name):
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
        raise RuntimeError("MATDIAG the picture " + name + " did not import")
    tex.set_editor_property("compression_no_alpha", False)
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)





# ------------------------------------------------------------------ sky, moon and road materials
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_textures


class Graph:
    """Small helpers for wiring material nodes"""

    def __init__(self, mat):
        self.mat = mat

    def node(self, cls, **props):
        n = lib.create_material_expression(self.mat, cls, 0, 0)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        lib.connect_material_expressions(a, out, b, pin)

    def mul(self, a, c):
        n = self.node(unreal.MaterialExpressionMultiply, const_b=c); self.link(a, n, "A"); return n

    def add(self, a, c):
        n = self.node(unreal.MaterialExpressionAdd, const_b=c); self.link(a, n, "A"); return n

    def muln(self, a, b, out_a="", out_b=""):
        n = self.node(unreal.MaterialExpressionMultiply); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

    def addn(self, a, b):
        n = self.node(unreal.MaterialExpressionAdd); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def one(self, cls, a, out=""):
        n = self.node(cls); self.link(a, n, "", out); return n

    def power(self, a, e):
        n = self.node(unreal.MaterialExpressionPower, const_exponent=e); self.link(a, n, "Base"); return n

    def scalar(self, name, default):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)

    def vector(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def const3(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def mask(self, a, r, g, b=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=False); self.link(a, n); return n

    def tex(self, texture, uv=None):
        n = self.node(unreal.MaterialExpressionTextureSample, texture=texture)
        if uv is not None:
            self.link(uv, n, "UVs")
        return n

    def out(self, a, prop, pin=""):
        lib.connect_material_property(a, pin, prop)


def unlit(name, blend, two_sided=True):
    mat = fresh_material(name)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", two_sided)
    set_flags(mat, ("used_with_instanced_static_meshes",))
    return mat, Graph(mat)


def finish(mat, name):
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    unreal.log("MATDIAG built " + name)


def build_star():
    # A soft point of light. Each star (instance) has its own brightness, colour and twinkle rate.
    mat, g = unlit("M_Star", unreal.BlendMode.BLEND_ADDITIVE)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    centre = g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    dist = g.node(unreal.MaterialExpressionDistance); g.link(uv, dist, "A"); g.link(centre, dist, "B")
    falloff = g.one(unreal.MaterialExpressionSaturate, g.one(unreal.MaterialExpressionOneMinus, g.mul(dist, 2.0)))
    soft = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=3.0, const_b=1.4)   # Soft = 1: a wide fuzzy glow (comet heads)
    g.link(g.scalar("Soft", 0.0), soft, "Alpha")
    core = g.node(unreal.MaterialExpressionPower); g.link(falloff, core, "Base"); g.link(soft, core, "Exponent")
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    bright = g.addn(g.scalar("BrightMin", 0.22), g.mul(g.power(rnd, 4.0), 2.6))      # most faint, a few very bright
    rate = g.add(g.mul(g.one(unreal.MaterialExpressionFrac, g.mul(rnd, 13.7)), 0.9), 0.3)
    wave = g.node(unreal.MaterialExpressionSine, period=1.0)
    g.link(g.addn(g.muln(g.node(unreal.MaterialExpressionTime), rate), g.mul(rnd, 7.0)), wave)
    twinkle = g.node(unreal.MaterialExpressionOneMinus)
    g.link(g.muln(g.scalar("Twinkle", 0.35), g.add(g.mul(wave, 0.5), 0.5)), twinkle)
    hue = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.const3((0.72, 0.84, 1.0)), hue, "A"); g.link(g.const3((1.0, 0.86, 0.66)), hue, "B")
    g.link(g.one(unreal.MaterialExpressionFrac, g.mul(rnd, 7.31)), hue, "Alpha")
    e = g.muln(g.muln(g.muln(hue, core), g.muln(bright, twinkle)), g.muln(g.scalar("Night", 1.0), g.scalar("Boost", 5.0)))
    g.out(g.muln(e, g.vector("Tint", (1.0, 1.0, 1.0))), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Star")


def build_streak():
    # A streak of light, brightest at the head (+U end) and fading along the tail and toward its edges
    mat, g = unlit("M_Streak", unreal.BlendMode.BLEND_ADDITIVE)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    x = g.mask(uv, True, False)
    head = g.muln(g.power(x, 2.5), g.one(unreal.MaterialExpressionSaturate, g.mul(g.one(unreal.MaterialExpressionOneMinus, x), 8.0)))   # fades out right at the head: no hard edge
    # Fan = 1: narrow at the head, spreading out along the tail (comets)
    width = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0)
    narrow = g.add(g.mul(x, -0.75), 1.0)
    g.link(narrow, width, "B"); g.link(g.scalar("Fan", 0.0), width, "Alpha")
    across = g.node(unreal.MaterialExpressionDivide)
    g.link(g.one(unreal.MaterialExpressionAbs, g.mul(g.add(g.mask(uv, False, True), -0.5), 2.0)), across, "A"); g.link(width, across, "B")
    prof = g.power(g.one(unreal.MaterialExpressionSaturate, g.one(unreal.MaterialExpressionOneMinus, across)), 2.0)
    e = g.muln(g.muln(g.vector("Tint", (0.95, 0.93, 1.0)), g.muln(head, prof)), g.muln(g.scalar("Night", 1.0), g.scalar("Intensity", 4.0)))
    g.out(e, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Streak")


def build_moon(tex):
    # The lit part of the moon: its painted face, glowing (added on top of the sky so it looks right by day too)
    mat, g = unlit("M_Moon", unreal.BlendMode.BLEND_ADDITIVE)
    t = g.tex(tex)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    e = g.muln(g.muln(t, g.vector("Tint", (1.0, 0.8, 0.45)), "RGB"), g.muln(g.muln(g.scalar("Bright", 3.0), t, "", "A"), vc, "", "A"))
    g.out(e, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Moon")
    # The glow round it: brightest at the rim, fading out to nothing
    mat, g = unlit("M_MoonHalo", unreal.BlendMode.BLEND_ADDITIVE)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    centre = g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    dist = g.node(unreal.MaterialExpressionDistance); g.link(uv, dist, "A"); g.link(centre, dist, "B")
    glow = g.power(g.one(unreal.MaterialExpressionSaturate, g.one(unreal.MaterialExpressionOneMinus, g.mul(dist, 2.0))), 3.5)
    # nothing over the moon disc itself; it fades in across the moon's outer rim, so there is no dark gap between moon and glow
    disc = g.node(unreal.MaterialExpressionDistance); g.link(uv, disc, "A"); g.link(g.mask(g.vector("DiscCenter", (0.5, 0.5, 0.0)), True, True), disc, "B")
    rim = g.scalar("DiscRadius", 0.156)
    outside = g.node(unreal.MaterialExpressionDivide)
    sub = g.node(unreal.MaterialExpressionSubtract); g.link(disc, sub, "A"); g.link(g.mul(rim, 0.86), sub, "B"); g.link(sub, outside, "A")
    g.link(g.mul(rim, 0.14), outside, "B")
    mask = g.one(unreal.MaterialExpressionSaturate, outside)
    e = g.muln(g.muln(g.vector("Tint", (1.0, 0.8, 0.45)), g.scalar("Bright", 3.0)), g.muln(g.mul(glow, 0.22), mask))
    g.out(e, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_MoonHalo")
    # The unlit part: a dark disc, faintly earth-lit, that hides the stars behind it at night and vanishes by day
    mat, g = unlit("M_MoonDark", unreal.BlendMode.BLEND_TRANSLUCENT)
    t = g.tex(tex)
    g.out(g.muln(t, g.scalar("EarthShine", 0.04), "RGB"), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.muln(t, g.scalar("Opacity", 1.0), "A"), unreal.MaterialProperty.MP_OPACITY)
    finish(mat, "M_MoonDark")


def build_cumulus(tex):
    # Puffy clouds: R = how much cloud, G = how lit. Lit tops and shaded undersides take their colours from the sun.
    mat, g = unlit("M_Cumulus", unreal.BlendMode.BLEND_TRANSLUCENT)
    t = g.tex(tex)
    col = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.vector("Shade", (0.70, 0.76, 0.88)), col, "A"); g.link(g.vector("Lit", (1.0, 1.0, 1.0)), col, "B"); g.link(t, col, "Alpha", "G")
    g.out(g.muln(col, g.scalar("Bright", 2.3)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.muln(t, g.scalar("Opacity", 0.95), "R"), unreal.MaterialProperty.MP_OPACITY)
    finish(mat, "M_Cumulus")


def build_dirt(tex):
    # Packed earth laid in world space (no stretching along curves); a second, much larger sampling breaks up the tiling
    mat = fresh_material("M_Dirt")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    g = Graph(mat)
    xy = g.mask(g.node(unreal.MaterialExpressionWorldPosition), True, True)
    near = g.tex(tex, g.mul(xy, 1.0 / 350.0))
    far = g.tex(tex, g.mul(xy, 1.0 / 1700.0))
    var = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=0.72, const_b=1.22)
    g.link(far, var, "Alpha", "G")
    base = g.muln(g.muln(near, var, "RGB"), g.vector("Tint", (1.0, 1.0, 1.0)))
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.92), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.2), unreal.MaterialProperty.MP_SPECULAR)
    # The edge: ragged in world space and dithered over a soft band, so the dirt frays into the grass instead of stopping at a line
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    edge = g.one(unreal.MaterialExpressionOneMinus, g.one(unreal.MaterialExpressionAbs, g.mul(g.add(g.mask(uv, True, False), -0.5), 2.0)))   # 0 at the sides, 1 down the middle
    rag = g.tex(tex, g.mul(xy, 1.0 / 260.0))
    noise = g.one(unreal.MaterialExpressionSaturate, g.mul(g.add(g.mask(rag, True, False), -0.15), 3.5))
    # plus a slow wander a metre or two long, so the edge bulges and bites in instead of running parallel to the road
    wander = unreal.EditorAssetLibrary.load_asset("/Game/T_Patches")
    slow = g.mask(g.tex(wander, g.mul(xy, 1.0 / 650.0)), True, False) if wander is not None else g.node(unreal.MaterialExpressionConstant, r=0.5)
    alpha = g.one(unreal.MaterialExpressionSaturate, g.mul(g.addn(g.addn(g.add(edge, -0.12), g.mul(noise, -0.16)), g.mul(slow, -0.22)), 2.4))   # a broad, gentle band
    speck = g.tex(tex, g.mul(xy, 1.0 / 32.0))   # fine grit breaks the soft band into scattered specks of dirt and grass
    grit = g.mul(g.add(g.mul(g.mask(speck, True, False), 3.5), -1.0), 0.55)
    g.out(g.addn(alpha, grit), unreal.MaterialProperty.MP_OPACITY_MASK)
    finish(mat, "M_Dirt")


def build_ground(tex, dirt):
    # The soil under the grass: a painted, world-aligned texture with large soft light and dark patches across it
    mat = fresh_material("M_Ground")
    g = Graph(mat)
    xy = g.mask(g.node(unreal.MaterialExpressionWorldPosition), True, True)
    near = g.tex(tex, g.mul(xy, 1.0 / 300.0))
    far = g.tex(tex, g.mul(xy, 1.0 / 2400.0))
    var = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=0.78, const_b=1.18)
    g.link(far, var, "Alpha", "G")
    grass = g.muln(g.muln(near, var, "RGB"), g.vector("Tint", (1.0, 1.0, 1.0)))
    # beside the roads (vertex colour red) the ground turns to trodden soil, in ragged patches rather than a smooth band
    soil = g.mul(g.tex(dirt, g.mul(xy, 1.0 / 350.0)), 0.95)
    worn = g.mask(g.node(unreal.MaterialExpressionVertexColor), True, False)
    ragged = g.tex(dirt, g.mul(xy, 1.0 / 140.0))
    mix_amt = g.one(unreal.MaterialExpressionSaturate, g.mul(g.add(g.addn(g.mul(worn, 1.25), g.mul(g.mask(ragged, True, False), 1.4)), -0.55), 1.8))
    blend = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(grass, blend, "A"); g.link(soil, blend, "B"); g.link(mix_amt, blend, "Alpha")
    g.out(blend, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.95), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.15), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, "M_Ground")


def build_cloth():
    # The knight's cape: plain lit cloth in a Color parameter, seen from both sides, always shaded with the normal the
    # cloth simulation gives it (the two-sided sign cancels the engine's back-face flip)
    mat = fresh_material("M_Cloth")
    mat.set_editor_property("two_sided", True)
    g = Graph(mat)
    g.out(g.vector("Color", (0.6, 0.1, 0.08)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.85), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.2), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.muln(g.const3((0.0, 0.0, 1.0)), g.node(unreal.MaterialExpressionTwoSidedSign)), unreal.MaterialProperty.MP_NORMAL)
    finish(mat, "M_Cloth")


def imported(png, name):
    import_texture(png, name)
    return unreal.EditorAssetLibrary.load_asset("/Game/" + name)


import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_BushLeaves.png"), "T_BushLeaves")   # painted by BuildMaterials.ps1

import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_LeafSingle.png"), "T_LeafSingle")   # painted by BuildMaterials.ps1
import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_GrassTuft.png"), "T_GrassTuft")     # painted by BuildMaterials.ps1
import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_TreeLeaves.png"), "T_TreeLeaves")   # painted by BuildMaterials.ps1

#              name          texture          tint                     gust lean cm  tips only  lift
imported(gen_textures.patches(), "T_Patches")
build_foliage("M_Grass",    "T_GrassTuft",   (0.47, 0.58, 0.38, 0.0), 9.0,          True,      0.0, vary=True)   # a softer, more natural green
build_foliage("M_Bush",     "T_BushLeaves",  (0.70, 0.78, 0.52, 0.0), 4.0,          True,      0.0)
build_foliage("M_TreeLeaf", "T_TreeLeaves",  (1.0, 1.0, 1.0, 0.0),    10.0,         False,     0.3, param=True, gain=0.88, crown_shade=True)
# one loose leaf (flying, falling, lying on the ground and in piles): same painted style and tints as the tree leaves, no wind sway
import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_LeafLitter.png"), "T_LeafLitter")   # painted by BuildMaterials.ps1
build_foliage("M_LeafLitter", "T_LeafLitter", (1.0, 1.0, 1.0, 0.0),   0.0,          False,     0.0, param=True, gain=1.0, wind=False, glow_amt=0.14)   # fallen leaves: a scatter of small leaves per card
build_foliage("M_FlyLeaf",  "T_LeafSingle",  (1.0, 1.0, 1.0, 0.0),    0.0,          False,     0.0, param=True, gain=1.0, wind=False, glow_amt=0.14)   # fallen leaves stay bright even in shade
build_bark(imported(gen_textures.bark(), "T_Bark"))

build_star()
build_streak()
build_moon(imported(gen_textures.moon(), "T_Moon"))
build_cumulus(imported(gen_textures.cumulus(), "T_Cumulus"))
for old in ("M_MilkyWay", "T_MilkyWay", "M_Cirrus", "T_Cirrus"):   # taken out
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + old):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + old)
build_dirt(imported(gen_textures.dirt(), "T_Dirt"))
build_ground(imported(gen_textures.ground(), "T_Ground"), unreal.EditorAssetLibrary.load_asset("/Game/T_Dirt"))
build_cloth()
unreal.log("MATDIAG all materials built")
