# TownLab buildings (2026-11-14): Tools/townlab_bake.py's low models from Saved/KnightBuild/TownLab into /Game/TownLab/Buildings,
# on their own M_TL_Building (M_Building's look, but the window marks come from a picture, T_<Name>_Mask, instead of the vertex colour,
# and MR's red channel is the baked ambient occlusion). Collision: building_clean.py's <Old>_Col.fbx of the same design, as SM_<Name>_Col.
# Run by ToolsBuildTownLabBuildings.ps1 in a windowless editor (the editor closed on this project).
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "TownLab")
col_work = os.path.join(proj, "Saved", "KnightBuild", "Buildings")
folder = "/Game/TownLab/Buildings"   # (never /Game/Buildings: that folder belongs to the town pipeline)
NANITE = {"TL_House", "TL_Store"}
NAMES = sorted(n[:-4] for n in os.listdir(work) if n.endswith(".fbx"))
COL_FROM = {"TL_House": "OakHillHouse_300k", "TL_Store": "OakHillStore_300k"}
# build-b (townbuild2, 2026-11-14): the town hall, church, well and potion shop
NANITE |= {"TL_TownHall", "TL_Church", "TL_Well", "TL_PotionShop"}
COL_FROM.update({"TL_TownHall": "OakHillTownHall_300k", "TL_Church": "OakHillChurch_300k", "TL_Well": "OakHillWell_150k", "TL_PotionShop": "OakHillPotionShop_300k"})
# build-a (2026-11-14): the bank, bank stall, smithy and corner house
NANITE |= {"TL_Bank", "TL_BankStall", "TL_Smith", "TL_CornerHouse"}
COL_FROM.update({"TL_Bank": "OakHillBank_300k", "TL_BankStall": "BankStall_300k", "TL_Smith": "OakHillSmith_300k", "TL_CornerHouse": "OakHillCornerHouse_300k"})
# build-c (2026-11-14): the market stall and the potion stall
NANITE |= {"TL_MarketStall", "TL_PotionStall"}
COL_FROM.update({"TL_MarketStall": "GeneralStore_300k", "TL_PotionStall": "PotionStall_300k"})
# TL_ONLY (BuildTownLabBuildings.ps1 -Only): import just those buildings onto the existing M_TL_Building, leaving it, the other
# buildings and the shared decor untouched (several agents rebuild their own designs at once)
ONLY = [n for n in os.environ.get("TL_ONLY", "").split(",") if n]
if ONLY:
    NAMES = [n for n in NAMES if n in ONLY]
    if sorted(NAMES) != sorted(ONLY): raise RuntimeError("BUILDDIAG TL_ONLY asks for %s but Saved/KnightBuild/TownLab has %s" % (ONLY, NAMES))
TEST_HOUSES = set()
for f in ("/Game/TownLab", folder):
    if not lib.does_directory_exist(f): lib.make_directory(f)

def pic(name, srgb, normal=False):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): f = os.path.join(col_work, name + ".png")
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

GLOW_STRENGTH = 1.0   # lit windows (T_<Name>_Glow): emissive = GlowTex x GlowStrength x (lit or not, below). Was 2.0 until 2026-11-13
                      #   (owner: "reduce the window brightness by another 50%")
FIRE_LEVEL = 0.0      # (no forge on the TownLab buildings: 0, so no part of the mask can burn all day; M_Building has 2.0 for the smithy. Townlab review 2026-11-14: windows lit at noon)
ON_FROM = 0.2         # a window lights when Night (ADayNight::Dusk) passes OnFrom + OnSpread x HouseOffset + WinSpread x its own random:
ON_SPREAD = 0.5       #   off in broad day, the houses one by one from Dusk 0.2, the last at about 0.9 (and off again the same way)
WIN_SPREAD = 0.2      #   (a house's windows a little apart)
ON_SHARP = 12.0       # how quickly one window comes on (over 1/12 of the Dusk range)
DARK_SHARE = 0.3      # of each building's windows, this share never lights (owner: "remove the yellow lighting from some of the windows")
GLASS_COLOR = (0.035, 0.04, 0.05)   # an unlit pane's colour (Meshy painted the panes yellow in the colour picture too)
GLASS_ROUGH = 0.25
GLASS_MASK_GAIN = 2.0 # glass where the glow picture is over half bright
# per-building overrides (owner, 2026-11-12: "the windows in all the homes 35% dimmer": the two house designs only)
GLOW_BY_NAME = {"TL_House": GLOW_STRENGTH * 0.65}
GLOW_BY_NAME["TL_CornerHouse"] = GLOW_STRENGTH * 0.65   # (build-a: a home, as the house)
FIRE_BY_NAME = {"TL_Smith": 2.0}   # (build-a: the forge burns all day, as M_Building's FireLevel 2.0 for the smithy)

# Lighting does the shading (owner, 2026-11-12, with Tools/building_flatten.py taking the baked light out of the colour): steadier,
# slightly rougher surfaces, and a faint warm rim of sunlight along the edges that makes the shapes pop. All material parameters.
ROUGH_TARGET = 0.8    # roughness = lerp(MRTex.G, RoughTarget, RoughEven): Meshy's roughness changes island by island (0.55-0.75 on the
ROUGH_EVEN = 0.35     #   house), which reads as a blotchy sheen; a third of the way to one value (0.6 lost glossy warmth in recesses)
RIM_STRENGTH = 0.06   # rim (added to the emissive) = Fresnel(RimPower) on the mesh's own normal x the sun's light where it reaches the
RIM_POWER = 4.0       #   ground (SkyAtmosphereLightIlluminance: warm and dim at the low sun by itself) x lerp(RimShade, 1, how far the
RIM_SHADE = 0.25      #   face turns to the sun) x halfway between the surface's colour and white x RimStrength

def make_material(default_base, default_mr, default_normal, default_glow, default_mask):
    # (a picture slot with no default picture doesn't compile, and the game then silently draws it in plain grey: check the log for "Failed to compile Material")
    path = folder + "/M_TL_Building"
    if lib.does_asset_exist(path): lib.delete_asset(path)
    m = tools.create_asset("M_TL_Building", folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_nanite", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
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
    mel.connect_material_property(mr, "B", MP.MP_METALLIC)
    mel.connect_material_property(nrm, "RGB", MP.MP_NORMAL)
    mel.connect_material_property(mr, "R", MP.MP_AMBIENT_OCCLUSION)   # (the baked occlusion dims the sky light in creases)
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
    glow = tex("GlowTex", 1000, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_glow)
    gs = scalar("GlowStrength", GLOW_STRENGTH, -700, 1300)
    mul0 = node(unreal.MaterialExpressionMultiply, -350, 1100)
    link(glow, "RGB", mul0, "A")
    link(gs, "", mul0, "B")
    # Which windows are lit (2026-11-13, owner: "some windows" dark, the houses switching on one by one as it gets darker and off again
    # as it gets lighter). building_clean.py paints every window face's vertex colour: R a random number per window, G 1 on a window,
    # B 1 on faces that never glow (painted specks); the forge's fire is (0,0,0) and burns all day at FireLevel. Each building gets its own
    # random HouseOffset (custom primitive data 0, OakHillLife::SetUpGlow) and "Night" is ADayNight::Dusk (OakHillLife::UpdateGlow).
    # A window lights when Night passes OnFrom + OnSpread x HouseOffset + WinSpread x (its own random); DarkShare of each building's
    # windows never light. UseMask 0 (MoveLab's test houses, no window colours) keeps the old all-lit glow.
    vc = tex("MaskTex", 900, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, default_mask)   # (the window marks as a picture)
    house = node(unreal.MaterialExpressionScalarParameter, -1500, 1000, parameter_name="HouseOffset", default_value=0.5,
                 use_custom_primitive_data=True, primitive_data_index=0)
    def mad(a, a_out, k, b, c):   # a x k + b x c, as nodes
        m1 = node(unreal.MaterialExpressionMultiply, -1300, 900, const_b=k); link(a, a_out, m1, "A")
        m2 = node(unreal.MaterialExpressionMultiply, -1300, 1000, const_b=c); link(b, "", m2, "A")
        ad = node(unreal.MaterialExpressionAdd, -1200, 950); link(m1, "", ad, "A"); link(m2, "", ad, "B")
        return ad
    win_rand = node(unreal.MaterialExpressionFrac, -1100, 900); link(mad(vc, "R", 3.3, house, 5.9), "", win_rand, "")
    dark_rand = node(unreal.MaterialExpressionFrac, -1100, 1000); link(mad(vc, "R", 7.13, house, 3.71), "", dark_rand, "")
    t1 = node(unreal.MaterialExpressionMultiply, -1000, 850); link(scalar("OnSpread", ON_SPREAD, -1200, 800), "", t1, "A"); link(house, "", t1, "B")
    t2 = node(unreal.MaterialExpressionMultiply, -1000, 900); link(scalar("WinSpread", WIN_SPREAD, -1200, 850), "", t2, "A"); link(win_rand, "", t2, "B")
    t3 = node(unreal.MaterialExpressionAdd, -900, 850); link(t1, "", t3, "A"); link(t2, "", t3, "B")
    thresh = node(unreal.MaterialExpressionAdd, -800, 850); link(t3, "", thresh, "A"); link(scalar("OnFrom", ON_FROM, -1000, 780), "", thresh, "B")
    late = node(unreal.MaterialExpressionSubtract, -700, 850); link(scalar("Night", 0.0, -900, 760), "", late, "A"); link(thresh, "", late, "B")
    sharp = node(unreal.MaterialExpressionMultiply, -600, 850); link(late, "", sharp, "A"); link(scalar("OnSharp", ON_SHARP, -800, 760), "", sharp, "B")
    on_win = node(unreal.MaterialExpressionSaturate, -500, 850); link(sharp, "", on_win, "")
    keep = node(unreal.MaterialExpressionSubtract, -1000, 1050); link(scalar("KeepLit", 1.0 - DARK_SHARE, -1200, 1100), "", keep, "A"); link(dark_rand, "", keep, "B")
    keep_k = node(unreal.MaterialExpressionMultiply, -900, 1050, const_b=1000.0); link(keep, "", keep_k, "A")
    keep_s = node(unreal.MaterialExpressionSaturate, -800, 1050); link(keep_k, "", keep_s, "")
    win = node(unreal.MaterialExpressionMultiply, -400, 900); link(on_win, "", win, "A"); link(keep_s, "", win, "B")
    pick = node(unreal.MaterialExpressionLinearInterpolate, -300, 900); link(scalar("FireLevel", FIRE_LEVEL, -500, 960), "", pick, "A"); link(win, "", pick, "B"); link(vc, "G", pick, "Alpha")
    never = node(unreal.MaterialExpressionOneMinus, -300, 1000); link(vc, "B", never, "")
    on_m = node(unreal.MaterialExpressionMultiply, -200, 950); link(pick, "", on_m, "A"); link(never, "", on_m, "B")
    use = scalar("UseMask", 1.0, -300, 1050)
    on = node(unreal.MaterialExpressionLinearInterpolate, -100, 950, const_a=1.0); link(on_m, "", on, "B"); link(use, "", on, "Alpha")
    mul = node(unreal.MaterialExpressionMultiply, -50, 1100); link(mul0, "", mul, "A"); link(on, "", mul, "B")
    # The glass: Meshy painted the lit panes yellow in the colour picture too, so by day an unlit window still read as lit. Where the
    # glow picture marks a window, the colour turns to dark glass, a little glossier (a lit window's emissive goes on top of it).
    gmax1 = node(unreal.MaterialExpressionMax, -400, 1200); link(glow, "R", gmax1, "A"); link(glow, "G", gmax1, "B")
    gmax = node(unreal.MaterialExpressionMax, -350, 1250); link(gmax1, "", gmax, "A"); link(glow, "B", gmax, "B")
    gm2 = node(unreal.MaterialExpressionMultiply, -300, 1250, const_b=GLASS_MASK_GAIN); link(gmax, "", gm2, "A")
    gm3 = node(unreal.MaterialExpressionSaturate, -250, 1250); link(gm2, "", gm3, "")
    gm4 = node(unreal.MaterialExpressionMultiply, -200, 1250); link(gm3, "", gm4, "A"); link(vc, "G", gm4, "B")
    gm5 = node(unreal.MaterialExpressionMultiply, -150, 1250); link(gm4, "", gm5, "A"); link(never, "", gm5, "B")
    glass = node(unreal.MaterialExpressionMultiply, -100, 1250); link(gm5, "", glass, "A"); link(use, "", glass, "B")
    gcol = node(unreal.MaterialExpressionVectorParameter, -400, 1350, parameter_name="GlassColor", default_value=unreal.LinearColor(*GLASS_COLOR, 1.0))
    bc = node(unreal.MaterialExpressionLinearInterpolate, 0, -200); link(base, "RGB", bc, "A"); link(gcol, "", bc, "B"); link(glass, "", bc, "Alpha")
    mel.connect_material_property(bc, "", MP.MP_BASE_COLOR)
    rough_g = node(unreal.MaterialExpressionLinearInterpolate, 0, 250, const_b=GLASS_ROUGH)
    link(rough, "", rough_g, "A"); link(glass, "", rough_g, "Alpha")
    mel.connect_material_property(rough_g, "", MP.MP_ROUGHNESS)
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
if ONLY:
    no_glow = lib.load_asset(folder + "/T_NoGlow")
    parent = lib.load_asset(folder + "/M_TL_Building")
    if parent is None or no_glow is None: raise RuntimeError("BUILDDIAG TL_ONLY needs the existing M_TL_Building and T_NoGlow")
else:
    no_glow = pic("T_NoGlow", True)          # black: the default GlowTex, and the glow of any building without lit windows
    parent = make_material(pic("T_%s_Base" % first, True), pic("T_%s_MR" % first, False), pic("T_%s_Normal" % first, False, True), no_glow, pic("T_%s_Mask" % first, False))
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in NAMES:
    mi_path = folder + "/MI_" + n
    if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_%s_Base" % n, True))
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", pic("T_%s_MR" % n, False))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", pic("T_%s_Normal" % n, False, True))
    glow_t = pic("T_%s_Glow" % n, True)
    mel.set_material_instance_texture_parameter_value(mi, "GlowTex", glow_t or no_glow)
    # (no lit windows, as the open stalls: no glow picture, so nothing glows or turns to glass whatever the mask says; the black
    # T_NoGlow stands in for the mask too rather than a 2048 picture of nothing)
    mel.set_material_instance_texture_parameter_value(mi, "MaskTex", pic("T_%s_Mask" % n, False) if glow_t else no_glow)
    if n in GLOW_BY_NAME: mel.set_material_instance_scalar_parameter_value(mi, "GlowStrength", GLOW_BY_NAME[n])
    if n in FIRE_BY_NAME: mel.set_material_instance_scalar_parameter_value(mi, "FireLevel", FIRE_BY_NAME[n])
    if n in TEST_HOUSES: mel.set_material_instance_scalar_parameter_value(mi, "UseMask", 0.0)
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
    d.set_editor_property("vertex_color_import_option",   # (the window colours: see make_material)
                          unreal.VertexColorImportOption.IGNORE)
    d.set_editor_property("build_nanite", n in NANITE)
    # (a fresh asset every time: importing over an existing mesh keeps ITS old import settings, so the window colours above were
    # silently dropped from every building imported before they existed, 2026-11-13; nothing holds a reference, the game loads by name)
    if lib.does_asset_exist(folder + "/SM_" + n): lib.delete_asset(folder + "/SM_" + n)
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

# Collision (2026-11-13, owner: "still being cut off by an invisible wall outside these houses", "go up on the stairs", "go under the
# awning"): building_clean.py's <Name>_Col.fbx, a reduced copy of the model, becomes SM_<Name>_Col with its own triangles as its
# collision (complex as simple: steps, porches, the forge's open front, walls stopping at the stone). Never drawn, no Nanite;
# OakHillTown.cpp puts it, hidden, under each building on every machine (the server too).
for n in NAMES:
    f = os.path.join(col_work, COL_FROM.get(n, n) + "_Col.fbx")
    if not os.path.exists(f): continue
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
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.IGNORE)
    d.set_editor_property("build_nanite", False)
    task = unreal.AssetImportTask()
    if lib.does_asset_exist(folder + "/SM_" + n + "_Col"): lib.delete_asset(folder + "/SM_" + n + "_Col")   # (fresh, as above)
    task.filename = f
    task.destination_path = folder; task.destination_name = "SM_" + n + "_Col"
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    cm = lib.load_asset(folder + "/SM_" + n + "_Col")
    if cm is None: raise RuntimeError("BUILDDIAG " + n + "_Col did not import")
    bs = cm.get_editor_property("body_setup")
    bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    cm.set_editor_property("body_setup", bs)
    lib.save_asset(folder + "/SM_" + n + "_Col")
    b = cm.get_bounding_box()
    unreal.log("BUILDDIAG %s_Col min %s max %s" % (n, b.min, b.max))

# The decorations (Tools/townlab_decor.py): ivy and moss cards on one painted atlas (M_TL_Foliage: masked, two-sided, no Nanite),
# moss cushions and little stone piles in their vertex colour (M_TL_VertexPaint). Placed by Source/Jeebsland/TownLabDecor.h.
decor = os.path.join(work, "Decor")
if os.path.isdir(decor) and not ONLY:
    def plain_material(name, build):
        path = folder + "/" + name
        if lib.does_asset_exist(path): lib.delete_asset(path)
        m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        m.set_editor_property("used_with_instanced_static_meshes", True)
        build(m); mel.recompile_material(m); lib.save_asset(path)
        return m
    task = unreal.AssetImportTask()
    task.filename = os.path.join(decor, "T_TL_Foliage.png"); task.destination_path = folder; task.destination_name = "T_TL_Foliage"
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    fol_tex = lib.load_asset(folder + "/T_TL_Foliage"); fol_tex.set_editor_property("srgb", True); lib.save_asset(folder + "/T_TL_Foliage")
    def foliage(m):
        E = mel.create_material_expression
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("two_sided", True)
        m.set_editor_property("opacity_mask_clip_value", 0.4)
        t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
        t.set_editor_property("parameter_name", "FoliageTex"); t.set_editor_property("texture", fol_tex)
        mel.connect_material_property(t, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(t, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        r = E(m, unreal.MaterialExpressionConstant, -300, 200); r.set_editor_property("r", 0.75)
        mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    def vpaint(m):
        E = mel.create_material_expression
        m.set_editor_property("two_sided", True)
        # (the colour is a parameter per piece, MI_TL_Moss / MI_TL_Stones; the vertex colour only adds a little lump-to-lump variation)
        v = E(m, unreal.MaterialExpressionVertexColor, -700, 0)
        k = E(m, unreal.MaterialExpressionMultiply, -500, 0); k.set_editor_property("const_b", 1.2)
        mel.connect_material_expressions(v, "G", k, "A")
        a = E(m, unreal.MaterialExpressionAdd, -400, 0); a.set_editor_property("const_b", 0.8)
        mel.connect_material_expressions(k, "", a, "A")
        s = E(m, unreal.MaterialExpressionSaturate, -320, 0); mel.connect_material_expressions(a, "", s, "")
        c = E(m, unreal.MaterialExpressionVectorParameter, -500, -200); c.set_editor_property("parameter_name", "Color")
        c.set_editor_property("default_value", unreal.LinearColor(0.3, 0.4, 0.1, 1.0))
        mul = E(m, unreal.MaterialExpressionMultiply, -200, 0)
        mel.connect_material_expressions(c, "", mul, "A"); mel.connect_material_expressions(s, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        r = E(m, unreal.MaterialExpressionConstant, -300, 200); r.set_editor_property("r", 0.9)
        mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    m_fol = plain_material("M_TL_Foliage", foliage)
    m_vp = plain_material("M_TL_VertexPaint", vpaint)
    solid = {}
    for mi_name, col in (("MI_TL_Moss", (0.085, 0.14, 0.025)), ("MI_TL_Stones", (0.30, 0.27, 0.22))):   # (linear colours: moss yellow-green, warm grey stone)
        p = folder + "/" + mi_name
        if lib.does_asset_exist(p): lib.delete_asset(p)
        mi = tools.create_asset(mi_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, m_vp)
        mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(col[0], col[1], col[2], 1.0))
        lib.save_asset(p); solid[mi_name] = mi
    for f in sorted(os.listdir(decor)):
        if not f.endswith(".fbx"): continue
        n = f[:-4]; painted = n in ("TL_MossClump", "TL_Stones")
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", True); ui.set_editor_property("import_as_skeletal", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        ui.set_editor_property("import_materials", False); ui.set_editor_property("import_textures", False); ui.set_editor_property("import_animations", False)
        d = ui.get_editor_property("static_mesh_import_data")
        d.set_editor_property("combine_meshes", True); d.set_editor_property("auto_generate_collision", False)
        d.set_editor_property("generate_lightmap_u_vs", False); d.set_editor_property("convert_scene", True)
        d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE if painted else unreal.VertexColorImportOption.IGNORE)
        d.set_editor_property("build_nanite", False)
        if lib.does_asset_exist(folder + "/SM_" + n): lib.delete_asset(folder + "/SM_" + n)
        task = unreal.AssetImportTask()
        task.filename = os.path.join(decor, f); task.destination_path = folder; task.destination_name = "SM_" + n
        task.replace_existing = True; task.automated = True; task.save = True; task.options = ui
        tools.import_asset_tasks([task])
        sm = lib.load_asset(folder + "/SM_" + n)
        if sm is None: raise RuntimeError("BUILDDIAG " + n + " did not import")
        sm.set_material(0, (solid["MI_TL_Moss"] if n == "TL_MossClump" else solid["MI_TL_Stones"]) if painted else m_fol)
        unreal.log("BUILDDIAG decor %s vertex colours %s" % (n, unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).has_vertex_colors(sm)))
        lib.save_asset(folder + "/SM_" + n)
        unreal.log("BUILDDIAG decor %s" % n)
unreal.log("BUILDDIAG done")
