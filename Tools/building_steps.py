# Rebuilds a building's front steps as clean, crisp stone steps (2026-11-14, owner on the storybook cottage: "the front stairs are
# weirdly smoothed over"). Meshy made them as a shallow ramp with faint ridges; this deletes the ramp's walking faces (treads and
# risers; the side walls and piers stay) and puts in real box steps, each face mapped onto the model's own stone from its landing
# (an affine fit of the landing's top from position to picture), so they wear the same painted stone.
# Blender: blender -b -P Tools/building_steps.py -- <in.glb> <out.glb> <name>     (numbers per building below, in the file's units)
import bpy, bmesh, sys
import numpy as np
from mathutils import Vector

STEPS = {
    # storybook cottage, file frame (door to -Y): stairs between the pier (x 0.10) and the right wall (x 0.44), ground z -0.912 at
    # y -0.73 up to the landing z -0.751 from y -0.32; the landing's own top (picture source) x 0.20-0.42, y -0.32..-0.26
    "StorybookCottage_300k": dict(x0=0.113, x1=0.432, yfront=-0.665, ytop=-0.322, zlow=-0.912, ztop=-0.751, n=5, under=-0.94,
                                  land=(0.20, 0.42, -0.318, -0.262, -0.756, -0.746), nose=0.004,
                                  riser=(0.20, 0.42, -0.34, -0.31, -0.786, -0.752), slabs=3, gap=0.0015, bevel=0.003),   # (the top step's riser, x, y, z)
}

a = sys.argv[sys.argv.index("--") + 1:]
src, out, cfg = a[0], a[1], STEPS[a[2]]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
M = ob.matrix_world; Mi = M.inverted(); R = M.to_3x3()
bm = bmesh.new(); bm.from_mesh(ob.data); uvl = bm.loops.layers.uv.active
bm.faces.ensure_lookup_table()
c = cfg
# 1. the landing's picture: fit (x, y) -> (u, v) on its top faces (world frame)
X, U = [], []
lx0, lx1, ly0, ly1, lz0, lz1 = c["land"]
def inland(f):
    m = M @ f.calc_center_median(); nz = (R @ f.normal).normalized().z
    return nz > 0.9 and lx0 <= m.x <= lx1 and ly0 <= m.y <= ly1 and lz0 <= m.z <= lz1
# (one picture island only: grow from the face nearest the box middle across edges whose two sides share their UVs)
mid = Vector(((lx0 + lx1) / 2, (ly0 + ly1) / 2, (lz0 + lz1) / 2))
seed = min((f for f in bm.faces if inland(f)), key=lambda f: ((M @ f.calc_center_median()) - mid).length)
isle, todo = {seed}, [seed]
while todo:
    f = todo.pop()
    for l in f.loops:
        for o in l.edge.link_loops:
            g = o.face
            if g in isle or not inland(g): continue
            if (o[uvl].uv - l.link_loop_next[uvl].uv).length < 1e-5 and (o.link_loop_next[uvl].uv - l[uvl].uv).length < 1e-5:
                isle.add(g); todo.append(g)
for f in isle:
        for l in f.loops:
            w = M @ l.vert.co; X.append((w.x, w.y, 1.0)); U.append(tuple(l[uvl].uv))
X, U = np.array(X), np.array(U)
A, res, *_ = np.linalg.lstsq(X, U, rcond=None)
err = np.abs(X @ A - U).max()
print("STEPS landing corners %d, fit worst %.4f" % (len(X), err))
if len(X) < 30 or err > 0.01: raise SystemExit("STEPS the landing is not one even picture: pick another land box")
# (from here on the landing box is the island's own extent, 15% in from each side, so no picture is taken from past its edge)
ix, iy = X[:, 0], X[:, 1]
lx0, lx1 = ix.min() + 0.15 * np.ptp(ix), ix.max() - 0.15 * np.ptp(ix)
ly0, ly1 = iy.min() + 0.15 * np.ptp(iy), iy.max() - 0.15 * np.ptp(iy)
print("STEPS picture island x %.3f..%.3f y %.3f..%.3f" % (lx0, lx1, ly0, ly1))
# the riser's picture: the landing's own front face (the top real step's riser, before the ramp goes), fit (x, z) -> (u, v), one island
rx0, rx1, ry0, ry1, rz0, rz1 = c["riser"]
def inris(f):
    m = M @ f.calc_center_median(); ny = (R @ f.normal).normalized().y
    return ny < -0.8 and rx0 <= m.x <= rx1 and ry0 <= m.y <= ry1 and rz0 <= m.z <= rz1
rmid = Vector(((rx0 + rx1) / 2, (ry0 + ry1) / 2, (rz0 + rz1) / 2))
seed = min((f for f in bm.faces if inris(f)), key=lambda f: ((M @ f.calc_center_median()) - rmid).length)
isle, todo = {seed}, [seed]
while todo:
    f = todo.pop()
    for l in f.loops:
        for o in l.edge.link_loops:
            g = o.face
            if g in isle or not inris(g): continue
            if (o[uvl].uv - l.link_loop_next[uvl].uv).length < 1e-5 and (o.link_loop_next[uvl].uv - l[uvl].uv).length < 1e-5:
                isle.add(g); todo.append(g)
XR = np.array([((M @ l.vert.co).x, (M @ l.vert.co).z, 1.0) for f in isle for l in f.loops])
UR = np.array([tuple(l[uvl].uv) for f in isle for l in f.loops])
AR = np.linalg.lstsq(XR, UR, rcond=None)[0]
print("STEPS riser corners %d, fit worst %.4f" % (len(XR), np.abs(XR @ AR - UR).max()))
qx0, qx1 = XR[:, 0].min() + 0.1 * np.ptp(XR[:, 0]), XR[:, 0].max() - 0.1 * np.ptp(XR[:, 0])
qz0, qz1 = XR[:, 1].min() + 0.1 * np.ptp(XR[:, 1]), XR[:, 1].max() - 0.1 * np.ptp(XR[:, 1])
def ruv(x, z): return Vector(tuple(np.array((x, z, 1.0)) @ AR))
# 2. delete the ramp: faces in the stair box that aren't side walls
kill = []
for f in bm.faces:
    m = M @ f.calc_center_median(); n = (R @ f.normal).normalized()
    if c["x0"] <= m.x <= c["x1"] and c["yfront"] - 0.012 <= m.y <= c["ytop"] + 0.004 and c["under"] <= m.z <= c["ztop"] + 0.004 and (abs(n.x) < 0.6 or c["x0"] + 0.01 < m.x < c["x1"] - 0.01):
        kill.append(f)
    # (and the ramp's foot spilling out in front of the bottom step, whatever way it faces)
    elif c["x0"] - 0.01 <= m.x <= c["x1"] + 0.03 and c["yfront"] - 0.09 <= m.y < c["yfront"] - 0.012 and m.z <= c["zlow"] + 0.05:
        kill.append(f)
bmesh.ops.delete(bm, geom=kill, context="FACES")
print("STEPS removed %d faces" % len(kill))
# 3. the steps: tread i (only its visible depth), its riser down to the tread below, and both ends closed from the ground up
dep = c["ytop"] - c["yfront"]; d = dep / c["n"]; h = (c["ztop"] - c["zlow"]) / c["n"]
ly_len = ly1 - ly0
def uv_of(x, y): return Vector(tuple(np.array((x, y, 1.0)) @ A))
def UX(x): return lx0 + (lx1 - lx0) * (x - c["x0"]) / (c["x1"] - c["x0"])
def V(x, y, z): return bm.verts.new(Mi @ Vector((x, y, z)))
x0, x1, yb = c["x0"], c["x1"], c["ytop"] + 0.004
new = []
def face(pts, uvf, want):
    f = bm.faces.new([V(*p) for p in pts]); f.normal_update()
    if (R @ f.normal).dot(Vector(want)) < 0: f.normal_flip()
    for l in f.loops:
        w = M @ l.vert.co; l[uvl].uv = uvf(w)
    new.append(f)
# (2026-11-14, second pass) The step stone: Meshy's picture islands here are tiny (about 25 texels), so any stone stretched over a
# whole tread came out blurred and mossy. Instead every slab is real geometry, a separate box with bevelled edges and a thin gap to
# its neighbours, so the joints and step edges are drawn by the light (the town's flat-painted look), each wearing ONE clean texel of
# the model's own stone: the least mossy spots of the landing, a slightly different one per slab, the darker ones on fronts and ends.
img = next(n for n in ob.data.materials[0].node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"].links[0].from_node.image
W, H = img.size
px = np.empty(W * H * 4, dtype=np.float32); img.pixels.foreach_get(px); px = px.reshape(H, W, 4)
cands = []
for fa in np.linspace(0.1, 0.9, 9):
    for fb in np.linspace(0.1, 0.9, 9):
        for uv in (uv_of(lx0 + fa * (lx1 - lx0), ly0 + fb * (ly1 - ly0)), ruv(qx0 + fa * (qx1 - qx0), qz0 + fb * (qz1 - qz0))):
            p = px[min(int(uv.y * H), H - 1), min(int(uv.x * W), W - 1), :3]
            moss = p[1] - (p[0] + p[2]) / 2; lum = p @ np.array([0.30, 0.59, 0.11])
            cands.append((moss, lum, uv.copy()))
cands.sort(key=lambda t: t[0]); clean = sorted(cands[:12], key=lambda t: -t[1])   # the 12 least green, lightest first
light, dark = [t[2] for t in clean[:6]], [t[2] for t in clean[6:]]
print("STEPS stone texels: moss %.3f..%.3f, lightness %.2f..%.2f" % (clean[0][0], max(t[0] for t in clean), clean[-1][1], clean[0][1]))
gap, bev = c["gap"], c["bevel"]
slab_edges = []
def box(xa, xb, ya, yb_, za, zb_, top_uv, side_uv):
    vs = [V(x, y, z) for z in (za, zb_) for y in (ya, yb_) for x in (xa, xb)]   # 0..3 bottom, 4..7 top
    quads = [((4, 5, 7, 6), (0, 0, 1), top_uv), ((0, 1, 5, 4), (0, -1, 0), side_uv), ((0, 2, 6, 4), (-1, 0, 0), side_uv),
             ((1, 3, 7, 5), (1, 0, 0), side_uv)]   # (no back or bottom: always hidden)
    fs = []
    for q, want, uv in quads:
        f = bm.faces.new([vs[k] for k in q]); f.normal_update()
        if (R @ f.normal).dot(Vector(want)) < 0: f.normal_flip()
        for l in f.loops: l[uvl].uv = uv
        new.append(f); fs.append(f)
    slab_edges.extend({e for f in fs for e in f.edges if all(len([g for g in e.link_faces if g in fs]) == 2 for _ in [0])})
k_ = 0
for i in range(c["n"]):
    yf = c["yfront"] + d * i - c["nose"]; zt = c["zlow"] + h * (i + 1)
    zb = c["under"] if i == 0 else c["zlow"] + h * i - 0.004
    ye = yb
    K = c["slabs"]
    edges = [x0 + (x1 - x0) * k / K for k in range(K + 1)]
    if i % 2: edges = [x0] + [e - (x1 - x0) / (2 * K) for e in edges[1:-1]] + [x1]   # (every other step shifted half a slab, like laid stone)
    for a_, (xa, xb) in enumerate(zip(edges, edges[1:])):
        box(xa + (gap / 2 if a_ else 0), xb - (gap / 2 if a_ < len(edges) - 2 else 0), yf, ye, zb, zt, light[k_ % len(light)], dark[k_ % len(dark)])
        k_ += 1
bmesh.ops.bevel(bm, geom=list(set(slab_edges)), offset=bev, segments=2, profile=0.5, affect="EDGES", clamp_overlap=True)
bm.to_mesh(ob.data); bm.free()
print("STEPS added %d slabs" % k_)
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB")
print("STEPS wrote", out)
