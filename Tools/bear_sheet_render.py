import bpy, sys, math
from mathutils import Vector
a = sys.argv[sys.argv.index("--")+1:]
GLB, TAG, ACT = a[0], a[1], a[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
for o in list(bpy.data.objects):
    if o.name.startswith("Icosphere"): bpy.data.objects.remove(o)
A = next(o for o in bpy.data.objects if o.type=="ARMATURE")
act = bpy.data.actions[ACT]
A.animation_data_create(); A.animation_data.action = act
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "SINGLE"; sc.display.shading.single_color = (0.55,0.4,0.3)
sc.render.resolution_x = 1280; sc.render.resolution_y = 720
sc.render.film_transparent = False
sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.8,0.85,0.9)
cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); sc.collection.objects.link(cam); sc.camera = cam
cam.data.type = "ORTHO"; cam.data.ortho_scale = 4.2
cam.location = (8, 0, 0.9); cam.rotation_euler = (math.radians(90), 0, math.radians(90))
f0,f1 = [int(x) for x in act.frame_range]
n = 0
for f in range(f0, f1+1, max(1,(f1-f0)//12)):
    sc.frame_set(f); sc.render.filepath = "C:/Users/johnl/Documents/Unreal Projects/JeebsAgents/critters/Saved/bearsheet/%s_%03d.png" % (TAG, n); bpy.ops.render.render(write_still=True); n += 1
print("RENDERED", n)
