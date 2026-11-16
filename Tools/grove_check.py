# Prints the static switches and season parameters of the groves' autumn materials into Saved/GroveCheck.txt (2026-11-15).
import unreal, os
mel = unreal.MaterialEditingLibrary
out = []
for n in ['MI_Fall_AspenLeavesBig']:
    m = unreal.load_asset('/Game/TreeGroves/' + n)
    out.append('== %s parent %s' % (n, m.get_editor_property('parent').get_path_name()))
    for s in mel.get_static_switch_parameter_names(m):
        v = mel.get_material_instance_static_switch_parameter_value(m, s)
        if 'eason' in str(s) or 'all' in str(s) or 'ealth' in str(s): out.append('  switch %s = %s' % (s, v))
    for s in mel.get_scalar_parameter_names(m):
        if 'eason' in str(s) or 'all' in str(s) or 'ealth' in str(s): out.append('  scalar %s = %s' % (s, mel.get_material_instance_scalar_parameter_value(m, s)))
open(os.path.join(unreal.SystemLibrary.get_project_directory(), 'Saved', 'GroveCheck.txt'), 'w').write('\n'.join(out))
unreal.log('GROVECHECK done')
