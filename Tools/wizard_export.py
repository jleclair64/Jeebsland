# The wizard for Unreal (2026-11-21, agent wizard): his two Meshy bodies (the plain-clothes base and the robed one, each rigged by Meshy), his hat and his
# spell book, as ONE skinned mesh on the KNIGHT's own 24-bone skeleton, so he has exactly the knight's dimensions and plays the knight's moves as they are.
#
# How a Meshy body gets onto the knight's bones: Meshy's rig has the same 24 bones under other names (as the troll's, Tools/troll_rig_export.py). Each point is
# moved by its bones' offsets (knight joint minus wizard joint, eased along each bone toward the next joint, mixed by the point's weights), which lays the
# wizard's joints exactly on the knight's; then the weights are renamed to the knight's bones.
#
# Material slots (FKnightAnimator shows and hides them, KnightAnimatorWizard.cpp):
#   Wiz_BaseUpper  the plain-clothes man above the boot line (his "naked" look)      Wiz_BaseFeet  his socks and ankle boots
#   Wiz_RobeUpper  the robed man above the boot line (worn robe: replaces BaseUpper) Wiz_RobeFeet  the tall wizard boots (worn boots: replaces BaseFeet)
#   Wiz_HatBase / Wiz_HatRobe    the hat, fitted to each head       Wiz_BookBase / Wiz_BookRobe   the spell book on his belt, fitted to each outfit
# A worn piece replaces what is under it rather than covering it, so nothing can show through.
# Writes <out>\SK_Wizard.fbx, T_Wiz_<Base|Robe|Hat|Book>_<Base|MR|Normal>.png and preview pictures wizard_<look>_<side>.png.
# Run: blender -b -P wizard_export.py -- <rigged knight.glb> <Art\Wizard folder> <output folder>
import bpy, sys, os, math
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
KNIGHT, ART, OUT = a[0], a[1], a[2]
os.makedirs(OUT, exist_ok=True)
TEX = 2048
ZCUT = {"Base": 0.345, "Robe": 0.345}    # (m) the boot line on each body: below it is the Feet slot
HAT_WIDTH = 0.80      # (m) the brim's width on his head
HAT_DROP = 0.105      # (m) how far the brim's plane sits under the top of his head
HAT_YAW = 0.0         # (degrees) turns the hat about the upright
HAT_TILT = -8.0       # (degrees) tips it back
BOOK_LONG = 0.25      # (m) the book's long side
BOOK_AT = 140.0       # (degrees round his waist from straight ahead, toward his left: behind the left hip, clear of the swinging arm)
BOOK_Z = 0.90         # (m) its middle's height
# Meshy's bone -> the knight's (without "mixamorig:")
NAME = {"Spine02": "Spine", "Spine01": "Spine1", "Spine": "Spine2", "neck": "Neck", "head_end": "HeadTop_End"}
NEXT = {"Hips": "Spine", "Spine": "Spine1", "Spine1": "Spine2", "Spine2": "Neck", "Neck": "Head"}
for s in ("Left", "Right"):
    NEXT.update({s + "Shoulder": s + "Arm", s + "Arm": s + "ForeArm", s + "ForeArm": s + "Hand", s + "UpLeg": s + "Leg", s + "Leg": s + "Foot", s + "Foot": s + "ToeBase"})

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=KNIGHT)
arm = next(x for x in bpy.data.objects if x.type == "ARMATURE")
for x in [o for o in bpy.data.objects if o.type == "MESH"]: bpy.data.objects.remove(x)
for act in list(bpy.data.actions): bpy.data.actions.remove(act)
arm.name = "Armature"
arm.data.pose_position = "REST"; bpy.context.view_layer.update()
K = {b.name.replace("mixamorig:", ""): arm.matrix_world @ b.head_local for b in arm.data.bones}

def tex_kind(mat, node):
    for l in mat.node_tree.links:
        if l.from_node != node: continue
        if l.to_socket.name == "Base Color": return "Base"
        if l.to_node.type == "NORMAL_MAP": return "Normal"
        if l.to_node.type in ("SEPARATE_COLOR", "SEPRGB", "SEPARATE_RGB") or l.to_socket.name in ("Metallic", "Roughness"): return "MR"
    return None

def save_pics(ob, tag):
    mat = ob.material_slots[0].material
    for n in mat.node_tree.nodes:
        if n.type != "TEX_IMAGE" or not n.image: continue
        kind = tex_kind(mat, n)
        if not kind: continue
        c = n.image.copy(); c.scale(TEX, TEX)
        c.filepath_raw = os.path.join(OUT, "T_Wiz_%s_%s.png" % (tag, kind)); c.file_format = "PNG"; c.save()
        bpy.data.images.remove(c)
    return mat

def flatten(ob):
    """The mesh in world space, with no parent."""
    M = ob.matrix_world.copy()
    ob.parent = None
    for m in list(ob.modifiers): ob.modifiers.remove(m)
    ob.data.transform(M); ob.matrix_world = Matrix.Identity(4)
    ob.data.update()

def bind(ob):
    ob.data.transform(arm.matrix_world.inverted()); ob.data.update()   # (the knight rig carries a scale: its meshes live in its own space)
    ob.parent = arm; ob.matrix_parent_inverse.identity(); ob.matrix_basis.identity()
    m = ob.modifiers.new("Armature", "ARMATURE"); m.object = arm

def split_slots(ob, mat, names, pick):
    ob.data.materials.clear()
    for n in names:
        m = mat.copy(); m.name = n; ob.data.materials.append(m)
    cnt = [0] * len(names)
    for p in ob.data.polygons:
        p.material_index = pick(p); cnt[p.material_index] += 1
    print("WIZ slots", dict(zip(names, cnt)))

def body(tag):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ART, "wizard_%s_rigged.glb" % ("base" if tag == "Base" else "robed")))
    new = [o for o in bpy.data.objects if o not in before]
    rig = next(o for o in new if o.type == "ARMATURE")
    ob = max((o for o in new if o.type == "MESH"), key=lambda o: len(o.data.vertices))
    rig.data.pose_position = "REST"; bpy.context.view_layer.update()
    W = {NAME.get(b.name, b.name): rig.matrix_world @ b.head_local for b in rig.data.bones}
    miss = [k for k in K if k not in W]
    if miss: print("WIZ", tag, "bones the Meshy rig lacks:", miss)
    flatten(ob)
    d = {k: K[k] - W[k] for k in K if k in W}
    print("WIZ", tag, "largest joint move (cm):", max((round(v.length * 100, 1), k) for k, v in d.items()))
    gname = {g.index: NAME.get(g.name, g.name) for g in ob.vertex_groups}
    for v in ob.data.vertices:
        off = Vector(); tot = 0.0
        for g in v.groups:
            b = gname[g.group]
            if b not in d or g.weight <= 0: continue
            o = d[b]; c = NEXT.get(b)
            if c and c in d:
                ab = W[c] - W[b]
                t = max(0.0, min(1.0, (v.co - W[b]).dot(ab) / max(ab.dot(ab), 1e-9)))
                o = d[b].lerp(d[c], t)
            off += o * g.weight; tot += g.weight
        if tot > 0: v.co += off / tot
    for g in ob.vertex_groups: g.name = "mixamorig:" + NAME.get(g.name, g.name)
    for o in new:
        if o is not ob: bpy.data.objects.remove(o)
    mat = save_pics(ob, tag)
    zc = ZCUT[tag]
    # a clean edge at the boot line: the mesh is cut there, so the two slots part along a level ring, not a ragged row of triangles
    import bmesh
    bm = bmesh.new(); bm.from_mesh(ob.data)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-5, plane_co=(0, 0, zc), plane_no=(0, 0, 1))
    bm.to_mesh(ob.data); bm.free(); ob.data.update()
    split_slots(ob, mat, ["Wiz_%sUpper" % tag, "Wiz_%sFeet" % tag], lambda p: 1 if p.center.z < zc else 0)
    ob.name = "Wiz" + tag
    return ob

def prop(file, tag):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ART, file))
    new = [o for o in bpy.data.objects if o not in before]
    ob = max((o for o in new if o.type == "MESH"), key=lambda o: len(o.data.vertices))
    flatten(ob)
    for o in new:
        if o is not ob: bpy.data.objects.remove(o)
    save_pics(ob, tag)
    return ob

def weight_all(ob, bone):
    g = ob.vertex_groups.new(name="mixamorig:" + bone)
    g.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")

def head_of(bd):
    """The top of the head and its middle, from the points that follow the head bone."""
    gi = bd.vertex_groups["mixamorig:Head"].index
    P = [v.co for v in bd.data.vertices if any(g.group == gi and g.weight > 0.6 for g in v.groups) and v.co.z > K["Head"].z + 0.10]
    top = max(p.z for p in P)
    crown = [p for p in P if p.z > top - 0.06]
    return top, sum((p.x for p in crown)) / len(crown), sum((p.y for p in crown)) / len(crown)

def hat_for(src, bd, tag):
    ob = src.copy(); ob.data = src.data.copy(); bpy.context.scene.collection.objects.link(ob)
    me = ob.data
    r = max(math.hypot(v.co.x, v.co.y) for v in me.vertices)
    rim = sorted(v.co.z for v in me.vertices if math.hypot(v.co.x, v.co.y) > 0.8 * r)
    brim_z = rim[len(rim) // 2]
    top, cx, cy = head_of(bd)
    s = HAT_WIDTH / (2 * r)
    M = Matrix.Translation(Vector((cx, cy + 0.01, top - HAT_DROP))) @ Matrix.Rotation(math.radians(HAT_TILT), 4, "X") @ Matrix.Rotation(math.radians(HAT_YAW), 4, "Z") @ Matrix.Scale(s, 4) @ Matrix.Translation(Vector((0, 0, -brim_z)))
    me.transform(M); me.update()
    me.materials[0] = me.materials[0].copy(); me.materials[0].name = "Wiz_Hat" + tag
    weight_all(ob, "Head"); ob.name = "WizHat" + tag
    print("WIZ hat", tag, "head top %.3f, scale %.3f" % (top, s))
    return ob

def book_for(src, bd, tag):
    ob = src.copy(); ob.data = src.data.copy(); bpy.context.scene.collection.objects.link(ob)
    me = ob.data
    lo = Vector((min(v.co[i] for v in me.vertices) for i in range(3))); hi = Vector((max(v.co[i] for v in me.vertices) for i in range(3)))
    size = hi - lo; mid = (lo + hi) / 2
    order = sorted(range(3), key=lambda i: size[i])   # thin, short, long
    s = BOOK_LONG / size[order[2]]
    # the book's frame: its long side up, its thin side out from his body
    ang = math.radians(BOOK_AT)
    out = Vector((math.sin(ang), -math.cos(ang), 0.0))   # (his front is -Y, his left +X)
    side = Vector((0, 0, 1)).cross(out).normalized()
    ax = [Vector(), Vector(), Vector()]
    ax[order[0]] = out; ax[order[2]] = Vector((0, 0, 1)); ax[order[1]] = side
    R = Matrix((ax[0], ax[1], ax[2])).transposed().to_4x4()
    if R.determinant() < 0:
        ax[order[1]] = -side; R = Matrix((ax[0], ax[1], ax[2])).transposed().to_4x4()
    # how far out his body reaches there (hips, spine and thigh points only: not the arm)
    names = {g.index: g.name for g in bd.vertex_groups}
    reach = 0.0
    for v in bd.data.vertices:
        if abs(v.co.z - BOOK_Z) > 0.07: continue
        w = sum(g.weight for g in v.groups if any(k in names[g.group] for k in ("Hips", "Spine", "UpLeg")))
        if w < 0.9: continue
        p = Vector((v.co.x - K["Hips"].x, v.co.y - K["Hips"].y, 0.0))
        if p.length > 1e-4 and p.normalized().dot(out) > 0.9: reach = max(reach, p.dot(out))
    at = Vector((K["Hips"].x, K["Hips"].y, BOOK_Z)) + out * (reach + size[order[0]] * s * 0.5 - 0.012)
    me.transform(Matrix.Translation(at) @ R @ Matrix.Scale(s, 4) @ Matrix.Translation(-mid)); me.update()
    me.materials[0] = me.materials[0].copy(); me.materials[0].name = "Wiz_Book" + tag
    weight_all(ob, "Hips"); ob.name = "WizBook" + tag
    print("WIZ book", tag, "body reaches %.3f, scale %.3f" % (reach, s))
    return ob

base = body("Base")
robe = body("Robe")
hat_src = prop("wizard_hat.glb", "Hat")
book_src = prop("wizard_book.glb", "Book")
parts = [base, robe, hat_for(hat_src, base, "Base"), hat_for(hat_src, robe, "Robe"), book_for(book_src, base, "Base"), book_for(book_src, robe, "Robe")]
bpy.data.objects.remove(hat_src); bpy.data.objects.remove(book_src)
for p in parts: bind(p)
bpy.context.view_layer.update()
print("WIZ triangles", {p.name: len(p.data.polygons) for p in parts})

# ---- preview pictures: each look from the front, the side, the back and a corner ----
def pictures():
    s = bpy.context.scene
    s.render.engine = "BLENDER_EEVEE"; s.render.resolution_x = 520; s.render.resolution_y = 760
    s.world = bpy.data.worlds.new("w"); s.world.use_nodes = True
    s.world.node_tree.nodes["Background"].inputs[0].default_value = (0.8, 0.8, 0.82, 1); s.world.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    l = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); l.data.energy = 3; l.rotation_euler = (0.8, 0.2, 0.6); s.collection.objects.link(l)
    cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); s.collection.objects.link(cam); s.camera = cam
    cam.data.type = "ORTHO"; cam.data.ortho_scale = 2.3
    mid = Vector((0, 0, 1.0))
    looks = {"naked": ("Wiz_BaseUpper", "Wiz_BaseFeet"), "full": ("Wiz_RobeUpper", "Wiz_RobeFeet", "Wiz_HatRobe", "Wiz_BookRobe"),
             "robe_only": ("Wiz_RobeUpper", "Wiz_BaseFeet"), "hat_boots_book": ("Wiz_BaseUpper", "Wiz_RobeFeet", "Wiz_HatBase", "Wiz_BookBase")}
    hidden = []
    for name, on in looks.items():
        tmp = []
        for p in parts:
            c = p.copy(); c.data = p.data.copy(); s.collection.objects.link(c); tmp.append(c)
            keep = [i for i, m in enumerate(c.data.materials) if m.name.split(".")[0] in on]
            import bmesh
            bm = bmesh.new(); bm.from_mesh(c.data)
            bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index not in keep], context="FACES")
            bm.to_mesh(c.data); bm.free()
        for p in parts: p.hide_render = True
        for n, dv in (("front", (0, -1, 0)), ("side", (1, 0, 0)), ("back", (0, 1, 0)), ("corner", (-0.6, 0.7, 0.4))):
            D = Vector(dv).normalized(); cam.location = mid + D * 6
            cam.rotation_euler = D.to_track_quat("Z", "Y").to_euler()
            s.render.filepath = os.path.join(OUT, "wizard_%s_%s.png" % (name, n))
            bpy.ops.render.render(write_still=True)
        for c in tmp: bpy.data.objects.remove(c)
        for p in parts: p.hide_render = False
    bpy.data.objects.remove(cam); bpy.data.objects.remove(l)
if os.environ.get("WIZ_NOPICS") != "1": pictures()

bpy.ops.object.select_all(action="DESELECT")
for x in [arm] + parts: x.select_set(True)
bpy.context.view_layer.objects.active = arm
fbx = os.path.join(OUT, "SK_Wizard.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False, bake_anim=False,
                         apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="OFF", colors_type="NONE",
                         use_armature_deform_only=False, primary_bone_axis="Y", secondary_bone_axis="X", use_tspace=True)
print("WIZ wrote", fbx, os.path.getsize(fbx) // 1024, "KB")
