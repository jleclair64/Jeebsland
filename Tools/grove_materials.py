# Autumn leaf materials for the sample tree groves (TreeGroves.cpp, 2026-11-15): child material instances of the packs' own leaf materials with
# their fall / season switch turned on (the packs ship none ready made; the switches are static, so a run-time dynamic instance cannot set them).
# Output: /Game/TreeGroves (never cooked: DefaultGame.ini). The packs' own folders are only read.
# Run with the editor closed: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file, forward slashes> -unattended -nullrhi
import unreal
mel = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DST = '/Game/TreeGroves'
if not EAL.does_directory_exist(DST): EAL.make_directory(DST)
DZ = '/Game/DZ_Assets/DZ_Trees/Material_Instances'
MP = '/Game/Megaplant_Library'
# (new name, parent, static switches, scalars)
JOBS = [
    ('MI_Fall_AspenLeavesBig',   DZ + '/Aspen/MI_Aspen_Leaves_Big_1',      {'Use Fall Setup?': True}, {'Fall Color Amount': 1.0}),
    ('MI_Fall_AspenLeavesSmall', DZ + '/Aspen/MI_Aspen_Leaves_Small_7',    {'Use Fall Setup?': True}, {'Fall Color Amount': 1.0}),
    ('MI_Fall_CorkOakLeaves',    DZ + '/Cork_Oak/MI_Cork_Oak_Leaves_1',    {'Use Fall Setup?': True}, {'Fall Color Amount': 1.0}),
    ('MI_Fall_HornbeamFoliage',  MP + '/Tree_Hornbeam/Materials/MI_Tree_Hornbeam_01_Foliage', {'Seasons': True}, {}),
    ('MI_Fall_EnglishOakFoliage', MP + '/Tree_EnglishOak/Materials/MI_Tree_EnglishOak_01_Foliage', {'Seasons': True}, {}),
]
for name, parent, switches, scalars in JOBS:
    path = DST + '/' + name
    if EAL.does_asset_exist(path): EAL.delete_asset(path)
    mi = tools.create_asset(name, DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', unreal.load_asset(parent))
    for k, v in switches.items(): mel.set_material_instance_static_switch_parameter_value(mi, k, v)
    for k, v in scalars.items(): mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    EAL.save_asset(path, only_if_is_dirty=False)
    unreal.log('GROVEMAT made %s from %s %s %s' % (path, parent, switches, scalars))
unreal.log('GROVEMAT done')
