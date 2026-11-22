import unreal
MEL = unreal.MaterialEditingLibrary
for p in ("/Game/TownDecor/M_TownDecor", "/Game/TownDecor/M_TownDecorTwoSided", "/Game/M_StumpBark"):
    m = unreal.EditorAssetLibrary.load_asset(p)
    unreal.log("GREYDIAG %s two_sided=%s" % (p, m.get_editor_property("two_sided")))
    for n in MEL.get_texture_parameter_names(m):
        t = MEL.get_material_default_texture_parameter_value(m, n)
        unreal.log("GREYDIAG   tex %s default=%s" % (n, t.get_path_name() if t else None))
    for e in m.get_editor_property("expression_collection").get_editor_property("expressions") if hasattr(m, "get_editor_property") and False else []:
        pass
for n in ("AwningStriped", "RaisedBed", "TulipBed", "WellOld", "Barrel"):
    mi = unreal.EditorAssetLibrary.load_asset("/Game/TownDecor/MI_" + n)
    unreal.log("GREYDIAG MI_%s parent=%s" % (n, mi.get_editor_property("parent").get_name()))
    for tp in mi.get_editor_property("texture_parameter_values"):
        v = tp.get_editor_property("parameter_value")
        unreal.log("GREYDIAG    %s = %s" % (tp.get_editor_property("parameter_info").get_editor_property("name"), v.get_name() if v else None))
unreal.log("GREYDIAG done")
