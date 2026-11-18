# Blender (background): a skin mask for one townsperson (2026-11-18, the trolls), from its Meshy rig's own picture: light warm skin (hue
# 0-40, saturation 0.15-0.85, brightness over VMIN), not the blue-green garment, not leather or hair (darker). Writes T_Smith_Skin.png (the
# mask) and smith_preview.png (the skin turned troll green, to check by eye) to <out>, and prints the skin's mean linear brightness
# (SkinLum, set on MI_Smith). Args after "--": <rigged .glb> <out dir> [VMIN; 0.4 used]. Run it from PowerShell (Git Bash's sandbox
# drops Blender's file writes). Tools/fixlook_assets.py imports the copy in Art/People/Smith/T_Smith_Skin.png.
import bpy, sys, os
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
RIG, OUT = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=RIG)
img = None
for o in bpy.data.objects:
    if o.type != 'MESH': continue
    for m in o.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and img is None: img = n.image
w, h = img.size
px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
rgb = px[..., :3]
mx = rgb.max(-1); mn = rgb.min(-1); sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0)
r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
d = np.maximum(mx - mn, 1e-5)
hue = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60.0
VMIN = float(argv[2]) if len(argv) > 2 else 0.5
key = ((hue > 0) & (hue < 40) & (sat > 0.15) & (sat < 0.85) & (mx > VMIN)).astype(np.float32)
def grow(a, op, n=6):
    for _ in range(n):
        bb = a.copy()
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            bb = op(bb, np.roll(np.roll(a, dy, 0), dx, 1))
        a = bb
    return a
key = grow(grow(key, np.minimum, 2), np.maximum, 2)
key = grow(grow(key, np.maximum, 4), np.minimum, 4)
for _ in range(2):
    k = np.zeros_like(key)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            k += np.roll(np.roll(key, dy, 0), dx, 1)
    key = k / 9.0
sel = key > 0.5
lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4) @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
print("SMITHSKIN share %.3f SkinLum %.4f (linear)" % (float(sel.mean()), float(lin[sel].mean())))
def save(arr, name):
    im = bpy.data.images.new(name, w, h, alpha=False); im.pixels = arr.ravel()
    im.filepath_raw = os.path.join(OUT, name + ".png"); im.file_format = 'PNG'; im.save()
mask = np.zeros((h, w, 4), dtype=np.float32); mask[..., 0] = key; mask[..., 1] = key; mask[..., 2] = key; mask[..., 3] = 1.0
save(mask, "T_Smith_Skin")
prev = px.copy()
troll = np.array([0.42, 0.52, 0.36], dtype=np.float32)
l = (rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32))[..., None]
prev[..., :3] = rgb * (1 - key[..., None]) + (l * troll / 0.6) * key[..., None]
prev[..., 3] = 1.0
save(prev, "smith_preview")
save(px, "smith_base")
