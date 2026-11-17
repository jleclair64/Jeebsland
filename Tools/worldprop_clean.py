# World props from Meshy's community library (2026-11-16, the forest ruin): Art/WorldProps/Originals/<file> (git-ignored; links in Art/WorldProps/Sources.md)
# -> Saved/KnightBuild/WorldProps/<Name>.fbx + T_<Name>_Base/Normal/MR.png, imported by Tools/worldprop_import.py (Tools/BuildWorldProps.ps1).
# Run in Blender: blender -b -P Tools/worldprop_clean.py -- <Art/WorldProps> <out dir> [names]
# Per prop (WORLD below):
#   axes: where the model's own x, y, z go in the game's frame (signed letters). height: its height in cm. The origin goes to the middle of its foot, at its lowest point.
#   cut: lambda (face centre in the model's own frame, metres; (r, g, b) of the picture there; face normal) -> True to delete (dirt piles, stray pieces).
#   reduce: triangles kept (welded, collapse-decimated, fresh UVs and pictures baked from the full model: base 2048, normal and metal/roughness 1024).
#   stone: (hue, saturation, value, fine, slow): the whole picture evened to one clean stone colour, keeping 'fine' of the fine detail (the joints, chips)
#     and 'slow' of the gentle shading; the hue keeps 15% of each pixel's own.
#   hulls: (cell cm, lowest cm): the simple collision: the model cut into upright columns 'cell' wide, one convex hull (UCX_) per column round the
#     vertices in it, columns lower than 'lowest' left out (rubble he steps over). A window or arch opening in a column is filled.
import bpy, bmesh, os, sys, math
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

def _hsv(c):
    mx, mn = max(c), min(c)
    if mx - mn < 1e-5: return 0.0, 0.0, mx
    r, g, b = c
    if mx == r: h = ((g - b) / (mx - mn)) % 6.0
    elif mx == g: h = (b - r) / (mx - mn) + 2.0
    else: h = (r - g) / (mx - mn) + 4.0
    return h * 60.0, (mx - mn) / mx, mx

WORLD = {
    # "Ruined Fortress" by eromaxi (CC0): a curved stone wall with a window arch and fallen blocks. Cut: the dark earth mound under its right side and the
    # dark roof slab on its left (owner: no dirt piles).
    "Ruin": dict(file="Originals/Ruin_Original.glb", axes=("Y", "-X", "Z"), height=500.0, reduce=40000,
                 thin=0.012, cut=lambda p, c, n: (_hsv(c)[2] < 0.10) or (p[2] < -0.50 and _hsv(c)[2] < 0.30 and _hsv(c)[1] > 0.35),
                 cut_final=lambda p, n: (p.x < -215.0 and p.z > 290.0) or (p.x < -170.0 and 300.0 < p.z < 350.0 and abs(n.z) > 0.5),
                 stone=(36.0, 0.13, 0.52, 0.55, 0.45), hulls=(70.0, 35.0)),
}
names = a[2:] or list(WORLD)
IDX = {"X": 0, "Y": 1, "Z": 2}

def axis_matrix(ax):
    m = Matrix.Identity(3)
    for col, s in enumerate(ax):
        sign = -1.0 if s[0] == "-" else 1.0
        for row in range(3): m[row][col] = 0.0
        m[IDX[s[-1]]][col] = sign
    return m

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

def bake(o, hi, name, size=2048):
    # fresh UVs on the reduced model and its pictures baked from the full one (as Tools/prop_clean.py's bake_to_new_uvs)
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
    kw = dict(use_selected_to_active=True, cage_extrusion=0.006 * diag, max_ray_distance=0.02 * diag, margin=16, use_clear=True)
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
    print("WPROP", name, "baked", ", ".join(sorted(got)))
    return got

def blur(a_, rad):
    import numpy as np
    for ax in (0, 1):
        c = np.cumsum(a_, axis=ax, dtype=np.float64)
        n = a_.shape[ax]
        hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
        t = np.take(c, hi_, axis=ax); b_v = np.take(c, np.maximum(lo_, 0), axis=ax)
        shape = [1, 1]; shape[ax] = n
        a_ = t - np.where(lo_.reshape(shape) < 0, 0.0, b_v)
    return a_

def restone(im, P):
    # one clean pale stone: hue and saturation set (15% of each pixel's own hue kept), the value from the picture's own shading and detail
    import numpy as np
    h0, s0, v0, keep_fine, keep_slow = P["stone"]
    W, H = im.size
    arr = np.array(im.pixels[:], dtype=np.float32).reshape(H, W, 4)
    rgb = arr[..., :3]
    V = rgb.max(axis=2); C = V - rgb.min(axis=2); S = C / np.maximum(V, 1e-4)
    r_, g_, b_ = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    Hd = np.where(V == r_, ((g_ - b_) / np.maximum(C, 1e-6)) % 6.0, np.where(V == g_, (b_ - r_) / np.maximum(C, 1e-6) + 2.0, (r_ - g_) / np.maximum(C, 1e-6) + 4.0)) * 60.0
    used = V > 0.01
    rad = max(4, W // 64)
    wt = blur(used.astype(np.float64), rad)
    slow = blur(np.where(used, V, 0.0), rad) / np.maximum(wt, 1.0)
    fine = np.where(used, V / np.maximum(slow, 0.02), 1.0)
    mean_ = max(float(V[used].mean()), 0.02)
    Vn = np.clip(v0 * np.power(np.maximum(slow, 0.02) / mean_, keep_slow) * np.power(fine, keep_fine), 0.0, 1.0)
    dh = ((Hd - h0 + 180.0) % 360.0) - 180.0
    Hn = (h0 + 0.15 * np.clip(dh, -40.0, 40.0)) % 360.0
    Sn = np.clip(s0 + 0.15 * (S - s0), 0.0, 1.0)
    h6 = Hn / 60.0; X = 1.0 - np.abs(h6 % 2.0 - 1.0)
    Cn = Vn * Sn; Xn = Cn * X; m = Vn - Cn
    seg = np.floor(h6).astype(np.int32) % 6
    z = 0 * Cn
    R = np.choose(seg, [Cn, Xn, z, z, Xn, Cn]); G = np.choose(seg, [Xn, Cn, Cn, Xn, z, z]); B = np.choose(seg, [z, z, Xn, Cn, Cn, Xn])
    arr[..., 0] = np.where(used, R + m, r_); arr[..., 1] = np.where(used, G + m, g_); arr[..., 2] = np.where(used, B + m, b_)
    im.pixels.foreach_set(arr.reshape(-1)); im.update()
    print("WPROP stone: value mean %.3f -> %.3f" % (mean_, float(Vn[used].mean())))

def save(im, path):
    im.filepath_raw = path; im.file_format = "PNG"; im.save()

for name in names:
    P = WORLD[name]
    f = os.path.join(src, P["file"])
    if not os.path.exists(f):
        print("WPROP missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
    # cut: by where a face is and the colour under it
    if "cut" in P:
        mat = me.materials[0]
        tb = tex_node(mat, "Base Color").image
        W, H = tb.size
        import numpy as np
        px = np.array(tb.pixels[:], dtype=np.float32).reshape(H, W, 4)
        uvl = me.uv_layers.active.data
        bm = bmesh.new(); bm.from_mesh(me)
        uv = bm.loops.layers.uv.active
        gone = []
        for fc in bm.faces:
            u = sum((l[uv].uv for l in fc.loops), Vector((0.0, 0.0))) / len(fc.loops)
            c = px[min(H - 1, max(0, int((u.y % 1.0) * H))), min(W - 1, max(0, int((u.x % 1.0) * W))), :3]
            if P["cut"](fc.calc_center_median(), tuple(float(x) for x in c), fc.normal): gone.append(fc)
        bmesh.ops.delete(bm, geom=gone, context="FACES")
        # (bits left floating: islands of under 40 faces, once welded: the model is split along its picture's seams)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bm.faces.ensure_lookup_table()
        seen = set(); stray = []
        for fc in bm.faces:
            if fc in seen: continue
            comp = [fc]; seen.add(fc); k = 0
            while k < len(comp):
                for e in comp[k].edges:
                    for g in e.link_faces:
                        if g not in seen: seen.add(g); comp.append(g)
                k += 1
            if len(comp) < 40: stray.extend(comp)
        bmesh.ops.delete(bm, geom=stray, context="FACES")
        # thin: faces of paper-thin sheets (what was left of the roof slab and its hanging shards, review round 2): a ray from the face into the model
        # meets the other side within 'thin' (a share of the model's height); stone blocks are far thicker. Then the strays again.
        nthin = 0
        if P.get("thin"):
            from mathutils.bvhtree import BVHTree
            bm.faces.ensure_lookup_table()
            zs = [v.co.z for v in bm.verts]; t_ = P["thin"] * (max(zs) - min(zs))
            tree = BVHTree.FromBMesh(bm)
            thin = []
            for fc in bm.faces:
                c_ = fc.calc_center_median(); n_ = fc.normal
                if n_.length < 0.5: continue
                hit = tree.ray_cast(c_ - n_ * (t_ * 0.02), -n_, t_)
                if hit[0] is not None: thin.append(fc)
            bmesh.ops.delete(bm, geom=thin, context="FACES")
            nthin = len(thin)
            bm.faces.ensure_lookup_table()
            seen = set(); stray2 = []
            for fc in bm.faces:
                if fc in seen: continue
                comp = [fc]; seen.add(fc); k = 0
                while k < len(comp):
                    for e in comp[k].edges:
                        for g in e.link_faces:
                            if g not in seen: seen.add(g); comp.append(g)
                    k += 1
                if len(comp) < 200: stray2.extend(comp)
            bmesh.ops.delete(bm, geom=stray2, context="FACES")
            stray += stray2
        bm.to_mesh(me); bm.free(); me.update()
        print("WPROP", name, "cut", len(gone), "faces, and", len(stray), "stray, and", nthin, "thin")
    # reduce, with the full model kept for the bake
    hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
    bm = bmesh.new(); bm.from_mesh(me)
    ext = max((max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts)) for i in range(3))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=ext * 2e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free(); me.update()
    tris_now = sum(len(p.vertices) - 2 for p in me.polygons)
    md = o.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, P["reduce"] / float(tris_now)); md.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=md.name)
    me = o.data
    try: bpy.ops.mesh.customdata_custom_splitnormals_clear()
    except Exception as e_: print("WPROP custom normals kept:", e_)
    me.shade_smooth(); me.set_sharp_from_angle(angle=math.radians(50.0))
    got = bake(o, hi, name)
    me = o.data
    print("WPROP", name, "tris from", tris0, "to", sum(len(p.vertices) - 2 for p in me.polygons))
    if "stone" in P and "Base" in got: restone(got["Base"], P)
    # into the game's frame: the axes, the size, the origin at the middle of the foot
    M3 = axis_matrix(P["axes"])
    for v in me.vertices: v.co = M3 @ v.co
    xs = [v.co for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    S = P["height"] / (hi_.z - lo.z)
    g = Vector(((lo.x + hi_.x) * 0.5, (lo.y + hi_.y) * 0.5, lo.z))
    for v in me.vertices: v.co = (v.co - g) * S
    me.update()
    # cut_final: faces to delete in the game's frame (cm; centre, normal), after the bake (its pictures stay right): the roof slab, which is thick and
    # the same grey as the stone, so neither the colour cut nor 'thin' found it (review round 2)
    if "cut_final" in P:
        bm = bmesh.new(); bm.from_mesh(me)
        gone = [f for f in bm.faces if P["cut_final"](f.calc_center_median(), f.normal)]
        bmesh.ops.delete(bm, geom=gone, context="FACES")
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        bm.to_mesh(me); bm.free(); me.update()
        print("WPROP", name, "cut_final", len(gone), "faces")
    xs = [v.co.copy() for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi_ = Vector((max(c[i] for c in xs) for i in range(3)))
    print("WPROP", name, "size cm", tuple(round(hi_[i] - lo[i]) for i in range(3)), "lo", tuple(round(x) for x in lo), "hi", tuple(round(x) for x in hi_))
    # the simple collision: one convex hull per upright column of the model
    hulls = []
    if "hulls" in P:
        cell, lowest = P["hulls"]
        cols = {}
        for c in xs: cols.setdefault((int(math.floor(c.x / cell)), int(math.floor(c.y / cell))), []).append(c)
        for key, ps in sorted(cols.items()):
            if len(ps) < 8: continue
            if max(p.z for p in ps) - min(p.z for p in ps) < lowest and max(p.z for p in ps) < lowest: continue
            hb = bmesh.new()
            for p in ps: hb.verts.new(p)
            res = bmesh.ops.convex_hull(hb, input=hb.verts[:])
            loose = list({v for v in list(res.get("geom_interior", [])) + list(res.get("geom_unused", [])) if isinstance(v, bmesh.types.BMVert)})
            if loose: bmesh.ops.delete(hb, geom=loose, context="VERTS")
            if len(hb.faces) < 4: hb.free(); continue
            if len(hb.verts) > 64:   # (Unreal keeps hulls small: thin the points out)
                bmesh.ops.dissolve_limit(hb, angle_limit=math.radians(8.0), verts=hb.verts[:], edges=hb.edges[:])
            hm = bpy.data.meshes.new("UCX_%s_%02d" % (name, len(hulls)))
            hb.to_mesh(hm); hb.free()
            ho = bpy.data.objects.new(hm.name, hm); bpy.context.scene.collection.objects.link(ho)
            hulls.append(ho)
        print("WPROP", name, "collision hulls", len(hulls), "cell", cell, "cm")
    # pictures
    for tag, im in got.items():
        save(im, os.path.join(out, "T_%s_%s.png" % (name, tag)))
    # out to FBX: Blender's y -> the game's -y under the importer's scene conversion (as prop_clean.py), so y flipped and the faces turned
    o.name = name; o.data.name = name
    for ob in [o] + hulls:
        for v in ob.data.vertices: v.co.y = -v.co.y
        bm = bmesh.new(); bm.from_mesh(ob.data); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(ob.data); bm.free(); ob.data.update()
    bpy.ops.object.select_all(action="DESELECT")
    for ob in [o] + hulls: ob.select_set(True)
    bpy.context.view_layer.objects.active = o
    fbx = os.path.join(out, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("WPROP wrote", fbx, "tris", sum(len(p.vertices) - 2 for p in o.data.polygons))
