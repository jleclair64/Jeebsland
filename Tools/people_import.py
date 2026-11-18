# Brings the townspeople (Tools/people_export.py writes Saved/PeopleBuild) into /Game/People: per person a skeletal mesh with
# its own skeleton, three clips (Idle, Walk, Talk), the colour texture and the dye mask, and a material instance of M_Person
# (the main garment dyed per person by ANpc: Tint). Run by Tools/ImportPeople.ps1 inside a windowless editor. Wipes /Game/People.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(proj, "Saved", "PeopleBuild")
FOLDER = "/Game/People"

def log(s): unreal.log("PEOPLEDIAG " + s)

# (ImportPeople.ps1 deletes Content/People before this runs)
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")

def run(task_file, dest, name, options=None):
    t = unreal.AssetImportTask()
    t.filename = task_file
    t.destination_path = dest
    t.destination_name = name
    t.replace_existing = True
    t.automated = True
    t.save = True
    if options is not None: t.options = options
    tools.import_asset_tasks([t])
    return lib.load_asset(dest + "/" + name)

names = sorted(f[3:-4] for f in os.listdir(SRC) if f.startswith("SK_") and f.endswith(".fbx"))
log("people: %s" % names)
# ---- The master material: the base texture, its main garment (the mask's red) dyed to Tint keeping the cloth's own shading
def make_master(base_tex, mask_tex):
    m = tools.create_asset("M_Person", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 0)
    tex.set_editor_property("parameter_name", "Base")
    tex.set_editor_property("texture", base_tex)
    mask = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 400)
    mask.set_editor_property("parameter_name", "Mask")
    mask.set_editor_property("texture", mask_tex)
    mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    w = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -1000, 200)
    w.set_editor_property("constant", unreal.LinearColor(0.2126, 0.7152, 0.0722, 1.0))
    dot = mel.create_material_expression(m, unreal.MaterialExpressionDotProduct, -800, 150)
    mel.connect_material_expressions(tex, "RGB", dot, "A")
    mel.connect_material_expressions(w, "", dot, "B")
    glum = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 300)
    glum.set_editor_property("parameter_name", "GarmentLum")
    glum.set_editor_property("default_value", 0.1)
    div = mel.create_material_expression(m, unreal.MaterialExpressionDivide, -600, 200)
    mel.connect_material_expressions(dot, "", div, "A")
    mel.connect_material_expressions(glum, "", div, "B")
    clamp = mel.create_material_expression(m, unreal.MaterialExpressionClamp, -450, 200)
    clamp.set_editor_property("min_default", 0.0)
    clamp.set_editor_property("max_default", 2.5)
    mel.connect_material_expressions(div, "", clamp, "")
    tint = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -450, 350)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.10, 0.10, 0.10, 1.0))
    dyed = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 250)
    mel.connect_material_expressions(clamp, "", dyed, "A")
    mel.connect_material_expressions(tint, "", dyed, "B")
    dye = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -450, 500)
    dye.set_editor_property("parameter_name", "Dye")
    dye.set_editor_property("default_value", 1.0)
    amt = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 450)
    mel.connect_material_expressions(mask, "RGB", amt, "A")   # (a grayscale sample: every channel is the mask)
    mel.connect_material_expressions(dye, "", amt, "B")
    lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -150, 100)
    mel.connect_material_expressions(tex, "RGB", lerp, "A")
    mel.connect_material_expressions(dyed, "", lerp, "B")
    mel.connect_material_expressions(amt, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for prop, val, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.82, 600), (unreal.MaterialProperty.MP_SPECULAR, 0.35, 700)):
        c = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -150, y)
        c.set_editor_property("r", val)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(m)
    lib.save_loaded_asset(m, False)
    return m


smes = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
TEX = {}
for n in names:
    tb = run(os.path.join(SRC, "T_%s_Base.png" % n), FOLDER, "T_%s_Base" % n)
    tm = run(os.path.join(SRC, "T_%s_Mask.png" % n), FOLDER, "T_%s_Mask" % n)
    tm.set_editor_property("srgb", False)
    tm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    tm.set_editor_property("max_texture_size", 1024)
    lib.save_loaded_asset(tm, False)
    tb.set_editor_property("max_texture_size", 2048)
    lib.save_loaded_asset(tb, False)
    TEX[n] = (tb, tm)
m = make_master(*TEX[names[0]])
for n in names:
    tb, tm = TEX[n]
    # the body
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
    skm = run(os.path.join(SRC, "SK_%s.fbx" % n), FOLDER, "SK_%s" % n, ui)
    if skm is None: raise RuntimeError("PEOPLEDIAG SK_%s did not import" % n)
    mi = tools.create_asset("MI_%s" % n, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_texture_parameter_value(mi, "Base", tb)
    mel.set_material_instance_texture_parameter_value(mi, "Mask", tm)
    lib.save_loaded_asset(mi, False)
    slots = []
    for s in skm.get_editor_property("materials"):
        s.set_editor_property("material_interface", mi)
        slots.append(s)
    skm.set_editor_property("materials", slots)
    # cheaper bodies further off: 3 more LODs at the engine's default reduction (50%, 25%, 12.5% of the triangles)
    try:
        ok = smes.regenerate_lod(skm, 4, False, False)
        log("%s LODs regenerated: %s" % (n, ok))
    except Exception as e:
        log("%s LOD regeneration failed: %s" % (n, e))
    skm.modify()
    lib.save_loaded_asset(skm, False)
    skel = skm.get_editor_property("skeleton")
    lib.save_loaded_asset(skel, False)
    b = skm.get_bounds()
    log("%s mesh: bones %d, LODs %d, bounds origin %s extent %s" % (n, len(skel.get_editor_property("bone_tree")) if hasattr(skel, "bone_tree") else -1,
        skm.get_lod_num() if hasattr(skm, "get_lod_num") else -1, b.origin, b.box_extent))
    # the clips
    for key in ("Idle", "Walk", "Talk"):
        f = os.path.join(SRC, "A_%s_%s.fbx" % (n, key))
        if not os.path.exists(f): log("%s missing clip %s" % (n, key)); continue
        ua = unreal.FbxImportUI()
        ua.set_editor_property("import_mesh", False)
        ua.set_editor_property("import_as_skeletal", True)
        ua.set_editor_property("import_materials", False)
        ua.set_editor_property("import_textures", False)
        ua.set_editor_property("import_animations", True)
        ua.set_editor_property("automated_import_should_detect_type", False)
        ua.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        ua.set_editor_property("skeleton", skel)
        an = ua.get_editor_property("anim_sequence_import_data")
        an.set_editor_property("convert_scene", True)
        an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        an.set_editor_property("import_bone_tracks", True)
        an.set_editor_property("use_default_sample_rate", False)
        an.set_editor_property("custom_sample_rate", 30)
        an.set_editor_property("snap_to_closest_frame_boundary", True)
        seq = run(f, FOLDER, "A_%s_%s" % (n, key), ua)
        if seq is None: log("%s clip %s did not import" % (n, key)); continue
        log("%s clip %s: %.2f s" % (n, key, seq.get_play_length()))
lib.save_directory(FOLDER, only_if_is_dirty=False, recursive=True)
log("done")
