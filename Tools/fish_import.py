# Fish (2026-11-26, agent fish; from frog_import.py): Saved/KnightBuild/Fish (made by fish_clean.py) into /Game/Fish, one static mesh and material instance each (on the shared /Game/TownDecor/M_TownDecor), LODs by JeebMeshTools, no collision. Run by Tools/BuildFish.ps1 with the editor closed.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Fish")
folder = "/Game/Fish"
PARENT = "/Game/TownDecor/M_TownDecor"
NAMES = [n[:-4] for n in os.listdir(work) if n.endswith(".fbx")]
ONLY = [s for s in os.environ.get("JEEB_FISH", "").split(",") if s]
if ONLY: NAMES = [n for n in NAMES if n in ONLY]
LODS = {n: ([1.0, 0.4, 0.12], [1.0, 0.18, 0.05]) for n in ("Bass", "Trout", "Salmon", "Perch", "GoldenCarp")}
CAP = {"Base": 2048, "Normal": 1024, "MR": 1024}
if not lib.does_directory_exist(folder): lib.make_directory(folder)


def pic(name, kind):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): return None
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    t.set_editor_property("srgb", kind == "Base")
    if kind == "Normal": t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif kind == "MR": t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    t.set_editor_property("max_texture_size", CAP[kind])
    lib.save_asset(folder + "/" + name)
    return t


parent = lib.load_asset(PARENT)
if parent is None or not parent.get_editor_property("used_with_instanced_static_meshes"):
    raise RuntimeError("WPROPDIAG " + PARENT + " missing or not flagged for instanced meshes")
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in NAMES:
    mi_path = folder + "/MI_" + n
    if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for kind in ("Base", "MR", "Normal"):
        t = pic("T_%s_%s" % (n, kind), kind)
        if t is None: raise RuntimeError("WPROPDIAG %s has no %s picture" % (n, kind))
        mel.set_material_instance_texture_parameter_value(mi, kind + "Tex", t)
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
    d.set_editor_property("one_convex_hull_per_ucx", True)
    d.set_editor_property("generate_lightmap_u_vs", False)
    d.set_editor_property("convert_scene", True)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, n + ".fbx")
    task.destination_path = folder; task.destination_name = "SM_" + n
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    sm = lib.load_asset(folder + "/SM_" + n)
    if sm is None: raise RuntimeError("WPROPDIAG " + n + " did not import")
    slots = sm.get_editor_property("static_materials")
    for i_ in range(len(slots)): sm.set_material(i_, mi)
    ns = sm.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", False)
    sm.set_editor_property("nanite_settings", ns)
    pct, scr = LODS[n]
    unreal.JeebMeshTools.set_generated_lods(sm, pct, scr)
    bs = sm.get_editor_property("body_setup")
    bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
    lib.save_asset(folder + "/SM_" + n)
    hulls = len(bs.get_editor_property("agg_geom").get_editor_property("convex_elems"))
    b = sm.get_bounding_box()
    tris = [unreal.JeebMeshTools.lod_triangles(sm, i) for i in range(sm.get_num_lods())]
    unreal.log("WPROPDIAG %s min %s max %s hulls %d slots %d lod tris %s" % (n, b.min, b.max, hulls, len(slots), tris))
    pass
unreal.log("WPROPDIAG done")
