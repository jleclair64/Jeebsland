# Rebuilds the foliage and bark materials (M_Grass, M_Bush, M_TreeLeaf, M_BarkTint) from the textures already in /Game.
# Run with Tools\BuildMaterials.ps1 (the editor must be closed). The textures were painted by MakeGrassCards.bat.
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
    """Breezes that roll across the land now and then, a gentle constant stir between them,
    and every instance fluttering on its own beat. strength = sway at the tip in cm during calm air."""
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

    # two slow waves roll across the map in different directions; where they line up there is a gust
    s1 = sine(addn(addn(mul(wx, 0.00020), mul(wy, 0.00013)), mul(t, -0.04)))
    s2 = sine(addn(addn(mul(wx, 0.00009), mul(wy, -0.00017)), mul(t, 0.017)))
    u1 = add(mul(s1, 0.5), 0.5)
    u2 = add(mul(s2, 0.5), 0.5)
    gust = node(unreal.MaterialExpressionSaturate)
    link(mul(add(muln(u1, u2), -0.25), 2.5), gust)
    gust2 = muln(gust, gust)

    # each instance flutters on its own random beat; calm air still stirs it, a gust shakes it hard
    flutter = sine(addn(mul(t, 0.9), addn(mul(rnd, 6.28), mul(wx, 0.01))))
    sway_amp = add(mul(gust2, 2.0), 1.0)
    total = addn(muln(flutter, sway_amp), mul(gust2, 1.5))     # sway, plus a lean downwind during a gust

    if tip_only:
        uv = node(unreal.MaterialExpressionTextureCoordinate)
        tip = node(unreal.MaterialExpressionOneMinus); link(mask(uv, False, True, False), tip)   # V = 0 at the top of the card
        total = muln(total, tip)

    dirv = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(1.0, 0.45, lift, 0.0))
    final = muln(mul(total, strength), dirv)
    lib.connect_material_property(final, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def build_foliage(name, tex_name, tint_rgb, strength, tip_only, lift, param=False, gain=1.0):
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
    glow.set_editor_property("const_b", 0.12)
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


#              name          texture          tint                     sway cm  tips only  lift
build_foliage("M_Grass",    "T_GrassTuft",   (0.72, 0.68, 0.42, 0.0), 4.0,     True,      0.0)
build_foliage("M_Bush",     "T_BushLeaves",  (0.78, 0.72, 0.50, 0.0), 2.5,     True,      0.0)
build_foliage("M_TreeLeaf", "T_TreeLeaves",  (1.0, 1.0, 1.0, 0.0),    5.0,     False,     0.35, param=True, gain=0.88)
build_bark()
unreal.log("MATDIAG all materials built")
