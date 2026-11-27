# Unreal (windowless editor): imports only A_Horse_Gallop (2026-11-26) from Saved/Gallop/A_Horse_Gallop.fbx onto the horse's skeleton,
# without rebuilding every animal (Tools/BuildDeer.ps1 does the whole folder and now includes the gallop too).
#   Blender: Tools/deer_export.py -- Horse Art/Animals/Horse/Horse_clips.glb (same) Saved/Gallop Gallop=horse_Sprint
#   UnrealEditor-Cmd Jeebsland.uproject -ExecutePythonScript=Tools/horse_gallop_import.py -unattended -nosplash
import os, unreal
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
f = os.path.join(proj, "Saved", "Gallop", "A_Horse_Gallop.fbx")
lib = unreal.EditorAssetLibrary
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
skm = lib.load_asset("/Game/Animals/SK_Horse")
skel = skm.get_editor_property("skeleton")
ua = unreal.FbxImportUI()
ua.set_editor_property("import_mesh", False)
ua.set_editor_property("import_as_skeletal", True)
ua.set_editor_property("import_materials", False)
ua.set_editor_property("import_textures", False)
ua.set_editor_property("import_animations", True)
ua.set_editor_property("automated_import_should_detect_type", False)
ua.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
ua.set_editor_property("skeleton", skel)
an = ua.get_editor_property("anim_sequence_import_data")
an.set_editor_property("convert_scene", True)
an.set_editor_property("convert_scene_unit", True)
an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
an.set_editor_property("import_bone_tracks", True)
an.set_editor_property("use_default_sample_rate", False)
an.set_editor_property("custom_sample_rate", 30)
an.set_editor_property("snap_to_closest_frame_boundary", True)
t = unreal.AssetImportTask()
t.filename = f; t.destination_path = "/Game/Animals"; t.destination_name = "A_Horse_Gallop"
t.replace_existing = True; t.automated = True; t.save = True; t.options = ua
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
seq = lib.load_asset("/Game/Animals/A_Horse_Gallop")
unreal.log("GALLOPDIAG %s" % ("imported %.2f s" % seq.get_play_length() if seq else "FAILED"))
