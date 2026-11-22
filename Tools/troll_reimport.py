# Re-imports only the troll's body (/Game/People/SK_Troll) over the existing asset, keeping its skeleton, MI_Troll and LODs, from
# Saved/PeopleBuildTroll/SK_Troll.fbx (people_export.py for Troll; its skin weights are fixed there, 2026-11-22). Unlike
# people_import.py it leaves every other townsperson alone. Run in a windowless editor:
#   UnrealEditor-Cmd.exe Jeebsland.uproject -ExecutePythonScript=<this> -unattended -nosplash
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(proj, "Saved", "PeopleBuildTroll", "SK_Troll.fbx")
FOLDER = "/Game/People"
def log(s): unreal.log("TROLLREIMPORT " + s)

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
old = lib.load_asset(FOLDER + "/SK_Troll")
skel = lib.load_asset(FOLDER + "/SK_Troll_Skeleton")
mi = lib.load_asset(FOLDER + "/MI_Troll")
if old is None or skel is None or mi is None: raise RuntimeError("TROLLREIMPORT missing SK_Troll, its skeleton or MI_Troll")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("create_physics_asset", False)
ui.set_editor_property("skeleton", skel)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("import_morph_targets", False)
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
t = unreal.AssetImportTask()
t.filename = SRC; t.destination_path = FOLDER; t.destination_name = "SK_Troll"
t.replace_existing = True; t.automated = True; t.save = True; t.options = ui
tools.import_asset_tasks([t])
skm = lib.load_asset(FOLDER + "/SK_Troll")
slots = []
for s in skm.get_editor_property("materials"):
    s.set_editor_property("material_interface", mi)
    slots.append(s)
skm.set_editor_property("materials", slots)
try:
    ok = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).regenerate_lod(skm, 4, False, False)
    log("LODs regenerated: %s" % ok)
except Exception as e:
    log("LOD regeneration failed: %s" % e)
skm.modify()
lib.save_loaded_asset(skm, False)
log("skeleton kept %s" % (skm.get_editor_property("skeleton") == skel))
log("done")
