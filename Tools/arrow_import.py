# Imports the distinct arrows (Tools/arrow_heads.py output in Saved/ArrowWork/out) as /Game/Props/SM_Arrow_<Kind>, on the plain arrow's material (MI_Arrow:
# the same pictures, the head and feathers tinted by the vertex mask). Run by Tools/BuildArrows.ps1 in a windowless editor.
import os
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "ArrowWork", "out")
mi = lib.load_asset("/Game/Props/MI_Arrow")
if mi is None: raise RuntimeError("ARROWDIAG no MI_Arrow")
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for k in ("Cheap", "Bodkin", "Heavy", "Fire", "Eagle"):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True); ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False); ui.set_editor_property("import_textures", False); ui.set_editor_property("import_animations", False)
    d = ui.get_editor_property("static_mesh_import_data")
    d.set_editor_property("combine_meshes", True); d.set_editor_property("auto_generate_collision", False); d.set_editor_property("generate_lightmap_u_vs", False)
    d.set_editor_property("convert_scene", True)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    t = unreal.AssetImportTask()
    t.filename = os.path.join(work, "Arrow_%s.fbx" % k); t.destination_path = "/Game/Props"; t.destination_name = "SM_Arrow_" + k
    t.replace_existing = True; t.automated = True; t.save = True; t.options = ui
    tools.import_asset_tasks([t])
    sm = lib.load_asset("/Game/Props/SM_Arrow_" + k)
    if sm is None: raise RuntimeError("ARROWDIAG %s did not import" % k)
    for i in range(len(sm.get_editor_property("static_materials"))): sm.set_material(i, mi)
    lib.save_asset("/Game/Props/SM_Arrow_" + k)
    b = sm.get_bounding_box()
    unreal.log("ARROWDIAG %s min %s max %s" % (k, b.min, b.max))
unreal.log("ARROWDIAG done")
