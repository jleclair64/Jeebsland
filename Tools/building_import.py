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
NANITE = {"OakHillHouse_Detailed", "OakHillHouse_300k"}
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

def make_material(default_base, default_mr, default_normal):
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
    mel.connect_material_property(mr, "G", MP.MP_ROUGHNESS)
    mel.connect_material_property(nrm, "RGB", MP.MP_NORMAL)
    mel.recompile_material(m)
    lib.save_asset(path)
    return m

first = NAMES[0]
parent = make_material(pic("T_%s_Base" % first, True), pic("T_%s_MR" % first, False), pic("T_%s_Normal" % first, False, True))
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in NAMES:
    mi_path = folder + "/MI_" + n
    if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_%s_Base" % n, True))
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", pic("T_%s_MR" % n, False))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", pic("T_%s_Normal" % n, False, True))
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
