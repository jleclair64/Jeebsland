# The playable troll's joints (2026-11-19): where each of the knight's 24 bones sits in the knight's rig and in the troll's (both Meshy rigs,
# the same 24 bones under different names), at their bind poses, in Blender's metres. FClipLib::GetTroll (KnightAnimator.cpp) reads it to
# give the knight's animator the troll's proportions; the knight's clips then play on the troll unchanged.
# Run: blender -b -P troll_rig_export.py -- <rigged knight.glb> <Troll_rigged.glb> <out csv>
import bpy, sys
a = sys.argv[sys.argv.index("--") + 1:]
KNIGHT, TROLL, OUT = a[0], a[1], a[2]
# knight bone (without "mixamorig:") -> troll bone
MAP = {"Hips": "Hips", "Spine": "Spine02", "Spine1": "Spine01", "Spine2": "Spine", "Neck": "neck", "Head": "Head", "HeadTop_End": "head_end", "headfront": "headfront"}
def joints(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
    return {b.name.replace("mixamorig:", ""): tuple(arm.matrix_world @ b.head_local) for b in arm.data.bones}
K = joints(KNIGHT)
T = joints(TROLL)
with open(OUT, "w", newline="\n") as f:
    f.write("Bone,KnightX,KnightY,KnightZ,TrollBone,TrollX,TrollY,TrollZ\n")
    f.write("# Made by Tools/troll_rig_export.py from the two rigs (Blender metres, +X the model's left, -Y its front, +Z up). Don't edit by hand.\n")
    for k, p in K.items():
        t = MAP.get(k, k)
        if t not in T: print("TROLLRIG missing", t); continue
        q = T[t]
        f.write("%s,%.4f,%.4f,%.4f,%s,%.4f,%.4f,%.4f\n" % (k, p[0], p[1], p[2], t, q[0], q[1], q[2]))
print("TROLLRIG wrote", OUT)
