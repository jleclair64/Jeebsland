# Blue smoke swirling inside the wizard's crystal ball and the staff's orb, and the staff's orb 25% bigger (2026-11-23, agent smear).
# Run with Tools/OrbSmoke.ps1 (a windowless editor, this project's editor closed). Safe to run again (skips what is there).
#  1. /Game/Props/M_PropGlow (the staff's, wand's and ball's glow material) gets, inside its glow ellipsoid only:
#       SmokeStrength (0 = off, the wand), SmokeColor: two layers of 3D gradient noise in the glow's own sphere space, turning in
#       different directions over time, one of them looked at through the ball (shifted along the view direction: depth), laid over
#       the glow as light wisps and darker gaps. No lights, no particles: it is the surface's own shading, so nothing can trail.
#       The material says it has pixel animation (TSR keeps no old copies of the pattern).
#     OrbGrow (0 = off): the vertices of the glow ellipsoid are pushed out from its centre by this share (0.25 = a quarter bigger), the
#       claws (the tier's steel, vertex red) and the shaft stay where they are.
#  2. MI_Staff gets OrbGrow 0.25 and SmokeStrength; MI_CrystalBall gets SmokeStrength. MI_Wand is untouched.
#  3. M_WizardOrb (the guard bubble and the pipe's puffs): translucent velocity on, if this engine has the setting.
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
E = mel.create_material_expression
MP = unreal.MaterialProperty


def log(s): unreal.log("ORBSMOKE " + s)


def node(m, cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError("ORBSMOKE could not link %s.%s -> %s.%s" % (a.get_name(), ao, b.get_name(), bi))


def const(m, v, x, y): return node(m, unreal.MaterialExpressionConstant, x, y, r=v)
def sca(m, name, v, x, y): return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)
def vec(m, name, v, x, y): return node(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*v))


def op(m, cls, a, b, x, y, ao="", bo=""):
    n = node(m, cls, x, y); link(a, ao, n, "A"); link(b, bo, n, "B"); return n


def has_param(m, name):
    return name in [str(p) for p in mel.get_vector_parameter_names(m)] + [str(p) for p in mel.get_scalar_parameter_names(m)]


def upstream(m, start):
    seen, todo, out = set(), [start], []
    while todo:
        n = todo.pop()
        if n is None or n.get_name() in seen: continue
        seen.add(n.get_name()); out.append(n)
        for i in mel.get_inputs_for_material_expression(m, n): todo.append(i)
    return out


def noise_fn():
    for name in dir(unreal.NoiseFunction):
        u = name.upper()
        if "GRADIENT" in u and "ALU" in u: return getattr(unreal.NoiseFunction, name)
    raise RuntimeError("ORBSMOKE no gradient ALU noise function in " + str(dir(unreal.NoiseFunction)))


g = lib.load_asset("/Game/Props/M_PropGlow")
if g is None: raise RuntimeError("ORBSMOKE no M_PropGlow (run Tools/FixLookAssets.ps1 first)")
if not has_param(g, "SmokeStrength"):
    src, so = mel.get_material_property_input_node(g, MP.MP_EMISSIVE_COLOR), mel.get_material_property_input_node_output_name(g, MP.MP_EMISSIVE_COLOR)
    if src is None: raise RuntimeError("ORBSMOKE M_PropGlow has no emissive")
    up = upstream(g, src)
    cen = next(n for n in up if isinstance(n, unreal.MaterialExpressionVectorParameter) and str(n.get_editor_property("parameter_name")) == "GlowCentre")
    rad = next(n for n in up if isinstance(n, unreal.MaterialExpressionVectorParameter) and str(n.get_editor_property("parameter_name")) == "GlowRadii")
    lock = next((n for n in up if isinstance(n, unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property("parameter_name")) == "ExposureLock"), None)
    lp = next((n for n in up if isinstance(n, (unreal.MaterialExpressionLocalPosition, unreal.MaterialExpressionTransformPosition))), None)
    if lp is None: raise RuntimeError("ORBSMOKE no local position found")
    X, Y = 1200, 3200   # (an empty part of the graph)

    # the position in the glow's own unit sphere: (local - centre) / radii
    sub = op(g, unreal.MaterialExpressionSubtract, lp, cen, X, Y, "", "")
    p = op(g, unreal.MaterialExpressionDivide, sub, rad, X + 150, Y, "", "")
    pr = node(g, unreal.MaterialExpressionComponentMask, X + 300, Y, r=True, g=True, b=True, a=False); link(p, "", pr, "")   # (float3)
    # the mask: 1 inside the ellipsoid (d 0.95) to 0 past it (1.05), not on the tier's steel (vertex red)
    d = node(g, unreal.MaterialExpressionSquareRoot, X + 300, Y + 100); dot = node(g, unreal.MaterialExpressionDotProduct, X + 150, Y + 100); link(pr, "", dot, "A"); link(pr, "", dot, "B"); link(dot, "", d, "")
    edge = op(g, unreal.MaterialExpressionSubtract, const(g, 1.05, X + 300, Y + 180), d, X + 450, Y + 100)
    sat = node(g, unreal.MaterialExpressionSaturate, X + 750, Y + 100); link(op(g, unreal.MaterialExpressionMultiply, edge, const(g, 10.0, X + 450, Y + 180), X + 600, Y + 100), "", sat, "")
    notsteel = node(g, unreal.MaterialExpressionOneMinus, X + 600, Y + 200); link(node(g, unreal.MaterialExpressionVertexColor, X + 450, Y + 220), "R", notsteel, "")
    mask = op(g, unreal.MaterialExpressionMultiply, sat, notsteel, X + 900, Y + 140)

    # --- the swirl: a turn about the up axis and one about the forward axis, as sin and cos of the time
    T = node(g, unreal.MaterialExpressionTime, X, Y + 400)

    def turned(pos, speed, plane, x, y):   # (pos float3 rotated about an axis by time * speed)
        a = op(g, unreal.MaterialExpressionMultiply, T, const(g, speed, x, y + 60), x + 120, y)
        s = node(g, unreal.MaterialExpressionSine, x + 250, y); link(a, "", s, "")
        c = node(g, unreal.MaterialExpressionCosine, x + 250, y + 80); link(a, "", c, "")
        comp = [node(g, unreal.MaterialExpressionComponentMask, x + 100, y + 200 + 70 * i, r=(i == 0), g=(i == 1), b=(i == 2), a=False) for i in range(3)]
        for cm in comp: link(pos, "", cm, "")
        u, v = (0, 1) if plane == "z" else (1, 2)    # the two axes that turn
        nu = op(g, unreal.MaterialExpressionSubtract, op(g, unreal.MaterialExpressionMultiply, comp[u], c, x + 400, y + 200), op(g, unreal.MaterialExpressionMultiply, comp[v], s, x + 400, y + 280), x + 550, y + 200)
        nv = op(g, unreal.MaterialExpressionAdd, op(g, unreal.MaterialExpressionMultiply, comp[u], s, x + 400, y + 360), op(g, unreal.MaterialExpressionMultiply, comp[v], c, x + 400, y + 440), x + 550, y + 360)
        parts = [None, None, None]; parts[u] = nu; parts[v] = nv; parts[3 - u - v] = comp[3 - u - v]
        ab = node(g, unreal.MaterialExpressionAppendVector, x + 700, y + 200); link(parts[0], "", ab, "A"); link(parts[1], "", ab, "B")
        ac = node(g, unreal.MaterialExpressionAppendVector, x + 850, y + 200); link(ab, "", ac, "A"); link(parts[2], "", ac, "B")
        return ac

    # depth: the same spot seen a little deeper in along the line of sight (the camera vector in the model's space)
    cam = node(g, unreal.MaterialExpressionCameraVectorWS if hasattr(unreal, "MaterialExpressionCameraVectorWS") else unreal.MaterialExpressionCameraVector, X, Y + 900)
    camL = node(g, unreal.MaterialExpressionTransform, X + 150, Y + 900,
                transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL); link(cam, "", camL, "")
    nrm = node(g, unreal.MaterialExpressionNormalize, X + 300, Y + 900); link(camL, "", nrm, "")
    deep = op(g, unreal.MaterialExpressionSubtract, pr, op(g, unreal.MaterialExpressionMultiply, nrm, const(g, 0.8, X + 300, Y + 960), X + 450, Y + 900), X + 600, Y + 900)

    tA = turned(pr, 0.55, "z", X, Y + 1300)
    tB = turned(deep, -0.42, "x", X + 1000, Y + 1300)
    fn = noise_fn()
    nA = node(g, unreal.MaterialExpressionNoise, X + 1100, Y + 1000, scale=2.3, quality=1, noise_function=fn, levels=2, level_scale=2.0, turbulence=False, output_min=-1.0, output_max=1.0)
    nB = node(g, unreal.MaterialExpressionNoise, X + 1900, Y + 1000, scale=3.1, quality=1, noise_function=fn, levels=1, level_scale=2.0, turbulence=False, output_min=-1.0, output_max=1.0)
    link(tA, "", nA, ""); link(tB, "", nB, "")
    mix = op(g, unreal.MaterialExpressionAdd, op(g, unreal.MaterialExpressionMultiply, nA, const(g, 0.6, X + 1300, Y + 1000), X + 1450, Y + 1000), op(g, unreal.MaterialExpressionMultiply, nB, const(g, 0.5, X + 2100, Y + 1000), X + 2250, Y + 1000), X + 2400, Y + 1000)
    pat0 = node(g, unreal.MaterialExpressionSaturate, X + 2700, Y + 1000); link(op(g, unreal.MaterialExpressionAdd, op(g, unreal.MaterialExpressionMultiply, mix, const(g, 0.55, X + 2400, Y + 1100), X + 2550, Y + 1000), const(g, 0.5, X + 2550, Y + 1100), X + 2700, Y + 1050), "", pat0, "")
    pat = op(g, unreal.MaterialExpressionMultiply, pat0, pat0, X + 2900, Y + 1000)   # (soft: wisps and darker gaps)

    sstr = sca(g, "SmokeStrength", 0.0, X + 2700, Y + 1300)
    scol = vec(g, "SmokeColor", (0.35, 0.65, 1.0, 1.0), X + 2700, Y + 1400)
    amt = node(g, unreal.MaterialExpressionSaturate, X + 2900, Y + 1300); link(sstr, "", amt, "")
    # glow darkened in the gaps: lerp(1, 0.45 + 0.55 * pat0, amt)
    dark = node(g, unreal.MaterialExpressionLinearInterpolate, X + 3100, Y + 1000)
    link(const(g, 1.0, X + 2900, Y + 1100), "", dark, "A")
    link(op(g, unreal.MaterialExpressionAdd, op(g, unreal.MaterialExpressionMultiply, pat0, const(g, 0.8, X + 2900, Y + 1150), X + 3000, Y + 1150), const(g, 0.2, X + 3000, Y + 1250), X + 3100, Y + 1150), "", dark, "B")
    link(amt, "", dark, "Alpha")
    base = op(g, unreal.MaterialExpressionMultiply, src, dark, X + 3300, Y + 800, so, "")
    # wisps: SmokeColor * SmokeStrength * pat * mask, on the screen's own scale like the glow (exposure lock)
    extra = op(g, unreal.MaterialExpressionMultiply, op(g, unreal.MaterialExpressionMultiply, scol, sstr, X + 3000, Y + 1400, "RGB", ""), op(g, unreal.MaterialExpressionMultiply, pat, mask, X + 3000, Y + 1500), X + 3300, Y + 1400)
    if lock is not None and hasattr(unreal, "MaterialExpressionEyeAdaptation"):
        ea = node(g, unreal.MaterialExpressionEyeAdaptation, X + 3000, Y + 1650)
        inv = op(g, unreal.MaterialExpressionDivide, const(g, 1.0, X + 3000, Y + 1750), ea, X + 3150, Y + 1650)
        by = node(g, unreal.MaterialExpressionLinearInterpolate, X + 3300, Y + 1650)
        link(const(g, 1.0, X + 3150, Y + 1750), "", by, "A"); link(inv, "", by, "B"); link(lock, "", by, "Alpha")
        extra = op(g, unreal.MaterialExpressionMultiply, extra, by, X + 3500, Y + 1500)
    total = op(g, unreal.MaterialExpressionAdd, base, extra, X + 3700, Y + 1000)
    mel.connect_material_property(total, "", MP.MP_EMISSIVE_COLOR)

    # --- OrbGrow: the vertices in the ellipsoid move out from its centre (a quarter bigger at 0.25), in the world's frame
    grow = sca(g, "OrbGrow", 0.0, X + 600, Y + 2200)
    vm = op(g, unreal.MaterialExpressionMultiply, sub, grow, X + 800, Y + 2200)
    vmask = op(g, unreal.MaterialExpressionMultiply, vm, mask, X + 1000, Y + 2200)
    wv = node(g, unreal.MaterialExpressionTransform, X + 1200, Y + 2200,
              transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
              transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(vmask, "", wv, "")
    mel.connect_material_property(wv, "", MP.MP_WORLD_POSITION_OFFSET)
    g.set_editor_property("has_pixel_animation", True)
    mel.recompile_material(g)
    lib.save_asset("/Game/Props/M_PropGlow")
    log("M_PropGlow: smoke and OrbGrow built")
else:
    log("M_PropGlow already has SmokeStrength")

for name, grow, strength, col in (("MI_Staff", 0.25, 0.6, (0.05, 0.35, 1.0)), ("MI_CrystalBall", 0.0, 0.7, (0.05, 0.25, 1.0))):
    mi = lib.load_asset("/Game/Props/" + name)
    if mi is None: raise RuntimeError("ORBSMOKE no " + name)
    mel.set_material_instance_scalar_parameter_value(mi, "OrbGrow", grow)
    mel.set_material_instance_scalar_parameter_value(mi, "SmokeStrength", strength)
    mel.set_material_instance_vector_parameter_value(mi, "SmokeColor", unreal.LinearColor(col[0], col[1], col[2], 1.0))
    mel.update_material_instance(mi)
    lib.save_asset("/Game/Props/" + name)
    log("%s: OrbGrow %.2f smoke %.2f" % (name, grow, strength))

o = lib.load_asset("/Game/Wizard/M_WizardOrb")
if o is not None:
    for prop in ("output_translucent_velocity", "b_output_translucent_velocity"):
        try:
            o.set_editor_property(prop, True); log("M_WizardOrb: %s on" % prop); break
        except Exception as e:
            log("M_WizardOrb: no property %s (%s)" % (prop, e))
    mel.recompile_material(o)
    lib.save_asset("/Game/Wizard/M_WizardOrb")
log("done")

# ---- 4. M_PipePuff (/Game/Wizard): the pipe's smoke, a soft grey puff, densest in the middle and fading to nothing at its edge (the old puffs
# used the guard bubble's rim-lit material, so six of them stacked read as a chain of rings, copies of one another). Color and Opacity as M_WizardOrb.
PUFF = "/Game/Wizard/M_PipePuff"
if not lib.does_asset_exist(PUFF):
    folder = "/Game/Wizard"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    m = tools.create_asset("M_PipePuff", folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    col = vec(m, "Color", (0.55, 0.55, 0.58, 1.0), -700, 0)
    opa = sca(m, "Opacity", 0.5, -700, 250)
    fre = node(m, unreal.MaterialExpressionFresnel, -700, 450, exponent=1.6, base_reflect_fraction=0.0)
    mid = node(m, unreal.MaterialExpressionOneMinus, -500, 450); link(fre, "", mid, "")
    a = op(m, unreal.MaterialExpressionMultiply, opa, mid, -300, 300)
    mel.connect_material_property(col, "RGB", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(a, "", MP.MP_OPACITY)
    for prop in ("output_translucent_velocity",):
        try: m.set_editor_property(prop, True)
        except Exception as e: log("M_PipePuff: no " + prop)
    mel.recompile_material(m)
    lib.save_asset(PUFF)
    log("M_PipePuff built")
