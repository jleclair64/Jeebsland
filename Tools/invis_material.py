# The invisibility potion's material (2026-11-26): /Game/Effects/M_Invisible. Run with Tools/InvisMaterial.ps1 (a windowless editor, this
# project's editor closed). Safe to run again: it rebuilds the material from scratch.
# Unlit translucent, nearly clear: Opacity in the middle, Opacity + Edge at the outline (a Fresnel rim, like glass), a faint cool tint there,
# and refraction (pixel normal offset) bending what is behind him by Refraction (1 = none), with a slow ripple over the normal (Shimmer)
# made from world position and time (no textures). Translucent velocity on and Has Pixel Animation, so TSR gets motion vectors and keeps no
# ghost copies. Used with skeletal meshes (the suit), instanced and procedural meshes (the code-built gear), morph targets and cloth.
# Parameters (set per machine by KnightCharacterInvis.cpp from Tuning.ini [Invisibility]): Opacity, Edge, Refraction, Shimmer.
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
E = mel.create_material_expression
MP = unreal.MaterialProperty
PATH = "/Game/Effects/M_Invisible"


def log(s): unreal.log("INVISMAT " + s)


def node(m, cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("INVISMAT could not link %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def const(m, v, x, y): return node(m, unreal.MaterialExpressionConstant, x, y, r=v)
def sca(m, name, v, x, y): return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def op(m, cls, a, b, x, y, ao="", bo=""):
    n = node(m, cls, x, y); link(a, ao, n, "A"); link(b, bo, n, "B"); return n


def setp(m, names, v):
    for n in names:
        try:
            m.set_editor_property(n, v); log("%s = %s" % (n, v)); return True
        except Exception as e:
            pass
    log("no property among %s" % (names,))
    return False


if lib.does_asset_exist(PATH): lib.delete_asset(PATH)
tools = unreal.AssetToolsHelpers.get_asset_tools()
m = tools.create_asset("M_Invisible", "/Game/Effects", unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes", "used_with_morph_targets", "used_with_clothing", "used_with_spline_meshes"):
    setp(m, [flag], True)
setp(m, ["output_translucent_velocity"], True)
setp(m, ["has_pixel_animation"], True)
# refraction by pixel normal offset (screen-space bend from the normal; cheap, no IOR flip at grazing angles)
ok = False
for enum_name in ("RefractionMode", "RefractionMethod"):
    en = getattr(unreal, enum_name, None)
    if en is None: continue
    for member in ("RM_PIXEL_NORMAL_OFFSET", "PIXEL_NORMAL_OFFSET"):
        if hasattr(en, member):
            ok = setp(m, ["refraction_method", "refraction_mode"], getattr(en, member)); break
    if ok: break
if not ok: log("refraction method left as default")

# rim: Fresnel
fre = node(m, unreal.MaterialExpressionFresnel, -900, 0, exponent=2.5, base_reflect_fraction=0.0)
opa = sca(m, "Opacity", 0.04, -900, 200)
edge = sca(m, "Edge", 0.12, -900, 300)
rim = op(m, unreal.MaterialExpressionMultiply, fre, edge, -650, 250)
alpha = op(m, unreal.MaterialExpressionAdd, opa, rim, -450, 220)
sat = node(m, unreal.MaterialExpressionSaturate, -300, 220); link(alpha, "", sat, "")
mel.connect_material_property(sat, "", MP.MP_OPACITY)

# colour: a faint cool white, a little brighter at the rim
tint = node(m, unreal.MaterialExpressionConstant3Vector, -650, -200, constant=unreal.LinearColor(0.55, 0.65, 0.8, 1.0))
glow = op(m, unreal.MaterialExpressionMultiply, tint, fre, -450, -150)
mel.connect_material_property(glow, "", MP.MP_EMISSIVE_COLOR)

# the shimmer: a tangent-space normal tilted by two slow sine waves over world position and time
wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 500)
tm = node(m, unreal.MaterialExpressionTime, -1500, 650)
mz = node(m, unreal.MaterialExpressionComponentMask, -1300, 450, r=False, g=False, b=True, a=False); link(wp, "", mz, "")
mxy = node(m, unreal.MaterialExpressionComponentMask, -1300, 550, r=True, g=True, b=False, a=False); link(wp, "", mxy, "")
dxy = node(m, unreal.MaterialExpressionDotProduct, -1150, 550); link(mxy, "", dxy, "A")
dirc = node(m, unreal.MaterialExpressionConstant2Vector, -1300, 650, r=0.7, g=0.7); link(dirc, "", dxy, "B")
a1 = op(m, unreal.MaterialExpressionAdd, op(m, unreal.MaterialExpressionMultiply, mz, const(m, 0.07, -1300, 380), -1150, 420),
        op(m, unreal.MaterialExpressionMultiply, tm, const(m, 2.3, -1300, 720), -1150, 700), -1000, 450)
a2 = op(m, unreal.MaterialExpressionSubtract, op(m, unreal.MaterialExpressionMultiply, dxy, const(m, 0.05, -1150, 620), -1000, 580),
        op(m, unreal.MaterialExpressionMultiply, tm, const(m, 1.7, -1150, 780), -1000, 760), -850, 600)
s1 = node(m, unreal.MaterialExpressionSine, -850, 450, period=6.2831853); link(a1, "", s1, "")
s2 = node(m, unreal.MaterialExpressionSine, -700, 600, period=6.2831853); link(a2, "", s2, "")
shim = sca(m, "Shimmer", 0.3, -700, 750)
nx = op(m, unreal.MaterialExpressionMultiply, s1, shim, -550, 450)
ny = op(m, unreal.MaterialExpressionMultiply, s2, shim, -550, 600)
nxy = op(m, unreal.MaterialExpressionAppendVector, nx, ny, -400, 520)
nrm = op(m, unreal.MaterialExpressionAppendVector, nxy, const(m, 1.0, -550, 700), -250, 560)
mel.connect_material_property(nrm, "", MP.MP_NORMAL)

# refraction strength (pixel normal offset reads (value - 1) as the bend), strongest in the middle, half at the rim
refr = sca(m, "Refraction", 1.06, -450, 900)
mel.connect_material_property(refr, "", MP.MP_REFRACTION)

mel.recompile_material(m)
lib.save_asset(PATH)
log("done")
