# Brings the filler props from Saved/KnightBuild/Filler into /Game/Filler (2026-11-13): one static mesh per piece, a material instance per slot on the shared M_Filler
# (M_FillerTwoSided for cloth and leaf cards). Run by Tools\BuildFillerLineup.ps1 in a windowless editor. A slot named Glow (lantern glass, flames) is lit:
# emissive = its own picture x GlowAmount; a piece whose name ends in Flame is all glow. /Game/Filler is never cooked (DefaultGame.ini): the MoveLab lineup skips it in a package.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Filler")
folder = "/Game/Filler"
NAMES = sorted(n[:-4] for n in os.listdir(work) if n.endswith(".fbx"))
if not lib.does_directory_exist(folder): lib.make_directory(folder)
GLOW = 5.0

def pic(name, srgb, normal=False):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): return None
    path = folder + "/" + name
    if lib.does_asset_exist(path): return lib.load_asset(path)     # (a finished picture is kept: rebuild everything by deleting /Game/Filler)
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(path)
    t.set_editor_property("srgb", srgb)
    if normal:
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property("flip_green_channel", True)   # (glTF's normal maps point green the other way from Unreal's)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(path)
    return t

def make_material(mname, two_sided, default_base, default_mr, default_normal):
    path = folder + "/" + mname
    if lib.does_asset_exist(path): lib.delete_asset(path)
    m = tools.create_asset(mname, folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", two_sided)
    E = mel.create_material_expression
    def tex(name, y, kind, default):
        t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
        t.set_editor_property("parameter_name", name); t.set_editor_property("sampler_type", kind); t.set_editor_property("texture", default)
        return t
    base = tex("BaseTex", -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_base)
    mr = tex("MRTex", 200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, default_mr)
    nrm = tex("NormalTex", 600, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, default_normal)
    MP = unreal.MaterialProperty
    mel.connect_material_property(base, "RGB", MP.MP_BASE_COLOR)
    mel.connect_material_property(mr, "G", MP.MP_ROUGHNESS)
    mel.connect_material_property(mr, "B", MP.MP_METALLIC)
    mel.connect_material_property(nrm, "RGB", MP.MP_NORMAL)
    g = E(m, unreal.MaterialExpressionScalarParameter, -700, 900)
    g.set_editor_property("parameter_name", "GlowAmount"); g.set_editor_property("default_value", 0.0)
    mul = E(m, unreal.MaterialExpressionMultiply, -350, 800)
    mel.connect_material_expressions(base, "RGB", mul, "A"); mel.connect_material_expressions(g, "", mul, "B")
    mel.connect_material_property(mul, "", MP.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    lib.save_asset(path)
    return m

first = NAMES[0]
args = (pic("T_%s_Base" % first, True), pic("T_%s_MR" % first, False), pic("T_%s_Normal" % first, False, True))
make_material("M_Filler", False, *args)
make_material("M_FillerTwoSided", True, *args)
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")

def make_mi(n, suffix, parent_name, base, mr, nm, glow):
    p = folder + "/MI_" + n + suffix
    if lib.does_asset_exist(p): lib.delete_asset(p)
    mi = tools.create_asset("MI_" + n + suffix, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load_asset(folder + "/" + parent_name))
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", base)
    if mr: mel.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    if nm: mel.set_material_instance_texture_parameter_value(mi, "NormalTex", nm)
    if glow: mel.set_material_instance_scalar_parameter_value(mi, "GlowAmount", GLOW)
    lib.save_asset(p)
    return mi

for n in NAMES:
    base = pic("T_%s_Base" % n, True); mr = pic("T_%s_MR" % n, False); nm = pic("T_%s_Normal" % n, False, True)
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
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, n + ".fbx")
    task.destination_path = folder; task.destination_name = "SM_" + n
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    sm = lib.load_asset(folder + "/SM_" + n)
    if sm is None: raise RuntimeError("FILLDIAG " + n + " did not import")
    slots = [str(s.get_editor_property("material_slot_name")) for s in sm.get_editor_property("static_materials")]
    all_glow = n.endswith("Flame")
    for i, sname in enumerate(slots):
        if sname == "Glow" or all_glow:
            mi = make_mi(n, "_Glow", "M_Filler", base, mr, nm, True)
        elif sname == "Cloth":
            mi = make_mi(n, "_Cloth", "M_FillerTwoSided", base, mr, nm, False)
        else:
            mi = make_mi(n, "", "M_FillerTwoSided" if os.path.exists(os.path.join(work, n + ".two")) else "M_Filler", base, mr, nm, False)
        sm.set_material(i, mi)
    lib.save_asset(folder + "/SM_" + n)
    b = sm.get_bounding_box()
    unreal.log("FILLDIAG %s slots %s min %s max %s" % (n, slots, b.min, b.max))
unreal.log("FILLDIAG done")
