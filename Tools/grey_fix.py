# Grey props fix (2026-11-22): M_TownDecorTwoSided had no default pictures (the one-sided parent has the barrel's), so the parent
# failed to compile ("Param2D Found NULL") and every two-sided prop (awning, raised bed, tulip bed ...) drew the flat grey default material.
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
m = EAL.load_asset("/Game/TownDecor/M_TownDecorTwoSided")
defaults = {"BaseTex": "/Game/TownDecor/T_AwningStriped_Base", "MRTex": "/Game/TownDecor/T_AwningStriped_MR", "NormalTex": "/Game/TownDecor/T_AwningStriped_Normal"}
exprs = MEL.get_material_expressions(m) if hasattr(MEL, "get_material_expressions") else m.get_editor_property("expressions")
n = 0
for e in exprs:
    if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
        pn = str(e.get_editor_property("parameter_name"))
        if pn in defaults:
            e.set_editor_property("texture", EAL.load_asset(defaults[pn])); n += 1
unreal.log("GREYFIX set %d default pictures on M_TownDecorTwoSided" % n)
MEL.recompile_material(m)
EAL.save_asset("/Game/TownDecor/M_TownDecorTwoSided")
for p in ("/Game/TownDecor/M_TownDecorTwoSided",):
    mm = EAL.load_asset(p)
    for nme in MEL.get_texture_parameter_names(mm):
        t = MEL.get_material_default_texture_parameter_value(mm, nme)
        unreal.log("GREYFIX %s %s default=%s" % (p, nme, t.get_name() if t else None))
unreal.log("GREYFIX done")
