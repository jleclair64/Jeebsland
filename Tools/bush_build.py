# The new bushes (2026-11-18, owner: "too many bushes ... not colored correctly ... too cartoony and not fluffy ... Let's do an overhaul").
# Each bush is a cluster of leaf-spray cards cut from the DZ tree packs' own leaf atlases (the columnar aspen's and the cork oak's sprays),
# so the bushes wear the very leaves, colours and wind of the pack trees round Oak Hill. Cards start near the bush's heart and reach out
# with a gentle droop; their normals point out from the bush's middle so it lights as one soft round mass (fluffy, not a heap of flat
# cards). Vertex colour red/green carries the pack material's wind weight (0 at a card's base, more at its tip).
#
#   blender -b --factory-startup -P Tools/bush_build.py -- <folder with the atlas TGAs> <output folder>
#
# The atlases come from the packs (Saved/Scratch_bush/bush_export.py writes them as TGA); only their alpha is read here, to find each
# spray's rectangle. Output: one FBX per kind and LOD (SM_Bush<Kind>.fbx, SM_Bush<Kind>_LOD1.fbx), imported by Tools/BuildBushes.ps1.
import bpy, bmesh, sys, os, math, random
import numpy as np
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index('--') + 1:]
TEX, OUT = args[0], args[1]
os.makedirs(OUT, exist_ok=True)

def sprays(name, cols, rows_scale, min_w):
    """Rectangles (u0, v0, u1, v1 in Blender UV: v up) of the sprays in an atlas, from its alpha, split by empty columns."""
    im = bpy.data.images.load(os.path.join(TEX, name + '.tga'))
    W, H = im.size
    w = cols; h = max(64, int(H * cols / W * rows_scale))
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
        pad = 1
        out.append(((x0 - pad) / w, max(0, y0 - pad) / h, min(w, x1 + pad) / w, min(h, y1 + pad) / h, (x1 - x0) * W / w, (y1 - y0) * H / h))
    bpy.data.images.remove(im)
    print('BUSHB sprays %s: %d %s' % (name, len(out), ' '.join('[%.3f %.3f %.3f %.3f]' % r[:4] for r in out)))
    return out

ASPEN = sprays('T_Aspen_Leaves_Diffuse', 1024, 1.0, 20)
ASPEN_S = sprays('T_Aspen_Leaves_Small_Diffuse', 2048, 1.0, 20)
OAK = sprays('T_Cork_Oak_Leaves_Diffuse', 1024, 1.0, 20)

def clear():
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
    for m in list(bpy.data.meshes): bpy.data.meshes.remove(m)

def mat(name):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    return m

class Builder:
    def __init__(self, slots):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.col = self.bm.loops.layers.color.new('Col')
        self.slots = slots
        self.norms = []   # per loop custom normals, in face/loop order
        self.cards = 0

    def card(self, slot, rect, base, d, side, length, droop, centre, roundness, wind, segs=3):
        """One spray card: from base along d (unit), side is the card's width axis (unit, perpendicular to d)."""
        u0, v0, u1, v1, pw, ph = rect
        width = length * pw / ph
        up = Vector((0, 0, 1))
        rows = []
        for s in range(segs + 1):
            t = s / segs
            p = base + d * (length * t) - up * (droop * length * t * t)
            row = []
            for k in (-0.5, 0.0, 0.5):
                row.append((p + side * (width * k), (u0 + (u1 - u0) * (k + 0.5), v0 + (v1 - v0) * t), t))
            rows.append(row)
        verts = [[self.bm.verts.new(q[0]) for q in row] for row in rows]
        fn = d.cross(side).normalized()
        for s in range(segs):
            for k in range(2):
                quad = [(s, k), (s, k + 1), (s + 1, k + 1), (s + 1, k)]
                f = self.bm.faces.new([verts[a][b] for a, b in quad])
                f.material_index = slot
                for loop, (a, b) in zip(f.loops, quad):
                    pos, uv, t = rows[a][b]
                    loop[self.uv].uv = uv
                    w = wind * t
                    loop[self.col] = (w, w * 0.6, 0.0, 1.0)
                    out = (Vector(pos) - centre)
                    out = out.normalized() if out.length > 1e-3 else up
                    nf = fn if fn.dot(out) >= 0 else -fn
                    self.norms.append((out * roundness + nf * (1 - roundness)).normalized())
        self.cards += 1

    def finish(self, name):
        me = bpy.data.meshes.new(name)
        self.bm.to_mesh(me)
        self.bm.free()
        for s in self.slots: me.materials.append(mat(s))
        # (custom normals: the loops come out in the order the faces were made)
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

def rand_unit_perp(d, r):
    a = Vector((r.uniform(-1, 1), r.uniform(-1, 1), r.uniform(-1, 1)))
    s = a - d * a.dot(d)
    if s.length < 1e-3: s = d.orthogonal()
    return s.normalized()

def dome(name, seed, n, radius, height, rects, slot, inner_rects=None, inner_slot=1, inner_n=0, droop=0.18, wind=0.45, keep=1.0,
         stretch=(1.0, 1.0), low=False, slots=('Leaves', 'LeavesSmall')):
    """A round shrub: cards from inside an inner ellipsoid out to the shell. keep < 1 drops cards (the far LOD), the rest grow to cover."""
    r = random.Random(seed)
    b = Builder(list(slots))
    centre = Vector((0, 0, height * (0.18 if low else 0.38)))
    grow = 1.0 / math.sqrt(keep)
    def one(rects_, slot_, length_k, base_k):
        # direction: over the dome, more to the sides and up than down
        while True:
            z = r.uniform(-0.2, 1.0) if not low else r.uniform(0.45, 0.95)
            phi = r.uniform(0, 2 * math.pi)
            q = math.sqrt(max(0.0, 1 - z * z))
            d = Vector((q * math.cos(phi) * stretch[0], q * math.sin(phi) * stretch[1], z * height / radius * (0.9 if not low else 1.0)))
            if d.length > 1e-3: break
        d.normalize()
        base = Vector((r.uniform(-1, 1) * radius * base_k * stretch[0], r.uniform(-1, 1) * radius * base_k * stretch[1],
                       r.uniform(0.05, 0.45) * height if not low else r.uniform(0.0, 0.08)))
        # reach out to about the shell: an ellipsoid of radius x radius x height
        ex = Vector((radius * stretch[0], radius * stretch[1], height))
        tgt = centre + Vector((d.x * ex.x, d.y * ex.y, d.z * (height - centre.z if d.z > 0 else centre.z)))
        L = max(0.15, (tgt - base).length * length_k * r.uniform(0.85, 1.1))
        dd = (tgt - base).normalized()
        if dd.z < -0.2: dd.z = -0.2; dd.normalize()
        side = rand_unit_perp(dd, r)
        rect = rects_[r.randrange(len(rects_))]
        b.card(slot_, rect, base, dd, side, L * min(grow, 1.25), droop * r.uniform(0.6, 1.3), centre, 0.75, wind)
    for i in range(int(n * keep)): one(rects, slot, 1.0, 0.3)
    if inner_rects:
        for i in range(int(inner_n * keep)): one(inner_rects, inner_slot, 0.75, 0.45)
    return b.finish(name)

def hedge(name, seed, n, length, depth, height, rects, slot, keep=1.0):
    """A hedge: a long rounded box of fine-leaved sprays, ends rounded, top slightly domed."""
    r = random.Random(seed)
    b = Builder(['Leaves'])
    grow = 1.0 / math.sqrt(keep)
    for i in range(int(n * keep)):
        x = r.uniform(-0.5, 0.5) * (length - depth * 0.6)
        base = Vector((x, r.uniform(-0.15, 0.15) * depth, r.uniform(0.1, 0.75) * height))
        # aim at a point on the shell: pick a face of the rounded box
        f = r.random()
        if f < 0.4:   tgt = Vector((x + r.uniform(-0.3, 0.3), depth * 0.5 * r.choice((-1, 1)), r.uniform(0.15, 0.95) * height))
        elif f < 0.75: tgt = Vector((x + r.uniform(-0.3, 0.3), r.uniform(-0.45, 0.45) * depth, height * r.uniform(0.95, 1.05)))
        elif f < 0.88: tgt = Vector((length * 0.5 * r.choice((-1, 1)), r.uniform(-0.4, 0.4) * depth, r.uniform(0.15, 0.95) * height))
        else:          tgt = Vector((x, r.uniform(-0.45, 0.45) * depth, r.uniform(0.0, 0.15) * height)) + Vector((0, 0.5 * depth * r.choice((-1, 1)), 0))
        dd = (tgt - base)
        L = max(0.2, dd.length * r.uniform(0.75, 0.95))
        dd.normalize()
        side = rand_unit_perp(dd, r)
        centre = Vector((x, 0, height * 0.45))
        b.card(slot, rects[r.randrange(len(rects))], base, dd, side, L * min(grow, 1.2), 0.1 * r.uniform(0.5, 1.5), centre, 0.7, 0.3)
    return b.finish(name)

KINDS = {
    # name: (builder, kwargs); LOD1 keeps ~40% of the cards, each grown to cover
    'BushFull':  (dome, dict(seed=11, n=90, radius=0.95, height=1.35, rects=ASPEN, slot=0, inner_rects=ASPEN_S, inner_n=60, droop=0.2)),
    'BushSmall': (dome, dict(seed=23, n=40, radius=0.55, height=0.75, rects=ASPEN, slot=0, inner_rects=ASPEN_S, inner_n=30, droop=0.18)),
    'BushFine':  (dome, dict(seed=37, n=110, radius=0.75, height=1.25, rects=OAK, slot=0, droop=0.14, slots=('Leaves',))),
    'BushLow':   (dome, dict(seed=41, n=44, radius=0.7, height=0.65, rects=OAK, slot=0, droop=0.6, low=True, wind=0.6, slots=('Leaves',))),
}
clear()
for name, (fn, kw) in KINDS.items():
    for lod, keep in ((0, 1.0), (1, 0.4)):
        ob = fn('SM_' + name + ('' if lod == 0 else '_LOD1'), keep=keep, **kw)
        export(ob, os.path.join(OUT, ob.name + '.fbx'))
        bpy.data.objects.remove(ob, do_unlink=True)
for lod, keep in ((0, 1.0), (1, 0.4)):
    ob = hedge('SM_BushHedge' + ('' if lod == 0 else '_LOD1'), 53, 200, 2.4, 0.85, 1.1, OAK, 0, keep=keep)
    export(ob, os.path.join(OUT, ob.name + '.fbx'))
    bpy.data.objects.remove(ob, do_unlink=True)
print('BUSHB done')
