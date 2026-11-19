# The magic bolt's material, /Game/Props/M_GlowOrb (Projectile.cpp loads it by name), rebuilt on its own: run in a windowless editor
# with this project's editor closed (UnrealEditor-Cmd <uproject> -ExecutePythonScript=Tools/bolt_glow.py); Tools/fixlook_assets.py runs
# this same file for its step 3, so the two never drift apart.
# 2026-11-19 look review: the staff's cast flashed a large white blob at release. Its Strength 4 was on the scene's own light scale, so
# under the night or indoor exposure it went far past white and the tonemapper bleached the cyan out; the white-hot heart added more.
# Now, as the staff's orb (fixlook_assets.py step 2): ExposureLock divides by the camera's exposure, so Strength is its brightness on
# screen, the same by day and at dusk, kept under white; the heart is only a little lighter than the rim, never white.
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
E = mel.create_material_expression


def log(s): unreal.log("BOLTGLOW " + s)


def node(m, cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("BOLTGLOW could not link %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def const(m, v, x, y):
    c = E(m, unreal.MaterialExpressionConstant, x, y); c.set_editor_property("r", v); return c


ORB = "/Game/Props/M_GlowOrb"
if lib.does_asset_exist(ORB): lib.delete_asset(ORB)
o = tools.create_asset("M_GlowOrb", "/Game/Props", unreal.Material, unreal.MaterialFactoryNew())
o.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
o.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
fr = node(o, unreal.MaterialExpressionFresnel, -900, 0, exponent=1.6, base_reflect_fraction=0.0)
core = node(o, unreal.MaterialExpressionOneMinus, -750, 0); link(fr, "", core, "")
c2 = node(o, unreal.MaterialExpressionMultiply, -600, 0); link(core, "", c2, "A"); link(core, "", c2, "B")
col = node(o, unreal.MaterialExpressionVectorParameter, -900, 200, parameter_name="Color", default_value=unreal.LinearColor(0.02, 0.72, 1.0, 1.0))
st = node(o, unreal.MaterialExpressionScalarParameter, -900, 350, parameter_name="Strength", default_value=0.9)
a = node(o, unreal.MaterialExpressionMultiply, -600, 200); link(col, "RGB", a, "A"); link(st, "", a, "B")
soft = node(o, unreal.MaterialExpressionMultiply, -450, 200); link(a, "", soft, "A"); link(core, "", soft, "B")   # its colour, fading to the rim
# a slightly lighter heart: the same colour lifted a quarter toward white, never white itself
heart = node(o, unreal.MaterialExpressionMultiply, -450, 350); link(c2, "", heart, "A"); link(st, "", heart, "B")
h2 = node(o, unreal.MaterialExpressionMultiply, -300, 350); link(heart, "", h2, "A"); link(const(o, 0.12, -450, 450), "", h2, "B")
add = node(o, unreal.MaterialExpressionAdd, -150, 250); link(soft, "", add, "A"); link(h2, "", add, "B")
out = add
if hasattr(unreal, "MaterialExpressionEyeAdaptation"):
    ea = node(o, unreal.MaterialExpressionEyeAdaptation, -300, 550)
    inv = node(o, unreal.MaterialExpressionDivide, -150, 550); link(const(o, 1.0, -300, 650), "", inv, "A"); link(ea, "", inv, "B")
    lk = node(o, unreal.MaterialExpressionMultiply, 0, 300); link(add, "", lk, "A"); link(inv, "", lk, "B")
    out = lk
    log("exposure lock built")
else:
    log("WARNING no MaterialExpressionEyeAdaptation: no exposure lock")
mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(o)
lib.save_asset(ORB)
log("bolt material ready")
