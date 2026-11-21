# Brings the Dark Plate set into /Game/DarkPlate (2026-11-21): SK_DarkPlate on the suit's own skeleton (/Game/NewSuit/SK_NewSuit_Skeleton) and one material
# instance per piece, with Meshy's normal map. Run by Tools\ImportDarkPlate.ps1 inside a windowless editor, after Tools\ImportNewSuit.ps1 if that ran.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "DarkPlate")
folder = "/Game/DarkPlate"
PIECES = ["Chest", "Legs", "Helmet", "Gauntlets", "Shoulders"]
if lib.does_directory_exist(folder): lib.delete_directory(folder)

def pic(name, srgb):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, name + ".png")
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    if t is None: raise RuntimeError("SUITDIAG the picture " + name + " did not import")
    t.set_editor_property("srgb", srgb)
    t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    if name.endswith("_Normal"): t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(folder + "/" + name)
    return t

parent = lib.load_asset("/Game/Knight/M_KnightSkin")
flat = lib.load_asset("/Engine/EngineMaterials/FlatNormal")
mats = {}
for p in PIECES:
    path = folder + "/MI_DP_" + p
    mi = tools.create_asset("MI_DP_" + p, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_DP_%s_Base" % p, True))
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", pic("T_DP_%s_MR" % p, False))
    nrm = os.path.exists(os.path.join(work, "T_DP_%s_Normal.png" % p))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", pic("T_DP_%s_Normal" % p, False) if nrm else flat)
    lib.save_asset(path)
    mats[p] = mi
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("skeleton", lib.load_asset("/Game/NewSuit/SK_NewSuit_Skeleton"))
ui.set_editor_property("create_physics_asset", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("import_morph_targets", True)   # (the fists, FistR / FistL: knight_fist.py)
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
sk.set_editor_property("use_t0_as_ref_pose", False)
an = ui.get_editor_property("anim_sequence_import_data")
an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
an.set_editor_property("import_bone_tracks", True)
an.set_editor_property("use_default_sample_rate", False)
an.set_editor_property("custom_sample_rate", 30)   # (Mixamo's own rate: the clips run 19 and 31 frames at it)
an.set_editor_property("snap_to_closest_frame_boundary", True)
task = unreal.AssetImportTask()
task.filename = os.path.join(work, "SK_DarkPlate.fbx")
task.destination_path = folder
task.destination_name = "SK_DarkPlate"
task.replace_existing = True
task.automated = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_DarkPlate")
if skm is None: raise RuntimeError("SUITDIAG SK_DarkPlate did not import")
def morphs(m):
    """The mesh's morph target names (the fists, knight_fist.py)."""
    try: return [str(n) for n in m.get_all_morph_target_names()]
    except Exception as e: return ["?" + str(e)]
unreal.log("SUITDIAG morph targets: %s" % morphs(skm))
# each slot's material, last: it must be what is saved
skm = lib.load_asset(folder + "/SK_DarkPlate")
new_slots = []
for m in skm.get_editor_property("materials"):
    nm = str(m.get_editor_property("material_slot_name"))
    hit = [p for p in mats if p in nm]
    if not hit: raise RuntimeError("SUITDIAG slot with no piece: " + nm)
    m.set_editor_property("material_interface", mats[hit[0]])
    new_slots.append(m)
skm.set_editor_property("materials", new_slots)
skm.modify()
lib.save_loaded_asset(skm, False)
lib.save_directory(folder)
check = lib.load_asset(folder + "/SK_DarkPlate").get_editor_property("materials")
unreal.log("SUITDIAG slots: %s" % [(str(m.get_editor_property("material_slot_name")), m.get_editor_property("material_interface").get_name() if m.get_editor_property("material_interface") else None) for m in check])
b = skm.get_bounds()
unreal.log("SUITDIAG bounds extent %s" % b.box_extent)
unreal.log("SUITDIAG files now: %s" % [p.split("/")[-1] for p in lib.list_assets(folder, recursive=False)])
unreal.log("SUITDIAG done")
