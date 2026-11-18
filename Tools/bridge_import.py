# Brings the stone bridge from Saved/KnightBuild/Bridge (made by bridge_clean.py) into /Game/Bridge (2026-11-18): the pictures, M_Bridge (a plain
# painted-stone material, Nanite-ready), MI_Bridge and SM_Bridge (Nanite; no collision of its own: Bridge.cpp builds the walking surface).
# Run by Tools\BuildBridge.ps1 in a windowless editor (this copy's editor closed).
# (2026-11-18: run Tools/FixLookAssets.ps1 after this: it gives M_Bridge its warm Tint, set on MI_Bridge.)
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Bridge")
folder = "/Game/Bridge"
ROUGH_TARGET, ROUGH_EVEN = 0.8, 0.35   # as M_Building: roughness = lerp(MR.G, RoughTarget, RoughEven)
if not lib.does_directory_exist(folder): lib.make_directory(folder)

def pic(name, kind):
    f = os.path.join(work, name + ".png")
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    t.set_editor_property("srgb", kind == "Base")
    if kind == "Normal":
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property("flip_green_channel", True)   # (glTF's normal maps point green the other way from Unreal's)
    elif kind == "MR": t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    t.set_editor_property("max_texture_size", 2048)
    lib.save_asset(folder + "/" + name)
    return t

base, mr, nrm = pic("T_Bridge_Base", "Base"), pic("T_Bridge_MR", "MR"), pic("T_Bridge_Normal", "Normal")

path = folder + "/M_Bridge"
if lib.does_asset_exist(path): lib.delete_asset(path)
m = tools.create_asset("M_Bridge", folder, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("used_with_nanite", True)
E = mel.create_material_expression
def tex(name, y, kind, default):
    t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
    t.set_editor_property("parameter_name", name); t.set_editor_property("sampler_type", kind); t.set_editor_property("texture", default)
    return t
tb = tex("BaseTex", -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, base)
tm = tex("MRTex", 200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, mr)
tn = tex("NormalTex", 600, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, nrm)
MP = unreal.MaterialProperty
mel.connect_material_property(tb, "RGB", MP.MP_BASE_COLOR)
mel.connect_material_property(tm, "B", MP.MP_METALLIC)
mel.connect_material_property(tn, "RGB", MP.MP_NORMAL)
def scalar(name, value, x, y):
    s = E(m, unreal.MaterialExpressionScalarParameter, x, y); s.set_editor_property("parameter_name", name); s.set_editor_property("default_value", value); return s
lerp = E(m, unreal.MaterialExpressionLinearInterpolate, -350, 250)
mel.connect_material_expressions(tm, "G", lerp, "A")
mel.connect_material_expressions(scalar("RoughTarget", ROUGH_TARGET, -700, 380), "", lerp, "B")
mel.connect_material_expressions(scalar("RoughEven", ROUGH_EVEN, -700, 440), "", lerp, "Alpha")
mel.connect_material_property(lerp, "", MP.MP_ROUGHNESS)
mel.recompile_material(m)
lib.save_asset(path)

mi_path = folder + "/MI_Bridge"
if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
mi = tools.create_asset("MI_Bridge", folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(mi, m)
lib.save_asset(mi_path)

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
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
d.set_editor_property("build_nanite", True)
if lib.does_asset_exist(folder + "/SM_Bridge"): lib.delete_asset(folder + "/SM_Bridge")
task = unreal.AssetImportTask()
task.filename = os.path.join(work, "Bridge.fbx")
task.destination_path = folder; task.destination_name = "SM_Bridge"
task.replace_existing = True; task.automated = True; task.save = True
task.options = ui
tools.import_asset_tasks([task])
sm = lib.load_asset(folder + "/SM_Bridge")
if sm is None: raise RuntimeError("BRIDGEDIAG SM_Bridge did not import")
sm.set_material(0, mi)
lib.save_asset(folder + "/SM_Bridge")
b = sm.get_bounding_box()
unreal.log("BRIDGEDIAG nanite %s min %s max %s" % (sm.get_editor_property("nanite_settings").enabled, b.min, b.max))
unreal.log("BRIDGEDIAG done")
