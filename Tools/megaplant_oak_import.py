# Imports Quixel's "Megaplants: English Oak" from Fab's USD download (Fab's "Export to Unreal Engine" failed for it)
# into /Game/Megaplant_Library/Tree_EnglishOak, laid out like the Hornbeam that did arrive (Tree_Hornbeam):
#   Tree_EnglishOak_01/SK_EnglishOak_01_A..D (+ _Skeleton): the four trees, Nanite assembly skeletal meshes with dynamic wind
#   Instances/SKM_*/SK_*: the branch pieces the assemblies are built from
#   Materials/MI_Tree_EnglishOak_01_Bark / _Foliage: children of PVE's MA_Foliage_Trees (the USD carries no textures, see below)
# Run headless: UnrealEditor-Cmd <uproject> -ExecutePythonScript=Tools/megaplant_oak_import.py -unattended -nullrhi
# Env OAK_STEP: all (import then organise), import, or organise (materials, Nanite, wind and folders on what _Import holds).
import os, unreal

SRC = os.environ.get('OAK_SRC', 'C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary/Megaplants__English_Oak-83642c38/usd/tree_english_oak_forest__extracted/')
PROJ = os.path.abspath(unreal.SystemLibrary.get_project_directory()).replace("\\", "/") + "/"
WRAP = PROJ + 'Saved/OakImport/OakTrees.usda'
ROOT = '/Game/Megaplant_Library/Tree_EnglishOak'
RAW = ROOT + '/_Import'
EAL = unreal.EditorAssetLibrary

def log(*a): unreal.log('OAKIMP ' + ' '.join(str(x) for x in a))

def write_wrapper():
    s = '#usda 1.0\n(\n    metersPerUnit = 1\n    upAxis = "Y"\n)\n\n'
    for x in 'ABCD':
        n = 'Tree_English_Oak_Forest_01_' + x
        s += 'def Xform "%s" (\n    prepend references = @%s%s.usd@</%s>\n)\n{\n}\n\n' % (n, SRC, n, n)
    os.makedirs(os.path.dirname(WRAP), exist_ok=True)
    open(WRAP, 'w').write(s)

def do_import():
    write_wrapper()
    for d in (RAW, ROOT + '/Tree_EnglishOak_01', ROOT + '/Instances'):
        if EAL.does_directory_exist(d): EAL.delete_directory(d)
    o = unreal.UsdStageImportOptions()
    for k, v in [('import_actors', False), ('import_geometry', True), ('import_skeletal_animations', False),
                 ('import_level_sequences', False), ('import_materials', True), ('import_groom_assets', False),
                 ('import_sparse_volume_textures', False), ('import_sounds', False),
                 ('share_assets_for_identical_prims', True), ('prim_path_folder_structure', False),
                 ('existing_asset_policy', unreal.ReplaceAssetPolicy.REPLACE)]:
        try: o.set_editor_property(k, v)
        except Exception as e: log('option', k, 'ERR', e)
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', WRAP)
    t.set_editor_property('destination_path', RAW)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', False)
    t.set_editor_property('options', o)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    log('imported objects', len(t.get_editor_property('imported_object_paths') or []))
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log('saved')

def listing(path):
    for p in EAL.list_assets(path, recursive=True):
        a = unreal.load_asset(p)
        extra = ''
        if isinstance(a, unreal.SkeletalMesh):
            ns = a.get_editor_property('nanite_settings')
            extra = 'nanite=%s slots=%s bones=%s bounds=%s' % (ns.enabled, [str(m.material_slot_name) for m in a.get_editor_property('materials')],
                len(a.get_editor_property('skeleton').get_editor_property('bone_tree')) if a.get_editor_property('skeleton') else '?', a.get_bounds().box_extent)
        elif isinstance(a, unreal.StaticMesh):
            ns = a.get_editor_property('nanite_settings')
            extra = 'nanite=%s slots=%s bounds=%s' % (ns.enabled, [str(m.material_slot_name) for m in a.get_editor_property('static_materials')], a.get_bounds().box_extent)
        log('ASSET', p, type(a).__name__, extra)

HB = '/Game/Megaplant_Library/Tree_Hornbeam'
MAT = ROOT + '/Materials'
# Oak bark is warmer and browner than the hornbeam's smooth grey (USD's own flat colour for it is 0.142, 0.066, 0.031)
BARK_TINT = unreal.LinearColor(0.62, 0.50, 0.40, 1.0)

def make_materials():
    out = {}
    for kind in ('Bark', 'Foliage'):
        dst = '%s/MI_Tree_EnglishOak_01_%s' % (MAT, kind)
        if not EAL.does_asset_exist(dst):
            EAL.duplicate_asset('%s/Materials/MI_Tree_Hornbeam_01_%s' % (HB, kind), dst)
        mi = unreal.load_asset(dst)
        if kind == 'Bark':
            unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, 'BaseColor Tint', BARK_TINT)
            # The oak's bark UVs tile ("Tileable" in the USD) where the hornbeam's do not, so the branch mask read from
            # the hornbeam bark texture punched speckled holes through the oak trunks: no mask on the oak's bark
            unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(mi, 'mask out Branches', False)
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        out[kind] = mi
    return out

def slot_material(name, mats):
    n = str(name).lower()
    if 'twosided' in n or 'foliage' in n or 'leaf' in n: return mats['Foliage']
    return mats['Bark']

TREE_DIR = ROOT + '/Tree_EnglishOak_01'
INST_DIR = ROOT + '/Instances'

def aud(m, cls):
    return [d for d in (m.get_editor_property('asset_user_data') or []) if d.get_class().get_name() == cls]

NANITE_FIELDS = ('enabled', 'shape_preservation', 'voxel_ndf', 'voxel_opacity', 'lerp_u_vs', 'explicit_tangents', 'separable',
                 'keep_percent_triangles', 'trim_relative_error', 'generate_fallback', 'fallback_target', 'fallback_percent_triangles',
                 'fallback_relative_error', 'max_edge_length_factor', 'num_rays', 'voxel_level', 'ray_back_up')

def copy_nanite(a, ref, solid=False):
    """The Hornbeam's Nanite foliage settings, field by field: the whole struct also carries a Nanite assembly's
    part list, so copying it would swap the oak's branches for the hornbeam's. solid: the
    tree's assembly gets Preserve Area: Voxelize left see-through speckles on oak A's thin trunk at about 9 m and None lost every leaf at a distance (2026-11-15);
    the branch and leaf parts keep their own Voxelize."""
    mine, theirs = a.get_editor_property('nanite_settings'), ref.get_editor_property('nanite_settings')
    for f in NANITE_FIELDS:
        try: mine.set_editor_property(f, theirs.get_editor_property(f))
        except Exception as e: log('nanite field', f, 'ERR', e)
    if solid: mine.set_editor_property('shape_preservation', unreal.NaniteShapePreservation.PRESERVE_AREA)
    a.set_editor_property('nanite_settings', mine)

def solid_trunks():
    """OAK_STEP=trunks: only the trees' shape preservation (see copy_nanite)."""
    for x in 'ABCD':
        p = TREE_DIR + '/SK_EnglishOak_01_' + x
        a = unreal.load_asset(p)
        ns = a.get_editor_property('nanite_settings')
        if ns.get_editor_property('shape_preservation') != unreal.NaniteShapePreservation.PRESERVE_AREA:
            ns.set_editor_property('shape_preservation', unreal.NaniteShapePreservation.PRESERVE_AREA)
            a.set_editor_property('nanite_settings', ns)
            EAL.save_asset(p, only_if_is_dirty=False)
        log('TRUNK', x, a.get_editor_property('nanite_settings').get_editor_property('shape_preservation'))

def organise():
    """Materials, Nanite, wind and the Hornbeam's folder layout, on what the import step made in _Import."""
    mats = make_materials()
    hb_tree = unreal.load_asset(HB + '/Tree_Hornbeam_01/SK_Hornbeam_01_A')
    hb_part = unreal.load_asset(HB + '/Instances/SKM_Branch_Large_01')
    raw = RAW + '/OakTrees/SkeletalMeshes'
    imported_mats = [p for p in EAL.list_assets(RAW, recursive=True) if isinstance(unreal.load_asset(p), unreal.MaterialInterface)]
    moves = []
    for p in EAL.list_assets(raw, recursive=False):
        a = unreal.load_asset(p); n = a.get_name()
        if isinstance(a, unreal.SkeletalMesh):
            ms = []
            for m in a.get_editor_property('materials'):
                cur = m.get_editor_property('material_interface')
                mat = slot_material((cur.get_name() if cur else '') + str(m.material_slot_name), mats)
                ms.append(unreal.SkeletalMaterial(material_interface=mat, material_slot_name=m.material_slot_name))
            a.set_editor_property('materials', ms)
            a.set_editor_property('physics_asset', None)
            if n.endswith('_Geo'):
                continue                                   # the trunk the importer copied into each tree; deleted below
            if n.startswith('SK_Tree_English_Oak_Forest_01_'):
                x = n[-1]
                copy_nanite(a, hb_tree, solid=True)
                ok = unreal.JeebEditorTools.import_dynamic_wind_json(a, SRC + 'Tree_English_Oak_Forest_01_%s_DynamicWind.json' % x)
                wd = aud(a, 'DynamicWindSkeletalData')   # (its simulation groups are not open to Python: the JSON's defaults, influence 1 each)
                log('TREE', x, 'wind', ok, 'wind data', len(wd), [(str(m.material_slot_name), m.material_interface.get_name()) for m in a.get_editor_property('materials')])
                moves.append((p, '%s/SK_EnglishOak_01_%s' % (TREE_DIR, x)))
                moves.append((a.get_editor_property('skeleton').get_path_name().split('.')[0], '%s/SK_EnglishOak_01_%s_Skeleton' % (TREE_DIR, x)))
            else:
                copy_nanite(a, hb_part)
                log('PART', n, [str(m.material_slot_name) for m in ms])
                moves.append((p, '%s/SKM_%s' % (INST_DIR, n[3:])))
                moves.append((a.get_editor_property('skeleton').get_path_name().split('.')[0], '%s/SK_%s' % (INST_DIR, n[3:])))
            EAL.save_loaded_asset(a, False)
    for src, dst in moves:
        if EAL.does_asset_exist(dst): EAL.delete_asset(dst)
        log('MOVE', src.split('/')[-1], '->', dst, EAL.rename_asset(src, dst))
    # What is left in _Import: the trunk copies, physics assets, the importer's flat-colour materials
    for p in EAL.list_assets(RAW, recursive=True):
        refs = [r for r in EAL.find_package_referencers_for_asset(p, False) if not r.startswith(RAW)]
        log('LEFT', p, 'referenced by', refs[:3])
        if not refs: EAL.delete_asset(p)
    if not EAL.list_assets(RAW, recursive=True): EAL.delete_directory(RAW)
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)

step = os.environ.get('OAK_STEP', 'import')
if step in ('import', 'all'):
    do_import()
if step in ('organise', 'all'):
    organise()
if step == 'materials':
    make_materials()
    EAL.save_directory(MAT, only_if_is_dirty=False, recursive=True)
if step == 'trunks':
    solid_trunks()
listing(ROOT)
log('done')
