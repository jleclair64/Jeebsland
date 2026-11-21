# Brings the walk-in house into the game (2026-11-21; run by Tools\BuildInterior.ps1 in a windowless editor):
#  - SM_TL_HouseOpen and SM_TL_HouseOpen_Col (Saved/KnightBuild/Interior, from Tools/interior_house_open.py) into /Game/TownLab/Buildings,
#    imported as townlab_import.py imports SM_TL_House (normals and the glass marks in the vertex colour kept, Nanite) on its own
#    material MI_TL_House; the _Col with its own triangles as its collision, never drawn
#  - the reduced Meshy community props SM_Int_*.glb (Tools/interior_props.py) into /Game/Interior/<name> (each brings its material
#    and pictures)
# Prints the bounds of every mesh AHouseInterior's list uses (INTBOUNDS), to place them by.
import os, unreal
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Interior")
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
ONLY = os.environ.get("INTERIOR_IMPORT", "all")   # all / house / props / none

def fbx(f, dest, name, colours, nanite):
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
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE if colours else unreal.VertexColorImportOption.IGNORE)
    d.set_editor_property("build_nanite", nanite)
    if lib.does_asset_exist(dest + "/" + name): lib.delete_asset(dest + "/" + name)
    t = unreal.AssetImportTask()
    t.filename = f; t.destination_path = dest; t.destination_name = name
    t.replace_existing = True; t.automated = True; t.save = True; t.options = ui
    tools.import_asset_tasks([t])
    return lib.load_asset(dest + "/" + name)

if ONLY in ("all", "house"):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    folder = "/Game/TownLab/Buildings"
    hmat = lib.load_asset(folder + "/MI_TL_House")
    m = fbx(os.path.join(work, "SM_TL_HouseOpen.fbx"), folder, "SM_TL_HouseOpen", True, True)
    if not m: raise RuntimeError("INTIMPORT SM_TL_HouseOpen did not import")
    m.set_material(0, hmat)
    lib.save_asset(folder + "/SM_TL_HouseOpen")
    print("INTIMPORT SM_TL_HouseOpen", m.get_bounding_box().min, m.get_bounding_box().max, "nanite", m.get_editor_property("nanite_settings").enabled)
    c = fbx(os.path.join(work, "SM_TL_HouseOpen_Col.fbx"), folder, "SM_TL_HouseOpen_Col", False, False)
    if not c: raise RuntimeError("INTIMPORT SM_TL_HouseOpen_Col did not import")
    bs = c.get_editor_property("body_setup")
    bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    c.set_editor_property("body_setup", bs)
    lib.save_asset(folder + "/SM_TL_HouseOpen_Col")
    print("INTIMPORT SM_TL_HouseOpen_Col", c.get_bounding_box().min, c.get_bounding_box().max)

if ONLY in ("all", "props"):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX True")
    for fn in sorted(os.listdir(work)):
        if not fn.startswith("SM_Int_") or not fn.endswith(".glb"): continue
        name = fn[:-4]
        folder = "/Game/Interior/" + name[7:]
        if lib.does_directory_exist(folder): lib.delete_directory(folder)
        t = unreal.AssetImportTask()
        t.filename = os.path.join(work, fn); t.destination_path = folder; t.destination_name = name
        t.replace_existing = True; t.automated = True; t.save = True
        tools.import_asset_tasks([t])
        found = [p for p in lib.list_assets(folder, recursive=True) if str(lib.find_asset_data(p).asset_class_path.asset_name) == "StaticMesh"]
        if not found: print("INTIMPORT FAILED", name); continue
        m = lib.load_asset(found[0])
        if found[0].split(".")[0] != folder + "/" + name: lib.rename_asset(found[0].split(".")[0], folder + "/" + name); m = lib.load_asset(folder + "/" + name)
        lib.save_asset(m.get_path_name())
        print("INTIMPORT", m.get_path_name())
    lib.save_directory("/Game/Interior")

# bounds of everything the room uses
import csv
with open(os.path.join(proj, "Content", "Data", "InteriorHouse.csv")) as f:
    seen = set()
    for row in csv.reader(f):
        if not row or not row[0].startswith("/Game/") or row[0] in seen: continue
        seen.add(row[0])
        m = lib.load_asset(row[0].split(".")[0])
        if not m: print("INTBOUNDS MISSING", row[0]); continue
        b = m.get_bounding_box()
        print("INTBOUNDS %s lo %.0f %.0f %.0f hi %.0f %.0f %.0f" % (row[0], b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z))
print("INTIMPORT done")
