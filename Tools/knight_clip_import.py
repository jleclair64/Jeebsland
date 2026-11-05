# Brings the knight's retargeted clips into /Game/NewSuit/Clips (2026-11-05), on the suit's own skeleton. Run by
# Tools\ImportKnightClips.ps1 inside a windowless editor, after the suit itself (knight_suit_import.py wipes /Game/NewSuit, clips too).
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Clips")
folder = "/Game/NewSuit/Clips"
sk = lib.load_asset("/Game/NewSuit/SK_NewSuit")
if sk is None: raise RuntimeError("CLIPDIAG no SK_NewSuit: import the suit first")
skeleton = sk.get_editor_property("skeleton")
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
n = 0
for fn in sorted(os.listdir(work)):
    if not fn.lower().endswith(".fbx"): continue
    name = fn[:-4]
    path = folder + "/" + name
    if lib.does_asset_exist(path): lib.delete_asset(path)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("skeleton", skeleton)
    an = ui.get_editor_property("anim_sequence_import_data")
    an.set_editor_property("convert_scene", True)
    an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    an.set_editor_property("import_bone_tracks", True)
    an.set_editor_property("use_default_sample_rate", False)
    an.set_editor_property("custom_sample_rate", 30)
    an.set_editor_property("snap_to_closest_frame_boundary", True)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, fn)
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    found = [p for p in lib.list_assets(folder, recursive=False) if p.split(".")[0].split("/")[-1].startswith(name)]
    if not found: raise RuntimeError("CLIPDIAG did not import " + name)
    n += 1
    if n <= 3:
        a = lib.load_asset(found[0].split(".")[0])
        unreal.log("CLIPDIAG %s -> %s: %.3f s" % (name, found[0].split(".")[0], a.get_play_length()))
lib.save_directory(folder)
unreal.log("CLIPDIAG files: %d" % len(lib.list_assets(folder, recursive=False)))
unreal.log("CLIPDIAG done")
