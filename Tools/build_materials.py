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


def build_foliage(name, tex_name, tint_rgb, strength, tip_only, lift, param=False, gain=1.0, wind=True):
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
    lib.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(col, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, -100)   # a faint glow so deep shade is never pitch black
    lib.connect_material_expressions(col, "", glow, "A")
    glow.set_editor_property("const_b", 0.07)
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 300)
    rough.set_editor_property("r", 1.0)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -700, 400)
    spec.set_editor_property("r", 0.0)
    lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    lib.connect_material_property(ts, "A", unreal.MaterialProperty.MP_OPACITY_MASK)

    if wind:
        add_wind(mat, strength, tip_only, lift)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    unreal.log("MATDIAG built " + name)


def build_bark():
    bark = fresh_material("M_BarkTint")
    bark.set_editor_property("two_sided", True)
    set_flags(bark, ("used_with_instanced_static_meshes", "used_with_static_lighting"))
    tint = lib.create_material_expression(bark, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.16, 0.09, 0.05, 1.0))
    lib.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = lib.create_material_expression(bark, unreal.MaterialExpressionMultiply, -250, 100)   # trunks in deep shade stay a readable brown
    lib.connect_material_expressions(tint, "", glow, "A")
    glow.set_editor_property("const_b", 0.03)
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
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


import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_BushLeaves.png"), "T_BushLeaves")   # painted by BuildMaterials.ps1

import_texture(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved", "T_LeafSingle.png"), "T_LeafSingle")   # painted by BuildMaterials.ps1

#              name          texture          tint                     gust lean cm  tips only  lift
build_foliage("M_Grass",    "T_GrassTuft",   (0.72, 0.68, 0.42, 0.0), 9.0,          True,      0.0)
build_foliage("M_Bush",     "T_BushLeaves",  (0.70, 0.78, 0.52, 0.0), 4.0,          True,      0.0)
build_foliage("M_TreeLeaf", "T_TreeLeaves",  (1.0, 1.0, 1.0, 0.0),    10.0,         False,     0.3, param=True, gain=0.88)
# one loose leaf (flying, falling, lying on the ground and in piles): same painted style and tints as the tree leaves, no wind sway
build_foliage("M_FlyLeaf",  "T_LeafSingle",  (1.0, 1.0, 1.0, 0.0),    0.0,          False,     0.0, param=True, gain=0.88, wind=False)
build_bark()
unreal.log("MATDIAG all materials built")
