# Blender (background): renders the exported armour sets (Saved\KnightBuild\SK_Knight*.fbx) side by side, each slot in its
# own colour, from the front and the side, with lines at the skeleton's fist and elbow heights, to check the cuts and the arm
# fit before importing into Unreal.
# Run: blender -b -P Tools/knight_blender_compare.py -- <work dir>
import bpy, sys, os, math

work = sys.argv[sys.argv.index("--") + 1:][0]
bpy.ops.wm.read_factory_settings(use_empty=True)
SETS = [("Heavy", "SK_Knight.fbx"), ("Classic", "SK_Knight_Classic.fbx"), ("Light", "SK_Knight_Light.fbx"), ("Mail", "SK_Knight_Mail.fbx")]
SLOT_COL = {"Helm": (0.3, 0.8, 0.3), "Body": (0.85, 0.25, 0.25), "Shoulders": (0.95, 0.85, 0.2), "Boots": (0.25, 0.45, 0.95), "Gloves": (0.2, 0.85, 0.9)}
for i, (name, f) in enumerate(SETS):
    path = os.path.join(work, f)
    if not os.path.exists(path): continue
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    for o in set(bpy.data.objects) - before:
        o.location.x += i * 1.2 if o.parent is None else 0.0
        if o.type == 'MESH':
            for slot in o.material_slots:
                key = next((k for k in SLOT_COL if slot.material and slot.material.name.startswith(k)), None)
                if key:
                    m = bpy.data.materials.new("%s_%s" % (name, key)); m.diffuse_color = (*SLOT_COL[key], 1.0); slot.material = m
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.render.resolution_x, scene.render.resolution_y = 1600, 800
cam_data = bpy.data.cameras.new("Cam"); cam_data.type = 'ORTHO'; cam_data.ortho_scale = 5.0
cam = bpy.data.objects.new("Cam", cam_data); scene.collection.objects.link(cam); scene.camera = cam
# lines at the skeleton's fist (-37.8 cm) and elbow (-6 cm) in game space (Blender metres, Z up as exported)
for z in (-0.378, -0.06):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(1.8, 0, z)); c = bpy.context.active_object; c.scale = (5.0, 0.004, 0.004)
    m = bpy.data.materials.new("Line"); m.diffuse_color = (1, 1, 0, 1); c.data.materials.append(m)
for nm, loc, rot in [("front", (1.8, -6, -0.15), (math.radians(90), 0, 0)), ("side", (1.8, 0, -0.15), None)]:
    if nm == "side":
        # every set seen from its right side, one after another along the line
        for o in bpy.data.objects:
            if o.type == 'ARMATURE': o.rotation_euler.z = math.radians(90)
        loc, rot = (1.8, -6, -0.15), (math.radians(90), 0, 0)
    cam.location, cam.rotation_euler = loc, rot
    scene.render.filepath = os.path.join(work, "sets_%s.png" % nm)
    bpy.ops.render.render(write_still=True)
