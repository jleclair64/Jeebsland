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

HOLLOW = (0.72, 0.95, 0.80)      # the 45 m patches: deeper green hollows ...
SUNNY = (1.17, 1.09, 0.74)       # ... to sunny yellow-green swathes
DRY = (1.45, 1.05, 0.55)         # sporadic warm dry patches, about 30 m across (Breath of the Wild's golden meadow patches)
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
    col = g.muln(col, g.lerp(g.c3(HOLLOW), g.c3(SUNNY), n["patchy"]))
    col = g.muln(col, g.lerpc(0.84, 1.10, n["big"]))
    col = g.muln(col, g.lerp(g.c3((0.97, 1.0, 1.02)), g.c3((1.02, 1.0, 0.95)), n["med2"]))
    dry = g.mul(g.ramp(g.addn(n["dry"], g.mul(g.add(n["rag"], -0.5), 0.25)), 0.66, 0.82), DRY_SHARE)
    return g.lerp(col, g.muln(col, g.c3(DRY)), dry)


def grey(g, col):
    return g.desat(col, GRASS_DESATURATION)


def ground_lift(g, col):
    """flat ground only (M_Ground, M_TL_Ground): never the blades"""
    return g.muln(col, g.vector("Lift", LIFT))


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
