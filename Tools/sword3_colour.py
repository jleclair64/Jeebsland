# Sword 3 (dele's "sword" from Meshy's library, Art/Props/Sword3_raw.glb) comes untextured grey, with no UVs. This gives it two looks by position, a clean steel blade, guard and
# pommel and a dark leather-wrapped grip (the model's own frame: tip at -Z, guard at z 0.40..0.55, grip z 0.55..0.88, pommel above), bakes them into a base-colour picture and a
# metal/roughness picture on fresh UVs, and writes Art/Props/Sword3.glb, which Tools/prop_clean.py then reduces and bakes like any other prop (it needs a textured model).
# Run: blender -b -P Tools/sword3_colour.py
import bpy, os, math
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(proj, "Art", "Props", "Sword3_raw.glb"))
o = next(x for x in bpy.data.objects if x.type == "MESH")
me = o.data
LOOKS = [("Steel", (0.50, 0.52, 0.56), 0.30, 0.85), ("Leather", (0.17, 0.095, 0.05), 0.75, 0.0)]
def mat(name, rgb, rough, metal, emit=False):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    if emit:
        e = nt.nodes.new("ShaderNodeEmission"); e.inputs["Color"].default_value = (0.0, rough, metal, 1.0); nt.links.new(e.outputs[0], out.inputs["Surface"])
    else:
        b = nt.nodes.new("ShaderNodeBsdfPrincipled"); b.inputs["Base Color"].default_value = (*rgb, 1.0); b.inputs["Roughness"].default_value = 0.5; nt.links.new(b.outputs[0], out.inputs["Surface"])
    return m
M = o.matrix_world
for p in me.polygons:
    z = (M @ p.center).z
    p.material_index = 1 if 0.555 < z < 0.875 else 0
bpy.context.view_layer.objects.active = o; o.select_set(True)
uv = me.uv_layers.new(name="UV"); me.uv_layers.active = uv
bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.004)
bpy.ops.object.mode_set(mode="OBJECT")
sc = bpy.context.scene; sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
def bake(tag, emit, noncolor):
    im = bpy.data.images.new("Sword3_" + tag, 1024, 1024, alpha=False)
    if noncolor: im.colorspace_settings.name = "Non-Color"
    me.materials.clear()
    for n, rgb, r, mt in LOOKS: me.materials.append(mat(n, rgb, r, mt, emit))
    for m in me.materials:
        t = m.node_tree.nodes.new("ShaderNodeTexImage"); t.image = im; m.node_tree.nodes.active = t
    if emit: bpy.ops.object.bake(type="EMIT", margin=16)
    else: bpy.ops.object.bake(type="DIFFUSE", pass_filter={"COLOR"}, margin=16)
    return im
base = bake("Base", False, False)
mr = bake("MR", True, True)
fin = bpy.data.materials.new("Sword3"); fin.use_nodes = True
nt = fin.node_tree; pr = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
tb = nt.nodes.new("ShaderNodeTexImage"); tb.image = base; nt.links.new(tb.outputs["Color"], pr.inputs["Base Color"])
tm = nt.nodes.new("ShaderNodeTexImage"); tm.image = mr
sp = nt.nodes.new("ShaderNodeSeparateColor"); nt.links.new(tm.outputs["Color"], sp.inputs["Color"])
nt.links.new(sp.outputs["Green"], pr.inputs["Roughness"]); nt.links.new(sp.outputs["Blue"], pr.inputs["Metallic"])
me.materials.clear(); me.materials.append(fin)
for p in me.polygons: p.material_index = 0
bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.gltf(filepath=os.path.join(proj, "Art", "Props", "Sword3.glb"), export_format="GLB", use_selection=True, export_yup=True, export_image_format="AUTO")
print("SWORD3 written")
