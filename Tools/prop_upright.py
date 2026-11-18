"""Stands a tilted single-picture model upright (the staff and wand came out tilted like their pictures): the principal axis -> +Z.
blender -b --factory-startup --python Tools/prop_upright.py -- in.glb out.glb [flip]   (flip: the other end up)"""
import bpy, sys
import numpy as np
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a[0])
M = [o for o in bpy.context.scene.objects if o.type == "MESH"]
P = np.array([[(o.matrix_world @ v.co)[i] for i in range(3)] for o in M for v in o.data.vertices])
w, vec = np.linalg.eigh(np.cov((P - P.mean(axis=0)).T)); d = vec[:, 2]
if d[2] < 0: d = -d
if len(a) > 2: d = -d
q = Vector(d).rotation_difference(Vector((0, 0, 1)))
print("UPRIGHT axis", d)
for o in bpy.context.scene.objects:
    if o.parent is None: o.matrix_world = q.to_matrix().to_4x4() @ o.matrix_world
bpy.ops.export_scene.gltf(filepath=a[1], export_format="GLB")
