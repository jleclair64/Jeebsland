# Fallen log bark (owner 2026-11-22: the log read red and light beside the trees; recolour it like the stumps, M_StumpBark).
# M_LogBark = M_StumpBark's recipe (grey-brown bark tint) but the moss (green-dominant pixels) keeps its own colour.
# Run in the editor with: -ExecutePythonScript=<this> (editor closed / own copy). Safe to run again.
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
TINT = (0.42, 0.37, 0.33)
path = "/Game/M_LogBark"
if EAL.does_asset_exist(path):
    EAL.delete_asset(path)
mat = tools.create_asset("M_LogBark", "/Game", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("used_with_instanced_static_meshes", True)

def node(cls, **p):
    n = MEL.create_material_expression(mat, cls, 0, 0)
    for k, v in p.items():
        n.set_editor_property(k, v)
    return n
def link(a, b, pin="", out=""):
    if not MEL.connect_material_expressions(a, out, b, pin):
        raise RuntimeError("LOGDIAG link failed " + pin)
def mulk(a, k, out=""):
    n = node(unreal.MaterialExpressionMultiply, const_b=k); link(a, n, "A", out); return n
def mask(a, r, g, b):
    n = node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=False); link(a, n); return n
def muln(a, b):
    n = node(unreal.MaterialExpressionMultiply); link(a, n, "A"); link(b, n, "B"); return n

base = node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Base")
nrm = node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="Normal", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
mr = node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="MR", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
c = mask(base, True, True, True)
def lum(a):
    d = node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
    link(a, d); k = node(unreal.MaterialExpressionConstant, r=1.0); link(k, d, "Fraction")
    return mask(d, True, False, False)
l = lum(c)
tint = node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(*TINT, 1.0))
recol = muln(mulk(l, 1.6), tint)
orig = mulk(c, 0.75)
# moss = green clearly above red: saturate((g - r) * 14)
g = mask(c, False, True, False); r = mask(c, True, False, False)
d = node(unreal.MaterialExpressionSubtract); link(g, d, "A"); link(r, d, "B")
moss = node(unreal.MaterialExpressionSaturate); link(mulk(d, 14.0), moss)
rec_amt = node(unreal.MaterialExpressionScalarParameter, parameter_name="Recolour", default_value=0.85)
alpha = node(unreal.MaterialExpressionMultiply); link(rec_amt, alpha, "A")
one_minus = node(unreal.MaterialExpressionOneMinus); link(moss, one_minus)
link(one_minus, alpha, "B")
col = node(unreal.MaterialExpressionLinearInterpolate); link(orig, col, "A"); link(recol, col, "B"); link(alpha, col, "Alpha")
MP = unreal.MaterialProperty
MEL.connect_material_property(col, "", MP.MP_BASE_COLOR)
MEL.connect_material_property(nrm, "", MP.MP_NORMAL)
rough = node(unreal.MaterialExpressionLinearInterpolate, const_a=0.75, const_b=1.0)
link(mask(mr, False, True, False), rough, "Alpha")
MEL.connect_material_property(rough, "", MP.MP_ROUGHNESS)
spec = node(unreal.MaterialExpressionConstant, r=0.25)
MEL.connect_material_property(spec, "", MP.MP_SPECULAR)
MEL.recompile_material(mat)
EAL.save_asset(path)

old = EAL.load_asset("/Game/TownDecor/MI_LogMossy")
mpath = "/Game/TownDecor/MI_LogMossyBark"
mi = EAL.load_asset(mpath) if EAL.does_asset_exist(mpath) else tools.create_asset("MI_LogMossyBark", "/Game/TownDecor", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
MEL.set_material_instance_parent(mi, mat)
vals = {}
for tp in old.get_editor_property("texture_parameter_values"):
    vals[str(tp.get_editor_property("parameter_info").get_editor_property("name"))] = tp.get_editor_property("parameter_value")
unreal.log("LOGDIAG old log textures: %s" % {k: (v.get_name() if v else None) for k, v in vals.items()})
for src, dst in (("BaseTex", "Base"), ("NormalTex", "Normal"), ("MRTex", "MR")):
    if vals.get(src):
        MEL.set_material_instance_texture_parameter_value(mi, dst, vals[src])
EAL.save_asset(mpath)
sm = EAL.load_asset("/Game/TownDecor/SM_LogMossy")
for i, sl in enumerate(sm.static_materials):
    if sl.material_interface and sl.material_interface.get_name() in ("MI_LogMossy", "MI_LogMossyBark"):
        sm.set_material(i, mi)
        unreal.log("LOGDIAG slot %d of SM_LogMossy set to %s" % (i, mpath))
EAL.save_asset("/Game/TownDecor/SM_LogMossy")
unreal.log("LOGDIAG done")
