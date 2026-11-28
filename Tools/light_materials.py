# The torch's flame and Glimmer's orb (2026-11-27, agent light): /Game/Effects/M_TorchFire and /Game/Effects/M_GlimmerOrb.
# Run with Tools/LightMaterials.ps1 (a windowless editor, this project's editor closed). Safe to run again: each is rebuilt from scratch.
#
# M_TorchFire: one flame layer on a camera-facing card (AKnightCharacter::PlaceLights stacks three: a wide red-orange outer flame, a middle one and a
# small white-yellow core). Additive and unlit. The shape is a teardrop in the card's UVs whose sides are pushed about by two layers of noise that
# scroll upward at different speeds (big slow licks and small fast ones), more toward the tip, so the outline keeps dancing; the tip breaks up
# into licks; the colour runs from a hot white-yellow low in the middle through orange to red at the edges and tips. No textures (ALU noise).
# Translucent velocity on, Has Pixel Animation and Responsive AA, so TSR takes motion vectors for the card and trusts this frame's flame over
# its history (no ghost copies or smears). Parameters (per card, set by code): Tint (the outer colour), Core (0-1: how hot), Seed, Speed, Intensity.
#
# M_GlimmerOrb: a soft ball of light for Glimmer's tip (and the torch's embers): brightest in the middle, fading to nothing at the rim (a view-angle
# falloff), additive, the same TSR settings. Parameters: Tint, Intensity.
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
E = mel.create_material_expression
MP = unreal.MaterialProperty


def log(s): unreal.log("LIGHTMAT " + s)


def node(m, cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("LIGHTMAT could not link %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def const(m, v, x=0, y=0): return node(m, unreal.MaterialExpressionConstant, x, y, r=v)
def sca(m, name, v, x=0, y=0): return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)
def vec(m, name, c, x=0, y=0): return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(c[0], c[1], c[2], 1.0))
def c3(m, c, x=0, y=0): return node(m, unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(c[0], c[1], c[2], 1.0))


def op(m, cls, a, b, x=0, y=0, ao="", bo=""):
    n = node(m, cls, x, y); link(a, ao, n, "A"); link(b, bo, n, "B"); return n


def one(m, cls, src, x=0, y=0, **props):
    n = node(m, cls, x, y, **props); link(src, "", n, ""); return n


def mul(m, a, b): return op(m, unreal.MaterialExpressionMultiply, a, b if not isinstance(b, (int, float)) else const(m, b))
def add(m, a, b): return op(m, unreal.MaterialExpressionAdd, a, b if not isinstance(b, (int, float)) else const(m, b))
def sub(m, a, b): return op(m, unreal.MaterialExpressionSubtract, a, b if not isinstance(b, (int, float)) else const(m, b))
def sat(*args): return one(CUR[0], unreal.MaterialExpressionSaturate, args[-1])   # (sat(x) or sat(m, x))
CUR = [None]


def lerp(m, a, b, t):
    n = node(m, unreal.MaterialExpressionLinearInterpolate, 0, 0); link(a, "", n, "A"); link(b, "", n, "B"); link(t, "", n, "Alpha"); return n


def pw(m, a, e):
    n = node(m, unreal.MaterialExpressionPower, 0, 0, const_exponent=float(e)); link(a, "", n, "Base"); return n


def setp(m, names, v):
    for n in names:
        try:
            m.set_editor_property(n, v); log("%s = %s" % (n, v)); return True
        except Exception:
            pass
    log("no property among %s" % (names,))
    return False


def fresh(name):
    path = "/Game/Effects/" + name
    if lib.does_asset_exist(path): lib.delete_asset(path)
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Effects", unreal.Material, unreal.MaterialFactoryNew())
    CUR[0] = m
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    for flag in ("used_with_instanced_static_meshes",):
        setp(m, [flag], True)
    setp(m, ["output_translucent_velocity"], True)
    setp(m, ["has_pixel_animation"], True)
    setp(m, ["enable_responsive_aa"], True)
    return m, path


def noise(m, pos):
    n = node(m, unreal.MaterialExpressionNoise, 0, 0, scale=1.0, levels=2, output_min=-1.0, output_max=1.0, quality=1)
    for fn in ("NOISEFUNCTION_GRADIENT_ALU", "NOISEFUNCTION_SIMPLEX_ALU", "NOISEFUNCTION_VALUE_ALU"):
        if hasattr(unreal.NoiseFunction, fn):
            n.set_editor_property("noise_function", getattr(unreal.NoiseFunction, fn)); log("noise " + fn); break
    if not mel.connect_material_expressions(pos, "", n, "Position") and not mel.connect_material_expressions(pos, "", n, ""):
        raise RuntimeError("LIGHTMAT noise position")
    return n


def build_fire():
    m, path = fresh("M_TorchFire")
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    u = one(m, unreal.MaterialExpressionComponentMask, uv, r=True, g=False, b=False, a=False)
    v = one(m, unreal.MaterialExpressionComponentMask, uv, r=False, g=True, b=False, a=False)
    y = one(m, unreal.MaterialExpressionOneMinus, v)                          # 0 at the foot, 1 at the top
    t = mul(m, node(m, unreal.MaterialExpressionTime, -1600, 300), sca(m, "Speed", 1.0))
    seed = sca(m, "Seed", 0.0)

    def pos(us, ys, rise, seed_off):
        a = op(m, unreal.MaterialExpressionAppendVector, mul(m, u, us), sub(m, mul(m, y, ys), mul(m, t, rise)))
        return op(m, unreal.MaterialExpressionAppendVector, a, add(m, seed, seed_off))
    n1 = noise(m, pos(2.2, 1.5, 1.6, 0.0))     # big, slow licks
    n2 = noise(m, pos(5.5, 3.8, 3.3, 3.7))     # small, quick ones
    sway = mul(m, add(m, mul(m, n1, 0.15), mul(m, n2, 0.055)), pw(m, y, 1.3))
    xd = one(m, unreal.MaterialExpressionAbs, add(m, sub(m, u, 0.5), sway))
    # the teardrop's half-width: swelling quickly off the foot, narrowing to the tip
    w = mul(m, mul(m, pw(m, sat(mul(m, y, 2.2)), 0.5), pw(m, sat(one(m, unreal.MaterialExpressionOneMinus, y)), 0.85)), 0.42)
    soft = op(m, unreal.MaterialExpressionMax, mul(m, w, 0.75), const(m, 0.01))
    shape = sat(op(m, unreal.MaterialExpressionDivide, sub(m, w, xd), soft))
    tip = sat(mul(m, add(m, sub(m, const(m, 1.0), y), mul(m, n2, 0.16)), 3.0))   # licks break off near the top
    foot = sat(mul(m, y, 8.0))
    a = mul(m, mul(m, shape, tip), foot)
    heat = mul(m, mul(m, a, sub(m, const(m, 1.2), y)), add(m, mul(m, sca(m, "Core", 0.0), 0.9), 0.55))
    red = vec(m, "Tint", (1.0, 0.16, 0.02))
    orange = c3(m, (1.0, 0.48, 0.07))
    yellow = c3(m, (1.0, 0.86, 0.52))
    col = lerp(m, lerp(m, red, orange, sat(mul(m, heat, 2.0))), yellow, sat(sub(m, mul(m, heat, 2.0), 1.0)))
    em = mul(m, mul(m, col, a), sca(m, "Intensity", 3.0))
    mel.connect_material_property(em, "", MP.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    lib.save_asset(path)
    log("built M_TorchFire")


def build_orb():
    m, path = fresh("M_GlimmerOrb")
    rim = node(m, unreal.MaterialExpressionFresnel, -900, 0, exponent=1.0, base_reflect_fraction=0.0)
    core = pw(m, one(m, unreal.MaterialExpressionOneMinus, rim), 4.0)
    em = mul(m, mul(m, core, vec(m, "Tint", (0.78, 0.9, 1.0))), sca(m, "Intensity", 4.0))
    mel.connect_material_property(em, "", MP.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    lib.save_asset(path)
    log("built M_GlimmerOrb")


build_fire()
build_orb()
log("done")
