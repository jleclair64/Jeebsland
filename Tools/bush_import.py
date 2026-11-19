# Imports the new bushes (Tools/bush_build.py's FBX files) into /Game/Bushes, each with its far LOD, and makes their leaf materials:
# children of the DZ tree packs' one-colour leaf twins (/Game/TreeGroves, Tools/grove_materials.py), so the bushes take the same
# "Fall Color Tint" the forest's trees take (Bushes.cpp sets it live from jeeb.Trees.DzGreen and jeeb.Bush.Tint). No collision, no Nanite.
# Run by Tools/BuildBushes.ps1 with the editor closed.
import unreal, os
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
work = os.environ.get('JEEB_BUSH_FBX', '').replace('\\', '/')
DST = '/Game/Bushes'
if not lib.does_directory_exist(DST): lib.make_directory(DST)

# (name, parent: the leaf twin the forest uses)
MATS = [
    ('MI_BushAspen', '/Game/TreeGroves/MI_Fall_AspenLeavesBig'),
    ('MI_BushAspenSmall', '/Game/TreeGroves/MI_Fall_AspenLeavesSmall'),
    ('MI_BushOak', '/Game/TreeGroves/MI_Fall_CorkOakLeaves'),
]
GREEN = unreal.LinearColor(0.17, 0.24, 0.065, 1.0)   # (a touch darker than jeeb.Trees.DzGreen's default: bushes stand in the trees' shade; live in game)
made = {}
for name, parent in MATS:
    path = DST + '/' + name
    if lib.does_asset_exist(path): lib.delete_asset(path)
    mi = tools.create_asset(name, DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset(parent))
    mel.set_material_instance_vector_parameter_value(mi, 'Fall Color Tint', GREEN)
    mel.update_material_instance(mi)
    lib.save_asset(path, only_if_is_dirty=False)
    made[name] = mi
    unreal.log('BUSHDIAG material %s from %s' % (path, parent))

# kind: the material of each slot, in the FBX's slot order
KINDS = {
    'BushFull':  ['MI_BushAspen', 'MI_BushAspenSmall'],
    'BushSmall': ['MI_BushAspen', 'MI_BushAspenSmall'],
    'BushFine':  ['MI_BushOak'],
    'BushLow':   ['MI_BushOak'],
    'BushHedge': ['MI_BushOak'],
}
unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX False')
def options():
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    d = ui.get_editor_property('static_mesh_import_data')
    d.set_editor_property('combine_meshes', True)
    d.set_editor_property('auto_generate_collision', False)
    d.set_editor_property('generate_lightmap_u_vs', False)
    d.set_editor_property('convert_scene', True)
    d.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    d.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    d.set_editor_property('build_nanite', False)
    return ui

for kind, mats in KINDS.items():
    path = DST + '/SM_' + kind
    if lib.does_asset_exist(path): lib.delete_asset(path)
    task = unreal.AssetImportTask()
    task.filename = work + '/SM_' + kind + '.fbx'
    task.destination_path = DST; task.destination_name = 'SM_' + kind
    task.replace_existing = True; task.automated = True; task.save = True
    task.options = options()
    tools.import_asset_tasks([task])
    sm = lib.load_asset(path)
    if sm is None: raise RuntimeError('BUSHDIAG %s did not import' % kind)
    lod = sub.import_lod(sm, 1, work + '/SM_' + kind + '_LOD1.fbx')
    for i, m in enumerate(mats):
        if i < len(sm.static_materials): sm.set_material(i, made[m])
    sm.set_editor_property('light_map_resolution', 4)
    try:
        sub.set_lod_screen_sizes(sm, [1.0, 0.18]) if hasattr(sub, 'set_lod_screen_sizes') else None
    except Exception as e:
        unreal.log('BUSHDIAG screen sizes: %s' % e)
    bs = sm.get_editor_property('body_setup')
    if bs: bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    lib.save_asset(path, only_if_is_dirty=False)
    b = sm.get_bounding_box()
    unreal.log('BUSHDIAG %s lods %d (lod1 %s) tris %s slots %d size %.0f x %.0f x %.0f cm' % (kind, sm.get_num_lods(), lod,
        [sm.get_num_triangles(l) for l in range(sm.get_num_lods())], len(sm.static_materials), b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z))
unreal.log('BUSHDIAG done')
