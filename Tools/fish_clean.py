# Fish (2026-11-26, agent fish; from frog_clean.py): Meshy community models Art/Fish/<Name>.glb (Bass Trout Salmon Perch GoldenCarp) -> Saved/KnightBuild/Fish/<Name>.fbx + T_<Name>_Base/Normal/MR.png (then Tools/fish_import.py via Tools/BuildFish.ps1).
# Tools/worldprop_import.py via Tools/BuildLilyPads.ps1). Each is reduced to ~N triangles (pictures baked from the full model), laid on the XY plane,
# scaled to 100 cm across (the game scales it per pad) and given its origin at the underside's lowest point, middle of the pad.
# Run in Blender: blender -b -P Tools/lily_clean.py -- <Art/WorldProps> <out dir> [names]
import bpy, bmesh, os, sys, math
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)
FISH = {"Bass": 4000, "Trout": 4000, "Salmon": 4000, "Perch": 4000, "GoldenCarp": 4000}
SAT = 1.18   # a little richer than the photo-real library textures (hand-painted, not quite realistic)
LEVEL = {"GoldenCarp": 0.0}
names = a[2:] or list(FISH)

def tex_node(mat, sock_name):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    stack = [pr.inputs[sock_name]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(n.inputs)
    return None

def bake(o, hi, name, size=1024):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
    me = o.data
    old = [l.name for l in me.uv_layers]
    uvb = me.uv_layers.new(name="Bake"); me.uv_layers.active = uvb
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")
    hmat = hi.data.materials[0]
    tb = tex_node(hmat, "Base Color"); tn = tex_node(hmat, "Normal"); tm = tex_node(hmat, "Metallic") or tex_node(hmat, "Roughness")
    lmat = bpy.data.materials.new("Baked"); lmat.use_nodes = True
    me.materials.clear(); me.materials.append(lmat)
    ln = lmat.node_tree.nodes
    tgt = ln.new("ShaderNodeTexImage")
    def img(tag, sz, noncolor):
        im = bpy.data.images.new("%s_%s" % (tag, name), sz, sz, alpha=True)
        if noncolor: im.colorspace_settings.name = "Non-Color"
        return im
    pts = [v.co for v in me.vertices]
    diag = (Vector([max(p[i] for p in pts) for i in range(3)]) - Vector([min(p[i] for p in pts) for i in range(3)])).length
    hi.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = o
    kw = dict(use_selected_to_active=True, cage_extrusion=0.02 * diag, max_ray_distance=0.05 * diag, margin=16, use_clear=True)
    got = {}
    if tn:
        im = img("Normal", size // 2, True); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw); got["Normal"] = im
    out_node = next(n for n in hmat.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
    em = hmat.node_tree.nodes.new("ShaderNodeEmission")
    hmat.node_tree.links.new(em.outputs[0], out_node.inputs["Surface"])
    for tag, tnode, sz, nc in (("Base", tb, size, False), ("MR", tm, size // 2, True)):
        if not tnode: continue
        hmat.node_tree.links.new(tnode.outputs["Color"], em.inputs["Color"])
        im = img(tag, sz, nc); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
    for n_ in old: me.uv_layers.remove(me.uv_layers[n_])
    bpy.data.objects.remove(hi, do_unlink=True)
    return got

def grade(im, name):
    # richer, cleaner colour (everything a touch more saturated); the golden carp is the koi model turned to gold by its light and dark
    import numpy as np
    W, H = im.size
    a_ = np.array(im.pixels[:], dtype=np.float32).reshape(H, W, 4)
    rgb = a_[..., :3]
    lum = (rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722)[..., None]
    if name == "GoldenCarp":
        stops = [(0.02, (0.14, 0.035, 0.002)), (0.30, (0.80, 0.30, 0.012)), (0.80, (1.0, 0.66, 0.16))]
        l = lum[..., 0]
        out = np.zeros_like(rgb)
        for c in range(3):
            out[..., c] = np.interp(l, [s_[0] for s_ in stops], [s_[1][c] for s_ in stops])
        rgb = out
    else:
        rgb = np.clip(lum + (rgb - lum) * SAT, 0.0, 1.0)
    a_[..., :3] = rgb
    im.pixels[:] = a_.reshape(-1).tolist()

def flat(tag, sz, col):
    im = bpy.data.images.new("%s_flat" % tag, sz, sz, alpha=True); im.colorspace_settings.name = "Non-Color"
    im.pixels[:] = [col[0], col[1], col[2], 1.0] * (sz * sz)
    return im

for name in names:
    f = os.path.join(src, name + ".glb")
    if not os.path.exists(f): continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    ms = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in ms: o.select_set(True)
    bpy.context.view_layer.objects.active = ms[0]
    if len(ms) > 1: bpy.ops.object.join()
    o = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    if name in LEVEL:   # (the koi swims head-down: turn it about its side axis until its body line is level)
        import numpy as np
        P = np.array([[v.co.y, v.co.z] for v in me.vertices]); P = P - P.mean(0)
        w, V = np.linalg.eigh(np.cov(P.T)); ax = V[:, 1]
        ang = math.atan2(ax[1], ax[0]); ang = ang - math.pi if ang > math.pi / 2 else (ang + math.pi if ang < -math.pi / 2 else ang)
        import mathutils
        R = mathutils.Matrix.Rotation(-ang + math.radians(LEVEL[name]), 4, "X")
        for v in me.vertices: v.co = R @ v.co
        me.update()
    xs = [v.co.copy() for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    S = 100.0 / max(hi_.x - lo.x, hi_.y - lo.y, hi_.z - lo.z)
    g = Vector(((lo.x + hi_.x) * 0.5, (lo.y + hi_.y) * 0.5, (lo.z + hi_.z) * 0.5))
    for v in me.vertices: v.co = (v.co - g) * S
    me.update()
    hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
    tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
    bm0 = bmesh.new(); bm0.from_mesh(o.data); bmesh.ops.remove_doubles(bm0, verts=bm0.verts, dist=0.02); bm0.to_mesh(o.data); bm0.free()   # (weld the glb's split vertices first: decimating split seams opened see-through cracks)
    md = o.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, FISH[name] / float(tris0)); md.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=md.name)
    me = o.data
    me.shade_smooth()
    got = bake(o, hi, name, 1024)   # (a fresh atlas with a 16 px margin: Meshy's own, shrunk, left white flecks along its seams on the frog)
    if "Base" in got: grade(got["Base"], name)
    if "Normal" not in got: got["Normal"] = flat("Normal", 256, (0.5, 0.5, 1.0))
    if "MR" not in got: got["MR"] = flat("MR", 256, (0.0, 0.45, 0.0))
    for tag, im in got.items():
        im.filepath_raw = os.path.join(out, "T_%s_%s.png" % (name, tag)); im.file_format = "PNG"; im.save()
    o.name = name; o.data.name = name
    for v in me.vertices: v.co.y = -v.co.y
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(me); bm.free(); me.update()
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    fbx = os.path.join(out, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("WPROP", name, "tris", tris0, "->", sum(len(p.vertices) - 2 for p in me.polygons), "wrote", fbx)
