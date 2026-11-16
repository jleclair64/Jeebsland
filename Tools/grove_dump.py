# Lists the tree packs' meshes (bounds, materials, their parameters) and what their showroom maps place, into Saved/GroveDump.txt (2026-11-15, for the tree groves).
import unreal
mel = unreal.MaterialEditingLibrary
out = []
def log(s): out.append(s)
EAL = unreal.EditorAssetLibrary
def dump_mat(m, seen):
    if m is None: return
    p = m.get_path_name()
    if p in seen: return
    seen.add(p)
    log("  MAT %s %s" % (p, type(m).__name__))
    if isinstance(m, unreal.MaterialInstanceConstant):
        log("    parent %s" % (m.get_editor_property("parent").get_path_name() if m.get_editor_property("parent") else None))
        for n in mel.get_scalar_parameter_names(m): log("    scalar %s = %s" % (n, mel.get_material_instance_scalar_parameter_value(m, n)))
        for n in mel.get_vector_parameter_names(m): log("    vector %s = %s" % (n, mel.get_material_instance_vector_parameter_value(m, n)))
        for n in mel.get_static_switch_parameter_names(m): log("    switch %s = %s" % (n, mel.get_material_instance_static_switch_parameter_value(m, n)))
ROOTS = ['/Game/PN_interactiveSpruceForest/Meshes', '/Game/DZ_Assets/DZ_Trees/Meshes', '/Game/Megaplant_Library/Tree_EnglishOak/Tree_EnglishOak_01', '/Game/Megaplant_Library/Tree_Hornbeam/Tree_Hornbeam_01']
seen = set()
for r in ROOTS:
    for p in sorted(EAL.list_assets(r, recursive=True)):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            b = a.get_bounds()
            ns = a.get_editor_property('nanite_settings')
            log("SM %s origin=%s extent=%s nanite=%s lods=%s" % (p, b.origin, b.box_extent, ns.enabled, a.get_num_lods()))
            for i, m in enumerate(a.get_editor_property('static_materials')):
                log("  slot %d %s -> %s" % (i, m.material_slot_name, m.material_interface.get_path_name() if m.material_interface else None))
                dump_mat(m.material_interface, seen)
        elif isinstance(a, unreal.SkeletalMesh):
            b = a.get_bounds()
            ns = a.get_editor_property('nanite_settings')
            log("SK %s origin=%s extent=%s nanite=%s" % (p, b.origin, b.box_extent, ns.enabled))
            for i, m in enumerate(a.get_editor_property('materials')):
                log("  slot %d %s -> %s" % (i, m.material_slot_name, m.material_interface.get_path_name() if m.material_interface else None))
                dump_mat(m.material_interface, seen)
for mp in ['/Game/PN_interactiveSpruceForest/Map/Showroom', '/Game/DZ_Assets/DZ_Trees/Maps/DZ_Sample_Trees_Overview', '/Game/PN_OpenWorldFoliage/Maps/Showroom']:
    try:
        w = unreal.EditorLoadingAndSavingUtils.load_map(mp)
        log("=== MAP %s" % mp)
        counts = {}
        for a in unreal.EditorLevelLibrary.get_all_level_actors():
            comps = a.get_components_by_class(unreal.StaticMeshComponent)
            for c in comps:
                m = c.get_editor_property('static_mesh')
                if m:
                    k = m.get_path_name()
                    counts[k] = counts.get(k, 0) + 1
            for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                m = c.get_editor_property('static_mesh')
                if m: counts[m.get_path_name() + ' (ISM x%d)' % c.get_instance_count()] = 1
        for k in sorted(counts): log("  %s x%d" % (k, counts[k]))
    except Exception as e:
        log("MAP %s ERR %s" % (mp, e))
import os
d = os.path.join(unreal.SystemLibrary.get_project_directory(), 'Saved')
open(os.path.join(d, 'GroveDump.txt'), 'w').write('\n'.join(out))
unreal.log("GROVEDUMP done %d lines" % len(out))
