# Log bark, second try: M_LogBark's moss-keeping graph rendered flat grey (default material), so use the proven M_StumpBark parent.
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
# what did the first MI keep?
old = EAL.load_asset("/Game/TownDecor/MI_LogMossyBark")
if old:
    unreal.log("LOGDIAG2 MI_LogMossyBark parent=%s" % old.get_editor_property("parent").get_name())
    for tp in old.get_editor_property("texture_parameter_values"):
        v = tp.get_editor_property("parameter_value")
        unreal.log("LOGDIAG2    %s = %s" % (tp.get_editor_property("parameter_info").get_editor_property("name"), v.get_name() if v else None))
src = EAL.load_asset("/Game/TownDecor/MI_LogMossy")
vals = {str(tp.get_editor_property("parameter_info").get_editor_property("name")): tp.get_editor_property("parameter_value") for tp in src.get_editor_property("texture_parameter_values")}
parent = EAL.load_asset("/Game/M_StumpBark")
mpath = "/Game/TownDecor/MI_LogMossyBark"
if EAL.does_asset_exist(mpath):
    EAL.delete_asset(mpath)
mi = tools.create_asset("MI_LogMossyBark", "/Game/TownDecor", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
MEL.set_material_instance_parent(mi, parent)
for s, d in (("BaseTex", "Base"), ("NormalTex", "Normal"), ("MRTex", "MR")):
    ok = MEL.set_material_instance_texture_parameter_value(mi, d, vals[s])
    unreal.log("LOGDIAG2 set %s ok=%s" % (d, ok))
MEL.set_material_instance_scalar_parameter_value(mi, "Recolour", 0.7)
MEL.update_material_instance(mi)
EAL.save_asset(mpath)
sm = EAL.load_asset("/Game/TownDecor/SM_LogMossy")
for i, sl in enumerate(sm.static_materials):
    sm.set_material(i, mi)
EAL.save_asset("/Game/TownDecor/SM_LogMossy")
unreal.log("LOGDIAG2 done")
