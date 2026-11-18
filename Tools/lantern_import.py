# Puts the repainted lanterns (Tools/lantern_glass.py, 2026-11-17) on the shipped copies in /Game/TownDecor, without the whole
# Filler -> TownDecor pipeline (Tools/CopyTownDecor.ps1 re-copies every kind): the base and normal pictures are replaced, and the mesh is
# reimported from Tools/filler_export.py's FBX (its glass faces moved into the Glow slot), keeping the two TownDecor material instances,
# no Nanite and the same generated LODs as town_decor_copy.py (1.0 / 0.35 / about 300 triangles, from screen sizes 1.0 / 0.2 / 0.05).
# Run by Tools/LanternImport.ps1 in a windowless editor (this copy's editor closed). Env LANTERN_WORK: the folder with <Name>.fbx and the PNGs.
import os
import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
WORK = os.environ["LANTERN_WORK"]
NAMES = os.environ.get("LANTERN_NAMES", "LanternOil LanternBlack").split()
DST = "/Game/TownDecor"
LOD_PERCENT_1, LOD_SCREEN, LOD_LAST_TRIS = 0.35, [1.0, 0.2, 0.05], 300.0
TEX_PROPS = ("srgb", "compression_settings", "max_texture_size", "flip_green_channel", "lod_group", "mip_gen_settings")


def import_task(src, name, options=None):
    t = unreal.AssetImportTask()
    t.filename = src; t.destination_path = DST; t.destination_name = name
    t.replace_existing = True; t.automated = True; t.save = False
    if options is not None: t.options = options
    tools.import_asset_tasks([t])


for n in NAMES:
    for kind in ("Base", "Normal"):
        png = os.path.join(WORK, "T_%s_%s.png" % (n, kind))
        path = "%s/T_%s_%s" % (DST, n, kind)
        old = EAL.load_asset(path)
        if old is None or not os.path.exists(png):
            raise RuntimeError("LANTERNIMP FAIL missing %s or %s" % (path, png))
        keep = {p: old.get_editor_property(p) for p in TEX_PROPS}
        import_task(png, "T_%s_%s" % (n, kind))
        t = EAL.load_asset(path)
        for p, v in keep.items(): t.set_editor_property(p, v)
        EAL.save_asset(path)
        unreal.log("LANTERNIMP %s picture replaced (%s)" % (path, ", ".join("%s=%s" % kv for kv in keep.items())))

    mpath = "%s/SM_%s" % (DST, n)
    old = EAL.load_asset(mpath)
    if old is None: raise RuntimeError("LANTERNIMP FAIL missing " + mpath)
    ob = old.get_bounding_box()
    body_mi = EAL.load_asset("%s/MI_%s" % (DST, n)); glow_mi = EAL.load_asset("%s/MI_%s_Glow" % (DST, n))
    if body_mi is None or glow_mi is None: raise RuntimeError("LANTERNIMP FAIL missing material instances for " + n)
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
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.IGNORE)
    import_task(os.path.join(WORK, n + ".fbx"), "SM_" + n, ui)
    sm = EAL.load_asset(mpath)
    mats = sm.get_editor_property("static_materials")
    names = []
    for i, s in enumerate(mats):
        sname = str(s.get_editor_property("material_slot_name")); names.append(sname)
        s.set_editor_property("material_interface", glow_mi if sname.startswith("Glow") else body_mi)
        mats[i] = s
    sm.set_editor_property("static_materials", mats)
    ns = sm.get_editor_property("nanite_settings"); ns.set_editor_property("enabled", False); sm.set_editor_property("nanite_settings", ns)
    tris = sm.get_num_triangles(0)
    unreal.JeebMeshTools.set_generated_lods(sm, [1.0, LOD_PERCENT_1, max(0.004, min(LOD_PERCENT_1, LOD_LAST_TRIS / max(tris, 1)))], LOD_SCREEN)
    EAL.save_asset(mpath)
    nb = sm.get_bounding_box()
    unreal.log("LANTERNIMP %s slots %s tris %d lods %d last %d bounds before %s %s after %s %s" % (
        mpath, names, tris, sm.get_num_lods(), unreal.JeebMeshTools.lod_triangles(sm, sm.get_num_lods() - 1), ob.min, ob.max, nb.min, nb.max))
unreal.log("LANTERNIMP done")
