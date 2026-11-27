# The troll's sack of rocks and his throwing rock (2026-11-22) for Tools/troll_kit_import.py:
#   SM_RockSack: Meshy's sack (Saved/TrollKit/lo_sack.glb), 42 cm tall, its pivot at the tied neck (it hangs from his left fist).
#   SM_TrollRock: a lumpy grey stone about 13 cm across made here (a squashed, noised ball with a painted picture), pivot in the middle.
# Both carry black vertex colour so M_Prop keeps their own pictures.   blender -b -P troll_rocks_export.py -- <lo folder> <out folder>
import bpy, bmesh, sys, os, math, random
import numpy as np
from mathutils import Vector, Matrix, noise

argv = sys.argv[sys.argv.index("--") + 1:]
LO, OUT = argv[0], argv[1]
os.makedirs(OUT, exist_ok=True)

def black(ob):
    me = ob.data
    a = me.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
    for d in a.data: d.color = (0, 0, 0, 1)

def export(ob, name):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, name + ".fbx"), use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE",
                             apply_unit_scale=True, axis_forward="-Y", axis_up="Z", mesh_smooth_type="EDGE", colors_type="SRGB", path_mode='STRIP')

def save_pic(img, path, size=1024):
    c = img.copy(); c.scale(size, size); c.filepath_raw = path; c.file_format = 'PNG'; c.save(); bpy.data.images.remove(c)

# ---- the sack
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(LO, "lo_sack.glb"))
sack = next(o for o in bpy.data.objects if o.type == 'MESH')
for o in list(bpy.data.objects):
    if o != sack: bpy.data.objects.remove(o)
sack.parent = None
sack.data.transform(sack.matrix_world); sack.matrix_world = Matrix.Identity(4)
P = np.array([tuple(v.co) for v in sack.data.vertices])
lo, hi = P.min(0), P.max(0)
s = 0.42 / (hi[2] - lo[2])
top = Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2] - (hi[2] - lo[2]) * 0.08))   # (the knot, a little under the very top)
sack.data.transform(Matrix.Diagonal((s, s, s, 1)) @ Matrix.Translation(-top))
# (2026-11-24, owner: the bag "is shattered".) Meshy's sack is 6000 loose triangles in 465 separate bits, shaded flat, some facing inwards: in the game
# it read as a net of black holes. Its points are welded (3 mm here), every face is turned to face out from the sack's middle line, and it is shaded
# smooth below 45 degrees; the game's material is two-sided as well (troll_kit_import.py), so a face still the wrong way round is not a hole.
bm = bmesh.new(); bm.from_mesh(sack.data)
n0 = len(bm.verts); e0 = sum(1 for e in bm.edges if len(e.link_faces) == 1)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.003)
bm.normal_update()
flip = []
zs = [v.co.z for v in bm.verts]; zlo, zhi = min(zs), max(zs)
for f in bm.faces:
    c = f.calc_center_median()
    if c.z > zlo + (zhi - zlo) * 0.78: continue          # (the rocks on top and the strap: left as they are)
    out = Vector((c.x, c.y, (c.z - (zlo + (zhi - zlo) * 0.4)) * 0.6))
    if out.length > 1e-5 and f.normal.dot(out) < 0: flip.append(f)
if sum(1 for e in bm.edges if len(e.link_faces) == 1) < 100: flip = []   # (2026-11-26: the cleaned sack, Tools/troll_kit_clean.py, is one closed surface with its normals already outward)
bmesh.ops.reverse_faces(bm, faces=flip)
e1 = sum(1 for e in bm.edges if len(e.link_faces) == 1)
bm.to_mesh(sack.data); bm.free(); sack.data.update()
for p in sack.data.polygons: p.use_smooth = True
sack.data.set_sharp_from_angle(angle=math.radians(45))
print("TKROCK sack welded %d -> %d points, open edges %d -> %d, %d faces turned outwards" % (n0, len(sack.data.vertices), e0, e1, len(flip)))
black(sack)
for n in sack.material_slots[0].material.node_tree.nodes:
    if n.type == 'TEX_IMAGE' and n.image:
        dst = None
        for l in n.outputs[0].links:
            nd, sk = l.to_node, l.to_socket.name
            if nd.type == 'BSDF_PRINCIPLED' and sk == "Base Color": dst = "Base"
            elif nd.type == 'NORMAL_MAP': dst = "Normal"
            else: dst = dst or "MR"
        if dst: save_pic(n.image, os.path.join(OUT, "T_RockSack_%s.png" % dst))
export(sack, "RockSack")
print("TKROCK sack %d tris, scale %.3f" % (sum(len(p.vertices) - 2 for p in sack.data.polygons), s))

# ---- the rock
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=0.065)
rock = bpy.context.active_object
random.seed(7)
for v in rock.data.vertices:
    p = v.co.normalized()
    d = 1 + 0.18 * noise.noise(p * 1.6 + Vector((3.1, 0.4, 7.7))) + 0.06 * noise.noise(p * 4.3 + Vector((1, 9, 2)))
    v.co = Vector((p.x * 1.08, p.y * 0.95, p.z * 0.78)) * 0.065 * d
# a few flat broken faces: crisp, bold, a little cartoon
bm = bmesh.new(); bm.from_mesh(rock.data)
for (nx, ny, nz, k) in ((0.3, -0.8, 0.5, 0.050), (-0.7, 0.2, -0.6, 0.048), (0.8, 0.5, 0.1, 0.055)):
    n = Vector((nx, ny, nz)).normalized()
    for v in bm.verts:
        h = v.co.dot(n)
        if h > k: v.co -= n * (h - k)
bm.to_mesh(rock.data); bm.free()
rock.data.shade_smooth()
bpy.ops.object.modifier_add(type='WEIGHTED_NORMAL'); bpy.ops.object.modifier_apply(modifier="WeightedNormal")
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(island_margin=0.02); bpy.ops.object.mode_set(mode='OBJECT')
# its picture: warm grey stone, painted blotches at two unrelated sizes, a few pale flecks (BotW/Overwatch stone, not photo)
N = 512
y, x = np.mgrid[0:N, 0:N] / N
def vn(f, ox):
    return np.array([[noise.noise(Vector((i * f + ox, j * f, 0.5))) for i in x[0]] for j in y[:, 0]])
a = vn(5.0, 0.0); b = vn(17.0, 4.3)
base = np.array([0.42, 0.41, 0.38]) + (0.07 * a + 0.03 * b)[..., None] * np.array([1.0, 1.0, 0.95])
base[(b > 0.55)] += 0.06
img = bpy.data.images.new("T_TrollRock_Base", N, N)
img.pixels[:] = np.dstack([np.clip(base, 0, 1), np.ones((N, N))]).ravel().tolist()
img.filepath_raw = os.path.join(OUT, "T_TrollRock_Base.png"); img.file_format = 'PNG'; img.save()
mr = np.zeros((N, N, 4)); mr[..., 0] = 1; mr[..., 1] = 0.82 - 0.08 * a; mr[..., 2] = 0; mr[..., 3] = 1
im2 = bpy.data.images.new("T_TrollRock_MR", N, N); im2.pixels[:] = mr.ravel().tolist()
im2.filepath_raw = os.path.join(OUT, "T_TrollRock_MR.png"); im2.file_format = 'PNG'; im2.save()
black(rock)
export(rock, "TrollRock")
print("TKROCK rock %d tris" % sum(len(p.vertices) - 2 for p in rock.data.polygons))
print("TKROCK done")
