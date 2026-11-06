# Brings the bare knight (knight_bare_export.py) into /Game/NewSuit on the suit's own skeleton. Run by Tools\ImportBareKnight.ps1 inside a windowless editor.
# knight_suit_import.py wipes /Game/NewSuit: run this again after any suit import.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
folder = "/Game/NewSuit"
skeleton = lib.load_asset(folder + "/SK_NewSuit").get_editor_property("skeleton")
if lib.does_asset_exist(folder + "/SK_NewSuitBare"): lib.delete_asset(folder + "/SK_NewSuitBare")

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("skeleton", skeleton)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("create_physics_asset", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("import_morph_targets", False)
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
task = unreal.AssetImportTask()
task.filename = os.path.join(proj, "Saved", "KnightBuild", "Suit", "SK_NewSuitBare.fbx")
task.destination_path = folder
task.destination_name = "SK_NewSuitBare"
task.replace_existing = True
task.automated = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_NewSuitBare")
if skm is None: raise RuntimeError("SUITDIAG SK_NewSuitBare did not import")
mail = lib.load_asset(folder + "/MI_Suit_Mail")
slots = []
for m in skm.get_editor_property("materials"):
    m.set_editor_property("material_interface", mail)
    slots.append(m)
skm.set_editor_property("materials", slots)
skm.modify()
lib.save_loaded_asset(skm, False)
unreal.log("SUITDIAG bare slots: %s" % [str(m.get_editor_property("material_slot_name")) for m in skm.get_editor_property("materials")])
unreal.log("SUITDIAG done")
