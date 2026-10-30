# Brings the Meshy knight's pictures into /Game/Knight and builds M_Knight, the material the knight's model pieces use.
# Run by Tools\BuildKnight.ps1 inside a windowless editor (the editor must be closed).
#   T_Knight_Base: the painted colour (steel plate, dark cloth, brown belt)
#   T_Knight_MR:   glTF metal/roughness (green = roughness, blue = metal)
#   T_Knight_N:    the normal map (the crisp bevels and panel lines); kept in its own OpenGL form, the mesh's tangents allow for it
# M_Knight tints only the metal (where blue is high) by its Tint parameter, so each armour tier recolours the plate while
# the cloth and the belt keep their own colours. Roughness never drops below MinRough: a near-mirror finish reflected the
# grass and turned the plate green.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
art = os.path.join(proj, "Art", "Knight")


def import_texture(path, name, srgb, normal=False):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game/Knight"
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset("/Game/Knight/" + name)
    if tex is None:
        raise RuntimeError("KNIGHTDIAG the picture " + name + " did not import")
    tex.set_editor_property("srgb", srgb)
    # always at full size: the knight is close to the camera and his picture is spread thin over the whole body
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", False)
    elif not srgb:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_asset("/Game/Knight/" + name)
    return tex


base = import_texture(os.path.join(art, "T_Knight_Base.jpg"), "T_Knight_Base", True)
mr = import_texture(os.path.join(art, "T_Knight_MR.jpg"), "T_Knight_MR", False)
nm = import_texture(os.path.join(art, "T_Knight_N.jpg"), "T_Knight_N", False, normal=True)

if unreal.EditorAssetLibrary.does_asset_exist("/Game/Knight/M_Knight"):
    unreal.EditorAssetLibrary.delete_asset("/Game/Knight/M_Knight")
mat = tools.create_asset("M_Knight", "/Game/Knight", unreal.Material, unreal.MaterialFactoryNew())


def node(cls, x, y, **props):
    n = lib.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


tb = node(unreal.MaterialExpressionTextureSample, -900, 0, texture=base)
tm = node(unreal.MaterialExpressionTextureSample, -900, 300, texture=mr, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
tint = node(unreal.MaterialExpressionVectorParameter, -900, -250, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
one = node(unreal.MaterialExpressionConstant3Vector, -700, -350, constant=unreal.LinearColor(1, 1, 1, 1))
# how much of the tint each pixel takes: the metal mask, pushed up a little so worn plate edges tint too
metal_k = node(unreal.MaterialExpressionMultiply, -650, 350, const_b=1.4)
lib.connect_material_expressions(tm, "B", metal_k, "A")
metal = node(unreal.MaterialExpressionSaturate, -500, 350)
lib.connect_material_expressions(metal_k, "", metal, "")
mix = node(unreal.MaterialExpressionLinearInterpolate, -450, -250)
lib.connect_material_expressions(one, "", mix, "A")
lib.connect_material_expressions(tint, "", mix, "B")
lib.connect_material_expressions(metal, "", mix, "Alpha")
col = node(unreal.MaterialExpressionMultiply, -250, 0)
lib.connect_material_expressions(tb, "RGB", col, "A")
lib.connect_material_expressions(mix, "", col, "B")
lib.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

ms = node(unreal.MaterialExpressionScalarParameter, -500, 500, parameter_name="MetalScale", default_value=1.0)
mm = node(unreal.MaterialExpressionMultiply, -250, 400)
lib.connect_material_expressions(metal, "", mm, "A")
lib.connect_material_expressions(ms, "", mm, "B")
lib.connect_material_property(mm, "", unreal.MaterialProperty.MP_METALLIC)

mn = node(unreal.MaterialExpressionScalarParameter, -500, 650, parameter_name="MinRough", default_value=0.22)
rough = node(unreal.MaterialExpressionMax, -250, 600)
lib.connect_material_expressions(tm, "G", rough, "A")
lib.connect_material_expressions(mn, "", rough, "B")
lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

tn = node(unreal.MaterialExpressionTextureSample, -900, 800, texture=nm, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
lib.connect_material_property(tn, "RGB", unreal.MaterialProperty.MP_NORMAL)

lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset("/Game/Knight/M_Knight")
unreal.log("KNIGHTDIAG knight material built")
