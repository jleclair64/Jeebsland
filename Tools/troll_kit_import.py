# The troll's kit into /Game/TrollKit (2026-11-22): SK_TrollArmour on his own skeleton (/Game/People/SK_Troll_Skeleton), one material instance of
# M_KnightSkin per piece and part (<Piece>_Metal takes the tier's colour in the game, <Piece>_Soft is leather and fur), and the static SM_RockSack and
# SM_TrollRock on M_Prop. Run by Tools\ImportTrollKit.ps1 in a windowless editor, from Saved/TrollKit/Out (Tools/troll_armour_fit.py, troll_rocks_export.py).
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "TrollKit", "Out")
folder = "/Game/TrollKit"
PIECES = ["Chest", "Legs", "Helmet", "Arms", "Shoulders"]
if lib.does_directory_exist(folder): lib.delete_directory(folder)

def pic(name, srgb, group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): return None
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    if t is None: raise RuntimeError("TKDIAG the picture " + name + " did not import")
    t.set_editor_property("srgb", srgb)
    t.set_editor_property("lod_group", group)
    if name.endswith("_Normal"): t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(folder + "/" + name)
    return t

skin = lib.load_asset("/Game/Knight/M_KnightSkin")
flat = lib.load_asset("/Engine/EngineMaterials/FlatNormal")
mats = {}
for p in PIECES:
    base, mr, nrm = pic("T_TK_%s_Base" % p, True), pic("T_TK_%s_MR" % p, False), pic("T_TK_%s_Normal" % p, False)
    for part in ("Metal", "Soft"):
        n = "MI_TK_%s_%s" % (p, part)
        mi = tools.create_asset(n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, skin)
        mel.set_material_instance_texture_parameter_value(mi, "BaseTex", base)
        if mr: mel.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
        mel.set_material_instance_texture_parameter_value(mi, "NormalTex", nrm if nrm else flat)
        lib.save_asset(folder + "/" + n)
        mats["%s_%s" % (p, part)] = mi

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("skeleton", lib.load_asset("/Game/People/SK_Troll_Skeleton"))
ui.set_editor_property("create_physics_asset", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
sk.set_editor_property("use_t0_as_ref_pose", False)
task = unreal.AssetImportTask()
task.filename = os.path.join(work, "SK_TrollArmour.fbx")
task.destination_path = folder; task.destination_name = "SK_TrollArmour"
task.replace_existing = True; task.automated = True; task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_TrollArmour")
if skm is None: raise RuntimeError("TKDIAG SK_TrollArmour did not import")
slots = []
for m in skm.get_editor_property("materials"):
    nm = str(m.get_editor_property("material_slot_name"))
    hit = [k for k in mats if nm.startswith(k)]
    if not hit: raise RuntimeError("TKDIAG slot with no piece: " + nm)
    m.set_editor_property("material_interface", mats[hit[0]])
    slots.append(m)
skm.set_editor_property("materials", slots)
skm.modify()
lib.save_loaded_asset(skm, False)
unreal.log("TKDIAG armour slots: %s" % [str(m.get_editor_property("material_slot_name")) for m in slots])
unreal.log("TKDIAG armour bounds extent %s" % skm.get_bounds().box_extent)

prop = lib.load_asset("/Game/Props/M_Prop")
for n in ("RockSack", "TrollRock"):
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, prop)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_%s_Base" % n, True, unreal.TextureGroup.TEXTUREGROUP_WORLD))
    mr = pic("T_%s_MR" % n, False, unreal.TextureGroup.TEXTUREGROUP_WORLD)
    if mr: mel.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    nm = pic("T_%s_Normal" % n, False, unreal.TextureGroup.TEXTUREGROUP_WORLD)
    if nm: mel.set_material_instance_texture_parameter_value(mi, "NormalTex", nm)
    lib.save_asset(folder + "/MI_" + n)
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
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    t = unreal.AssetImportTask()
    t.filename = os.path.join(work, n + ".fbx"); t.destination_path = folder; t.destination_name = "SM_" + n
    t.replace_existing = True; t.automated = True; t.save = True; t.options = ui
    tools.import_asset_tasks([t])
    sm = lib.load_asset(folder + "/SM_" + n)
    if sm is None: raise RuntimeError("TKDIAG SM_" + n + " did not import")
    for i in range(len(sm.get_editor_property("static_materials"))): sm.set_material(i, mi)
    lib.save_asset(folder + "/SM_" + n)
    b = sm.get_bounding_box()
    unreal.log("TKDIAG SM_%s min %s max %s" % (n, b.min, b.max))
lib.save_directory(folder)
unreal.log("TKDIAG files: %s" % [p.split("/")[-1] for p in lib.list_assets(folder, recursive=False)])
unreal.log("TKDIAG done")
