# Brings the props from Saved/KnightBuild/Props into /Game/Props (2026-11-05): one static mesh and one material instance each, on the shared M_Prop.
# Run by Tools\BuildProps.ps1 in a windowless editor. M_Prop: the model's own pictures, except where the vertex colour's red says the tier's or the potion's colour goes (Tint).
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Props")
folder = "/Game/Props"
NAMES = [n[:-4] for n in os.listdir(work) if n.endswith(".fbx")]
if not lib.does_directory_exist(folder): lib.make_directory(folder)

def pic(name, srgb, normal=False):
    f = os.path.join(work, name + ".png")
    if not os.path.exists(f): return None
    task = unreal.AssetImportTask()
    task.filename = f; task.destination_path = folder; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    t.set_editor_property("srgb", srgb)
    if normal: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(folder + "/" + name)
    return t

def make_material(default_base, default_mr):
    # (a picture slot with no default picture doesn't compile, and the game then silently draws every prop in plain grey: check the log for "Failed to compile Material")
    path = folder + "/M_Prop"
    if lib.does_asset_exist(path): lib.delete_asset(path)
    m = tools.create_asset("M_Prop", folder, unreal.Material, unreal.MaterialFactoryNew())
    E = mel.create_material_expression
    def tex(name, x, y, kind):
        t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        t.set_editor_property("parameter_name", name)
        t.set_editor_property("sampler_type", kind)
        return t
    base = tex("BaseTex", -1200, -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    base.set_editor_property("texture", default_base)
    mr = tex("MRTex", -1200, 200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    mr.set_editor_property("texture", default_mr)
    nrm = tex("NormalTex", -1200, 600, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    flat = lib.load_asset("/Engine/EngineMaterials/FlatNormal")
    nrm.set_editor_property("texture", flat)
    vc = E(m, unreal.MaterialExpressionVertexColor, -1200, 1000)
    def vec(name, x, y, v):
        p = E(m, unreal.MaterialExpressionVectorParameter, x, y)
        p.set_editor_property("parameter_name", name); p.set_editor_property("default_value", unreal.LinearColor(*v))
        return p
    def sca(name, x, y, v):
        p = E(m, unreal.MaterialExpressionScalarParameter, x, y)
        p.set_editor_property("parameter_name", name); p.set_editor_property("default_value", v)
        return p
    tint = vec("Tint", -900, -500, (1, 1, 1, 1))
    gain = sca("TintGain", -900, -350, 1.0)
    floor = sca("TintFloor", -900, -420, 0.45)   # (a near-black head still takes the colour, with the painted detail on top)
    metal = sca("TintMetal", -900, 300, 0.0)
    rough = sca("TintRough", -900, 450, 0.5)
    des = E(m, unreal.MaterialExpressionDesaturation, -800, -200)
    mel.connect_material_expressions(base, "RGB", des, "")
    mul1 = E(m, unreal.MaterialExpressionMultiply, -600, -300)
    mel.connect_material_expressions(des, "", mul1, "A"); mel.connect_material_expressions(gain, "", mul1, "B")
    add1 = E(m, unreal.MaterialExpressionAdd, -500, -300)
    mel.connect_material_expressions(mul1, "", add1, "A"); mel.connect_material_expressions(floor, "", add1, "B")
    mul2 = E(m, unreal.MaterialExpressionMultiply, -400, -300)
    mel.connect_material_expressions(add1, "", mul2, "A"); mel.connect_material_expressions(tint, "", mul2, "B")
    def lerp(a, an, b, bn, x, y):
        l = E(m, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, an, l, "A"); mel.connect_material_expressions(b, bn, l, "B"); mel.connect_material_expressions(vc, "R", l, "Alpha")
        return l
    lb = lerp(base, "RGB", mul2, "", -250, -200)
    lm = lerp(mr, "B", metal, "", -250, 100)
    lr = lerp(mr, "G", rough, "", -250, 300)
    MP = unreal.MaterialProperty
    mel.connect_material_property(lb, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(lm, "", MP.MP_METALLIC)
    mel.connect_material_property(lr, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(nrm, "RGB", MP.MP_NORMAL)
    mel.recompile_material(m)
    lib.save_asset(path)
    return m

parent = make_material(pic("T_%s_Base" % NAMES[0], True), pic("T_%s_MR" % NAMES[0], False))
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
for n in NAMES:
    mi_path = folder + "/MI_" + n
    if lib.does_asset_exist(mi_path): lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_" + n, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_%s_Base" % n, True))
    mr = pic("T_%s_MR" % n, False)
    if mr: mel.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    nm = pic("T_%s_Normal" % n, False, True)
    if nm: mel.set_material_instance_texture_parameter_value(mi, "NormalTex", nm)
    lib.save_asset(mi_path)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    d = ui.get_editor_property("static_mesh_import_data")
    d.set_editor_property("combine_meshes", True)
    d.set_editor_property("auto_generate_collision", False)
    d.set_editor_property("generate_lightmap_u_vs", False)
    d.set_editor_property("convert_scene", True)
    d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, n + ".fbx")
    task.destination_path = folder; task.destination_name = "SM_" + n
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    sm = lib.load_asset(folder + "/SM_" + n)
    if sm is None: raise RuntimeError("PROPDIAG " + n + " did not import")
    sm.set_material(0, mi)
    lib.save_asset(folder + "/SM_" + n)
    b = sm.get_bounding_box()
    unreal.log("PROPDIAG %s min %s max %s" % (n, b.min, b.max))
unreal.log("PROPDIAG done")
