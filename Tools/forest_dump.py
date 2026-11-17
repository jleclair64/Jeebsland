# Lists what the Oak Hill forest (OakForest.cpp, 2026-11-16) pulls in from the tree packs: for each mesh it uses, its bounds, Nanite,
# LODs, vertices and screen sizes, collision and material slots; then every package those meshes (and /Game/TreeGroves' fall leaf
# materials) depend on, recursively, with sizes, flagging any that sits in a DirectoriesToNeverCook folder of Config/DefaultGame.ini
# (that would be missing in a package). Writes Saved/ForestDump.txt. Read-only: saves nothing. Run with the editor closed:
#   UnrealEditor-Cmd.exe Jeebsland.uproject -ExecutePythonScript=<path with forward slashes>/Tools/forest_dump.py -unattended -nullrhi
import unreal, os, re
out = []
def log(s): out.append(s)
MESHES = ['/Game/DZ_Assets/DZ_Trees/Meshes/Cork_Oak/SM_Cork_Oak_1', '/Game/DZ_Assets/DZ_Trees/Meshes/Aspen/SM_Columnar_Aspen_1']
for f in ['full/high/spruce_full_01', 'full/high/spruce_full_02', 'full/high/spruce_full_03', 'half/high/spruce_half_02', 'small/spruce_small_01']:
    MESHES.append('/Game/PN_interactiveSpruceForest/Meshes/' + f)
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for p in MESHES:
    a = unreal.load_asset(p)
    if not isinstance(a, unreal.StaticMesh):
        log('MISSING %s' % p); continue
    bb = a.get_bounding_box()
    nl = a.get_num_lods()
    verts = [sub.get_number_verts(a, l) for l in range(nl)]
    try: ss = [round(x, 3) for x in sub.get_lod_screen_sizes(a)]
    except Exception: ss = []
    log('SM %s size=%s nanite=%s lods=%d verts=%s screen=%s simplecoll=%s' % (p, bb.max - bb.min, a.get_editor_property('nanite_settings').enabled, nl, verts, ss, sub.get_simple_collision_count(a)))
    for i, m in enumerate(a.get_editor_property('static_materials')):
        log('  slot %d %s -> %s' % (i, m.material_slot_name, m.material_interface.get_path_name() if m.material_interface else None))
proj = unreal.SystemLibrary.get_project_directory()
never = re.findall(r'^\+DirectoriesToNeverCook=\(Path="([^"]+)"\)', open(os.path.join(proj, 'Config', 'DefaultGame.ini')).read(), re.M)
AR = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True, include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False)
seen = set()
todo = MESHES + ['/Game/TreeGroves/MI_Fall_AspenLeavesBig', '/Game/TreeGroves/MI_Fall_AspenLeavesSmall', '/Game/TreeGroves/MI_Fall_CorkOakLeaves']
while todo:
    p = todo.pop()
    if p in seen: continue
    seen.add(p)
    for d in (AR.get_dependencies(p, opts) or []):
        d = str(d)
        if d.startswith('/Game/') and d not in seen: todo.append(d)
tot = 0; bad = 0
for p in sorted(seen):
    fp = os.path.join(proj, 'Content', p[len('/Game/'):] + '.uasset')
    s = os.path.getsize(fp) if os.path.exists(fp) else 0
    u = fp[:-7] + '.ubulk'
    if os.path.exists(u): s += os.path.getsize(u)
    tot += s
    nc = [n for n in never if p.startswith(n + '/')]
    if nc: bad += 1
    log('DEP %8.1f MB %s%s' % (s / 1048576.0, p, ('   NEVER-COOKED by ' + nc[0]) if nc else ''))
log('DEP total %.1f MB in %d packages; %d in never-cooked folders' % (tot / 1048576.0, len(seen), bad))
open(os.path.join(proj, 'Saved', 'ForestDump.txt'), 'w').write('\n'.join(out))
unreal.log('FORESTDUMP done')
