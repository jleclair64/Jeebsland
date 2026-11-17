# Lists every texture that would be cooked (all of /Game but the DirectoriesToNeverCook folders of Config/DefaultGame.ini) with its
# LOD group, size, compression and file size, totals per top folder, and what capping the tree and grass packs' groups at 2048 would
# drop. Writes Saved/TextureSurvey.txt. Read-only: saves nothing. Run with the editor closed (2026-11-16):
#   UnrealEditor-Cmd.exe Jeebsland.uproject -run=pythonscript -script=<path with forward slashes>/Tools/texture_survey.py -unattended -nullrhi
import unreal, os, re
out = []
def log(s): out.append(s)
proj = unreal.SystemLibrary.get_project_directory()
never = re.findall(r'^\+DirectoriesToNeverCook=\(Path="([^"]+)"\)', open(os.path.join(proj, 'Config', 'DefaultGame.ini')).read(), re.M)
AR = unreal.AssetRegistryHelpers.get_asset_registry()
flt = unreal.ARFilter(package_paths=['/Game'], recursive_paths=True, class_paths=[unreal.TopLevelAssetPath('/Script/Engine', 'Texture2D')])
assets = AR.get_assets(flt)
PACKS = ('DZ_Assets', 'PN_')
# bytes a pixel of the top mip in the cooked format (BC1 0.5, BC4 0.5, BC5/BC7/BC3 1, uncompressed RGBA8 4, HDR 8)
BPP = {'TC_DEFAULT': 0.75, 'TC_NORMALMAP': 1.0, 'TC_MASKS': 1.0, 'TC_GRAYSCALE': 1.0, 'TC_ALPHA': 0.5, 'TC_BC7': 1.0, 'TC_HDR': 8.0, 'TC_VECTOR_DISPLACEMENTMAP': 4.0, 'TC_EDITOR_ICON': 4.0}
rows = []
for a in assets:
    p = str(a.package_name)
    if any(p == n or p.startswith(n + '/') for n in never): continue
    t = a.get_asset()
    if t is None: continue
    try: sx, sy = t.blueprint_get_size_x(), t.blueprint_get_size_y()
    except Exception: sx = sy = 0
    try: imp = t.get_editor_property('imported_size'); ix, iy = imp.x, imp.y
    except Exception: ix, iy = sx, sy
    grp = str(t.get_editor_property('lod_group')).split('.')[-1]
    comp = str(t.get_editor_property('compression_settings')).split('.')[-1]
    try: mx = t.get_editor_property('max_texture_size')
    except Exception: mx = 0
    lb = t.get_editor_property('lod_bias')
    try: noalpha = t.get_editor_property('compression_no_alpha')
    except Exception: noalpha = False
    top = p[len('/Game/'):].split('/')[0] if '/' in p[len('/Game/'):] else '(root)'
    rows.append((p, top, grp, comp, ix, iy, mx, lb, noalpha))
def cooked_bytes(ix, iy, comp, noalpha, cap):
    w, h = ix, iy
    while max(w, h) > cap: w, h = max(1, w // 2), max(1, h // 2)
    bpp = BPP.get(comp, 1.0)
    if comp == 'TC_DEFAULT': bpp = 0.5 if noalpha else 1.0
    return w * h * bpp * 4.0 / 3.0
tot_now = {}; tot_cap = {}; big = {}
for (p, top, grp, comp, ix, iy, mx, lb, na) in rows:
    eff = max(ix, iy)
    if mx and mx < eff: eff = mx
    now = cooked_bytes(ix, iy, comp, na, eff)
    cap = cooked_bytes(ix, iy, comp, na, min(eff, 2048)) if (top.startswith(PACKS) or grp in ('TEXTUREGROUP_World', 'TEXTUREGROUP_WorldNormalMap', 'TEXTUREGROUP_WorldSpecular')) else now
    tot_now[top] = tot_now.get(top, 0) + now; tot_cap[top] = tot_cap.get(top, 0) + cap
    key = (top, grp)
    b = big.setdefault(key, [0, 0, 0])
    b[0] += 1
    if max(ix, iy) > 2048: b[1] += 1
    b[2] = max(b[2], max(ix, iy))
    if max(ix, iy) > 2048: log('BIG %s %s %s %dx%d max=%d bias=%d' % (p, grp, comp, ix, iy, mx, lb))
log('--- per folder and group: textures, over 2048, largest')
for k in sorted(big): log('GROUP %-28s %-32s n=%d over2048=%d largest=%d' % (k[0], k[1], big[k][0], big[k][1], big[k][2]))
log('--- per folder: estimated cooked MB now -> with every World group texture capped at 2048')
for k in sorted(tot_now): log('SIZE %-28s %8.1f -> %8.1f' % (k, tot_now[k] / 1048576.0, tot_cap[k] / 1048576.0))
log('TOTAL %.1f -> %.1f MB (before pak compression)' % (sum(tot_now.values()) / 1048576.0, sum(tot_cap.values()) / 1048576.0))
# the road material: what it is and what it costs
m = unreal.load_asset('/Game/M_Dirt')
if m:
    for prop in ['blend_mode', 'shading_model', 'two_sided', 'opacity_mask_clip_value', 'used_with_instanced_static_meshes', 'used_with_nanite']:
        try: log('M_Dirt %s = %s' % (prop, m.get_editor_property(prop)))
        except Exception as e: log('M_Dirt %s ? %s' % (prop, e))
    try:
        st = unreal.MaterialEditingLibrary.get_statistics(m)
        log('M_Dirt pixel %d vertex %d samplers %d' % (st.num_pixel_shader_instructions, st.num_vertex_shader_instructions, st.num_samplers))
    except Exception as e: log('M_Dirt stats ? %s' % e)
    for t in unreal.MaterialEditingLibrary.get_used_textures(m): log('M_Dirt uses %s' % t.get_path_name())
mpc = unreal.load_asset('/Game/MPC_JeebPalette')
if mpc:
    try:
        for v in mpc.get_editor_property('vector_parameters'): log('MPC %s = %s' % (v.get_editor_property('parameter_name'), v.get_editor_property('default_value')))
    except Exception as e: log('MPC ? %s' % e)
open(os.path.join(proj, 'Saved', 'TextureSurvey.txt'), 'w').write('\n'.join(out))
unreal.log('TEXSURVEY done %d textures' % len(rows))
