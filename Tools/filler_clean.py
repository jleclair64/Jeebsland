# Oak Hill's filler props (barrels, crates, sacks, carts, pots, hay, bushes, walls, fences, lanterns...) from Meshy's free library
# (2026-11-12). Run by Tools\BuildFiller.ps1 in Blender; nothing here touches Unreal:
#   blender -b --factory-startup -P Tools/filler_clean.py -- [--debug] [Piece ...] | --survey Source ... | --sheets [family ...] | --pairs Piece ...
# Scratch pictures go to %TEMP%/jeeb_filler (or FILLER_TMP).
# In:  Art/Filler/Originals/<Source>.glb (Meshy GLBs: one PBR material, 100k-1M triangles; Art/Filler/Sources.md lists where each came from).
# Out: Art/Filler/Clean/<Piece>.glb, textures embedded (base colour and normal map as JPEG, metal/roughness), one line per piece:
#      FILLER <Piece> tris=<n> size=<x>x<y>x<z>m tex=<px>
# Frame of every result: real metres times WORLD (below), origin at the bottom centre, sitting on Z=0 in Blender (Z up). The GLB
# itself is +Y up as glTF always is (Blender's exporter turns it); Unreal's glTF import turns it back to Z up. Front = Blender -Y.
#
# Scale: the game draws the knight and the Oak Hill houses at 1.2x real size (HouseScale), so every piece is baked in at WORLD = 1.2
# times its real size: draw them at scale 1.0. A barrel "1.0 m" in the table is 1.2 m in the file; the FILLER line gives the file's size.
#
# What happens to a piece (each step's numbers are in the table or in DEFAULTS):
#  1. Split. The source is welded and broken into loose parts (connected components), then parts whose boxes touch are grouped
#     ("clusters": a pot and its handle, a cart and its wheels). --survey <Source> prints both lists, sorted biggest first, and draws
#     the clusters numbered (Docs/Filler/Survey/<Source>.jpg). A piece takes what its "take" rules say and leaves what "drop" says:
#       "all" | ("cluster", i, ...) | ("comp", i, ...) | ("near", x, y, z) the cluster nearest a point | ("box", (x0,y0,z0), (x1,y1,z1))
#       the FACES whose centre is inside a box (cuts a fused mesh; "fill" then closes the holes left by the cut).
#     Rules are in the source's own frame as Blender imports it (Z up, about -1..1). Specks under "speck" (share of the piece's
#     vertices, default 0.3%) are dropped. "moves": [(rule, (dx, dy, dz))] shifts parts first (the lid onto its crate).
#  2. Turn ("rot": degrees about X, Y, Z), size ("size": (axis, metres real): "Z" height, "L" longest side across, "X"/"Y"), and set on
#     the ground at its bottom centre.
#  3. Triangles ("tris"): a copy is collapse-decimated to the budget ("sym": a mirror axis to keep it even), smoothed by angle
#     ("sharp" degrees), gets a fresh Smart UV layout, and the full model's colour and normal detail are baked onto it (selected-to-
#     active, a cage of 1% of its size), plus the source's metal map if it has one and the low model's own ambient occlusion. Sources
#     under the budget keep their mesh ("subdiv": 1 smooths a low-poly one first).
#  4. Colour, on the baked picture ("tex": 1024 small/medium, 2048 large), in CIE Lab, after building_flatten.py (copied, not shared):
#     Meshy's painted-in shadow is lifted by the occlusion ("unshade"); a rolling guidance filter and an edge-preserving smoothing melt
#     AI speckle and smudges into clean patches (never across a UV island; "edge" is the step kept as a line, FLAT has the rest); a
#     softened share of the variation comes back as painted brush work; light and shade inside each surface is cut ("shade") and each
#     surface is moved towards its colour group ("even"). Then every colour group (k-means) is a material: an "anchors" entry
#     {(L, a, b): material} within 12 Lab units wins; a group darker than "gap_l" is a gap (seams, cracks) and takes the material
#     round it, staying dark; with a source metal map, metal groups are iron; otherwise the nearest look (LOOKS) among the piece's
#     "mats". "zones": [(rule, mats)] limits the materials by position (iron only on the band; burlap only for the sacks in the cart),
#     rules as in step 1. Each material is moved onto its palette colour (PALETTE): L' = target + "keepL" x (L - mean),
#     ab' = target + "keepC" x (ab - mean), so wood is one rich brown with only a touch of grain and no fade along it.
#     Material "keep" only softens (flowers, painted signs); "glow" goes to its own material slot named Glow (lantern glass, flames).
#  5. Metal/roughness from the materials (iron is a little metallic and smoother), and the baked normal map with its finest ripples
#     (AI noise) smoothed: "ndetail" of them kept. Materials are single-sided unless "two_sided" (cloth flaps, leaf cards).
#  --debug paints each piece in its materials' flat colours (Docs/Filler/Debug/<Piece>.jpg: wood brown, iron blue, burlap yellow,
#  clay orange, leaf green, stone grey, glow yellow, keep magenta), to check the sorting. --pairs draws the original above the result.
#
# The table: one row per piece.
#   src: source file name without .glb.  fam: family for the contact sheets (FAMILIES).  take / drop / moves / fill / speck: step 1.
#   rot, size: step 2.  tris, sym, sharp, subdiv: step 3.  tex, mats, zones, anchors, keepL, keepC, shade, even, unshade, gap_l and
#   any FLAT setting: step 4.  ndetail, two_sided: step 5.
import bpy, bmesh, sys, os, math, time, json
import numpy as np
from mathutils import Vector, Matrix, Euler

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIG = os.path.join(PROJ, "Art", "Filler", "Originals")
CLEAN = os.path.join(PROJ, "Art", "Filler", "Clean")
DOCS = os.path.join(PROJ, "Docs", "Filler")
WORLD = 1.2      # the game's 1.2x (HouseScale and the knight): baked into every file

# ---------------------------------------------------------------------------------------------------------------------------------
# The pieces
# ---------------------------------------------------------------------------------------------------------------------------------
WOOD_IRON = ["wood", "iron"]
CANDLE_WAX = {(79, 8, 24): "glow", (79, 9, 32): "glow", (64, 7, 17): "glow"}    # (the wire lanterns' candles)
MOSS = {"leaf": (46.0, -17.0, 24.0)}     # (moss on stone: a calmer, deeper shade of the leaf green, not lime)
# (torch flames: the bright yellow-orange inside the head, for taking the flame off the body as its own piece)
TORCH_FLAME = [("box", (-0.2, -0.2, 0.52), (0.2, 0.2, 1.1)), ("and", ("box", (-0.16, -0.16, 0.3), (0.16, 0.16, 0.52)), ("lab", (35, -10, 28), (100, 90, 130)))]
WALL_FLAME = [("box", (-0.2, -0.5, 0.72), (0.2, 0.1, 1.1)), ("and", ("box", (-0.2, -0.5, 0.4), (0.2, 0.1, 0.72)), ("lab", (35, -10, 28), (100, 90, 130)))]
TOON_LOGS = {"bark": (42.0, 12.0, 22.0), "woodcut": (70.0, 11.0, 40.0)}     # (the chopping block's cartoon logs: lighter bark)
# (the spice heaps inside the five sacks' mouths keep their own colours)
SPICE_ZONES = [([("box", (x - 0.12, -0.12, 0.1), (x + 0.12, 0.12, 0.5)) for x in (-0.8, -0.4, 0.0, 0.4, 0.79)], ["keep"])]
PIECES = {
    # storage
    "Barrel":        dict(src="Barrel_leeshane", fam="storage", size=("Z", 0.95), tris=12000, mats=WOOD_IRON, sharp=60),
    "CrateOpen":     dict(src="CrateBand_15756568954", fam="storage", size=("Z", 0.70), tris=14000, mats=["wood"], sharp=40,
                          zones=[([("box", (-1, -1.1, 0.0), (-0.6, 1.1, 0.45)), ("box", (0.58, -1.1, 0.0), (1, 1.1, 0.45)),
                                   ("box", (-1, -1.1, 0.0), (1, -0.92, 0.45)), ("box", (-1, 0.92, 0.0), (1, 1.1, 0.45))], WOOD_IRON)]),   # (iron only on the band and rings)
    "Crate":         dict(src="CrateLid_japaninazuma000", fam="storage", moves=[(("comp", 1), (0.0, 0.0, -0.715))], size=("Z", 0.72),
                          tris=12000, mats=WOOD_IRON, sharp=40),
    "CrateNoLid":    dict(src="CrateLid_japaninazuma000", fam="storage", take=[("comp", 0)], size=("Z", 0.62), tris=12000, mats=WOOD_IRON, sharp=40),
    "GrainSack":     dict(src="GrainSack_rafael", fam="storage", size=("Z", 0.72), tris=4000, subdiv=1, mats=["burlap", "grain"], sharp=80),
    # the cart scene: the cart with its two sacks, the loose sacks, barrels and the jug round it (fused into one mesh where they touch)
    "HandCart":      dict(src="CartSacks_eromaxi", fam="farm", drop=[("comp", 1, 2), ("box", (-0.83, 0.2, -0.6), (-0.40, 0.65, 0.0)),
                          ("box", (-0.5, -0.8, -0.6), (0.25, -0.09, 0.0)), ("box", (-0.45, -0.12, -0.6), (0.1, 0.0, -0.14))], fill=40, speck=0.03, size=("L", 2.6),
                          tris=30000, tex=2048, mats=["wood"],
                          zones=[(("box", (0.15, 0.0, 0.07), (0.78, 0.55, 0.6)), ["burlap"])], sharp=45),
    "BarrelSmall":   dict(src="CartSacks_eromaxi", fam="storage", take=[("comp", 1)], size=("Z", 0.85), tris=10000, mats=WOOD_IRON, sharp=60),
    "ClayJug":       dict(src="CartSacks_eromaxi", fam="pots", take=[("comp", 2)], size=("Z", 0.4), tris=6000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),   # (its painted stripes melted away: one clay)
    "SackPileOpen":  dict(src="CartSacks_eromaxi", fam="storage", take=[("box", (-0.5, -0.8, -0.6), (0.2, -0.12, 0.0))], drop=[("comp", 1, 2)], fill=40, speck=0.03, two_sided=True,
                          size=("L", 1.45), tris=16000, mats=["burlap", "grain"], sharp=80),
    "WheatSack":     dict(src="WheatSack_VetRo5o3", fam="storage", size=("Z", 0.85), tris=6000, mats=["burlap"], sharp=80, plain=["all"], plain_mat="burlap"),   # (its AI lettering and wheat print cleared)
    "SpiceSacks":    dict(src="SpiceSacks_gtarcan", fam="storage", size=("Z", 0.55), tris=16000, mats=["burlap"], sharp=80, zones=SPICE_ZONES),
    "SackWall":      dict(src="SackWall_Nighthawk666", fam="storage", size=("Z", 0.7), tris=20000, tex=2048, mats=["burlap"], sharp=80),
    "SackStack":     dict(src="SackPyramid_theanh75", fam="storage", size=("Z", 0.7), tris=20000, tex=2048, mats=["burlap"], sharp=80),
    # firewood, logs and rope
    "Firewood":      dict(src="Firewood_15756568954", fam="firewood", size=("L", 1.0), tris=12000, mats=["bark", "woodcut"], sharp=40),
    "LogPile":       dict(src="LogPile_HighTide", fam="firewood", size=("L", 2.4), tris=30000, tex=2048, mats=["bark", "woodcut"], sharp=45),
    "LogStack":      dict(src="LogStack_Jusin_Cloud", fam="firewood", size=("L", 2.0), tris=16000, mats=["bark", "woodcut"], sharp=45, looks={"woodcut": (57.0, 7.0, 22.0)}),
    "FirewoodRack":  dict(src="FirewoodWall_idontreallycare", fam="firewood", size=("L", 2.4), tris=30000, tex=2048, mats=["bark", "woodcut"], sharp=40),
    "RopeCoil":      dict(src="CoiledRope_theanh75", fam="firewood", size=("L", 0.6), tris=8000, mats=["rope"], sharp=80),
    "RopeHank":      dict(src="KnottedRope_HackerYacker", fam="firewood", size=("L", 0.7), tris=8000, mats=["rope"], sharp=80),
    # pots (single pots from one group; one scale for all: the amphora 0.8 m)
    "ClayPitcher":   dict(src="Jug_mmedelescobes", fam="pots", size=("Z", 0.5), tris=7100, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    "PotAmphora":    dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 0)], size=("Z", 0.8), tris=8000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    "PotJug":        dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 4)], size=("Z", 0.54), tris=6000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    "PotWide":       dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 2)], size=("Z", 0.37), tris=6000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    "PotRound":      dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 3)], size=("Z", 0.34), tris=5000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    "PotVase":       dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 9)], size=("Z", 0.39), tris=5000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    # (the cauldron's source pot is grey-green clay on a wicker stand over stones: the pot is painted dark iron, the stand wood)
    "Cauldron":      dict(src="Cauldron_bjlin", fam="pots", drop=[("box", (0.33, -1, -1), (1, -0.25, -0.1)), ("box", (-0.05, -1, -1), (0.33, -0.6, -0.25))],
                          speck=0.002, size=("Z", 0.95), tris=24000, tex=2048, mats=["stone", "wood"], sharp=50,
                          zones=[(("box", (-1, -1, -0.45), (1, 1, 1)), ["iron", "wood"])], looks={"iron": (48.0, 5.0, 25.0), "stone": (46.0, 5.0, 23.0), "wood": (50.0, 27.0, 45.0)}),
    "Basket":        dict(src="Cauldron_bjlin", fam="pots", take=[("box", (0.33, -1, -1), (1, -0.25, -0.1))], speck=0.01, size=("Z", 0.5), tris=8000,
                          mats=["wood"], targets={"wood": (47.0, 12.0, 26.0)}, sharp=50),
    "PotSmall":      dict(src="Pots_ProteinPenguin", fam="pots", take=[("comp", 10)], size=("Z", 0.23), tris=4000, mats=["clay"], sharp=80, keepL=0.5, keepC=0.2, edge=30.0, even=0.7),
    # farm
    "HayBaleRound":  dict(src="HayBaleRound_pasaulusvitautus", fam="farm", size=("Z", 1.35), remesh=0.012, tris=12000, tex=2048, mats=["straw"], sharp=80, gap_l=10.0, keepL=0.6),
    "HayBale":       dict(src="HayBaleSquare_hkmatay", fam="farm", size=("L", 1.0), remesh=0.012, tris=10000, mats=["straw", "rope"], sharp=60, gap_l=10.0, keepL=0.6),
    "HayWagon":      dict(src="HayWagon_epix.asmr", fam="farm", size=("L", 2.6), tris=30000, tex=2048, mats=["wood"], sharp=45, gap_l=10.0, keepL=0.55,
                          zones=[(("box", (-1, -1, 0.24), (1, 1, 1)), ["straw"])]),
    "ThatchWagon":   dict(src="CoveredWagon_3DMinisByChad", fam="farm", size=("L", 2.6), tris=30000, tex=2048, mats=["wood"], sharp=45, gap_l=10.0, keepL=0.8,
                          zones=[(("box", (-1, -1.1, 0.42), (1, 1.1, 1)), ["straw"])]),
    "WateringCan":   dict(src="WateringCan_giacomopreziosi647", fam="farm", size=("Z", 0.42), tris=9000, mats=["iron"], sharp=50),
    "WoodTub":       dict(src="WoodBucket_8aanliegen", fam="farm", size=("L", 0.6), remesh=0.006, tris=10000, mats=WOOD_IRON, sharp=60),
    "Pail":          dict(src="MetalBucket_wyndala", fam="farm", size=("Z", 0.45), tris=7000, subdiv=1, recap=True, mats=WOOD_IRON, sharp=60, looks={"iron": (68.0, 0.0, -1.0)}),
    "ChickenCoop":   dict(src="ChickenCoop_arq.thompson", fam="farm", size=("Z", 1.5), tris=20000, tex=2048, mats=["wood", "bark"], sharp=40),
    "NestingBoxes":  dict(src="NestingBoxes_arq.thompson", fam="farm", size=("Z", 1.2), tris=16000, mats=["wood", "straw"], sharp=40, gap_l=10.0, keepL=0.45, shade=0.8),
    # garden and yard
    "RaisedBed":     dict(src="RaisedBed_EvanieStarchild", fam="garden", size=("L", 2.0), tris=20000, tex=2048, mats=["wood", "soil", "leaf"], sharp=45, two_sided=True),
    "TulipBed":      dict(src="TulipBed_daiki2007september", fam="garden", size=("L", 1.8), tris=20000, tex=2048, mats=["wood", "soil", "keep"], sharp=45, two_sided=True),
    "CabbageCrate":  dict(src="CabbageCrate_bigbro2236", fam="garden", size=("L", 0.8), remesh=0.008, tris=16000, mats=["wood", "leaf"], sharp=45,
                          zones=[([("box", (-1, -1, -1), (-0.8, 1, 0.05)), ("box", (0.8, -1, -1), (1, 1, 0.05)), ("box", (-1, -1, -1), (1, -0.66, 0.05)), ("box", (-1, 0.64, -1), (1, 1, 0.05)), ("box", (-1, -1, -1), (1, 1, -0.4))], ["wood"])],
                          plain=[("box", (-1, -1, -1), (-0.8, 1, 0.05)), ("box", (0.8, -1, -1), (1, 1, 0.05)), ("box", (-1, -1, -1), (1, -0.66, 0.05)), ("box", (-1, 0.64, -1), (1, 1, 0.05))]),
    "VegCart":       dict(src="VegCart_SandersonArts", fam="garden", size=("L", 2.2), tris=30000, tex=2048, mats=["wood", "iron", "leaf"], sharp=45,
                          zones=[(("box", (-1, -1, 0.42), (1, 1, 1)), ["straw"])]),
    "Scarecrow":     dict(src="Scarecrow_arq.thompson", fam="garden", size=("Z", 2.0), tris=16000, tex=2048, mats=["wood", "burlap", "iron", "keep"], sharp=45, two_sided=True),
    "ButcherBlock":  dict(src="Stool_aafir.ahmed.yahya", fam="garden", size=("Z", 0.75), tris=8000, mats=["wood"], keepC=0.1, sharp=40),
    "ChoppingBlock": dict(src="ChoppingBlock_eungyukm", fam="garden", take=[("box", (0.06, -0.7, -0.7), (1.05, 0.7, 0.7))], fill=60, size=("Z", 1.2), tris=8000, subdiv=1,
                          mats=["bark", "woodcut"], zones=[(("box", (0.1, -0.5, -0.13), (1.05, 0.7, 0.7)), WOOD_IRON)], sharp=40, looks=TOON_LOGS),
    "LogStackSmall": dict(src="ChoppingBlock_eungyukm", fam="firewood", take=[("box", (-1.05, -0.7, -0.7), (0.06, 0.7, 0.7))], drop=[("comp", 1)], fill=60,
                          size=("Z", 0.75), tris=6000, mats=["bark", "woodcut"], sharp=40, looks=TOON_LOGS),
    "Stump":         dict(src="Stump_ktmarine1999", fam="garden", size=("L", 1.1), tris=12000, mats=["bark", "woodcut", "leaf"], sharp=45),
    # bushes (leaf greens on the palette, each a little its own shade)
    "HedgeBox":      dict(src="HedgeBox_idan.shmueli.sk", fam="bushes", size=("Z", 1.0), tris=20000, tex=2048, mats=["leaf"], sharp=80, targets={"leaf": (54.0, -24.0, 32.0)}),
    "BushLeafy":     dict(src="BushTrunk_Tansy", fam="bushes", size=("Z", 1.0), remesh=0.015, tris=12000, mats=["leaf"], sharp=80),
    "Cypress":       dict(src="Cypress_dra86", fam="bushes", size=("Z", 2.6), remesh=0.015, tris=12000, tex=2048, mats=["leaf"], sharp=80, targets={"leaf": (50.0, -22.0, 28.0)}),
    "BushBerries":   dict(src="BushBerries_ggsellivanovich", fam="bushes", size=("Z", 1.0), remesh=0.015, tris=12000, mats=["leaf", "keep"], sharp=80),
    "HedgeSegment":  dict(src="HedgeLong_fabrizio.ceramponi", fam="bushes", remesh=0.01, tile=("X", -0.6, 0.6), size=("Z", 0.95), tris=14000, mats=["leaf"], sharp=80,
                          targets={"leaf": (54.0, -24.0, 32.0)}),
    "HedgeEnd":      dict(src="HedgeLong_fabrizio.ceramponi", fam="bushes", remesh=0.01, tile=("X", 0.2, None), size=("Z", 0.95), tris=10000, mats=["leaf"], sharp=80,
                          targets={"leaf": (54.0, -24.0, 32.0)}),
    "BushRound":     dict(src="BushRound_Tansy", fam="bushes", size=("Z", 1.0), remesh=0.015, tris=12000, mats=["leaf"], sharp=80, targets={"leaf": (58.0, -22.0, 38.0)}),
    "BushWhiteFlowers": dict(src="BushWhiteFlowers_axx.s", fam="bushes", size=("Z", 0.9), remesh=0.015, tris=12000, mats=["leaf", "keep"], sharp=80),
    "BushYellowFlowers": dict(src="BushYellowFlowers_TooPH", fam="bushes", size=("Z", 0.9), remesh=0.015, tris=12000, mats=["leaf", "bark", "keep"], sharp=80),
    "BushBall":      dict(src="BushLeafy_Tansy", fam="bushes", size=("Z", 1.0), remesh=0.015, tris=12000, mats=["leaf"], sharp=80, targets={"leaf": (55.0, -24.0, 34.0)}),
    "BushSpiky":     dict(src="BushSpiky_rennan.alecrim", fam="bushes", size=("Z", 0.85), tris=12000, mats=["leaf"], sharp=80, two_sided=True),
    "BushDome":      dict(src="Bush01_idan.shmueli.sk", fam="bushes", size=("Z", 1.1), remesh=0.015, tris=12000, mats=["leaf"], sharp=80, targets={"leaf": (54.0, -22.0, 36.0)}),
    "ShrubLow":      dict(src="ShrubLow_LWN_STUDIO", fam="bushes", size=("Z", 0.6), remesh=0.012, tris=12000, mats=["leaf"], sharp=80, targets={"leaf": (52.0, -24.0, 30.0)},
                          looks={"leaf": (40.0, -20.0, 0.0)}),
    "PlantClump":    dict(src="PlantClump_wi.sdomzcd", fam="bushes", size=("L", 1.2), remesh=0.008, tris=14000, mats=["leaf"], sharp=80, keepC=0.5),
    # planters and flower boxes (flowers and their leaves keep their own colours; window boxes about 1 m, plaza planters 1.6 m)
    # dropped (photo-like petals bake into dark shards at any triangle count): "FlowerBoxSunflower": dict(src="FlowerBoxSunflower_Axinovium", fam="planters", size=("L", 1.0), tris=30000, mats=["wood", "keep"], zones=[(("box", (-1, -1, -0.03), (1, 1, 1)), ["keep"])], sharp=45, two_sided=True),
    "FlowerBoxIvy":  dict(src="FlowerBoxIvy_greycharming", fam="planters", size=("L", 1.0), tris=16000, mats=["wood", "keep"], sharp=45, two_sided=True),
    "FlowerBoxLong": dict(src="FlowerBoxLong_Axinovium", fam="planters", size=("L", 1.1), tris=30000, mats=["wood", "keep"], sharp=45, two_sided=True),
    # dropped (its photo-like petunias bake into dark purple shards): "FlowerBoxClay": dict(src="FlowerBoxTerracotta_heymisscici", fam="planters", take=[("box", (-1, -1, -1), (-0.02, 1, 1))], size=("L", 0.9), tris=16000,
    #                     mats=["clay", "keep"], sharp=45, two_sided=True),
    "WindowBoxRed":  dict(src="WindowBoxGeranium_4tcw7vw7d6", fam="planters", size=("L", 0.9), tris=12000, mats=["cloth_red", "keep"], sharp=45, two_sided=True,
                          zones=[(("box", (-1, -1, 0.0), (1, 1, 1)), ["keep"])]),
    "PlanterGilded": dict(src="PlanterGilded_cadillaceldoradocb86", fam="planters", size=("L", 1.6), tris=20000, tex=2048, mats=["stone", "keep"], sharp=45, two_sided=True,
                          zones=[(("box", (-1, -1, 0.15), (1, 1, 1)), ["keep"])], looks={"stone": (70.0, 4.0, 30.0)}),
    # dropped (a low, faceted scan of stone blocks; the moss smears over the faces): "PlanterStone":  dict(src="PlanterStone_narcis.lascuw", fam="planters", size=("L", 1.6), tris=16000, tex=2048, mats=["stone", "moss", "keep"], sharp=45, two_sided=True,
    #                     zones=[(("box", (-1, -1, -1), (1, 1, 0.4)), ["stone", "moss"])]),
    # rocks and boulders (the buildings' stone; moss on the palette)
    "BoulderMossy":  dict(src="BoulderMossy_thesavagebear555", fam="rocks", size=("L", 2.0), tris=16000, tex=2048, mats=["stone", "moss"], sharp=50),
    # dropped (the grass blades will not decimate (74k after three passes) and bake dark; the rocks facet): "RocksGrass":    dict(src="RocksGrass_yasmymvbadachu", fam="rocks", size=("L", 1.6), tris=20000, tex=2048, mats=["stone", "leaf"], sharp=50, two_sided=True),
    "BoulderScan":   dict(src="BoulderScan_bk2sanity", fam="rocks", size=("L", 1.8), tris=16000, tex=2048, mats=["stone", "moss"], sharp=50),
    "RockTall":      dict(src="RockTall_axx.s", fam="rocks", size=("Z", 2.4), tris=5200, mats=["stone", "moss"], sharp=40),
    "RockCluster":   dict(src="RockCluster_somany", fam="rocks", drop=[("comp", 1)], size=("L", 2.4), tris=20000, tex=2048, mats=["stone", "leaf"], sharp=50, two_sided=True),
    "RockPale":      dict(src="RockPale_Zahra", fam="rocks", size=("L", 1.6), tris=3000, mats=["stone"], keepL=0.45, sharp=40),
    "RocksForest":   dict(src="RocksForest_ethanjones333", fam="rocks", size=("L", 2.2), tris=20000, tex=2048, mats=["stone", "moss"], sharp=50),
    # fallen wood (bark and cut faces on the log palette, moss on the palette)
    "LogMossy":      dict(src="LogMossy_Natural-Ai", fam="logs", size=("L", 2.4), tris=16000, tex=2048, mats=["bark", "woodcut", "moss"], sharp=45),
    "TreeSnag":      dict(src="StumpFallen_lilu_1", fam="logs", drop=[("comp", 2, 3)], size=("L", 3.0), tris=16000, tex=2048, mats=["bark", "woodcut", "moss"], sharp=45),
    "LogPine":       dict(src="LogPine_Nope", fam="logs", size=("L", 3.0), tris=12000, tex=2048, mats=["bark", "woodcut"], sharp=45),
    "LogCartoon":    dict(src="Driftwood_nhaidwin343", fam="logs", size=("L", 2.0), tris=10000, mats=["bark", "woodcut", "moss"], sharp=45,
                          looks={"bark": (50.0, 22.0, 40.0), "woodcut": (68.0, 12.0, 48.0)}),
    "LogHollow":     dict(src="LogHollow_minjooo1023", fam="logs", size=("L", 2.6), tris=16000, tex=2048, mats=["bark", "woodcut", "leaf"], sharp=45, two_sided=True),
    "LogRoots":      dict(src="DriftRoot_RaBBiT_Studios", fam="logs", size=("L", 2.8), tris=16000, tex=2048, mats=["bark", "woodcut"], sharp=45),
    # stumps (with the garden's Stump)
    "StumpGrass":    dict(src="StumpGrass_UltrasombrA", fam="stumps", size=("L", 1.2), tris=12000, mats=["bark", "woodcut", "leaf", "keep"], sharp=45),
    "StumpStones":   dict(src="StumpStones_theanh75", fam="stumps", size=("L", 1.4), tris=16000, tex=2048, mats=["bark", "woodcut", "leaf", "stone"], sharp=45, two_sided=True),
    "StumpRooty":    dict(src="StumpRooty_Osmoze", fam="stumps", size=("Z", 1.2), tris=16000, tex=2048, mats=["bark", "woodcut", "moss"], sharp=45),
    "StumpMushroom": dict(src="StumpMushroom_vasilioglo_2000", fam="stumps", size=("L", 1.4), tris=16000, tex=2048, mats=["bark", "woodcut", "moss", "keep"], sharp=45),
    "StumpPainted":  dict(src="StumpPainted_prostonilbine", fam="stumps", size=("L", 1.2), tris=12000, mats=["bark", "woodcut", "moss"], sharp=45),
    # walls (tile: the run's axis and its two square cuts, see tile_cut; segment lengths in the FILLER line and the report)
    "WallStone":     dict(src="WallClean_ktmarine1999", fam="walls", tile=("X", -0.95, 0.95), size=("Z", 1.0), tris=12000, mats=["stone"], sharp=40),
    "WallStoneEnd":  dict(src="WallClean_ktmarine1999", fam="walls", tile=("X", None, -0.55), size=("Z", 1.0), tris=5000, mats=["stone"], sharp=40),
    "WallPillar":    dict(src="WallPillars_thayatomeeru", fam="walls", take=[("box", (-1.1, -0.2, -0.47), (1.1, 0.2, 0.7))], fill=40,
                          tile=("X", -0.885, 0.885), size=("Z", 1.2), tris=16000, mats=["stone"], sharp=40),
    "WallPillarEnd": dict(src="WallPillars_thayatomeeru", fam="walls", take=[("box", (-1.1, -0.2, -0.47), (1.1, 0.2, 0.7))], fill=40,
                          tile=("X", None, -0.70), size=("Z", 1.2), tris=6000, mats=["stone"], sharp=40),
    # dropped (the moss decimates into jagged shards): "WallMossy":     dict(src="WallMossy_Chris", fam="walls", tile=("X", -0.97, 0.97), size=("Z", 1.2), tris=16000, mats=["stone", "leaf"], sharp=40),
    # dropped (far too much moss, reads as a hedge): "WallGreen":     dict(src="WallGreen_hogemin", fam="walls", tile=("X", -0.93, 0.93), size=("Z", 0.9), tris=9000, mats=["stone", "leaf"], sharp=40),
    # fences
    "FencePicket":   dict(src="FencePicket_ub3rv3r7", fam="walls", tile=("X", -0.985, 0.985), size=("Z", 0.95), tris=8000, mats=["wood"], sharp=40),
    "FencePicketEnd": dict(src="FencePicket_ub3rv3r7", fam="walls", tile=("X", None, -0.80), size=("Z", 0.95), tris=3000, mats=["wood"], sharp=40),
    "FenceStick":    dict(src="FenceStick_thumbstarfpv", fam="walls", tile=("X", -0.838, 0.831), size=("Z", 1.1), tris=14000, mats=["wood", "rope"], sharp=40),
    "FenceStickPost": dict(src="FenceStick_thumbstarfpv", fam="walls", tile=("X", -0.93, -0.745), size=("Z", 1.13), tris=3000, mats=["wood", "rope"], sharp=40),
    # dropped (dark weathered rails with black streaks: dirty, off the style): "FenceRail":     dict(src="FenceRailA_dhinesh18x", fam="walls", tile=("X", -0.426, 0.431), size=("Z", 1.1), tris=6000, mats=["wood", "iron"], sharp=40),
    "FencePalisade": dict(src="FencePointed_gregory.crost", fam="walls", tile=("X", -0.842, 0.845), size=("Z", 1.6), tris=20000, mats=["wood"], sharp=40),
    # stairs (front = the bottom step, Blender -Y; rise 0.17 m a step)
    "StairsStone":   dict(src="StairsLight_axx.s", fam="walls", size=("Z", 1.19), tris=16000, mats=["stone", "leaf"], sharp=40, targets=MOSS),
    "StairsStoneShort": dict(src="StairsLight_axx.s", fam="walls", tile=("Y", None, -0.19), size=("X", 1.367), tris=8000, mats=["stone", "leaf"], sharp=40, targets=MOSS),
    "StairsWalled":  dict(src="StairsWalls_axx.s", fam="walls", size=("Z", 1.36), tris=20000, mats=["stone", "leaf"], sharp=40, targets=MOSS),
    # dropped (too steep and tall for Oak Hill's small pad steps): "StairsSteep":   dict(src="StairsGrey_eromaxi", fam="walls", size=("Z", 2.15), tris=20000, mats=["stone", "leaf"], sharp=40),
    # furniture
    # dropped (reads as a modern park bench): "BenchPark":     dict(src="BenchPark_pokemonpikaaaa", fam="furniture", size=("Z", 0.85), tris=12000, mats=WOOD_IRON, sharp=40),
    "BenchCarved":   dict(src="BenchArms_EvanieStarchild", fam="furniture", rot=(0, 0, -90), size=("Z", 1.05), tris=14000, mats=["wood", "leaf"], sharp=40, targets=MOSS),
    "TrestleTable":  dict(src="TrestleTable_Ganda", fam="furniture", size=("Z", 0.78), tris=10000, mats=["wood"], sharp=40),
    "BenchStone":    dict(src="BenchStone_bear4969", fam="furniture", size=("Z", 0.47), tris=8000, mats=["stone", "leaf"], keepL=0.45, keepC=0.2, sharp=40, targets=MOSS),
    "SignSkillIssue": dict(src="SignSkillIssue_RaBBiT_Studios", fam="furniture", size=("Z", 1.8), tris=8000, mats=["wood", "plaster", "iron"], sharp=40),
    "Signpost":      dict(src="SignpostTwo_eungyukm", fam="furniture", size=("Z", 2.0), tris=8000, mats=["wood"], sharp=40,
                          plain=[("box", (-1, -1, 0.2), (1, 1, 1))]),
    "SignArrow":     dict(src="SignArrowSingle_Khufu", fam="furniture", size=("Z", 2.0), tris=6000, mats=["wood"], sharp=40,
                          plain=[("box", (-1, -1, 0.45), (1, 1, 0.75))]),
    # lights (glass and flames in the Glow slot)
    "LanternBlack":  dict(src="LanternBlack_GoodWork", fam="lights", size=("Z", 0.5), tris=6000, mats=["iron", "glow"], sharp=40),
    # dropped (no glass or flame, a duplicate of LanternBlack): "LanternFrame":  dict(src="LanternQuartet_sevenevesai", fam="lights", take=[("comp", 1)], size=("Z", 0.45), tris=6000, mats=["iron"], sharp=40),
    # dropped (a paper lantern reads East Asian, not Oak Hill): "PaperLantern":  dict(src="PaperLanterns_Raven", fam="lights", take=[("cluster", 1)], size=("Z", 0.85), tris=4000, mats=["glow", "wood", "iron"], sharp=60),
    "LanternOil":    dict(src="LanternOil_kasulatarun42", fam="lights", size=("Z", 0.42), tris=4000, mats=["iron", "glow"], sharp=40),
    "CandleCageTall": dict(src="WireLanterns_meggie_lou_0815", fam="lights", take=[("cluster", 0)], speck=0.0005, size=("Z", 0.5), tris=20000,
                          mats=["iron", "wood"], anchors={(27, 9, 14): "iron"}, sharp=40,
                          zones=[(("box", (0.22, -0.28, -0.63), (0.78, 0.28, -0.18)), ["iron", "wood", "glow"], CANDLE_WAX)]),
    "CandleCageShort": dict(src="WireLanterns_meggie_lou_0815", fam="lights", take=[("cluster", 1)], speck=0.0005, size=("Z", 0.38), tris=20000,
                          mats=["iron", "wood"], anchors={(27, 9, 14): "iron"}, sharp=40,
                          zones=[(("box", (-0.78, -0.28, -0.63), (-0.22, 0.28, -0.18)), ["iron", "wood", "glow"], CANDLE_WAX)]),
    # (lamp posts: the groups' Lab from a first run pinned to their parts; the lantern zone has its own: frame iron, glass glow)
    "LampPostStone": dict(src="LampPostStone_Khufu", fam="lights", size=("Z", 2.4), tris=16000, mats=["wood", "iron", "stone", "leaf"], sharp=40, targets=MOSS,
                          anchors={(36, 6, 11): "wood", (26, 5, 8): "wood", (35, -2, -4): "iron", (33, -1, 5): "iron", (40, -1, 12): "stone",
                                   (58, -2, 4): "stone", (31, -9, 29): "leaf", (88, 0, 16): "stone"},
                          zones=[(("box", (-0.36, -0.4, 0.1), (-0.07, 0.4, 0.55)), ["iron", "glow"],
                                  {(36, 6, 11): "iron", (26, 5, 8): "iron", (58, -2, 4): "iron", (40, -1, 12): "iron", (31, -9, 29): "iron", (88, 0, 16): "glow"})]),
    "LampPostRope":  dict(src="LampPostRope_meishancui", fam="lights", size=("Z", 2.4), tris=20000, mats=["wood", "rope", "stone", "leaf"], sharp=40,
                          anchors={(30, 3, 17): "wood", (38, 4, 21): "wood", (35, 1, 8): "stone", (25, 0, -9): "rope", (43, -5, 29): "leaf",
                                   (52, 4, 25): "wood", (68, 0, 10): "stone", (68, -11, 44): "leaf"},
                          zones=[(("box", (0.2, -0.4, -0.15), (0.6, 0.4, 0.48)), ["iron", "glow"],
                                  {(30, 3, 17): "iron", (38, 4, 21): "iron", (35, 1, 8): "iron", (25, 0, -9): "iron", (43, -5, 29): "iron",
                                   (52, 4, 25): "glow", (68, 0, 10): "glow", (68, -11, 44): "iron"})]),
    "LampPostPillar": dict(src="LampPostPillar_adiaseptic", fam="lights", size=("Z", 2.6), tris=16000, mats=["wood", "iron", "stone"], sharp=40,
                          zones=[(("box", (-0.4, -0.3, 0.3), (-0.08, 0.3, 0.72)), ["iron", "glow"],
                                  {(32, 8, 16): "iron", (38, 8, 21): "iron", (29, 5, 12): "iron", (49, 9, 31): "glow", (79, 2, 55): "glow"}),
                                 (("box", (-1, -1, -1), (1, 1, -0.43)), ["stone"])]),
    "LampPostGallows": dict(src="LampPostGallows_farialtaf", fam="lights", size=("Z", 2.5), tris=10000, mats=["wood"], sharp=40,
                          zones=[(("box", (-0.75, -0.3, -0.1), (-0.25, 0.3, 0.62)), ["wood", "glow"], {(58, -1, 0): "glow"})]),
    "AwningStriped": dict(src="AwningBlue_rafael", fam="lights", size=("L", 2.4), tris=8000, subdiv=1, mats=["cloth_blue", "plaster", "wood"],
                          two_sided=True, sharp=80),
    # dropped (blotchy dirty cloth on one odd pole): "CanopyCloth":   dict(src="CanopyOrange_epix.asmr", fam="lights", size=("L", 3.2), tris=10000, mats=["cloth_red", "iron", "wood"], two_sided=True, sharp=60),
    # --- market stalls (small booths; cloth in its own two-sided Cloth slot) ---
    "StallGreen":    dict(src="MarketStallGreen_jm9500050", fam="stalls", size=("Z", 2.4), tris=20000, tex=2048, mats=["wood", "cloth_green", "cloth_cream"],
                          cloth=["cloth_green", "cloth_cream"], two_sided=True, sharp=45),
    "StallRedTent":  dict(src="MarketTentRed_rafael.marinsbolado", fam="stalls", size=("Z", 2.4), tris=8000, subdiv=1, mats=["cloth_crimson", "cloth_cream", "wood"],
                          cloth=["cloth_crimson", "cloth_cream"], two_sided=True, sharp=80),
    # --- banners: the cloth alone (rod cut off), origin at its top edge's middle, all of it in the Cloth slot ---
    "BannerGriffin": dict(src="BannerGriffin_Warface", fam="banners", take=[("box", (-1, -1, -1.1), (1, 1, 0.545))], size=("Z", 1.5), tris=3000,
                          mats=["cloth_navy", "cloth_cream", "cloth_gold"], cloth="all", origin="top", two_sided=True, sharp=80),
    "BannerKnot":    dict(src="BannerKnot_lvxmagick", fam="banners", take=[("box", (-1, -1, -1.1), (1, 1, 0.93))], size=("Z", 1.6), tris=8000,
                          mats=["cloth_gold", "leather"], cloth="all", origin="top", two_sided=True, sharp=80),
    "BannerLion":    dict(src="BannerLion_erencakmakhtc", fam="banners", take=[("box", (-1, -1, -1.1), (1, 1, 0.77))], size=("Z", 1.6), tris=3000, subdiv=1,
                          mats=["cloth_crimson", "cloth_gold"], cloth="all", origin="top", two_sided=True, sharp=80),
    # dropped (a steampunk compass, scroll and brass gears for an emblem; recoloured, the navy goes patchy grey): "BannerNavy":    dict(src="BannerNavy_adrianmerino1996", fam="banners", take=[("box", (-0.305, -1, -1.1), (0.305, 1, 0.76))], size=("Z", 1.6), tris=10000,
    #                       mats=["cloth_navy", "cloth_cream", "cloth_gold", "iron"], cloth="all", origin="top", two_sided=True, sharp=80),
    "BannerSwordShield": dict(src="BannerSwordShield_marin", fam="banners", take=[("box", (-1, -1, -1.1), (1, 1, 0.52))], size=("Z", 1.5), tris=8000,
                          mats=["cloth_crimson", "cloth_gold"], cloth="all", origin="top", two_sided=True, sharp=80),
    "BannerSwords":  dict(src="BannerSwords_testedoteste596", fam="banners", take=[("box", (-0.8, -1, -1.1), (0.8, 1, 0.83))], size=("Z", 1.5), tris=8000,
                          mats=["cloth_crimson", "cloth_cream"], cloth="all", origin="top", two_sided=True, sharp=80),
    # banner poles, their own cloth cut off (combinations below hang the banners above on their crossbars)
    "BannerPoleStone": dict(src="BannerPoleStone_renenod803", fam="banners", drop=[("box", (-0.16, -1, -0.45), (0.45, 1, 0.615))], size=("Z", 2.6), tris=8000,
                          mats=["wood", "iron", "stone"], sharp=40),
    "BannerPostTattered": dict(src="BannerPostTattered_wfh.suresh2020", fam="banners", drop=[("box", (-0.14, -1, -0.7), (0.45, 1, 0.74)),
                          ("and", ("box", (-0.14, -1, 0.6), (0.45, 1, 0.84)), ("lab", (30, -20, -40), (95, 8, 2)))], size=("Z", 2.6), tris=8000,
                          mats=["wood", "rope"], sharp=40),
    # pole + banner pieces ("combo": the cleaned banner scaled to the crossbar's free span, its top edge at the bar; file metres, measured
    # on the cleaned poles: the stone pole's bar runs x -0.45..0.56 at z 2.56..2.70, the tattered post's x -0.3..0.64 at z 2.69..2.80)
    "BannerPoleGriffin": dict(fam="banners", combo=[("BannerPoleStone", (0, 0, 0), None, 0), ("BannerGriffin", (0.1, 0, 2.60), 0.8, 0)]),
    "BannerPoleLion":    dict(fam="banners", combo=[("BannerPoleStone", (0, 0, 0), None, 0), ("BannerLion", (0.1, 0, 2.60), 0.8, 0)]),
    "BannerPoleSwordShield": dict(fam="banners", combo=[("BannerPoleStone", (0, 0, 0), None, 0), ("BannerSwordShield", (0.1, 0, 2.60), 0.8, 0)]),
    "BannerPostKnot":    dict(fam="banners", combo=[("BannerPostTattered", (0, 0, 0), None, 0), ("BannerKnot", (0.17, 0, 2.74), 0.82, 0)]),
    "BannerPostSwords":  dict(fam="banners", combo=[("BannerPostTattered", (0, 0, 0), None, 0), ("BannerSwords", (0.17, 0, 2.74), 0.82, 0)]),
    # dropped (its crossbar is wrapped in the red cloth, so cutting the banner leaves floating spikes; the horned iron base reads orcish): "BannerPostSpiked": dict(src="BannerPostSpiked_harry", fam="banners", drop=[("and", ("box", (-0.26, -1, -0.45), (0.26, 1, 0.735)), ("lab", (0, 14, 0), (100, 90, 70))),
    #                       ("and", ("box", (-0.2, -1, 0.2), (0.2, -0.035, 0.62)), ("lab", (0, -20, -20), (28, 14, 20)))], size=("Z", 2.6), tris=10000,
    #                       mats=["wood", "iron"], sharp=40),
    # --- wells (the town's well is 3.4 m across in the game; back-yard wells smaller) ---
    # (zones by height: roof, frame, stone ring; colour alone mixed the thatch, the stone and the brick up)
    "WellThatched":  dict(src="WellThatched_xuynpho11", fam="wells", size=("L", 2.8), tris=30000, tex=2048, mats=["wood", "iron", "rope"], sharp=45, targets=MOSS,
                          zones=[(("box", (-1.1, -1.1, 0.45), (1.1, 1.1, 1.1)), ["thatch", "moss"]), (("box", (-1.1, -1.1, -1.1), (1.1, 1.1, -0.4)), ["stone", "moss"])]),
    "WellVillage":   dict(src="WellVillage_ZIMBO", fam="wells", size=("L", 2.8), tris=30000, tex=2048, mats=["stone", "wood", "iron", "leaf", "keep"], sharp=45, targets=MOSS,
                          anchors={(22, 3, 4): "stone", (41, 4, 4): "stone"},
                          zones=[(("box", (-1.1, -1.1, 0.38), (1.1, 1.1, 1.1)), ["wood"]), (("box", (-0.92, -0.3, -0.08), (-0.58, 0.3, 0.32)), ["iron", "glow"])]),
    "WellStylised":  dict(src="WellStylised_OG_Assets", fam="wells", size=("Z", 3.3), tris=20000, tex=2048, mats=["stone", "wood", "iron", "rope"], sharp=45),
    "WellRedRoof":   dict(src="WellRedRoof_Furio", fam="wells", size=("L", 2.6), tris=24000, tex=2048, mats=["wood", "iron"], sharp=45,
                          zones=[(("box", (-1.1, -1.1, 0.32), (1.1, 1.1, 1.1)), ["tile", "wood"], {(39, 6, 13): "wood", (26, 22, 17): "tile", (40, 31, 23): "tile"}),
                                 (("box", (-0.62, -0.62, -1.1), (0.62, 0.62, -0.15)), ["stone"])]),
    "WellCobble":    dict(src="WellCobble_LoSVaGoS", fam="wells", size=("L", 5.5), tris=40000, tex=2048, mats=["stone", "wood", "tile", "leaf", "keep"], sharp=45,
                          looks={"tile": (50.0, -20.0, -10.0)}, anchors={(55, -7, 26): "stone"}, targets=MOSS),
    "WellOld":       dict(src="WellOld_dainis", fam="wells", size=("L", 2.2), tris=8000, mats=["wood"], sharp=40,
                          zones=[(("box", (-1.1, -1.1, -1.1), (1.1, 1.1, -0.18)), ["stone"])]),
    "WellMossy":     dict(src="WellMossy_epix.asmr", fam="wells", size=("L", 2.2), tris=16000, tex=2048, mats=["stone", "wood", "moss"], sharp=45),
    # --- torches: each body alone; each flame its own piece in the body's frame (the game puts its own flame and light there) ---
    "TorchBlazing":  dict(src="TorchBlazing_list80.80", fam="torches", drop=[TORCH_FLAME], size=("Z", 0.62), tris=6000, mats=["wood", "iron", "rope"], sharp=40),
    "TorchBlazingFlame": dict(src="TorchBlazing_list80.80", fam="torches", take=[TORCH_FLAME], frame="TorchBlazing", tris=2000, mats=["glow"], sharp=80),
    "TorchWall":     dict(src="TorchWall_Ganda", fam="torches", drop=[WALL_FLAME], size=("Z", 0.8), tris=8000, mats=["wood", "iron"], origin="back", sharp=40),
    "TorchWallFlame": dict(src="TorchWall_Ganda", fam="torches", take=[WALL_FLAME], frame="TorchWall", tris=2000, mats=["glow"], sharp=80),
    "Sconce":        dict(src="Sconce_Frikicornio", fam="torches", take=[("comp", 0)], size=("Z", 0.45), tris=6000, mats=["brass", "plaster"], origin="back", sharp=40),
    "SconceFlame":   dict(src="Sconce_Frikicornio", fam="torches", take=[("comp", 1)], frame="Sconce", tris=1000, mats=["glow"], sharp=80),
    "TorchUnlitRope": dict(src="TorchUnlit_joespadaford", fam="torches", take=[("cluster", 0)], size=("Z", 0.7), tris=6000, mats=["wood", "rope"], sharp=40),
    "TorchUnlitTar": dict(src="TorchUnlit_joespadaford", fam="torches", take=[("cluster", 1)], size=("Z", 0.7), tris=6000, mats=["wood", "tar", "rope"], sharp=40),
    # dropped (a photo-scan ground patch with grass blades; cleaned it reads as a grey smudge, not a puddle: a road decal would do better): "MudPatch":      dict(src="MudPatch_keko", fam="torches", take=[("comp", 0)], size=("L", 2.0), tris=4000, mats=["soil", "stone", "keep"], sharp=60),
}
DEFAULTS = dict(take=["all"], drop=[], moves=[], fill=0, speck=0.003, rot=(0, 0, 0), sym=None, sharp=45, subdiv=0, tex=1024,
                mats=["wood"], zones=[], anchors={}, keepL=0.75, keepC=0.3, shade=0.6, even=0.35, ndetail=0.35, gap_l=20.0, unshade=0.8, two_sided=False, far=24.0, targets={}, looks={})
FAMILIES = [("storage", "Storage"), ("firewood", "Firewood and rope"), ("pots", "Pots and cauldrons"), ("farm", "Hay and farm"),
            ("garden", "Garden and flowers"), ("bushes", "Bushes"), ("walls", "Walls, fences and stairs"),
            ("furniture", "Benches, tables and signs"), ("lights", "Lanterns, lamp posts and awnings"),
            ("stalls", "Market stalls"), ("banners", "Banners and banner poles"), ("wells", "Wells"), ("torches", "Torches and mud"),
            ("planters", "Planters and flower boxes"), ("rocks", "Rocks and boulders"), ("logs", "Fallen wood"), ("stumps", "Stumps")]

# ---------------------------------------------------------------------------------------------------------------------------------
# The palette (CIE Lab, D65, of the sRGB base colour) and how each material looks in the Meshy originals
# ---------------------------------------------------------------------------------------------------------------------------------
def _lab_of_linear(rgb):
    M_ = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    x = M_ @ np.array(rgb) / np.array([0.9505, 1.0, 1.089])
    f = np.where(x > 0.008856, np.cbrt(x), 7.787 * x + 16 / 116)
    return (round(float(116 * f[1] - 16), 1), round(float(500 * (f[0] - f[1])), 1), round(float(200 * (f[1] - f[2])), 1))

def _hsv_linear(h, s, v):
    # (hue degrees in the browns, under 60) -> linear RGB
    mn = v * (1 - s)
    return (v, mn + (v - mn) * h / 60.0, mn)

# WOOD_NOTE: the item style guide's game-tested held-prop brown (hue 25, saturation 0.74, value 0.28 in linear colour) is Lab
# (49, 9, 22); on a barrel in daylight that read pale and washed out, like weathered pine, so the filler wood is a little darker and
# richer (39, 11, 21): still well lighter than the buildings' timber (20, 12, 16), as a barrel's oak is next to a beam.
# name: (Lab target, roughness, metallic). Wood is the item style guide's game-tested brown (hue 25, saturation 0.74, value 0.28 in
# linear colour); stone and plaster are the Oak Hill buildings' (building_palette.py), so the props sit with the town.
PALETTE = {
    "wood":    ((39.0, 11.0, 21.0), 0.72, 0.0),     # rich stained-walnut brown (see WOOD_NOTE)
    "iron":    ((31.0, 0.0, -1.5), 0.42, 0.6),     # dark steel, a little cool, no rust
    "burlap":  ((53.0, 8.0, 23.0), 0.9, 0.0),      # warm natural tan, not yellow
    "rope":    ((58.0, 7.0, 24.0), 0.9, 0.0),      # a shade lighter than burlap
    "leather": ((30.0, 12.0, 17.0), 0.6, 0.0),     # dark brown leather
    "clay":    ((47.0, 16.0, 26.0), 0.8, 0.0),     # warm terracotta, softer than Meshy's
    "straw":   ((70.0, 5.0, 42.0), 0.9, 0.0),      # golden straw that sits with the meadow
    "grain":   ((66.0, 9.0, 44.0), 0.8, 0.0),      # wheat in a sack
    "leaf":    ((56.0, -24.0, 34.0), 0.85, 0.0),   # soft, a little bright, autumn-friendly green
    "stone":   ((48.8, -0.2, 8.4), 0.85, 0.0),     # the buildings' warm grey stone
    "plaster": ((89.9, 0.8, 15.9), 0.85, 0.0),     # the buildings' cream (canvas, linen)
    "keep":    (None, 0.75, 0.0),                  # its own colours, only cleaned (flowers, painted signs)
    "glow":    (None, 0.5, 0.0),                   # lantern glass and flames: own colour, own material slot "Glow"
}
# what each material looks like in the originals (Lab), for sorting the colour groups
LOOKS = {
    "wood": (45.0, 9.0, 20.0), "iron": (28.0, 2.0, 5.0), "burlap": (58.0, 8.0, 22.0), "rope": (62.0, 6.0, 24.0),
    "leather": (30.0, 10.0, 15.0), "clay": (50.0, 25.0, 30.0), "straw": (72.0, 5.0, 45.0), "grain": (68.0, 18.0, 58.0),
    "leaf": (45.0, -20.0, 30.0), "stone": (55.0, 0.0, 6.0), "plaster": (85.0, 2.0, 12.0), "keep": None, "glow": (85.0, 10.0, 60.0),
}
LOOK_W = np.array([0.35, 1.0, 1.0], np.float32)     # lightness counts a third when sorting, so lit and shaded wood stay wood
# a group can only be this material if its lightness is in this range (light, greyish worn wood is not iron)
LOOK_L = {"iron": (0.0, 50.0), "plaster": (65.0, 100.0), "leather": (0.0, 42.0)}
# (walls, furniture and lights: dyed awning cloth, its colour kept but calmed onto the palette; used only by pieces that name them)
PALETTE.update({"cloth_blue": ((40.0, 2.0, -26.0), 0.85, 0.0),     # a calm market-awning blue, not electric
                "cloth_red":  ((46.0, 28.0, 30.0), 0.85, 0.0)})    # a soft rust-orange, a little redder than the clay
LOOKS.update({"cloth_blue": (38.0, 8.0, -40.0), "cloth_red": (42.0, 32.0, 36.0)})
# (stalls and banners: rich dyed cloth, still calm enough to sit with the palette)
PALETTE.update({"cloth_green":   ((40.0, -22.0, 12.0), 0.85, 0.0),    # a deep market green, not lime
                "cloth_cream":   ((86.0, 1.0, 15.0), 0.85, 0.0),      # unbleached linen stripes
                "cloth_crimson": ((40.0, 44.0, 26.0), 0.85, 0.0)})    # a rich banner red, a little warm
LOOKS.update({"cloth_green": (42.0, -22.0, 10.0), "cloth_cream": (82.0, 2.0, 14.0), "cloth_crimson": (42.0, 50.0, 30.0)})
# (banners, wells and torches; used only by pieces that name them)
PALETTE.update({"cloth_navy":  ((30.0, 4.0, -28.0), 0.85, 0.0),     # a deep banner blue
                "cloth_gold":  ((62.0, 10.0, 50.0), 0.85, 0.0),     # woven gold or mustard (not metal)
                "brass":       ((58.0, 6.0, 36.0), 0.38, 0.85),     # polished brass fittings, warm, not orange
                "thatch":      ((50.0, 6.0, 23.0), 0.9, 0.0),       # a roof's straw, calmer and browner than the houses' orange-ish thatch
                "tile":        ((30.0, 21.0, 19.0), 0.8, 0.0),      # the buildings' red-brown clay roof tiles (building_palette.py)
                "tar":         ((20.0, 2.0, 3.0), 0.6, 0.0)})       # a torch's tarred head
LOOKS.update({"cloth_navy": (28.0, 6.0, -30.0), "cloth_gold": (62.0, 12.0, 55.0), "brass": (60.0, 8.0, 42.0), "thatch": (60.0, 8.0, 35.0),
              "tile": (38.0, 28.0, 24.0), "tar": (18.0, 2.0, 2.0)})
# (logs, yards and gardens; used only by pieces that name them)
PALETTE.update({"bark":    ((34.0, 7.0, 14.0), 0.88, 0.0),      # a log's bark: a deeper, greyer brown than the worked wood
                "woodcut": ((57.0, 11.0, 26.0), 0.8, 0.0),      # a log's sawn or split face: light honey-tan, not orange
                "soil":    ((31.0, 8.0, 14.0), 0.95, 0.0)})     # dark garden earth
LOOKS.update({"bark": (38.0, 6.0, 14.0), "woodcut": (64.0, 15.0, 38.0), "soil": (28.0, 8.0, 12.0)})
PALETTE.update({"moss": ((47.0, -18.0, 30.0), 0.95, 0.0)})     # moss on rocks, logs and stumps: a shade deeper and softer than the leaves
LOOKS.update({"moss": (42.0, -14.0, 26.0)})

# the filters' settings (texels at a 1024 picture, scaled with its size), after building_flatten.py's
FLAT = dict(texture_scale=1.5, rolls=4, edge=10.0, smooth=6.0, passes=3, variation=0.45, variation_colour=0.2, variation_size=2.0,
            grain=0.08, shade_size=24.0, classes=8, group_soft=3.0, crisp=0.2, antialias=0.7, normal_smooth=2.0)

# ---------------------------------------------------------------------------------------------------------------------------------
# Colour (copied from building_flatten.py)
# ---------------------------------------------------------------------------------------------------------------------------------
M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], dtype=np.float32)
WHITE = np.array([0.9505, 1.0, 1.089], dtype=np.float32)
MI = np.linalg.inv(M).astype(np.float32)

def to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((np.maximum(rgb, 0) + 0.055) / 1.055) ** 2.4)
    xyz = c @ M.T / WHITE
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1).astype(np.float32)

def to_rgb(lab):
    fy = (lab[..., 0] + 16) / 116; fx = fy + lab[..., 1] / 500; fz = fy - lab[..., 2] / 200
    f = np.stack([fx, fy, fz], -1)
    xyz = np.where(f > 0.206893, f ** 3, (f - 16 / 116) / 7.787) * WHITE
    c = np.clip(xyz @ MI.T, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055).astype(np.float32)

# edge-aware filtering: the domain transform's recursive filter (Gastal and Oliveira 2011), as in building_flatten.py
STOP = 1e4

def _sweep(F, V):
    for j in range(1, F.shape[0]):
        F[j] += V[j][:, None] * (F[j - 1] - F[j])
    for j in range(F.shape[0] - 2, -1, -1):
        F[j] += V[j + 1][:, None] * (F[j + 1] - F[j])

def rf(F, dx, dy, sigma_s, passes):
    F = F.astype(np.float32).copy()
    dxT = np.ascontiguousarray(dx.T); passes = int(passes)
    for k in range(passes):
        sig = sigma_s * np.sqrt(3.0) * 2 ** (passes - k - 1) / np.sqrt(4 ** passes - 1)
        A = np.float32(np.exp(-np.sqrt(2.0) / sig))
        G = np.ascontiguousarray(F.transpose(1, 0, 2)); _sweep(G, A ** dxT); F = np.ascontiguousarray(G.transpose(1, 0, 2))
        _sweep(F, A ** dy)
    return F

def steps(F, scale, same):
    dx = np.ones(F.shape[:2], np.float32); dy = np.ones(F.shape[:2], np.float32)
    if scale:
        dx[:, 1:] += scale * np.abs(F[:, 1:] - F[:, :-1]).sum(-1)
        dy[1:] += scale * np.abs(F[1:] - F[:-1]).sum(-1)
    sx, sy = same
    dx[:, 1:][~sx] = STOP; dy[1:][~sy] = STOP
    return dx, dy

def same_of(lbl):
    return lbl[:, 1:] == lbl[:, :-1], lbl[1:] == lbl[:-1]

def box3(F):
    P = np.pad(F, ((1, 1), (1, 1), (0, 0)), mode="edge")
    return sum(P[y:y + F.shape[0], x:x + F.shape[1]] for y in range(3) for x in range(3)) / 9.0

def kmeans(X, k, iters=20, seed=1):
    rng = np.random.default_rng(seed)
    S = X[rng.choice(len(X), min(len(X), 150_000), replace=False)]
    C = S[rng.choice(len(S), k, replace=False)].copy()
    for _ in range(iters):
        l = ((S[:, None, :] - C[None]) ** 2).sum(2).argmin(1)
        for j in range(k):
            if (l == j).any(): C[j] = S[l == j].mean(0)
    return C

def assign(X, C):
    out = np.empty(len(X), np.int32)
    for c in range(0, len(X), 1 << 20):
        out[c:c + (1 << 20)] = ((X[c:c + (1 << 20), None, :] - C[None]) ** 2).sum(2).argmin(1)
    return out

# ---------------------------------------------------------------------------------------------------------------------------------
# Pictures
# ---------------------------------------------------------------------------------------------------------------------------------
def img_get(im):
    W, H = im.size
    p = np.empty(W * H * 4, np.float32); im.pixels.foreach_get(p)
    return p.reshape(H, W, 4)

def img_new(name, arr, noncolor=False):
    H, W = arr.shape[:2]
    im = bpy.data.images.new(name, W, H, alpha=False)
    if noncolor: im.colorspace_settings.name = "Non-Color"
    p = np.ones((H, W, 4), np.float32); p[..., :3] = np.clip(arr[..., :3], 0, 1)
    im.pixels.foreach_set(p.ravel()); im.pack()
    return im

def save_jpg(arr, path, quality=88):
    H, W = arr.shape[:2]
    im = bpy.data.images.new("out", W, H, alpha=False)
    p = np.ones((H, W, 4), np.float32); p[..., :3] = np.clip(arr[::-1, :, :3], 0, 1)   # (arrays run top-down, Blender bottom-up)
    im.pixels.foreach_set(p.ravel()); im.filepath_raw = path; im.file_format = "JPEG"
    bpy.context.scene.render.image_settings.quality = quality
    im.save(); bpy.data.images.remove(im)

def tex_node(mat, sock_name):
    pr = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if pr is None: return None
    stack = [pr.inputs[sock_name]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(n.inputs)
    return None

# ---------------------------------------------------------------------------------------------------------------------------------
# Mesh helpers
# ---------------------------------------------------------------------------------------------------------------------------------
def verts_np(me):
    co = np.empty(len(me.vertices) * 3, np.float64); me.vertices.foreach_get("co", co); return co.reshape(-1, 3)

def tris_of(me):
    return sum(len(p.vertices) - 2 for p in me.polygons)

def components(me):
    # loose parts: a label per vertex (labels spread to the smallest vertex number along edges, with pointer jumping)
    n = len(me.vertices)
    e = np.empty(len(me.edges) * 2, np.int64); me.edges.foreach_get("vertices", e); e = e.reshape(-1, 2)
    lab = np.arange(n)
    while True:
        m = np.minimum(lab[e[:, 0]], lab[e[:, 1]])
        new = lab.copy(); np.minimum.at(new, e[:, 0], m); np.minimum.at(new, e[:, 1], m)
        new = new[new]
        if np.array_equal(new, lab): break
        lab = new
    _, comp, cnt = np.unique(lab, return_inverse=True, return_counts=True)
    order = np.argsort(-cnt, kind="stable"); rank = np.empty_like(order); rank[order] = np.arange(len(order))
    return rank[comp]          # component 0 = the one with the most vertices

def clusters_of(co, comp, gap):
    # loose parts whose boxes (grown by gap) touch are one cluster; numbered biggest first
    K = comp.max() + 1
    lo = np.full((K, 3), np.inf); hi = np.full((K, 3), -np.inf)
    np.minimum.at(lo, comp, co); np.maximum.at(hi, comp, co)
    par = list(range(K))
    def find(i):
        while par[i] != i: par[i] = par[par[i]]; i = par[i]
        return i
    for i in range(K):
        ov = np.all((lo[i] - gap <= hi) & (hi[i] + gap >= lo), axis=1); ov[i] = False
        for j in np.nonzero(ov)[0]:
            a_, b_ = find(i), find(int(j))
            if a_ != b_: par[max(a_, b_)] = min(a_, b_)
    root = np.array([find(i) for i in range(K)])
    cnt = np.bincount(comp, minlength=K)
    _, cl = np.unique(root, return_inverse=True)
    size = np.bincount(cl, cnt)
    order = np.argsort(-size, kind="stable"); rank = np.empty_like(order); rank[order] = np.arange(len(order))
    return rank[cl][comp]      # a cluster per vertex

def face_select(me, rule, vcomp, vclus, co):
    # which faces a rule takes (bool per face)
    nf = len(me.polygons)
    fv = np.empty(nf, np.int64); me.polygons.foreach_get("loop_start", fv)
    lv = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lv)
    v0 = lv[fv]
    if rule == "all": return np.ones(nf, bool)
    if isinstance(rule, list): return np.any([face_select(me, r, vcomp, vclus, co) for r in rule], 0)
    kind = rule[0]
    if kind == "comp": return np.isin(vcomp[v0], rule[1:])
    if kind == "cluster": return np.isin(vclus[v0], rule[1:])
    if kind == "near":
        p = np.array(rule[1:4]); K = vclus.max() + 1
        cen = np.stack([np.bincount(vclus, co[:, i], K) / np.maximum(np.bincount(vclus, None, K), 1) for i in range(3)], 1)
        return vclus[v0] == int(((cen - p) ** 2).sum(1).argmin())
    if kind == "box":
        c = np.empty(nf * 3); me.polygons.foreach_get("center", c); c = c.reshape(-1, 3)
        return np.all((c >= np.array(rule[1])) & (c <= np.array(rule[2])), 1)
    if kind == "and": return np.all([face_select(me, r, vcomp, vclus, co) for r in rule[1:]], 0)
    if kind == "lab":
        # ("lab", (L0, a0, b0), (L1, a1, b1)): faces whose source colour (Lab, at the face's UV centre) is inside the range: the hay
        # on a wagon, told from the wagon's wood by colour where the two are one fused mesh
        lab = face_lab(me)
        return np.all((lab >= np.array(rule[1])) & (lab <= np.array(rule[2])), 1)
    raise SystemExit("FILLER unknown rule %r" % (rule,))

_FACE_LAB = {}
def face_lab(me):
    # each face's base colour in the source's own texture (Lab, a little smoothed), cached per mesh
    key = (me.name, len(me.polygons), len(me.loops))     # (the face count too: two sources in one run can share a mesh name)
    if key in _FACE_LAB: return _FACE_LAB[key]
    nf = len(me.polygons)
    out = np.full((nf, 3), 50.0, np.float32)
    uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    ls = np.empty(nf, np.int64); me.polygons.foreach_get("loop_start", ls)
    lt = np.empty(nf, np.int64); me.polygons.foreach_get("loop_total", lt)
    fuv = np.add.reduceat(uv, ls) / lt[:, None]
    mi = np.empty(nf, np.int64); me.polygons.foreach_get("material_index", mi)
    for k, mat in enumerate(me.materials):
        if mat is None or not mat.use_nodes: continue
        pr = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        lk = pr.inputs["Base Color"].links if pr else []
        if not lk or lk[0].from_node.type != "TEX_IMAGE": continue
        im = lk[0].from_node.image; arr = img_get(im)        # (Blender's rows: bottom up, row 0 = v 0)
        arr = box3(box3(arr[..., :3]))
        h, w = arr.shape[:2]; sel = mi == k
        x = np.clip((fuv[sel, 0] % 1.0) * w, 0, w - 1).astype(int); y = np.clip((fuv[sel, 1] % 1.0) * h, 0, h - 1).astype(int)
        out[sel] = to_lab(arr[y, x])
    _FACE_LAB[key] = out
    return out

def point_select(pts, rule, vcomp, vclus, co):
    # which points (in the source's frame) a rule takes: boxes by position, parts and clusters by the nearest source vertex
    if rule == "all": return np.ones(len(pts), bool)
    if isinstance(rule, list): return np.any([point_select(pts, r, vcomp, vclus, co) for r in rule], 0)
    if rule[0] == "box": return np.all((pts >= np.array(rule[1])) & (pts <= np.array(rule[2])), 1)
    from mathutils.kdtree import KDTree
    kd = KDTree(len(co))
    for i, c in enumerate(co): kd.insert(c, i)
    kd.balance()
    near = np.array([kd.find(p)[1] for p in pts])
    if rule[0] == "comp": return np.isin(vcomp[near], rule[1:])
    if rule[0] == "cluster": return np.isin(vclus[near], rule[1:])
    raise SystemExit("FILLER zone rule %r not supported" % (rule,))

def keep_faces(o, keep):
    bm = bmesh.new(); bm.from_mesh(o.data); bm.faces.ensure_lookup_table()
    gone = [f for f in bm.faces if not keep[f.index]]
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(o.data); bm.free(); o.data.update()

def fill_holes(o, sides):
    # close the openings a box cut left; the new faces borrow the UVs of their corners' old neighbours (a smear of the edge's colour)
    bm = bmesh.new(); bm.from_mesh(o.data)
    uvl = bm.loops.layers.uv.active
    edges = [e for e in bm.edges if e.is_boundary]
    res = bmesh.ops.holes_fill(bm, edges=edges, sides=sides)
    new = set(res["faces"])
    for f in new:
        for l in f.loops:
            for l2 in l.vert.link_loops:
                if l2.face not in new: l[uvl].uv = l2[uvl].uv.copy(); break
    bm.to_mesh(o.data); bm.free(); o.data.update()
    print("FILLER holes filled", len(new))

def tile_cut(o, tile):
    # opt-in "tile": (axis, a, b): cut the piece square at axis = a and axis = b (the source's frame after "rot"; None = no cut on that
    # side) and cap the cuts, so copies laid end to end along that axis meet with no gap and no doubled post (cut through the middle
    # of a post or pillar, so two halves make one). The caps borrow their edges' UVs; they are hidden where copies meet.
    ax = "XYZ".index(tile[0])
    bm = bmesh.new(); bm.from_mesh(o.data)
    planes = []
    for val, keep_hi in ((tile[1], True), (tile[2], False)):
        if val is None: continue
        no = Vector((0, 0, 0)); no[ax] = 1.0; pt = Vector((0, 0, 0)); pt[ax] = val
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-6, plane_co=pt, plane_no=no,
                               clear_inner=keep_hi, clear_outer=not keep_hi)
        planes.append(val)
    uvl = bm.loops.layers.uv.active
    edges = [e for e in bm.edges if e.is_boundary and any(all(abs(v.co[ax] - p) < 1e-4 for v in e.verts) for p in planes)]
    new = set(bmesh.ops.holes_fill(bm, edges=edges, sides=0)["faces"])
    if new:
        new |= set(bmesh.ops.triangulate(bm, faces=list(new), quad_method="BEAUTY", ngon_method="BEAUTY")["faces"])
        for f in (new if uvl is not None else ()):      # (a remeshed skin has no UVs yet)
            for l in f.loops:
                for l2 in l.vert.link_loops:
                    if l2.face not in new: l[uvl].uv = l2[uvl].uv.copy(); break
    bm.to_mesh(o.data); bm.free(); o.data.update()
    print("FILLER tile cut at %s %s, %d cap faces" % (tile[0], planes, len(new)))

def tile_snap(o, ax, ends, tol):
    # after decimating: vertices within tol of a cut end go back onto it exactly
    co = verts_np(o.data)
    for e in ends:
        m = np.abs(co[:, ax] - e) < tol; co[m, ax] = e
    co[:, ax] = np.clip(co[:, ax], min(ends), max(ends)) if len(ends) == 2 else co[:, ax]
    o.data.vertices.foreach_set("co", co.ravel()); o.data.update()

def plain_zones(name, lo, back, P, rgb, nrm, size, comp, clus, co):
    # opt-in "plain": [rule, ...]: the wood inside these zones is painted plain (AI-gibberish lettering cleared): each texel's
    # difference from its board's smoothed colour is clamped to a few Lab units, so painted strokes go and a touch of grain stays
    fc = np.empty(len(lo.data.polygons) * 3); lo.data.polygons.foreach_get("center", fc); fc = back(fc.reshape(-1, 3))
    fz = np.zeros(len(lo.data.polygons), np.int32)
    for rule in P["plain"]: fz[point_select(fc, rule, comp, clus, co)] = 1
    ids, used, zid = island_map(lo.data, size, size, fz)
    w = (zid == 1).astype(np.float32) * DEBUG_MATS.get(name, {}).get(P.get("plain_mat", "wood"), np.ones_like(zid, np.float32))
    lab = to_lab(rgb)
    dx, dy = steps(lab, 0, same_of(ids))
    sig = 28.0 * size / 1024.0
    # (the board's colour without its strokes: a wide smoothing that leaves the strokes out by weighting them down)
    B = rf(lab, dx, dy, sig, 2)
    for _ in range(2):
        d = np.abs(lab[..., 0] - B[..., 0])
        wt = np.exp(-(d / 6.0) ** 2)[..., None]
        B = rf(lab * wt, dx, dy, sig, 2) / np.maximum(rf(wt, dx, dy, sig, 2), 1e-3)
    # (carved letters are often UV islands of their own, whose smoothing stays letter-dark: keep every texel's board colour near
    # the zone's median wood)
    on = (w > 0.5) & used
    if on.any():
        Bm = np.zeros_like(lab)                  # (each UV island's own median, so a post running through the zone keeps its shade)
        for i in np.unique(ids[on]):
            sel = on & (ids == i); Bm[ids == i] = np.median(lab[sel], 0)
        B = np.where(on[..., None], Bm + np.clip(B - Bm, np.array([-3.0, -1.5, -1.5], np.float32), np.array([3.0, 1.5, 1.5], np.float32)), B)
    R = np.clip(lab - B, np.array([-4.0, -2.0, -2.0], np.float32), np.array([4.0, 2.0, 2.0], np.float32))
    # (strokes, whose sharp edges read as letters even at low contrast, take the board's smooth colour; the grain is fainter and stays)
    m = np.clip((np.sqrt(((lab - B) ** 2).sum(-1)) - 3.0) / 4.0, 0, 1)
    for _ in range(2):
        m = np.maximum.reduce([m, np.roll(m, 1, 0), np.roll(m, -1, 0), np.roll(m, 1, 1), np.roll(m, -1, 1)])
    m = box3(m[..., None])[..., 0]
    R = R * (1 - m[..., None])
    out = lab * (1 - w[..., None]) + (B + R) * w[..., None]
    v = nrm[..., :2] * 2 - 1
    vb = rf(v, dx, dy, 12.0 * size / 1024.0, 2)     # (carved letters go with the paint: only a soft hint of the relief stays)
    v = v * (1 - w[..., None]) + (vb + 0.15 * (v - vb)) * w[..., None]
    z = np.sqrt(np.clip(1 - (v ** 2).sum(-1), 0, 1))
    print("FILLER %s plain zones: %.1f%% of the picture" % (name, 100 * (w * used).sum() / max(1, used.sum())))
    return to_rgb(out), np.concatenate([v * 0.5 + 0.5, z[..., None] * 0.5 + 0.5], -1)

def dup(o, name):
    c = o.copy(); c.data = o.data.copy(); c.name = name; bpy.context.scene.collection.objects.link(c); c.hide_render = False; return c

def only_select(o):
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o

# ---------------------------------------------------------------------------------------------------------------------------------
# UV islands of the low model, drawn into the picture (after building_flatten.py's)
# ---------------------------------------------------------------------------------------------------------------------------------
def island_map(me, W, H, fz=None):
    me.calc_loop_triangles()
    uv = np.empty(len(me.loops) * 2, np.float32); me.uv_layers.active.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
    lt = np.empty(len(me.loop_triangles) * 3, np.int64); me.loop_triangles.foreach_get("loops", lt)
    tuv = uv[lt.reshape(-1, 3)]
    T = len(tuv)
    key = np.round(tuv.reshape(-1, 2) * np.array([W * 8, H * 8])).astype(np.int64)
    _, kid = np.unique(key[:, 0] * (H * 64) + key[:, 1], return_inverse=True); kid = kid.reshape(T, 3)
    lab = np.arange(T, dtype=np.int64)
    while True:
        kl = np.full(kid.max() + 1, T, np.int64)
        np.minimum.at(kl, kid.ravel(), np.repeat(lab, 3))
        new = np.minimum(lab, kl[kid].min(1)); new = new[new]
        if np.array_equal(new, lab): break
        lab = new
    _, isl = np.unique(lab, return_inverse=True)
    tz = np.zeros(T, np.int32)
    if fz is not None:
        tp = np.empty(T, np.int64); me.loop_triangles.foreach_get("polygon_index", tp); tz = fz[tp]
    zid = np.zeros((H, W), np.int32)
    ids = np.full((H, W), -1, np.int32)
    P = tuv * np.array([W, H], np.float32)
    x0 = np.ceil(P[..., 0].min(1) - 0.5).astype(np.int64); x1 = np.floor(P[..., 0].max(1) - 0.5).astype(np.int64)
    y0 = np.ceil(P[..., 1].min(1) - 0.5).astype(np.int64); y1 = np.floor(P[..., 1].max(1) - 0.5).astype(np.int64)
    size = np.maximum(x1 - x0 + 1, y1 - y0 + 1)
    ok = size > 0
    lo = 0
    for s in [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048, 4096]:
        sel = np.nonzero(ok & (size > lo) & (size <= s))[0]; lo = s
        step = max(1, 4_000_000 // (s * s))
        for c in range(0, len(sel), step):
            t = sel[c:c + step]
            gx = x0[t][:, None] + np.arange(s)[None]; gy = y0[t][:, None] + np.arange(s)[None]
            px = np.broadcast_to(gx[:, None, :] + 0.5, (len(t), s, s)); py = np.broadcast_to(gy[:, :, None] + 0.5, (len(t), s, s))
            A, B, C = P[t, 0], P[t, 1], P[t, 2]
            def edge(p, q):
                return (q[:, 0, None, None] - p[:, 0, None, None]) * (py - p[:, 1, None, None]) - (q[:, 1, None, None] - p[:, 1, None, None]) * (px - p[:, 0, None, None])
            e0, e1, e2 = edge(A, B), edge(B, C), edge(C, A)
            inside = ((e0 >= -1e-6) & (e1 >= -1e-6) & (e2 >= -1e-6)) | ((e0 <= 1e-6) & (e1 <= 1e-6) & (e2 <= 1e-6))
            inside &= (gx[:, None, :] >= 0) & (gx[:, None, :] < W) & (gy[:, :, None] >= 0) & (gy[:, :, None] < H)
            tt, yy, xx = np.nonzero(inside)
            ids[gy[tt, yy], gx[tt, xx]] = isl[t[tt]]
            zid[gy[tt, yy], gx[tt, xx]] = tz[t[tt]]
    used = ids >= 0
    for _ in range(24):
        miss = ids < 0
        if not miss.any(): break
        n = ids.copy()
        for sh in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            r = np.roll(ids, sh, (0, 1)); take = (n < 0) & (r >= 0); n[take] = r[take]; zid[take] = np.roll(zid, sh, (0, 1))[take]
        ids = n
    ids[ids < 0] = 0
    return ids, used, zid

# ---------------------------------------------------------------------------------------------------------------------------------
# Bake: the full model's colour and normal detail onto the low model's fresh UVs
# ---------------------------------------------------------------------------------------------------------------------------------
def setup_cycles():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
    sc.render.bake.use_pass_direct = False; sc.render.bake.use_pass_indirect = False

def bake(lo, hi, size):
    setup_cycles()
    me = lo.data
    for l in list(me.uv_layers): me.uv_layers.remove(l)
    uvb = me.uv_layers.new(name="UV"); me.uv_layers.active = uvb
    only_select(lo)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60.0), island_margin=0.006)
    bpy.ops.object.mode_set(mode="OBJECT")
    # (every material of the source is baked: most Meshy files have one, a few have several)
    tbs = [(m, tex_node(m, "Base Color")) for m in hi.data.materials if m is not None and m.use_nodes]
    tb = any(t for _m, t in tbs)
    lmat =bpy.data.materials.new("Bake"); lmat.use_nodes = True
    me.materials.clear(); me.materials.append(lmat)
    ln = lmat.node_tree.nodes
    tgt = ln.new("ShaderNodeTexImage")
    co = verts_np(me); diag = float(np.linalg.norm(co.max(0) - co.min(0)))
    hi.select_set(True); lo.select_set(True); bpy.context.view_layer.objects.active = lo
    kw = dict(use_selected_to_active=True, cage_extrusion=0.01 * diag, max_ray_distance=0.03 * diag, margin=8, use_clear=True)
    got = {}
    im = bpy.data.images.new("BakeN", size, size, alpha=False); im.colorspace_settings.name = "Non-Color"
    tgt.image = im; ln.active = tgt
    bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw); got["Normal"] = img_get(im)[..., :3]
    # ambient occlusion of the full model (its insides and crevices), to take Meshy's baked shadow back out of the colour
    bpy.context.scene.cycles.samples = 24
    bpy.context.scene.world = bpy.context.scene.world or bpy.data.worlds.new("W")
    bpy.context.scene.world.light_settings.distance = 0.25 * diag
    im4 = bpy.data.images.new("BakeAO", size // 2, size // 2, alpha=False); im4.colorspace_settings.name = "Non-Color"
    tgt.image = im4; ln.active = tgt
    # (on the low model itself: the full Meshy model has inner shells and stray faces that read as half occluded everywhere)
    hi.hide_render = True; hi.select_set(False)
    bpy.ops.object.bake(type="AO", margin=4, use_clear=True, use_selected_to_active=False)
    hi.hide_render = False; hi.select_set(True)
    got["AO"] = np.repeat(np.repeat(img_get(im4)[..., 0], 2, 0), 2, 1)
    bpy.data.images.remove(im4)
    bpy.context.scene.cycles.samples = 1
    if tb:
        ems = []
        for hmat, t in tbs:
            nt = hmat.node_tree
            out_node = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
            em = nt.nodes.new("ShaderNodeEmission")
            if t: nt.links.new(t.outputs["Color"], em.inputs["Color"])
            else:
                pr = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
                if pr: em.inputs["Color"].default_value = pr.inputs["Base Color"].default_value
            nt.links.new(em.outputs[0], out_node.inputs["Surface"])
            ems.append((hmat, em))
        im2 = bpy.data.images.new("BakeB", size, size, alpha=False)
        tgt.image = im2; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got["Base"] = img_get(im2)[..., :3]
        bpy.data.images.remove(im2)
        # the source's metal map (glTF: blue = metallic), when it has one: tells iron from wood better than colour does
        tms = [(hmat, em, tex_node(hmat, "Metallic")) for hmat, em in ems]
        if any(t for _h, _e, t in tms):
            for hmat, em, tm in tms:
                for l in list(em.inputs["Color"].links): hmat.node_tree.links.remove(l)
                if tm:
                    sep = hmat.node_tree.nodes.new("ShaderNodeSeparateColor")
                    hmat.node_tree.links.new(tm.outputs["Color"], sep.inputs["Color"])
                    hmat.node_tree.links.new(sep.outputs[2], em.inputs["Color"])
                else: em.inputs["Color"].default_value = (0, 0, 0, 1)
            im3 = bpy.data.images.new("BakeM", size // 2, size // 2, alpha=False); im3.colorspace_settings.name = "Non-Color"
            tgt.image = im3; ln.active = tgt
            bpy.ops.object.bake(type="EMIT", **dict(kw, margin=4))
            m = img_get(im3)[..., 0]
            got["Metal"] = np.repeat(np.repeat(m, 2, 0), 2, 1)
            bpy.data.images.remove(im3)
    bpy.data.images.remove(im)
    ln.remove(tgt)
    return got

# ---------------------------------------------------------------------------------------------------------------------------------
# Colour: clean, then onto the palette
# ---------------------------------------------------------------------------------------------------------------------------------
def classify(C, mats, anchors, gmet, gap, far=24.0, lk={}):
    # each colour group's material: an anchor within 12 Lab units wins; then a dark group is a gap (it takes its neighbours' material);
    # then, if the source has a metal map, a group that is mostly metal is iron and one that is hardly metal is not; then the nearest
    # look among the zone's materials
    out = []
    looks = [(m, np.array(lk.get(m, LOOKS.get(m)), np.float32)) for m in mats if lk.get(m, LOOKS.get(m)) is not None]     # (a piece's own "looks" win, and skip the lightness gate)
    for j, c in enumerate(C):
        cl = c / np.array([0.35, 1, 1], np.float32)      # back to plain Lab
        best = None
        for k, m in anchors.items():
            d = float(np.linalg.norm((np.array(k) - cl) * LOOK_W))
            if d < 12.0 and (best is None or d < best[0]): best = (d, m)
        if best is not None and best[1] in mats + ["gap", "keep", "glow"]: out.append(best[1]); continue
        if gap[j]: out.append("gap"); continue
        if not looks: out.append("keep"); continue
        ok = [(m, l) for m, l in looks if m in lk or LOOK_L.get(m, (0, 100))[0] <= cl[0] <= LOOK_L.get(m, (0, 100))[1]] or looks
        if gmet is not None and "iron" in mats and "iron" not in lk:      # (unless the piece says what its iron looks like)
            if gmet[j] >= 0.45: out.append("iron"); continue
            if gmet[j] < 0.2: ok = [t for t in ok if t[0] != "iron"] or ok
        d, m = min(((float(np.linalg.norm((l - cl) * LOOK_W)), m) for m, l in ok), key=lambda t: t[0])
        # (with "keep" among the materials, a group far from every look keeps its own colour: flowers, berries, a painted pumpkin)
        out.append("keep" if "keep" in mats and d > far else m)
    return out

def recolour(name, base, metalmap, ao, ids, used, zid, o):
    H, W = base.shape[:2]; sc = W / 1024.0
    if ao is not None and o["unshade"] > 0:
        # Meshy paints the model's shadow into its colour (dark insides, crevices): lift it back by the occlusion, softly
        print("FILLER %s occlusion percentiles 5/25/50/75/95: %s" % (name, np.round(np.percentile(ao[used], [5, 25, 50, 75, 95]), 2)))
        a_ = rf(np.clip(ao, 0.0, 1.0)[..., None], *steps(base, 0, same_of(ids)), 3.0 * sc, 2)[..., 0]
        lin = np.where(base <= 0.04045, base / 12.92, ((np.maximum(base, 0) + 0.055) / 1.055) ** 2.4)
        lin = np.clip(lin / np.clip(a_, 0.3, 1.0)[..., None] ** o["unshade"], 0, 1)
        base = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * lin ** (1 / 2.4) - 0.055).astype(np.float32)
    lab = to_lab(base)
    same = same_of(ids)
    if ao is not None: used = used & (ao > 0.06)      # (faces hidden inside the model don't count in the colour statistics)
    ts = o["texture_scale"] * sc
    dxs, dys = steps(lab, 0, same)
    G = rf(lab, dxs, dys, ts, 2)
    for _ in range(int(o["rolls"])):
        dx, dy = steps(G, ts / o["edge"], same)
        G = rf(lab, dx, dy, ts, 2)
    dx, dy = steps(G, o["smooth"] * sc / o["edge"], same)
    P = rf(G, dx, dy, o["smooth"] * sc, o["passes"])
    R = lab - P
    V = rf(R, dxs, dys, o["variation_size"] * sc, 2)
    out = P + V * np.array([o["variation"], o["variation_colour"], o["variation_colour"]], np.float32) + (R - V) * o["grain"]

    # colour groups and their materials, per zone
    X = (P * np.array([0.35, 1, 1], np.float32)).reshape(-1, 3)
    K = int(o["classes"])
    C = kmeans(X[used.ravel()], K)
    cls = assign(X, C).reshape(H, W)
    for _ in range(2):
        nb = np.stack([np.roll(cls, s, (0, 1)) for s in ((1, 0), (-1, 0), (0, 1), (0, -1))])
        agree = (nb == nb[0]).sum(0) >= 3
        cls = np.where(agree & (nb[0] != cls), nb[0], cls)
    Cp = C / np.array([0.35, 1, 1], np.float32)
    gap = Cp[:, 0] < o["gap_l"]
    gmet = None
    if metalmap is not None and float(metalmap[used].std()) > 0.05:
        gmet = np.array([float(metalmap[used & (cls == j)].mean()) if (used & (cls == j)).any() else 0.0 for j in range(K)])
    zmats = [list(o["mats"])] + [list(z[1]) for z in o["zones"]]
    # (a zone may carry its own anchors as a third entry, over the piece's: a lantern's light grey glass is glow there, stone elsewhere)
    zanch = [o["anchors"]] + [{**o["anchors"], **(z[2] if len(z) > 2 else {})} for z in o["zones"]]
    gm = [classify(C, zm, za, gmet, gap, o["far"], o["looks"]) for zm, za in zip(zmats, zanch)]
    share = np.bincount(cls[used], minlength=K) / max(1, used.sum())
    for j in np.argsort(-share):
        print("FILLER %s group Lab(%.0f %.0f %.0f)%s %4.1f%% -> %s" % (name, Cp[j][0], Cp[j][1], Cp[j][2],
              "" if gmet is None else " metal %.2f" % gmet[j], 100 * share[j], " / ".join(g[j] for g in gm)))

    # baked light and shade inside each surface (island x group) is cut
    region = ids.astype(np.int64) * 64 + cls
    rsame = same_of(region)
    dxr, dyr = steps(lab, 0, rsame)
    Bk = rf(out[..., :1], dxr, dyr, o["shade_size"] * sc, 2)[..., 0]
    _, rid = np.unique(region.ravel(), return_inverse=True)
    n = np.bincount(rid).astype(np.float64)
    rmL = (np.bincount(rid, out[..., 0].ravel()) / n)[rid].reshape(H, W)
    out[..., 0] -= o["shade"] * (Bk - rmL)
    # each surface's lightness moved "even" of the way to its colour group's (a darker island beside a lighter one of the same paint)
    gL = np.array([float(out[..., 0][used & (cls == j)].mean()) if (used & (cls == j)).any() else 50.0 for j in range(K)])
    rmL = (np.bincount(rid, out[..., 0].ravel()) / n)[rid].reshape(H, W)
    out[..., 0] -= o["even"] * (rmL - gL[cls])

    # soft material weights per texel (from the distance to every group, by the texel's zone)
    d2 = ((X.reshape(H, W, 1, 3) - C[None, None]) ** 2).sum(-1)
    wk = np.exp(-(d2 - d2.min(-1, keepdims=True)) / (2 * o["group_soft"] ** 2)); wk /= wk.sum(-1, keepdims=True)
    mats = sorted({m for g in gm for m in g} - {"gap"})
    W_m = {m: np.zeros((H, W), np.float32) for m in mats}
    Wg = np.zeros((H, W), np.float32)
    for z, g in enumerate(gm):
        inz = (zid == z)
        for j in range(K):
            if g[j] == "gap": Wg += wk[..., j] * inz
            else: W_m[g[j]] += wk[..., j] * inz
    # gaps (dark seams, cracks, shadowed insides) take the material round them, inside the same island, and stay dark
    if Wg.max() > 0 and mats:
        tot = sum(W_m.values())
        sig = 12.0 * sc
        Nt = rf(tot[..., None], dxs, dys, sig, 2)[..., 0]
        for m in mats:
            Nm = rf(W_m[m][..., None], dxs, dys, sig, 2)[..., 0]
            fallback = 1.0 / len(mats)
            W_m[m] = W_m[m] + Wg * np.where(Nt > 1e-3, Nm / np.maximum(Nt, 1e-3), fallback)
    res = np.zeros_like(out); rough = np.zeros((H, W), np.float32); metal = np.zeros((H, W), np.float32)
    glow = np.zeros((H, W), np.float32)
    tot = np.maximum(sum(W_m.values()), 1e-6) if mats else np.ones((H, W), np.float32)
    for m in mats:
        w = W_m[m] / tot; tgt, r_, mt = PALETTE[m]; tgt = o["targets"].get(m, tgt)     # ("targets": a piece's own shade of a material)
        # (the material's mean leaves out its gaps, so seams stay as much darker as they were)
        wm = w * used * (1 - Wg)
        sw = wm.sum()
        if sw < 1: mean = out.reshape(-1, 3).mean(0)
        else: mean = (out * wm[..., None]).reshape(-1, 3).sum(0) / sw
        if tgt is None:
            mapped = mean + 0.85 * (out - mean)          # keep: its own colour, a little calmer
        else:
            mapped = np.empty_like(out)
            mapped[..., 0] = tgt[0] + o["keepL"] * (out[..., 0] - mean[0])
            mapped[..., 1:] = np.array(tgt[1:], np.float32) + o["keepC"] * (out[..., 1:] - mean[1:])
            # (a gap keeps its darkness but takes the material's colour, scaled down with its lightness)
            mapped[..., 1:] = mapped[..., 1:] * (1 - Wg[..., None]) + np.array(tgt[1:], np.float32) * np.clip(mapped[..., :1] / max(tgt[0], 1), 0.2, 1) * Wg[..., None]
        res += mapped * w[..., None]; rough += r_ * w; metal += mt * w
        if m == "glow": glow += w
        print("FILLER %s %-7s %4.1f%% mean Lab(%.0f %.0f %.0f) -> %s" % (name, m, 100 * (w * used).sum() / max(1, used.sum()), mean[0], mean[1], mean[2],
              "kept" if tgt is None else "(%.0f %.0f %.0f)" % tgt))
    out = res
    DEBUG_MATS[name] = {m: W_m[m] / tot for m in mats}
    L = out[..., :1]
    out[..., 0] = np.clip((L + o["crisp"] * (L - box3(L)))[..., 0], 0, 100)
    gx = np.zeros((H, W), np.float32); gy = np.zeros((H, W), np.float32)
    gx[:, 1:] = np.abs(P[:, 1:] - P[:, :-1]).sum(-1); gy[1:] = np.abs(P[1:] - P[:-1]).sum(-1)
    g = np.maximum(np.maximum(gx, np.roll(gx, -1, 1)), np.maximum(gy, np.roll(gy, -1, 0)))
    em = (np.clip((g - o["edge"]) / o["edge"], 0, 1) * o["antialias"])[..., None]
    out = out * (1 - em) + box3(out) * em
    return to_rgb(out), rough, metal, glow, P, same

def clean_normal(nm, P, same, o, sc):
    v = nm[..., :2] * 2 - 1
    dxn, dyn = steps(P, o["normal_smooth"] * sc / (o["edge"] * 3), same)
    lo = rf(v, dxn, dyn, o["normal_smooth"] * sc, 2)
    v = lo + o["ndetail"] * (v - lo)
    l2 = (v ** 2).sum(-1)
    v = np.where((l2 > 0.98)[..., None], v * np.sqrt(0.98 / np.maximum(l2, 1e-6))[..., None], v)
    z = np.sqrt(np.clip(1 - (v ** 2).sum(-1), 0, 1))
    return np.concatenate([v * 0.5 + 0.5, z[..., None] * 0.5 + 0.5], -1)

# ---------------------------------------------------------------------------------------------------------------------------------
# One source, all its pieces
# ---------------------------------------------------------------------------------------------------------------------------------
def load_source(src):
    f = os.path.join(ORIG, src + ".glb")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    objs = [x for x in bpy.data.objects if x.type == "MESH"]
    for x in objs:
        only_select(x); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if len(objs) > 1:
        bpy.ops.object.select_all(action="DESELECT")
        for x in objs: x.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]; bpy.ops.object.join()
    o = objs[0]; o.parent = None
    for x in [x for x in bpy.data.objects if x.type != "MESH"]: bpy.data.objects.remove(x, do_unlink=True)
    bm = bmesh.new(); bm.from_mesh(o.data)
    co = np.array([v.co[:] for v in bm.verts]); ext = float((co.max(0) - co.min(0)).max())
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=ext * 1e-5)
    bm.to_mesh(o.data); bm.free(); o.data.update()
    o.name = "Source"
    return o

def _emit_mat(name, rgb):
    m = bpy.data.materials.new(name); m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (0, 0, 0, 1); p.inputs["Emission Color"].default_value = (*rgb, 1); p.inputs["Emission Strength"].default_value = 1.0
    return m

def _text(body, loc, size, mat, rot=(0, 0, 0)):
    t = bpy.data.curves.new("t", "FONT"); t.body = body; t.size = size; t.align_x = "CENTER"; t.align_y = "CENTER"
    o = bpy.data.objects.new("t", t); o.location = loc; o.rotation_euler = rot
    t.materials.append(mat); bpy.context.scene.collection.objects.link(o)
    return o

def _ortho(lo, hi, view, W, H):
    # an orthographic picture from the top ("top": x right, y up) or the front ("front": looking along +Y, x right, z up)
    sc = scene_setup(W, H, False)
    cam = sc.camera; cam.data.type = "ORTHO"
    c = (lo + hi) / 2; ext = float((hi - lo).max()) * 1.15
    cam.data.ortho_scale = ext; cam.data.clip_end = 100
    if view == "top": cam.location = (c[0], c[1], hi[2] + 5); cam.rotation_euler = (0, 0, 0)
    else: cam.location = (c[0], lo[1] - 5, c[2]); cam.rotation_euler = (math.radians(90), 0, 0)
    p = os.path.join(TMP, "_view_%d.png" % os.getpid()); sc.render.filepath = p
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(p); a = img_get(im)[::-1, :, :3].copy(); bpy.data.images.remove(im); os.remove(p)
    cam.data.type = "PERSP"
    return a

def survey(src):
    # prints the loose parts and clusters, and draws the source: three-quarter view with the cluster numbers, then the top and the
    # front with a 0.1 grid (numbers every 0.2) in the source's own frame, for "box" rules (Docs/Filler/Survey/<src>.jpg)
    o = load_source(src)
    me = o.data; co = verts_np(me)
    comp = components(me); ext = float((co.max(0) - co.min(0)).max())
    clus = clusters_of(co, comp, 0.01 * ext)
    print("FILLER survey %s: %d triangles, %d loose parts, %d clusters, box %s .. %s" % (src, tris_of(me), comp.max() + 1, clus.max() + 1,
          np.round(co.min(0), 2), np.round(co.max(0), 2)))
    for kind, lab in (("comp", comp), ("cluster", clus)):
        for i in range(min(lab.max() + 1, 30)):
            p = co[lab == i]
            print("FILLER   %s %d: %d verts, centre %s, lo %s, hi %s" % (kind, i, len(p), np.round((p.min(0) + p.max(0)) / 2, 2), np.round(p.min(0), 2), np.round(p.max(0), 2)))
    lo_, hi_ = co.min(0), co.max(0)
    red = _emit_mat("red", (1, 0.05, 0.05)); yel = _emit_mat("yel", (1, 0.9, 0.1))
    labels = []
    for i in range(min(clus.max() + 1, 40)):
        p = co[clus == i]; c = (p.min(0) + p.max(0)) / 2
        labels.append(_text(str(i), (c[0], c[1], p.max(0)[2] + 0.04 * ext), 0.12 * ext, red, (math.radians(60), 0, math.radians(-35))))
    shots = [render_view([o], (lo_, hi_), -35, 30, 700, 700, ground=False)]
    for t in labels: bpy.data.objects.remove(t, do_unlink=True)
    # the grids
    grid = []
    step = 0.1 if ext < 3 else 0.5
    g0 = np.floor(lo_ / step) * step; g1 = np.ceil(hi_ / step) * step
    def line(a_, b_, mat):
        bpy.ops.mesh.primitive_cube_add(size=1); l = bpy.context.active_object
        l.location = (np.array(a_) + np.array(b_)) / 2; l.scale = np.maximum(np.abs(np.array(b_) - np.array(a_)), 0.002 * ext)
        l.data.materials.append(mat); grid.append(l)
    for x in np.arange(g0[0], g1[0] + 1e-6, step):
        line((x, g0[1], hi_[2] + 0.01), (x, g1[1], hi_[2] + 0.01), yel)
        if abs(round(x / step) % 2) < 0.5: grid.append(_text("%.1f" % x, (x, g0[1] - 0.04 * ext, hi_[2] + 0.01), 0.035 * ext, yel))
    for y in np.arange(g0[1], g1[1] + 1e-6, step):
        line((g0[0], y, hi_[2] + 0.01), (g1[0], y, hi_[2] + 0.01), yel)
        if abs(round(y / step) % 2) < 0.5: grid.append(_text("%.1f" % y, (g0[0] - 0.06 * ext, y, hi_[2] + 0.01), 0.035 * ext, yel))
    shots.append(_ortho(np.minimum(lo_, g0) - 0.08 * ext, np.maximum(hi_, g1), "top", 700, 700))
    for g in grid: bpy.data.objects.remove(g, do_unlink=True)
    grid = []
    for x in np.arange(g0[0], g1[0] + 1e-6, step):
        line((x, lo_[1] - 0.01, g0[2]), (x, lo_[1] - 0.01, g1[2]), yel)
        if abs(round(x / step) % 2) < 0.5: grid.append(_text("%.1f" % x, (x, lo_[1] - 0.01, g0[2] - 0.04 * ext), 0.035 * ext, yel, (math.radians(90), 0, 0)))
    for z in np.arange(g0[2], g1[2] + 1e-6, step):
        line((g0[0], lo_[1] - 0.01, z), (g1[0], lo_[1] - 0.01, z), yel)
        if abs(round(z / step) % 2) < 0.5: grid.append(_text("%.1f" % z, (g0[0] - 0.06 * ext, lo_[1] - 0.01, z), 0.035 * ext, yel, (math.radians(90), 0, 0)))
    shots.append(_ortho(np.minimum(lo_, g0) - 0.08 * ext, np.maximum(hi_, g1), "front", 700, 700))
    os.makedirs(os.path.join(DOCS, "Survey"), exist_ok=True)
    save_jpg(np.concatenate(shots, 1), os.path.join(DOCS, "Survey", src + ".jpg"))

def build_source(src, names):
    o_src = load_source(src)
    o_src.hide_render = True       # (the whole source stays in the scene for the next piece, but must not shade this one's bakes)
    me = o_src.data; co = verts_np(me)
    comp = components(me); ext = float((co.max(0) - co.min(0)).max())
    clus = clusters_of(co, comp, 0.01 * ext)
    for name in names:
        t0 = time.time()
        P = dict(DEFAULTS); P.update(FLAT); P.update(PIECES[name])
        hi = dup(o_src, name + "_hi")
        keep = np.zeros(len(me.polygons), bool)
        for r in P["take"]: keep |= face_select(me, r, comp, clus, co)
        for r in P["drop"]: keep &= ~face_select(me, r, comp, clus, co)
        # moves (on the full copy, before cutting)
        if P["moves"]:
            hco = verts_np(hi.data)
            for r, d in P["moves"]:
                vm = np.zeros(len(hco), bool)
                fsel = face_select(me, r, comp, clus, co)
                for p in np.nonzero(fsel)[0]: vm[list(me.polygons[int(p)].vertices)] = True
                hco[vm] += np.array(d)
            hi.data.vertices.foreach_set("co", hco.ravel()); hi.data.update()
        keep_faces(hi, keep)
        # specks: loose parts with fewer than speck x the piece's vertices
        c2 = components(hi.data); cnt = np.bincount(c2)
        small = cnt < P["speck"] * cnt.sum()
        if small.any():
            fv = np.array([p.vertices[0] for p in hi.data.polygons])
            keep_faces(hi, ~small[c2[fv]])
            print("FILLER %s specks dropped: %d parts, %d verts" % (name, int(small.sum()), int(cnt[small].sum())))
        if P["fill"]: fill_holes(hi, P["fill"])
        # turn, size, onto the ground
        Rm = np.array(Euler([math.radians(x) for x in P["rot"]], "XYZ").to_matrix())
        hi.rotation_mode = "XYZ"      # (the glTF importer leaves objects in quaternion mode, where rotation_euler does nothing)
        hi.rotation_euler = Euler([math.radians(x) for x in P["rot"]], "XYZ")
        only_select(hi); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        tile = P.get("tile")
        if tile and P.get("remesh"):
            # (a remeshed tile: the source is cut a little long, the voxel skin is cut square at the real ends after, so its
            # smoothing never rounds the ends off into a groove where two copies meet)
            e0 = verts_np(hi.data); m_ = 4 * P["remesh"] * float((e0.max(0) - e0.min(0)).max())
            tile_cut(hi, (tile[0], None if tile[1] is None else tile[1] - m_, None if tile[2] is None else tile[2] + m_))
        elif tile: tile_cut(hi, tile)
        hco = verts_np(hi.data); lo_, hi_ = hco.min(0), hco.max(0); dim = hi_ - lo_
        if P.get("frame"):
            # opt-in "frame": <piece>: sized and placed exactly as that piece of the same source is, so the two line up when both
            # sit at the same spot (a torch's flame over its torch)
            F = dict(DEFAULTS); F.update(PIECES[P["frame"]])
            kf = np.zeros(len(me.polygons), bool)
            for r in F["take"]: kf |= face_select(me, r, comp, clus, co)
            for r in F["drop"]: kf &= ~face_select(me, r, comp, clus, co)
            lt = np.empty(len(me.polygons), np.int64); me.polygons.foreach_get("loop_total", lt)
            lv = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lv)
            fco = co[np.unique(lv[np.repeat(kf, lt)])] @ np.array(Euler([math.radians(x) for x in F["rot"]], "XYZ").to_matrix()).T
            lo_, hi_ = fco.min(0), fco.max(0); dim = hi_ - lo_; P["size"] = F["size"]; P["origin"] = F.get("origin")
        ax, metres = P["size"]
        cur = {"X": dim[0], "Y": dim[1], "Z": dim[2], "L": max(dim[0], dim[1])}[ax]
        s = metres * WORLD / cur
        c3 = np.array([(lo_[0] + hi_[0]) / 2, (lo_[1] + hi_[1]) / 2, hi_[2] if P.get("origin") == "top" else lo_[2]])
        if P.get("origin") == "back": c3[1] = hi_[1]      # (opt-in: origin on the back face, for things hung on a wall: front is -Y)
        hco = (hco - c3) * s
        back = lambda q: (q / s + c3) @ Rm        # (a point of the piece back in the source's frame, for zones; moves not undone)
        hi.data.vertices.foreach_set("co", hco.ravel()); hi.data.update()
        tris0 = tris_of(hi.data)
        # the low model
        lo = dup(hi, name)
        if P.get("remesh"):
            # opt-in "remesh" (share of the piece's size): the low model is a closed voxel skin over the source, smoothed, so loose
            # fibres (hay, straw) and crumpled AI surfaces don't decimate into jagged shards; the fibres come back in the bake
            only_select(lo)
            rm = lo.modifiers.new("Rem", "REMESH"); rm.mode = "VOXEL"; rm.voxel_size = P["remesh"] * float(dim.max()) * s
            rm.use_smooth_shade = True
            bpy.ops.object.modifier_apply(modifier=rm.name)
            sm = lo.modifiers.new("Smo", "CORRECTIVE_SMOOTH"); sm.factor = 0.5; sm.iterations = 8; sm.smooth_type = "SIMPLE"
            sm.use_only_smooth = True
            bpy.ops.object.modifier_apply(modifier=sm.name)
            if tile:
                tax = "XYZ".index(tile[0])
                tile_cut(lo, (tile[0], None if tile[1] is None else (tile[1] - c3[tax]) * s, None if tile[2] is None else (tile[2] - c3[tax]) * s))
            tris0 = tris_of(lo.data)
            print("FILLER %s remeshed: %d triangles" % (name, tris0))
        if tris0 > P["tris"]:
            dm = lo.modifiers.new("Dec", "DECIMATE"); dm.decimate_type = "COLLAPSE"; dm.ratio = P["tris"] / float(tris0)
            dm.use_collapse_triangulate = True
            if P["sym"]: dm.use_symmetry = True; dm.symmetry_axis = P["sym"]
            only_select(lo); bpy.ops.object.modifier_apply(modifier=dm.name)
            for _ in range(3):
                # (from millions of triangles one collapse pass can stop well short of the budget: go again)
                if tris_of(lo.data) <= 1.1 * P["tris"]: break
                dm = lo.modifiers.new("Dec", "DECIMATE"); dm.decimate_type = "COLLAPSE"; dm.ratio = P["tris"] / float(tris_of(lo.data))
                dm.use_collapse_triangulate = True
                if P["sym"]: dm.use_symmetry = True; dm.symmetry_axis = P["sym"]
                bpy.ops.object.modifier_apply(modifier=dm.name)
            if P.get("tile"):
                tax = "XYZ".index(P["tile"][0])
                if P.get("remesh"): e0, e1 = [None if v is None else (v - c3[tax]) * s for v in P["tile"][1:3]]
                else: e0, e1 = (float(hco[:, tax].min()), float(hco[:, tax].max()))
                if P["tile"][1] is None: e0 = None
                if P["tile"][2] is None: e1 = None
                ends = [e for e in (e0, e1) if e is not None]
                tile_snap(lo, tax, ends, 0.004 * float(dim.max()) * s)
                lco = verts_np(lo.data)[:, tax]
                print("FILLER %s tiles along %s: segment %.3f m in the file (%.3f m real)" % (name, P["tile"][0],
                      float(lco.max() - lco.min()), float(lco.max() - lco.min()) / WORLD))
        elif P["subdiv"]:
            sd = lo.modifiers.new("Sub", "SUBSURF"); sd.levels = P["subdiv"]; sd.render_levels = P["subdiv"]
            only_select(lo); bpy.ops.object.modifier_apply(modifier=sd.name)
            if P.get("recap") and tris_of(lo.data) > 1.1 * P["tris"]:
                # (opt-in "recap": smoothed past the budget, back down to it, keeping the rounder shape)
                dm = lo.modifiers.new("Dec", "DECIMATE"); dm.decimate_type = "COLLAPSE"; dm.ratio = P["tris"] / float(tris_of(lo.data))
                dm.use_collapse_triangulate = True
                bpy.ops.object.modifier_apply(modifier=dm.name)
        only_select(lo)
        try: bpy.ops.mesh.customdata_custom_splitnormals_clear()
        except Exception: pass
        lo.data.shade_smooth(); lo.data.set_sharp_from_angle(angle=math.radians(P["sharp"]))
        if P["subdiv"] and tris0 <= P["tris"]:
            # the low-poly source is the bake's source too: smooth its shading so the bake carries no facets
            hi.data.shade_smooth()
        size = P["tex"]
        got = bake(lo, hi, size)
        # zones: which faces of the low model fall in each zone rule (by their centres, in the source's frame)
        fz = np.zeros(len(lo.data.polygons), np.int32)
        if P["zones"]:
            fc = np.empty(len(lo.data.polygons) * 3); lo.data.polygons.foreach_get("center", fc); fc = back(fc.reshape(-1, 3))
            for zi, (rule, *_m) in enumerate(P["zones"]):
                fz[point_select(fc, rule, comp, clus, co)] = zi + 1
            print("FILLER %s zones: %s faces" % (name, np.bincount(fz, minlength=len(P["zones"]) + 1).tolist()))
        ids, used, zid = island_map(lo.data, size, size, fz)
        rgb, rough, metal, glow, Pl, same = recolour(name, got["Base"], got.get("Metal"), got.get("AO"), ids, used, zid, P)
        nrm = clean_normal(got["Normal"], Pl, same, P, size / 1024.0)
        if P.get("plain"): rgb, nrm = plain_zones(name, lo, back, P, rgb, nrm, size, comp, clus, co)
        # material(s): Body, and Glow for faces mostly painted glow
        mat = make_material(name, rgb, nrm, rough, metal, emissive=False, two_sided=P["two_sided"])
        me_lo = lo.data; me_lo.materials.clear(); me_lo.materials.append(mat)
        if (glow > 0.5).any():
            old = bpy.data.materials.get("Glow")     # (an earlier piece's in the same run: move it aside so this slot is exactly "Glow")
            if old: old.name = "Glow_done"
            gm = make_material(name + "_Glow", rgb, nrm, rough, metal, emissive=True); gm.name = "Glow"
            me_lo.materials.append(gm)
            uvd = me_lo.uv_layers.active.data
            for p in me_lo.polygons:
                u = np.mean([uvd[li].uv[:] for li in p.loop_indices], 0)
                x = min(size - 1, max(0, int(u[0] * size))); y = min(size - 1, max(0, int(u[1] * size)))
                if glow[y, x] > 0.5: p.material_index = 1
        if P.get("cloth"):
            # opt-in "cloth": [material, ...] or "all": those faces go to their own two-sided slot named Cloth (the game's wind cloth)
            Wm = DEBUG_MATS.get(name, {})
            cw = np.ones((size, size), np.float32) if P["cloth"] == "all" else sum((Wm[m] for m in P["cloth"] if m in Wm), np.zeros((size, size), np.float32))
            old = bpy.data.materials.get("Cloth")
            if old: old.name = "Cloth_done"
            cm = make_material(name + "_Cloth", rgb, nrm, rough, metal, emissive=False, two_sided=True); cm.name = "Cloth"
            me_lo.materials.append(cm); ci = len(me_lo.materials) - 1
            uvd = me_lo.uv_layers.active.data; nc = 0
            for p in me_lo.polygons:
                if p.material_index != 0: continue
                u = np.mean([uvd[li].uv[:] for li in p.loop_indices], 0)
                x = min(size - 1, max(0, int(u[0] * size))); y = min(size - 1, max(0, int(u[1] * size)))
                if cw[y, x] > 0.5: p.material_index = ci; nc += 1
            print("FILLER %s cloth slot: %d of %d faces" % (name, nc, len(me_lo.polygons)))
        bpy.data.objects.remove(hi, do_unlink=True)
        if DEBUG: debug_render(name, lo, size)
        lco = verts_np(lo.data); d = lco.max(0) - lco.min(0)
        os.makedirs(CLEAN, exist_ok=True)
        only_select(lo)
        out = os.path.join(CLEAN, name + ".glb")
        bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_image_format="JPEG", export_jpeg_quality=90,
                                  export_yup=True, export_apply=True, export_normals=True, export_tangents=False, export_materials="EXPORT")
        print("FILLER %s tris=%d size=%.2fx%.2fx%.2fm tex=%d (from %d triangles; %.0f s; %.1f MB)" % (
            name, tris_of(lo.data), d[0], d[1], d[2], size, tris0, time.time() - t0, os.path.getsize(out) / 1e6))
        bpy.data.objects.remove(lo, do_unlink=True)
        for im in list(bpy.data.images):
            if im.users == 0: bpy.data.images.remove(im)

def make_material(name, rgb, nrm, rough, metal, emissive, two_sided=False):
    mat = bpy.data.materials.new(name); mat.use_nodes = True
    mat.use_backface_culling = not two_sided      # (glTF doubleSided: only for thin cloth, flaps and leaf cards)
    nt = mat.node_tree; pr = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    tb = nt.nodes.new("ShaderNodeTexImage"); tb.image = img_new("T_%s_Base" % name, rgb)
    nt.links.new(tb.outputs["Color"], pr.inputs["Base Color"])
    tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = img_new("T_%s_Normal" % name, nrm, True)
    nm = nt.nodes.new("ShaderNodeNormalMap"); nt.links.new(tn.outputs["Color"], nm.inputs["Color"]); nt.links.new(nm.outputs[0], pr.inputs["Normal"])
    H, W = rough.shape
    mr = np.zeros((H, W, 3), np.float32); mr[..., 1] = rough; mr[..., 2] = metal
    small = mr.reshape(H // 2, 2, W // 2, 2, 3).mean((1, 3))
    tm = nt.nodes.new("ShaderNodeTexImage"); tm.image = img_new("T_%s_MR" % name, small, True)
    sp = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(tm.outputs["Color"], sp.inputs["Color"])
    nt.links.new(sp.outputs[1], pr.inputs["Roughness"]); nt.links.new(sp.outputs[2], pr.inputs["Metallic"])
    if emissive:
        nt.links.new(tb.outputs["Color"], pr.inputs["Emission Color"]); pr.inputs["Emission Strength"].default_value = 1.0
    return mat

DEBUG_MATS = {}
DEBUG_COL = {"wood": (0.45, 0.2, 0.05), "iron": (0.1, 0.3, 0.9), "burlap": (0.95, 0.85, 0.2), "rope": (0.9, 0.6, 0.9), "leather": (0.4, 0.0, 0.4),
             "clay": (0.9, 0.3, 0.1), "straw": (1.0, 1.0, 0.5), "grain": (1.0, 0.6, 0.0), "leaf": (0.1, 0.8, 0.1), "stone": (0.6, 0.6, 0.6),
             "plaster": (1.0, 1.0, 1.0), "keep": (1.0, 0.0, 1.0), "glow": (1.0, 1.0, 0.0),
             "bark": (0.15, 0.05, 0.0), "woodcut": (1.0, 0.75, 0.5), "soil": (0.1, 0.1, 0.1), "moss": (0.3, 0.5, 0.0)}

def debug_render(name, lo, size):
    # --debug: the piece painted in its materials' flat colours (wood brown, iron blue, burlap yellow...), Docs/Filler/Debug/<Piece>.jpg
    wm = DEBUG_MATS.get(name)
    if not wm: return
    arr = np.zeros((size, size, 3), np.float32)
    for m, w in wm.items(): arr += np.array(DEBUG_COL.get(m, (0.5, 0.5, 0.5)), np.float32) * w[..., None]
    im = img_new("Dbg_" + name, arr)
    mat = bpy.data.materials.new("Dbg"); mat.use_nodes = True
    nt = mat.node_tree; pr = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = im; nt.links.new(t.outputs["Color"], pr.inputs["Base Color"])
    keep = list(lo.data.materials); lo.data.materials.clear(); lo.data.materials.append(mat)
    hidden = [x for x in bpy.context.scene.objects if x != lo and x.type == "MESH" and not x.hide_render]
    for x in hidden: x.hide_render = True
    eng = bpy.context.scene.render.engine
    lco = verts_np(lo.data)
    shots = [render_view([lo], (lco.min(0), lco.max(0)), yaw, pit, 500, 500, ground=False) for yaw, pit in ((-35, 25), (145, 25), (0, 89))]
    bpy.context.scene.render.engine = eng
    lo.data.materials.clear()
    for m in keep: lo.data.materials.append(m)
    for x in hidden: x.hide_render = False
    os.makedirs(os.path.join(DOCS, "Debug"), exist_ok=True)
    save_jpg(np.concatenate(shots, 1), os.path.join(DOCS, "Debug", name + ".jpg"))

# ---------------------------------------------------------------------------------------------------------------------------------
# Pictures for the owner: contact sheets by family, and before/after pairs
# ---------------------------------------------------------------------------------------------------------------------------------
def scene_setup(W, H, ground=True):
    sc = bpy.context.scene
    try: sc.render.engine = "BLENDER_EEVEE"
    except Exception: sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x = W; sc.render.resolution_y = H; sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = "Standard"; sc.view_settings.look = "None"
    w = sc.world or bpy.data.worlds.new("w"); sc.world = w
    w.use_nodes = True; bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 0.55
    if "Sun" not in bpy.data.objects:
        sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN")); sun.data.energy = 2.8; sun.data.angle = math.radians(8)
        sun.rotation_euler = (math.radians(45), 0, math.radians(-50)); sc.collection.objects.link(sun)   # from the upper left of the view
    if ground and "Ground" not in bpy.data.objects:
        bpy.ops.mesh.primitive_plane_add(size=200); g = bpy.context.active_object; g.name = "Ground"
        gm = bpy.data.materials.new("GroundM"); gm.use_nodes = True
        p = next(n for n in gm.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        p.inputs["Base Color"].default_value = (0.21, 0.21, 0.21, 1); p.inputs["Roughness"].default_value = 0.9
        g.data.materials.append(gm)
    if "Cam" not in bpy.data.objects:
        cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam
    return sc

def aim(cam, lo, hi, yaw, pitch, aspect, lens=50.0, pad=1.08):
    c = (np.array(lo) + np.array(hi)) / 2
    d = np.array([math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)), -math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch))])
    cam.data.lens = lens
    rot = Vector(-d).to_track_quat("-Z", "Y")
    cam.rotation_euler = rot.to_euler()
    # distance so the box fits: project its corners on the camera's right and up
    R = rot.to_matrix(); right = np.array(R.col[0]); up = np.array(R.col[1])
    corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]) - c
    half_w = np.abs(corners @ right).max() * pad; half_h = np.abs(corners @ up).max() * pad
    depth = np.abs(corners @ d).max()
    f = cam.data.lens / cam.data.sensor_width     # (sensor fits the width)
    dist_w = half_w * 2 * f; dist_h = half_h * 2 * f * aspect
    cam.location = Vector(c + d * (max(dist_w, dist_h) + depth))
    cam.data.clip_end = 1000

def render_view(objs, box, yaw, pitch, W, H, ground=True):
    sc = scene_setup(W, H, ground)
    aim(sc.camera, box[0], box[1], yaw, pitch, W / H)
    p = os.path.join(TMP, "_view_%d.png" % os.getpid()); sc.render.filepath = p
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(p); a = img_get(im)[::-1, :, :3].copy(); bpy.data.images.remove(im); os.remove(p)
    return a

def figure():
    # a 1.8 m person (drawn at the game's 1.2x, like the knight), plain grey: body, head
    mat = bpy.data.materials.new("Fig"); mat.use_nodes = True
    next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Base Color"].default_value = (0.32, 0.36, 0.45, 1)
    parts = []
    h = 1.8 * WORLD
    for loc, rad, depth in (((0, 0, 0.42 * h), 0.16 * WORLD, 0.84 * h), ((0, 0, 0.62 * h), 0.2 * WORLD, 0.3 * h)):
        bpy.ops.mesh.primitive_cylinder_add(radius=rad, depth=depth, location=loc, vertices=24); parts.append(bpy.context.active_object)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.11 * WORLD, location=(0, 0, h - 0.11 * WORLD)); parts.append(bpy.context.active_object)
    for p in parts: p.data.materials.append(mat); p.data.shade_smooth()
    return parts

def label(text, x, y, size):
    t = bpy.data.curves.new("L", "FONT"); t.body = text; t.size = size; t.align_x = "CENTER"; t.align_y = "TOP"
    o = bpy.data.objects.new("L", t); o.location = (x, y, 0.005); bpy.context.scene.collection.objects.link(o)
    m = bpy.data.materials.new("Lm"); m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (0, 0, 0, 1); p.inputs["Emission Color"].default_value = (0.95, 0.95, 0.95, 1); p.inputs["Emission Strength"].default_value = 1.0
    t.materials.append(m)
    return o

SHEET_YAW, SHEET_PITCH = -18.0, 30.0

def sheets(only=None):
    # one picture per family, orthographic so sizes compare truly: the pieces on shelves of rows (shortest in front), the 1.8 m figure
    # (at the game's 1.2x) at the front left, names on the ground in front of each piece
    have = {n for n in PIECES if os.path.exists(os.path.join(CLEAN, n + ".glb"))}
    for fam, title in FAMILIES:
        if only and fam not in only: continue
        names = [n for n in PIECES if PIECES[n].get("fam") == fam and n in have]
        if not names: continue
        bpy.ops.wm.read_factory_settings(use_empty=True)
        W_, H_ = 1800, 1100
        sc = scene_setup(W_, H_)
        figs = figure()
        items = []
        for n in names:
            before = set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=os.path.join(CLEAN, n + ".glb"))
            objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
            co = np.concatenate([verts_np(o.data) @ np.array(o.matrix_world)[:3, :3].T + np.array(o.matrix_world)[:3, 3] for o in objs])
            items.append((n, objs, co.min(0), co.max(0)))
        items.sort(key=lambda t: t[3][2] - t[2][2])
        total = sum(t[3][0] - t[2][0] + 0.6 for t in items) + 1.0
        rowW = max(4.0, math.sqrt(total * 3.0))
        cot = math.cos(math.radians(SHEET_PITCH)) / math.sin(math.radians(SHEET_PITCH))
        x, y, depth, tall = 1.0, 0.0, 0.0, 1.8 * WORLD
        pts = [np.array([[0, 0, 0], [0, 0, 1.8 * WORLD]])]
        for n, objs, lo, hi in items:
            w = hi[0] - lo[0]; dd = hi[1] - lo[1]
            if x + w > rowW and x > 1.0:
                y += depth + 0.45 + 0.8 * tall * cot; x = 0.4; depth = 0.0; tall = 0.0
            for o in objs:
                o.location.x += x - lo[0]; o.location.y += y - lo[1]; o.location.z += max(0.0, -lo[2])     # (pieces hung from their top edge stand on the ground here)
            label(n, x + w / 2, y - 0.08, 0.17)
            pts.append(np.array([[x, y - 0.35, 0], [x + w, y + dd, hi[2] - lo[2]]]))
            x += w + 0.6; depth = max(depth, dd); tall = max(tall, hi[2] - lo[2])
        # orthographic camera fitted to everything
        P = np.concatenate([np.array([[a[0], b[1], c[2]] for a in p_ for b in p_ for c in p_]) for p_ in pts])
        cam = sc.camera; cam.data.type = "ORTHO"
        d = np.array([math.sin(math.radians(SHEET_YAW)) * math.cos(math.radians(SHEET_PITCH)), -math.cos(math.radians(SHEET_YAW)) * math.cos(math.radians(SHEET_PITCH)), math.sin(math.radians(SHEET_PITCH))])
        rot = Vector(-d).to_track_quat("-Z", "Y"); cam.rotation_euler = rot.to_euler()
        R = rot.to_matrix(); right = np.array(R.col[0]); up = np.array(R.col[1])
        u = P @ right; v = P @ up
        cu, cv = (u.min() + u.max()) / 2, (v.min() + v.max()) / 2
        cam.data.ortho_scale = max(u.max() - u.min(), (v.max() - v.min()) * W_ / H_) * 1.08
        cam.location = Vector(right * cu + up * cv + d * 60.0 - d * float((P @ d).mean()) + d * float((P @ d).mean()))
        cam.data.clip_end = 200
        p = os.path.join(TMP, "_sheet_%d.png" % os.getpid()); sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        im = bpy.data.images.load(p); a = img_get(im)[::-1, :, :3].copy(); bpy.data.images.remove(im); os.remove(p)
        os.makedirs(DOCS, exist_ok=True)
        out = os.path.join(DOCS, "Sheet_%s.jpg" % fam)
        save_jpg(a, out, 85)
        print("FILLER sheet %s: %d pieces (%s)" % (out, len(names), ", ".join(names)))

def pairs(names):
    # the original (scaled to the same size) beside the cleaned piece, from two sides
    for n in names:
        P = dict(DEFAULTS); P.update(PIECES[n])
        rows = []
        for which in ("orig", "clean"):
            bpy.ops.wm.read_factory_settings(use_empty=True)
            scene_setup(700, 700)
            before = set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=os.path.join(ORIG if which == "orig" else CLEAN, (P["src"] if which == "orig" else n) + ".glb"))
            objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
            co = np.concatenate([verts_np(o.data) @ np.array(o.matrix_world)[:3, :3].T + np.array(o.matrix_world)[:3, 3] for o in objs])
            lo, hi = co.min(0), co.max(0)
            if which == "orig":
                # (stand it on the ground at the clean one's height)
                ref = os.path.join(CLEAN, n + ".glb")
                s = 1.0
                for o in objs: o.location -= Vector(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]))
                lo, hi = lo - np.array(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2])), hi - np.array(((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]))
            elif lo[2] < 0:
                # (pieces hung from their top edge stand on the ground here too)
                for o in objs: o.location.z -= lo[2]
                hi = hi - np.array((0, 0, lo[2])); lo = lo - np.array((0, 0, lo[2]))
            views = [render_view(objs, (lo, hi), yaw, 25, 700, 700) for yaw in (-35, 145)]
            rows.append(np.concatenate(views, 1))
        out = os.path.join(DOCS, "Pair_%s.jpg" % n)
        save_jpg(np.concatenate(rows, 0), out, 85)
        print("FILLER pair", out)

REF_HOUSE = os.path.join(PROJ, "Art", "Buildings", "OakHillHouse_300k.glb")   # (read only: Meshy's colours, before the game's repaint)

def _import(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objs = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    bpy.context.view_layer.update()
    for o in objs:
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    bpy.context.view_layer.update()
    co = np.concatenate([verts_np(o.data) @ np.array(o.matrix_world)[:3, :3].T + np.array(o.matrix_world)[:3, 3] for o in objs])
    return objs, co.min(0), co.max(0)

def build_combo(name, P):
    # opt-in "combo": [(piece, (x, y, z), width, turn), ...]: cleaned pieces put together into one file (a banner hung on a pole),
    # each moved to (x, y, z) in the file's metres, scaled to "width" across X (None: as it is) and turned "turn" degrees about Z.
    # Their materials (and so their texture sets) stay their own; no new bake.
    t0 = time.time()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = []
    for piece, loc, width, turn in P["combo"]:
        objs, lo, hi = _import(os.path.join(CLEAN, piece + ".glb"))
        s = 1.0 if not width else width / float(hi[0] - lo[0])
        M = Matrix.Translation(Vector(loc)) @ Matrix.Rotation(math.radians(turn), 4, "Z") @ Matrix.Scale(s, 4)
        for o in objs:
            o.matrix_world = M @ o.matrix_world
        parts += objs
    bpy.context.view_layer.update()
    for o in parts:
        only_select(o); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts: o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]; bpy.ops.object.join()
    o = parts[0]; o.name = name
    lco = verts_np(o.data); d = lco.max(0) - lco.min(0)
    out = os.path.join(CLEAN, name + ".glb")
    only_select(o)
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_image_format="JPEG", export_jpeg_quality=90,
                              export_yup=True, export_apply=True, export_normals=True, export_tangents=False, export_materials="EXPORT")
    print("FILLER %s tris=%d size=%.2fx%.2fx%.2fm (put together from %s; %.0f s; %.1f MB; materials %s)" % (
        name, tris_of(o.data), d[0], d[1], d[2], ", ".join(c[0] for c in P["combo"]), time.time() - t0, os.path.getsize(out) / 1e6,
        ", ".join(m.name for m in o.data.materials)))

def compare(title, names):
    # --compare <Title> <Piece> ...: the candidates side by side (Docs/Filler/Compare_<Title>.jpg), each tiling piece as two copies end
    # to end, in rows in front of an Oak Hill house at the game's size (14.4 m long; its lower front shows, for colour and scale), with
    # the palette's swatches and the 1.8 m figure; plus a close look at each tiling piece's seam (scratch, in TMP)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    W_, H_ = 2000, 1150
    sc = scene_setup(W_, H_)
    items = []
    for n in names:
        if n.startswith("@"):
            # (opt-in "@<path under the project>=<metres>": a reference model, e.g. the town's current well, scaled so its longer
            # footprint side is that many metres, labelled with its file name)
            pth, ln = n[1:].split("=")
            objs, lo, hi = _import(os.path.join(PROJ, pth))
            s = float(ln) / max(hi[0] - lo[0], hi[1] - lo[1])
            for o in objs: o.matrix_world = Matrix.Scale(s, 4) @ o.matrix_world
            lo, hi = lo * s, hi * s
            items.append((os.path.splitext(os.path.basename(pth))[0], objs, lo, hi, 1)); continue
        objs, lo, hi = _import(os.path.join(CLEAN, n + ".glb"))
        t = PIECES[n].get("tile")
        k = 2 if (t and t[1] is not None and t[2] is not None) else 1
        items.append((n, objs, lo, hi, k))
    items.sort(key=lambda it: it[3][2] - it[2][2])      # (shortest in front, so no label hides behind a taller piece)
    tall = max(it[3][2] - it[2][2] for it in items)
    lab = max(0.12, 0.09 * tall)
    rowW = max(8.0, min(18.0, sum(max((it[3][0] - it[2][0]) * it[4], 0.62 * lab * len(it[0])) + 0.8 for it in items) / 2.0))
    lab = max(lab, 0.014 * rowW)
    figs = figure()
    for f in figs: f.location.x -= 1.0
    pts = [np.array([[-1.3, -0.4, 0], [-0.7, 0.4, 1.8 * WORLD]])]
    x = y = depth = 0.0
    seams = []
    rowT = 0.0
    for n, objs, lo, hi, k in items:
        w = hi[0] - lo[0]; dd = hi[1] - lo[1]
        slot = max(k * w, 0.62 * lab * len(n))       # (room for the label)
        if x > 0 and (x + slot > rowW or (hi[2] - lo[2]) > 2.2 * rowT):     # (a much taller piece starts its own row)
            y += depth + 0.6 + 2.3 * rowT; x = 0.0; depth = 0.0; rowT = 0.0     # (clear of the row in front, seen from 24 degrees up)
        x += (slot - k * w) / 2
        for o in objs: o.location.x += x - lo[0]; o.location.y += y - lo[1]; o.location.z += max(0.0, -lo[2])
        for i in range(1, k):
            for o in objs:
                c = o.copy(); sc.collection.objects.link(c); c.location.x += i * w
        if k == 2: seams.append((n, x + w, y + dd / 2, dd, hi[2] - lo[2]))
        label(n, x + k * w / 2, y - 0.1, lab)
        pts.append(np.array([[x - (slot - k * w) / 2, y - 0.25 - 2 * lab, 0], [x + k * w + (slot - k * w) / 2, y + dd, hi[2] - lo[2]]]))
        x += k * w + (slot - k * w) / 2 + 0.8; depth = max(depth, dd); rowT = max(rowT, hi[2] - lo[2])
    # the palette's swatches, left of the figure
    sw = 0.3
    x0 = -1.6 - 4 * 1.5 * sw
    for i, m in enumerate(["stone", "wood", "iron", "plaster", "leaf", "rope", "cloth_blue", "cloth_red"]):
        bpy.ops.mesh.primitive_cube_add(size=sw, location=(x0 + (i % 4) * 1.5 * sw, sw / 2 + (i // 4) * 1.8 * sw, sw / 2))
        cb = bpy.context.active_object
        mt = bpy.data.materials.new("Sw_" + m); mt.use_nodes = True
        pr = next(nn for nn in mt.node_tree.nodes if nn.type == "BSDF_PRINCIPLED")
        rgb = to_rgb(np.array(PALETTE[m][0], np.float32)[None, None])[0, 0]
        lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
        pr.inputs["Base Color"].default_value = (*lin, 1); pr.inputs["Roughness"].default_value = PALETTE[m][1]
        cb.data.materials.append(mt)
    pts.append(np.array([[x0 - sw, 0, 0], [x0 + 6 * sw, 3.6 * sw, sw]]))
    # the house behind the last row, at the game's size; only its lower front is framed
    if os.path.exists(REF_HOUSE):
        hob, hlo, hhi = _import(REF_HOUSE)
        sz = hhi - hlo; s = 14.4 / max(sz[0], sz[1])
        for o in hob:
            o.scale = o.scale * s; o.location = (o.location - Vector(((hlo[0] + hhi[0]) / 2, (hlo[1] + hhi[1]) / 2, hlo[2]))) * s
        bpy.context.view_layer.update()
        hd = sz[1] * s
        hx = rowW / 2; hy = y + depth + 1.5 + hd / 2
        for o in hob: o.location.x += hx; o.location.y += hy
        pts.append(np.array([[hx - 5.0, hy - hd / 2, 0], [hx + 5.0, hy - hd / 2 + 0.5, min(3.2, sz[2] * s)]]))
    cam = sc.camera; cam.data.type = "ORTHO"
    P_ = np.concatenate([np.array([[a[0], b[1], c[2]] for a in p_ for b in p_ for c in p_]) for p_ in pts])
    yaw, pitch = -15.0, 24.0
    d = np.array([math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)), -math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch))])
    rot = Vector(-d).to_track_quat("-Z", "Y"); cam.rotation_euler = rot.to_euler()
    R = rot.to_matrix(); right = np.array(R.col[0]); up = np.array(R.col[1])
    u = P_ @ right; v = P_ @ up
    cam.data.ortho_scale = max(u.max() - u.min(), (v.max() - v.min()) * W_ / H_) * 1.04
    cam.location = Vector(right * (u.min() + u.max()) / 2 + up * (v.min() + v.max()) / 2 + d * 80.0)
    cam.data.clip_end = 300
    p = os.path.join(TMP, "_cmp_%d.png" % os.getpid()); sc.render.filepath = p
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(p); a = img_get(im)[::-1, :, :3].copy(); bpy.data.images.remove(im); os.remove(p)
    out = os.path.join(DOCS, "Compare_%s.jpg" % title)
    save_jpg(a, out, 82)
    print("FILLER compare", out)
    # each seam close up, from the front and from above at an angle (scratch)
    for n, sx, sy, dep, ht in seams:
        cam.data.type = "PERSP"
        shots = []
        for yaw_, pit_ in ((-30, 15), (30, 40)):
            sc.render.resolution_x = 700; sc.render.resolution_y = 700
            aim(cam, (sx - 0.6 * ht, sy - dep / 2, 0), (sx + 0.6 * ht, sy + dep / 2, ht), yaw_, pit_, 1.0)
            sc.render.filepath = p; bpy.ops.render.render(write_still=True)
            im = bpy.data.images.load(p); shots.append(img_get(im)[::-1, :, :3].copy()); bpy.data.images.remove(im); os.remove(p)
        save_jpg(np.concatenate(shots, 1), os.path.join(TMP, "Seam_%s.jpg" % n), 85)
        print("FILLER seam", os.path.join(TMP, "Seam_%s.jpg" % n))

# ---------------------------------------------------------------------------------------------------------------------------------
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
import tempfile
TMP = os.environ.get("FILLER_TMP") or os.path.join(tempfile.gettempdir(), "jeeb_filler")
os.makedirs(TMP, exist_ok=True)
mode, names, DEBUG = "build", [], False
for x in args:
    if x == "--debug": DEBUG = True
    elif x in ("--survey", "--sheets", "--pairs", "--compare"): mode = x[2:]
    else: names.append(x)
T0 = time.time()
if mode == "survey":
    for s in names: survey(s)
elif mode == "sheets":
    sheets(names or None)
elif mode == "pairs":
    pairs(names)
elif mode == "compare":
    compare(names[0], names[1:])
else:
    todo = names or list(PIECES)
    bad = [n for n in todo if n not in PIECES]
    if bad: raise SystemExit("FILLER unknown pieces: " + ", ".join(bad))
    by_src = {}
    combos = [n for n in todo if "combo" in PIECES[n]]
    for n in todo:
        if n in combos: continue
        src = PIECES[n]["src"]
        if not os.path.exists(os.path.join(ORIG, src + ".glb")): print("FILLER missing source", src, "for", n); continue
        by_src.setdefault(src, []).append(n)
    for src, ns in by_src.items():
        build_source(src, ns)
    for n in combos:          # (after the sources, so they use this run's pieces)
        build_combo(n, PIECES[n])
print("FILLER done in %.0f s" % (time.time() - T0))
