# Brings the wizard (Tools/wizard_export.py) into /Game/Wizard (2026-11-21): SK_Wizard on the suit's own skeleton (/Game/NewSuit/SK_NewSuit_Skeleton),
# one material instance per Meshy picture set (his plain-clothes body, his robed body, the hat, the spell book), and M_WizardOrb, the see-through
# blue material of his guard orb and his teleport's flashes. Run by Tools\ImportWizard.ps1 inside a windowless editor; again after any ImportNewSuit.ps1.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "Wizard", "Build")
folder = "/Game/Wizard"
SETS = ["Base", "Robe", "Hat", "Book"]
if lib.does_directory_exist(folder): lib.delete_directory(folder)

def pic(name, srgb):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, name + ".png")
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    t = lib.load_asset(folder + "/" + name)
    if t is None: raise RuntimeError("WIZDIAG the picture " + name + " did not import")
    t.set_editor_property("srgb", srgb)
    t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    if name.endswith("_Normal"): t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    elif not srgb: t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    lib.save_asset(folder + "/" + name)
    return t

parent = lib.load_asset("/Game/Knight/M_KnightSkin")
flat = lib.load_asset("/Engine/EngineMaterials/FlatNormal")
mats = {}
for p in SETS:
    mi = tools.create_asset("MI_Wiz_" + p, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "BaseTex", pic("T_Wiz_%s_Base" % p, True))
    if os.path.exists(os.path.join(work, "T_Wiz_%s_MR.png" % p)):
        mel.set_material_instance_texture_parameter_value(mi, "MRTex", pic("T_Wiz_%s_MR" % p, False))
    nrm = os.path.exists(os.path.join(work, "T_Wiz_%s_Normal.png" % p))
    mel.set_material_instance_texture_parameter_value(mi, "NormalTex", pic("T_Wiz_%s_Normal" % p, False) if nrm else flat)
    lib.save_asset(folder + "/MI_Wiz_" + p)
    mats[p] = mi

# ---- the orb: unlit, see-through, brighter toward its rim (a soap-bubble shield), Color and Opacity set by the game
orb = tools.create_asset("M_WizardOrb", folder, unreal.Material, unreal.MaterialFactoryNew())
orb.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
orb.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
orb.set_editor_property("two_sided", True)
orb.set_editor_property("used_with_instanced_static_meshes", True)   # (the pipe's smoke puffs are instanced)
col = mel.create_material_expression(orb, unreal.MaterialExpressionVectorParameter, -700, 0)
col.set_editor_property("parameter_name", "Color"); col.set_editor_property("default_value", unreal.LinearColor(0.25, 0.55, 1.0, 1.0))
opa = mel.create_material_expression(orb, unreal.MaterialExpressionScalarParameter, -700, 250)
opa.set_editor_property("parameter_name", "Opacity"); opa.set_editor_property("default_value", 1.0)
fre = mel.create_material_expression(orb, unreal.MaterialExpressionFresnel, -700, 450)
fre.set_editor_property("exponent", 2.5); fre.set_editor_property("base_reflect_fraction", 0.0)
def const(v, x, y):
    c = mel.create_material_expression(orb, unreal.MaterialExpressionConstant, x, y); c.set_editor_property("r", v); return c
def op(cls, a, b, x, y):
    e = mel.create_material_expression(orb, cls, x, y)
    mel.connect_material_expressions(a, "", e, "A"); mel.connect_material_expressions(b, "", e, "B"); return e
# glow = Color * (0.10 + 0.8 * fresnel)
rim = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, fre, const(0.8, -500, 520), -450, 450), const(0.10, -500, 600), -300, 450)
glow = op(unreal.MaterialExpressionMultiply, col, rim, -150, 100)
mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
# opacity = Opacity * (0.04 + 0.45 * fresnel), never past 0.9
body = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, fre, const(0.45, -500, 720), -450, 700), const(0.04, -500, 800), -300, 700)
alpha = op(unreal.MaterialExpressionMultiply, opa, body, -150, 400)
sat = mel.create_material_expression(orb, unreal.MaterialExpressionMin, 0, 400)
mel.connect_material_expressions(alpha, "", sat, "A"); mel.connect_material_expressions(const(0.9, -150, 600), "", sat, "B")
mel.connect_material_property(sat, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(orb)
lib.save_loaded_asset(orb, False)

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("skeleton", lib.load_asset("/Game/NewSuit/SK_NewSuit_Skeleton"))
ui.set_editor_property("create_physics_asset", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("import_morph_targets", True)
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
sk.set_editor_property("use_t0_as_ref_pose", False)
task = unreal.AssetImportTask()
task.filename = os.path.join(work, "SK_Wizard.fbx")
task.destination_path = folder
task.destination_name = "SK_Wizard"
task.replace_existing = True
task.automated = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_Wizard")
if skm is None: raise RuntimeError("WIZDIAG SK_Wizard did not import")
new_slots = []
for m in skm.get_editor_property("materials"):
    nm = str(m.get_editor_property("material_slot_name"))
    hit = [p for p in ("Hat", "Book", "Base", "Robe") if ("Wiz_" + p) in nm]
    if not hit: raise RuntimeError("WIZDIAG slot with no picture set: " + nm)
    m.set_editor_property("material_interface", mats[hit[0]])
    new_slots.append(m)
skm.set_editor_property("materials", new_slots)
skm.modify()
lib.save_loaded_asset(skm, False)
lib.save_directory(folder)
check = lib.load_asset(folder + "/SK_Wizard").get_editor_property("materials")
unreal.log("WIZDIAG slots: %s" % [(str(m.get_editor_property("material_slot_name")), m.get_editor_property("material_interface").get_name() if m.get_editor_property("material_interface") else None) for m in check])
unreal.log("WIZDIAG bounds extent %s" % skm.get_bounds().box_extent)
unreal.log("WIZDIAG files now: %s" % [p.split("/")[-1] for p in lib.list_assets(folder, recursive=False)])
unreal.log("WIZDIAG done")
