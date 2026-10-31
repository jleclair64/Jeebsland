# Blender: exports an action made on the knight's rig (Art/Knight/knight_rig.blend, built by knight_rig.py) as a clip file
# the game plays on the knight's joints: Content/Anim/<clip>.json.
#
# Run: blender -b <rig .blend> -P Tools/knight_clip_export.py -- <project dir> <action name> [<clip name>] [--loop]
#      blender -b Art/Knight/knight_rig.blend -P Tools/knight_clip_export.py -- <project dir> --test
#      (--test makes a small test action first: the sword arm lifts, the elbow bends and the wrist cocks, then all return)
#
# WHAT IS EXPORTED. For every joint and every frame: the joint's rotation relative to its parent, in GAME axes, as a
# quaternion [x, y, z, w]. The game's joints all have zero rotation at rest, so this is exactly what the game sets on that
# joint's pivot (SetRelativeRotation). How it is worked out, independent of how the Blender bones are rolled:
#   G(bone)  = posed armature-space rotation x inverse(rest armature-space rotation)   (how far the bone has turned)
#   local    = inverse(G(parent)) x G(bone)
#   game     = (-x, y, -z, w) of the Blender quaternion (the rig is the game mirrored in Y; see knight_rig.py)
# The root joint (BodyRoot) also gets its movement from rest, in game cm ("root"). Timeline markers become clip markers
# (in seconds), e.g. "hit_start", "hit_end", "step": the game reads gameplay timing from these, not from frame numbers.
#
# CHECK. Before writing, the joint positions are rebuilt from the exported numbers the way the game will (the game's joint
# positions and parenting, rotations in game axes) and compared with Blender's posed bones; the worst gap is printed.
import bpy, sys, os, json, math
from mathutils import Vector, Quaternion, Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
proj = argv[0]
test = "--test" in argv
loop = "--loop" in argv
names = [a for a in argv[1:] if not a.startswith("--")]
arm = bpy.data.objects["KnightRig"]
scene = bpy.context.scene

def to_game(v): return Vector((v.x, -v.y, v.z))
def q_to_game(q): return [-q.x, q.y, -q.z, q.w]

if test:
    act = bpy.data.actions.new("TestArmWave")
    arm.animation_data_create(); arm.animation_data.action = act
    keys = {   # bone: [(frame, axis, degrees)], axes in the bone's own frame
        "ArmPivotR": [(1, (1, 0, 0), 0), (15, (1, 0, 0), -70), (30, (1, 0, 0), 0)],
        "ElbowR":    [(1, (1, 0, 0), 0), (15, (1, 0, 0), 50), (30, (1, 0, 0), 0)],
        "WristR":    [(1, (0, 1, 0), 0), (15, (0, 1, 0), 40), (30, (0, 1, 0), 0)],
        "UpperPivot":[(1, (0, 1, 0), 0), (15, (0, 1, 0), 25), (30, (0, 1, 0), 0)],
    }
    for bn, ks in keys.items():
        pb = arm.pose.bones[bn]; pb.rotation_mode = 'QUATERNION'
        for f, ax, deg in ks:
            pb.rotation_quaternion = Quaternion(ax, math.radians(deg))
            pb.keyframe_insert("rotation_quaternion", frame=f)
    scene.frame_start, scene.frame_end = 1, 30
    scene.timeline_markers.new("hit_start", frame=13); scene.timeline_markers.new("hit_end", frame=17)
    action_name, clip_name = "TestArmWave", "test_arm_wave"
else:
    action_name = names[0]
    clip_name = names[1] if len(names) > 1 else action_name
    act = bpy.data.actions[action_name]
    arm.animation_data_create(); arm.animation_data.action = act
    f0, f1 = act.frame_range
    scene.frame_start, scene.frame_end = int(f0), int(f1)

fps = scene.render.fps / scene.render.fps_base
bones = [b for b in arm.data.bones]                     # (parents always come before their children)
order = sorted(bones, key=lambda b: len(b.parent_recursive))
joint_names = [b.name for b in order]
rest_q = {b.name: b.matrix_local.to_quaternion() for b in order}
rest_head = {b.name: b.head_local.copy() for b in order}

frames, roots, worst = [], [], 0.0
for f in range(scene.frame_start, scene.frame_end + 1):
    scene.frame_set(f)
    G = {}
    for b in order:
        pb = arm.pose.bones[b.name]
        G[b.name] = pb.matrix.to_quaternion() @ rest_q[b.name].inverted()
    row = []
    for b in order:
        local = (G[b.parent.name].inverted() @ G[b.name]) if b.parent else G[b.name]
        row.append([round(c, 6) for c in q_to_game(local.normalized())])
    root = order[0]
    off = to_game(arm.pose.bones[root.name].matrix.translation - rest_head[root.name])
    frames.append(row); roots.append([round(off.x, 4), round(off.y, 4), round(off.z, 4)])
    # the check: rebuild every joint's position as the game will, and compare
    world_q, world_p = {}, {}
    for i, b in enumerate(order):
        x, y, z, w = row[i]
        lq = Quaternion((w, x, y, z))   # (a game-axes quaternion, used here in game space)
        hp = to_game(rest_head[b.name])
        if b.parent:
            pq, pp = world_q[b.parent.name], world_p[b.parent.name]
            world_q[b.name] = pq @ lq
            world_p[b.name] = pp + pq @ (hp - to_game(rest_head[b.parent.name]))
        else:
            world_q[b.name] = lq
            world_p[b.name] = hp + off
        blender_p = to_game(arm.pose.bones[b.name].head)
        worst = max(worst, (world_p[b.name] - blender_p).length)

markers = {m.name: round((m.frame - scene.frame_start) / fps, 4) for m in scene.timeline_markers}
clip = {
    "version": 1, "name": clip_name, "fps": fps, "frames": len(frames),
    "duration": round((len(frames) - 1) / fps, 4), "loop": loop,
    "joints": joint_names, "markers": markers, "root": roots, "rot": frames,
}
out_dir = os.path.join(proj, "Content", "Anim"); os.makedirs(out_dir, exist_ok=True)
path = os.path.join(out_dir, clip_name + ".json")
with open(path, "w") as fh: json.dump(clip, fh, separators=(",", ":"))
print("CLIP %s: %d frames at %g fps, %d joints, markers %s" % (clip_name, len(frames), fps, len(joint_names), markers))
print("CLIP check: worst joint position gap between Blender and the game rebuild = %.4f cm" % worst)
print("CLIP written %s" % path)
