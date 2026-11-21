# The Dark Plate gauntlets' upper-arm plates (rerebraces), 2026-11-21. The Meshy arm piece stops at the elbow, so his mail sleeve showed between pauldron
# and elbow and read as leopard print next to the black steel. This copies a band of the piece's own vambrace (same triangles, same texture, so the same
# look) onto each upper arm, turned end for end (its narrow wrist end sits on the elbow cop, its wide elbow end goes up inside the pauldron), widened to the
# upper arm by the ratio of the mail arm's own widths, and weighted to the upper arm bone. Used by knight_darkplate_export.py before the fists.
import bpy, math
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

# (m, along the forearm from the elbow joint) the band of vambrace copied; (m, up the upper arm from the elbow joint) where it goes
SRC_T0, SRC_T1 = 0.03, 0.20
DST_S0, DST_S1 = 0.0, 0.19
ELBOW_BLEND = 0.05   # (m) half the length of the blend from the upper arm bone to the forearm bone, centred on the elbow joint
SPIN = 180.0      # (degrees) the copy turned about the upper arm, so the vambrace's outer plates face out
OFF_MIN, OFF_MAX = 0.006, 0.02   # (m) how far over his mail each point of the copy stands, at least and at most (the elbow cop's points stood out as spikes)


def _bone(arm, n):
    return arm.matrix_world @ arm.data.bones["mixamorig:" + n].head_local

def _radius(body, arm, side, bone, a, b):
    """The mail arm's middle distance from the line a-b, over points weighted to that bone."""
    gi = body.vertex_groups.get("mixamorig:" + side + bone)
    if gi is None: return None
    ax = (b - a).normalized(); L = (b - a).length; out = []
    mw = body.matrix_world
    for v in body.data.vertices:
        if any(g.group == gi.index and g.weight > 0.7 for g in v.groups):
            d = mw @ v.co - a; t = d.dot(ax)
            if 0.2 * L < t < 0.8 * L: out.append((d - ax * t).length)
    return float(np.median(out)) if out else None

def _axis(body, arm, side, a, b):
    """The mail upper arm's own middle line (its points' centres low and high), which sits off the bone; returns (point at the elbow's height, direction up)."""
    gi = body.vertex_groups.get("mixamorig:" + side + "Arm")
    ax = (b - a).normalized(); L = (b - a).length; lo, hi = [], []
    mw = body.matrix_world
    for v in body.data.vertices:
        if any(g.group == gi.index and g.weight > 0.7 for g in v.groups):
            p = mw @ v.co; t = (p - a).dot(ax)
            if 0.15 * L < t < 0.35 * L: lo.append(p[:])
            elif 0.5 * L < t < 0.7 * L: hi.append(p[:])
    if not lo or not hi: return a, ax
    cl, ch = Vector(np.mean(lo, 0)), Vector(np.mean(hi, 0))
    d = (ch - cl).normalized()
    return cl - d * (cl - a).dot(d), d

def add(gaunt, arm, body, log=print):
    bpy.context.view_layer.update()
    me = gaunt.data
    names = {g.index: g.name.split(":")[-1] for g in gaunt.vertex_groups}
    nv = len(me.vertices)
    P = np.empty((nv, 3)); me.vertices.foreach_get("co", P.ravel())
    mw = np.array(gaunt.matrix_world); P = P @ mw[:3, :3].T + mw[:3, 3]
    fore_w = {"Right": np.zeros(nv), "Left": np.zeros(nv)}
    for v in me.vertices:
        for g in v.groups:
            n = names.get(g.group, "")
            for sd in ("Right", "Left"):
                if n == sd + "ForeArm": fore_w[sd][v.index] = g.weight
    tris = np.empty(len(me.loops), dtype=np.int64); me.loops.foreach_get("vertex_index", tris); tris = tris.reshape(-1, 3)
    cn = np.array([c.vector[:] for c in me.corner_normals]).reshape(-1, 3, 3)
    uvl = me.uv_layers.active
    uv = np.empty(len(me.loops) * 2); uvl.data.foreach_get("uv", uv); uv = uv.reshape(-1, 3, 2)
    dg = bpy.context.evaluated_depsgraph_get(); be = body.evaluated_get(dg); bm_ = be.to_mesh()
    bvh = BVHTree.FromPolygons([(body.matrix_world @ v.co)[:] for v in bm_.vertices], [list(p.vertices) for p in bm_.polygons]); be.to_mesh_clear()
    new_objs = []
    for sd in ("Right", "Left"):
        S, E, W = _bone(arm, sd + "Arm"), _bone(arm, sd + "ForeArm"), _bone(arm, sd + "Hand")
        f = (W - E).normalized(); E2, u = _axis(body, arm, sd, E, S)
        h = u.cross(f).normalized()
        Rf = Matrix((f, h, f.cross(h))).transposed()
        Ru = Matrix((-u, h, (-u).cross(h))).transposed()
        R = Matrix.Rotation(math.radians(SPIN), 3, u) @ Ru @ Rf.transposed()
        rf, ru = _radius(body, arm, sd, "ForeArm", E, W), _radius(body, arm, sd, "Arm", E, S)
        k = (DST_S1 - DST_S0) / (SRC_T1 - SRC_T0)
        e = np.array(E[:]); fa = np.array(f[:]); Rn = np.array(R)
        t = (P - e) @ fa
        ok = (fore_w[sd] > 0.9) & (t > SRC_T0) & (t < SRC_T1)
        sel = np.where(ok[tris].all(1))[0]
        if not len(sel):
            fw = fore_w[sd] > 0.9
            log("RRB %s: nothing to copy (forearm points %d, along %.3f..%.3f; elbow %s wrist %s)" % (sd, fw.sum(), t[fw].min() if fw.any() else 0, t[fw].max() if fw.any() else 0, E, W)); continue
        vi = np.unique(tris[sel]); remap = -np.ones(nv, dtype=np.int64); remap[vi] = np.arange(len(vi))
        tv = t[vi]; rad = (P[vi] - e) - np.outer(tv, fa); rad -= rad.mean(0)   # (about the band's own middle, which sits off the bone)
        c0 = e + (P[vi] - e - np.outer(tv, fa)).mean(0)   # (the band's own middle line)
        sd_ = DST_S1 - (tv - SRC_T0) * k
        Q = np.empty((len(vi), 3))
        for j in range(len(vi)):   # each point keeps its height over the mail: on the forearm's mail, then on the upper arm's
            r = Vector(rad[j]); L = r.length; dv = r / max(L, 1e-9)
            a_s = Vector(c0 + fa * tv[j]); hs = bvh.ray_cast(a_s, dv, 0.2)
            off = min(max(L - (hs[3] if hs[0] else rf), OFF_MIN), OFF_MAX)
            dd = R @ dv; a_d = E2 + u * sd_[j]; hd = bvh.ray_cast(a_d, dd, 0.25)
            Q[j] = a_d + dd * ((hd[3] if hd[0] else ru) + off)
        m2 = bpy.data.meshes.new("rrb_" + sd)
        m2.from_pydata(Q.tolist(), [], remap[tris[sel]].tolist()); m2.update()
        m2.uv_layers.new(name=uvl.name); m2.uv_layers[0].data.foreach_set("uv", uv[sel].ravel())
        for mat in me.materials: m2.materials.append(mat)
        nn = cn[sel].reshape(-1, 3) @ Rn.T
        m2.normals_split_custom_set(nn.tolist()); m2.update()
        ob = bpy.data.objects.new("rrb_" + sd, m2); bpy.context.scene.collection.objects.link(ob)
        vg = ob.vertex_groups.new(name="mixamorig:" + sd + "Arm"); vf = ob.vertex_groups.new(name="mixamorig:" + sd + "ForeArm")
        for j in range(len(vi)):   # (2026-11-21 owner: the plate popped out of the arm: its low end sat 3 cm below the elbow joint on the upper arm bone alone, so a bent elbow drove it out through the forearm; now the last 5 cm above the joint blend to the forearm bone, half at the joint)
            wf = min(max((ELBOW_BLEND - sd_[j]) / (2 * ELBOW_BLEND), 0.0), 1.0)
            vg.add([j], 1.0 - wf, "REPLACE")
            if wf > 0.001: vf.add([j], wf, "REPLACE")
        new_objs.append(ob)
        log("RRB %s: %d triangles copied, stretched %.2f, mail arm radius %.3f / forearm %.3f" % (sd, len(sel), k, ru or 0, rf or 0))
    if not new_objs: return
    bpy.ops.object.select_all(action="DESELECT")
    for ob in new_objs: ob.select_set(True)
    gaunt.select_set(True); bpy.context.view_layer.objects.active = gaunt
    bpy.ops.object.join()
