# Brings the Meshy knight's pictures into /Game/Knight, builds M_Knight (the material the knight's model pieces use) and
# M_KnightSkin, and imports the skinned knight SK_Knight (2026-11-01) from Saved\KnightBuild\SK_Knight.fbx.
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


def import_texture(path, name, srgb, normal=False, flip_green=False):
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
        tex.set_editor_property("flip_green_channel", flip_green)
    elif not srgb:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_asset("/Game/Knight/" + name)
    return tex


base = import_texture(os.path.join(art, "T_Knight_Base.jpg"), "T_Knight_Base", True)
mr = import_texture(os.path.join(art, "T_Knight_MR.jpg"), "T_Knight_MR", False)
nm = import_texture(os.path.join(art, "T_Knight_N.jpg"), "T_Knight_N", False, normal=True)


# The knight material, built twice: M_Knight for the cut pieces (KnightParts.bin, drawn from both sides by the game) and
# M_KnightSkin for the skinned knight SK_Knight (two-sided, allowed on skinned meshes, and reading the DirectX copy of the normal
# map: the cut pieces flip their tangents instead).
def build_material(name, normal_tex, skin):
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/Knight/" + name):
        unreal.EditorAssetLibrary.delete_asset("/Game/Knight/" + name)
    mat = tools.create_asset(name, "/Game/Knight", unreal.Material, unreal.MaterialFactoryNew())
    if skin:
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("used_with_skeletal_mesh", True)
    
    
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
    # The inside of the shell: each piece is a thin skin drawn from both sides, so through an opening (the top of a greave, the
    # gap at a bent knee) you would see its inner face lit like polished plate and read it as a hole. Wherever the surface's
    # outward normal faces away from the camera we are looking at that inner face: paint it the dark of the shadowed inside.
    vn = node(unreal.MaterialExpressionVertexNormalWS, -900, -600)
    cv = node(unreal.MaterialExpressionCameraVectorWS, -900, -500)
    dp = node(unreal.MaterialExpressionDotProduct, -700, -550)
    lib.connect_material_expressions(vn, "", dp, "A")
    lib.connect_material_expressions(cv, "", dp, "B")
    outside_k = node(unreal.MaterialExpressionMultiply, -550, -550, const_b=6.0)
    lib.connect_material_expressions(dp, "", outside_k, "A")
    outside_b = node(unreal.MaterialExpressionAdd, -450, -550, const_b=0.5)
    lib.connect_material_expressions(outside_k, "", outside_b, "A")
    outside = node(unreal.MaterialExpressionSaturate, -350, -550)
    lib.connect_material_expressions(outside_b, "", outside, "")
    inner = node(unreal.MaterialExpressionConstant3Vector, -350, -450, constant=unreal.LinearColor(0.03, 0.03, 0.035, 1))
    shown = node(unreal.MaterialExpressionLinearInterpolate, -150, -100)
    lib.connect_material_expressions(inner, "", shown, "A")
    lib.connect_material_expressions(col, "", shown, "B")
    lib.connect_material_expressions(outside, "", shown, "Alpha")
    lib.connect_material_property(shown, "", unreal.MaterialProperty.MP_BASE_COLOR)
    
    ms = node(unreal.MaterialExpressionScalarParameter, -500, 500, parameter_name="MetalScale", default_value=1.0)
    mm = node(unreal.MaterialExpressionMultiply, -250, 400)
    lib.connect_material_expressions(metal, "", mm, "A")
    lib.connect_material_expressions(ms, "", mm, "B")
    mm2 = node(unreal.MaterialExpressionMultiply, -150, 400)   # (no shine on the inside)
    lib.connect_material_expressions(mm, "", mm2, "A")
    lib.connect_material_expressions(outside, "", mm2, "B")
    lib.connect_material_property(mm2, "", unreal.MaterialProperty.MP_METALLIC)
    
    mn = node(unreal.MaterialExpressionScalarParameter, -500, 650, parameter_name="MinRough", default_value=0.22)
    rough = node(unreal.MaterialExpressionMax, -250, 600)
    lib.connect_material_expressions(tm, "G", rough, "A")
    lib.connect_material_expressions(mn, "", rough, "B")
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    
    tn = node(unreal.MaterialExpressionTextureSample, -900, 800, texture=normal_tex, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    lib.connect_material_property(tn, "RGB", unreal.MaterialProperty.MP_NORMAL)
    
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/Knight/" + name)
    unreal.log("KNIGHTDIAG knight material built " + name)
    return mat


build_material("M_Knight", nm, False)
nm_dx = import_texture(os.path.join(art, "T_Knight_N.jpg"), "T_Knight_N_DX", False, normal=True, flip_green=True)
skin_mat = build_material("M_KnightSkin", nm_dx, True)

# ---- The skinned knight (2026-11-01): Saved\KnightBuild\SK_Knight.fbx from knight_blender_export.py becomes the skeletal mesh
# /Game/Knight/SK_Knight (with its skeleton SK_Knight_Skeleton). Its five material slots (Helm, Body, Shoulders, Boots, Gloves)
# all get M_KnightSkin; the game tints each slot for its armour tier. The legacy FBX importer is used: it honours the options.
fbx = os.path.join(proj, "Saved", "KnightBuild", "SK_Knight.fbx")
if os.path.exists(fbx):
    for old in ("/Game/Knight/SK_Knight", "/Game/Knight/SK_Knight_Skeleton", "/Game/Knight/SK_Knight_PhysicsAsset"):   # (a fresh import: replacing keeps the old options)
        if unreal.EditorAssetLibrary.does_asset_exist(old): unreal.EditorAssetLibrary.delete_asset(old)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)
    sk = ui.get_editor_property("skeletal_mesh_import_data")
    sk.set_editor_property("import_morph_targets", False)
    sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    sk.set_editor_property("convert_scene", True)
    sk.set_editor_property("use_t0_as_ref_pose", True)
    sk.set_editor_property("import_uniform_scale", 100.0)   # (the file comes in metres)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = "/Game/Knight"
    task.destination_name = "SK_Knight"
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    skm = unreal.EditorAssetLibrary.load_asset("/Game/Knight/SK_Knight")
    if skm is None:
        raise RuntimeError("KNIGHTDIAG SK_Knight did not import")
    mats = skm.get_editor_property("materials")
    for m in mats:
        m.set_editor_property("material_interface", skin_mat)
    skm.set_editor_property("materials", mats)
    unreal.EditorAssetLibrary.save_directory("/Game/Knight")   # (the mesh AND the skeleton the import made with it)
    skel = skm.get_editor_property("skeleton")
    unreal.log("KNIGHTDIAG skeleton %s" % (skel.get_path_name() if skel else None))
    b = skm.get_bounds()
    unreal.log("KNIGHTDIAG SK_Knight imported: %d material slots %s, bounds origin %s extent %s" % (len(mats), [str(m.get_editor_property("material_slot_name")) for m in mats], b.origin, b.box_extent))
