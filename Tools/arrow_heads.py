# Distinct arrows (2026-11-26, Docs/ThursdayList.md item 13): one shared shaft and fletching (the cleaned Meshy arrow, Saved/KnightBuild/Props/Arrow.fbx from
# Tools/BuildProps.ps1 Arrow) with a head built here for each kind, in the item style (crisp bevels, a little bolder than life). Run by Tools/BuildArrows.ps1:
#   blender -b -P arrow_heads.py -- <Arrow.fbx> <out folder>
# Writes Arrow_<Kind>.fbx beside the shaft's pictures. The Broadhead keeps the Meshy arrow's own head (it is one already).
# Frame: the tip at the origin, the shaft back along -X, centimetres. The vertex colour "Mask": red takes the arrow's head colour (JeebItems::ArrowTipColor),
# green the feathers' (ArrowFeatherColor). New faces take their picture from a spot on the old head (metal) or on the shaft (wood) by UV.
import bpy, bmesh, sys, os, math
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = args[0], args[1]
os.makedirs(OUT, exist_ok=True)

def load():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=SRC)
    o = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
    o.scale = (1, 1, 1); o.location = (0, 0, 0); o.rotation_euler = (0, 0, 0)
    return o

def uv_spot(me, test):
    # the middle UV of the face nearest the middle of those passing test (a spot inside one picture island)
    uv = me.uv_layers[0].data
    faces = [p for p in me.polygons if test(p.center)]
    c = sum((p.center for p in faces), Vector()) / max(1, len(faces))
    f = min(faces, key=lambda p: (p.center - c).length)
    return sum((uv[li].uv for li in f.loop_indices), Vector((0, 0))) / f.loop_total

def lathe(bm, prof, sides, sy=1.0, sz=1.0, rot=0.0):
    # prof: [(x, r)] from back to front; r 0 closes to a point. Flat-shaded rings (crisp facets).
    rings = []
    for x, r in prof:
        if r <= 1e-4: rings.append([bm.verts.new((x, 0, 0))]); continue
        rings.append([bm.verts.new((x, r * sy * math.cos(rot + 2 * math.pi * k / sides), r * sz * math.sin(rot + 2 * math.pi * k / sides))) for k in range(sides)])
    faces = []
    if len(rings[0]) > 1: faces.append(bm.faces.new(list(reversed(rings[0]))))
    for a, b in zip(rings, rings[1:]):
        if len(a) == 1 and len(b) == 1: continue
        if len(b) == 1:
            for k in range(sides): faces.append(bm.faces.new((a[k], a[(k + 1) % sides], b[0])))
        elif len(a) == 1:
            for k in range(sides): faces.append(bm.faces.new((a[0], b[(k + 1) % sides], b[k])))
        else:
            for k in range(sides): faces.append(bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k])))
    if len(rings[-1]) > 1: faces.append(bm.faces.new(rings[-1]))
    return faces

def blade(bm, prof, thick):
    # a flat two-edged blade: diamond section (a centre ridge, bevelled to both edges); prof [(x, half width)] back to front
    return lathe(bm, prof, 4, 1.0, 1.0, 0.0) if thick is None else lathe_scaled(bm, prof, thick)

def lathe_scaled(bm, prof, thick):
    # 4 sides: +Y edge, +Z ridge, -Y edge, -Z ridge; the ridge stays 'thick' (cm) whatever the width
    rings = []
    for x, w in prof:
        if w <= 1e-4: rings.append([bm.verts.new((x, 0, 0))]); continue
        t = min(thick, w * 0.6)
        rings.append([bm.verts.new((x, w, 0)), bm.verts.new((x, 0, t)), bm.verts.new((x, -w, 0)), bm.verts.new((x, 0, -t))])
    faces = []
    if len(rings[0]) > 1: faces.append(bm.faces.new(list(reversed(rings[0]))))
    for a, b in zip(rings, rings[1:]):
        if len(b) == 1:
            for k in range(4): faces.append(bm.faces.new((a[k], a[(k + 1) % 4], b[0])))
        else:
            for k in range(4): faces.append(bm.faces.new((a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k])))
    return faces

SR = 0.62   # the shaft's radius
SOCK = [(-13.0, SR + 0.12), (-12.4, SR + 0.2), (-9.0, SR + 0.16)]   # a socket over the end of the shaft

def build(kind):
    o = load(); me = o.data
    metal_uv = uv_spot(me, lambda c: -6.0 < c.x < -2.0)
    wood_uv = uv_spot(me, lambda c: -40.0 < c.x < -20.0)
    feather = [li for p in me.polygons if p.center.x < -50.0 for li in p.loop_indices]
    if kind != "Broadhead":
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_center_median().x > -12.0], context="FACES")
        bm.to_mesh(me); bm.free()
    if kind == "Eagle":   # bigger, swept-back feathers: the fletching pushed out and back
        for v in me.vertices:
            if v.co.x < -50.0:
                r = math.hypot(v.co.y, v.co.z)
                if r > 0.9: s = 1.0 + 0.4 * min(1.0, (r - 0.9) / 1.5); v.co.y *= s; v.co.z *= s; v.co.x -= 2.5 * min(1.0, (r - 0.9) / 1.5)
    # the new head, as its own object (its own flat facets), joined on after
    hm = bpy.data.meshes.new("Head"); bm = bmesh.new()
    metal, wood = [], []
    if kind == "Cheap":   # no metal: the shaft whittled to a point, fire-hardened
        wood += lathe(bm, [(-13.0, SR), (-6.0, SR * 0.95), (-2.0, SR * 0.45), (0.0, 0.0)], 8)
    elif kind == "Bodkin":   # a long slim square needle that punches through mail
        metal += lathe(bm, SOCK, 8)
        metal += lathe(bm, [(-9.2, SR + 0.1), (-8.0, 0.62), (-4.0, 0.42), (0.0, 0.0)], 4, rot=math.pi / 4)
    elif kind == "Heavy":   # a thick blunt ogive with a collar: weight up front
        metal += lathe(bm, [(-13.0, SR + 0.14), (-11.2, 1.05), (-10.6, 1.05), (-10.0, SR + 0.2)], 8)
        metal += lathe(bm, [(-10.2, SR + 0.2), (-9.0, 1.25), (-6.5, 1.45), (-3.5, 1.2), (-1.2, 0.6), (0.0, 0.0)], 8, rot=math.pi / 8)
    elif kind == "Fire":   # a small blade and a wrapped bundle behind it that holds the flame
        metal += lathe(bm, SOCK, 8)
        metal += lathe_scaled(bm, [(-9.2, 0.5), (-7.5, 1.35), (-5.5, 1.25), (0.0, 0.0)], 0.32)
        metal += lathe(bm, [(-16.5, SR + 0.05), (-15.8, 1.15), (-14.6, 1.35), (-13.4, 1.25), (-12.6, 1.4), (-11.4, 1.1), (-10.8, SR + 0.2)], 9, rot=0.3)
    elif kind == "Eagle":   # a long leaf blade, the finest head
        metal += lathe(bm, SOCK, 8)
        metal += lathe(bm, [(-11.0, 0.95), (-10.4, 0.95)], 8)
        metal += lathe_scaled(bm, [(-9.2, 0.45), (-7.0, 1.45), (-4.5, 1.6), (-2.0, 0.9), (0.0, 0.0)], 0.38)
    if bm.faces:
        bm.to_mesh(hm)
    bm.free()
    if len(hm.polygons):
        ho = bpy.data.objects.new("Head", hm); bpy.context.scene.collection.objects.link(ho)
        hm.uv_layers.new(name=me.uv_layers[0].name)
        ca = hm.color_attributes.new("Mask", "BYTE_COLOR", "CORNER")
        nm = len(metal)
        for i, p in enumerate(hm.polygons):
            is_metal = i < nm if kind != "Cheap" else False
            for li in p.loop_indices:
                hm.uv_layers[0].data[li].uv = metal_uv if is_metal else wood_uv
                ca.data[li].color = (1.0, 0.0, 0.0, 1.0) if is_metal else (0.0, 0.0, 0.0, 1.0)
        for p in hm.polygons: p.use_smooth = False
        if me.materials: hm.materials.append(me.materials[0])
        bpy.ops.object.select_all(action="DESELECT")
        ho.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = o
        bpy.ops.object.join()
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    fbx = os.path.join(OUT, "Arrow_%s.fbx" % kind)
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", colors_type="LINEAR", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("ARROW wrote", fbx, len(o.data.polygons), "faces")

for k in ("Cheap", "Broadhead", "Bodkin", "Heavy", "Fire", "Eagle"):
    build(k)
