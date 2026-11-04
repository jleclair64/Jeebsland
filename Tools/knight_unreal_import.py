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


def import_texture(path, name, srgb, normal=False, flip_green=False, folder="/Game/Knight"):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset(folder + "/" + name)
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
    unreal.EditorAssetLibrary.save_asset(folder + "/" + name)
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
    
    
    # (2026-11-03) the skinned knight's pictures are parameters, so each armour style's material instance gives its own
    if skin:
        tb = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 0, texture=base, parameter_name="BaseTex")
        tm = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 300, texture=mr, parameter_name="MRTex", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    else:
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
    if skin:
        # Desat (0..1): the metal's own colour turned grey before the tint, for a model whose colour is baked into its picture
        # (the Classic style is painted gold: steel or silver tinted over gold came out yellow). Gain brightens the grey.
        ds = node(unreal.MaterialExpressionScalarParameter, -900, 150, parameter_name="Desat", default_value=0.0)
        dsk = node(unreal.MaterialExpressionMultiply, -650, 150)
        lib.connect_material_expressions(ds, "", dsk, "A")
        lib.connect_material_expressions(metal, "", dsk, "B")
        grey = node(unreal.MaterialExpressionDesaturation, -500, 50)
        lib.connect_material_expressions(tb, "RGB", grey, "")
        lib.connect_material_expressions(dsk, "", grey, "Fraction")
        gain = node(unreal.MaterialExpressionScalarParameter, -650, 220, parameter_name="DesatGain", default_value=1.0)
        gk = node(unreal.MaterialExpressionLinearInterpolate, -400, 100)   # (the gain only where the grey is used)
        one_s = node(unreal.MaterialExpressionConstant, -650, 280, r=1.0)
        lib.connect_material_expressions(one_s, "", gk, "A")
        lib.connect_material_expressions(gain, "", gk, "B")
        lib.connect_material_expressions(dsk, "", gk, "Alpha")
        bright = node(unreal.MaterialExpressionMultiply, -350, 0)
        lib.connect_material_expressions(grey, "", bright, "A")
        lib.connect_material_expressions(gk, "", bright, "B")
        lib.connect_material_expressions(bright, "", col, "A")
    else:
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
    
    if skin:
        tn = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, 800, texture=normal_tex, parameter_name="NormalTex", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    else:
        tn = node(unreal.MaterialExpressionTextureSample, -900, 800, texture=normal_tex, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    lib.connect_material_property(tn, "RGB", unreal.MaterialProperty.MP_NORMAL)
    
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/Knight/" + name)
    unreal.log("KNIGHTDIAG knight material built " + name)
    return mat


build_material("M_Knight", nm, False)
nm_dx = import_texture(os.path.join(art, "T_Knight_N.jpg"), "T_Knight_N_DX", False, normal=True, flip_green=True)
skin_mat = build_material("M_KnightSkin", nm_dx, True)


# ---- The armour styles (2026-11-03). Each style is its own Meshy model and its own skinned mesh, all on ONE skeleton
# (SK_Knight_Skeleton, made by the Heavy import; the others are imported onto it and never make their own):
#   Heavy   = /Game/Knight/SK_Knight          (the original model; the skeleton was built on it)
#   Classic = /Game/Knight/SK_Knight_Classic  (Art\Knight Gold Armor: its gold is baked into its picture, so Desat greys it)
#   Light   = /Game/Knight/SK_Knight_Light    (Art\Knight Tunic)
#   Mail    = /Game/Knight/SK_Knight_Mail     (Art\Knight Base Chainmail, worn under every style)
# Each gets MI_KnightSkin_<Style> in /Game/Knight/<Style>/ (an instance of M_KnightSkin with its own pictures) on every
# material slot (Helm, Body, Shoulders, Boots, Gloves); the game tints each slot for its tier at run time.
flat = unreal.EditorAssetLibrary.load_asset("/Engine/EngineMaterials/FlatNormal")   # (the new models have no normal map)
STYLES = [
    ("Heavy",   "SK_Knight",         None,                          None,             0.0),
    ("Classic", "SK_Knight_Classic", os.path.join(proj, "Art", "Knight Gold Armor"),     "T_Classic", 1.0),
    ("Light",   "SK_Knight_Light",   os.path.join(proj, "Art", "Knight Tunic"),          "T_Light",   0.0),
    ("Mail",    "SK_Knight_Mail",    os.path.join(proj, "Art", "Knight Base Chainmail"), "T_Mail",    0.0),
]
mel = unreal.MaterialEditingLibrary
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
skel = None
for style, mesh_name, pic_dir, pic, desat in STYLES:
    fbx = os.path.join(proj, "Saved", "KnightBuild", mesh_name + ".fbx")
    if not os.path.exists(fbx):
        unreal.log_warning("KNIGHTDIAG %s: no %s, skipped" % (style, fbx))
        continue
    folder = "/Game/Knight/" + style
    if pic_dir:
        tb = import_texture(os.path.join(pic_dir, pic + "_Base.png"), pic + "_Base", True, folder=folder)
        tm = import_texture(os.path.join(pic_dir, pic + "_MR.png"), pic + "_MR", False, folder=folder)
        tn = flat
    else:
        tb, tm, tn = base, mr, nm_dx
    mi_path = folder + "/MI_KnightSkin_" + style
    if unreal.EditorAssetLibrary.does_asset_exist(mi_path): unreal.EditorAssetLibrary.delete_asset(mi_path)
    mi = tools.create_asset("MI_KnightSkin_" + style, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, skin_mat)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", tb)
    mel.set_material_instance_texture_parameter_value(mi, "MRTex", tm)
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", tn)
    mel.set_material_instance_scalar_parameter_value(mi, "Desat", desat)
    unreal.EditorAssetLibrary.save_asset(mi_path)

    # A fresh import (replacing keeps the old options). Only Heavy may remake the skeleton; the others delete only their mesh.
    olds = ["/Game/Knight/" + mesh_name, "/Game/Knight/" + mesh_name + "_PhysicsAsset"]
    if style == "Heavy": olds.append("/Game/Knight/SK_Knight_Skeleton")
    for old in olds:
        if unreal.EditorAssetLibrary.does_asset_exist(old): unreal.EditorAssetLibrary.delete_asset(old)
    if style != "Heavy" and skel is None:
        skel = unreal.EditorAssetLibrary.load_asset("/Game/Knight/SK_Knight_Skeleton")
        if skel is None: raise RuntimeError("KNIGHTDIAG no SK_Knight_Skeleton: build Heavy first")
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)
    if style != "Heavy": ui.set_editor_property("skeleton", skel)
    sk = ui.get_editor_property("skeletal_mesh_import_data")
    sk.set_editor_property("import_morph_targets", False)
    sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    sk.set_editor_property("convert_scene", True)
    sk.set_editor_property("use_t0_as_ref_pose", True)
    sk.set_editor_property("import_uniform_scale", 100.0)   # (the file comes in metres)
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = "/Game/Knight"
    task.destination_name = mesh_name
    task.replace_existing = True
    task.automated = True
    task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    skm = unreal.EditorAssetLibrary.load_asset("/Game/Knight/" + mesh_name)
    if skm is None:
        raise RuntimeError("KNIGHTDIAG %s did not import" % mesh_name)
    mats = skm.get_editor_property("materials")
    for m in mats:
        m.set_editor_property("material_interface", mi)
    skm.set_editor_property("materials", mats)
    unreal.EditorAssetLibrary.save_directory("/Game/Knight")   # (the mesh AND, for Heavy, the skeleton the import made with it)
    if style == "Heavy": skel = skm.get_editor_property("skeleton")
    b = skm.get_bounds()
    unreal.log("KNIGHTDIAG %s imported: skeleton %s, %d material slots %s, bounds origin %s extent %s" % (mesh_name, skm.get_editor_property("skeleton").get_path_name(), len(mats), [str(m.get_editor_property("material_slot_name")) for m in mats], b.origin, b.box_extent))

# every style must sit on the one skeleton, or the game's bone copy would drive the wrong bones
paths = set()
for _, mesh_name, _, _, _ in STYLES:
    skm = unreal.EditorAssetLibrary.load_asset("/Game/Knight/" + mesh_name)
    if skm: paths.add(skm.get_editor_property("skeleton").get_path_name())
if len(paths) == 1: unreal.log("KNIGHTDIAG all sets share one skeleton " + paths.pop())
else: unreal.log_error("KNIGHTDIAG the sets are on different skeletons: %s" % sorted(paths))
