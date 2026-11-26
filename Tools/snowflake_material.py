# Builds /Game/M_Snowflake: a soft round snowflake for a camera-facing quad (AOpenWorld, 2026-11-26; the cubes read as cubes).
# Unlit, masked (so the flakes write motion vectors and TSR leaves no ghost trails), two-sided; a round disc whose rim is softened
# (TSR blends the one-pixel edge), white a touch cool and brighter in the middle. Run with the editor closed: Tools/BuildSnowflake.ps1
import unreal

NAME = "M_Snowflake"
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
path = "/Game/" + NAME
if lib.does_asset_exist(path):
    lib.delete_asset(path)
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, "/Game", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
for flag in ("used_with_instanced_static_meshes",):
    try:
        mat.set_editor_property(flag, True)
    except Exception as e:
        unreal.log("SNOWFLAKE flag " + flag + ": " + str(e))


def node(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


uv = node(unreal.MaterialExpressionTextureCoordinate, -900, 0)
mid = node(unreal.MaterialExpressionConstant2Vector, -900, 150)
mid.set_editor_property("r", 0.5)
mid.set_editor_property("g", 0.5)
dist = node(unreal.MaterialExpressionDistance, -700, 50)
mel.connect_material_expressions(uv, "", dist, "A")
mel.connect_material_expressions(mid, "", dist, "B")
# 1 in the middle, 0 at the rim of the quad's inscribed circle (distance 0.5); the mask keeps the part over 0.5
ss = node(unreal.MaterialExpressionSmoothStep, -500, 50)
ss.set_editor_property("const_min", 0.5)
ss.set_editor_property("const_max", 0.0)
mel.connect_material_expressions(dist, "", ss, "Value")
mel.connect_material_property(ss, "", unreal.MaterialProperty.MP_OPACITY_MASK)
mat.set_editor_property("opacity_mask_clip_value", 0.35)
# colour: soft white, cool, a little brighter in the middle (Color is a parameter)
c = node(unreal.MaterialExpressionVectorParameter, -500, -150)
c.set_editor_property("parameter_name", "Color")
c.set_editor_property("default_value", unreal.LinearColor(1.5, 1.52, 1.58, 1.0))
core = node(unreal.MaterialExpressionLinearInterpolate, -300, -100)
k0 = node(unreal.MaterialExpressionConstant, -500, 0)
k0.set_editor_property("r", 0.7)
k1 = node(unreal.MaterialExpressionConstant, -500, 100)
k1.set_editor_property("r", 1.0)
mel.connect_material_expressions(k0, "", core, "A")
mel.connect_material_expressions(k1, "", core, "B")
mel.connect_material_expressions(ss, "", core, "Alpha")
col = node(unreal.MaterialExpressionMultiply, -100, -100)
mel.connect_material_expressions(c, "", col, "A")
mel.connect_material_expressions(core, "", col, "B")
mel.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(mat)
lib.save_asset(path)
unreal.log("SNOWFLAKE built " + path)
