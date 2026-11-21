# The Dark Plate set for Unreal (2026-11-21): every fitted piece from Saved\KnightBuild\DarkPieces (Tools\BuildDarkPlate.ps1) as one skinned mesh on the
# knight's 24-bone skeleton, one material slot per piece (slot names as SK_NewSuit's, so the game finds each piece the same way). No mail body and no clips:
# it is drawn as a follower of the suit (FKnightAnimator::MakeMesh), and his mail under the plate is SK_NewSuitUnder, as for every other armour.
# Writes Saved\KnightBuild\DarkPlate\SK_DarkPlate.fbx and each piece's pictures (T_DP_<Piece>_Base / _MR / _Normal.png, 2048 x 2048).
# Run through Blender: blender -b -P knight_darkplate_export.py -- <rigged knight.glb> <pieces folder> <output folder>
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.dont_write_bytecode = True

a = sys.argv[sys.argv.index("--") + 1:]
rig, pieces, out = a[0], a[1], a[2]
os.makedirs(out, exist_ok=True)
PIECES = [("chest", "Chest"), ("legs", "Legs"), ("helmet", "Helmet"), ("gauntlets", "Gauntlets"), ("shoulders", "Shoulders")]
TEX = 2048

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig)
for x in list(bpy.data.objects):
    if x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
body = next(x for x in bpy.data.objects if x.type == "MESH")
arm.name = "Armature"
arm.data.pose_position = "REST"; bpy.context.view_layer.update()

def save_pics(ob, tag):
    mat = ob.material_slots[0].material
    for im in [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]:
        kind = "MR" if "metallic" in im.name else "Normal" if "normal" in im.name else "Base"
        c = im.copy(); c.scale(TEX, TEX)
        c.filepath_raw = os.path.join(out, "T_DP_%s_%s.png" % (tag, kind)); c.file_format = "PNG"; c.save()
        bpy.data.images.remove(c)
    mat.name = "Suit_" + tag

plates = []
for fn, tag in PIECES:
    with bpy.data.libraries.load(os.path.join(pieces, fn + "_clean.blend")) as (src, dst):
        dst.objects = [fn]
    ob = dst.objects[0]; bpy.context.scene.collection.objects.link(ob)
    ob.parent = arm; ob.matrix_parent_inverse.identity(); ob.matrix_basis.identity()
    for m in ob.modifiers:
        if m.type == "ARMATURE": m.object = arm
    save_pics(ob, tag); plates.append(ob)
bpy.context.view_layer.update()

# the fists, as on the Classic gauntlets (knight_fist.py: FistR / FistL morph targets, set per hand by FKnightAnimator)
if os.environ.get("DP_NOFIST") != "1":
    import knight_fist
    knight_fist.make_fists(next(p for p in plates if p.name == "gauntlets"), arm, bare=False, log=lambda s: print("DPX " + s))
bpy.data.objects.remove(body)
print("DPX triangles", sum(len(p.data.polygons) for p in plates), {p.name: len(p.data.polygons) for p in plates})

bpy.ops.object.select_all(action="DESELECT")
for x in [arm] + plates: x.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(out, "SK_DarkPlate.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False, bake_anim=False,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("DPX wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
