# Re-imports only the pictures Tools/townlab_panes.py rewrites (the colour, glow and window-mask pictures), over the existing texture
# assets, with the settings the full importers give them; the meshes and materials are left alone (2026-11-15).
# Run in a windowless editor (the editor closed on this project), PANES_ONLY = comma-separated names:
#   TL_<Name> -> /Game/TownLab/Buildings from Saved/KnightBuild/TownLab; any other name -> /Game/Buildings from Saved/KnightBuild/Buildings.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
names = [n for n in os.environ.get("PANES_ONLY", "").split(",") if n]
if not names: raise RuntimeError("BUILDDIAG PANES_ONLY is empty")
for n in names:
    tl = n.startswith("TL_")
    work = os.path.join(proj, "Saved", "KnightBuild", "TownLab" if tl else "Buildings")
    if os.environ.get("PANES_BEFORE"): work = os.path.join(work, "PrePanes")   # (the pictures as baked, for before/after pictures)
    folder = "/Game/TownLab/Buildings" if tl else "/Game/Buildings"
    for kind, srgb in (("Base", True), ("Glow", True), ("Mask", False)):
        name = "T_%s_%s" % (n, kind)
        f = os.path.join(work, name + ".png")
        if not os.path.exists(f): continue
        if not lib.does_asset_exist(folder + "/" + name): raise RuntimeError("BUILDDIAG %s/%s does not exist: run the full importer" % (folder, name))
        task = unreal.AssetImportTask()
        task.filename = f; task.destination_path = folder; task.destination_name = name
        task.replace_existing = True; task.automated = True; task.save = True
        tools.import_asset_tasks([task])
        t = lib.load_asset(folder + "/" + name)
        t.set_editor_property("srgb", srgb)
        if not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        lib.save_asset(folder + "/" + name)
        unreal.log("BUILDDIAG reimported %s/%s" % (folder, name))
unreal.log("BUILDDIAG done")
