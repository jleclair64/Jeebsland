# Blender (background): a library animal (Docs/DeerPlan.md) reduced for the game before Meshy rigs it.
#   blender -b --factory-startup -P Tools/deer_clean.py -- <in.glb> <out.glb> [triangles=20000] [texture=2048]
# The full model is welded and collapsed to the triangle budget, given fresh UVs, and its colour and normal pictures are baked from
# the full model (a heavy collapse across a library model's UV seams otherwise shatters the picture: Docs/MeshyCommunityLibrary.md).
# Writes one .glb (the pictures inside it) and prints DEER lines: triangles, size, which way it faces.
import bpy, bmesh, sys, math
from mathutils import Vector

a = sys.argv[sys.argv.index("--") + 1:]
SRC, DST = a[0], a[1]
TRIS = int(a[2]) if len(a) > 2 else 20000
SIZE = int(a[3]) if len(a) > 3 else 2048
NAME = a[4] if len(a) > 4 else "Stag"   # (2026-11-22: the horse reuses this)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
hi = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
V = [v.co for v in hi.data.vertices]
lo_ = Vector([min(c[i] for c in V) for i in range(3)]); hi_ = Vector([max(c[i] for c in V) for i in range(3)])
tris0 = sum(len(p.vertices) - 2 for p in hi.data.polygons)
print("DEER full model: tris %d, size x %.3f y %.3f z %.3f m, lowest z %.3f" % (tris0, hi_.x - lo_.x, hi_.y - lo_.y, hi_.z - lo_.z, lo_.z))
# Which way does it face? The antlers and head are the highest part: their mean x,y against the body's middle
top = [c for c in V if c.z > lo_.z + 0.8 * (hi_.z - lo_.z)]
head = sum(top, Vector()) / len(top); mid = (lo_ + hi_) / 2
print("DEER head is toward x %+.3f y %+.3f of the middle (Blender -Y is glTF +Z, the way Meshy's rigging wants the face)" % (head.x - mid.x, head.y - mid.y))

lo = hi.copy(); lo.data = hi.data.copy(); bpy.context.scene.collection.objects.link(lo)
me = lo.data
bm = bmesh.new(); bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=(hi_ - lo_).length * 2e-5)
bm.to_mesh(me); bm.free(); me.update()
bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = lo; lo.select_set(True)
now = sum(len(p.vertices) - 2 for p in me.polygons)
md = lo.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, TRIS / float(now)); md.use_collapse_triangulate = True
bpy.ops.object.modifier_apply(modifier=md.name)
me = lo.data
try: bpy.ops.mesh.customdata_custom_splitnormals_clear()
except Exception as e: print("DEER custom normals kept:", e)
me.shade_smooth()

# fresh UVs and the bake
sc = bpy.context.scene
sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
old = [l.name for l in me.uv_layers]
uvb = me.uv_layers.new(name="Bake"); me.uv_layers.active = uvb
bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.01)
bpy.ops.object.mode_set(mode="OBJECT")

def tex_node(mat, sock):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    if not pr.inputs[sock].links: return None
    n = pr.inputs[sock].links[0].from_node
    if n.type == "NORMAL_MAP": n = n.inputs["Color"].links[0].from_node if n.inputs["Color"].links else None
    if n and n.type == "SEPARATE_COLOR": n = n.inputs[0].links[0].from_node if n.inputs[0].links else None
    return n if (n and n.type == "TEX_IMAGE") else None

hmat = hi.data.materials[0]
tb = tex_node(hmat, "Base Color"); tn = tex_node(hmat, "Normal")
print("DEER full model pictures: base %s, normal %s" % (tb.image.size[:] if tb else None, tn.image.size[:] if tn else None))
lmat = bpy.data.materials.new("M_" + NAME); lmat.use_nodes = True
me.materials.clear(); me.materials.append(lmat)
ln = lmat.node_tree.nodes; lpr = next(n for n in ln if n.type == "BSDF_PRINCIPLED")
tgt = ln.new("ShaderNodeTexImage")
diag = (hi_ - lo_).length
hi.select_set(True); lo.select_set(True); bpy.context.view_layer.objects.active = lo
kw = dict(use_selected_to_active=True, cage_extrusion=0.008 * diag, max_ray_distance=0.02 * diag, margin=32, margin_type="EXTEND", use_clear=True)
imn = bpy.data.images.new("T_" + NAME + "_Normal", SIZE // 2, SIZE // 2, alpha=False); imn.colorspace_settings.name = "Non-Color"
tgt.image = imn; ln.active = tgt
bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw)
out_node = next(n for n in hmat.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
em = hmat.node_tree.nodes.new("ShaderNodeEmission")
hmat.node_tree.links.new(em.outputs[0], out_node.inputs["Surface"])
hmat.node_tree.links.new(tb.outputs["Color"], em.inputs["Color"])
imb = bpy.data.images.new("T_" + NAME + "_Base", SIZE, SIZE, alpha=False)
tgt.image = imb; ln.active = tgt
bpy.ops.object.bake(type="EMIT", **kw)
ln.remove(tgt)
lk = lmat.node_tree.links
nb = ln.new("ShaderNodeTexImage"); nb.image = imb; lk.new(nb.outputs["Color"], lpr.inputs["Base Color"])
nn = ln.new("ShaderNodeTexImage"); nn.image = imn
nm = ln.new("ShaderNodeNormalMap"); lk.new(nn.outputs["Color"], nm.inputs["Color"]); lk.new(nm.outputs[0], lpr.inputs["Normal"])
lpr.inputs["Roughness"].default_value = 0.8; lpr.inputs["Metallic"].default_value = 0.0
for n_ in old: me.uv_layers.remove(me.uv_layers[n_])
for im in (imb, imn): im.pack()
bpy.data.objects.remove(hi, do_unlink=True)
lo.name = NAME; me.name = NAME
# feet on the ground, middle over the origin
lo.location = Vector((-(lo_.x + hi_.x) / 2, -(lo_.y + hi_.y) / 2, -lo_.z))
bpy.ops.object.select_all(action="DESELECT"); lo.select_set(True); bpy.context.view_layer.objects.active = lo
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
open_edges = 0
bm = bmesh.new(); bm.from_mesh(me); open_edges = sum(1 for e in bm.edges if e.is_boundary); bm.free()
print("DEER reduced: tris %d (from %d), open edges %d, texture %d" % (sum(len(p.vertices) - 2 for p in me.polygons), tris0, open_edges, SIZE))
bpy.ops.export_scene.gltf(filepath=DST, export_format="GLB", use_selection=True, export_image_format="AUTO", export_apply=True)
print("DEER wrote", DST)
