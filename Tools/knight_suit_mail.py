# What is left of the chain mail under the new suit (2026-11-05). Used by knight_suit_export.py inside Blender.
# Owner: chain mail never shows through the armour, as though it's covered. So everything under plate is deleted, not set deeper (it follows different
# weights and would come back through when he moves). Since the arm harness covers the whole arm (its upper tube goes up inside the pauldron's plates), only one part stays:
#  - a dark pelvis (the second material), so there's no hole through him under the skirt, narrow enough that the thighs' plates don't swing through it.
import bpy, bmesh
from mathutils import Vector

PELVIS_X, PELVIS_Y = 0.095, 0.85   # (m, share) the dark pelvis no wider than this, near the hip joints, where a thigh plate doesn't sweep through it; and its depth
PELVIS_EDGE_STEPS = 12   # smooths its saw-toothed edge


def trim_mail(arm, body, plates, trees):
    tree = {p.name: t for p, t in zip(plates, trees)}
    wb = body.matrix_world; wbi = wb.inverted()
    names = {g.index: g.name.replace("mixamorig:", "") for g in body.vertex_groups}

    # ---- which faces stay
    pelvis = []
    for v in body.data.vertices:
        co = wb @ v.co
        ws = {names[g.group]: g.weight for g in v.groups}
        n_ = (wb.to_3x3() @ v.normal).normalized()
        pelvis.append(ws.get("Hips", 0.0) > 0.3 and co.z < 1.02 and tree["legs"].ray_cast(co, -n_, 0.06)[0] is None)   # (a point already outside the leg plates goes)
    bm = bmesh.new(); bm.from_mesh(body.data); bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    under = bpy.data.materials.new("Suit_Under"); under.diffuse_color = (0.02, 0.02, 0.02, 1.0); body.data.materials.append(under)
    gone = []
    for f in bm.faces:
        idx = [v.index for v in f.verts]
        if sum(pelvis[i] for i in idx) * 2 > len(idx): f.material_index = 1
        else: gone.append(f)
    n_before = len(bm.faces)
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    # (the model's points are split along its texture's seams: weld them for the shape; the texture's own seams stay)
    bmesh.ops.remove_doubles(bm, verts=[v for v in bm.verts if v.link_faces], dist=2e-5 / wb.to_scale()[0])
    for v in [v for v in bm.verts if not v.link_faces]: bm.verts.remove(v)

    # ---- the pelvis: narrower, with a clean edge
    pelvis_v = [v for v in bm.verts if any(f.material_index == 1 for f in v.link_faces)]
    ys = [(wb @ v.co).y for v in pelvis_v]; y_mid = (max(ys) + min(ys)) / 2
    for v in pelvis_v:
        co = wb @ v.co
        co.x = max(-PELVIS_X, min(PELVIS_X, co.x)); co.y = y_mid + (co.y - y_mid) * PELVIS_Y
        v.co = wbi @ co
    for _ in range(PELVIS_EDGE_STEPS):
        moved = []
        for v in pelvis_v:
            if v.is_boundary:
                ends = [e.other_vert(v) for e in v.link_edges if e.is_boundary]
                if len(ends) == 2: moved.append((v, v.co.lerp((ends[0].co + ends[1].co) / 2, 0.6)))
            elif _ < 4 and v.link_edges:
                moved.append((v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5)))
        for v, c in moved: v.co = c
    pts = [wb @ v.co for v in pelvis_v]
    print("SUIT pelvis: %d points, x %.3f..%.3f, y %.3f..%.3f, z %.3f..%.3f" % (len(pts), min(p.x for p in pts), max(p.x for p in pts), min(p.y for p in pts), max(p.y for p in pts), min(p.z for p in pts), max(p.z for p in pts)))

    me = body.data
    bm.to_mesh(me); bm.free(); me.update()
    if "custom_normal" in me.attributes: me.attributes.remove(me.attributes["custom_normal"])   # (the model's own shading would no longer fit this shape)
    me.shade_smooth(); me.update()
    print("SUIT mail: %d faces hidden, %d left" % (n_before - len(me.polygons), len(me.polygons)))
