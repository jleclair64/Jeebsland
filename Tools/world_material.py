# Builds M_WorldGround: the material of the big world's land (AWorldTerrain). The terrain paints a colour into every corner
# of its mesh (green lowland, grey rock, snow, sand, river mud); this material just shows that colour, matte.
# Run by Tools\BuildWorldMaterial.ps1 inside a windowless editor (the editor must be closed).
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
if unreal.EditorAssetLibrary.does_asset_exist("/Game/M_WorldGround"):
    unreal.EditorAssetLibrary.delete_asset("/Game/M_WorldGround")
mat = tools.create_asset("M_WorldGround", "/Game", unreal.Material, unreal.MaterialFactoryNew())
vc = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -400, 0)
lib.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
rough.set_editor_property("r", 0.95)
lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
spec = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
spec.set_editor_property("r", 0.2)
lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset("/Game/M_WorldGround")
unreal.log("WORLDDIAG world ground material built")
