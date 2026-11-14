# TownLab (2026-11-14, a temporary test map, never cooked: Config/DefaultGame.ini): makes /Game/TownLab/TownLab (a copy of Field,
# like MoveLab: the game sees the map's name and builds the rebuilt Oak Hill beside the real one, TownLab.h) and the new town's
# ground material M_TL_Ground. Run by Tools/BuildTownLab.ps1 inside a windowless editor (this copy's editor closed).
#
# M_TL_Ground (the style sheet, Docs/TownLab/StyleSheet.md: Overwatch's Eichenwalde streets, Breath of the Wild's grass): one
# material for the whole town's ground, painting everything from the mesh's vertex colours (ATownLabTown::BuildGround):
#   R the street: irregular flagstone cobbles (Poly Haven "grassy_cobblestone", CC0, graded warm grey), grass in the joints, the outer stones
#     dropping out into packed earth and grass at a worn kerb
#   G worn earth: doorways, drip lines, the street's fringe
#   B a wall's foot: moss and damp, a deeper green
#   grass ground everywhere else, matching the meadow's tufts, with Breath of the Wild colour drift: sunny yellow-green swathes,
#   deeper green hollows and a few warm dry patches; far away every picture fades to its own average (no tiling, no shimmer)
# Textures: 2 new (the cobbles with their height in alpha, 2k; their normal map, 2k), the rest shared with M_Ground.
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "Art", "TownLab", "Textures", "Sources")
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

GRASS_TO_MEADOW = (1.139, 0.759, 0.358)   # (as world_material.py: T_GrassGround times this reads like the meadow's tufts)
GRASS_AVERAGE = (0.0705, 0.0916, 0.0083)
EARTH_BALANCE = (2.02, 2.79, 3.95)
EARTH_AVERAGE = (0.14, 0.10, 0.062)
COBBLE_GRADE = (0.92, 1.00, 1.12)          # Poly Haven "grassy_cobblestone" leans red-brown: toward the warm grey of the town's own stone
COBBLE_AVERAGE = (0.168, 0.138, 0.107)     # the graded picture's average (linear), what it fades to far away

# ------------------------------------------------------------------ the map
if EAL.does_asset_exist("/Game/TownLab/TownLab"):
    unreal.log("TOWNLABDIAG map already there")
elif EAL.duplicate_asset("/Game/Field", "/Game/TownLab/TownLab"):
    EAL.save_asset("/Game/TownLab/TownLab")
    unreal.log("TOWNLABDIAG map made")
else:
    unreal.log_error("TOWNLABDIAG could not copy Field")


def import_texture(path, name, normal=False, srgb=True):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", "/Game/TownLab")
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    tex = EAL.load_asset("/Game/TownLab/" + name)
    if normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    else:
        tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("max_texture_size", 2048)
    EAL.save_asset("/Game/TownLab/" + name)
    return tex


class Graph:
    def __init__(self, mat):
        self.mat = mat

    def node(self, cls, **props):
        n = lib.create_material_expression(self.mat, cls, 0, 0)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        if not lib.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("TOWNLABDIAG link failed: %s.%s -> %s.%s" % (a.get_class().get_name(), out, b.get_class().get_name(), pin))

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

    def one(self, cls, a, out=""):
        n = self.node(cls); self.link(a, n, "", out); return n

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

    def ramp(self, a, lo, hi, out=""):
        return self.sat(self.mul(self.add(a, -lo, out), 1.0 / (hi - lo)))

    def mask(self, a, r, g, b=False, alpha=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=alpha); self.link(a, n); return n

    def tex(self, texture, uv, normal=False):
        n = self.node(unreal.MaterialExpressionTextureSample, texture=texture)
        if normal:
            n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        self.link(uv, n, "UVs")
        return n

    def desat(self, a, amount):
        n = self.node(unreal.MaterialExpressionDesaturation, luminance_factors=unreal.LinearColor(0.3, 0.59, 0.11, 0.0))
        self.link(a, n, "")
        self.link(self.c(amount), n, "Fraction")
        return n

    def lum(self, a):
        return self.mask(self.desat(a, 1.0), True, False)

    def vector(self, name, rgb):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def out(self, a, prop, pin=""):
        lib.connect_material_property(a, pin, prop)


def build_ground(cobble, cobble_n, grass_tex, dirt_tex, patches):
    path = "/Game/TownLab/M_TL_Ground"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = tools.create_asset("M_TL_Ground", "/Game/TownLab", unreal.Material, unreal.MaterialFactoryNew())
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    xy = g.mask(wp, True, True)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    depth = g.node(unreal.MaterialExpressionPixelDepth)

    rag = g.mask(g.tex(patches, g.mul(xy, 1.0 / 170.0)), True, False)
    med = g.mask(g.tex(patches, g.mul(g.add(xy, 3100.0), 1.0 / 1100.0)), True, False)
    med2 = g.mask(g.tex(patches, g.mul(g.add(xy, -5300.0), 1.0 / 1730.0)), True, False)
    medm = g.mul(g.addn(med, med2), 0.5)
    patchy = g.mask(g.tex(patches, g.mul(xy, 1.0 / 4500.0)), True, False)
    big = g.mask(g.tex(patches, g.mul(g.add(xy, -7700.0), 1.0 / 26000.0)), True, False)
    dry_n = g.mask(g.tex(patches, g.mul(g.add(xy, 9100.0), 1.0 / 3300.0)), True, False)

    # Grass ground: as M_Ground's (so it meets the world's ground and the meadow's tufts), with stronger Breath of the Wild drift
    near_a = g.tex(grass_tex, g.mul(xy, 1.0 / 300.0))
    near_b = g.tex(grass_tex, g.mul(g.add(xy, 1777.0), 1.0 / 430.0))
    near = g.muln(g.lerp(near_a, near_b, g.ramp(medm, 0.42, 0.58)), g.c3(GRASS_TO_MEADOW))
    # past the grass blades (40 m) the ground carries the look: a broad sampling of the same picture keeps some texture out to
    # about 80 m before it settles to the average (from the air the plain average read flat next to the real town's meadow)
    mid = g.muln(g.tex(grass_tex, g.mul(g.add(xy, 4321.0), 1.0 / 1400.0)), g.c3(GRASS_TO_MEADOW))
    grass = g.lerp(near, mid, g.ramp(depth, 1500.0, 4500.0))
    grass = g.lerp(grass, g.c3(GRASS_AVERAGE), g.ramp(depth, 8000.0, 16000.0))
    grass = g.muln(grass, g.lerpc(0.90, 1.08, medm))
    grass = g.muln(grass, g.lerp(g.c3((0.72, 0.95, 0.80)), g.c3((1.17, 1.09, 0.74)), patchy))   # deeper green hollows to sunny yellow-green swathes
    grass = g.muln(grass, g.lerpc(0.84, 1.10, big))
    dry = g.mul(g.ramp(g.addn(dry_n, g.mul(g.add(rag, -0.5), 0.25)), 0.66, 0.82), 0.45)   # a few warm dry patches
    grass = g.lerp(grass, g.muln(grass, g.c3((1.45, 1.05, 0.55))), dry)
    grass = g.desat(g.muln(grass, g.vector("Tint", (1.12, 1.12, 1.08))), 0.3)   # (brighter: the grass blades catch more light than flat ground; the ground must match them where the blades stop)

    soil = g.muln(g.mask(g.tex(dirt_tex, g.mul(xy, 1.0 / 350.0)), True, True, True), g.c3(EARTH_BALANCE))
    soil = g.lerp(soil, g.c3(EARTH_AVERAGE), g.c(0.45))

    worn = g.mask(g.mask(vc, False, True), True, False)
    trampled = g.lerp(g.muln(grass, g.c3((1.07, 1.04, 0.93))), soil, g.c(0.22))
    t_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.7), g.mul(g.add(rag, -0.5), 0.45)), -0.32), 2.2))
    d_amt = g.sat(g.mul(g.add(g.addn(g.mul(worn, 1.45), g.mul(g.add(rag, -0.5), 1.3)), -1.0), 1.2))
    col = g.lerp(grass, trampled, t_amt)
    col = g.lerp(col, soil, d_amt)

    # A wall's foot: moss and damp, deeper and greener (the references' green creeping up every base)
    wall = g.mask(g.mask(vc, False, False, True), True, False)
    moss_amt = g.mul(g.muln(wall, g.ramp(g.addn(med, g.mul(rag, 0.5)), 0.35, 0.75)), 0.55)
    col = g.lerp(col, g.muln(col, g.c3((0.62, 0.86, 0.52))), moss_amt)

    # The street: mossy cobbles. Two samplings at unrelated sizes swapped in 10 m patches (no 2 m repeat lining up down a street).
    uva = g.mul(xy, 1.0 / 330.0)
    uvb = g.mul(g.add(xy, 913.0), 1.0 / 390.0)
    swap = g.ramp(medm, 0.49, 0.51)
    ca = g.tex(cobble, uva)
    cb = g.tex(cobble, uvb)
    ctex = g.lerp(ca, cb, swap)
    height = g.lerp(g.mul(ca, 1.0, "A"), g.mul(cb, 1.0, "A"), swap)
    stones = g.muln(g.mask(ctex, True, True, True), g.c3(COBBLE_GRADE))
    stones = g.desat(stones, 0.25)
    joint = g.one(unreal.MaterialExpressionOneMinus, g.ramp(height, 0.34, 0.46))
    stones = g.lerp(stones, g.muln(grass, g.c3((0.85, 1.0, 0.8))), g.mul(joint, 0.3))   # a little more of the meadow's green in the joints (the picture has grass in them already), the meadow's own green
    stones = g.lerp(stones, g.c3(COBBLE_AVERAGE), g.ramp(depth, 3000.0, 9000.0))
    street = g.mask(vc, True, False)
    # the outer stones drop out into earth at a worn kerb: each stone's height and brightness pick when it goes
    pick = g.addn(g.mul(height, 0.7), g.mul(g.lum(ctex), 0.9))
    t = g.addn(g.mul(street, 1.55), g.mul(g.add(rag, -0.5), 0.45))
    cob = g.sat(g.mul(g.add(g.addn(t, g.mul(pick, 0.6)), -1.05), 5.0))
    col = g.lerp(col, soil, g.mul(g.ramp(street, 0.1, 0.45), 0.35))   # packed earth under where the stones thin out
    col = g.lerp(col, stones, cob)

    flat = g.c3((0.0, 0.0, 1.0))
    na = g.mask(g.tex(cobble_n, uva, normal=True), True, True, True)
    nb = g.mask(g.tex(cobble_n, uvb, normal=True), True, True, True)
    bumps = g.lerp(na, nb, swap)
    bumps = g.lerp(bumps, flat, g.ramp(depth, 2500.0, 6000.0))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(flat, bumps, cob), unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerp(g.c(0.95), g.lerp(g.c(0.72), g.c(0.92), joint), cob), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.c(0.18), unreal.MaterialProperty.MP_SPECULAR)
    # the grass blades are lit through (two-sided foliage) and stay bright at the low sun; flat ground goes dark. SkyFill (set by
    # ATownLabTown from ADayNight::Dusk, jeeb.TownLab.GroundFill) adds that light back where the blades stop, so no dark plain
    g.out(g.muln(g.muln(col, g.c3((1.12, 0.97, 0.8))), g.scalar("SkyFill", 0.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    EAL.save_asset(path)
    unreal.log("TOWNLABDIAG built M_TL_Ground")


cob = import_texture(os.path.join(SRC, "T_TL_Cobble.png"), "T_TL_Cobble")
cob_n = import_texture(os.path.join(SRC, "T_TL_CobbleN.jpg"), "T_TL_CobbleN", normal=True)
build_ground(cob, cob_n, EAL.load_asset("/Game/T_GrassGround"), EAL.load_asset("/Game/T_Dirt"), EAL.load_asset("/Game/T_Patches"))
unreal.log("TOWNLABDIAG done")
