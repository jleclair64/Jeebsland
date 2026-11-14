# Brings the plaza oak (Tools/plaza_oak_clean.py's Saved/KnightBuild/Trees/PlazaOak.fbx and T_PlazaOak_Base.png) into /Game/Trees:
# SM_PlazaOak (Nanite, no collision of its own: OakHillTown.cpp gives the trunk a capsule), SM_PlazaOakShadow, T_PlazaOak_Base and M_PlazaOak.
# M_PlazaOak: the autumn picture, rough, two-sided (Meshy's leaf clumps are thin shells in places), and a gentle sway of the crown:
# the world position offset grows from nothing at CalmBelow (cm above the foot) to full at FullAbove, WindCm at most, two slow
# waves out of step across the tree. OakHillTown.cpp turns the sway off beyond 60 m (WPO disable distance).
# Run by Tools/BuildPlazaOak.ps1 in a windowless editor.
import os
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
work = os.path.join(proj, "Saved", "KnightBuild", "Trees")
folder = "/Game/Trees"
WIND_CM = 6.0         # the crown's sway at its top (cm)
CALM_BELOW = 550.0    # no sway below this height (cm above the foot: the trunk)
FULL_ABOVE = 1500.0   # full sway above this
WIND_SPEED = 0.9      # radians a second of the slow wave
ROUGH = 0.85
if not lib.does_directory_exist(folder): lib.make_directory(folder)

task = unreal.AssetImportTask()
task.filename = os.path.join(work, "T_PlazaOak_Base.png"); task.destination_path = folder; task.destination_name = "T_PlazaOak_Base"
task.replace_existing = True; task.automated = True; task.save = True
tools.import_asset_tasks([task])
tex = lib.load_asset(folder + "/T_PlazaOak_Base")
tex.set_editor_property("srgb", True)
lib.save_asset(folder + "/T_PlazaOak_Base")

path = folder + "/M_PlazaOak"
if lib.does_asset_exist(path): lib.delete_asset(path)
m = tools.create_asset("M_PlazaOak", folder, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("used_with_nanite", True)
m.set_editor_property("two_sided", True)
m.set_editor_property("max_world_position_offset_displacement", WIND_CM * 1.5)
E = mel.create_material_expression
MP = unreal.MaterialProperty
def node(cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n
def link(a, a_out, b, b_in): mel.connect_material_expressions(a, a_out, b, b_in)
def scalar(name, value, x, y): return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)
base = node(unreal.MaterialExpressionTextureSampleParameter2D, -600, -200, parameter_name="BaseTex", texture=tex)
mel.connect_material_property(base, "RGB", MP.MP_BASE_COLOR)
mel.connect_material_property(scalar("Roughness", ROUGH, -600, 50), "", MP.MP_ROUGHNESS)
mel.connect_material_property(scalar("Specular", 0.35, -600, 120), "", MP.MP_SPECULAR)
# the sway
lp = node(unreal.MaterialExpressionLocalPosition, -1400, 400)
lz = node(unreal.MaterialExpressionComponentMask, -1250, 400, r=False, g=False, b=True, a=False); link(lp, "", lz, "")
h0 = node(unreal.MaterialExpressionSubtract, -1100, 400); link(lz, "", h0, "A"); link(scalar("CalmBelow", CALM_BELOW, -1250, 480), "", h0, "B")
hf = node(unreal.MaterialExpressionDivide, -950, 450); link(h0, "", hf, "A"); link(scalar("SwayRange", FULL_ABOVE - CALM_BELOW, -1250, 560), "", hf, "B")
hs = node(unreal.MaterialExpressionSaturate, -820, 450); link(hf, "", hs, "")
wp = node(unreal.MaterialExpressionWorldPosition, -1400, 750)
wxy = node(unreal.MaterialExpressionComponentMask, -1250, 750, r=True, g=True, b=False, a=False); link(wp, "", wxy, "")
ph = node(unreal.MaterialExpressionDotProduct, -1100, 750); link(wxy, "", ph, "A")
pv = node(unreal.MaterialExpressionConstant2Vector, -1250, 830, r=0.0011, g=0.0007); link(pv, "", ph, "B")
tm = node(unreal.MaterialExpressionTime, -1250, 900)
ts = node(unreal.MaterialExpressionMultiply, -1100, 900); link(tm, "", ts, "A"); link(scalar("WindSpeed", WIND_SPEED, -1250, 960), "", ts, "B")
a1 = node(unreal.MaterialExpressionAdd, -950, 820); link(ts, "", a1, "A"); link(ph, "", a1, "B")
s1 = node(unreal.MaterialExpressionSine, -820, 820, period=6.2831853); link(a1, "", s1, "")
a2m = node(unreal.MaterialExpressionMultiply, -950, 920, const_b=1.37); link(a1, "", a2m, "A")
s2 = node(unreal.MaterialExpressionSine, -820, 920, period=6.2831853); link(a2m, "", s2, "")
v2 = node(unreal.MaterialExpressionAppendVector, -680, 860); link(s1, "", v2, "A"); link(s2, "", v2, "B")
v3 = node(unreal.MaterialExpressionAppendVector, -560, 860, ); zero = node(unreal.MaterialExpressionConstant, -680, 960, r=0.0)
link(v2, "", v3, "A"); link(zero, "", v3, "B")
amp = node(unreal.MaterialExpressionMultiply, -680, 450); link(hs, "", amp, "A"); link(scalar("WindCm", WIND_CM, -820, 520), "", amp, "B")
wpo = node(unreal.MaterialExpressionMultiply, -420, 600); link(v3, "", wpo, "A"); link(amp, "", wpo, "B")
mel.connect_material_property(wpo, "", MP.MP_WORLD_POSITION_OFFSET)
mel.recompile_material(m)
lib.save_asset(path)

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")
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
d.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
d.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.IGNORE)
d.set_editor_property("build_nanite", True)
task = unreal.AssetImportTask()
task.filename = os.path.join(work, "PlazaOak.fbx")
task.destination_path = folder; task.destination_name = "SM_PlazaOak"
task.replace_existing = True; task.automated = True; task.save = True
task.options = ui
tools.import_asset_tasks([task])
sm = lib.load_asset(folder + "/SM_PlazaOak")
if sm is None: raise RuntimeError("OAKDIAG SM_PlazaOak did not import")
sm.set_material(0, lib.load_asset(path))
lib.save_asset(folder + "/SM_PlazaOak")
b = sm.get_bounding_box()
unreal.log("OAKDIAG nanite %s min %s max %s" % (sm.get_editor_property("nanite_settings").enabled, b.min, b.max))

# The shadow copy (plaza_oak_clean.py: 60k triangles, gaps cut in the leaves): drawn only into the sun's shadows by OakHillTown.cpp,
# so the lawn under the crown is dappled. Not Nanite (small); the same material, so it sways with the tree.
if os.path.exists(os.path.join(work, "PlazaOakShadow.fbx")):
    d.set_editor_property("build_nanite", False)
    task = unreal.AssetImportTask()
    task.filename = os.path.join(work, "PlazaOakShadow.fbx")
    task.destination_path = folder; task.destination_name = "SM_PlazaOakShadow"
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = ui
    tools.import_asset_tasks([task])
    ss = lib.load_asset(folder + "/SM_PlazaOakShadow")
    if ss is None: raise RuntimeError("OAKDIAG SM_PlazaOakShadow did not import")
    ns = ss.get_editor_property("nanite_settings")   # (the import option alone left it Nanite: switch it off on the mesh)
    ns.set_editor_property("enabled", False)
    ss.set_editor_property("nanite_settings", ns)
    ss.set_material(0, lib.load_asset(path))
    lib.save_asset(folder + "/SM_PlazaOakShadow")
    unreal.log("OAKDIAG shadow copy nanite %s" % ss.get_editor_property("nanite_settings").enabled)
unreal.log("OAKDIAG done")   # (last: the editor may crash in Python on its way out after this; everything is saved by then)
