# Cleans Meshy's split armour (Art\Knight Split) and fits it to the rigged chainmail knight, for the MoveLab armour test (2026-11-05).
# Run through Blender: blender -b -P knight_split_clean.py -- <project> <output folder>
#   1. scales the armour (Meshy delivers it 1/15 size) onto the knight in his rest pose
#   2. deletes what is not armour: the black cloth at the elbows, hips, knees and neck, found from the model's own pictures
#      (not metal and not coloured). It keeps the belt (coloured), the glove hands and the visor's dark slits.
#   3. brings each piece down to its triangle budget, smooths the cut edges and gives them a small inward rim
#   4. copies the skeleton's weights from the chainmail, limited to the bones each piece may follow
#   5. sorts the chainmail's faces by which piece covers them, so a worn piece hides what is under it and it can never poke through
#   6. writes SK_Split_Body.fbx, SK_Split_Plate.fbx (a material per slot), the clips, and the pictures
import bpy, bmesh, sys, os, json
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index("--") + 1:]
proj, out = a[0], a[1]
os.makedirs(out, exist_ok=True)
src = os.path.join(proj, "Art", "Knight Split")
rig_path = [os.path.join(src, f) for f in os.listdir(src) if "rigged" in f and f.endswith(".glb")][0]
arm_path = [os.path.join(src, f) for f in os.listdir(src) if "split" in f and f.endswith(".glb")][0]

# ------- which Meshy part is which slot (fixed by the file; checked by its position in the log below)
SLOT_OF = {"model_part0": "Gloves", "model_part4": "Gloves", "model_part1": "Shoulders", "model_part3": "Shoulders",
           "model_part2": "Helm", "model_part5": "Chest", "model_part6": "Legs"}
BUDGET = {"Helm": 3500, "Chest": 9000, "Shoulders": 5500, "Gloves": 5000, "Legs": 9000}
BODY_BUDGET = 20000
# (metres, knight scale) cloth patches bigger than this go even if they touch no cut edge; smaller ones are shadows between plates
BIG_CLOTH_AREA = 0.0035
SPECK_AREA = 0.0006      # a loose scrap of armour smaller than this is a leftover
RIM = 0.007              # how far the cut edges turn inward
COVER_REACH = 0.09       # how far above the chainmail a plate may be and still count as covering it
BACK_REACH = 0.06        # how far the chainmail may stick out through a plate and still count as under it
ALLOWED = {
    "Helm": ["Head", "Neck", "HeadTop_End"],
    "Chest": ["Hips", "Spine", "Spine1", "Spine2", "Neck", "LeftShoulder", "RightShoulder", "LeftUpLeg", "RightUpLeg"],
    "Shoulders": ["Spine2", "LeftShoulder", "RightShoulder", "LeftArm", "RightArm"],
    "Gloves": ["LeftForeArm", "RightForeArm", "LeftHand", "RightHand", "LeftArm", "RightArm"],
    "Legs": ["Hips", "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg", "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase"],
}

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig_path)
for o in list(bpy.data.objects):
    if o.name.startswith("Icosphere"): bpy.data.objects.remove(o)
armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")
clips = [x.name for x in bpy.data.actions]
armature.data.pose_position = "REST"
if armature.animation_data: armature.animation_data.action = None
bpy.context.view_layer.update()
body = next(o for o in bpy.data.objects if o.type == "MESH")

def evaluated_points(o):
    dg = bpy.context.evaluated_depsgraph_get()
    e = o.evaluated_get(dg); m = e.to_mesh()
    pts = np.array([o.matrix_world @ v.co for v in m.vertices]); e.to_mesh_clear()
    return pts

pts = evaluated_points(body)
bmin, bmax = pts.min(0), pts.max(0)
hand_heads = [armature.matrix_world @ armature.data.bones[n].head_local for n in ("mixamorig:LeftHand", "mixamorig:RightHand")]
print("SPL body", bmin.round(3), bmax.round(3), "clips", clips)

before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=arm_path)
parts = sorted([o for o in bpy.data.objects if o not in before and o.type == "MESH"], key=lambda o: o.name)
amin = np.min([np.array([p.matrix_world @ v.co for v in p.data.vertices]).min(0) for p in parts], axis=0)
amax = np.max([np.array([p.matrix_world @ v.co for v in p.data.vertices]).max(0) for p in parts], axis=0)
S = float((bmax[2] - bmin[2]) / (amax[2] - amin[2]))
Off = (bmin + bmax) / 2 - S * (amin + amax) / 2
print("SPL armour scale", round(S, 3), "offset", Off.round(4))

mat = parts[0].material_slots[0].material
base_img = mr_img = None
for n in mat.node_tree.nodes:
    if n.type == "BSDF_PRINCIPLED":
        for sock, tag in ((n.inputs["Base Color"], "b"), (n.inputs["Metallic"], "m")):
            nd = sock.links[0].from_node if sock.links else None
            while nd is not None and nd.type != "TEX_IMAGE":
                nd = nd.inputs[0].links[0].from_node if nd.inputs and nd.inputs[0].links else None
            if nd is not None:
                if tag == "b": base_img = nd.image
                else: mr_img = nd.image
B = np.array(base_img.pixels[:], dtype=np.float32).reshape(base_img.size[1], base_img.size[0], -1)
M = np.array(mr_img.pixels[:], dtype=np.float32).reshape(mr_img.size[1], mr_img.size[0], -1)

def is_cloth(face, uvl):
    ss = [l[uvl].uv.copy() for l in face.loops]
    c = sum(ss, Vector((0, 0))) / len(ss)
    bv, mv = [], []
    for t in ss + [c]:
        q = (t + c) / 2
        x = int((q.x % 1) * (B.shape[1] - 1)); y = int((q.y % 1) * (B.shape[0] - 1))
        bv.append(B[y, x, :3]); mv.append(M[y, x, 2])
    col = np.median(bv, axis=0)
    return float(np.median(mv)) < 0.5 and float(col.max() - col.min()) < 0.04

def regions(faces):
    fs = set(faces); seen = set(); res = []
    for f in faces:
        if f in seen: continue
        st = [f]; seen.add(f); r = []
        while st:
            c = st.pop(); r.append(c)
            for e in c.edges:
                for g in e.link_faces:
                    if g in fs and g not in seen: seen.add(g); st.append(g)
        res.append(r)
    return res

slot_objs = {}
for p in parts:
    slot = SLOT_OF[p.name]
    # the piece in the knight's rest space, in world coordinates
    M4 = p.matrix_world.copy()
    bm = bmesh.new(); bm.from_mesh(p.data)
    for v in bm.verts: v.co = Vector(np.array(M4 @ v.co) * S + Off)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.0012)
    bm.faces.ensure_lookup_table()
    uvl = bm.loops.layers.uv.active
    cloth = [f for f in bm.faces if is_cloth(f, uvl)]
    doomed = []; kept_cloth = 0.0
    for r in regions(cloth):
        area = sum(f.calc_area() for f in r)
        cen = sum((f.calc_center_median() for f in r), Vector()) / len(r)
        touches_cut = any(e.is_boundary for f in r for e in f.edges)
        if slot == "Gloves" and min((cen - h).length for h in hand_heads) < 0.2: kept_cloth += area; continue   # the glove's hand
        if slot == "Helm" and not touches_cut: kept_cloth += area; continue   # the visor's dark slits
        if touches_cut or area > BIG_CLOTH_AREA: doomed += r
        else: kept_cloth += area
    total = sum(f.calc_area() for f in bm.faces)
    gone = sum(f.calc_area() for f in doomed)
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    # loose scraps left behind
    scraps = [r for r in regions(bm.faces[:]) if sum(f.calc_area() for f in r) < SPECK_AREA]
    if scraps: bmesh.ops.delete(bm, geom=[f for r in scraps for f in r], context="FACES")
    print("SPL %s (%s): %d faces, removed %.0f%% of its area as cloth, kept %.0f cm2 dark, %d scraps" % (p.name, slot, len(bm.faces), 100 * gone / total, kept_cloth * 1e4, len(scraps)))
    me = bpy.data.meshes.new(p.name + "_c"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(p.name + "_c", me); bpy.context.collection.objects.link(o)
    me.materials.clear(); me.materials.append(mat)
    slot_objs.setdefault(slot, []).append(o)
for p in parts: bpy.data.objects.remove(p)

def select_only(o):
    for x in bpy.context.selected_objects: x.select_set(False)
    o.select_set(True); bpy.context.view_layer.objects.active = o

def decimate(o, target):
    tris = sum(len(f.vertices) - 2 for f in o.data.polygons)
    if tris <= target: return
    select_only(o)
    m = o.modifiers.new("d", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = target / tris; m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)

def tidy_edges(o):
    bm = bmesh.new(); bm.from_mesh(o.data)
    for _ in range(8):   # slide the cut edges along themselves until the saw teeth are gone
        moves = {}
        for v in bm.verts:
            be = [e for e in v.link_edges if e.is_boundary]
            if len(be) == 2:
                moves[v] = v.co * 0.5 + (be[0].other_vert(v).co + be[1].other_vert(v).co) * 0.25
        for v, c in moves.items(): v.co = c
    bm.normal_update()
    edges = [e for e in bm.edges if e.is_boundary]
    vnorm = {}
    for e in edges:
        for v in e.verts: vnorm[v] = v.normal.copy()
    r = bmesh.ops.extrude_edge_only(bm, edges=edges)
    new_verts = [g for g in r["geom"] if isinstance(g, bmesh.types.BMVert)]
    bm.normal_update()
    for v in new_verts:
        n = next((vnorm[e.other_vert(v)] for e in v.link_edges if e.other_vert(v) in vnorm), None)
        if n is not None: v.co -= n * RIM
    bm.to_mesh(o.data); bm.free()
    for poly in o.data.polygons: poly.use_smooth = True

final = {}
for slot, objs in slot_objs.items():
    select_only(objs[0])
    for x in objs[1:]: x.select_set(True)
    bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active; o.name = "Plate_" + slot
    decimate(o, BUDGET[slot]); tidy_edges(o)
    final[slot] = o
    print("SPL slot", slot, "tris", sum(len(f.vertices) - 2 for f in o.data.polygons))

# -------------------------------------- the chainmail body, down to budget (with the armature off, so weights stay on the vertices)
for m in body.modifiers: m.show_viewport = True
body_tris = sum(len(f.vertices) - 2 for f in body.data.polygons)
select_only(body)
d = body.modifiers.new("d", "DECIMATE"); d.decimate_type = "COLLAPSE"; d.ratio = BODY_BUDGET / body_tris; d.use_collapse_triangulate = True
bpy.ops.object.modifier_apply(modifier=d.name)
body_tris = sum(len(f.vertices) - 2 for f in body.data.polygons)
print("SPL body tris", body_tris)

# -------------------------------------- weights: copied from the body, limited to each piece's bones
# a stand-in for the body in world space (rest pose), carrying its weights, for the transfer
dg = bpy.context.evaluated_depsgraph_get()
stand = bpy.data.objects.new("stand", body.data.copy()); bpy.context.collection.objects.link(stand)
wm = body.matrix_world.copy()
stand.matrix_world = wm
for g in body.vertex_groups: stand.vertex_groups.new(name=g.name)
for v in body.data.vertices:
    for g in v.groups: stand.vertex_groups[g.group].add([v.index], g.weight, "REPLACE")
bvh_body = BVHTree.FromPolygons([wm @ v.co for v in body.data.vertices], [list(p.vertices) for p in body.data.polygons])
body_w = [{body.vertex_groups[g.group].name: g.weight for g in v.groups} for v in body.data.vertices]
polys = body.data.polygons
for slot, o in final.items():
    allow = {"mixamorig:" + n for n in ALLOWED[slot]}
    vg = {}
    def group(n):
        if n not in vg: vg[n] = o.vertex_groups.new(name=n)
        return vg[n]
    empty = 0
    for v in o.data.vertices:
        co = o.matrix_world @ v.co
        loc, nrm, idx, dist = bvh_body.find_nearest(co)
        if idx is None: continue
        pv = polys[idx].vertices
        pc = [wm @ body.data.vertices[i].co for i in pv]
        w = np.array([1.0 / max((c - loc).length, 1e-4) for c in pc]); w /= w.sum()
        acc = {}
        for k, i in enumerate(pv):
            for n, wt in body_w[i].items():
                if n in allow: acc[n] = acc.get(n, 0.0) + wt * w[k]
        if not acc:   # all its bones are off limits: the nearest allowed bone takes it
            empty += 1
            best = min(allow, key=lambda n: (co - armature.matrix_world @ armature.data.bones[n].head_local).length if n in armature.data.bones else 1e9)
            acc = {best: 1.0}
        top = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
        t = sum(x[1] for x in top)
        for n, wt in top: group(n).add([v.index], wt / t, "REPLACE")
    if slot == "Helm":   # a helmet is one rigid thing
        for n, g in list(vg.items()): o.vertex_groups.remove(g)
        vg.clear()
        g = o.vertex_groups.new(name="mixamorig:Head")
        g.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    print("SPL weights", slot, "bones", sorted(g.name.replace("mixamorig:", "") for g in o.vertex_groups), "fallbacks", empty)
bpy.data.objects.remove(stand)

# -------------------------------------- which chainmail each piece covers
trees = {}
for slot, o in final.items():
    trees[slot] = BVHTree.FromPolygons([v.co.copy() for v in o.data.vertices], [list(p.vertices) for p in o.data.polygons])
W = [wm @ v.co for v in body.data.vertices]
N = [(wm.to_3x3() @ v.normal).normalized() for v in body.data.vertices]
cover = [None] * len(W); poke = 0
for i, (co, n) in enumerate(zip(W, N)):
    best = None; bd = 1e9
    for slot, t in trees.items():
        hit = t.ray_cast(co, n, COVER_REACH)
        if hit[0] is not None and hit[3] < bd: bd = hit[3]; best = slot
        back = t.ray_cast(co, -n, BACK_REACH)   # the mail sticks out through this plate here
        if back[0] is not None:
            if hit[0] is None: poke += 1
            if back[3] < bd: bd = back[3]; best = slot
    cover[i] = best
print("SPL body verts covered", sum(1 for c in cover if c), "of", len(cover), "; outside a plate at rest:", poke)
SLOTS = ["Helm", "Chest", "Shoulders", "Gloves", "Legs"]
body_mat = body.data.materials[0]
body.data.materials.clear()
names = ["Bare"] + SLOTS
for n in names:
    m = body_mat.copy(); m.name = "Mail" + n; body.data.materials.append(m)
_w = {}
weld = [_w.setdefault((round(c.x / 0.004), round(c.y / 0.004), round(c.z / 0.004)), len(_w)) for c in W]
# flecks: a small patch of bare mail inside a plate hides with it
def patches(idx):
    idx = set(idx); seen = set(); res = []
    e2f = {}
    def keys(pl):   # edges by where they are: the mail's vertices are split along its texture seams
        vs = [weld[v] for v in pl.vertices]
        return [tuple(sorted((vs[j], vs[(j + 1) % len(vs)]))) for j in range(len(vs))]
    for i in idx:
        for k in keys(polys[i]): e2f.setdefault(k, []).append(i)
    for i in idx:
        if i in seen: continue
        st = [i]; seen.add(i); r = []
        while st:
            c = st.pop(); r.append(c)
            for k in keys(polys[c]):
                for g in e2f[k]:
                    if g not in seen: seen.add(g); st.append(g)
        res.append(r)
    return res
FLECK = 0.006
for p in polys:
    vs = [cover[i] for i in p.vertices]
    p.material_index = names.index(max(set(vs), key=vs.count)) if all(vs) else 0
for r in patches([p.index for p in polys if p.material_index == 0]):
    if sum(polys[i].area for i in r) * wm.median_scale ** 2 < FLECK:
        near = [cover[v] for i in r for v in polys[i].vertices if cover[v]]
        if near:
            for i in r: polys[i].material_index = names.index(max(set(near), key=near.count))
counts = {n: 0 for n in names}
for p in polys:
    counts[names[p.material_index]] += 1

print("SPL mail faces by piece", counts)

# -------------------------------------- one plate mesh, a material per slot, on the armature
sect = []
for k, slot in enumerate(SLOTS):
    o = final[slot]
    m = mat.copy(); m.name = slot
    o.data.materials.clear(); o.data.materials.append(m)
    sect.append(o)
select_only(sect[0])
for x in sect[1:]: x.select_set(True)
bpy.ops.object.join()
plate = bpy.context.view_layer.objects.active; plate.name = "SK_Split_Plate"
# into the body's own space (under the armature), weights as they are
inv = armature.matrix_world.inverted()
plate.data.transform(inv @ plate.matrix_world)
plate.parent = armature; plate.matrix_parent_inverse.identity(); plate.matrix_basis.identity()
arm_mod = plate.modifiers.new("Armature", "ARMATURE"); arm_mod.object = armature
body.name = "SK_Split_Body"
print("SPL plate tris", sum(len(f.vertices) - 2 for f in plate.data.polygons))
for img, nm in ((base_img, "T_Split_Base"), (mr_img, "T_Split_MR")):
    img.scale(2048, 2048); img.filepath_raw = os.path.join(out, nm + ".png"); img.file_format = "PNG"; img.save()
for img in bpy.data.images:
    if img.name == "texture_0": img.scale(2048, 2048); img.filepath_raw = os.path.join(out, "T_Mail2_Base.png"); img.file_format = "PNG"; img.save()
    if img.name == "texture_0_metallic_roughness": img.scale(2048, 2048); img.filepath_raw = os.path.join(out, "T_Mail2_MR.png"); img.file_format = "PNG"; img.save()
armature.data.pose_position = "POSE"
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "split_knight.blend"))
print("SPL saved")
