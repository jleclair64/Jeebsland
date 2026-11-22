# River lily pads (2026-11-22): Meshy models Art/WorldProps/Lily*.glb -> Saved/KnightBuild/WorldProps/<Name>.fbx + T_<Name>_Base/Normal/MR.png (then
# Tools/worldprop_import.py via Tools/BuildLilyPads.ps1). Each is reduced to ~N triangles (pictures baked from the full model), laid on the XY plane,
# scaled to 100 cm across (the game scales it per pad) and given its origin at the underside's lowest point, middle of the pad.
# Run in Blender: blender -b -P Tools/lily_clean.py -- <Art/WorldProps> <out dir> [names]
import bpy, bmesh, os, sys, math
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)
LILY = {"LilyPad": 1550, "LilyFlower": 1048, "LilyTwin": 1210}   # (triangles kept; no bake: Meshy's own UVs and pictures are kept, shrunk, since a fresh atlas of the flower shattered into scraps)
GREEN_GAIN = 0.55
names = a[2:] or list(LILY)

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

def calm_greens(im):
    # Meshy's leaf green is a neon in the game's light (2026-11-22 pictures): green-dominant pixels (not the pink petals or yellow centre) go darker and
    # a little warmer, towards the pond's own pad colour, keeping the veins' lighter strokes
    import numpy as np
    W, H = im.size
    a_ = np.array(im.pixels[:], dtype=np.float32).reshape(H, W, 4)
    r, g, b = a_[..., 0], a_[..., 1], a_[..., 2]
    m = ((g > r * 1.12) & (g > b * 1.4)).astype(np.float32)
    k = 1.0 - m * (1.0 - GREEN_GAIN)
    a_[..., 0] = r * k * (1.0 + m * 0.35); a_[..., 1] = g * k; a_[..., 2] = b * k
    im.pixels[:] = a_.reshape(-1).tolist()

def own_pictures(hi, name):
    hmat = hi.data.materials[0]
    got = {}
    for tag, sock, sz in (("Base", "Base Color", 1024), ("Normal", "Normal", 512), ("MR", "Metallic", 512)):
        n = tex_node(hmat, sock) or (tex_node(hmat, "Roughness") if tag == "MR" else None)
        if not n: continue
        im = n.image.copy(); im.scale(sz, sz); got[tag] = im
        if tag == "Base": calm_greens(im)
    return got

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
    xs = [v.co.copy() for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    S = 100.0 / max(hi_.x - lo.x, hi_.y - lo.y)
    g = Vector(((lo.x + hi_.x) * 0.5, (lo.y + hi_.y) * 0.5, lo.z))
    for v in me.vertices: v.co = (v.co - g) * S
    me.update()
    hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
    tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
    md = o.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, LILY[name] / float(tris0)); md.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=md.name)
    me = o.data
    me.shade_smooth()
    got = own_pictures(hi, name)
    bpy.data.objects.remove(hi, do_unlink=True)
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
