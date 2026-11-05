# Puts the new suit together for Unreal (2026-11-05): the rigged chain mail knight with his clips, plus every fitted piece from
# Saved\KnightBuild\Pieces (Tools\BuildNewSuit.ps1), as one skinned mesh on his 24-bone skeleton, one material slot per piece.
# Writes Saved\KnightBuild\Suit\SK_NewSuit.fbx and each piece's two pictures (T_<Piece>_Base.png, T_<Piece>_MR.png, 2048 x 2048).
# Run through Blender: blender -b -P knight_suit_export.py -- <rigged knight.glb> <pieces folder> <output folder>
import bpy, bmesh, sys, os
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index("--") + 1:]
rig, pieces, out = a[0], a[1], a[2]
os.makedirs(out, exist_ok=True)
PIECES = [("chest", "Chest"), ("legs", "Legs"), ("helmet", "Helmet"), ("gauntlets", "Gauntlets"), ("shoulders", "Shoulders")]
TEX = 2048           # each piece has its own picture, so this is sharper than the old whole-knight 4096
TORSO_INSET = 0.015  # (metres) the torso's mail is kept whole, or the armholes show through him, and is drawn this far in so it can't poke through

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=rig)
for x in list(bpy.data.objects):
    if x.name.startswith("Icosphere"): bpy.data.objects.remove(x)
arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
body = next(x for x in bpy.data.objects if x.type == "MESH")
arm.name = "Armature"
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
print("SUIT clips", [(x.name, tuple(int(f) for f in x.frame_range)) for x in bpy.data.actions])

def save_pics(ob, tag):
    mat = ob.material_slots[0].material
    ims = [n.image for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]
    for im in ims:
        kind = "MR" if "metallic" in im.name else "Base"
        c = im.copy(); c.scale(TEX, TEX)
        c.filepath_raw = os.path.join(out, "T_%s_%s.png" % (tag, kind)); c.file_format = "PNG"; c.save()
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
save_pics(body, "Mail")
bpy.context.view_layer.update()

# ---- the mail under the plate: what is covered is hidden (it could only poke through), except the torso (see TORSO_INSET)
trees = []
for pl in plates:
    wp = pl.matrix_world
    trees.append(BVHTree.FromPolygons([(wp @ v.co)[:] for v in pl.data.vertices], [list(p.vertices) for p in pl.data.polygons]))
wb = body.matrix_world; wbi = wb.inverted()
names = {g.index: g.name.replace("mixamorig:", "") for g in body.vertex_groups}
TORSO = {"Hips", "Spine", "Spine1", "Spine2", "Neck", "LeftShoulder", "RightShoulder"}
HAND = {"LeftHand", "RightHand"}
cov = []; torso = []; hand = []
for v in body.data.vertices:
    co = wb @ v.co; n = (wb.to_3x3() @ v.normal).normalized()
    cov.append(any(t.ray_cast(co, n, 0.09)[0] is not None for t in trees) or any(t.ray_cast(co, -n, 0.06)[0] is not None for t in trees))
    ws = {}
    for g in v.groups: ws[names[g.group]] = g.weight
    torso.append(sum(w for k, w in ws.items() if k in TORSO) > 0.5)
    hand.append(sum(w for k, w in ws.items() if k in HAND) > 0.3)
sc_ = wb.to_scale()[0]
n_in = 0
for i, v in enumerate(body.data.vertices):
    if torso[i] and cov[i]:
        v.co -= v.normal * (TORSO_INSET / sc_); n_in += 1
bm = bmesh.new(); bm.from_mesh(body.data); bm.verts.ensure_lookup_table()
def hide(f):
    idx = [v.index for v in f.verts]
    if sum(hand[i] for i in idx) * 2 > len(idx): return True   # (a bare hand can never poke out of a gauntlet)
    if sum(torso[i] for i in idx) * 2 > len(idx): return False
    return sum(cov[i] for i in idx) * 2 > len(idx)
gone = [f for f in bm.faces if hide(f)]
bmesh.ops.delete(bm, geom=gone, context="FACES")
bm.to_mesh(body.data); bm.free(); body.data.update()
print("SUIT mail: %d faces hidden, %d left; %d torso points set in" % (len(gone), len(body.data.polygons), n_in))
tot = len(body.data.polygons) + sum(len(p.data.polygons) for p in plates)
print("SUIT triangles", tot, {p.name: len(p.data.polygons) for p in plates})

# ---- the file
arm.data.pose_position = "POSE"
bpy.ops.object.select_all(action="DESELECT")
for x in [arm, body] + plates: x.select_set(True)
bpy.context.view_layer.objects.active = arm
if arm.animation_data is None: arm.animation_data_create()
fbx = os.path.join(out, "SK_NewSuit.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                         bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=True,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("SUIT wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
