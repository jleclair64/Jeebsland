# One-colour leaf materials for the DZ forests in the sample tree groves (TreeGroves.cpp; 2026-11-15, one colour per forest 2026-11-16):
# child material instances of the pack's own leaf materials with the fall switch on (static, so a run-time dynamic instance cannot set it).
# The pack's own fall look overlays a mosaic picture of red, orange, yellow and green on the leaves (Fall Overlay Image) and, at Fall Color
# Amount 1, doubles its saturation: every tree came out every colour at once, leaf by leaf, and glowing. Here the mosaic is a plain white
# picture, the leaf picture is turned grey (Base Color Desaturation 1) and the amount is 0, so the leaves take only the colour TreeGroves.cpp
# gives "Fall Color Tint" (one per forest, live from jeeb.Trees.Dz*), with the leaf picture's own lighter and darker shades.
# Output: /Game/TreeGroves (never cooked: DefaultGame.ini). The packs' own folders are only read.
# Run with the editor closed: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file, forward slashes> -unattended -nullrhi
import unreal
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DST = '/Game/TreeGroves'
if not EAL.does_directory_exist(DST): EAL.make_directory(DST)
DZ = '/Game/DZ_Assets/DZ_Trees/Material_Instances'
ONE = {'Fall Color Amount': 0.0, 'Fall Color Power': 2.0, 'Base Color Desaturation': 1.0}
WHITE = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
SW = {'Use Fall Setup?': True, 'Use Partly Colored Leaves': False}
# (new name, parent, static switches, scalars)
JOBS = [
    ('MI_Fall_AspenLeavesBig',   DZ + '/Aspen/MI_Aspen_Leaves_Big_1',   SW, ONE),
    ('MI_Fall_AspenLeavesSmall', DZ + '/Aspen/MI_Aspen_Leaves_Small_7', SW, ONE),
    ('MI_Fall_CorkOakLeaves',    DZ + '/Cork_Oak/MI_Cork_Oak_Leaves_1', SW, ONE),
]
for name, parent, switches, scalars in JOBS:
    path = DST + '/' + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    mi = tools.create_asset(name, DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', unreal.load_asset(parent))
    for k, v in switches.items(): mel.set_material_instance_static_switch_parameter_value(mi, k, v)
    for k, v in scalars.items(): mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.set_material_instance_texture_parameter_value(mi, 'Fall Overlay Image', WHITE)   # (one plain colour: no leaf-by-leaf mix)
    mel.update_material_instance(mi)
    EAL.save_asset(path, only_if_is_dirty=False)
    unreal.log('GROVEMAT made %s from %s %s %s, overlay %s' % (path, parent, switches, scalars, mel.get_material_instance_texture_parameter_value(mi, 'Fall Overlay Image')))
unreal.log('GROVEMAT done')
