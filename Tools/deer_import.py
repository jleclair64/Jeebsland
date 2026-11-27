# Brings the huntable animals (Tools/deer_export.py writes Saved/DeerBuild) into /Game/Animals: per animal a skeletal mesh with its own
# skeleton, its clips (Idle, Idle2, Walk, Run, Graze, Alert, Death), the colour and normal textures and a material instance of M_Animal.
# Run by Tools/BuildDeer.ps1 inside a windowless editor (which deletes Content/Animals first).
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(proj, "Saved", "DeerBuild")
FOLDER = "/Game/Animals"
CLIPS = ("Idle", "Idle2", "Walk", "Run", "Graze", "Alert", "Death", "Back", "Attack", "Gallop")
# (per animal: how much grey and what tint M_Animal puts on its coat; the stag's are M_Animal's defaults. The horse's bay coat is already
# a rich brown, so it keeps nearly all of it)
LOOK = {"Horse": (0.08, (0.95, 0.93, 0.91)), "Rabbit": (0.05, (0.95, 0.93, 0.91)), "Fox": (0.10, (0.92, 0.88, 0.86)),
        "Boar": (0.0, (1.0, 0.97, 0.94)), "Wolf": (0.05, (0.95, 0.94, 0.93)), "Moose": (0.05, (0.92, 0.90, 0.88))}   # (2026-11-24)

def log(s): unreal.log("DEERDIAG " + s)

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
log("animals: %s" % names)

def make_master(base_tex, normal_tex):
    m = tools.create_asset("M_Animal", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    tex.set_editor_property("parameter_name", "Base")
    tex.set_editor_property("texture", base_tex)
    # (the library model's coat is a strong orange, and the game's sunlight pushes colours redder still: a little grey in it and a touch
    # darker reads as a red deer's chestnut. Both are parameters: Grey 0 and Tint white give the picture as painted)
    grey = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 220)
    grey.set_editor_property("parameter_name", "Grey")
    grey.set_editor_property("default_value", 0.28)
    des = mel.create_material_expression(m, unreal.MaterialExpressionDesaturation, -450, 60)
    mel.connect_material_expressions(tex, "RGB", des, "")
    mel.connect_material_expressions(grey, "", des, "Fraction")
    tint = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -450, 220)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.80, 0.78, 0.76, 1.0))
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 100)
    mel.connect_material_expressions(des, "", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    nrm = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 350)
    nrm.set_editor_property("parameter_name", "Normal")
    nrm.set_editor_property("texture", normal_tex)
    nrm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    for prop, val, y in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.78, 600), (unreal.MaterialProperty.MP_SPECULAR, 0.3, 700)):
        c = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -300, y)
        c.set_editor_property("r", val)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(m)
    lib.save_loaded_asset(m, False)
    return m

smes = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
TEX = {}
for n in names:
    tb = run(os.path.join(SRC, "T_%s_Base.png" % n), FOLDER, "T_%s_Base" % n)
    tb.set_editor_property("max_texture_size", 2048)
    lib.save_loaded_asset(tb, False)
    tn = run(os.path.join(SRC, "T_%s_Normal.png" % n), FOLDER, "T_%s_Normal" % n)
    tn.set_editor_property("srgb", False)
    tn.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    tn.set_editor_property("flip_green_channel", True)   # (Blender bakes +Y normals, Unreal reads -Y)
    tn.set_editor_property("max_texture_size", 1024)
    lib.save_loaded_asset(tn, False)
    TEX[n] = (tb, tn)
m = make_master(*TEX[names[0]])
for n in names:
    tb, tn = TEX[n]
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
    sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    sk.set_editor_property("convert_scene", True)
    sk.set_editor_property("convert_scene_unit", True)   # (the files are in metres: deer_export.py)
    skm = run(os.path.join(SRC, "SK_%s.fbx" % n), FOLDER, "SK_%s" % n, ui)
    if skm is None: raise RuntimeError("DEERDIAG SK_%s did not import" % n)
    mi = tools.create_asset("MI_%s" % n, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_texture_parameter_value(mi, "Base", tb)
    mel.set_material_instance_texture_parameter_value(mi, "Normal", tn)
    if n in LOOK:
        mel.set_material_instance_scalar_parameter_value(mi, "Grey", LOOK[n][0])
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(*LOOK[n][1], 1.0))
    lib.save_loaded_asset(mi, False)
    slots = []
    for s in skm.get_editor_property("materials"):
        s.set_editor_property("material_interface", mi)
        slots.append(s)
    skm.set_editor_property("materials", slots)
    try:
        ok = smes.regenerate_lod(skm, 4, False, False)   # 3 more LODs at the engine's default reduction
        log("%s LODs regenerated: %s" % (n, ok))
    except Exception as e:
        log("%s LOD regeneration failed: %s" % (n, e))
    skm.modify()
    lib.save_loaded_asset(skm, False)
    skel = skm.get_editor_property("skeleton")
    lib.save_loaded_asset(skel, False)
    b = skm.get_bounds()
    log("%s mesh: LODs %d, bounds origin %s extent %s" % (n, skm.get_lod_num() if hasattr(skm, "get_lod_num") else -1, b.origin, b.box_extent))
    for key in CLIPS:
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
        an.set_editor_property("convert_scene_unit", True)
        an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        an.set_editor_property("import_bone_tracks", True)
        an.set_editor_property("use_default_sample_rate", False)
        an.set_editor_property("custom_sample_rate", 30)
        an.set_editor_property("snap_to_closest_frame_boundary", True)
        seq = run(f, FOLDER, "A_%s_%s" % (n, key), ua)
        if seq is None: log("%s clip %s did not import" % (n, key)); continue
        log("%s clip %s: %.2f s" % (n, key, seq.get_play_length()))
lib.save_directory(FOLDER, only_if_is_dirty=False, recursive=True)

# The horse's saddle (Tools/saddle_prep.py) and the eagle's two still bodies (Tools/eagle_prep.py, 2026-11-26): static meshes, each with its
# own instance of M_Animal (a flat normal)
def static_one(nm, tint):
    sf = os.path.join(SRC, "SM_%s.fbx" % nm)
    if not os.path.exists(sf): return
    ts = run(os.path.join(SRC, "T_%s_Base.png" % nm), FOLDER, "T_%s_Base" % nm)
    ts.set_editor_property("max_texture_size", 1024)
    lib.save_loaded_asset(ts, False)
    us = unreal.FbxImportUI()
    us.set_editor_property("import_mesh", True)
    us.set_editor_property("import_as_skeletal", False)
    us.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    us.set_editor_property("import_materials", False)
    us.set_editor_property("import_textures", False)
    sd = us.get_editor_property("static_mesh_import_data")
    sd.set_editor_property("convert_scene", True)
    sd.set_editor_property("convert_scene_unit", True)
    sd.set_editor_property("combine_meshes", True)
    sd.set_editor_property("generate_lightmap_u_vs", False)
    sm = run(sf, FOLDER, "SM_" + nm, us)
    if sm is None: log("SM_%s did not import" % nm); return
    mi = tools.create_asset("MI_" + nm, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, m)
    mel.set_material_instance_texture_parameter_value(mi, "Base", ts)
    mel.set_material_instance_texture_parameter_value(mi, "Normal", lib.load_asset("/Engine/EngineMaterials/FlatNormal"))
    mel.set_material_instance_scalar_parameter_value(mi, "Grey", 0.0)
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(*tint))
    lib.save_loaded_asset(mi, False)
    sm.set_material(0, mi)
    lib.save_loaded_asset(sm, False)
    b = sm.get_bounds()
    log("%s: bounds origin %s extent %s" % (nm.lower(), b.origin, b.box_extent))
static_one("Saddle", (1.15, 1.1, 1.05, 1.0))
static_one("EagleFly", (0.78, 0.68, 0.62, 1.0))   # (in the game's sun the repaint read orange: a cooler, darker brown)
static_one("EaglePerch", (1.0, 1.0, 1.0, 1.0))
lib.save_directory(FOLDER, only_if_is_dirty=False, recursive=True)
log("done")
