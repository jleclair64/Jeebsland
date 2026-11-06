# Turns Meshy's models in Art/Props into what the game holds (2026-11-05). Run by Tools\BuildProps.ps1 in Blender.
# Each prop gets its grip at the origin and the game's axes, a size, and a mask in its vertex colour (red 1 = takes the tier's or the potion's colour).
# Its pictures are shrunk: base colour 1024, the others 512. Results: Saved/KnightBuild/Props/<Name>.fbx, T_<Name>_Base/Normal/MR.png
#
# The numbers per prop live in PROPS:
#  length: the longest side in cm. axes: where the model's own x, y, z go in the game's frame (signed letters, X Y Z).
#  grip: where the hand holds it, as a share along the long axis in the game's frame (0 = lowest end, 1 = highest end); origin there.
#  bend: straighten the long axis into one gentle arc this deep, along this game axis (the longbow). thick: x its width and depth, about that line.
#  squeeze: (from, to, x): x the width and depth about the long axis, coming in between these shares along it and staying from there on.
#  widen: (axis, x): x its size along this game axis. A single picture leaves the depth a guess.
#  blade: (span, thick, from, root, tip): a head's arms are too flat from the side: past 'from' cm along span, they get this half-thickness along thick, root to tip, in cm.
#  neck: (axis, from cm, stretch cm): everything beyond this, on the low side of the axis, moves that much further out, drawing the faces between into a longer neck.
#  also: (axis, cm): everything beyond this along the axis takes the colour too (a pale patch on the bottle's base).
#  below: (axis, cm) / far: only there does the mask apply (below that value / further than far from the grip along the long axis).
#  wood: (hue degrees, saturation, value, largest hue that counts; all in linear colour; the game's lighting darkens and reddens it, so what reads as brown there is about 25, 0.74, 0.28): evens the wood in the base picture to one rich brown (owner: brown, a little variance, no drastic change, just a
#  touch of grain). Keeps 15% of each pixel's own hue, 25% of the slow changes in brightness and 60% of the fine grain. Hues above the last number (a gold inlay) are left alone.
#  The grip's other two axes come from what is actually at the grip, not the bounding box.
#  mask: "grey" = low-saturation pixels take the tier (metal parts), "colour" = saturated pixels take it (a potion's liquid), "far" = everything beyond 'far' of the
#  grip along the long axis takes it (a stave but not its leather grip).
import bpy, bmesh, os, sys, math
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

# Frames, from the code that holds them:
#  on the right fist (pickaxe, potion): the working end runs along -Z, grip at the origin (KnightDesign's Xf(GripPos, DownTo(BladeDir)))
#  on the bow's hold (longbow): X along the limbs, Z toward the string, grip at the origin (WeaponDesign K_Bow)
#  on the crossbow's hold: X toward the prod, Z up, the rear grip at the origin
PROPS = {
    "Potion":   dict(file="Potion.glb",   axes=("X", "-Y", "-Z"), length=21.0, grip=("Z", 0.25), mask="colour", also=("Z", 13.0), neck=("Z", -1.0, 4.5)),                       # (held by the neck: the body is wider than a fist)
    "Pickaxe":  dict(file="Pickaxe.glb",  axes=("Y", "X", "-Z"),  length=92.0, grip=("Z", 0.85), mask="grey", below=("Z", -60.0), blade=("Y", "X", 3.4, 2.2, 0.35), wood=(25.0, 0.74, 0.30, 60.0)),
    "Longbow":  dict(file="Longbow.glb",  axes=("Z", "-Y", "X"),  length=156.0, grip=("X", 0.50), mask="grey", far=12.0, widen=("Y", 1.5), wood=(25.0, 0.74, 0.28, 60.0)),
    "Crossbow": dict(file="Crossbow.glb", axes=("-X", "-Y", "Z"), length=88.0, grip=("X", 0.36), mask="grey", wood=(25.0, 0.74, 0.28, 60.0)),
}
names = a[2:] or list(PROPS)
IDX = {"X": 0, "Y": 1, "Z": 2}

def axis_matrix(ax):
    # the model's own x, y, z -> game axes
    m = Matrix.Identity(3)
    for col, s in enumerate(ax):
        sign = -1.0 if s[0] == "-" else 1.0
        for row in range(3): m[row][col] = 0.0
        m[IDX[s[-1]]][col] = sign
    return m

def shrink(img, size, path, srgb=True):
    im = img if img.name.startswith("Graded") else img.copy()   # (a copy of a made-up picture comes out black)
    im.scale(size, size)
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()

for name in names:
    P = PROPS[name]
    f = os.path.join(src, P["file"])
    if not os.path.exists(f):
        print("PROP missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    # (the model's own normals, by face and corner: moving the vertices below doesn't turn them, and the game then lights the model from the wrong side)
    own_normals = {}
    for poly in me.polygons:
        for li_ in poly.loop_indices:
            own_normals[(poly.index, me.loops[li_].vertex_index)] = me.corner_normals[li_].vector.copy()
    # the pictures, by what they feed
    mat = me.materials[0]
    nodes = mat.node_tree.nodes
    pr = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    def tex_of(sock):
        seen = []; stack = [sock]
        while stack:
            s = stack.pop()
            for l in s.links:
                n = l.from_node
                if n.type == "TEX_IMAGE": return n.image
                stack.extend(i for i in n.inputs)
        return None
    base = tex_of(pr.inputs["Base Color"]); normal = tex_of(pr.inputs["Normal"]); mr = tex_of(pr.inputs["Metallic"]) or tex_of(pr.inputs["Roughness"])
    W, H = base.size
    pix = list(base.pixels)
    def colour_at(u, v):
        x = min(W - 1, max(0, int((u % 1.0) * W))); y = min(H - 1, max(0, int((v % 1.0) * H)))
        i = (y * W + x) * 4
        return pix[i], pix[i + 1], pix[i + 2]
    # into the game's frame, at the right size, grip at the origin
    M3 = axis_matrix(P["axes"])
    for v in me.vertices: v.co = M3 @ v.co
    xs = [v.co for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi = Vector((max(c[i] for c in xs) for i in range(3)))
    S = P["length"] / max(hi - lo)
    long_ax, share = P["grip"]; li = IDX[long_ax]
    g = Vector(((lo[i] + hi[i]) * 0.5 for i in range(3)))
    g[li] = lo[li] + (hi[li] - lo[li]) * share
    band = [c for c in xs if abs(c[li] - g[li]) < 0.03 * (hi[li] - lo[li])]   # (centred on what the hand actually holds there)
    if band:
        for i in range(3):
            if i != li: g[i] = (min(c[i] for c in band) + max(c[i] for c in band)) * 0.5
    for v in me.vertices: v.co = (v.co - g) * S
    # one gentle arc along the long axis: take out the stave's own bends, tips toward +axis
    if "bend" in P:
        bi = IDX[P["bend"][0]]; depth = P["bend"][1]
        half = P["length"] * 0.5
        N = 48
        bins = [[] for _ in range(N + 1)]
        for v in me.vertices:
            t = max(0.0, min(1.0, (v.co[li] + half) / (2.0 * half)))
            bins[int(round(t * N))].append(v.co[bi])
        centre = [(sum(b) / len(b)) if b else None for b in bins]
        known = [i for i, c in enumerate(centre) if c is not None]
        for i in range(N + 1):
            if centre[i] is None:
                j = min(known, key=lambda k: abs(k - i)); centre[i] = centre[j]
        # (smoothed, so details on the stave stay)
        smooth = [sum(centre[i + k] for k in (-2, -1, 0, 1, 2) if 0 <= i + k <= N) / sum(1 for k in (-2, -1, 0, 1, 2) if 0 <= i + k <= N) for i in range(N + 1)]
        thick = P.get("thick", 1.0)
        oi = 3 - li - bi
        for v in me.vertices:
            x = v.co[li]; t = max(0.0, min(1.0, (x + half) / (2.0 * half)))
            f_ = t * N; i0 = min(N - 1, int(f_)); c = smooth[i0] + (smooth[i0 + 1] - smooth[i0]) * (f_ - i0)
            v.co[bi] = (v.co[bi] - c) * thick + depth * (x * x) / (half * half)
            v.co[oi] *= thick
    if "squeeze" in P:
        a0, a1, sq = P["squeeze"]
        lo2 = min(v.co[li] for v in me.vertices); hi2 = max(v.co[li] for v in me.vertices)
        for v in me.vertices:
            t = max(0.0, min(1.0, ((v.co[li] - lo2) / (hi2 - lo2) - a0) / (a1 - a0)))
            k = 1.0 + (sq - 1.0) * t * t * (3.0 - 2.0 * t)
            for i in range(3):
                if i != li: v.co[i] *= k
    if "widen" in P:
        wi = IDX[P["widen"][0]]
        for v in me.vertices: v.co[wi] *= P["widen"][1]
    if "neck" in P:
        ni = IDX[P["neck"][0]]
        for v in me.vertices:
            if v.co[ni] < P["neck"][1]: v.co[ni] -= P["neck"][2]
    if "blade" in P:
        si, ti = IDX[P["blade"][0]], IDX[P["blade"][1]]
        st, root_, tip_ = P["blade"][2:]
        far_ = max(abs(v.co[si]) for v in me.vertices)
        flat = max((abs(v.co[ti]) for v in me.vertices if abs(v.co[si]) > st + 2.0), default=1.0)
        for v in me.vertices:
            d = abs(v.co[si])
            if d > st:
                t = min(1.0, (d - st) / max(far_ - st, 1.0))
                k = min(1.0, (d - st) / 1.5)   # (eases out of the collar)
                want = (v.co[ti] / max(flat, 1e-4)) * (root_ + (tip_ - root_) * t)
                v.co[ti] = v.co[ti] + (want - v.co[ti]) * k
    # one rich brown for the wood
    if "wood" in P:
        import numpy as np
        hue_, sat_, val_, hue_max = P["wood"]
        arr = np.array(pix, dtype=np.float32).reshape(H, W, 4)
        r_, g_c, b_ = arr[..., 0], arr[..., 1], arr[..., 2]
        V = arr[..., :3].max(axis=2); C = V - arr[..., :3].min(axis=2)
        S = C / np.maximum(V, 1e-4)
        Hu = np.degrees(np.arctan2(np.sqrt(3.0) * (g_c - b_), 2.0 * r_ - g_c - b_)) % 360.0   # (near enough for browns)
        wood = (S > 0.35) & (V > 0.015) & ((Hu < hue_max) | (Hu > 340.0))
        def blur(a_, rad):
            for ax in (0, 1):
                c = np.cumsum(a_, axis=ax, dtype=np.float64)
                n = a_.shape[ax]
                hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
                t = np.take(c, hi_, axis=ax)
                b_v = np.take(c, np.maximum(lo_, 0), axis=ax)
                shape = [1, 1]; shape[ax] = n
                a_ = t - np.where(lo_.reshape(shape) < 0, 0.0, b_v)
            return a_
        rad = max(4, W // 28)
        wt = blur(wood.astype(np.float64), rad)
        slow = blur(V * wood, rad) / np.maximum(wt, 1.0)   # (brightness in the wood nearby, not counting what is not wood)
        fine = np.where(wood, V / np.maximum(slow, 0.02), 1.0)
        mean_ = max(float(V[wood].mean()) if wood.any() else 0.4, 0.02)
        Vn = val_ * (1.0 + 0.6 * (fine - 1.0)) * np.power(np.maximum(slow, 0.02) / mean_, 0.25)
        dh = ((Hu + 180.0) % 360.0 - 180.0) - hue_
        Hn = np.radians(hue_ + 0.15 * np.clip(dh, -40.0, 40.0))
        Sn = np.clip(sat_ + 0.15 * (S - sat_), 0.0, 1.0)
        # (back to RGB from hue, saturation, value, for hues in the browns)
        Vn = np.clip(Vn, 0.0, 1.0)
        h6 = (np.degrees(Hn) % 360.0) / 60.0
        X_ = 1.0 - np.abs(h6 % 2.0 - 1.0)
        Cn = Vn * Sn; Xn = Cn * X_; m_ = Vn - Cn
        Rn = np.where(h6 < 1.0, Cn, np.where(h6 >= 5.0, Cn, Xn)); Gn = np.where(h6 < 1.0, Xn, np.where(h6 >= 5.0, 0.0, Cn)); Bn = np.where(h6 >= 5.0, Xn, 0.0)
        arr[..., 0] = np.where(wood, Rn + m_, r_); arr[..., 1] = np.where(wood, Gn + m_, g_c); arr[..., 2] = np.where(wood, Bn + m_, b_)
        pix = arr.reshape(-1).tolist()
        graded = bpy.data.images.new("Graded", W, H, alpha=True)   # (a packed picture copies from its file, so the change goes in a new one)
        graded.pixels.foreach_set(arr.reshape(-1))
        graded.update()
        base = graded
        print("PROP", name, "wood %.0f%% of the picture" % (100.0 * wood.mean()))
    # the mask
    uv = me.uv_layers.active.data
    if "Mask" in me.color_attributes: me.color_attributes.remove(me.color_attributes["Mask"])
    ca = me.color_attributes.new("Mask", "BYTE_COLOR", "CORNER")
    vm = {}
    for poly in me.polygons:
        for li_ in poly.loop_indices:
            r, g_, b = colour_at(*uv[li_].uv)
            mx, mn = max(r, g_, b), min(r, g_, b)
            sat = (mx - mn) / max(mx, 1e-4)
            vi = me.loops[li_].vertex_index
            if P["mask"] == "grey": val = 1.0 if sat < 0.30 or mx < 0.06 else 0.0   # (dark steel counts, whatever faint hue it has)
            elif P["mask"] == "colour": val = 1.0 if sat > 0.45 and g_ < 0.35 * r else 0.0   # (the red liquid, not the tan cork)
            else: val = 0.0
            if "also" in P and me.vertices[vi].co[IDX[P["also"][0]]] > P["also"][1]: val = 1.0
            if "far" in P and abs(me.vertices[vi].co[li]) <= P["far"]: val = 0.0
            if "below" in P and me.vertices[vi].co[IDX[P["below"][0]]] >= P["below"][1]: val = 0.0
            ca.data[li_].color = (val, val, val, 1.0)
    me.update()
    # the pictures
    shrink(base, 1024, os.path.join(out, "T_%s_Base.png" % name))
    if normal: shrink(normal, 512, os.path.join(out, "T_%s_Normal.png" % name))
    if mr: shrink(mr, 512, os.path.join(out, "T_%s_MR.png" % name))
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    xs = [v.co for v in me.vertices]
    print("PROP", name, "tris", tris, "lo", tuple(round(min(c[i] for c in xs), 1) for i in range(3)), "hi", tuple(round(max(c[i] for c in xs), 1) for i in range(3)),
          "masked %.0f%%" % (100.0 * sum(c.color[0] > 0.5 for c in ca.data) / max(1, len(ca.data))))
    fbx = os.path.join(out, name + ".fbx")
    # (Blender's y -> the game's -y under the importer's scene conversion, so y is flipped on the way out: what comes in has the frame above)
    for v in me.vertices: v.co.y = -v.co.y
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(me); bm.free(); me.update()
    # the normals, turned the same way
    turned = [None] * len(me.loops)
    for poly in me.polygons:
        for li_ in poly.loop_indices:
            n_ = M3 @ own_normals[(poly.index, me.loops[li_].vertex_index)]
            turned[li_] = (n_.x, -n_.y, n_.z)
    me.normals_split_custom_set(turned)
    me.update()
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", colors_type="LINEAR", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("PROP wrote", fbx)
