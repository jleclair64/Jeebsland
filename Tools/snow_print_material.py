# Builds /Game/M_SnowPrint: the soft footprint the knight leaves in deep snow (AKnightCharacter::SnowFeet, 2026-11-22).
# A deferred decal: a blue-grey shade in an oval, soft at its edge, a little rougher, its middle a touch darker (the boot's hollow).
# Run with the editor closed:
#   UnrealEditor-Cmd.exe Jeebsland.uproject -ExecutePythonScript=Tools/snow_print_material.py -unattended -nosplash -stdout
import unreal

NAME = "M_SnowPrint"
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
path = "/Game/" + NAME
if lib.does_asset_exist(path):
    lib.delete_asset(path)
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, "/Game", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
try:
    mat.set_editor_property("decal_blend_mode", unreal.DecalBlendMode.DBM_TRANSLUCENT)
except Exception:
    pass


def node(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


uv = node(unreal.MaterialExpressionTextureCoordinate, -900, 0)
# stretch the decal's square into a boot's oval: (u - 0.5, (v - 0.5) * 1.0) measured against 0.5
mid = node(unreal.MaterialExpressionConstant2Vector, -900, 150)
mid.set_editor_property("r", 0.5)
mid.set_editor_property("g", 0.5)
dist = node(unreal.MaterialExpressionDistance, -700, 50)
mel.connect_material_expressions(uv, "", dist, "A")
mel.connect_material_expressions(mid, "", dist, "B")
ss = node(unreal.MaterialExpressionSmoothStep, -500, 50)
ss.set_editor_property("const_min", 0.5)
ss.set_editor_property("const_max", 0.22)
mel.connect_material_expressions(dist, "", ss, "Value")
# opacity: up to 0.55 in the middle, nothing at the edge
k = node(unreal.MaterialExpressionConstant, -500, 200)
k.set_editor_property("r", 0.55)
op = node(unreal.MaterialExpressionMultiply, -300, 100)
mel.connect_material_expressions(ss, "", op, "A")
mel.connect_material_expressions(k, "", op, "B")
mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
# colour: the snow in its own shadow (cool blue-grey), a little darker toward the middle
outer = node(unreal.MaterialExpressionConstant3Vector, -500, -250)
outer.set_editor_property("constant", unreal.LinearColor(0.62, 0.68, 0.80, 1.0))
inner = node(unreal.MaterialExpressionConstant3Vector, -500, -120)
inner.set_editor_property("constant", unreal.LinearColor(0.42, 0.48, 0.62, 1.0))
col = node(unreal.MaterialExpressionLinearInterpolate, -300, -150)
mel.connect_material_expressions(outer, "", col, "A")
mel.connect_material_expressions(inner, "", col, "B")
mel.connect_material_expressions(ss, "", col, "Alpha")
mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough = node(unreal.MaterialExpressionConstant, -300, 300)
rough.set_editor_property("r", 0.95)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
mel.recompile_material(mat)
lib.save_asset(path)
unreal.log("SNOWPRINT built " + path)
