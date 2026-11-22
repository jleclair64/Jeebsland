# M_StumpBark had no default pictures either (same failure as M_TownDecorTwoSided): give it a stump's.
import unreal
MEL = unreal.MaterialEditingLibrary; EAL = unreal.EditorAssetLibrary
m = EAL.load_asset("/Game/M_StumpBark")
d = {"Base": "/Game/TownDecor/T_StumpGrass_Base", "Normal": "/Game/TownDecor/T_StumpGrass_Normal", "MR": "/Game/TownDecor/T_StumpGrass_MR"}
n = 0
for e in MEL.get_material_expressions(m):
    if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
        pn = str(e.get_editor_property("parameter_name"))
        if pn in d:
            e.set_editor_property("texture", EAL.load_asset(d[pn])); n += 1
MEL.recompile_material(m); EAL.save_asset("/Game/M_StumpBark")
unreal.log("STUMPFIX set %d defaults" % n)
for k in ("Stump", "StumpRooty", "StumpMushroom", "StumpGrass"):
    mi = EAL.load_asset("/Game/TownDecor/MI_%sBark" % k)
    if mi:
        MEL.update_material_instance(mi); EAL.save_asset("/Game/TownDecor/MI_%sBark" % k)
unreal.log("STUMPFIX done")
