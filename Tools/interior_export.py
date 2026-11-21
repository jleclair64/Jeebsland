# Writes SM_TL_House and its solid copy SM_TL_House_Col out as FBX (Saved/KnightBuild/Interior) for Tools/interior_house_open.py,
# and lists the size of every Stylized Medieval Tavern mesh (Baturinets, /Game/Medieval_Tavern) for the room. Run by BuildInterior.ps1.
import os, unreal
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(proj, "Saved", "KnightBuild", "Interior"); os.makedirs(out, exist_ok=True)
lib = unreal.EditorAssetLibrary
for n in ("SM_TL_House", "SM_TL_House_Col"):
    m = lib.load_asset("/Game/TownLab/Buildings/" + n)
    t = unreal.AssetExportTask(); t.object = m; t.filename = os.path.join(out, n + ".fbx"); t.automated = True; t.replace_identical = True; t.prompt = False
    t.exporter = unreal.StaticMeshExporterFBX()
    o = unreal.FbxExportOption(); o.collision = False; o.level_of_detail = False; t.options = o
    print("INTEXPORT", n, unreal.Exporter.run_asset_export_task(t), [s.material_interface.get_path_name() if s.material_interface else None for s in m.static_materials])
for p in sorted(lib.list_assets("/Game/Medieval_Tavern", recursive=True)):
    a = lib.find_asset_data(p)
    if str(a.asset_class_path.asset_name) != "StaticMesh": continue
    m = a.get_asset(); b = m.get_bounding_box()
    print("INTTAVERN %s lo %s hi %s tris %d mats %s" % (p.split(".")[0], b.min, b.max, m.get_num_triangles(0) if hasattr(m,'get_num_triangles') else -1, [s.material_interface.get_name() if s.material_interface else None for s in m.static_materials]))
print("INTEXPORT done")
