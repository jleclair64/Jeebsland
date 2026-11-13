# Brings the buildings from Saved/KnightBuild/Buildings into /Game/Buildings (2026-11-06): one static mesh and one material instance each, on the shared M_Building.
# Run by Tools\BuildBuildings.ps1 in a windowless editor. NANITE lists the models built with Nanite.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Buildings")
folder = "/Game/Buildings"
NANITE = {"OakHillHouse_Detailed", "OakHillHouse_300k", "OakHillCornerHouse_300k", "OakHillSmith_300k", "OakHillBank_300k", "OakHillStore_300k",
          "OakHillPotionShop_300k", "OakHillTownHall_300k", "OakHillChurch_300k", "OakHillWell_150k", "GeneralStore_300k", "BankStall_300k", "PotionStall_300k"}
NAMES = sorted(n[:-4] for n in os.listdir(work) if n.endswith(".fbx"))
if not lib.does_directory_exist(folder): lib.make_directory(folder)

def pic(name, srgb, normal=False):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): return None
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    t.set_editor_property("srgb", srgb)
    if normal:
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property("flip_green_channel", True)   # (glTF's normal maps point green the other way from Unreal's)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(folder + "/" + name)
    return t

GLOW_STRENGTH = 2.0   # lit windows (T_<Name>_Glow from Tools/building_glow.py): emissive = GlowTex x GlowStrength; "slightly glow" in daylight
# per-building overrides (owner, 2026-11-12: "the windows in all the homes 35% dimmer": the two house designs only)
GLOW_BY_NAME = {"OakHillHouse_300k": GLOW_STRENGTH * 0.65, "OakHillCornerHouse_300k": GLOW_STRENGTH * 0.65}

# Lighting does the shading (owner, 2026-11-12, with Tools/building_flatten.py taking the baked light out of the colour): steadier,
# slightly rougher surfaces, and a faint warm rim of sunlight along the edges that makes the shapes pop. All material parameters.
ROUGH_TARGET = 0.8    # roughness = lerp(MRTex.G, RoughTarget, RoughEven): Meshy's roughness changes island by island (0.55-0.75 on the
ROUGH_EVEN = 0.35     #   house), which reads as a blotchy sheen; a third of the way to one value (0.6 lost glossy warmth in recesses)
RIM_STRENGTH = 0.06   # rim (added to the emissive) = Fresnel(RimPower) on the mesh's own normal x the sun's light where it reaches the
RIM_POWER = 4.0       #   ground (SkyAtmosphereLightIlluminance: warm and dim at the low sun by itself) x lerp(RimShade, 1, how far the
RIM_SHADE = 0.25      #   face turns to the sun) x halfway between the surface's colour and white x RimStrength

def make_material(default_base, default_mr, default_normal, default_glow):
    # (a picture slot with no default picture doesn't compile, and the game then silently draws it in plain grey: check the log for "Failed to compile Material")
    path = folder + "/M_Building"
    if lib.does_asset_exist(path): lib.delete_asset(path)
    m = tools.create_asset("M_Building", folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_nanite", True)
    E = mel.create_material_expression
    def tex(name, y, kind, default):
        t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
        t.set_editor_property("parameter_name", name)
        t.set_editor_property("sampler_type", kind)
        t.set_editor_property("texture", default)
        return t
    base = tex("BaseTex", -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_base)
    mr = tex("MRTex", 200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, default_mr)
    nrm = tex("NormalTex", 600, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, default_normal)
    MP = unreal.MaterialProperty
    mel.connect_material_property(base, "RGB", MP.MP_BASE_COLOR)
    mel.connect_material_property(mr, "B", MP.MP_METALLIC)
    mel.connect_material_property(nrm, "RGB", MP.MP_NORMAL)
    def scalar(name, value, x, y):
        s = E(m, unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", name); s.set_editor_property("default_value", value)
        return s
    def node(cls, x, y, **props):
        n = E(m, cls, x, y)
        for k, v in props.items(): n.set_editor_property(k, v)
        return n
    def link(a, a_out, b, b_in): mel.connect_material_expressions(a, a_out, b, b_in)
    rough = node(unreal.MaterialExpressionLinearInterpolate, -350, 250)
    link(mr, "G", rough, "A")
    link(scalar("RoughTarget", ROUGH_TARGET, -700, 380), "", rough, "B")
    link(scalar("RoughEven", ROUGH_EVEN, -700, 440), "", rough, "Alpha")
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    glow = tex("GlowTex", 1000, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_glow)
    gs = scalar("GlowStrength", GLOW_STRENGTH, -700, 1300)
    mul = node(unreal.MaterialExpressionMultiply, -350, 1100)
    link(glow, "RGB", mul, "A")
    link(gs, "", mul, "B")
    # the rim
    vn = node(unreal.MaterialExpressionVertexNormalWS, -1100, 1500)
    fr = node(unreal.MaterialExpressionFresnel, -800, 1500, base_reflect_fraction=0.0)
    link(vn, "", fr, "Normal")
    link(scalar("RimPower", RIM_POWER, -1100, 1580), "", fr, "ExponentIn")
    sun_dir = node(unreal.MaterialExpressionSkyAtmosphereLightDirection, -1100, 1700)
    facing = node(unreal.MaterialExpressionDotProduct, -800, 1700)
    link(vn, "", facing, "A"); link(sun_dir, "", facing, "B")
    lit_raw = node(unreal.MaterialExpressionMultiply, -650, 1700, const_b=1.5)
    link(facing, "", lit_raw, "A")
    lit_add = node(unreal.MaterialExpressionAdd, -500, 1700, const_b=0.35)
    link(lit_raw, "", lit_add, "A")
    lit_sat = node(unreal.MaterialExpressionSaturate, -380, 1700)
    link(lit_add, "", lit_sat, "")
    lit = node(unreal.MaterialExpressionLinearInterpolate, -250, 1700, const_b=1.0)
    link(scalar("RimShade", RIM_SHADE, -500, 1780), "", lit, "A"); link(lit_sat, "", lit, "Alpha")
    tint = node(unreal.MaterialExpressionLinearInterpolate, -350, 1900, const_b=1.0, const_alpha=0.5)
    link(base, "RGB", tint, "A")
    sun = node(unreal.MaterialExpressionSkyAtmosphereLightIlluminance, -800, 2000)
    r1 = node(unreal.MaterialExpressionMultiply, -150, 1500); link(fr, "", r1, "A"); link(lit, "", r1, "B")
    r2 = node(unreal.MaterialExpressionMultiply, -50, 1700); link(r1, "", r2, "A"); link(tint, "", r2, "B")
    r3 = node(unreal.MaterialExpressionMultiply, 50, 1900); link(r2, "", r3, "A"); link(sun, "", r3, "B")
    rim = node(unreal.MaterialExpressionMultiply, 150, 2000); link(r3, "", rim, "A"); link(scalar("RimStrength", RIM_STRENGTH, -100, 2100), "", rim, "B")
    emis = node(unreal.MaterialExpressionAdd, 250, 1200)
    link(mul, "", emis, "A"); link(rim, "", emis, "B")
    mel.connect_material_property(emis, "", MP.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    lib.save_asset(path)
    return m

first = NAMES[0]
no_glow = pic("T_NoGlow", True)          # black: the default GlowTex, and the glow of any building without lit windows
parent = make_material(pic("T_%s_Base" % first, True), pic("T_%s_MR" % first, False), pic("T_%s_Normal" % first, False, True), no_glow)
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in NAMES:
    mi_path = folder + "/MI_" + n
    if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_%s_Base" % n, True))
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", pic("T_%s_MR" % n, False))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", pic("T_%s_Normal" % n, False, True))
    mel.set_material_instance_texture_parameter_value(mi, "GlowTex", pic("T_%s_Glow" % n, True) or no_glow)
    if n in GLOW_BY_NAME: mel.set_material_instance_scalar_parameter_value(mi, "GlowStrength", GLOW_BY_NAME[n])
    lib.save_asset(mi_path)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    d = ui.get_editor_property("static_mesh_import_data")
    d.set_editor_property("combine_meshes", True)
    d.set_editor_property("auto_generate_collision", False)
    d.set_editor_property("generate_lightmap_u_vs", False)
    d.set_editor_property("convert_scene", True)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.IGNORE)
    d.set_editor_property("build_nanite", n in NANITE)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, n + ".fbx")
    task.destination_path = folder; task.destination_name = "SM_" + n
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    sm = lib.load_asset(folder + "/SM_" + n)
    if sm is None: raise RuntimeError("BUILDDIAG " + n + " did not import")
    sm.set_material(0, mi)
    lib.save_asset(folder + "/SM_" + n)
    b = sm.get_bounding_box()
    unreal.log("BUILDDIAG %s nanite %s min %s max %s" % (n, sm.get_editor_property("nanite_settings").enabled, b.min, b.max))
unreal.log("BUILDDIAG done")
