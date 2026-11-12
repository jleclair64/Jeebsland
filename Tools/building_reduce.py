# Reduces a Meshy building to a triangle budget, keeping its textures (2026-11-11). Run in Blender:
#   blender -b -P Tools/building_reduce.py -- <in.glb> <out.glb> [triangles, default 300000]
# Art/Buildings keeps <Name>_Detailed.glb (the full download, git-ignored) and <Name>_300k.glb (what the game uses).
# The budget: about 300k per building design, about 150k at the least for small ones (the test house frayed under that).
# Delete the _Detailed file once the reduced one is in the game and checked; the Meshy link in Docs/TownBuildings.md is enough to fetch it again.
import bpy, sys
import numpy as np
a =sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
budget = int(a[2]) if len(a) > 2 else 300000
bpy.ops.wm.read_factory_settings(use_empty=True)
if src.lower().endswith(".fbx"):
    bpy.ops.import_scene.fbx(filepath=src)        # a library model downloaded as FBX (its pictures sit beside it)
    for img in bpy.data.images:
        if img.source == "FILE" and not img.packed_file: img.pack()
else:
    bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
tris = lambda: sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
before = tris()
if before > budget:
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        m = o.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"
        m.ratio = budget / before; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=m.name)
print("REDUCE", src, before, "->", tris())
# pictures at most 4K: some library models carry 8K ones (the church: 45 MB instead of about 20)
for img in bpy.data.images:
    _ = len(img.pixels)     # a packed picture reports size 0 until it is loaded
    print("REDUCE picture", img.name, tuple(img.size), img.file_format, img.packed_file is not None)
    if img.packed_file and img.packed_file.size > 6e6:
        # a 4K picture stored at top JPEG quality (the church's run 16 MB each): mark it changed so the exporter encodes it again at quality 92
        px = np.empty(len(img.pixels), dtype=np.float32); img.pixels.foreach_get(px); img.pixels.foreach_set(px); img.update()
    if max(img.size) > 4096:
        img.scale(img.size[0] * 4096 // max(img.size), img.size[1] * 4096 // max(img.size))
        img.pack()          # the exporter writes the packed file, so pack the smaller one
# every picture as JPEG, like Meshy's own: a repainted colour picture would otherwise come back as a 24 MB PNG (buildings need no see-through parts)
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_image_format="JPEG", export_jpeg_quality=92)
print("REDUCE wrote", out)
