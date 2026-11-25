# Blender (background): one clips file per animal from Meshy's downloads (2026-11-24, Docs/AnimalsPlan2.md).
#   blender -b --factory-startup -P Tools/animal_merge_clips.py -- <merged_animations.glb> <out.glb> [<extra clip .glb>=<NewName> ...]
# Meshy's rigging hands back a free dog walk and run (each its own skinned file) and the paid clips come in one merged file; this puts
# every action on the merged file's armature as NLA tracks, so Tools/deer_export.py reads them like the stag's clips file.
import bpy, sys
a = sys.argv[sys.argv.index("--") + 1:]
MAIN, OUT, EXTRA = a[0], a[1], a[2:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=MAIN)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
keep = set(bpy.data.objects)
acts = list(bpy.data.actions)
for e in EXTRA:
    path, nm = e.rsplit("=", 1)
    before = set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [x for x in bpy.data.actions if x not in before]
    if new: new[0].name = nm; acts.append(new[0])
    for x in new[1:]: bpy.data.actions.remove(x)
    for o in [o for o in bpy.data.objects if o not in keep]: bpy.data.objects.remove(o)
arm.animation_data_create()
for t in list(arm.animation_data.nla_tracks): arm.animation_data.nla_tracks.remove(t)
for act in acts:
    tr = arm.animation_data.nla_tracks.new(); tr.name = act.name
    tr.strips.new(act.name, int(act.frame_range[0]), act); tr.mute = True
    print("MERGE action", act.name, act.frame_range[:])
arm.animation_data.action = None
for o in [o for o in bpy.data.objects if o.type == "MESH" and not any(m.type == "ARMATURE" for m in o.modifiers)]: bpy.data.objects.remove(o)
bpy.ops.object.select_all(action="DESELECT")
for o in bpy.data.objects: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_animation_mode="NLA_TRACKS", export_image_format="AUTO")
print("MERGE wrote", OUT)
