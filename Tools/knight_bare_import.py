# Brings the bare knight (knight_bare_export.py) into /Game/NewSuit on the suit's own skeleton. Run by Tools\ImportBareKnight.ps1 inside a windowless editor.
# knight_suit_import.py wipes /Game/NewSuit: run this again after any suit import.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
folder = "/Game/NewSuit"
skeleton = lib.load_asset(folder + "/SK_NewSuit").get_editor_property("skeleton")
if lib.does_asset_exist(folder + "/SK_NewSuitBare"): lib.delete_asset(folder + "/SK_NewSuitBare")

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("skeleton", skeleton)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("create_physics_asset", False)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("import_morph_targets", True)   # (the fists, FistR / FistL: knight_fist.py; the under-mail has none)
sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
sk.set_editor_property("convert_scene", True)
task = unreal.AssetImportTask()
task.filename = os.path.join(proj, "Saved", "KnightBuild", "Suit", "SK_NewSuitBare.fbx")
task.destination_path = folder
task.destination_name = "SK_NewSuitBare"
task.replace_existing = True
task.automated = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_NewSuitBare")
if skm is None: raise RuntimeError("SUITDIAG SK_NewSuitBare did not import")
def morphs(m):
    """The mesh's morph target names (the fists, knight_fist.py)."""
    try: return [str(n) for n in m.get_all_morph_target_names()]
    except Exception as e: return ["?" + str(e)]
unreal.log("SUITDIAG bare morph targets: %s" % morphs(skm))
mail = lib.load_asset(folder + "/MI_Suit_Mail")
slots = []
for m in skm.get_editor_property("materials"):
    m.set_editor_property("material_interface", mail)
    slots.append(m)
skm.set_editor_property("materials", slots)
skm.modify()
lib.save_loaded_asset(skm, False)
unreal.log("SUITDIAG bare slots: %s" % [str(m.get_editor_property("material_slot_name")) for m in skm.get_editor_property("materials")])

# ---- The mail always under the armour (knight_under_export.py). Its material moves it 4 cm away from the camera as it draws, so a plate it comes near always wins; Tint lets a test picture show it red.
if lib.does_asset_exist(folder + "/SK_NewSuitUnder"): lib.delete_asset(folder + "/SK_NewSuitUnder")
if lib.does_asset_exist(folder + "/M_SuitUnder"): lib.delete_asset(folder + "/M_SuitUnder")
mel = unreal.MaterialEditingLibrary
um = tools.create_asset("M_SuitUnder", folder, unreal.Material, unreal.MaterialFactoryNew())
um.set_editor_property("used_with_skeletal_mesh", True)
tex = mel.create_material_expression(um, unreal.MaterialExpressionTextureSample, -700, 0)
tex.set_editor_property("texture", lib.load_asset(folder + "/T_Mail_Base"))
grey = mel.create_material_expression(um, unreal.MaterialExpressionDesaturation, -450, 0)
mel.connect_material_expressions(tex, "RGB", grey, "")
tint = mel.create_material_expression(um, unreal.MaterialExpressionVectorParameter, -450, 150)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(2.3, 2.3, 2.3, 1.0))   # (Meshy painted the mail nearly black: greyed and lifted as the plate is)
mul = mel.create_material_expression(um, unreal.MaterialExpressionMultiply, -200, 0)
mel.connect_material_expressions(grey, "", mul, "A")
mel.connect_material_expressions(tint, "", mul, "B")
mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
for prop, x, y in ((unreal.MaterialProperty.MP_METALLIC, 0.4, 200), (unreal.MaterialProperty.MP_ROUGHNESS, 0.6, 300)):
    c = mel.create_material_expression(um, unreal.MaterialExpressionConstant, -200, y)
    c.set_editor_property("r", x)
    mel.connect_material_property(c, "", prop)
# (the toward-the-camera line worked out by hand: the engine's Camera Vector node only works for colours, not for moving points)
campos = mel.create_material_expression(um, unreal.MaterialExpressionCameraPositionWS, -900, 350)
here = mel.create_material_expression(um, unreal.MaterialExpressionWorldPosition, -900, 450)
sub = mel.create_material_expression(um, unreal.MaterialExpressionSubtract, -700, 400)
mel.connect_material_expressions(campos, "", sub, "A")
mel.connect_material_expressions(here, "", sub, "B")
cam = mel.create_material_expression(um, unreal.MaterialExpressionNormalize, -450, 400)
mel.connect_material_expressions(sub, "", cam, "")
back = mel.create_material_expression(um, unreal.MaterialExpressionConstant, -450, 500)
back.set_editor_property("r", -4.0)
push = mel.create_material_expression(um, unreal.MaterialExpressionMultiply, -200, 400)
mel.connect_material_expressions(cam, "", push, "A")
mel.connect_material_expressions(back, "", push, "B")
mel.connect_material_property(push, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
mel.recompile_material(um)
lib.save_loaded_asset(um, False)
task = unreal.AssetImportTask()
task.filename = os.path.join(proj, "Saved", "KnightBuild", "Suit", "SK_NewSuitUnder.fbx")
task.destination_path = folder
task.destination_name = "SK_NewSuitUnder"
task.replace_existing = True
task.automated = True
task.save = True
task.options = ui
tools.import_asset_tasks([task])
skm = lib.load_asset(folder + "/SK_NewSuitUnder")
if skm is None: raise RuntimeError("SUITDIAG SK_NewSuitUnder did not import")
slots = []
for m in skm.get_editor_property("materials"):
    m.set_editor_property("material_interface", um)
    slots.append(m)
skm.set_editor_property("materials", slots)
skm.modify()
lib.save_loaded_asset(skm, False)
unreal.log("SUITDIAG under slots: %s" % [str(m.get_editor_property("material_slot_name")) for m in skm.get_editor_property("materials")])
unreal.log("SUITDIAG done")
