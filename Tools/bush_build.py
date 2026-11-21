# The new bushes (2026-11-18, owner: "too many bushes ... not colored correctly ... too cartoony and not fluffy ... Let's do an overhaul").
# Each bush is a dense shell of many small leaf-spray cards cut from the DZ tree packs' own leaf atlases (the aspen's and the cork oak's
# sprays), so the bushes wear the very leaves, colours and wind of the pack trees round Oak Hill. Second pass (same evening): the first
# bushes used a hundred big cards and read as cabbages and agaves; now a 2 m bush is about 900 cards of 30-45 cm, each a twig of a dozen
# or more leaves a few cm long, packed over a rounded shell that reaches the ground, with the tips poking past it for a soft broken edge,
# and an inner layer so the middle can't be seen through. Normals point out from the bush's middle so it lights as one soft mass.
# Vertex colour red/green carries the pack material's wind weight (0 at a card's base, more at its tip).
#
#   blender -b --factory-startup -P Tools/bush_build.py -- <folder with the atlas TGAs> <output folder>
#
# The atlases come from the packs (Tools/bush_atlas_export.py writes them as TGA); only their alpha is read here, to find each spray's
# rectangle. Output: one FBX per kind and LOD (SM_Bush<Kind>.fbx, SM_Bush<Kind>_LOD1.fbx), imported by Tools/BuildBushes.ps1.
import bpy, bmesh, sys, os, math, random
import numpy as np
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
TEX, OUT = args[0], args[1]
os.makedirs(OUT, exist_ok=True)

def sprays(name, cols, min_w, stem_cut):
    """Rectangles (u0, v0, u1, v1 in Blender UV: v up, pixel w, pixel h) of the sprays in an atlas, from its alpha, split by empty
    columns. stem_cut drops that share of each spray's bottom (the bare stalk), so every card is leaves from base to tip."""
    im = bpy.data.images.load(os.path.join(TEX, name + '.tga'))
    W, H = im.size
    w = cols; h = max(64, int(H * cols / W))
    im.scale(w, h)
    a = np.empty(w * h * 4, dtype=np.float32); im.pixels.foreach_get(a)
    a = a.reshape(h, w, 4)[:, :, 3] > 0.5          # rows bottom-up
    colany = a.sum(axis=0) > 1
    out = []; x = 0
    while x < w:
        if not colany[x]: x += 1; continue
        x0 = x
        while x < w and colany[x]: x += 1
        x1 = x
        if x1 - x0 < min_w: continue
        rows = np.where(a[:, x0:x1].any(axis=1))[0]
        y0, y1 = rows.min(), rows.max() + 1
        y0 = y0 + int((y1 - y0) * stem_cut)
        pad = 1
        out.append(((x0 - pad) / w, max(0, y0 - pad) / h, min(w, x1 + pad) / w, min(h, y1 + pad) / h, (x1 - x0) * W / w, (y1 - y0) * H / h))
    bpy.data.images.remove(im)
    print('BUSHB sprays %s: %d %s' % (name, len(out), ' '.join('[%.3f %.3f %.3f %.3f]' % r[:4] for r in out)))
    return out

ASPEN = sprays('T_Aspen_Leaves_Diffuse', 1024, 20, 0.2)
OAK = sprays('T_Cork_Oak_Leaves_Diffuse', 1024, 20, 0.15)

def clear():
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes): bpy.data.meshes.remove(m)

class Builder:
    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.col = self.bm.loops.layers.color.new('Col')
        self.norms = []   # per loop custom normals, in face/loop order
        self.cards = 0

    def card(self, rect, base, d, side, length, droop, nrm_at, roundness, wind, wind0, segs):
        """One spray card, one quad wide: from base along d (unit); side is the card's width axis (unit, perpendicular to d)."""
        u0, v0, u1, v1, pw, ph = rect
        width = length * pw / ph
        up = Vector((0, 0, 1))
        rows = []
        for s in range(segs + 1):
            t = s / segs
            p = base + d * (length * t) - up * (droop * length * t * t)
            rows.append([(p + side * (width * k), (u0 + (u1 - u0) * (k + 0.5), v0 + (v1 - v0) * t), t) for k in (-0.5, 0.5)])
        verts = [[self.bm.verts.new(q[0]) for q in row] for row in rows]
        fn = d.cross(side).normalized()
        for s in range(segs):
            quad = [(s, 0), (s, 1), (s + 1, 1), (s + 1, 0)]
            f = self.bm.faces.new([verts[a][b] for a, b in quad])
            for loop, (a, b) in zip(f.loops, quad):
                pos, uv, t = rows[a][b]
                loop[self.uv].uv = uv
                w = wind0 + (wind - wind0) * t
                loop[self.col] = (w, w * 0.6, 0.0, 1.0)
                out = nrm_at(Vector(pos))
                nf = fn if fn.dot(out) >= 0 else -fn
                self.norms.append((out * roundness + nf * (1 - roundness)).normalized())
        self.cards += 1

    def finish(self, name):
        me = bpy.data.meshes.new(name)
        self.bm.to_mesh(me)
        self.bm.free()
        me.materials.append(bpy.data.materials.get('Leaves') or bpy.data.materials.new('Leaves'))
        me.normals_split_custom_set([tuple(n) for n in self.norms])
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        tris = sum(len(p.vertices) - 2 for p in me.polygons)
        print('BUSHB %s: %d cards, %d triangles, size %s' % (name, self.cards, tris, tuple(round(x, 2) for x in ob.dimensions)))
        return ob

def export(ob, path):
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, apply_unit_scale=True, global_scale=1.0, apply_scale_options='FBX_SCALE_NONE',
                             object_types={'MESH'}, mesh_smooth_type='OFF', use_mesh_modifiers=False, axis_forward='-Y', axis_up='Z',
                             colors_type='LINEAR', use_tspace=False, add_leaf_bones=False, bake_anim=False)

def shell(name, seed, n, ax, ay, h, rects, length, m=2.0, keep=1.0, inner=0.25, droop=0.1, wind=0.5, lift=0.42, lumps=0.14):
    """A bush: n spray cards over a rounded shell (a superellipsoid of half sizes ax x ay x (h/2+), exponent m: 2 round, 4 boxy for the
    hedge) whose bottom is cut flat at the ground. Each card sits on the shell (or, for the 'inner' share, deeper in to close the middle),
    lies roughly along the surface facing out and leans up and out, so its tip pokes past the shell. keep < 1 is the far LOD: fewer,
    larger, single-segment cards."""
    r = random.Random(seed)
    b = Builder()
    rl = random.Random(seed * 31 + 7)         # the same lumps for both LODs
    lobes = [(Vector((rl.uniform(-1, 1), rl.uniform(-1, 1), rl.uniform(-0.5, 0.5))).normalized() * rl.uniform(2.0, 4.5), rl.uniform(0, 6.3)) for _ in range(5)]
    def lump(v):                        # a natural, uneven outline: a few broad bulges and dips round the shell
        return 1.0 + lumps * sum(math.sin(v.dot(k) + ph) for k, ph in lobes) / 2.2
    cz = h * lift                       # the shell's middle
    az = h - cz                         # half height above the middle; below it the shell is cut by the ground
    A = Vector((ax, ay, az))
    def surf(v):                        # superellipsoid level of point v (1 on the shell)
        q = Vector((v.x / A.x, v.y / A.y, (v.z - cz) / A.z))
        return (abs(q.x) ** m + abs(q.y) ** m + abs(q.z) ** m) ** (1.0 / m)
    def nrm_at(p):                      # the shell's outward normal (rounded: from the gradient, softened toward the middle)
        q = Vector((p.x / A.x, p.y / A.y, (p.z - cz) / A.z))
        g = Vector((math.copysign(abs(q.x) ** (m - 1), q.x) / A.x, math.copysign(abs(q.y) ** (m - 1), q.y) / A.y,
                    math.copysign(abs(q.z) ** (m - 1), q.z) / A.z))
        g = g.normalized() if g.length > 1e-6 else Vector((0, 0, 1))
        c = (p - Vector((0, 0, cz))); c = c.normalized() if c.length > 1e-4 else Vector((0, 0, 1))
        return (g * 0.6 + c * 0.4 + Vector((0, 0, 0.25))).normalized()
    # sample points on the shell roughly evenly by area: points on the bounding box's faces, weighted by face area, pulled to the shell
    faces = [((0, ax), ay * az * 2), ((0, -ax), ay * az * 2), ((1, ay), ax * az * 2), ((1, -ay), ax * az * 2), ((2, az), ax * ay * 2.4)]
    tot = sum(f[1] for f in faces)
    count = int(n * keep)
    grow = 1.0 / math.sqrt(keep)
    segs = 2 if keep >= 1.0 else 1
    made = 0; tries = 0
    while made < count and tries < count * 20:
        tries += 1
        k = r.random() * tot
        for (axis, val), wgt in faces:
            k -= wgt
            if k <= 0: break
        p = [r.uniform(-ax, ax), r.uniform(-ay, ay), cz + r.uniform(-az, az)]
        p[axis] = val + (cz if axis == 2 else 0)
        p = Vector(p)
        if p.z < 0.0: continue
        s = surf(p)
        p = Vector((p.x / s, p.y / s, cz + (p.z - cz) / s))
        off = Vector((p.x, p.y, p.z - cz)); f = lump(off.normalized() if off.length > 1e-4 else Vector((0, 0, 1)))
        p = Vector((p.x * f, p.y * f, cz + (p.z - cz) * f))
        depth = 1.0 if r.random() > inner else r.uniform(0.45, 0.85)
        p = Vector((p.x * depth, p.y * depth, cz + (p.z - cz) * depth))
        if p.z < 0.03: p.z = r.uniform(0.03, 0.12)          # the bottom rim sits on the ground
        nrm = nrm_at(p)
        # along the surface (random direction in the tangent plane), leaning out and up
        t = Vector((r.uniform(-1, 1), r.uniform(-1, 1), r.uniform(-1, 1)))
        t = (t - nrm * t.dot(nrm))
        if t.length < 1e-3: continue
        t.normalize()
        d = (t * 0.75 + nrm * 0.55 + Vector((0, 0, 0.3))).normalized()
        if p.z < 0.25 * h and d.z < 0.1: d = (d + Vector((0, 0, 0.3))).normalized()
        side = d.cross(nrm)
        side = side.normalized() if side.length > 1e-3 else d.orthogonal().normalized()
        ang = r.uniform(-0.6, 0.6)                          # a little twist about the stem
        side = (side * math.cos(ang) + d.cross(side) * math.sin(ang)).normalized()
        stray = r.random() < 0.06          # a few longer twigs reach out past the rest
        L = length * r.uniform(0.8, 1.2) * (1.5 if stray else 1.0) * (0.85 if depth < 1 else 1.0) * min(grow, 1.6)
        base = p - d * (L * 0.5)
        if base.z < 0.0: base.z = 0.0
        w0 = 0.05 + 0.25 * min(1.0, p.z / h)               # sway grows toward the top and out to the tips
        WS = 0.4                                            # (owner 2026-11-21: bushes swayed too much; 60% less wind)
        b.card(rects[r.randrange(len(rects))], base, d, side, L, droop * r.uniform(0.5, 1.5), nrm_at, 0.8, WS * (w0 + wind * 0.5), WS * w0, segs)
        made += 1
    return b.finish(name)

KINDS = {
    # name: kwargs of shell(); LOD1 keeps 30% of the cards, each grown to cover
    'BushFull':  dict(seed=11, n=900, ax=0.88, ay=0.8, h=1.5, rects=ASPEN, length=0.42),
    'BushSmall': dict(seed=23, n=420, ax=0.62, ay=0.58, h=1.0, rects=ASPEN, length=0.32),
    'BushFine':  dict(seed=37, n=760, ax=0.82, ay=0.78, h=1.45, rects=OAK, length=0.36),
    'BushLow':   dict(seed=41, n=720, ax=1.1, ay=0.9, h=1.0, rects=ASPEN, length=0.32, lift=0.25, lumps=0.18),
    'BushHedge': dict(seed=53, n=1000, ax=1.2, ay=0.42, h=1.2, rects=OAK, length=0.3, m=4.0, lift=0.45, wind=0.3, lumps=0.05),
}
clear()
for name, kw in KINDS.items():
    for lod, keep in ((0, 1.0), (1, 0.3)):
        ob = shell('SM_' + name + ('' if lod == 0 else '_LOD1'), keep=keep, **kw)
        export(ob, os.path.join(OUT, ob.name + '.fbx'))
        bpy.data.objects.remove(ob, do_unlink=True)
print('BUSHB done')
