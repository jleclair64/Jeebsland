# Troll kit clean-up (2026-11-26, Thursday list item 15). The Meshy kit pieces in Art/TrollKit/Lo/lo_<piece>.glb were decimated by Meshy into
# loose triangles (hundreds of separate bits, 90% of the edges open: cracks and see-through slivers at close range). Their Meshy tasks have
# expired, so they are rebuilt here, without credits: the old model is welded, voxel-remeshed (the level-set closes every crack and fuses the
# bits into one closed surface), smoothed, reduced to a sensible triangle count, given fresh UVs, and its colour, metal/roughness and normal
# pictures are baked from the old model onto the new surface. The result is written over lo_<piece>.glb in the same layout the fit
# (Tools/troll_armour_fit.py) reads: Base colour, metallic-roughness (green = roughness, blue = metallic) and tangent normal map, one material.
# The old file is kept as Saved/TrollKit/Old/lo_<piece>_old.glb (not in git; the before/after boards use it). The skin weights come from the fit afterwards.
#
#   blender -b -P troll_kit_clean.py -- <lo folder> <piece[,piece..]> [voxel=0.008] [tris=14000] [tex=2048] [smooth=4] [out=<file>]
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
LO = argv[0]; PIECES = argv[1].split(",")
opt = dict(a.split("=", 1) for a in argv[2:])
TARGET = {"boot": 8000, "chest": 14000, "gauntlet": 8000, "sack": 6000, "shoulder": 10000}
def log(s): print("TKCLEAN " + s)

def tex_node(mat, sock):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    stack = [pr.inputs[sock]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(n.inputs)
    return None

def stats(ob):
    bm = bmesh.new(); bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bd = sum(1 for e in bm.edges if len(e.link_faces) == 1); nm = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    seen = set(); isl = 0
    for v in bm.verts:
        if v in seen: continue
        isl += 1; st = [v]; seen.add(v)
        while st:
            x = st.pop()
            for e in x.link_edges:
                o = e.other_vert(x)
                if o not in seen: seen.add(o); st.append(o)
    n = len(bm.faces); bm.free()
    return n, bd, nm, isl

for piece in PIECES:
    fn = os.path.join(LO, "lo_%s.glb" % piece)
    old = os.path.normpath(os.path.join(LO, "..", "..", "..", "Saved", "TrollKit", "Old", "lo_%s_old.glb" % piece))
    os.makedirs(os.path.dirname(old), exist_ok=True)
    if not os.path.exists(old):
        import shutil; shutil.copyfile(fn, old)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=old)
    hi = next(o for o in bpy.data.objects if o.type == 'MESH')
    for o in list(bpy.data.objects):
        if o != hi: bpy.data.objects.remove(o)
    hi.parent = None; hi.data.transform(hi.matrix_world); hi.matrix_world = hi.matrix_world.Identity(4); hi.name = "Hi"
    hmat = hi.data.materials[0]
    n0 = stats(hi)
    log("%s: old %d tris, open edges %d, non-manifold %d, %d bits" % ((piece,) + n0))
    dims = hi.dimensions; diag = dims.length
    voxel = float(opt.get("voxel", 0.008)); target = int(opt.get("tris", TARGET[piece])); tex = int(opt.get("tex", 2048))

    # the low copy: welded, voxel-remeshed
    lo = hi.copy(); lo.data = hi.data.copy(); lo.name = "Lo"; bpy.context.scene.collection.objects.link(lo)
    lo.data.materials.clear()
    bm = bmesh.new(); bm.from_mesh(lo.data); bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-3); bm.to_mesh(lo.data); bm.free()
    for uvl in list(lo.data.uv_layers): lo.data.uv_layers.remove(uvl)
    bpy.ops.object.select_all(action='DESELECT'); bpy.context.view_layer.objects.active = lo; lo.select_set(True)
    # Blender's voxel pass cannot tell inside from outside on loose open triangles (it left a lattice), so every triangle is first given
    # thickness (a closed little slab of Thick, about a plate's worth): each slab is a clean volume, the overlapping slabs fuse, cracks smaller than a voxel close.
    sd = lo.modifiers.new("S", 'SOLIDIFY'); sd.thickness = float(opt.get("thick", 0.014)); sd.offset = 0.0; sd.use_even_offset = False
    bpy.ops.object.modifier_apply(modifier="S")
    lo.data.remesh_voxel_size = voxel; lo.data.remesh_voxel_adaptivity = 0.0
    bpy.ops.object.voxel_remesh()
    log("%s: voxel %.4f -> %d faces" % (piece, voxel, len(lo.data.polygons)))
    # smooth the stair-steps a little (volume kept by smooth / un-smooth passes)
    bm = bmesh.new(); bm.from_mesh(lo.data)
    for k in range(int(opt.get("smooth", 6))):
        bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=-0.53, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    # the voxel pass leaves a little closed blob wherever one stray triangle floated: bits under Minbit faces go (the real parts are thousands)
    bm.faces.ensure_lookup_table(); seen = set(); bits = []
    for f in bm.faces:
        if f in seen: continue
        seen.add(f); st = [f]; grp = []
        while st:
            x = st.pop(); grp.append(x)
            for e in x.edges:
                for g in e.link_faces:
                    if g not in seen: seen.add(g); st.append(g)
        bits.append(grp)
    bits.sort(key=len, reverse=True)
    minbit = int(opt.get("minbit", 3000))
    gone = [f for g in bits if len(g) < minbit for f in g]
    log("%s: bits after the voxel pass (faces) %s ... dropping %d bits of under %d faces (%d faces)" % (piece, [len(g) for g in bits[:12]], sum(1 for g in bits if len(g) < minbit), minbit, len(gone)))
    bmesh.ops.delete(bm, geom=gone, context='FACES')
    bm.to_mesh(lo.data); bm.free()
    # (the fine voxel surface, kept as the source of the normal map: the old model's own surface is shards, whose faces would bake in as a noisy normal map)
    vox = lo.copy(); vox.data = lo.data.copy(); vox.name = "Vox"; bpy.context.scene.collection.objects.link(vox)
    for p in vox.data.polygons: p.use_smooth = True
    bpy.context.view_layer.objects.active = lo
    dm = lo.modifiers.new("D", 'DECIMATE'); dm.ratio = min(1.0, target / max(1, len(lo.data.polygons))); dm.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier="D")
    log("%s: decimated to %d faces" % (piece, len(lo.data.polygons)))
    for p in lo.data.polygons: p.use_smooth = True
    lo.data.set_sharp_from_angle(angle=math.radians(50))
    n1 = stats(lo)
    log("%s: new %d tris, open edges %d, non-manifold %d, %d bits" % ((piece,) + n1))

    # UVs: unwrapped on a heavily smoothed copy of the same mesh and laid on the real one (same corners), so the fur tufts and lumps do not each
    # become an island: the first try (smart project straight on the lumpy surface) made 1000s of tiny islands and a speckled, noisy normal map
    sm = lo.copy(); sm.data = lo.data.copy(); sm.name = "Smooth"; bpy.context.scene.collection.objects.link(sm)
    bm = bmesh.new(); bm.from_mesh(sm.data)
    for k in range(int(opt.get("uvsmooth", 40))): bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.to_mesh(sm.data); bm.free()
    sm.data.uv_layers.new(name="UVMap")
    bpy.ops.object.select_all(action='DESELECT'); bpy.context.view_layer.objects.active = sm; sm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(float(opt.get("angle", 70))), island_margin=float(opt.get("margin", 0.006)), area_weight=0.0)
    bpy.ops.object.mode_set(mode='OBJECT')
    n_l = len(lo.data.loops)
    arr = np.zeros(n_l * 2, dtype=np.float32); sm.data.uv_layers.active.data.foreach_get("uv", arr)
    lo.data.uv_layers.new(name="UVMap"); lo.data.uv_layers.active.data.foreach_set("uv", arr)
    bpy.data.objects.remove(sm, do_unlink=True)
    bpy.context.view_layer.objects.active = lo
    ids = set(); 
    uvv = arr.reshape(-1, 2)
    log("%s: UV loops %d, extent %.2f x %.2f" % (piece, n_l, uvv[:, 0].max() - uvv[:, 0].min(), uvv[:, 1].max() - uvv[:, 1].min()))

    # bake
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 1
    lmat = bpy.data.materials.new("Material_0"); lmat.use_nodes = True
    lo.data.materials.append(lmat)
    ln = lmat.node_tree.nodes; lk = lmat.node_tree.links
    lpr = next(n for n in ln if n.type == "BSDF_PRINCIPLED")
    tgt = ln.new("ShaderNodeTexImage")
    tb = tex_node(hmat, "Base Color"); tn = tex_node(hmat, "Normal"); tm = tex_node(hmat, "Roughness") or tex_node(hmat, "Metallic")
    def img(tag, sz, nc):
        im = bpy.data.images.new("%s_%s" % (tag, piece), sz, sz, alpha=False)
        if nc: im.colorspace_settings.name = "Non-Color"
        return im
    kw = dict(use_selected_to_active=True, cage_extrusion=0.012 * diag, max_ray_distance=0.03 * diag, margin=24, margin_type="EXTEND", use_clear=True)
    got = {}
    bpy.ops.object.select_all(action='DESELECT'); vox.select_set(True); lo.select_set(True); bpy.context.view_layer.objects.active = lo
    im = img("Normal", tex // 2, True); tgt.image = im; ln.active = tgt
    bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw); got["Normal"] = im
    nstr = float(opt.get("nstr", 0.6))   # (the fine surface's ripples are a little much in the game's light: eased toward flat)
    if nstr < 1.0:
        na = np.array(im.pixels[:], dtype=np.float32).reshape(-1, 4)
        na[:, :3] = np.array([0.5, 0.5, 1.0], dtype=np.float32) + (na[:, :3] - np.array([0.5, 0.5, 1.0], dtype=np.float32)) * nstr
        im.pixels.foreach_set(na.reshape(-1)); im.update()
    bpy.ops.object.select_all(action='DESELECT'); hi.select_set(True); lo.select_set(True); bpy.context.view_layer.objects.active = lo
    out_node = next(n for n in hmat.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
    em = hmat.node_tree.nodes.new("ShaderNodeEmission"); hmat.node_tree.links.new(em.outputs[0], out_node.inputs["Surface"])
    for tag, tnode, sz, nc in (("Base", tb, tex, False), ("MR", tm, tex // 2, True)):
        hmat.node_tree.links.new(tnode.outputs["Color"], em.inputs["Color"])
        im = img(tag, sz, nc); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
    ln.remove(tgt)
    # material in the layout the glTF exporter and importer round-trip: Base, MR through a separate (G roughness, B metallic), Normal map
    def node(im):
        n = ln.new("ShaderNodeTexImage"); n.image = im; return n
    lk.new(node(got["Base"]).outputs["Color"], lpr.inputs["Base Color"])
    sp = ln.new("ShaderNodeSeparateColor"); lk.new(node(got["MR"]).outputs["Color"], sp.inputs["Color"])
    lk.new(sp.outputs["Green"], lpr.inputs["Roughness"]); lk.new(sp.outputs["Blue"], lpr.inputs["Metallic"])
    nm = ln.new("ShaderNodeNormalMap"); lk.new(node(got["Normal"]).outputs["Color"], nm.inputs["Color"]); lk.new(nm.outputs[0], lpr.inputs["Normal"])
    bpy.data.objects.remove(hi, do_unlink=True); bpy.data.objects.remove(vox, do_unlink=True)
    for ob in list(bpy.data.objects):
        if ob != lo: bpy.data.objects.remove(ob)
    lo.name = "Mesh_0"
    bpy.ops.object.select_all(action='DESELECT'); lo.select_set(True); bpy.context.view_layer.objects.active = lo
    outp = fn if opt.get("out") is None else opt["out"]
    bpy.ops.export_scene.gltf(filepath=outp, export_format='GLB', use_selection=True, export_image_format='AUTO', export_apply=False)
    log("%s: wrote %s (%d tris)" % (piece, outp, sum(len(p.vertices) - 2 for p in lo.data.polygons)))
