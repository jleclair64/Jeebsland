# Builds M_Water (2026-11-13), the material on every water sheet: the sea and the rivers (WorldTerrain.cpp), the original
# square's pond (Pond.cpp), the castle's moat (Castle.cpp) and MoveLab's pond, all set through JeebWater.h. Pictures:
# Tools/water_textures.py (T_WaterN, T_WaterFoam). Run by Tools\BuildWater.ps1 in a windowless editor (editor closed).
#
# The look (owner, 2026-11-13: "more realistic ... right now the pond and lake are just flat blue"; CLAUDE.md art direction,
# Breath of the Wild meets real water): Unreal's Single Layer Water shading (no Water plugin, no Lumen needed). The water is
# see-through and tints what is under it by depth: clear in the shallows by the banks, teal over a metre or so, deep blue-green
# further down, so the shore has no hard colour edge and the knight's legs show when he wades. The sky and the banks reflect
# in it (screen-space reflections and the sky light), more at grazing angles (Fresnel), and the sun glints on small wind
# ripples: a ripple picture at two sizes crossing each other plus a slow broad swell. Rivers flow: the sheets carry the
# river's direction in their vertex colours (R, G = direction x 0.5 + 0.5, B = 0; still water is white), and the ripples drift
# downstream (a flow map: two copies half a cycle apart, faded across so the drift never visibly resets). A soft broken foam
# line where the water meets the bank or a wading leg. Far away the ripples calm and roughen so they never shimmer.
#
# Every number the look depends on is a material parameter; JeebWater.cpp's jeeb.Water.* console variables override them in
# a running game for tuning (no build).
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import water_textures   # noqa: E402

# Water by depth, per centimetre (Single Layer Water's units). Absorption takes red first, so depth turns teal then blue-green;
# scattering gives the water its own body colour where it is too deep to see the bottom.
ABSORPTION = (0.024, 0.017, 0.014)   # (owner 2026-11-13: dark natural slate, not bright blue, like a calm river below a weir)
SCATTERING = (0.0016, 0.0021, 0.0026)   # (the slate body colour. Keep it low: the sun lights it, so at the orange low sun more
                                        # turns the water into a muddy orange floor striped with tree shadows)
PHASE_G = 0.25
BEHIND_TINT = (0.95, 1.0, 0.88)   # what is seen through the water: a touch green-brown, like a real riverbed
ROUGHNESS = 0.14       # (lower and the sky's reflection breaks into hard white blotches)
ROUGHNESS_FAR = 0.22
SPECULAR = 0.25        # real water
RIPPLE = 0.28          # ripple strength (1 tilts the ripples up to 35 degrees: choppy, glittering everywhere)
FLOW_SPEED = 55.0      # cm/s downstream on the rivers
FOAM = 0.30            # how white the shore foam gets
FOAM_DEPTH = 14.0      # cm of water under which the foam line runs
FOAM_COLOR = (0.78, 0.84, 0.84)
FLOW_PERIOD = 3.0      # seconds per flow-map cycle

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary


def fresh_material(name):
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/" + name):
        unreal.EditorAssetLibrary.delete_asset("/Game/" + name)
    return tools.create_asset(name, "/Game", unreal.Material, unreal.MaterialFactoryNew())


def import_texture(png, name, normal=False):
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = "/Game"
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    tex = unreal.EditorAssetLibrary.load_asset("/Game/" + name)
    if tex is None:
        raise RuntimeError("WATERDIAG the picture " + name + " did not import")
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("srgb", False)
    unreal.EditorAssetLibrary.save_asset("/Game/" + name)
    return tex


class Graph:
    """Small helpers for wiring material nodes (as world_material.py's)"""

    def __init__(self, mat):
        self.mat = mat

    def node(self, cls, **props):
        n = lib.create_material_expression(self.mat, cls, 0, 0)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        if not lib.connect_material_expressions(a, out, b, pin):
            unreal.log_warning("WATERDIAG could not connect %s.%s -> %s.%s" % (a.get_name(), out, b.get_name(), pin))

    def c(self, v):
        return self.node(unreal.MaterialExpressionConstant, r=v)

    def c2(self, x, y):
        return self.node(unreal.MaterialExpressionConstant2Vector, r=x, g=y)

    def c3(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def mul(self, a, k, out=""):
        n = self.node(unreal.MaterialExpressionMultiply, const_b=k); self.link(a, n, "A", out); return n

    def add(self, a, k, out=""):
        n = self.node(unreal.MaterialExpressionAdd, const_b=k); self.link(a, n, "A", out); return n

    def muln(self, a, b):
        n = self.node(unreal.MaterialExpressionMultiply); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def addn(self, a, b):
        n = self.node(unreal.MaterialExpressionAdd); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def subn(self, a, b):
        n = self.node(unreal.MaterialExpressionSubtract); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def one(self, cls, a):
        n = self.node(cls); self.link(a, n, ""); return n

    def sat(self, a):
        return self.one(unreal.MaterialExpressionSaturate, a)

    def lerp(self, a, b, alpha):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, n, "A"); self.link(b, n, "B"); self.link(alpha, n, "Alpha")
        return n

    def lerpc(self, a_const, b_const, alpha):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, const_a=a_const, const_b=b_const)
        self.link(alpha, n, "Alpha")
        return n

    def ramp(self, a, lo, hi):
        return self.sat(self.mul(self.add(a, -lo), 1.0 / (hi - lo)))

    def mask(self, a, r, g, b=False, alpha=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=alpha); self.link(a, n); return n

    def append(self, a, b):
        n = self.node(unreal.MaterialExpressionAppendVector); self.link(a, n, "A"); self.link(b, n, "B"); return n

    def tex(self, texture, uv, normal=False):
        n = self.node(unreal.MaterialExpressionTextureSample, texture=texture)
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        self.link(uv, n, "UVs")
        return n

    def vector(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def out(self, a, prop, pin=""):
        if not lib.connect_material_property(a, pin, prop):
            unreal.log_warning("WATERDIAG could not connect to " + str(prop))


def build(normal_tex, foam_tex):
    mat = fresh_material("M_Water")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("tangent_space_normal", False)   # the sheets are flat and have no UVs: the normal is built in world space
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    g = Graph(mat)

    wp = g.node(unreal.MaterialExpressionWorldPosition)
    xy = g.mask(wp, True, True)
    yx = g.append(g.mul(g.mask(wp, False, True), -1.0), g.mask(wp, True, False))   # the same, turned 90 degrees
    t = g.node(unreal.MaterialExpressionTime)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    far = g.ramp(depth, 1500.0, 9000.0)

    # Flow (rivers): the direction from the vertex colour, nothing on still water (white)
    still = g.mask(vc, False, False, True)
    flow = g.muln(g.add(g.mul(g.mask(vc, True, True), 2.0), -1.0), g.one(unreal.MaterialExpressionOneMinus, still))
    flow = g.muln(flow, g.scalar("FlowSpeed", FLOW_SPEED))
    cyc = g.mul(t, 1.0 / FLOW_PERIOD)
    ph0 = g.one(unreal.MaterialExpressionFrac, cyc)
    ph1 = g.one(unreal.MaterialExpressionFrac, g.add(cyc, 0.5))
    blend = g.one(unreal.MaterialExpressionAbs, g.add(g.mul(ph0, 2.0), -1.0))   # 1 where copy 0 resets, so copy 1 shows then
    off0 = g.mul(g.muln(flow, ph0), FLOW_PERIOD)   # how far each copy has drifted (cm)
    off1 = g.mul(g.muln(flow, ph1), FLOW_PERIOD)
    off0r = g.append(g.mul(g.mask(off0, False, True), -1.0), g.mask(off0, True, False))   # (turned like yx)
    off1r = g.append(g.mul(g.mask(off1, False, True), -1.0), g.mask(off1, True, False))

    # The ripples: one picture at 4.2 m drifting with the wind, the same turned 90 degrees at 2.3 m drifting across it
    def ripple_a(off):
        return g.tex(normal_tex, g.addn(g.mul(g.subn(xy, off), 1.0 / 420.0), g.muln(t, g.c2(0.011, 0.006))), True)

    def ripple_b(off):
        return g.tex(normal_tex, g.addn(g.mul(g.subn(yx, off), 1.0 / 230.0), g.muln(t, g.c2(-0.013, 0.010))), True)

    na = g.lerp(ripple_a(off0), ripple_a(off1), blend)
    nb = g.lerp(ripple_b(off0r), ripple_b(off1r), blend)
    swell = g.tex(normal_tex, g.addn(g.mul(g.add(xy, 5100.0), 1.0 / 2900.0), g.muln(t, g.c2(0.0024, -0.0016))), True)
    slope = g.addn(g.addn(g.mul(g.mask(na, True, True), 0.42), g.mul(g.mask(nb, True, True), 0.30)), g.mul(g.mask(swell, True, True), 0.45))
    calm = g.lerpc(1.0, 0.30, far)   # far away the ripples calm (no shimmer)
    slope = g.muln(g.muln(slope, calm), g.scalar("Ripple", RIPPLE))
    normal = g.one(unreal.MaterialExpressionNormalize, g.append(slope, g.c(1.0)))
    g.out(normal, unreal.MaterialProperty.MP_NORMAL)

    rough = g.lerp(g.scalar("Roughness", ROUGHNESS), g.scalar("RoughnessFar", ROUGHNESS_FAR), far)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar("Specular", SPECULAR), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.c(0.0), unreal.MaterialProperty.MP_METALLIC)

    # Foam where the water is only a few centimetres deep over the bank (or a wading leg), broken into blobs
    behind = g.node(unreal.MaterialExpressionSceneDepthWithoutWater)
    try:
        behind.set_editor_property("fallback_depth", 100000.0)   # (anything not drawn behind: deep)
    except Exception:
        pass
    cam = g.node(unreal.MaterialExpressionCameraVectorWS)
    upness = g.one(unreal.MaterialExpressionAbs, g.mask(cam, False, False, True))
    thick = g.muln(g.subn(behind, depth), g.add(g.mul(upness, 0.85), 0.15))   # roughly the depth straight down
    per = g.node(unreal.MaterialExpressionDivide)
    g.link(thick, per, "A"); g.link(g.scalar("FoamDepth", FOAM_DEPTH), per, "B")
    shore = g.one(unreal.MaterialExpressionOneMinus, g.sat(per))
    blobs = g.mask(g.tex(foam_tex, g.addn(g.mul(xy, 1.0 / 190.0), g.muln(t, g.c2(0.012, 0.007)))), True, False)
    foam = g.sat(g.mul(g.subn(shore, g.mul(blobs, 0.95)), 3.0))   # (the blobs cut gaps right through the line)
    foam = g.muln(foam, shore)   # exactly none away from the shore: even 0.05 of foam shows in Single Layer Water as pale
                                 # hard-edged patches in the shape of the blob texture's gaps
    foam = g.muln(g.muln(foam, g.scalar("Foam", FOAM)), g.lerpc(1.0, 0.35, far))
    g.out(foam, unreal.MaterialProperty.MP_OPACITY)   # (in Single Layer Water, opacity is how much of the surface is foam)
    g.out(g.vector("FoamColor", FOAM_COLOR), unreal.MaterialProperty.MP_BASE_COLOR)

    # The water's body: what it does to light by depth
    out = g.node(unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
    def link_pin(src, names):
        for nm in names:   # (the pins' names, with or without spaces depending on the engine version)
            if lib.connect_material_expressions(src, "", out, nm):
                return
        unreal.log_error("WATERDIAG could not connect the water output pin " + names[0])

    link_pin(g.vector("Scattering", SCATTERING), ("ScatteringCoefficients", "Scattering Coefficients"))
    link_pin(g.vector("Absorption", ABSORPTION), ("AbsorptionCoefficients", "Absorption Coefficients"))
    link_pin(g.scalar("PhaseG", PHASE_G), ("PhaseG", "Phase G"))
    link_pin(g.vector("BehindTint", BEHIND_TINT), ("ColorScaleBehindWater", "Color Scale Behind Water"))

    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset("/Game/M_Water")
    unreal.log("WATERDIAG built M_Water")


normal_tex = import_texture(water_textures.water_normal(), "T_WaterN", normal=True)
foam_tex = import_texture(water_textures.water_foam(), "T_WaterFoam")
build(normal_tex, foam_tex)
unreal.log("WATERDIAG water material built")
