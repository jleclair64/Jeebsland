# Blender (background): the bear's rig and clips (2026-11-26, agent animals).
#   blender -b --factory-startup -P Tools/bear_rig.py -- <Wolf_clips.glb> <Bear_clean.glb> <out Bear_clips.glb>
# Why: Meshy's auto-rig refused the library bear twice (it is modelled mid-stride). Every bone of the wolf's Meshy rig points straight up
# (+Z) at rest, so moving its joints keeps each bone's rest orientation exactly, and the wolf's clips (rotations) play unchanged on the
# moved rig. So: the wolf's skeleton is stretched to the bear's body (per axis), each leg chain is bent onto the bear's own (mid-stride)
# foot, the tail is shrunk to the bear's stub, the bear is skinned to it by Blender's automatic (heat) weights, and the root's bounce is
# scaled. Writes one .glb with the skinned bear and the wolf's five actions (wolf_idle, dog_walk, dog_run, wolf_dead, leopard_skill2) as NLA
# tracks, read by Tools/BuildDeer.ps1 like the other clips files. No credits.
import bpy, sys
from mathutils import Vector
a = sys.argv[sys.argv.index("--") + 1:]
WOLF, BEAR, OUT = a[0], a[1], a[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=WOLF)
A = next(o for o in bpy.data.objects if o.type == "ARMATURE")
for o in [o for o in bpy.data.objects if o.type == "MESH"]: bpy.data.objects.remove(o)
acts = list(bpy.data.actions)
bpy.ops.import_scene.gltf(filepath=BEAR)
M = next(o for o in bpy.data.objects if o.type == "MESH")
for act in [x for x in bpy.data.actions if x not in acts]: bpy.data.actions.remove(act)
if A.animation_data: A.animation_data.action = None
S = Vector((2.6, 2.2, 1.55))      # wolf -> bear: width, length, height of the body
FEET = {"FL": Vector((0.28, -0.62, 0.02)), "FR": Vector((-0.05, -0.33, 0.02)),   # the bear's feet (vertex clusters on the ground)
        "RL": Vector((0.13, 0.66, 0.02)), "RR": Vector((-0.36, 0.56, 0.02))}
LEGS = {"FL": ["clavicle.L", "frontUpr.L", "frontLeg.L_03", "frontHorselink.L", "frontToe.L"],
        "FR": ["clavicle.R", "frontUpr.R", "frontHorselink.R", "frontFoot.R", "frontToe.R"],
        "RL": ["rearUpr.L", "rearLwr.L", "rearHorselink.L", "rearFoot.L", "rearToe.L"],
        "RR": ["rearUpr.R", "rearLwr.R", "rearHorselink.R", "rearFoot.R", "rearToe.R"]}
TAIL = ["tail1", "tail_02", "tail2", "tail4", "tail_05", "tail_06"]
bpy.context.view_layer.objects.active = A
bpy.ops.object.mode_set(mode="EDIT")
E = A.data.edit_bones
Wi = A.matrix_world.inverted(); W = A.matrix_world
old = {b.name: (W @ b.head, (b.tail - b.head).length) for b in E}
new = {n: Vector((h.x * S.x, h.y * S.y, h.z * S.z)) for n, (h, l) in old.items()}
for k, ch in LEGS.items():
    top = new[ch[0]]; foot = new[ch[-1]]; off = FEET[k] - foot
    for i, n in enumerate(ch):
        t = (top.z - new[n].z) / max(1e-3, top.z - foot.z)
        new[n] = new[n] + off * max(0.0, min(1.0, t))
t0 = new["tail1"]
for n in TAIL: new[n] = t0 + (new[n] - t0) * 0.25
for b in E:
    l = old[b.name][1] * 1.6
    b.head = Wi @ new[b.name]; b.tail = Wi @ (new[b.name] + Vector((0, 0, l)))
bpy.ops.object.mode_set(mode="OBJECT")
# the root's keyed movement (local; the bone points up, so its Y is world height): scale to the bear
for act in acts:
    fcs = act.fcurves if hasattr(act, "fcurves") else [fc for l in act.layers for s in l.strips for cb in s.channelbags for fc in cb.fcurves]
    for fc in fcs:
        if fc.data_path.endswith(".location"):
            for kp in fc.keyframe_points:
                kp.co.y *= S.z; kp.handle_left.y *= S.z; kp.handle_right.y *= S.z
bpy.ops.object.select_all(action="DESELECT")
M.select_set(True); A.select_set(True); bpy.context.view_layer.objects.active = A
# heat weights fail on the library mesh (loose, open pieces): weight a watertight voxel copy, then copy its weights across
P = M.copy(); P.data = M.data.copy(); bpy.context.collection.objects.link(P)
rm = P.modifiers.new("v", "REMESH"); rm.mode = "VOXEL"; rm.voxel_size = 0.025
bpy.ops.object.select_all(action="DESELECT"); P.select_set(True); bpy.context.view_layer.objects.active = P
bpy.ops.object.modifier_apply(modifier="v")
print("BEAR proxy verts %d" % len(P.data.vertices))
P.select_set(True); A.select_set(True); bpy.context.view_layer.objects.active = A
bpy.ops.object.parent_set(type="ARMATURE_AUTO")
for g in P.vertex_groups: M.vertex_groups.new(name=g.name)
dt = M.modifiers.new("dt", "DATA_TRANSFER"); dt.object = P; dt.use_vert_data = True
dt.data_types_verts = {"VGROUP_WEIGHTS"}; dt.vert_mapping = "POLYINTERP_NEAREST"
dt.layers_vgroup_select_src = "ALL"; dt.layers_vgroup_select_dst = "NAME"
bpy.ops.object.select_all(action="DESELECT"); M.select_set(True); bpy.context.view_layer.objects.active = M
bpy.ops.object.modifier_apply(modifier="dt")
bpy.data.objects.remove(P)
M.parent = A; M.matrix_parent_inverse = A.matrix_world.inverted(); print("BEAR arm matrix", A.matrix_world); am = M.modifiers.new("Armature", "ARMATURE"); am.object = A
unw = sum(1 for v in M.data.vertices if not v.groups)
print("BEAR groups %d, unweighted verts %d of %d" % (len(M.vertex_groups), unw, len(M.data.vertices)))
A.animation_data_create()
for t in list(A.animation_data.nla_tracks): A.animation_data.nla_tracks.remove(t)
for act in acts:
    tr = A.animation_data.nla_tracks.new(); tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act); tr.mute = True
A.animation_data.action = None
bpy.ops.object.select_all(action="DESELECT")
for o in bpy.data.objects: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_animation_mode="NLA_TRACKS", export_image_format="AUTO")
print("BEAR wrote", OUT)
