# Blender (background): the stag as Meshy rigged it (Docs/DeerPlan.md) -> FBX files for Unreal (deer_import.py).
#   blender -b --factory-startup -P Tools/deer_export.py -- <name> <body .glb> <clips .glb> <out dir> [<Key>=<action> ...] [sheets=<dir>]
# Writes <out>/SK_<name>.fbx (mesh + rig), T_<name>_Base.png, T_<name>_Normal.png, A_<name>_<Key>.fbx per clip, and prints DEER
# lines: triangles, size, each clip's length, how far the root travels and the planted hoof's speed (the game plays walk and run at
# real speed / this, so hooves don't slide). sheets=<dir>: six side-on frames of every clip, for a look before Unreal.
#
# Everything is turned into centimetres with nothing left on the armature object before it is written (norm below). Written the
# usual way (metres, the x100 and the turn to Unreal's axes on the armature object) Unreal folded that object into the body's root
# bone but not into the clips' root track: every clip played the stag 100 times too small, a speck in the grass (2026-11-22).
import bpy, sys, os, math
import numpy as np
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
NAME, RIG, CLIPFILE, OUT = argv[0], argv[1], argv[2], argv[3]
CLIPS = [c for c in argv[4:] if not c.startswith("sheets=")]
SHEETS = next((c[7:] for c in argv[4:] if c.startswith("sheets=")), None)
os.makedirs(OUT, exist_ok=True)
HOOVES = ("frontToe.R", "frontToe.L", "rearToe.R", "rearToe.L")


def all_fcurves(act):
    try:
        out = []
        for layer in act.layers:
            for strip in layer.strips:
                for bag in strip.channelbags: out.extend(bag.fcurves)
        if out: return out
    except Exception: pass
    return list(act.fcurves)


def norm(arm, meshes, acts):
    # centimetres, identity objects: the object transforms and a x100 go into the mesh and bone data, bone location keys x100
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'; sc.unit_settings.scale_length = 0.01
    S = Matrix.Scale(100.0, 4)
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        o.data.transform(S @ mw)
        o.matrix_world = Matrix.Identity(4)
    arm.data.transform(S @ arm.matrix_world)
    arm.matrix_world = Matrix.Identity(4)
    for o in meshes:
        o.parent = arm; o.matrix_parent_inverse = Matrix.Identity(4)
    for act in acts:
        for fc in all_fcurves(act):
            if fc.data_path.endswith("location"):
                for kp in fc.keyframe_points:
                    kp.co.y *= 100.0; kp.handle_left.y *= 100.0; kp.handle_right.y *= 100.0
                fc.update()
    bpy.context.view_layer.update()


def fresh(path, keep_mesh, action=None):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    for o in list(bpy.data.objects):
        if o.type == 'MESH' and (not keep_mesh or not any(m.type == 'ARMATURE' for m in o.modifiers)):
            bpy.data.objects.remove(o)   # (Meshy's stray icosphere; a clip needs no mesh)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.name = "Armature"   # (Unreal drops a root node with this name)
    act = None
    if action:
        act = bpy.data.actions.get(action)
        if act is None: raise RuntimeError("DEER no action %s in %s: %s" % (action, path, [a.name for a in bpy.data.actions]))
    for a_ in list(bpy.data.actions):
        if a_ != act: bpy.data.actions.remove(a_)
    if arm.animation_data: arm.animation_data.action = None
    norm(arm, [o for o in bpy.data.objects if o.type == 'MESH'], [act] if act else [])
    for pb in arm.pose.bones:   # (the body goes out in its rest pose)
        pb.location = (0, 0, 0); pb.rotation_quaternion = (1, 0, 0, 0); pb.rotation_euler = (0, 0, 0); pb.scale = (1, 1, 1)
    if act:
        arm.animation_data_create(); arm.animation_data.action = act
        try:
            if act.slots: arm.animation_data.action_slot = act.slots[0]
        except Exception: pass
    return arm, act


def export(path, objs, anim):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    # (axis_forward Y, axis_up Z: Blender's own axes, so the exporter adds no turn to the armature object and the file says Z is up)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=anim, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                             apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                             axis_forward="Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                             use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True, path_mode='STRIP')


# ---- The body
arm, _ = fresh(RIG, True)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in meshes:
    if not o.data.has_custom_normals: o.data.shade_smooth()
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in meshes)
P = [(o.matrix_world @ v.co) for o in meshes for v in o.data.vertices]
print("DEER %s mesh tris %d, bones %d, size x %.1f y %.1f z %.1f cm (Blender: it faces -Y), root bone at %s" % (NAME, tris, len(arm.data.bones),
      max(p.x for p in P) - min(p.x for p in P), max(p.y for p in P) - min(p.y for p in P), max(p.z for p in P) - min(p.z for p in P),
      [round(x, 1) for x in arm.data.bones["root"].head_local]))
for o in meshes:
    for m in o.data.materials:
        if not (m and m.use_nodes): continue
        pr = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        for sock, tag in (("Base Color", "Base"), ("Normal", "Normal")):
            if not pr.inputs[sock].links: continue
            n = pr.inputs[sock].links[0].from_node
            if n.type == 'NORMAL_MAP' and n.inputs['Color'].links: n = n.inputs['Color'].links[0].from_node
            if n.type == 'TEX_IMAGE' and n.image:
                n.image.filepath_raw = os.path.join(OUT, "T_%s_%s.png" % (NAME, tag)); n.image.file_format = 'PNG'; n.image.save()
                print("DEER %s picture %s %dx%d" % (NAME, tag, n.image.size[0], n.image.size[1]))
        m.name = "M_" + NAME
export(os.path.join(OUT, "SK_%s.fbx" % NAME), [arm] + meshes, False)

# ---- The clips
for c in CLIPS:
    key, action = c.split("=", 1)
    arm, act = fresh(CLIPFILE, bool(SHEETS), action)
    sc = bpy.context.scene
    # (glTF times are seconds; the importer laid them on the scene's 24 frames a second, the run ending on frame 22.4. Stretched onto 30 a
    # second every clip ends on a whole frame, so loops close cleanly in Unreal)
    k30 = 30.0 / (sc.render.fps / sc.render.fps_base)
    for fc in all_fcurves(act):
        for kp in fc.keyframe_points:
            kp.co.x *= k30; kp.handle_left.x *= k30; kp.handle_right.x *= k30
        fc.update()
    sc.render.fps = 30; sc.render.fps_base = 1.0
    f0, f1 = int(round(act.frame_range[0])), int(round(act.frame_range[1]))
    sc.frame_start, sc.frame_end = f0, f1
    fps = 30.0
    hoof = {s: [] for s in HOOVES if s in arm.pose.bones}
    root = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        for s in hoof: hoof[s].append(np.array(arm.matrix_world @ arm.pose.bones[s].head))
        root.append(np.array(arm.matrix_world @ arm.pose.bones["root"].head))
    root = np.array(root)
    H = {s: np.array(v) for s, v in hoof.items()}
    v = []
    for i in range(1, len(root)):   # the planted hoof's speed: each frame, the lowest hoof's slide
        s = min(H, key=lambda k: H[k][i, 2])
        v.append(np.linalg.norm((H[s][i] - H[s][i - 1])[:2]) * fps)
    low = min(float(a[:, 2].min()) for a in H.values())
    print("DEER %s clip %s (%s) %.2f s (%d frames at 30), planted hoof speed %.1f cm/s, root %.1f cm up at the start, moved %.1f cm (start to end) and wandered %.1f cm, lowest hoof %.1f cm" % (
        NAME, key, action, (f1 - f0) / fps, f1 - f0, float(np.median(v)), float(root[0, 2]), float(np.linalg.norm((root[-1] - root[0])[:2])),
        float(np.abs(root[:, :2] - root[0, :2]).max()), low))
    if SHEETS:
        os.makedirs(SHEETS, exist_ok=True)
        sc.frame_set(f1)
        dg = bpy.context.evaluated_depsgraph_get()
        zs = [(o.evaluated_get(dg).matrix_world @ v_.co).z for o in bpy.data.objects if o.type == 'MESH' for v_ in o.evaluated_get(dg).to_mesh().vertices]
        print("DEER %s clip %s last frame: lowest point %.1f cm, highest %.1f cm" % (NAME, key, min(zs), max(zs)))
        sc.render.engine = "BLENDER_EEVEE"; sc.render.resolution_x = 420; sc.render.resolution_y = 360
        sc.world = bpy.data.worlds.new("w"); sc.world.use_nodes = True
        sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.8, 0.8, 0.82, 1)
        l = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); l.data.energy = 3; l.rotation_euler = (0.8, 0.2, 0.6); sc.collection.objects.link(l)
        cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); sc.collection.objects.link(cam); sc.camera = cam
        cam.data.type = "ORTHO"; cam.data.ortho_scale = 300.0; cam.data.clip_end = 5000.0
        D = Vector((1.0, -0.35, 0.12)).normalized(); cam.location = Vector((0, 0, 95.0)) + D * 800
        cam.rotation_euler = D.to_track_quat("Z", "Y").to_euler()
        for k in range(6):
            sc.frame_set(f0 + int(round((f1 - f0) * k / 6.0)))
            sc.render.filepath = os.path.join(SHEETS, "%s_%d.png" % (key, k))
            bpy.ops.render.render(write_still=True)
        for o in [o for o in bpy.data.objects if o.type == 'MESH']: bpy.data.objects.remove(o)
    act.name = "A_%s_%s" % (NAME, key)
    export(os.path.join(OUT, "A_%s_%s.fbx" % (NAME, key)), [arm], True)
print("DEER %s done" % NAME)
