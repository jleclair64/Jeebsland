# Builds the two materials for Oak Hill's life (2026-11-12) without rebuilding the rest: M_ChimneySmoke and M_ClothWind.
# Run with Tools\BuildTownLifeMaterials.ps1 (the editor must be closed). It borrows the helpers and the approved wind (add_wind) from
# build_materials.py by running only that file's definitions, never its builds.
#
# M_ClothWind: a reusable wind-sway material for cloth (banners, awnings, market canopies, flags, hanging cloth). The cloth leans
# downwind and flutters with the same gust field as the grass and trees (add_wind), and an "attach mask" says how far each point may
# move: 0 where the cloth is fixed, 1 where it hangs free. See Docs/ClothWind.md for how to use it on a prop.
import os
import sys

_here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _here)
_src = open(os.path.join(_here, "build_materials.py"), encoding="utf-8").read().split("\nimport_texture(os.path.join(")[0]
__file__ = os.path.join(_here, "build_materials.py")
exec(compile(_src, __file__, "exec"), globals())


def build_chimney_smoke():
    # A soft puff of chimney smoke on a sphere: dense in the middle, wispy at the rim, a little see-through even at its densest.
    # The game (OakHillLife.cpp) gives every puff its own colour and opacity through the instance's custom data (r, g, b, opacity),
    # so each can be tinted by the light of the hour and fade in and out as it rises. Unlit: the colour is the whole look.
    mat, g = unlit("M_ChimneySmoke", unreal.BlendMode.BLEND_TRANSLUCENT, two_sided=False)
    def cd(i):
        return g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=i, const_default_value=(0.8 if i < 3 else 0.0))
    rim = g.node(unreal.MaterialExpressionFresnel, exponent=1.1, base_reflect_fraction=0.0)
    core = g.one(unreal.MaterialExpressionOneMinus, rim)
    soft = g.power(core, 2.6)
    opacity = g.muln(soft, cd(3))
    try:
        fade = g.node(unreal.MaterialExpressionDepthFade, fade_distance_default=70.0)   # no hard line where a puff meets a roof
        g.link(opacity, fade, "Opacity")
        opacity = fade
    except Exception as e:
        unreal.log("MATDIAG depth fade skipped: " + str(e))
    g.out(g.one(unreal.MaterialExpressionSaturate, opacity), unreal.MaterialProperty.MP_OPACITY)
    rg = g.node(unreal.MaterialExpressionAppendVector); g.link(cd(0), rg, "A"); g.link(cd(1), rg, "B")
    rgb = g.node(unreal.MaterialExpressionAppendVector); g.link(rg, rgb, "A"); g.link(cd(2), rgb, "B")
    g.out(rgb, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, "M_ChimneySmoke")


def build_cloth_wind():
    mat = fresh_material("M_ClothWind")
    mat.set_editor_property("two_sided", True)
    set_flags(mat, ("used_with_instanced_static_meshes",))
    g = Graph(mat)
    # Which point is how pinned: from UV V (0 at the top of the mesh's UV square, so a banner hung from its top edge works at once),
    # from UV U (MaskFromU = 1: a flag fixed at its left side), or painted in the vertex colour's red (MaskFromVertexColor = 1: 0 = pinned,
    # 1 = free). Flip turns it round. MaskPower eases it (1.5: the pinned edge stays still, the free edge moves most).
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    v = g.mask(uv, False, True)
    u = g.mask(uv, True, False)
    vc = g.mask(g.node(unreal.MaterialExpressionVertexColor), True, False)
    def mix(a, b, param):
        n = g.node(unreal.MaterialExpressionLinearInterpolate)
        g.link(a, n, "A"); g.link(b, n, "B"); g.link(param, n, "Alpha")
        return n
    m = mix(mix(v, u, g.scalar("MaskFromU", 0.0)), vc, g.scalar("MaskFromVertexColor", 0.0))
    m = mix(m, g.one(unreal.MaterialExpressionOneMinus, m), g.scalar("FlipMask", 0.0))
    m = g.one(unreal.MaterialExpressionSaturate, m)
    p = g.node(unreal.MaterialExpressionPower); g.link(m, p, "Base"); g.link(g.scalar("MaskPower", 1.5), p, "Exp")
    attach = g.muln(p, g.scalar("Sway", 1.0))   # Sway: 0 = still cloth, 1 = as the grass moves, up to 2 for light silk
    shade = add_wind(mat, 30.0, False, 0.0, attach_mask=attach)   # (30 cm of lean at the free edge in a full gust, scaled by Sway)
    g.out(g.muln(g.vector("Color", (0.6, 0.1, 0.08)), shade), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=1.0), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.04), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.muln(g.const3((0.0, 0.0, 1.0)), g.node(unreal.MaterialExpressionTwoSidedSign)), unreal.MaterialProperty.MP_NORMAL)
    finish(mat, "M_ClothWind")


build_chimney_smoke()
build_cloth_wind()
unreal.log("MATDIAG all materials built")
