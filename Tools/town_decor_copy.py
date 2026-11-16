# The library props the game ships (2026-11-14, widened for the fill 2026-11-15, Docs/FillPlan.md step 1b). /Game/Filler is the
# owner's filler lineup: 700 MB of options, git-ignored and never cooked. This copies the kinds the game uses into /Game/TownDecor
# (tracked and cooked): those Oak Hill's rebuilt decor names (Source/Jeebsland/TownLabDecor.h) and those of the fill (CATALOG below),
# each with its pictures, on two parent materials made like Filler's M_Filler / M_FillerTwoSided (Tools/filler_import.py) but
# flagged for instanced meshes (and Nanite), so every kind draws as instanced sets. It also measures each kind into
# Content/Data/FillKinds.csv, which AWorldFill reads (footprint, height, collision, shadow and cull class).
#
# Per kind, from the Nanite vs generated-LODs A/B (FillPlan section 4; numbers in the fillassets report of 2026-11-15):
#   - RENDER[size class] says Nanite or generated LODs (3 levels, the last about 300 triangles). Two-sided kinds (cloth, leaf
#     cards, flower boxes) and the flames are never Nanite (r.Nanite.Foliage would take them, slower).
#   - textures are capped for the package: base and normal 1024 (512 for tiny kinds), metal/roughness 512.
#   - meshes are deleted before they are copied, so no old setting survives; afterwards every mesh is checked (Nanite or its LOD
#     count, every material's instanced flag) and the run FAILS on anything wrong.
#   - TownDecor files no kind uses any more are deleted (the grain sacks went that way); BLOCKED kinds must never come back.
#
# FillKinds.csv: the measured columns are rewritten every run; the authored ones (Collision, Shadow, Cull, Slope, Flags, Where)
# keep whatever the CSV already says for a kind, so tune them in the CSV. A kind is copied when it is in CATALOG or TownLabDecor.h.
# Columns (metres, at scale 1.0; no comment lines, AWorldFill reads it by header): Kind, Mesh, Shape (box|circle), FootX FootY (the plan over
# the lowest metre; a circle's FootX is its diameter), FootCX FootCY (that footprint's centre from the pivot, in the kind's own X/Y), Height,
# Collision (none = walk-through|box|capsule|mesh), Shadow (yes|near|no), Cull (tiny|small|medium|large|landmark), Slope (max degrees), Flags
# (light, hang, part = drawn with another kind, flame=<Kind>), Where (the plan's zone letters), then for reference Render (nanite|lods),
# Tris, LodTris (last LOD), SizeX SizeY (the whole plan), MinZ (the bottom below the pivot: negative for hung pieces), TwoSided.
# Run by Tools/CopyTownDecor.ps1 (windowless editor; this copy's editor closed). Needs the JeebMeshTools editor helper (C++).
import csv
import os
import re
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
DECOR_H = os.path.join(PROJ, "Source", "Jeebsland", "TownLabDecor.h")
CSV_PATH = os.path.join(PROJ, "Content", "Data", "FillKinds.csv")
SRC, DST = "/Game/Filler", "/Game/TownDecor"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
PML = unreal.ProceduralMeshLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

BLOCKED = {"GrainSack", "Sword2", "Sword3", "SwordB", "SwordC"}   # deleted by the owner (2026-11-15): never copied again

# Nanite or generated LODs per size class (the drawn size: the longest side at scale 1.0). The A/B (2026-11-15, 586 pieces round Oak
# Hill, moving sun, solo, editor -game 1920x1080 at 60%): all on LODs cost +0.03 to +0.32 ms GPU on HIGH and +0.10 to +0.27 on LOW
# over no props; all on Nanite +0.24 to +0.74 on HIGH and +0.18 to +0.40 on LOW (with 2 shadow-map cascades each Nanite raster runs
# per cascade). Nanite was never clearly cheaper for any class (medium worst: up to +0.7 ms more than LODs), so every class is LODs.
RENDER = {"tiny": "lods", "small": "lods", "medium": "lods", "large": "lods", "landmark": "lods"}
LOD_PERCENT_1, LOD_SCREEN = 0.35, (1.0, 0.2, 0.05)   # LOD 1 keeps 35%, LOD 2 about 300 triangles; shown from these screen sizes
LOD_LAST_TRIS = 300.0

# The fill's kinds (FillPlan section 1, the owner's decisions of 2026-11-15: Oak Hill, its waiting plots and fringe, Blackrock,
# the market yard, cottage, pig pen, pond and castle; no open-world scatter yet). Per kind: where (the catalog's zone letters),
# collision (solid: a box, or an upright capsule for round pieces; thin: a capsule round the post at its foot; mesh: its own
# shape, for stairs; none: walk-through), shadow (yes, near: only near the player in settlements, no), flags.
# Owner: bushes and small pieces walk-through; crates and hay bales solid and climbable.
CATALOG = {
    # storage
    "Barrel": ("S M Y K C B", "solid", "yes", ""), "BarrelSmall": ("S M Y K", "solid", "near", ""),
    "Crate": ("M Y K C B", "solid", "near", ""), "CrateNoLid": ("M Y", "solid", "near", ""), "CrateOpen": ("M G F", "solid", "near", ""),
    "SackPileOpen": ("M F", "solid", "near", ""), "SackStack": ("M F B", "solid", "near", ""), "SackWall": ("M F B", "solid", "near", ""),
    "SpiceSacks": ("M", "none", "no", ""), "WheatSack": ("M F Y", "none", "no", ""),
    # firewood, rope
    "Firewood": ("Y K", "none", "no", ""), "FirewoodRack": ("Y B", "solid", "yes", ""), "LogPile": ("Y E F", "solid", "yes", ""),
    "LogStack": ("Y E", "solid", "yes", ""), "LogStackSmall": ("Y K", "none", "near", ""),
    "RopeCoil": ("W M C K", "none", "no", ""), "RopeHank": ("W M C K", "none", "no", ""),
    # pots
    "Basket": ("M G Y", "none", "no", ""), "Cauldron": ("K Y", "solid", "yes", ""),
    "ClayJug": ("S M Y", "none", "no", ""), "ClayPitcher": ("S M Y", "none", "no", ""), "PotJug": ("S M Y", "none", "no", ""),
    "PotRound": ("S M Y", "none", "no", ""), "PotSmall": ("S M Y", "none", "no", ""), "PotVase": ("S M Y", "none", "no", ""),
    "PotWide": ("S M Y", "none", "no", ""), "PotAmphora": ("M S", "none", "near", ""),
    # farm
    "ChickenCoop": ("Y F", "solid", "yes", ""), "NestingBoxes": ("Y F", "solid", "yes", ""), "HandCart": ("M R Y", "solid", "yes", ""),
    "HayBale": ("F Y R", "solid", "yes", ""), "HayBaleRound": ("F", "solid", "yes", ""),
    "HayWagon": ("F R", "solid", "yes", ""), "ThatchWagon": ("F R", "solid", "yes", ""),
    "Pail": ("Y G F W", "none", "no", ""), "WateringCan": ("Y G F W", "none", "no", ""), "WoodTub": ("Y G F W", "none", "no", ""),
    # garden
    "RaisedBed": ("G Y", "solid", "near", ""), "TulipBed": ("G Y", "solid", "near", ""), "CabbageCrate": ("G M", "none", "no", ""),
    "VegCart": ("M G", "solid", "yes", ""), "Scarecrow": ("G F", "thin", "yes", ""),
    "ButcherBlock": ("Y K", "solid", "near", ""), "ChoppingBlock": ("Y K", "solid", "near", ""), "Stump": ("Y E", "solid", "near", ""),
    # bushes (walk-through, as in Breath of the Wild)
    "BushBall": ("S Y G E W R", "none", "near", ""), "BushBerries": ("S Y G E W R", "none", "near", ""),
    "BushDome": ("S Y G E W R", "none", "near", ""), "BushLeafy": ("S Y G E W R", "none", "near", ""),
    "BushRound": ("S Y G E W R", "none", "near", ""), "BushSpiky": ("S Y G E W R", "none", "near", ""),
    "BushWhiteFlowers": ("S Y G E W R", "none", "near", ""), "BushYellowFlowers": ("S Y G E W R", "none", "near", ""),
    "ShrubLow": ("S Y E W", "none", "no", ""), "PlantClump": ("S Y E W", "none", "no", ""),
    "HedgeBox": ("S Y G", "solid", "yes", ""), "HedgeSegment": ("S Y G", "solid", "yes", ""), "HedgeEnd": ("S Y G", "solid", "yes", ""),
    "Cypress": ("V G P", "thin", "yes", ""),
    # walls, fences, stairs
    "WallStone": ("Y F V C R", "solid", "yes", ""), "WallStoneEnd": ("Y F V C R", "solid", "yes", ""),
    "WallPillar": ("Y F V C R", "solid", "yes", ""), "WallPillarEnd": ("Y F V C R", "solid", "yes", ""),
    "FencePicket": ("Y G", "solid", "near", ""), "FencePicketEnd": ("Y G", "solid", "near", ""),
    "FenceStick": ("F R E", "solid", "near", ""), "FenceStickPost": ("F R E", "solid", "near", ""),
    "FencePalisade": ("C B K", "solid", "yes", ""),
    "StairsStone": ("Y C", "mesh", "yes", ""), "StairsStoneShort": ("Y C", "mesh", "yes", ""), "StairsWalled": ("Y C", "mesh", "yes", ""),
    # furniture, signs
    "BenchCarved": ("P S V W G", "solid", "near", ""), "BenchStone": ("P S V W G", "solid", "near", ""),
    "TrestleTable": ("M K Y", "solid", "near", ""), "Signpost": ("R S", "thin", "yes", ""), "SignArrow": ("R S", "thin", "yes", ""),
    "SignSkillIssue": ("P", "thin", "yes", ""),
    # lights, awnings, stalls
    "LampPostStone": ("P S R", "thin", "yes", "light"), "LampPostRope": ("P S R", "thin", "yes", "light"),
    "LampPostPillar": ("P S R", "thin", "yes", "light"), "LampPostGallows": ("P S R", "thin", "yes", "light"),
    "LanternBlack": ("S M Y", "none", "no", "light"), "LanternOil": ("S M Y", "none", "no", "light"),
    "CandleCageTall": ("V", "none", "no", "light"), "CandleCageShort": ("V", "none", "no", "light"),
    "AwningStriped": ("M", "none", "yes", "hang"),
    "StallGreen": ("P M", "solid", "yes", ""), "StallRedTent": ("P M", "solid", "yes", ""),
    # banners (crests per town are still the owner's question: all kept)
    "BannerGriffin": ("C B", "none", "no", "hang"), "BannerKnot": ("C", "none", "no", "hang"), "BannerLion": ("C P", "none", "no", "hang"),
    "BannerSwordShield": ("C", "none", "no", "hang"), "BannerSwords": ("C", "none", "no", "hang"),
    "BannerPoleGriffin": ("P C R B", "thin", "yes", ""), "BannerPoleLion": ("P C R", "thin", "yes", ""),
    "BannerPoleStone": ("P C R", "thin", "yes", ""), "BannerPoleSwordShield": ("P C R", "thin", "yes", ""),
    "BannerPostKnot": ("P C R", "thin", "yes", ""), "BannerPostSwords": ("P C R", "thin", "yes", ""),
    "BannerPostTattered": ("C K", "thin", "yes", ""),
    # wells (the farmstead and open-world ones, WellRedRoof, WellStylised, WellMossy, wait for the open world)
    "WellVillage": ("B F", "solid", "yes", "light"), "WellThatched": ("B F", "solid", "yes", ""),
    "WellOld": ("C F", "solid", "yes", ""), "WellCobble": ("B", "solid", "yes", ""),
    # torches (the flame is drawn with its torch)
    "TorchBlazing": ("C B K", "none", "no", "light|flame=TorchBlazingFlame"), "TorchBlazingFlame": ("", "none", "no", "part"),
    "TorchWall": ("C B K", "none", "no", "hang|light|flame=TorchWallFlame"), "TorchWallFlame": ("", "none", "no", "part"),
    "Sconce": ("C B K", "none", "no", "hang|light|flame=SconceFlame"), "SconceFlame": ("", "none", "no", "part"),
    "TorchUnlitRope": ("K C", "none", "no", ""), "TorchUnlitTar": ("K C", "none", "no", ""),
    # planters
    "FlowerBoxIvy": ("S", "none", "no", ""), "FlowerBoxLong": ("S", "none", "no", ""), "WindowBoxRed": ("S", "none", "no", "hang"),
    "PlanterGilded": ("P", "solid", "near", ""),
    # rocks, fallen wood, stumps (the pond's far bank, the fringe, yards)
    "BoulderMossy": ("W F", "solid", "yes", ""), "RockPale": ("W R", "solid", "yes", ""), "LogMossy": ("W E", "solid", "yes", ""),
    "StumpGrass": ("Y E W", "solid", "near", ""), "StumpMushroom": ("Y E W", "solid", "near", ""),
    "StumpPainted": ("Y E W", "solid", "near", ""), "StumpRooty": ("Y E W", "solid", "near", ""), "StumpStones": ("Y E W", "solid", "near", ""),
}
# kinds that must stand level (wheels, legs, long bases that would float on a slope): Slope 12
LEVEL = {"HandCart", "HayWagon", "ThatchWagon", "VegCart", "BenchCarved", "BenchStone", "TrestleTable", "StallGreen", "StallRedTent",
         "WellVillage", "WellThatched", "WellOld", "WellCobble", "StairsStone", "StairsStoneShort", "StairsWalled", "ChickenCoop", "NestingBoxes",
         "RaisedBed", "TulipBed", "PlanterGilded", "FirewoodRack", "SackStack", "SackWall", "WallStone", "WallPillar", "FencePalisade",
         "FencePicket", "FenceStick", "HedgeBox", "HedgeSegment"}
LANDMARK = {"WellCobble"}
LARGE = {"BoulderMossy"}   # (the plan's cull list: boulders count as large)
FOOT_SLAB = 1.0   # metres: the footprint is the piece's plan at foot height, its lowest metre (a wagon's bed, a stall's counter)

decor_names = sorted(set(m[3:] for m in re.findall(r"/Game/Filler/(SM_[A-Za-z0-9_]+)", open(DECOR_H, encoding="utf-8").read())))
names = sorted(set(CATALOG) | set(decor_names))
if os.environ.get("DECOR_ONLY"):   # (a test run: just these kinds, comma separated; FillKinds.csv is then not written)
    names = [n for n in names if n in os.environ["DECOR_ONLY"].split(",")]
bad = sorted(set(names) & BLOCKED)
if bad:
    raise RuntimeError("DECORDIAG FAIL blocked kinds asked for: " + ", ".join(bad))
unreal.log("DECORDIAG %d kinds (%d from TownLabDecor.h)" % (len(names), len(decor_names)))

authored = {}
if os.path.exists(CSV_PATH):
    with open(CSV_PATH, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(line for line in f if not line.startswith("#")):
            authored[row["Kind"]] = row


def mesh_points(mesh):
    pts, tris = [], 0
    for s in range(mesh.get_num_sections(0)):
        r = PML.get_section_from_static_mesh(mesh, 0, s)
        pts.extend((v.x / 100.0, v.y / 100.0, v.z / 100.0) for v in r[0])
        tris += len(r[1]) // 3
    return pts, tris


def measure(mesh):
    pts, tris = mesh_points(mesh)
    xs, ys, zs = [p[0] for p in pts], [p[1] for p in pts], [p[2] for p in pts]
    z0, z1 = min(zs), max(zs)
    foot = [p for p in pts if p[2] <= z0 + FOOT_SLAB] or pts
    fx0, fx1 = min(p[0] for p in foot), max(p[0] for p in foot)
    fy0, fy1 = min(p[1] for p in foot), max(p[1] for p in foot)
    cx, cy = (fx0 + fx1) / 2, (fy0 + fy1) / 2
    hx, hy = (fx1 - fx0) / 2, (fy1 - fy0) / 2
    rmax = max(((p[0] - cx) ** 2 + (p[1] - cy) ** 2) ** 0.5 for p in foot)
    # round when the plan is about square and nothing reaches the corners of its box (a circle reaches 71% of the way)
    round_ = abs(hx - hy) <= 0.15 * max(hx, hy, 1e-3) and rmax <= 0.85 * (hx * hx + hy * hy) ** 0.5
    return {"tris": tris, "x0": min(xs), "x1": max(xs), "y0": min(ys), "y1": max(ys), "z0": z0, "z1": z1,
            "cx": cx, "cy": cy, "hx": hx, "hy": hy, "r": rmax, "round": round_}


def size_class(name, m):
    longest = max(m["x1"] - m["x0"], m["y1"] - m["y0"], m["z1"] - m["z0"])
    if name in LANDMARK:
        return "landmark"
    if name in LARGE:
        return "large"
    return "tiny" if longest < 0.5 else "small" if longest < 1.0 else "medium" if longest < 2.5 else "large"


# ------------------------------------------------------------------ parents
def ensure_parent(name, two_sided):
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
    else:
        m = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
        m.set_editor_property("two_sided", two_sided)
        E = MEL.create_material_expression

        def tex(pname, y, kind):
            t = E(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
            t.set_editor_property("parameter_name", pname)
            t.set_editor_property("sampler_type", kind)
            return t
        base = tex("BaseTex", -200, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        mr = tex("MRTex", 200, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        nrm = tex("NormalTex", 600, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        MP = unreal.MaterialProperty
        ok = MEL.connect_material_property(base, "RGB", MP.MP_BASE_COLOR)
        ok = MEL.connect_material_property(mr, "G", MP.MP_ROUGHNESS) and ok
        ok = MEL.connect_material_property(mr, "B", MP.MP_METALLIC) and ok
        ok = MEL.connect_material_property(nrm, "RGB", MP.MP_NORMAL) and ok
        g = E(m, unreal.MaterialExpressionScalarParameter, -700, 900)
        g.set_editor_property("parameter_name", "GlowAmount")
        g.set_editor_property("default_value", 0.0)
        mul = E(m, unreal.MaterialExpressionMultiply, -350, 800)
        ok = MEL.connect_material_expressions(base, "RGB", mul, "A") and ok
        ok = MEL.connect_material_expressions(g, "", mul, "B") and ok
        ok = MEL.connect_material_property(mul, "", MP.MP_EMISSIVE_COLOR) and ok
        if not ok:
            raise RuntimeError("DECORDIAG FAIL a connection failed in " + name)
        unreal.log("DECORDIAG made parent %s (its default pictures are unset: the instances set their own)" % name)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("used_with_nanite", "nanite" in RENDER.values())   # (no Nanite shaders cooked while every kind is LODs)
    MEL.recompile_material(m)
    EAL.save_asset(path)
    return m


parents = {False: ensure_parent("M_TownDecor", False), True: ensure_parent("M_TownDecorTwoSided", True)}
reg = unreal.AssetRegistryHelpers.get_asset_registry()
made = {"M_TownDecor", "M_TownDecorTwoSided"}
for pn in ("M_TownDecor", "M_TownDecorTwoSided"):   # (the parents' default pictures stay)
    for d in reg.get_dependencies(DST + "/" + pn, unreal.AssetRegistryDependencyOptions()) or []:
        if str(d).startswith(DST + "/"):
            made.add(str(d).split("/")[-1])

# ------------------------------------------------------------------ copies
tex_cap = {}          # texture asset name -> its cap (the largest any user needs)
rows, failures = [], []
done = 0
for n in names:
    src_mesh = SRC + "/SM_" + n
    if not EAL.does_asset_exist(src_mesh):
        failures.append("missing " + src_mesh)
        continue
    src = EAL.load_asset(src_mesh)
    m = measure(src)
    cls = size_class(n, m)
    two = False
    for sm in src.get_editor_property("static_materials"):
        mi = sm.get_editor_property("material_interface")
        par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
        if par is not None and "TwoSided" in par.get_name():
            two = True
    render = "lods" if (two or n.endswith("Flame")) else RENDER[cls]

    dst_mesh = DST + "/SM_" + n
    if EAL.does_asset_exist(dst_mesh):
        EAL.delete_asset(dst_mesh)
    if not EAL.duplicate_asset(src_mesh, dst_mesh):
        failures.append("could not copy " + src_mesh)
        continue
    mesh = EAL.load_asset(dst_mesh)
    mats = mesh.get_editor_property("static_materials")
    for i, sm in enumerate(mats):
        mi = sm.get_editor_property("material_interface")
        if mi is None or not isinstance(mi, unreal.MaterialInstanceConstant):
            failures.append("%s slot %d has no material instance" % (n, i))
            continue
        mi_two = "TwoSided" in mi.get_editor_property("parent").get_name()
        mi_name = mi.get_name()
        mi_path = DST + "/" + mi_name
        if mi_name not in made:
            if EAL.does_asset_exist(mi_path):
                EAL.delete_asset(mi_path)
            new_mi = tools.create_asset(mi_name, DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(new_mi, parents[mi_two])
            for p in ("BaseTex", "MRTex", "NormalTex"):
                t = MEL.get_material_instance_texture_parameter_value(mi, p)
                if t is None:
                    continue
                tname = t.get_name()
                tpath = DST + "/" + tname
                if t.get_path_name().startswith(SRC + "/") and not EAL.does_asset_exist(tpath):
                    if not EAL.duplicate_asset(t.get_path_name().split(".")[0], tpath):
                        failures.append("could not copy texture " + tname)
                        continue
                t2 = EAL.load_asset(tpath) if EAL.does_asset_exist(tpath) else t
                MEL.set_material_instance_texture_parameter_value(new_mi, p, t2)
                cap = 512 if p == "MRTex" else (512 if cls == "tiny" else 1024)
                tex_cap[tname] = max(tex_cap.get(tname, 0), cap)
                made.add(tname)
            MEL.set_material_instance_scalar_parameter_value(new_mi, "GlowAmount", MEL.get_material_instance_scalar_parameter_value(mi, "GlowAmount"))
            EAL.save_asset(mi_path)
            made.add(mi_name)
        sm.set_editor_property("material_interface", EAL.load_asset(mi_path))
        mats[i] = sm
    mesh.set_editor_property("static_materials", mats)

    ns = mesh.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", render == "nanite")
    mesh.set_editor_property("nanite_settings", ns)
    last_pct = max(0.004, min(LOD_PERCENT_1, LOD_LAST_TRIS / max(m["tris"], 1)))
    if render == "lods":
        unreal.JeebMeshTools.set_generated_lods(mesh, [1.0, LOD_PERCENT_1, last_pct], list(LOD_SCREEN))
    where, coll, shadow, flags = CATALOG.get(n, ("S Y", "none", "no", ""))
    a = authored.get(n, {})
    if (a.get("Collision") or coll) == "mesh":   # (stairs: walked on, so their own shape)
        bs = mesh.get_editor_property("body_setup")
        bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    EAL.save_asset(dst_mesh)
    made.add("SM_" + n)
    lod_tris = unreal.JeebMeshTools.lod_triangles(mesh, mesh.get_num_lods() - 1) if render == "lods" else 0

    # ---- the CSV row: measured columns, then authored ones (the CSV's own values win)
    if coll == "solid":
        coll = "capsule" if (m["round"] and m["z1"] - m["z0"] >= 2 * m["r"]) else "box"   # (a capsule wider than it is tall would be a ball)
    elif coll == "thin":
        coll = "capsule"
    round_foot = m["round"] or coll == "capsule"
    rows.append({
        "Kind": n, "Mesh": "/Game/TownDecor/SM_" + n,
        "Shape": "circle" if round_foot else "box",
        "FootX": "%.2f" % (2 * m["r"] if round_foot else 2 * m["hx"]),
        "FootY": "%.2f" % (2 * m["r"] if round_foot else 2 * m["hy"]),
        "FootCX": "%.2f" % m["cx"], "FootCY": "%.2f" % m["cy"],
        "Height": "%.2f" % (m["z1"] - m["z0"]),
        "Collision": a.get("Collision") or coll,
        "Shadow": a.get("Shadow") or shadow,
        "Cull": a.get("Cull") or cls,
        "Slope": a.get("Slope", "") if n in authored else ("12" if n in LEVEL else ""),   # (empty: AWorldFill's default, 20, or 30 for natural kinds)
        "Flags": a.get("Flags", "") if n in authored else flags,
        "Where": a.get("Where", "") if n in authored else where,
        "Render": render, "Tris": str(m["tris"]), "LodTris": str(lod_tris),
        "SizeX": "%.2f" % (m["x1"] - m["x0"]), "SizeY": "%.2f" % (m["y1"] - m["y0"]),
        "MinZ": "%.2f" % m["z0"], "TwoSided": "1" if two else "0",
    })
    done += 1
    unreal.log("DECORDIAG %s %s %s tris %d last LOD %d" % (n, cls, render, m["tris"], lod_tris))

# ------------------------------------------------------------------ texture caps
for tname, cap in tex_cap.items():
    t = EAL.load_asset(DST + "/" + tname)
    if t is None:
        failures.append("texture missing after copy " + tname)
        continue
    if t.get_editor_property("max_texture_size") != cap:
        t.set_editor_property("max_texture_size", cap)
        EAL.save_asset(DST + "/" + tname)

# ------------------------------------------------------------------ remove what no kind uses any more (the grain sacks went so)
if not os.environ.get("DECOR_ONLY"):
    removed = []
    for a in EAL.list_assets(DST, recursive=True, include_folder=False):
        p = a.split(".")[0]
        nm = p.split("/")[-1]
        if nm not in made:
            EAL.delete_asset(p)
            removed.append(nm)
    if removed:
        unreal.log("DECORDIAG removed unused: " + ", ".join(sorted(removed)))

# ------------------------------------------------------------------ checks (fail, not just log)
for a in EAL.list_assets(DST, recursive=True, include_folder=False):
    p = a.split(".")[0]
    nm = p.split("/")[-1]
    for b in BLOCKED:
        if b in nm:
            failures.append("blocked kind present: " + nm)
    if not nm.startswith("SM_"):
        continue
    k = nm[3:]
    mesh = EAL.load_asset(p)
    row = next((r for r in rows if r["Kind"] == k), None)
    want = row["Render"] if row else None
    nanite = mesh.get_editor_property("nanite_settings").enabled
    if want == "nanite" and not nanite:
        failures.append("%s should be Nanite" % k)
    if want == "lods" and (nanite or mesh.get_num_lods() < 3):
        failures.append("%s should have 3 generated LODs (nanite %s, %d LODs)" % (k, nanite, mesh.get_num_lods()))
    for sm in mesh.get_editor_property("static_materials"):
        mi = sm.get_editor_property("material_interface")
        base = mi.get_base_material() if mi else None
        if base is None or not base.get_editor_property("used_with_instanced_static_meshes"):
            failures.append("%s material %s is not flagged for instanced meshes" % (k, mi.get_name() if mi else "none"))
        elif nanite and not base.get_editor_property("used_with_nanite"):
            failures.append("%s material %s is not flagged for Nanite" % (k, mi.get_name()))
for a in EAL.list_assets(DST, recursive=True, include_folder=False):
    for d in reg.get_dependencies(a.split(".")[0], unreal.AssetRegistryDependencyOptions()) or []:
        if str(d).startswith(SRC + "/"):
            failures.append("%s still refers to %s" % (a, d))

# ------------------------------------------------------------------ FillKinds.csv
COLS = ["Kind", "Mesh", "Shape", "FootX", "FootY", "FootCX", "FootCY", "Height", "Collision", "Shadow", "Cull", "Slope", "Flags",
        "Where", "Render", "Tris", "LodTris", "SizeX", "SizeY", "MinZ", "TwoSided"]
if not os.environ.get("DECOR_ONLY"):
    with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLS, lineterminator="\n")
        w.writeheader()
        for r in sorted(rows, key=lambda r: r["Kind"]):
            w.writerow(r)

for x in failures:
    unreal.log_error("DECORDIAG FAIL " + x)
unreal.log("DECORDIAG copied %d of %d kinds, %d textures capped, %d failures" % (done, len(names), len(tex_cap), len(failures)))
if failures:
    raise RuntimeError("DECORDIAG FAILED (%d problems)" % len(failures))
