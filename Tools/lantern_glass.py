# Repaints a lantern's glass and cleans its frame in the cleaned library model (2026-11-17, owner: "why does the lantern look shattered?
# I'd expect it to light up in the middle and not be so spotty"). The Meshy picture painted the oil lantern's glass with dark crack lines
# and blotches, and the Glow slot's light is that picture (emissive = picture x jeeb.Lamp.Glow x jeeb.Lamp.Tint), so the lit glass read as
# broken shards; its frame was painted rusty and blotched.
#   - glass: every texel of the Glow slot's bright (glass) faces gets one clean warm colour, brightest at the flame's height and a little
#     darker and redder towards the top and bottom of the glass (the height of each texel comes from the mesh, through its UVs), so the
#     lit lantern glows evenly from its middle; its normal-map texels are made flat (no painted cracks catching the light)
#   - frame (everything else): the speckle and blotches are smoothed out (the bevels' highlights kept) and the colour pulled to a clean dark
#     steel, per Docs/ItemStyleGuide.md (clean, no rust, no dirt)
# Writes the result back into Art/Filler/Clean/<Name>.glb (so a later pipeline run keeps it) and the pictures as PNGs for Unreal
# (Tools/lantern_import.py puts them on /Game/TownDecor). Run it on the original GLB only (git checkout it first): it is not idempotent.
#   blender -b --factory-startup -P Tools/lantern_glass.py -- <Name> <out dir> [flame height m] [glow spread m]
#   (2026-11-17: LanternOil 0.19 0.07, its globe 0.13-0.28 m up; LanternBlack the defaults. Out dir Saved/LanternScratch/glass for Tools/LanternImport.ps1)
import bpy, os, sys, zlib, struct
import numpy as np

a = sys.argv[sys.argv.index("--") + 1:]
NAME, OUT = a[0], a[1]
FLAME = float(a[2]) if len(a) > 2 else 0.0    # the flame's height above the lantern's foot, m (0: 42% of the way up the glass)
SPREAD = float(a[3]) if len(a) > 3 else 0.0   # how far up and down the glow stays bright, m (0: 42% of the glass's height)
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PROJ, "Art", "Filler", "Clean", NAME + ".glb")
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
o = [x for x in bpy.data.objects if x.type == "MESH"][0]
me = o.data
me.calc_loop_triangles()

def tex_of(sock):
    stack = [sock]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n.image
            stack.extend(i for i in n.inputs)
    return None

def images_of(mat):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    emit = tex_of(pr.inputs["Emission Color"]) if "Emission Color" in pr.inputs else None
    return tex_of(pr.inputs["Base Color"]), tex_of(pr.inputs["Normal"]), emit

body_base, body_nrm, _ = images_of(me.materials[0])
glow_i = next(i for i, m in enumerate(me.materials) if m.name.startswith("Glow"))
glow_base, glow_nrm, glow_emit = images_of(me.materials[glow_i])

def read(img):
    W, H = img.size
    px = np.empty(W * H * 4, np.float32); img.pixels.foreach_get(px)
    return px.reshape(H, W, 4)

def png(arr, path):   # (8-bit RGBA, rows top-down; Blender's pixels run bottom-up)
    a8 = (np.clip(arr[::-1], 0, 1) * 255 + 0.5).astype(np.uint8)
    h, w = a8.shape[:2]
    raw = b"".join(b"\x00" + a8[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

base = read(body_base); H, W = base.shape[:2]
nrm = read(body_nrm) if body_nrm else None
uv = np.array([d.uv[:] for d in me.uv_layers.active.data])
co = np.array([v.co[:] for v in me.vertices])
lum = base[..., :3].mean(-1)

def tri_texels(t):   # the texels (ys, xs) inside a triangle and their barycentric weights
    P = np.array([uv[l] for l in t.loops]) * [W, H] - 0.5
    x0, y0 = np.floor(P.min(0)).astype(int); x1, y1 = np.ceil(P.max(0)).astype(int)
    x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W - 1), min(y1, H - 1)
    if x1 < x0 or y1 < y0: return None
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
    d = (P[1, 1] - P[2, 1]) * (P[0, 0] - P[2, 0]) + (P[2, 0] - P[1, 0]) * (P[0, 1] - P[2, 1])
    if abs(d) < 1e-9: return None
    l0 = ((P[1, 1] - P[2, 1]) * (xs - P[2, 0]) + (P[2, 0] - P[1, 0]) * (ys - P[2, 1])) / d
    l1 = ((P[2, 1] - P[0, 1]) * (xs - P[2, 0]) + (P[0, 0] - P[2, 0]) * (ys - P[2, 1])) / d
    l2 = 1 - l0 - l1
    inside = (l0 >= -0.02) & (l1 >= -0.02) & (l2 >= -0.02)
    if not inside.any(): return None
    return ys[inside], xs[inside], l0[inside], l1[inside], l2[inside]

# each face's brightness in the picture (the mean over its texels)
n_tex = np.zeros(len(me.polygons)); c_sum = np.zeros((len(me.polygons), 3))
for t in me.loop_triangles:
    r = tri_texels(t)
    if r is None: continue
    n_tex[t.polygon_index] += len(r[0]); c_sum[t.polygon_index] += base[r[0], r[1], :3].sum(0)
mc = c_sum / np.maximum(n_tex, 1)[:, None]
bright = mc.mean(1)
sat = (mc.max(1) - mc.min(1)) / np.maximum(mc.max(1), 1e-4)
is_glow = np.array([p.material_index == glow_i for p in me.polygons])
# UV islands (faces joined by an edge with the same ends and the same UVs; by position, as the GLB splits vertices per material)
par = list(range(len(me.polygons)))
def find(x):
    while par[x] != x: par[x] = par[par[x]]; x = par[x]
    return x
edges = {}
for p in me.polygons:
    li = list(p.loop_indices)
    for k in range(len(li)):
        a1, b1 = li[k], li[(k + 1) % len(li)]
        ka = (tuple(np.round(co[me.loops[a1].vertex_index], 5)), tuple(np.round(uv[a1], 5)))
        kb = (tuple(np.round(co[me.loops[b1].vertex_index], 5)), tuple(np.round(uv[b1], 5)))
        key = (min(ka, kb), max(ka, kb))
        if key in edges: par[find(p.index)] = find(edges[key])
        else: edges[key] = p.index
# glass: the faces painted like glass (bright and warm: the oil lantern's cream, the black lantern's gold; the frame's pale bevels are
# grey), and every face of a UV island that is mostly those, whichever slot Meshy's split put them in (it left some of the oil lantern's
# globe and half the black lantern's panes in the frame's slot, unlit, which read as dark shards and dead panes in the lit glass)
glass_face = (bright > 0.42) & ((sat > 0.2) | (bright > 0.6))
islands = {}
for i in range(len(me.polygons)): islands.setdefault(find(i), []).append(i)
# (and an island that is mostly frame is all frame: its few lit faces were bright patches of the metal, lit wedges on the cap)
moved = unlit = 0
seed = glass_face.copy()
for fl in islands.values():
    fl = np.array(fl)
    g = n_tex[fl][glass_face[fl]].sum() >= 0.5 * n_tex[fl].sum()
    glass_face[fl] = g
moved = int((glass_face & ~is_glow).sum()); unlit = int((~glass_face & is_glow).sum())
for p in me.polygons:
    p.material_index = glow_i if glass_face[p.index] else (0 if p.material_index == glow_i else p.material_index)
me.update()

# rasterise every triangle into UV space: who owns each texel, and (for glass) its height
owner = np.zeros((H, W), np.int8)          # 0 none, 1 frame, 2 glass
zmap = np.full((H, W), np.nan, np.float32)
for t in me.loop_triangles:
    r = tri_texels(t)
    if r is None: continue
    yy, xx, l0, l1, l2 = r
    if glass_face[t.polygon_index]:
        Z = np.array([co[v][2] for v in t.vertices])
        owner[yy, xx] = 2
        zmap[yy, xx] = l0 * Z[0] + l1 * Z[1] + l2 * Z[2]
    else:
        keep = owner[yy, xx] != 2
        owner[yy[keep], xx[keep]] = 1

# grow the glass 4 texels into the empty gutter round its islands (mip maps and texture filtering read there)
for _ in range(4):
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        src = np.roll(np.roll(owner, dy, 0), dx, 1) == 2
        zs = np.roll(np.roll(zmap, dy, 0), dx, 1)
        m = (owner == 0) & src
        owner[m] = 2; zmap[m] = zs[m]
glass = owner == 2
gz = zmap[glass]
z_lo, z_hi = np.percentile(gz, 3), np.percentile(gz, 97)
zf = FLAME if FLAME > 0 else z_lo + 0.42 * (z_hi - z_lo)
sig = SPREAD if SPREAD > 0 else 0.42 * (z_hi - z_lo)
t = np.nan_to_num(np.exp(-((zmap - zf) / sig) ** 2))
CENTRE = np.array([1.0, 0.95, 0.80])   # (sRGB as stored) warm cream at the flame: times the amber tint it lights a bright amber
EDGE = np.array([0.80, 0.60, 0.38])    # towards the top and bottom of the glass: darker, redder
col = EDGE + (CENTRE - EDGE) * t[..., None]

# the frame: blotches and speckle smoothed, bevel highlights kept, pulled to a clean dark steel
def blur(img, s):
    r = int(3 * s) + 1
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2); k /= k.sum()
    out = img.copy()
    for ax in (0, 1):
        out = sum(np.roll(out, i - r, ax) * k[i] for i in range(len(k)))
    return out
rgb = base[..., :3]
grey = rgb.mean(-1, keepdims=True)
rgb = grey + 0.35 * (rgb - grey)                      # (most of the rust's colour out)
fine, broad = blur(rgb, 1.0), blur(rgb, 6.0)
rgb = broad + 0.4 * (fine - broad)                    # (the blotches between fine edges and the broad shape mostly gone)
fm = (owner == 1)
mean = rgb[fm].mean(0) if fm.any() else rgb.reshape(-1, 3).mean(0)
STEEL = np.array([0.20, 0.21, 0.23])                  # (sRGB) clean dark steel with a cool touch
rgb = np.clip(STEEL * min(1.0, mean.mean() / STEEL.mean()) + 0.8 * (rgb - mean), 0.0, 1.0)   # (a frame already darker, the black lantern's, keeps its darkness)
rgb[glass] = col[glass]
base[..., :3] = rgb
if nrm is not None:
    nrm[glass, 0] = 0.5; nrm[glass, 1] = 0.5; nrm[glass, 2] = 1.0

def put(img, arr):
    img.pixels.foreach_set(arr.reshape(-1).astype(np.float32)); img.update(); img.pack()

put(body_base, base); png(base, os.path.join(OUT, "T_%s_Base.png" % NAME))
if nrm is not None: put(body_nrm, nrm); png(nrm, os.path.join(OUT, "T_%s_Normal.png" % NAME))
for img, arr in ((glow_base, base), (glow_nrm, nrm), (glow_emit, base)):
    if img is not None and arr is not None and img not in (body_base, body_nrm): put(img, arr)
# a check map: frame grey, glass by height (red low, green high)
dbg = np.zeros((H, W, 4), np.float32); dbg[..., 3] = 1
dbg[owner == 1, :3] = 0.3
dbg[glass, 0] = 1 - (zmap[glass] - z_lo) / (z_hi - z_lo); dbg[glass, 1] = (zmap[glass] - z_lo) / (z_hi - z_lo)
png(dbg, os.path.join(OUT, "%s_uvmap.png" % NAME))

bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.gltf(filepath=SRC, export_format="GLB", use_selection=True, export_image_format="JPEG", export_jpeg_quality=90,
                          export_yup=True, export_apply=True, export_normals=True, export_tangents=False, export_materials="EXPORT")
print("LANTERNGLASS %s glass faces %d of %d (%d moved into the Glow slot, %d out of it), glass texels %d, glass z %.3f-%.3f m, flame at %.3f m" % (
    NAME, glass_face.sum(), len(me.polygons), moved, unlit, glass.sum(), z_lo, z_hi, zf))
