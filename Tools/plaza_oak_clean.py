# The plaza oak (2026-11-13, owner: "Here's an oak tree for the middle of oak hill. See if you can color it autumn colors"): Meshy's
# community library "Tree" by Tansy (CC0, https://www.meshy.ai/posts/tree-01a00a2c-2417-7281-b2af-de7878770b7b), downloaded as
# Art/Trees/PlazaOak_Original.glb (1.88M triangles, one 2048 colour picture, git-ignored). This script, in Blender:
#  - drops the pale ground disc it stands on (and its painted moss), keeping the trunk and the dark surface roots;
#  - puts the origin at the trunk's foot, SINK of the height below where the roots meet the ground (the disc was a low dome);
#  - scales it to HEIGHT metres and reduces it to about TRIS triangles (Nanite draws it in the game);
#  - repaints its colour picture in autumn: the leaves in the world trees' own colours (Forest.cpp's oak tints: burnt orange, deep
#    red, amber, gold, a last green), picked by a soft 3D noise over the crown so whole branches turn together, the top and outside
#    a little further on than the inside, the leaves' own light and shade kept; the bark rich brown (Forest.cpp's trunk tint).
#    Done as a Cycles bake of a node graph onto the reduced model's own UVs;
#  - writes Saved/KnightBuild/Trees/PlazaOak.fbx + T_PlazaOak_Base.png (for Tools/plaza_oak_import.py), Art/Trees/PlazaOak.glb (the
#    cleaned model, kept in git) and front and top check pictures in Saved/KnightBuild/Trees.
# Run: blender -b -P Tools/plaza_oak_clean.py   (Tools/BuildPlazaOak.ps1 runs this, then the import)
import bpy, bmesh, os, sys, math
import numpy as np

proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(proj, "Art", "Trees", "PlazaOak_Original.glb")
OUT = os.path.join(proj, "Saved", "KnightBuild", "Trees")
ART = os.path.join(proj, "Art", "Trees", "PlazaOak.glb")
HEIGHT = 16.5         # metres from the ground to the crown's top (the plaza's lawn is 8.4 m in radius; the crown comes out ~8.5 m)
TRIS = 420000         # after reduction (Nanite: a hero tree, fine detail in the leaf clumps)
SINK = 0.032          # share of the original height under the ground: the disc's rim is ~0.01-0.02, its dome ~0.04 at the trunk
DISC_TOP = 0.046      # share of the height: below it, anything not dark is the disc (pale earth or painted moss), not a root
DARK = 0.30           # a root or trunk's colour (sRGB value) is darker than this
BAKE = 2048           # the repainted picture's size
ROOT_REACH = 5.0      # metres from the trunk's centre: surface roots further out are cut (lower than ROOT_LOW)
ROOT_LOW = 0.8

# The autumn colours (linear), Forest.cpp's oak tints, from the green that stays to the deepest red
# (second pass, visual review 2026-11-13: the first read mostly yellow at noon, so gold is a narrow band now and orange and red
# take most of the crown, as on the world's other trees)
RAMP = [(0.00, (0.17, 0.22, 0.07)),    # last green holdout
        (0.12, (0.34, 0.25, 0.065)),   # gold-brown
        (0.26, (0.50, 0.27, 0.06)),    # amber
        (0.46, (0.50, 0.165, 0.045)),  # burnt orange
        (0.68, (0.42, 0.10, 0.04)),    # red-orange
        (0.88, (0.32, 0.06, 0.04))]    # deep red
BARK = (0.29, 0.18, 0.10)              # rich brown, lighter than Forest.cpp's trunk tint (the first one read near-black in shade)
LEAF_GAIN = 1.0       # the leaves' brightness against the tints (1: the leaf picture's own shade averages to the tint)
BARK_GAIN = 1.0
SHADE_KEEP = 0.6      # how much of the painted light and shade stays (the rest is the flat tint): bright tips no longer go pale yellow
SHADOW_TRIS = 60000   # the shadow copy (not Nanite; drawn only into the sun's shadow maps)
SHADOW_COL_SCALE = 0.6  # per metre: open shafts through the whole crown, a 2D noise across it (bright dapples on the lawn)
SHADOW_COL_CUT = -0.15  # leaf faces where that noise is below this are cut (about a third of the crown's footprint)
SHADOW_THIN = 0.6       # and this share of the rest singly, so the shade between the dapples is a soft grey, not black
SHADE_MAX = 1.3      # and no spot is brighter than this times the tint
NOISE_SCALE = 3.2     # per original unit (the tree was 1.65 units tall): clumps of a few metres turn together
TOP_BIAS = 0.25       # the top and outside of the crown a little further into autumn than the inside

def say(*a): print("OAK", *a, flush=True)

os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
o = [x for x in bpy.data.objects if x.type == "MESH"][0]
bpy.context.view_layer.objects.active = o; o.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
me = o.data
img = me.materials[0].node_tree.nodes["Image Texture"].image if "Image Texture" in me.materials[0].node_tree.nodes else \
      [n.image for n in me.materials[0].node_tree.nodes if n.type == "TEX_IMAGE"][0]
w, h = img.size
px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)

# ---- classify faces: the disc goes
n = len(me.polygons)
cen = np.empty(n * 3, np.float32); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3)
ls = np.empty(n, np.int32); me.polygons.foreach_get("loop_start", ls)
uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
fu = (uv[ls] + uv[ls + 1] + uv[ls + 2]) / 3.0
col = px[np.clip((fu[:, 1] % 1.0) * (h - 1), 0, h - 1).astype(int), np.clip((fu[:, 0] % 1.0) * (w - 1), 0, w - 1).astype(int), :3]
V = col.max(1)
z = cen[:, 2]; z0 = float(z.min()); H0 = float(z.max()) - z0; zr = (z - z0) / H0
drop = (zr < SINK) | ((zr < DISC_TOP) & (V >= DARK))
say("faces", n, "dropping", int(drop.sum()), "(below the ground", int((zr < SINK).sum()), ")")
bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[bm.faces[i] for i in np.nonzero(drop)[0]], context="FACES")
# little loose pieces left of the disc (painted specks): any connected piece under the disc's top with few faces
bm.faces.ensure_lookup_table()
seen = set(); small = []
low = z0 + DISC_TOP * H0 * 1.3
for f in bm.faces:
    if f.index in seen or f.calc_center_median().z > low: continue
    stack = [f]; part = {f.index: f}
    while stack and len(part) <= 60:   # (a piece that grows past 60 faces is a root or the tree: leave it)
        g = stack.pop()
        for e in g.edges:
            for q in e.link_faces:
                if q.index not in part: part[q.index] = q; stack.append(q)
    seen.update(part.keys())
    if len(part) <= 60 and max(v.co.z for g in part.values() for v in g.verts) < low: small += list(part.values())
if small: bmesh.ops.delete(bm, geom=small, context="FACES")
say("loose disc specks removed", len(small))
bm.to_mesh(me); bm.free(); me.update()

# ---- origin at the trunk's foot, scaled to HEIGHT
co = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
band = np.abs((co[:, 2] - z0) / H0 - 0.2) < 0.03
cx, cy = float(co[band, 0].mean()), float(co[band, 1].mean())
ground = z0 + SINK * H0
k = HEIGHT / (z0 + H0 - ground)
o.location = (-cx * k, -cy * k, -ground * k); o.scale = (k, k, k)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
say("trunk centre", round(cx, 3), round(cy, 3), "scale", round(k, 3))
# the surface roots stop ROOT_REACH from the trunk (they ran 10 m out, under the plaza's well, and broke into lone pieces at the ends)
n = len(me.polygons)
cen = np.empty(n * 3, np.float32); me.polygons.foreach_get("center", cen); cen = cen.reshape(-1, 3)
far = (cen[:, 2] < ROOT_LOW) & (np.hypot(cen[:, 0], cen[:, 1]) > ROOT_REACH)
bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[bm.faces[i] for i in np.nonzero(far)[0]], context="FACES")
bm.faces.ensure_lookup_table()
seen = set(); small = []
for f in bm.faces:
    if f.index in seen or f.calc_center_median().z > ROOT_LOW: continue
    stack = [f]; part = {f.index: f}
    while stack and len(part) <= 150:
        g = stack.pop()
        for e in g.edges:
            for q in e.link_faces:
                if q.index not in part: part[q.index] = q; stack.append(q)
    seen.update(part.keys())
    if len(part) <= 150 and max(v.co.z for g in part.values() for v in g.verts) < ROOT_LOW: small += list(part.values())
if small: bmesh.ops.delete(bm, geom=small, context="FACES")
bm.to_mesh(me); bm.free(); me.update()
say("roots past", ROOT_REACH, "m cut:", int(far.sum()), "faces, and", len(small), "in lone pieces")

# ---- reduce
tris = sum(len(p.vertices) - 2 for p in me.polygons)
if tris > TRIS:
    m = o.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = TRIS / tris; m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)
tris = sum(len(p.vertices) - 2 for p in me.polygons)
say("triangles", tris)

# ---- the leaves' and bark's own average brightness (linear), so the tints come out as the world trees' colours
lin = np.where(px[:, :, :3] <= 0.04045, px[:, :, :3] / 12.92, ((px[:, :, :3] + 0.055) / 1.055) ** 2.4)
lum = lin @ np.array([0.2126, 0.7152, 0.0722], np.float32)
green = (lin[:, :, 1] - np.maximum(lin[:, :, 0], lin[:, :, 2])) > 0.01
leaf_mean = float(lum[green].mean()); bark_mean = float(lum[~green & (lum > 0.001)].mean())
say("leaf luminance", round(leaf_mean, 4), "bark luminance", round(bark_mean, 4))

# ---- the shadow (visual review 2026-11-13: the dense crown cast one near-black disc): a cut-down copy with gaps in the leaves,
# drawn only into the shadows in game (SM_PlazaOakShadow, hidden, bCastHiddenShadow) while the real tree casts none, so the
# lawn under it is dappled like the shade of a real oak. Bark stays whole. The crown is several leaf shells deep, so gaps in
# each shell (a 3D noise, tried first) still added up to a solid shadow: the gaps are shafts through the whole crown (a 2D
# noise) plus a fine thinning everywhere. OAK_SHADOW_ONLY=1 stops after it (no bake: a few minutes quicker).
import mathutils
sc = bpy.context.scene
o2 = o.copy(); o2.data = o.data.copy(); o2.name = "PlazaOakShadow"; sc.collection.objects.link(o2)
bpy.ops.object.select_all(action="DESELECT"); o2.select_set(True); bpy.context.view_layer.objects.active = o2
m = o2.modifiers.new("reduce", "DECIMATE"); m.decimate_type = "COLLAPSE"; m.ratio = SHADOW_TRIS / tris; m.use_collapse_triangulate = True
bpy.ops.object.modifier_apply(modifier=m.name)
me2 = o2.data; n2 = len(me2.polygons)
cen2 = np.empty(n2 * 3, np.float32); me2.polygons.foreach_get("center", cen2); cen2 = cen2.reshape(-1, 3)
ls2 = np.empty(n2, np.int32); me2.polygons.foreach_get("loop_start", ls2)
uv2 = np.empty(len(me2.loops) * 2, np.float32); me2.uv_layers.active.data.foreach_get("uv", uv2); uv2 = uv2.reshape(-1, 2)
fu2 = (uv2[ls2] + uv2[ls2 + 1] + uv2[ls2 + 2]) / 3.0
c2 = lin[np.clip((fu2[:, 1] % 1.0) * (h - 1), 0, h - 1).astype(int), np.clip((fu2[:, 0] % 1.0) * (w - 1), 0, w - 1).astype(int)]
leafy = ((c2[:, 1] - np.maximum(c2[:, 0], c2[:, 2])) > 0.002) & (cen2[:, 2] > 3.0)
rng = np.random.default_rng(4242)
nz = np.array([mathutils.noise.noise(mathutils.Vector((c[0] * SHADOW_COL_SCALE, c[1] * SHADOW_COL_SCALE, 0.37))) for c in cen2], np.float32)
gap = leafy & ((nz < SHADOW_COL_CUT) | (rng.random(n2) < SHADOW_THIN))
bm = bmesh.new(); bm.from_mesh(me2); bm.faces.ensure_lookup_table()
bmesh.ops.delete(bm, geom=[bm.faces[i] for i in np.nonzero(gap)[0]], context="FACES")
bm.to_mesh(me2); bm.free(); me2.update()
say("shadow copy: %d triangles, leaf faces %d, %d of them cut for gaps (%.0f%%)" % (n2, int(leafy.sum()), int(gap.sum()), 100.0 * gap.sum() / max(1, leafy.sum())))
fbx2 = os.path.join(OUT, "PlazaOakShadow.fbx")
bpy.ops.export_scene.fbx(filepath=fbx2, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO",
                         embed_textures=False, colors_type="NONE")
say("wrote", fbx2, round(os.path.getsize(fbx2) / 1e6, 1), "MB")
bpy.data.objects.remove(o2, do_unlink=True)
bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
if os.environ.get("OAK_SHADOW_ONLY"): say("shadow only: done"); sys.exit(0)

# ---- the autumn picture: a node graph baked (emission) onto the reduced model's own UVs
mat = me.materials[0]; nt = mat.node_tree; N = nt.nodes; L = nt.links
tex = [x for x in N if x.type == "TEX_IMAGE"][0]
def node(t, **kw):
    x = N.new(t)
    for a, b in kw.items(): setattr(x, a, b)
    return x
def math_(op, a, b=None, clamp=False):
    x = node("ShaderNodeMath", operation=op, use_clamp=clamp)
    for i, v in enumerate((a, b)):
        if v is None: continue
        if isinstance(v, (int, float)): x.inputs[i].default_value = v
        else: L.new(v, x.inputs[i])
    return x.outputs[0]
sep = node("ShaderNodeSeparateColor"); L.new(tex.outputs["Color"], sep.inputs[0])
lumn = node("ShaderNodeRGBToBW"); L.new(tex.outputs["Color"], lumn.inputs[0])
# greenness: G - max(R, B), soft from 0 to 0.04 (linear)
g_m = math_("SUBTRACT", sep.outputs[1], math_("MAXIMUM", sep.outputs[0], sep.outputs[2]))
leaf = math_("MULTIPLY", math_("SUBTRACT", g_m, 0.002), 1.0 / 0.03, clamp=True)
# which autumn colour: a soft 3D noise over the crown, plus a little more toward the top and outside
coord = node("ShaderNodeTexCoord")
noise = node("ShaderNodeTexNoise"); noise.inputs["Scale"].default_value = NOISE_SCALE / k; noise.inputs["Detail"].default_value = 1.5
noise.inputs["Roughness"].default_value = 0.45
L.new(coord.outputs["Object"], noise.inputs["Vector"])
sxyz = node("ShaderNodeSeparateXYZ"); L.new(coord.outputs["Object"], sxyz.inputs[0])
up = math_("DIVIDE", sxyz.outputs[2], HEIGHT)
outw = math_("DIVIDE", math_("SQRT", math_("ADD", math_("MULTIPLY", sxyz.outputs[0], sxyz.outputs[0]), math_("MULTIPLY", sxyz.outputs[1], sxyz.outputs[1]))), HEIGHT * 0.55)
bias = math_("MULTIPLY", math_("ADD", math_("MULTIPLY", up, 0.6), math_("MULTIPLY", outw, 0.4)), TOP_BIAS)
f = math_("ADD", math_("MULTIPLY", math_("SUBTRACT", noise.outputs["Fac"], 0.32), 1.0 / 0.36), math_("SUBTRACT", bias, TOP_BIAS * 0.55))
ramp = node("ShaderNodeValToRGB"); L.new(math_("MAXIMUM", math_("MINIMUM", f, 1.0), 0.0), ramp.inputs[0])
els = ramp.color_ramp.elements
els[0].position, els[0].color = RAMP[0][0], (*RAMP[0][1], 1)
els[1].position, els[1].color = RAMP[-1][0], (*RAMP[-1][1], 1)
for pos, c in RAMP[1:-1]: e = els.new(pos); e.color = (*c, 1)
# the leaves keep their own light and shade (their brightness over its average), so do the bark's
lshade = math_("MINIMUM", math_("ADD", math_("MULTIPLY", lumn.outputs[0], SHADE_KEEP * LEAF_GAIN / leaf_mean), (1.0 - SHADE_KEEP) * LEAF_GAIN), SHADE_MAX)
bshade = math_("MINIMUM", math_("ADD", math_("MULTIPLY", lumn.outputs[0], SHADE_KEEP * BARK_GAIN / bark_mean), (1.0 - SHADE_KEEP) * BARK_GAIN), SHADE_MAX)
lcol = node("ShaderNodeMix", data_type="RGBA", blend_type="MULTIPLY"); lcol.inputs[0].default_value = 1.0
L.new(ramp.outputs[0], lcol.inputs[6]);
lsh = node("ShaderNodeCombineColor"); [L.new(lshade, lsh.inputs[i]) for i in range(3)]
L.new(lsh.outputs[0], lcol.inputs[7])
bcol = node("ShaderNodeMix", data_type="RGBA", blend_type="MULTIPLY"); bcol.inputs[0].default_value = 1.0
bcol.inputs[6].default_value = (*BARK, 1)
bsh = node("ShaderNodeCombineColor"); [L.new(bshade, bsh.inputs[i]) for i in range(3)]
L.new(bsh.outputs[0], bcol.inputs[7])
mix = node("ShaderNodeMix", data_type="RGBA"); L.new(leaf, mix.inputs[0]); L.new(bcol.outputs[2], mix.inputs[6]); L.new(lcol.outputs[2], mix.inputs[7])
emit = node("ShaderNodeEmission"); L.new(mix.outputs[2], emit.inputs[0])
outn = [x for x in N if x.type == "OUTPUT_MATERIAL"][0]
L.new(emit.outputs[0], outn.inputs["Surface"])
baked = bpy.data.images.new("T_PlazaOak_Base", BAKE, BAKE, alpha=False)
baked.colorspace_settings.name = "sRGB"
tgt = node("ShaderNodeTexImage"); tgt.image = baked; N.active = tgt
sc = bpy.context.scene; sc.render.engine = "CYCLES"; sc.cycles.samples = 1; sc.cycles.device = "CPU"
sc.render.bake.margin = 8
bpy.ops.object.bake(type="EMIT")
pic = os.path.join(OUT, "T_PlazaOak_Base.png")
baked.filepath_raw = pic; baked.file_format = "PNG"; baked.save()
say("baked", pic)

# ---- a plain material with the new picture (for the GLB and the check pictures)
for x in list(N): N.remove(x)
t2 = node("ShaderNodeTexImage"); t2.image = baked
bsdf = node("ShaderNodeBsdfPrincipled"); bsdf.inputs["Roughness"].default_value = 0.85
L.new(t2.outputs["Color"], bsdf.inputs["Base Color"])
oo = node("ShaderNodeOutputMaterial"); L.new(bsdf.outputs[0], oo.inputs["Surface"])
mat.name = "M_PlazaOak"
if hasattr(mat, "blend_method"): mat.blend_method = "OPAQUE"

# ---- write
bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
o.name = "PlazaOak"
fbx = os.path.join(OUT, "PlazaOak.fbx")
bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, apply_unit_scale=True,
                         axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=False, bake_anim=False, add_leaf_bones=False, path_mode="AUTO",
                         embed_textures=False, colors_type="NONE")
say("wrote", fbx, round(os.path.getsize(fbx) / 1e6, 1), "MB")
bpy.ops.export_scene.gltf(filepath=ART, use_selection=True, export_format="GLB")
say("wrote", ART, round(os.path.getsize(ART) / 1e6, 1), "MB")
d = o.dimensions; say("size m", round(d.x, 2), round(d.y, 2), round(d.z, 2))
co = np.empty(len(me.vertices) * 3, np.float32); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
r = np.hypot(co[:, 0], co[:, 1])
for zz in (0.5, 1.0, 2.0, 3.0):
    m = np.abs(co[:, 2] - zz) < 0.1
    if m.any(): say("trunk radius at %.1f m: p50 %.2f p90 %.2f" % (zz, np.percentile(r[m], 50), np.percentile(r[m], 90)))
canopy = co[:, 2] > HEIGHT * 0.4
say("crown radius p95 %.2f max %.2f, lowest leaves above 0.3x radius %.2f m" % (np.percentile(r[canopy], 95), r[canopy].max(), co[(r > 3.0), 2].min()))

# ---- check pictures (Eevee, sun, grey ground)
sc.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
sc.render.resolution_x, sc.render.resolution_y = 1000, 900
world = bpy.data.worlds.new("w"); sc.world = world; world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.65, 0.8, 1); world.node_tree.nodes["Background"].inputs[1].default_value = 0.8
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sc.collection.objects.link(sun)
sun.data.energy = 3.5; sun.rotation_euler = (math.radians(50), 0, math.radians(30))
bpy.ops.mesh.primitive_plane_add(size=40); gp = bpy.context.active_object
gm = bpy.data.materials.new("g"); gm.use_nodes = True; gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.08, 0.16, 0.05, 1); gp.data.materials.append(gm)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
for nm, loc, rot in (("front", (0, -34, 7), (math.radians(88), 0, 0)), ("top", (0, -14, 34), (math.radians(25), 0, 0))):
    cam.location = loc; cam.rotation_euler = rot
    sc.render.filepath = os.path.join(OUT, "PlazaOak_%s.png" % nm)
    bpy.ops.render.render(write_still=True)
    say("picture", sc.render.filepath)
