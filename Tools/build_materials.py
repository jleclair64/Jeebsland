# Rebuilds the foliage and bark materials (M_Grass, M_Bush, M_TreeLeaf, M_BarkTint) from the textures already in /Game.
# Run with Tools\BuildMaterials.ps1 (the editor must be closed). The textures were painted by MakeGrassCards.bat.
import os
import unreal

GRASS_DESATURATION = 0.40   # how far the grass and the ground under it are pulled toward grey (0.15 until 2026-11-13: "too nuclear green"; 0.22, then 0.32 after the visual review against the reference painting, 0.40 after the second: the sunlit foreground blades still read lime)
GRASS_SHADOW_LIFT = 0.55    # how much the darkest parts of the grass are brightened (lights untouched)

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


def add_wind(mat, strength, tip_only, lift, attach_mask=None):
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

    # Gust field, in the manner of Breath of the Wild: not lines, but broad, soft, wobbly patches of stronger wind that roll
    # across the land with the breeze. Two layers of different size and speed (a big slow one, and a finer faster one riding
    # inside it) are laid out in the wind's own frame (along the wind, and across it) and bent by slow sine warps so no edge is
    # straight. Where a patch passes, the grass leans further downwind, the lean direction swings a little, and the blades
    # catch more light (so you see the gust roll through the field as a pale wave, not just a shear).
    ux = addn(mul(wx, 0.94), mul(wy, 0.34))          # distance along the wind (cm)
    sx = addn(mul(wy, 0.94), mul(wx, -0.34))         # distance across it

    def half(a):                                       # sine mapped to 0..1
        return add(mul(sine(a), 0.5), 0.5)

    def patch_layer(along_len, across_len, speed, lag, seed):
        u = addn(mul(ux, 1.0 / along_len), mul(t, -speed * 100.0 / along_len))      # cycles; the pattern travels at `speed` m/s
        s = mul(sx, 1.0 / across_len)
        # wobbly fronts: the front line is bent by a slow sine of its position across the wind, itself bent by one along it
        wob_s = mul(sine(addn(s, add(mul(sine(add(mul(u, 0.45), seed)), 0.17), seed * 0.7))), 0.30)
        front = half(addn(u, wob_s))
        # which stretches of the field catch it: also wavering, and slowly drifting
        wob_u = mul(sine(add(mul(u, 0.4), seed * 1.3)), 0.25)
        reach = half(add(addn(mul(s, 0.7), wob_u), seed))
        both = muln(front, reach)
        soft = node(unreal.MaterialExpressionSaturate); link(mul(add(both, -lag), 3.2), soft)   # patches with soft edges
        return muln(soft, soft)

    big = patch_layer(7500.0, 5200.0, 3.2, 0.26, 0.13)      # ~75 m long, ~50 m across, drifting at 3.2 m/s
    fine = patch_layer(2600.0, 1900.0, 4.6, 0.33, 0.61)     # ~26 m ripples, 4.6 m/s
    # the fine ripples live inside the big patches and ride on the open grass a little too
    gust_field = node(unreal.MaterialExpressionSaturate)
    link(addn(muln(big, add(mul(fine, 0.8), 0.45)), mul(fine, 0.12)), gust_field)
    tip_amp = 1.6 if tip_only else 0.5
    total = addn(total, mul(gust_field, tip_amp))
    # lean direction swings a little as the gusts pass (about 20 to 45 degrees off the +X axis)
    swing = add(mul(sine(addn(addn(mul(wx, 0.00006), mul(wy, 0.00004)), mul(t, 0.013))), 0.045), 0.06)
    cos_a = node(unreal.MaterialExpressionCosine, period=1.0); link(swing, cos_a)
    sin_a = sine(swing)
    # lighten the blades where a gust is passing: returned so the colour can use it
    shade = add(mul(gust_field, 0.20 if tip_only else 0.10), 1.0)

    if tip_only:
        uv = node(unreal.MaterialExpressionTextureCoordinate)
        tip = node(unreal.MaterialExpressionOneMinus); link(mask(uv, False, True, False), tip)   # V = 0 at the top of the card
        total = muln(total, tip)
        shade = add(mul(muln(gust_field, tip), 0.22), 1.0)
    if attach_mask is not None:
        # cloth (Tools/build_town_life_materials.py): sway only as far from the attached edge as the mask says (0 = pinned, 1 = free)
        total = muln(total, attach_mask)
        shade = add(mul(muln(gust_field, attach_mask), 0.10), 1.0)

    dirv = node(unreal.MaterialExpressionAppendVector); link(cos_a, dirv, "A"); link(sin_a, dirv, "B")
    dirv3 = node(unreal.MaterialExpressionAppendVector); link(dirv, dirv3, "A")
    link(node(unreal.MaterialExpressionConstant, r=lift), dirv3, "B")
    final = muln(mul(total, strength), dirv3)
    lib.connect_material_property(final, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    return shade


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
        return d, ""
    except Exception as e:
        unreal.log("MATDIAG dither unavailable, plain cut-out: " + str(e))
        lib.connect_material_property(ts, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        return ts, "A"   # (the node and output pin that feed the opacity mask)


# Tree crowns hand over to their far stand-ins (Forest.cpp, M_TreeFar) over a band of distance, FadeStart..FadeEnd. Both sides
# test the same screen noise against how far into the band the pixel is: the near crown keeps a pixel where the far one drops it,
# so the two cross-fade (smoothed by TSR / TAA) with no pop and never both on one pixel.
FADE_NEAR_HLSL = ("#if SHADOW_DEPTH_SHADER\nreturn 1.0;\n#endif\n"   # (shadows keep the whole near crown; the far stand-ins cast none)
                  "float n = frac(52.9829189 * frac(dot(Parameters.SvPosition.xy + float(View.StateFrameIndexMod8) * float2(2.083, 4.867), "
                  "float2(0.06711056, 0.00583715))));\nreturn n >= F ? 1.0 : 0.0;")
FADE_FAR_HLSL = FADE_NEAR_HLSL.replace("n >= F", "n < F")


def tree_fade_keep(mat, far):
    """1 where this crown (near, or its far stand-in) draws the pixel, 0 where the other one does. Shadows keep the whole
    near crown (the far stand-ins cast none)."""
    try:
        g = Graph(mat)
        d = g.node(unreal.MaterialExpressionDistance)
        g.link(g.node(unreal.MaterialExpressionWorldPosition), d, "A"); g.link(g.node(unreal.MaterialExpressionCameraPositionWS), d, "B")
        start, end = g.scalar("FadeStart", 7000.0), g.scalar("FadeEnd", 9000.0)
        into = g.node(unreal.MaterialExpressionSubtract); g.link(d, into, "A"); g.link(start, into, "B")
        width = g.node(unreal.MaterialExpressionSubtract); g.link(end, width, "A"); g.link(start, width, "B")
        frac_in = g.node(unreal.MaterialExpressionDivide); g.link(into, frac_in, "A"); g.link(width, frac_in, "B")
        f = g.one(unreal.MaterialExpressionSaturate, frac_in)
        # (worked out per vertex and handed on, so the pixels only compare it with the noise)
        try:
            vi = g.node(unreal.MaterialExpressionVertexInterpolator)
            if lib.connect_material_expressions(f, "", vi, "VS"):
                f = vi
            else:
                unreal.log("MATDIAG tree fade: per-pixel distance (no vertex interpolator pin)")
        except Exception as e:
            unreal.log("MATDIAG tree fade: per-pixel distance: " + str(e))
        c = g.node(unreal.MaterialExpressionCustom)
        c.set_editor_property("code", FADE_FAR_HLSL if far else FADE_NEAR_HLSL)
        c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        c.set_editor_property("description", "TreeFade")
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", "F")
        c.set_editor_property("inputs", [ci])
        g.link(f, c, "F")
        return c
    except Exception as e:
        unreal.log("MATDIAG tree fade skipped: " + str(e))
        return None


def build_tree_far():
    """The far stand-ins for tree crowns (Forest.cpp): soft leafy puffs for broad-leaf trees, tiered cones for conifers. The same
    average leaf (or needle) colour and Tint as the near crowns, lit softly like them (normals leaning up), darker deep in the crown
    (per-instance data 0), soft clumps and a lumpy rim instead of a clean ball, dithered in over FadeStart..FadeEnd where the near
    crowns dither out. Well inside the band it folds to its own centre in the vertex shader, so it costs nothing close up."""
    patches = unreal.EditorAssetLibrary.load_asset("/Game/T_Patches")   # (soft cloudy blotches, painted by gen_textures.py)
    mat = fresh_material("M_TreeFar")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("two_sided", False)
    mat.set_editor_property("tangent_space_normal", False)
    set_flags(mat, ("used_with_instanced_static_meshes",))
    g = Graph(mat)
    conifer = g.scalar("Conifer", 0.0)
    # One picture, laid in world space (tilted, so tops and sides both get it): soft blotches about a metre across that make the
    # crown read as heaped clumps of foliage, lighter and darker, and that bite into its outline (below). Fine leaf detail would
    # be under a pixel out here and only shimmer, so the leaf picture itself is not used.
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    wz = g.mask(wp, False, False, True)
    uvw = g.addn(g.mask(wp, True, True), g.muln(g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), wz))
    blot = g.tex(patches, g.mul(uvw, 1.0 / 320.0))
    blot_r = g.mask(blot, True, False)
    # The near crown seen from afar: its picture's average colour where there are leaves (measured from T_TreeLeaves and
    # T_PineNeedles) times the same Tint, a little lighter or darker per puff, darker deep in the crown (per-instance data 0)
    mean = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.const3((0.762, 0.681, 0.575)), mean, "A"); g.link(g.const3((0.511, 0.598, 0.425)), mean, "B"); g.link(conifer, mean, "Alpha")
    gain = g.node(unreal.MaterialExpressionLinearInterpolate)   # (matched by eye against the near crowns, -TreeBand=500 against =1)
    g.link(g.scalar("Gain", 0.45), gain, "A"); g.link(g.scalar("ConeGain", 0.38), gain, "B"); g.link(conifer, gain, "Alpha")
    col = g.desat(g.muln(g.muln(mean, g.vector("Tint", (0.5, 0.2, 0.05))), gain), 0.15)   # (the near crowns mix in glimpses of ground and shade)
    col = g.muln(col, g.add(g.mul(g.node(unreal.MaterialExpressionPerInstanceRandom), 0.16), 0.92))
    clumps = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.scalar("BlotDark", 0.7), clumps, "A"); g.link(g.node(unreal.MaterialExpressionConstant, r=1.1), clumps, "B"); g.link(blot_r, clumps, "Alpha")
    col = g.muln(col, clumps)
    shade = g.one(unreal.MaterialExpressionOneMinus, g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=0))
    col = g.muln(col, shade)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(col, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    glow = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=0.05, const_b=0.16); g.link(conifer, glow, "Alpha")   # (cones: a solid shell has no bright gaps, so a higher floor or it reads near black against a low sun)
    g.out(g.muln(col, glow), unreal.MaterialProperty.MP_EMISSIVE_COLOR)   # deep shade never pitch black, as the near leaves
    g.out(g.node(unreal.MaterialExpressionConstant, r=1.0), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.0), unreal.MaterialProperty.MP_SPECULAR)
    # soft light: the surface's own normal leaned toward straight up, like the near cards' fluffy normal
    vn = g.node(unreal.MaterialExpressionVertexNormalWS)
    lean = g.node(unreal.MaterialExpressionLinearInterpolate)
    soft = g.node(unreal.MaterialExpressionLinearInterpolate); g.link(g.scalar("Soft", 0.6), soft, "A"); g.link(g.scalar("ConeSoft", 0.8), soft, "B"); g.link(conifer, soft, "Alpha")
    g.link(vn, lean, "A"); g.link(g.const3((0.0, 0.0, 1.0)), lean, "B"); g.link(soft, lean, "Alpha")
    g.out(g.one(unreal.MaterialExpressionNormalize, lean), unreal.MaterialProperty.MP_NORMAL)
    # a lumpy rim: toward the outline the blotches decide what stays, so the silhouette is heaped and uneven, not a clean ball
    facing = g.node(unreal.MaterialExpressionDotProduct)
    g.link(vn, facing, "A"); g.link(g.node(unreal.MaterialExpressionCameraVectorWS), facing, "B")
    rim = g.one(unreal.MaterialExpressionSaturate, g.mul(g.add(g.one(unreal.MaterialExpressionOneMinus, g.one(unreal.MaterialExpressionAbs, facing)), -0.3), 1.8))
    rim_amt = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=0.35); g.link(conifer, rim_amt, "Alpha")   # (cones: a calmer outline, the boughs' tiers already break it; full strength tore them into shards)
    rim = g.muln(rim, rim_amt)
    body = g.node(unreal.MaterialExpressionLinearInterpolate)
    # (and a few gaps inside too, where the blotches are darkest: the near crowns are airy, with ground showing through)
    inner = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=0.2, const_b=0.5); g.link(conifer, inner, "Alpha")   # (cones: few gaps, they read as solid boughs)
    g.link(g.addn(g.mul(blot_r, 1.4), inner), body, "A"); g.link(g.mul(blot_r, 1.4), body, "B"); g.link(rim, body, "Alpha")
    cls = unreal.load_class(None, "/Script/Engine.MaterialExpressionDitherTemporalAA")   # (missing in 5.8: then a plain cut-out)
    dith = body
    if cls is not None:
        dith = g.node(cls); g.link(body, dith, "")
    mask = dith
    keep = tree_fade_keep(mat, far=True)
    if keep is not None:
        mask = g.muln(dith, keep)
    g.out(mask, unreal.MaterialProperty.MP_OPACITY_MASK)
    mat.set_editor_property("opacity_mask_clip_value", 0.35)
    # fold away (to the instance's centre) while the whole stand-in is still well inside the band: no pixels, no overdraw
    obj = g.node(unreal.MaterialExpressionObjectPositionWS)
    dist = g.node(unreal.MaterialExpressionDistance); g.link(obj, dist, "A"); g.link(g.node(unreal.MaterialExpressionCameraPositionWS), dist, "B")
    lim = g.node(unreal.MaterialExpressionSubtract); g.link(g.scalar("FadeStart", 7000.0), lim, "A"); g.link(g.scalar("FoldMargin", 900.0), lim, "B")
    gap = g.node(unreal.MaterialExpressionSubtract); g.link(lim, gap, "A"); g.link(dist, gap, "B")
    fold = g.one(unreal.MaterialExpressionSaturate, g.mul(gap, 0.01))
    to_centre = g.node(unreal.MaterialExpressionSubtract); g.link(obj, to_centre, "A"); g.link(g.node(unreal.MaterialExpressionWorldPosition), to_centre, "B")
    g.out(g.muln(to_centre, fold), unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish(mat, "M_TreeFar")


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
    g.link(g.const3((0.84, 0.97, 0.86)), shade, "A"); g.link(g.const3((1.02, 1.02, 0.90)), shade, "B"); g.link(p, shade, "Alpha", "R")   # deeper to lighter green (the lighter patches no warmer than the base: it is olive already, warmer read as hay)
    each = g.add(g.mul(g.node(unreal.MaterialExpressionPerInstanceRandom), 0.16), 0.92)   # a little less tuft-to-tuft speckle
    # and each tuft its own warmth, from fresh olive to a dry olive-brown, as in the owner's reference painting (2026-11-13)
    warm = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(g.const3((0.98, 1.0, 1.04)), warm, "A"); g.link(g.const3((1.10, 0.94, 0.70)), warm, "B")
    g.link(g.one(unreal.MaterialExpressionFrac, g.mul(g.node(unreal.MaterialExpressionPerInstanceRandom), 7.31)), warm, "Alpha")
    out = g.desat(g.muln(g.muln(g.muln(col, shade), each), warm), GRASS_DESATURATION)   # less vibrant, still fresh
    # Softer shadows between the blades: the darkest parts are lifted (up to 55%), the light parts are left exactly as they were
    lum = g.mask(g.desat(out, 1.0), True, False)
    lift = g.one(unreal.MaterialExpressionSaturate, g.add(g.mul(lum, -3.0), 1.0))
    return g.muln(out, g.add(g.mul(lift, GRASS_SHADOW_LIFT), 1.0))


def subsurface(mat, col, share):
    """the light that shines through a card: its colour times share (grass: less, so a low sun behind it does not light the lawn up)"""
    if share == 1.0:
        lib.connect_material_property(col, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        return
    m = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 0)
    lib.connect_material_expressions(col, "", m, "A")
    m.set_editor_property("const_b", share)
    lib.connect_material_property(m, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)


def build_foliage(name, tex_name, tint_rgb, strength, tip_only, lift, param=False, gain=1.0, wind=True, vary=False, glow_amt=0.05, crown_shade=False, tree_fade=False, sss=1.0):
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
    subsurface(mat, col, sss)
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
    edge, edge_pin = soft_edges(mat, ts)
    if tree_fade:
        # tree crowns dither out over FadeStart..FadeEnd while their far stand-ins (M_TreeFar) dither in
        keep = tree_fade_keep(mat, far=False)
        if keep is not None:
            faded = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 500)
            lib.connect_material_expressions(edge, edge_pin, faded, "A")
            lib.connect_material_expressions(keep, "", faded, "B")
            lib.connect_material_property(faded, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    fluffy_normal(mat, 0.93 if vary else 0.8)   # grass is lit almost evenly, so it reads as soft and fluffy

    if wind:
        shade = add_wind(mat, strength, tip_only, lift)
        lit = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, -100)   # blades catch more light where a gust passes
        lib.connect_material_expressions(col, "", lit, "A")
        lib.connect_material_expressions(shade, "", lit, "B")
        lib.connect_material_property(lit, "", unreal.MaterialProperty.MP_BASE_COLOR)
        subsurface(mat, lit, sss)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    unreal.log("MATDIAG built " + name)


BARK_V_SCALE = 0.5   # the trunks' UVs repeat the picture 5.5 times up a tree and twice round it: half as often up keeps the
                     # square bark scans (about 1 m of bark each) square on a trunk instead of squashed


def build_bark(furrow, furrow_n, plate, plate_n):
    # Bark from CC0 scans painted into plain values (Tools/bark_textures.py, Art/Bark, 2026-11-13): furrowed broadleaf bark, or
    # plated conifer bark where the Plates parameter is 1 (redwoods, Forest.cpp), each with its own soft normal map
    bark = fresh_material("M_BarkTint")
    bark.set_editor_property("two_sided", True)
    set_flags(bark, ("used_with_instanced_static_meshes", "used_with_static_lighting"))
    tint = lib.create_material_expression(bark, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.16, 0.09, 0.05, 1.0))
    plates = lib.create_material_expression(bark, unreal.MaterialExpressionScalarParameter, -900, 300)
    plates.set_editor_property("parameter_name", "Plates")
    plates.set_editor_property("default_value", 0.0)
    tc = lib.create_material_expression(bark, unreal.MaterialExpressionTextureCoordinate, -1100, 200)
    tc.set_editor_property("u_tiling", 1.0)
    tc.set_editor_property("v_tiling", BARK_V_SCALE)

    def sample(tex, normal):
        ts = lib.create_material_expression(bark, unreal.MaterialExpressionTextureSample, -800, 200)
        ts.set_editor_property("texture", tex)
        if normal:
            ts.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        lib.connect_material_expressions(tc, "", ts, "UVs")
        return ts

    def pick(a, b):
        m = lib.create_material_expression(bark, unreal.MaterialExpressionLinearInterpolate, -600, 200)
        lib.connect_material_expressions(a, "RGB", m, "A")
        lib.connect_material_expressions(b, "RGB", m, "B")
        lib.connect_material_expressions(plates, "", m, "Alpha")
        return m

    # the bark picture (plates and fissures, about mid-grey) coloured by the tint
    ts = pick(sample(furrow, False), sample(plate, False))
    nmap = pick(sample(furrow_n, True), sample(plate_n, True))
    col = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -350, 0)
    lib.connect_material_expressions(ts, "", col, "A")
    lib.connect_material_expressions(tint, "", col, "B")
    calm = lib.create_material_expression(bark, unreal.MaterialExpressionDesaturation, -300, 0)   # the tints a touch calmer: the redwoods read orange (review, 2026-11-13)
    calm.set_editor_property("luminance_factors", unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
    lib.connect_material_expressions(col, "", calm, "")
    calm_amt = lib.create_material_expression(bark, unreal.MaterialExpressionConstant, -400, 60)
    calm_amt.set_editor_property("r", 0.15)
    lib.connect_material_expressions(calm_amt, "", calm, "Fraction")
    col2 = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -250, 0)
    lib.connect_material_expressions(calm, "", col2, "A")
    col2.set_editor_property("const_b", 2.0)
    lib.connect_material_property(col2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -250, 100)   # trunks in deep shade stay a readable brown
    lib.connect_material_expressions(col2, "", glow, "A")
    glow.set_editor_property("const_b", 0.04)
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # The trunk and branch meshes are wound so Unreal sees their outside as the back face, and a two-sided material
    # flips the normal on back faces: the bark was lit as if facing inward (sunny side dark, shadows invisible).
    # Multiplying the normal by the two-sided sign cancels that flip, so the bark uses its outward normal on either face
    # (the engine flips the whole world normal, so the bark's normal map survives it the same way).
    sign = lib.create_material_expression(bark, unreal.MaterialExpressionTwoSidedSign, -500, 550)
    signed = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -300, 500)
    lib.connect_material_expressions(nmap, "", signed, "A")
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


def import_normal(png, name):
    import_texture(png, name)
    tex = unreal.EditorAssetLibrary.load_asset("/Game/" + name)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    tex.set_editor_property("srgb", False)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    return tex





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

    def desat(self, a, amount):
        """Pull a colour toward its own grey by amount (0.15 = 15% less vibrant), keeping the brightness"""
        n = self.node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
        self.link(a, n, "")
        self.link(self.node(unreal.MaterialExpressionConstant, r=amount), n, "Fraction")
        return n

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


def build_wood(name, tex, vertical):
    # Wood for the village palisade, walkways and beams. Vertical (logs, stakes): the grain runs up the log; the picture goes once
    # round the log's own UVs and up by world height, so it never smears. Horizontal (beams, planks): the grain runs along the
    # piece, found from the piece's own long axis, so it follows a beam whichever way the wall faces. Each piece is shifted to a
    # different slice of the picture. The colour comes from a Color parameter, like M_Color.
    mat = fresh_material(name)
    set_flags(mat, ("used_with_instanced_static_meshes",))
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    uv = g.node(unreal.MaterialExpressionAppendVector)
    if vertical:
        wz = g.mask(wp, False, False, True)
        around = g.addn(g.mask(g.node(unreal.MaterialExpressionTextureCoordinate), True, False), g.mul(rnd, 5.3))
        g.link(around, uv, "A")
        g.link(g.mul(wz, 1.0 / 260.0), uv, "B")
    else:
        def world_axis(x, y, z):
            ax = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(x, y, z, 0.0))
            tr = g.node(unreal.MaterialExpressionTransform)
            tr.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
            tr.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
            g.link(ax, tr, "")
            return g.one(unreal.MaterialExpressionNormalize, tr)

        def dot(a, b):
            n = g.node(unreal.MaterialExpressionDotProduct); g.link(a, n, "A"); g.link(b, n, "B"); return n

        along = dot(wp, world_axis(1.0, 0.0, 0.0))          # the piece's long axis
        wide = dot(wp, world_axis(0.0, 1.0, 0.0))
        wz = g.mask(wp, False, False, True)
        across = g.addn(g.addn(wide, wz), g.mul(rnd, 150.0 * 5.3))   # top faces vary with the width, side faces with the height
        g.link(g.mul(across, 1.0 / 150.0), uv, "A")
        g.link(g.mul(along, 1.0 / 260.0), uv, "B")
    col = g.mul(g.muln(g.tex(tex, uv), g.vector("Color", (0.3, 0.17, 0.08)), "RGB"), 2.0)
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.mul(col, 0.04), unreal.MaterialProperty.MP_EMISSIVE_COLOR)   # deep shade stays a readable brown
    g.out(g.node(unreal.MaterialExpressionConstant, r=1.0), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.0), unreal.MaterialProperty.MP_SPECULAR)
    finish(mat, name)


def build_flame_glow():
    # A soft ball of warm light for candle flames and carved pumpkins: additive and see-through, brightest in the middle and
    # fading to nothing at the rim (a view-angle falloff), and fading out where it meets the ground. Tint and Intensity are parameters.
    mat, g = unlit("M_Flame", unreal.BlendMode.BLEND_ADDITIVE)
    rim = g.node(unreal.MaterialExpressionFresnel, exponent=1.0, base_reflect_fraction=0.0)
    core = g.power(g.one(unreal.MaterialExpressionOneMinus, rim), 5.0)   # steep falloff: no visible disc edge
    glow = g.muln(g.muln(core, g.vector("Tint", (1.0, 0.5, 0.14))), g.scalar("Intensity", 3.0))
    g.out(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Flame")


def build_smoke():
    # A puff of poison smoke: unlit purple, denser toward the middle of each ball and wispy at the rim (a view-angle falloff),
    # fading out where it meets the ground so it does not cut a hard line. Overlapping puffs make the cloud.
    mat, g = unlit("M_Smoke", unreal.BlendMode.BLEND_TRANSLUCENT)
    rim = g.node(unreal.MaterialExpressionFresnel, exponent=1.4, base_reflect_fraction=0.0)
    core = g.one(unreal.MaterialExpressionOneMinus, rim)
    rnd = g.node(unreal.MaterialExpressionPerInstanceRandom)
    vary = g.add(g.mul(rnd, 0.5), 0.75)
    opacity = g.muln(g.mul(g.muln(core, vary), 0.5), g.scalar("Density", 1.0))
    try:
        fade = g.node(unreal.MaterialExpressionDepthFade, fade_distance_default=90.0)
        g.link(opacity, fade, "Opacity")
        opacity = fade
    except Exception as e:
        unreal.log("MATDIAG depth fade skipped: " + str(e))
    g.out(g.one(unreal.MaterialExpressionSaturate, opacity), unreal.MaterialProperty.MP_OPACITY)
    deep = g.const3((0.26, 0.03, 0.46)); light = g.const3((0.70, 0.34, 1.0))
    hue = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(deep, hue, "A"); g.link(light, hue, "B"); g.link(rim, hue, "Alpha")
    g.out(g.mul(hue, 1.3), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_Smoke")


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


def build_sword_trail():
    # The wind trail behind a sword cut (BotW style): a thin, soft, pale wisp along the tip's path. The trail's own vertices
    # carry its fade (vertex alpha: older parts and slow parts are fainter); across the ribbon (texture V: 0 inner edge, 1
    # outer) it is fullest in the middle and fades smoothly to both edges. No streaks (owner, 2026-10-31: no lines), and only
    # a little brighter than the daylight round it: a wisp of air, not a light.
    mat, g = unlit("M_SwordTrail", unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    v = g.mask(uv, False, True)
    across = g.mul(g.muln(v, g.one(unreal.MaterialExpressionOneMinus, v)), 4.0)   # 0 at the edges, 1 in the middle
    soft = g.muln(across, across)                                               # (squared: a soft, rounded edge)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    a = g.muln(soft, g.muln(vc, g.scalar("Opacity", 0.55), "A"))
    g.out(g.one(unreal.MaterialExpressionSaturate, a), unreal.MaterialProperty.MP_OPACITY)
    g.out(g.muln(g.vector("Tint", (0.92, 0.95, 1.0)), g.scalar("Bright", 0.5)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_SwordTrail")


def build_hit_box():
    # The hit box and hurt box view (HitBoxes.h, Smash style): flat see-through colour from the vertices (rgb colour, alpha
    # opacity), drawn over everything (no depth test) so the boxes inside the body show; a little denser at the rim so each
    # capsule's outline reads (two-sided: the far side of each capsule shows through too, as a slightly denser middle)
    mat, g = unlit("M_HitBox", unreal.BlendMode.BLEND_TRANSLUCENT)
    try:
        mat.set_editor_property("disable_depth_test", True)
    except Exception as e:
        unreal.log("MATDIAG hit box depth test kept: " + str(e))
    vc = g.node(unreal.MaterialExpressionVertexColor)
    rim = g.node(unreal.MaterialExpressionFresnel, exponent=2.0, base_reflect_fraction=0.0)
    a = g.muln(vc, g.add(g.mul(rim, 0.5), 0.75), "A")
    g.out(g.one(unreal.MaterialExpressionSaturate, a), unreal.MaterialProperty.MP_OPACITY)
    g.out(g.mul(vc, 1.0), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_HitBox")


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
    # (LOW's cloud ring stands 3 km out: fogged, the haze painted it pale peach whatever its own colour at the low sun.
    # DayNight sets its colours for each sun height itself, haze included, 2026-11-13)
    mat.set_editor_property("use_translucency_vertex_fog", False)
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
    grass = g.desat(g.muln(g.muln(near, var, "RGB"), g.vector("Tint", (1.0, 1.0, 1.0))), GRASS_DESATURATION)
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
    g.out(g.node(unreal.MaterialExpressionConstant, r=1.0), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.04), unreal.MaterialProperty.MP_SPECULAR)
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
build_foliage("M_Grass",    "T_GrassTuft",   (0.60, 0.47, 0.21, 0.0), 9.0,          True,      0.0, vary=True, glow_amt=0.075, sss=0.5)   # a softer, more natural green (glow 0.04 until 2026-11-14: shaded lawn went navy-black at eye level under big trees, where the blue sky light and the ambient occlusion between the blades are all it gets; 0.075 keeps a green floor)
# (2026-11-13, owner: "too bright in the shade / low sun", "too nuclear green" in the sun, then a reference painting (a village
# slope at the low sun: a muted olive-sage green, warm yellow-green where the sun catches it, deep olive in shade): was tint
# 0.47, 0.58, 0.38 (lime, hue about 90-95 on screen at noon, shade a cool 120-130), then 0.58, 0.53, 0.35 (lit 82, shade still 120+
# against the painting's 53-62), then 0.62, 0.52, 0.27, then 0.64, 0.52, 0.25, now 0.60, 0.47, 0.21 (a little yellower and 8% darker in the sun) with more grey (warmer, toward olive in sun and shade), glow 0.10,
# all of its colour shining through. The glow is what lit the shade up at the low sun, when the exposure rises; the ground under
# the grass follows in world_material.py, GRASS_TO_MEADOW.)
build_foliage("M_Bush",     "T_BushLeaves",  (0.66, 0.66, 0.38, 0.0), 4.0,          True,      0.0)   # (was 0.70, 0.78, 0.52 until 2026-11-14: the shrubs along the walls read lime beside the olive grass)
build_foliage("M_TreeLeaf", "T_TreeLeaves",  (1.0, 1.0, 1.0, 0.0),    10.0,         False,     0.3, param=True, gain=0.88, crown_shade=True, tree_fade=True)
# pine and redwood needles: boughs of fine needles, tinted per tree in the game, swaying gently, darker deep in the crown
import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_PineNeedles.png"), "T_PineNeedles")   # painted by BuildMaterials.ps1
build_foliage("M_PineNeedle", "T_PineNeedles", (1.0, 1.0, 1.0, 0.0),  6.0,          False,     0.2, param=True, gain=0.9, crown_shade=True, tree_fade=True)
# one loose leaf (flying, falling, lying on the ground and in piles): same painted style and tints as the tree leaves, no wind sway
import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_LeafLitter.png"), "T_LeafLitter")   # painted by BuildMaterials.ps1
build_foliage("M_LeafLitter", "T_LeafLitter", (1.0, 1.0, 1.0, 0.0),   0.0,          False,     0.0, param=True, gain=1.0, wind=False, glow_amt=0.14)   # fallen leaves: a scatter of small leaves per card
build_foliage("M_FlyLeaf",  "T_LeafSingle",  (1.0, 1.0, 1.0, 0.0),    0.0,          False,     0.0, param=True, gain=1.0, wind=False, glow_amt=0.14)   # fallen leaves stay bright even in shade
BARK_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Art", "Bark")   # painted by Tools/bark_textures.py
build_bark(imported(os.path.join(BARK_DIR, "T_BarkFurrow.png"), "T_BarkFurrow"), import_normal(os.path.join(BARK_DIR, "T_BarkFurrowN.png"), "T_BarkFurrowN"),
           imported(os.path.join(BARK_DIR, "T_BarkPlate.png"), "T_BarkPlate"), import_normal(os.path.join(BARK_DIR, "T_BarkPlateN.png"), "T_BarkPlateN"))
if unreal.EditorAssetLibrary.does_asset_exist("/Game/T_Bark"):   # the old painted bark: replaced by the scans
    unreal.EditorAssetLibrary.delete_asset("/Game/T_Bark")
bark_tex = imported(gen_textures.wood(), "T_Wood")   # a proper wood grain, not the tree bark
build_wood("M_WoodV", bark_tex, True)    # logs and stakes: grain runs up
build_wood("M_WoodH", bark_tex, False)   # beams and planks: grain runs along

build_star()
build_smoke()
build_flame_glow()
build_streak()
build_sword_trail()
build_hit_box()
build_moon(imported(gen_textures.moon(), "T_Moon"))
build_cumulus(imported(gen_textures.cumulus(), "T_Cumulus"))
for old in ("M_MilkyWay", "T_MilkyWay", "M_Cirrus", "T_Cirrus"):   # taken out
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + old):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + old)
# (M_Ground, M_Dirt and their textures are built by Tools/world_material.py since 2026-11-12: one ground for the whole world.
# build_dirt and build_ground above are the old versions, no longer run, so this script never puts the old ground back.)
build_cloth()
build_tree_far()   # far stand-ins for tree crowns (Forest.cpp); after M_TreeLeaf and M_PineNeedle
unreal.log("MATDIAG all materials built")
