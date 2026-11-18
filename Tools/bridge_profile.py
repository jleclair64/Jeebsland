# Measures the bridge model (Blender, ray casts): the top surface and the underside along its length and across it.
#   blender -b -P Tools/bridge_profile.py -- <in.glb> <out.json>
import bpy, sys, json, mathutils
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a[0])
ob = [o for o in bpy.data.objects if o.type == "MESH"][0]
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
pts = [ob.matrix_world @ mathutils.Vector(c) for c in ob.bound_box]
mn = [min(p[i] for p in pts) for i in range(3)]; mx = [max(p[i] for p in pts) for i in range(3)]
print("BRPROF bounds", mn, mx)
# which axis is the length? (the longer of X and Y)
L = 0 if (mx[0] - mn[0]) > (mx[1] - mn[1]) else 1
W = 1 - L
def top(l, w):
    o = [0, 0, mx[2] + 1]; o[L] = l; o[W] = w
    hit, loc, n, i = ob.ray_cast(mathutils.Vector(o), mathutils.Vector((0, 0, -1)))
    return loc[2] if hit else None
def under(l, w):
    # the first surface seen from below
    o = [0, 0, mn[2] - 1]; o[L] = l; o[W] = w
    hit, loc, n, i = ob.ray_cast(mathutils.Vector(o), mathutils.Vector((0, 0, 1)))
    return loc[2] if hit else None
out = {"L": L, "bounds": [mn, mx], "cross": {}, "centre": [], "under": [], "edge": {}}
N = 190
for k in range(N + 1):
    l = mn[L] + (mx[L] - mn[L]) * k / N
    out["centre"].append([round(l, 4), top(l, 0.0)])
    out["under"].append([round(l, 4), under(l, 0.0)])
# cross sections at a few stations
for st in (-0.9, -0.6, -0.3, 0.0, 0.3, 0.6, 0.9):
    row = []
    for j in range(0, 81):
        w = mn[W] + (mx[W] - mn[W]) * j / 80
        row.append([round(w, 4), top(st, w)])
    out["cross"][str(st)] = row
json.dump(out, open(a[1], "w"))
print("BRPROF done, length axis", "XY"[L])
