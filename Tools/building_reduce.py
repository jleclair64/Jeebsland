# Reduces a Meshy building to a triangle budget, keeping its textures (2026-11-11). Run in Blender:
#   blender -b -P Tools/building_reduce.py -- <in.glb> <out.glb> [triangles, default 300000] [--pictures-from <done.glb>]
# Art/Buildings keeps <Name>_Detailed.glb (the full download, git-ignored) and <Name>_300k.glb (what the game uses).
# The budget: about 300k per building design, about 150k at the least for small ones (the test house frayed under that).
# Delete the _Detailed file once the reduced one is in the game and checked; the Meshy link in Docs/TownBuildings.md is enough to fetch it again.
#
# The model is welded before it is reduced (2026-11-13). A glTF is split into separate pieces along every texture seam, and
# reducing those pieces one by one pulled their edges apart: the first reductions were full of hairline cracks (30-60k open edges
# each; the well 127k and in 1,800 loose pieces) that showed the grass behind as green slivers. Welded, the house has 2 open edges.
# Its normals are then worked out again from the reduced shape: smooth, but sharp across creases over SHARP_DEG.
#
# --pictures-from: put the pictures of an already finished model (the palette repaint and lit windows of building_palette.py
# and building_glow.py) on the new reduction. Both come from the same Meshy model, so they share one texture layout.
import bpy, bmesh, sys, math
import numpy as np
a = sys.argv[sys.argv.index("--") + 1:]
pics_from = None
if "--pictures-from" in a:
    k = a.index("--pictures-from"); pics_from = a[k + 1]; del a[k:k + 2]
src, out = a[0], a[1]
budget = int(a[2]) if len(a) > 2 else 300000
WELD = 1.0e-5      # weld points closer than this share of the model's longest side (0.1 mm on a 12 m building)
SHARP_DEG = 50.0   # creases sharper than this keep a hard edge in the shading
bpy.ops.wm.read_factory_settings(use_empty=True)
if src.lower().endswith(".fbx"):
    bpy.ops.import_scene.fbx(filepath=src)        # a library model downloaded as FBX (its pictures sit beside it)
    for img in bpy.data.images:
        if img.source == "FILE" and not img.packed_file: img.pack()
else:
    bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
tris = lambda: sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
def open_edges(o):
    bm = bmesh.new(); bm.from_mesh(o.data); n = sum(1 for e in bm.edges if e.is_boundary); bm.free(); return n
before = tris()
for o in meshes:
    me = o.data
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=float((co.max(0) - co.min(0)).max()) * WELD)
    bm.to_mesh(me); bm.free()
    print("REDUCE welded", o.name, "open edges", open_edges(o))
if before > budget:
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        m = o.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"
        m.ratio = budget / before; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=m.name)
for o in meshes:
    me = o.data
    if me.has_custom_normals:
        bpy.context.view_layer.objects.active = o
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    bm = bmesh.new(); bm.from_mesh(me)
    for f in bm.faces: f.smooth = True
    lim = math.radians(SHARP_DEG)
    for e in bm.edges:
        e.smooth = not (e.is_manifold and e.calc_face_angle(0.0) > lim)
    bm.to_mesh(me); bm.free()
    print("REDUCE", o.name, "open edges after", open_edges(o))
print("REDUCE", src, before, "->", tris())
if pics_from:
    keep = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=pics_from)
    done = [o for o in bpy.data.objects if o not in keep and o.type == "MESH"]
    mat = done[0].data.materials[0]
    for o in meshes:
        o.data.materials.clear(); o.data.materials.append(mat)
    for o in [o for o in bpy.data.objects if o not in keep]: bpy.data.objects.remove(o, do_unlink=True)
    print("REDUCE pictures from", pics_from, "material", mat.name)
# pictures at most 4K: some library models carry 8K ones (the church: 45 MB instead of about 20)
for img in bpy.data.images:
    if img.users == 0: continue
    _ = len(img.pixels)     # a packed picture reports size 0 until it is loaded
    print("REDUCE picture", img.name, tuple(img.size), img.file_format, img.packed_file is not None)
    if img.packed_file and img.packed_file.size > 6e6:
        # a 4K picture stored at top JPEG quality (the church's run 16 MB each): mark it changed so the exporter encodes it again at quality 92
        px = np.empty(len(img.pixels), dtype=np.float32); img.pixels.foreach_get(px); img.pixels.foreach_set(px); img.update()
    if max(img.size) > 4096:
        img.scale(img.size[0] * 4096 // max(img.size), img.size[1] * 4096 // max(img.size))
        img.pack()          # the exporter writes the packed file, so pack the smaller one
# every picture as JPEG, like Meshy's own: a repainted colour picture would otherwise come back as a 24 MB PNG (buildings need no see-through parts)
for o in bpy.data.objects: o.select_set(o in meshes)
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_image_format="JPEG", export_jpeg_quality=92, use_selection=True)
print("REDUCE wrote", out)
