# The 2026-11-18 visual review's asset changes (Tools/FixLookAssets.ps1, a windowless editor, the editor of this project closed).
# Safe to run again: the bridge and stall steps check what is already there; the glow, bolt and people materials are rebuilt each run.
# Vector parameters are linked by their RGB output: a float4 mixed with a picture's float3 fails to compile (grey people, 2026-11-18).
#  1. M_Bridge gets a Tint (multiplies the picture); MI_Bridge warms the cool blue-grey stone toward Oak Hill's beige (about 5%).
#  2. M_PropGlow (/Game/Props): a copy of M_Prop with a soft glow inside an ellipsoid round a point of the model (GlowCentre, GlowRadii,
#     model space, cm) on its untinted parts (vertex red 0: never the tier's steel): MI_Staff's orb (cyan) and MI_Wand's crystal (violet)
#     move onto it. M_Prop itself is not touched.
#  3. M_GlowOrb (/Game/Props): the magic bolt, unlit and additive, white at its heart and its gem colour toward the rim (Projectile.cpp).
#  4. M_Person (/Game/People) rebuilt in place with an optional skin dye: SkinMask (the Smith's by default, off unless SkinTint's alpha is 1), SkinLum, SkinTint
#     (alpha 0: off; ANpc sets it from PeopleWho.csv Skin). T_Smith_Skin (Art/People/Smith/T_Smith_Skin.png, Tools/people_skin_mask.py
#     from the Smith's own picture: light warm skin, not cloth, leather or hair) goes on MI_Smith: the trolls are the Smith body, green-grey.
#     (Tools/ImportPeople.ps1 rebuilds M_Person without the skin dye: run this again after it.)
#  5. M_SteeleToned + MI_SteeleStall (/Game/SteeleCreek): the market stall's own pictures through a grade (Desat, Tint): Steele
#     Creek's ore stall in rich brown wood and a muted roof instead of saturated orange (SteeleCreekTown.cpp). Oak Hill's is unchanged.
#  6. M_SteeleTonedProp: the same grade on a copy of MI_MineCart's parent; the mine carts' orange wood toned to brown.
import os
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
E = mel.create_material_expression
MP = unreal.MaterialProperty
proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def log(s): unreal.log("FIXLOOK " + s)


def vec(m, name, v, x, y):
    p = E(m, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name); p.set_editor_property("default_value", unreal.LinearColor(*v))
    return p


def sca(m, name, v, x, y):
    p = E(m, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name); p.set_editor_property("default_value", v)
    return p


def const(m, v, x, y):
    c = E(m, unreal.MaterialExpressionConstant, x, y); c.set_editor_property("r", v); return c


def node(m, cls, x, y, **props):
    n = E(m, cls, x, y)
    for k, v in props.items(): n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi): mel.connect_material_expressions(a, ao, b, bi)


def input_of(m, prop):
    n = mel.get_material_property_input_node(m, prop)
    o = mel.get_material_property_input_node_output_name(m, prop) if n else ""
    return n, o


def has_param(m, name):
    try:
        return name in [str(p) for p in mel.get_vector_parameter_names(m)] + [str(p) for p in mel.get_scalar_parameter_names(m)]
    except Exception:
        return False


# ---------------------------------------------------------------- 1. the bridge
m = lib.load_asset("/Game/Bridge/M_Bridge")
if m is None: raise RuntimeError("FIXLOOK no M_Bridge")
if not has_param(m, "Tint"):
    src, so = input_of(m, MP.MP_BASE_COLOR)
    t = vec(m, "Tint", (1.0, 1.0, 1.0, 1.0), -500, -350)
    mul = node(m, unreal.MaterialExpressionMultiply, -250, -250)
    link(src, so, mul, "A"); link(t, "", mul, "B")
    mel.connect_material_property(mul, "", MP.MP_BASE_COLOR)
    mel.recompile_material(m)
    lib.save_asset("/Game/Bridge/M_Bridge")
mi = lib.load_asset("/Game/Bridge/MI_Bridge")
mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(1.07, 1.02, 0.92, 1.0))
mel.update_material_instance(mi)
lib.save_asset("/Game/Bridge/MI_Bridge")
log("bridge tinted warm")

# ---------------------------------------------------------------- 2. the staff's orb and the wand's crystal
GLOW = "/Game/Props/M_PropGlow"
if lib.does_asset_exist(GLOW): lib.delete_asset(GLOW)   # (rebuilt every run from today's M_Prop; the instances are re-parented below)
if not lib.does_asset_exist(GLOW):
    g = lib.duplicate_asset("/Game/Props/M_Prop", GLOW)
    if g is None: raise RuntimeError("FIXLOOK could not copy M_Prop")
    src, so = input_of(g, MP.MP_BASE_COLOR)
    if hasattr(unreal, "MaterialExpressionLocalPosition"):
        lp = node(g, unreal.MaterialExpressionLocalPosition, -1900, 1500)
    else:   # (the world position taken back into the model's own space)
        wp = node(g, unreal.MaterialExpressionWorldPosition, -2100, 1500)
        lp = node(g, unreal.MaterialExpressionTransformPosition, -1900, 1500,
                  transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                  transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
        link(wp, "", lp, "")
    cen = vec(g, "GlowCentre", (0.0, 0.0, 0.0, 0.0), -1900, 1650)
    rad = vec(g, "GlowRadii", (1.0, 1.0, 1.0, 0.0), -1900, 1800)
    sub = node(g, unreal.MaterialExpressionSubtract, -1700, 1550); link(lp, "", sub, "A"); link(cen, "", sub, "B")
    div = node(g, unreal.MaterialExpressionDivide, -1550, 1600); link(sub, "", div, "A"); link(rad, "", div, "B")
    dot = node(g, unreal.MaterialExpressionDotProduct, -1400, 1600); link(div, "", dot, "A"); link(div, "", dot, "B")
    d = node(g, unreal.MaterialExpressionSquareRoot, -1250, 1600); link(dot, "", d, "")
    # mask: 1 inside d 0.95, 0 past 1.15, and only where the vertex red is 0 (not the tier's steel)
    edge = node(g, unreal.MaterialExpressionSubtract, -1100, 1600); link(const(g, 1.15, -1250, 1700), "", edge, "A"); link(d, "", edge, "B")
    k = node(g, unreal.MaterialExpressionMultiply, -950, 1600); link(edge, "", k, "A"); link(const(g, 5.0, -1100, 1700), "", k, "B")
    sat = node(g, unreal.MaterialExpressionSaturate, -800, 1600); link(k, "", sat, "")
    vc = node(g, unreal.MaterialExpressionVertexColor, -1100, 1800)
    notsteel = node(g, unreal.MaterialExpressionOneMinus, -950, 1800); link(vc, "R", notsteel, "")
    mask = node(g, unreal.MaterialExpressionMultiply, -650, 1650); link(sat, "", mask, "A"); link(notsteel, "", mask, "B")
    col = vec(g, "GlowColor", (0.2, 0.85, 1.0, 1.0), -800, 1900)
    gs = sca(g, "GlowStrength", 0.0, -800, 2050)
    em1 = node(g, unreal.MaterialExpressionMultiply, -500, 1900); link(col, "RGB", em1, "A"); link(gs, "", em1, "B")
    em = node(g, unreal.MaterialExpressionMultiply, -350, 1800); link(em1, "", em, "A"); link(mask, "", em, "B")
    mel.connect_material_property(em, "", MP.MP_EMISSIVE_COLOR)
    # the gem's own colour too (the bake left the orb brown): most of the way to a deeper copy of the glow colour
    deep = node(g, unreal.MaterialExpressionMultiply, -500, 1450); link(col, "RGB", deep, "A"); link(const(g, 0.45, -650, 1450), "", deep, "B")
    amt = node(g, unreal.MaterialExpressionMultiply, -500, 1550); link(mask, "", amt, "A"); link(const(g, 0.9, -650, 1550), "", amt, "B")
    lerp = node(g, unreal.MaterialExpressionLinearInterpolate, -200, 1400)
    rgb = node(g, unreal.MaterialExpressionComponentMask, -350, 1350, r=True, g=True, b=True, a=False); link(src, so, rgb, "")   # (always float3)
    link(rgb, "", lerp, "A"); link(deep, "", lerp, "B"); link(amt, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", MP.MP_BASE_COLOR)
    mel.recompile_material(g)
    lib.save_asset(GLOW)
g = lib.load_asset(GLOW)
# (model space, cm: Saved/KnightBuild/Props/<name>.fbx; the staff's orb centred 117.3 cm down its axis, radius about 9.5 across and
# 8.5 along; the wand's crystal from 38.5 to 43.2 cm down, about 2.5 cm across. The radii reach past the gem: only its surface shows)
for name, centre, radii, colour, strength in (
        ("MI_Staff", (0.0, 0.0, -117.3), (12.5, 12.5, 11.0), (0.20, 0.85, 1.0), 3.0),
        ("MI_Wand", (0.0, 0.0, -40.9), (3.8, 3.8, 3.6), (0.62, 0.20, 1.0), 3.5)):
    p = "/Game/Props/" + name
    mi = lib.load_asset(p)
    if mi is None: raise RuntimeError("FIXLOOK no " + p)
    mel.set_material_instance_parent(mi, g)
    mel.set_material_instance_vector_parameter_value(mi, "GlowCentre", unreal.LinearColor(centre[0], centre[1], centre[2], 0.0))
    mel.set_material_instance_vector_parameter_value(mi, "GlowRadii", unreal.LinearColor(radii[0], radii[1], radii[2], 0.0))
    mel.set_material_instance_vector_parameter_value(mi, "GlowColor", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "GlowStrength", strength)
    mel.update_material_instance(mi)
    lib.save_asset(p)
    log("%s glows (parent %s)" % (name, mi.get_editor_property("parent").get_name()))

# ---------------------------------------------------------------- 3. the magic bolt
ORB = "/Game/Props/M_GlowOrb"
if lib.does_asset_exist(ORB): lib.delete_asset(ORB)   # (rebuilt every run; Projectile.cpp loads it by name)
if not lib.does_asset_exist(ORB):
    o = tools.create_asset("M_GlowOrb", "/Game/Props", unreal.Material, unreal.MaterialFactoryNew())
    o.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    o.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    fr = node(o, unreal.MaterialExpressionFresnel, -900, 0, exponent=1.6, base_reflect_fraction=0.0)
    core = node(o, unreal.MaterialExpressionOneMinus, -750, 0); link(fr, "", core, "")
    c2 = node(o, unreal.MaterialExpressionMultiply, -600, 0); link(core, "", c2, "A"); link(core, "", c2, "B")
    c4 = node(o, unreal.MaterialExpressionMultiply, -450, 0); link(c2, "", c4, "A"); link(c2, "", c4, "B")
    col = vec(o, "Color", (0.22, 0.82, 1.0, 1.0), -900, 200)
    st = sca(o, "Strength", 4.0, -900, 350)
    a = node(o, unreal.MaterialExpressionMultiply, -600, 200); link(col, "RGB", a, "A"); link(st, "", a, "B")
    soft = node(o, unreal.MaterialExpressionMultiply, -450, 200); link(a, "", soft, "A"); link(core, "", soft, "B")   # (its colour, fading to the rim)
    w = node(o, unreal.MaterialExpressionMultiply, -450, 350); link(c4, "", w, "A"); link(st, "", w, "B")             # (a white-hot heart)
    w2 = node(o, unreal.MaterialExpressionMultiply, -300, 350); link(w, "", w2, "A"); link(const(o, 0.3, -450, 450), "", w2, "B")
    add = node(o, unreal.MaterialExpressionAdd, -150, 250); link(soft, "", add, "A"); link(w2, "", add, "B")
    mel.connect_material_property(add, "", MP.MP_EMISSIVE_COLOR)
    mel.recompile_material(o)
    lib.save_asset(ORB)
log("bolt material ready")

# ---------------------------------------------------------------- 4. the people's skin dye
skin_png = os.path.join(proj, "Art", "People", "Smith", "T_Smith_Skin.png")   # (Tools/people_skin_mask.py)
if not os.path.exists(skin_png): raise RuntimeError("FIXLOOK missing " + skin_png)
t = unreal.AssetImportTask()
t.filename = skin_png; t.destination_path = "/Game/People"; t.destination_name = "T_Smith_Skin"
t.replace_existing = True; t.automated = True; t.save = True
tools.import_asset_tasks([t])
sk = lib.load_asset("/Game/People/T_Smith_Skin")
sk.set_editor_property("srgb", False)
sk.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
sk.set_editor_property("max_texture_size", 1024)
lib.save_asset("/Game/People/T_Smith_Skin")

pm = lib.load_asset("/Game/People/M_Person")
if pm is None: raise RuntimeError("FIXLOOK no M_Person")
base_tex = lib.load_asset("/Game/People/T_Banker_Base"); mask_tex = lib.load_asset("/Game/People/T_Banker_Mask")
mel.delete_all_material_expressions(pm)
pm.set_editor_property("used_with_skeletal_mesh", True)


def tex(m, name, texture, x, y, kind):
    s = E(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    s.set_editor_property("parameter_name", name); s.set_editor_property("texture", texture); s.set_editor_property("sampler_type", kind)
    return s


tb = tex(pm, "Base", base_tex, -1400, 0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
tm = tex(pm, "Mask", mask_tex, -1400, 400, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
# (the default mask is the Smith's own: a grayscale texture, as the sampler needs (the engine's Black is a colour texture and failed the
# compile); SkinTint's alpha 0 keeps the dye off for everyone not given a Skin in PeopleWho.csv)
ts = tex(pm, "SkinMask", sk, -1400, 800, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
w = node(pm, unreal.MaterialExpressionConstant3Vector, -1200, 200, constant=unreal.LinearColor(0.2126, 0.7152, 0.0722, 1.0))
lum = node(pm, unreal.MaterialExpressionDotProduct, -1000, 150); link(tb, "RGB", lum, "A"); link(w, "", lum, "B")


def dyed_by(lum_param, lum_default, tint_param, tint_default, y):
    gl = sca(pm, lum_param, lum_default, -1000, y)
    dv = node(pm, unreal.MaterialExpressionDivide, -850, y); link(lum, "", dv, "A"); link(gl, "", dv, "B")
    cl = node(pm, unreal.MaterialExpressionClamp, -700, y, min_default=0.0, max_default=2.5); link(dv, "", cl, "")
    ti = vec(pm, tint_param, tint_default, -700, y + 120)
    mu = node(pm, unreal.MaterialExpressionMultiply, -550, y); link(cl, "", mu, "A"); link(ti, "RGB", mu, "B")   # (RGB: a float4 here fails the lerp with the picture's float3)
    return mu, ti


dyed, _ = dyed_by("GarmentLum", 0.1, "Tint", (0.10, 0.10, 0.10, 1.0), 300)
skin, skin_tint = dyed_by("SkinLum", 0.35, "SkinTint", (1.0, 1.0, 1.0, 0.0), 700)
skin_amt = node(pm, unreal.MaterialExpressionMultiply, -550, 900); skin_on = sca(pm, "SkinOn", 0.0, -700, 1000); link(ts, "R", skin_amt, "A"); link(skin_on, "", skin_amt, "B")   # (2026-11-18: skin dye off for everyone, SkinOn 0: only trolls were ever dyed and they have their own model)
mid = node(pm, unreal.MaterialExpressionLinearInterpolate, -350, 600)
link(tb, "RGB", mid, "A"); link(skin, "", mid, "B"); link(skin_amt, "", mid, "Alpha")   # (skin dye kept in the graph but never on: SkinTint alpha stays 0; no one is given a Skin row since 2026-11-18, human skin is the picture's own)
dye = sca(pm, "Dye", 1.0, -700, 500)
amt = node(pm, unreal.MaterialExpressionMultiply, -550, 450); link(tm, "R", amt, "A"); link(dye, "", amt, "B")
fin = node(pm, unreal.MaterialExpressionLinearInterpolate, -150, 300)
link(mid, "", fin, "A"); link(dyed, "", fin, "B"); link(amt, "", fin, "Alpha")
mel.connect_material_property(fin, "", MP.MP_BASE_COLOR)
for prop, val, y in ((MP.MP_ROUGHNESS, 0.82, 1100), (MP.MP_SPECULAR, 0.35, 1200)):
    mel.connect_material_property(const(pm, val, -150, y), "", prop)
mel.recompile_material(pm)
lib.save_asset("/Game/People/M_Person")
ms = lib.load_asset("/Game/People/MI_Smith")
mel.set_material_instance_texture_parameter_value(ms, "SkinMask", sk)
mel.set_material_instance_scalar_parameter_value(ms, "SkinLum", 0.3506)
mel.update_material_instance(ms)
lib.save_asset("/Game/People/MI_Smith")
for n in ("Banker", "Elder", "Guard", "Townsman", "Townswoman"):
    mi = lib.load_asset("/Game/People/MI_" + n)
    if mi: mel.update_material_instance(mi); lib.save_asset("/Game/People/MI_" + n)
log("people skin dye ready")

# ---------------------------------------------------------------- 5. Steele Creek's ore stall
def toned_copy(src_path, dest):
    """A copy of a material with its base colour through Desat (toward grey) and Grade (multiply), both off by default."""
    if lib.does_asset_exist(dest): return lib.load_asset(dest)
    s = lib.duplicate_asset(src_path, dest)
    if s is None: raise RuntimeError("FIXLOOK could not copy " + src_path)
    src, so = input_of(s, MP.MP_BASE_COLOR)
    rgb = node(s, unreal.MaterialExpressionComponentMask, -850, -650, r=True, g=True, b=True, a=False); link(src, so, rgb, "")
    wl = node(s, unreal.MaterialExpressionConstant3Vector, -700, -700, constant=unreal.LinearColor(0.2126, 0.7152, 0.0722, 1.0))
    l = node(s, unreal.MaterialExpressionDotProduct, -550, -650); link(rgb, "", l, "A"); link(wl, "", l, "B")
    ds = sca(s, "Desat", 0.0, -550, -500)
    gr = node(s, unreal.MaterialExpressionLinearInterpolate, -400, -600); link(rgb, "", gr, "A"); link(l, "", gr, "B"); link(ds, "", gr, "Alpha")
    ti = vec(s, "Grade", (1.0, 1.0, 1.0, 1.0), -400, -450)
    mu = node(s, unreal.MaterialExpressionMultiply, -250, -550); link(gr, "", mu, "A"); link(ti, "RGB", mu, "B")
    mel.connect_material_property(mu, "", MP.MP_BASE_COLOR)
    mel.recompile_material(s)
    lib.save_asset(dest)
    return s


TONED = "/Game/SteeleCreek/M_SteeleToned"
s = toned_copy("/Game/TownLab/Buildings/M_TL_Building", TONED)
src_mi = lib.load_asset("/Game/TownLab/Buildings/MI_TL_MarketStall")
STALL = "/Game/SteeleCreek/MI_SteeleStall"
st = lib.load_asset(STALL) if lib.does_asset_exist(STALL) else tools.create_asset("MI_SteeleStall", "/Game/SteeleCreek", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(st, s)
for p in ("BaseTex", "MRTex", "NormalTex", "GlowTex", "MaskTex"):
    v = mel.get_material_instance_texture_parameter_value(src_mi, p)
    if v: mel.set_material_instance_texture_parameter_value(st, p, v)
for p in ("GlowStrength", "FireLevel", "UseMask"):
    try: mel.set_material_instance_scalar_parameter_value(st, p, mel.get_material_instance_scalar_parameter_value(src_mi, p))
    except Exception: pass
mel.set_material_instance_scalar_parameter_value(st, "Desat", 0.45)
mel.set_material_instance_vector_parameter_value(st, "Grade", unreal.LinearColor(0.70, 0.56, 0.46, 1.0))
mel.update_material_instance(st)
lib.save_asset(STALL)
log("steele stall toned")

# ---------------------------------------------------------------- 6. Steele Creek's mine carts (their own orange wood, toned like the stall)
cart = lib.load_asset("/Game/SteeleCreek/MI_MineCart")
if cart:
    par = cart.get_editor_property("parent")
    CARTM = "/Game/SteeleCreek/M_SteeleTonedProp"
    if par and par.get_path_name().split(".")[0] != CARTM:
        tp = toned_copy(par.get_path_name().split(".")[0], CARTM)
        mel.set_material_instance_parent(cart, tp)
    mel.set_material_instance_scalar_parameter_value(cart, "Desat", 0.35)
    mel.set_material_instance_vector_parameter_value(cart, "Grade", unreal.LinearColor(0.78, 0.62, 0.52, 1.0))
    mel.update_material_instance(cart)
    lib.save_asset("/Game/SteeleCreek/MI_MineCart")
    log("mine carts toned (parent %s)" % cart.get_editor_property("parent").get_name())
log("done")
