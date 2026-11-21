# The walk-in house's shell (2026-11-21, owner: House_CLS1 behind the church; Docs/TownLab/InteriorPlan.md).
# Usage: blender -b -P Tools/interior_house_open.py -- <TL_House.fbx> <SM_TL_House_Col.fbx> <out folder> [picture folder]
# In: the house's game model as the TownLab bake wrote it (with window_inserts.py's glass, metres, Blender's frame: the door on -Y)
# and its solid copy as Unreal holds it (Tools/interior_export.py). Out: SM_TL_HouseOpen.fbx and SM_TL_HouseOpen_Col.fbx.
# The painted door is cut out of both (the same bisect-and-delete as fort_open_gate.py / window_inserts.py): every face of the
# front wall between the door posts, from the threshold to the lintel. A lining (two jambs and a head) runs from the outer face back
# to the room's inner wall, coloured from the door frame's own picture patch and marked "not glass" in the vertex colour, so the
# shell stays one mesh and one material. The room inside (floor, walls, beams) is AHouseInterior's (Content/Data/InteriorHouse.csv).
import bpy, bmesh, sys, os
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, colsrc, out = a[0], a[1], a[2]
pics = a[3] if len(a) > 3 else ""
os.makedirs(out, exist_ok=True)

# The opening, model metres (the game draws the house at 0.96): between the painted door's posts, threshold to just under the hood
DX0, DX1 = -0.44, 1.00
DZ0, DZ1 = -0.02, 2.25
FRONT = -2.45          # faces in front of this (y below it) belong to the front wall and its porch
OUTER, INNER = -3.10, -2.38   # the lining runs from the porch's face back through the room's front wall (its room face is at y -2.40, 230 cm in the game)
JAMB, HEAD = 0.07, 0.10

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)   # (the solid copy comes in cm under a 0.01 scale)
    return ob

def cut(ob, tag):
    me = ob.data
    bm = bmesh.new(); bm.from_mesh(me)
    def region(f):
        c = f.calc_center_median()
        return c.y < FRONT
    for co, no in (((DX0, 0, 0), (1, 0, 0)), ((DX1, 0, 0), (1, 0, 0)), ((0, 0, DZ1), (0, 0, 1)), ((0, 0, DZ0), (0, 0, 1))):
        ax = 0 if no[0] else 2
        geom = []
        for f in bm.faces:
            vs = [v.co[ax] for v in f.verts]
            if min(vs) < co[ax] < max(vs) and region(f): geom.append(f); geom.extend(f.edges); geom.extend(f.verts)
        geom = list({id(g): g for g in geom}.values())
        if geom: bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=co, plane_no=no, clear_inner=False, clear_outer=False)
    gone = []
    for f in bm.faces:
        c = f.calc_center_median()
        if c.y < FRONT and c.y > OUTER - 0.15 and DX0 + 1e-4 < c.x < DX1 - 1e-4 and DZ0 + 1e-4 < c.z < DZ1 - 1e-4: gone.append(f)
    print("HOUSEOPEN", tag, "faces cut", len(gone))
    bmesh.ops.delete(bm, geom=gone, context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    return bm

def box(bm, x0, x1, y0, y1, z0, z1, uv=None, uvl=None, col=None):
    vs = [bm.verts.new(Vector(p)) for p in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    fs = []
    for q in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        f = bm.faces.new([vs[i] for i in q])
        for k, l in enumerate(f.loops):
            if uvl and uv is not None: l[uvl].uv = uv + Vector((0.002 * (k % 2), 0.002 * (k // 2)))
            if col is not None: l[col] = (0.0, 0.0, 1.0, 1.0)
        f.smooth = False
        fs.append(f)
    return fs

# --- the drawn shell
ob = load(src)
me = ob.data
bm0 = bmesh.new(); bm0.from_mesh(me); uvl0 = bm0.loops.layers.uv.active
# the door frame's colour patch: the frame posts beside the old door (wood), at waist height
us = []
for f in bm0.faces:
    c = f.calc_center_median()
    if c.y < -2.95 and 0.6 < c.z < 1.6 and (DX0 - 0.12 < c.x < DX0 + 0.02 or DX1 - 0.02 < c.x < DX1 + 0.12) and uvl0:
        for l in f.loops: us.append(l[uvl0].uv.copy())
uv0 = sum(us, Vector((0, 0))) / max(1, len(us)) if us else Vector((0.5, 0.5))
print("HOUSEOPEN frame colour from", len(us), "corners, uv", tuple(round(x, 3) for x in uv0))
bm0.free()
bm = cut(ob, "shell")
uvl = bm.loops.layers.uv.active
col = bm.loops.layers.float_color.active or bm.loops.layers.color.active
n0 = len(bm.faces)
box(bm, DX0 - JAMB, DX0, OUTER, INNER, DZ0 - 0.02, DZ1 + HEAD, uv0, uvl, col)   # left jamb
box(bm, DX1, DX1 + JAMB, OUTER, INNER, DZ0 - 0.02, DZ1 + HEAD, uv0, uvl, col)   # right jamb
box(bm, DX0 - JAMB, DX1 + JAMB, OUTER, INNER, DZ1, DZ1 + HEAD, uv0, uvl, col)   # head
box(bm, DX0, DX1, OUTER, INNER, DZ0 - 0.06, DZ0, uv0, uvl, col)                 # threshold board
print("HOUSEOPEN lining faces", len(bm.faces) - n0, "colour layer", bool(col))
bm.to_mesh(me); bm.free(); me.update()
ob.name = "SM_TL_HouseOpen"
bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(out, "SM_TL_HouseOpen.fbx"), use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE",
                         global_scale=1.0, apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
                         use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False, colors_type="LINEAR")
print("HOUSEOPEN shell triangles", sum(len(p.vertices) - 2 for p in me.polygons))
if pics:
    sc = bpy.context.scene
    import math
    for nm, loc, rot, sc_ in (("open_front", (0.25, -12, 1.2), (math.radians(90), 0, 0), 4.0), ("open_in", (0.25, 0.5, 1.2), (math.radians(90), 0, math.radians(180)), 4.0)):
        cam = bpy.data.cameras.new(nm); cam.type = 'ORTHO'; cam.ortho_scale = sc_
        co = bpy.data.objects.new(nm, cam); sc.collection.objects.link(co); co.location = loc; co.rotation_euler = rot
        sc.camera = co
        sc.render.engine = 'BLENDER_WORKBENCH'; sc.render.resolution_x = 700; sc.render.resolution_y = 700
        sc.render.filepath = os.path.join(pics, nm + ".png")
        bpy.ops.render.render(write_still=True)

# --- the solid copy (what the server walks him against): the same cut, no lining (the room's walls and AHouseInterior's boxes do that)
ob = load(colsrc)
bm = cut(ob, "solid")
# the jambs stay solid so he can't slip into the gap between the shell and the room's wall
box(bm, DX0 - JAMB, DX0, OUTER, INNER, DZ0 - 0.02, DZ1 + HEAD)
box(bm, DX1, DX1 + JAMB, OUTER, INNER, DZ0 - 0.02, DZ1 + HEAD)
box(bm, DX0 - JAMB, DX1 + JAMB, OUTER, INNER, DZ1, DZ1 + HEAD)
bm.to_mesh(ob.data); bm.free()
ob.name = "SM_TL_HouseOpen_Col"
bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(out, "SM_TL_HouseOpen_Col.fbx"), use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE",
                         global_scale=1.0, apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE",
                         use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO", embed_textures=False, colors_type="NONE")
print("HOUSEOPEN solid triangles", sum(len(p.vertices) - 2 for p in ob.data.polygons))
