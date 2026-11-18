# One field colour for the grass blades (M_Grass, build_materials.py) and the ground under them (M_Ground, world_material.py),
# 2026-11-14 (owner: grass "more similar to the grass in the pictures I sent", Docs/TownLab/Refs and StyleSheet.md: one soft,
# painted-looking field with lots of colour variation, yellow-green in sun, deeper green in shade, warm dry patches).
#
# Both materials multiply their own grass colour by the SAME world-space patches, in the same order, so from any distance the
# blades and the ground they stand on shift together: before, the ground had broad swathes the blades lacked (from the air the
# ground's yellow-green swathes did not match the olive blades, and read muddy at the low sun).
#
# g is a world_material.Graph (or FieldGraph below, the same helpers); xy the world position's XY; patches T_Patches.

# The recipe is the rebuilt Oak Hill's lawn (M_TL_Ground, approved by the owner 2026-11-14), now used by all three: before, the
# world's ground pulled 45% toward grey with its swathes added on top, the lawn 30% with its own, the blades 45% with neither.
GRASS_DESATURATION = 0.30   # how far the grass and the ground under it are pulled toward grey (0.45 for the blades and the world
                            # until 2026-11-14's overhaul; the lawn's 0.30 now everywhere)

import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import palette   # noqa: E402  (2026-11-15: the colours live in Content/Data/Palette.ini; the materials read them from MPC_JeebPalette)

HOLLOW = palette.rgb("FieldHollow")   # the 45 m patches: deeper green hollows ...
SUNNY = palette.rgb("FieldSunny")     # ... to sunny yellow-green swathes
DRY = palette.rgb("FieldDry")         # sporadic warm dry patches, about 30 m across (Breath of the Wild's golden meadow patches)
DRY_SHARE = 0.45                 # how far a dry patch goes toward DRY at its heart
LIFT = (1.12, 1.12, 1.08)        # flat ground only, never the blades: the blades are lit through and catch more light than flat
                                 # ground, so the ground is this much brighter to meet them where they stop (the lawn's Tint)


def noises(g, xy, patches):
    """the soft noises every grass colour shares (the small T_Patches picture, cheap)"""
    n = {}
    n["rag"] = g.mask(g.tex(patches, g.mul(xy, 1.0 / 170.0)), True, False)                # ragged edges, about 1.5 m
    med = g.mask(g.tex(patches, g.mul(g.add(xy, 3100.0), 1.0 / 1100.0)), True, False)     # 10 m patches
    n["med2"] = g.mask(g.tex(patches, g.mul(g.add(xy, -5300.0), 1.0 / 1730.0)), True, False)   # the same at an unrelated size
    n["med"] = med
    n["medm"] = g.mul(g.addn(med, n["med2"]), 0.5)                                        # averaged: neither one's repeat shows from the air
    n["patchy"] = g.mask(g.tex(patches, g.mul(xy, 1.0 / 4500.0)), True, False)            # 45 m patches
    n["big"] = g.mask(g.tex(patches, g.mul(g.add(xy, -7700.0), 1.0 / 26000.0)), True, False)   # broad swathes, a few hundred metres
    n["dry"] = g.mask(g.tex(patches, g.mul(g.add(xy, 9100.0), 1.0 / 3300.0)), True, False)     # where the dry patches lie
    return n


def before_grey(g, col, n):
    """lightness patches, the hollows and sunny swathes, a gentle hue wander and the dry patches; before the pull toward grey"""
    col = g.muln(col, g.lerpc(0.90, 1.08, n["medm"]))
    col = g.muln(col, g.lerp(palette.colour(g.mat, "FieldHollow"), palette.colour(g.mat, "FieldSunny"), n["patchy"]))
    col = g.muln(col, g.lerpc(0.84, 1.10, n["big"]))
    col = g.muln(col, g.lerp(g.c3((0.97, 1.0, 1.02)), g.c3((1.02, 1.0, 0.95)), n["med2"]))
    dry = g.mul(g.ramp(g.addn(n["dry"], g.mul(g.add(n["rag"], -0.5), 0.25)), 0.66, 0.82), DRY_SHARE)
    return g.lerp(col, g.muln(col, palette.colour(g.mat, "FieldDry")), dry)


def grey(g, col):
    return g.desat(col, GRASS_DESATURATION)


def ground_lift(g, col):
    """flat ground only (M_Ground, M_TL_Ground): never the blades"""
    return g.muln(col, g.vector("Lift", LIFT))


# The ground under the blades (2026-11-15, owner: "when it's dark and there's shade, the bottom layer on the ground is brighter than
# the grass and seems to glow"). The low sun's fill (SkyFill) is emissive: it ignores shadow, so under trees and buildings at dusk the
# painted ground between the blades stayed lit while the blades went dark. The fill is only wanted where the blades have stopped
# (they are lit through; bare flat ground goes dark at the low sun), so it now comes in only past them, and under them the ground is
# the meadow's shaded floor, a touch darker than the blades at the low sun (Floor). AWorldTerrain / ATownLabTown set every value
# live from jeeb.Grass.PackFade, ADayNight::Dusk and jeeb.Ground.* (AWorldTerrain::SetUnderGrass); the defaults here are HIGH's.
def beyond_blades(g, depth):
    """0 among the grass blades near the camera, rising to 1 where they have sunk into the ground (FillFrom..FillTo, cm)"""
    lo = g.scalar("FillFrom", 2500.0)
    hi = g.scalar("FillTo", 5000.0)
    up = g.node(unreal.MaterialExpressionSubtract); g.link(depth, up, "A"); g.link(lo, up, "B")
    span = g.node(unreal.MaterialExpressionSubtract); g.link(hi, span, "A"); g.link(lo, span, "B")
    div = g.node(unreal.MaterialExpressionDivide); g.link(up, div, "A"); g.link(span, div, "B")
    return g.sat(div)


def under_grass_floor(g, col, beyond):
    """the grass ground's colour times Floor among the blades (1 at noon, jeeb.Ground.Floor at the low sun), unchanged past them"""
    k = g.node(unreal.MaterialExpressionLinearInterpolate, const_b=1.0)
    g.link(g.scalar("Floor", 1.0), k, "A"); g.link(beyond, k, "Alpha")
    col = g.muln(col, k)
    # FarTint (2026-11-17, owner: the ground past the grass "looks brighter than the tree trunks and up close grass"): past the blades
    # the flat ground is lit straight on while the blades shade each other, so it read as a bright yellow-green band. It is toned
    # darker and less yellow there only (jeeb.Ground.FarTint, live; AWorldTerrain::SetUnderGrass).
    return g.muln(col, g.lerp(g.c3((1.0, 1.0, 1.0)), g.mask(g.vector("FarTint", (0.6857, 0.7197, 0.6214)), True, True, True), beyond))


def under_grass_fill(g, fill, beyond):
    """the low sun's emissive fill: UnderFill of it among the blades (0 none, 1 the old look), all of it past them"""
    k = g.node(unreal.MaterialExpressionLinearInterpolate, const_b=1.0)
    g.link(g.scalar("UnderFill", 0.0), k, "A"); g.link(beyond, k, "Alpha")
    return g.muln(fill, k)


try:
    import unreal
    lib = unreal.MaterialEditingLibrary

    class FieldGraph:
        """world_material.Graph's helpers, for build_materials.py's grass (world_material.py can't be imported: it builds on import)"""

        def __init__(self, mat):
            self.mat = mat

        def node(self, cls, **props):
            n = lib.create_material_expression(self.mat, cls, 0, 0)
            for k, v in props.items():
                n.set_editor_property(k, v)
            return n

        def link(self, a, b, pin="", out=""):
            if not lib.connect_material_expressions(a, out, b, pin):
                raise RuntimeError("MATDIAG link failed: %s.%s -> %s.%s" % (a.get_class().get_name(), out, b.get_class().get_name(), pin))

        def c(self, v):
            return self.node(unreal.MaterialExpressionConstant, r=v)

        def c3(self, rgb):
            return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

        def mul(self, a, k, out=""):
            n = self.node(unreal.MaterialExpressionMultiply, const_b=k); self.link(a, n, "A", out); return n

        def add(self, a, k, out=""):
            n = self.node(unreal.MaterialExpressionAdd, const_b=k); self.link(a, n, "A", out); return n

        def muln(self, a, b, out_a="", out_b=""):
            n = self.node(unreal.MaterialExpressionMultiply); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

        def addn(self, a, b, out_a="", out_b=""):
            n = self.node(unreal.MaterialExpressionAdd); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

        def subn(self, a, b, out_a="", out_b=""):
            n = self.node(unreal.MaterialExpressionSubtract); self.link(a, n, "A", out_a); self.link(b, n, "B", out_b); return n

        def one(self, cls, a, out=""):
            n = self.node(cls); self.link(a, n, "", out); return n

        def sat(self, a):
            return self.one(unreal.MaterialExpressionSaturate, a)

        def lerp(self, a, b, alpha, out_alpha=""):
            n = self.node(unreal.MaterialExpressionLinearInterpolate)
            self.link(a, n, "A"); self.link(b, n, "B"); self.link(alpha, n, "Alpha", out_alpha)
            return n

        def lerpc(self, a_const, b_const, alpha, out_alpha=""):
            n = self.node(unreal.MaterialExpressionLinearInterpolate, const_a=a_const, const_b=b_const)
            self.link(alpha, n, "Alpha", out_alpha)
            return n

        def ramp(self, a, lo, hi, out=""):
            return self.sat(self.mul(self.add(a, -lo, out), 1.0 / (hi - lo)))

        def mask(self, a, r, g, b=False, alpha=False):
            n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=alpha); self.link(a, n); return n

        def append(self, a, b):
            n = self.node(unreal.MaterialExpressionAppendVector); self.link(a, n, "A"); self.link(b, n, "B"); return n

        def tex(self, texture, uv):
            n = self.node(unreal.MaterialExpressionTextureSample, texture=texture)
            self.link(uv, n, "UVs")
            return n

        def desat(self, a, amount):
            n = self.node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
            self.link(a, n, "")
            self.link(self.c(amount), n, "Fraction")
            return n

        def lum(self, a):
            return self.mask(self.desat(a, 1.0), True, False)

        def scalar(self, name, v):
            return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

        def vector(self, name, rgb):
            return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

        def out(self, a, prop, pin=""):
            lib.connect_material_property(a, pin, prop)
except ImportError:
    pass
