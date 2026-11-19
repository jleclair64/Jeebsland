# Opens the fort's gate (2026-11-18 review: the Meshy fort has a closed plank door, so nobody can walk in).
# Usage: blender -b -P Tools/fort_open_gate.py -- <in.glb> <out.glb>
# Works on Meshy's own model (x along the long side, the gate on -Y, 1 unit = 7.62 m before the game's 1.4x). Everything of the front
# wall between the two door posts (door leaves, straps, handles, the old low lintel and the log tips above it) is cut out; then it adds
# two tall timber gate posts, a double lintel at the palisade's top (the opening is 0.34 units = 3.6 m high and 3.2 m wide at the game's
# 1.4x, from the model's floor at z -0.31), and the two gate leaves swung back against the inside of the wall. New pieces take their
# colour from the old lintel's own picture patch, so no new texture is needed. BuildWarBuildings.ps1 runs this before building_clean.py.
import bpy, bmesh, sys, os
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, dst = a[0], a[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
me = ob.data
bm = bmesh.new(); bm.from_mesh(me)
uvl = bm.loops.layers.uv.active

GX0, GX1 = -0.125, 0.178          # the opening between the door posts (inner faces)
FRONT_Y = -0.28                    # the front wall lies at y < this
FLOOR = -0.31                      # the walking surface in model units
# 1. the colour patch of the old lintel (dark timber) in the picture
us = []
for f in bm.faces:
    c = f.calc_center_median()
    if c.y < -0.3 and -0.09 < c.z < -0.05 and -0.05 < c.x < 0.05 and uvl:
        for l in f.loops: us.append(l[uvl].uv.copy())
uv0 = sum(us, Vector((0, 0))) / max(1, len(us)) if us else Vector((0.5, 0.5))
print("FORTGATE uv sample", len(us), tuple(uv0))
# 2. split the front wall's geometry on the two opening planes so the cut is clean
for px in (GX0, GX1):
    geom = []
    for f in bm.faces:
        xs = [v.co.x for v in f.verts]
        if min(xs) < px < max(xs) and f.calc_center_median().y < FRONT_Y:
            geom.append(f); geom.extend(f.edges); geom.extend(f.verts)
    geom = list({id(g): g for g in geom}.values())
    if geom: bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-7, plane_co=(px, 0, 0), plane_no=(1, 0, 0), clear_inner=False, clear_outer=False)
bm.faces.ensure_lookup_table()
# 3. cut out everything of the front wall between the posts
gone = [f for f in bm.faces if f.calc_center_median().y < FRONT_Y and GX0 + 1e-5 < f.calc_center_median().x < GX1 - 1e-5]
print("FORTGATE cut faces", len(gone))
bmesh.ops.delete(bm, geom=gone, context='FACES')
loose = [v for v in bm.verts if not v.link_faces]
bmesh.ops.delete(bm, geom=loose, context='VERTS')
# 4. new timber
def box(x0, x1, y0, y1, z0, z1):
    vs = [bm.verts.new(Vector(p)) for p in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for q in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        f = bm.faces.new([vs[i] for i in q])
        for k, l in enumerate(f.loops):
            if uvl: l[uvl].uv = uv0 + Vector((0.002 * (k % 2), 0.002 * (k // 2)))
        f.smooth = False
box(-0.156, GX0 + 0.003, -0.385, -0.300, FLOOR - 0.02, 0.09)   # left post
box(GX1 - 0.003, 0.214, -0.385, -0.300, FLOOR - 0.02, 0.09)   # right post
box(-0.166, 0.224, -0.392, -0.296, 0.04, 0.07)                # lower lintel (underside 3.7 m above the floor at 1.4x)
box(-0.180, 0.238, -0.400, -0.290, 0.07, 0.095)               # upper lintel, a little proud
box(-0.158, -0.142, -0.300, -0.140, FLOOR, -0.09)             # gate leaf, swung back inside (left)
box(0.200, 0.216, -0.300, -0.140, FLOOR, -0.09)               # gate leaf (right)
bm.to_mesh(me); bm.free()
bpy.ops.object.select_all(action='DESELECT')
ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', use_selection=False)
print("FORTGATE wrote", dst)
