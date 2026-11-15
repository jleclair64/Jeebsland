# The world palette for the material builders (2026-11-15; Docs/Palette.md). One file holds the world's colours,
# Content/Data/Palette.ini; this reads it for the Python builders, keeps the palette's Material Parameter Collection
# /Game/MPC_JeebPalette in step with it, and adds the palette's nodes to a material:
#   colour(mat, "GrassBlade")         the named colour, read from the collection (instead of a baked constant), RGB
#   grade_plant(mat, col)             the knobs' grade for plants (season, then the plant matrix): grass, leaves, bushes
#   grade_plant(mat, col, share=...)  the same for evergreens (pines, redwoods): only a share of the season
#   grade_plant_extra(mat, col)       a plant layer inside a mixed material (M_Ground's grass): season and Greenness only,
#                                     the material then applies grade_world to its final colour
#   grade_world(mat, col)             earth, stone, bark: Warmth, Saturation, Brightness
# The game (Source/Jeebsland/Palette.cpp) writes the knobs' matrices and any edited colour into the collection while it runs.
# At the default knobs every grade is exactly the identity (a lerp by 0 and identity matrix rows), so a material looks the
# same as before it read the palette.
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(os.path.dirname(HERE), "Content", "Data", "Palette.ini")
MPC_PATH = "/Game/MPC_JeebPalette"
MARK = "JeebPalette"
# A material may read at most two collections. The Fab grass's wind function already reads two, so these take the palette's grade
# as plain vector parameters named "Pal<name>" (defaults: the same identity values), set by Meadow.cpp (JeebPalette::BindGrade).
PARAM_MODE = {"/Game/GrassReal/M_GrassLibrary", "/Game/GrassReal/M_MeadowFlowers"}
LUM = (0.3, 0.59, 0.11)   # as the materials' own Desaturation nodes

# the named colours the built materials read from the collection (the rest are read by the game's code: Palette.cpp)
MPC_COLOURS = ["GrassBlade", "GrassStem", "Bush", "FieldHollow", "FieldSunny", "FieldDry", "GroundGrass", "Earth", "Sand", "Snow",
               "RockFar", "Cobble", "Moss"]


def _parse():
    sections, cur = {}, None
    with open(INI, encoding="utf-8") as f:
        for raw in f:
            line = raw.split(";", 1)[0].strip()
            if not line:
                continue
            m = re.match(r"^\[(\w+)\]$", line)
            if m:
                cur = sections.setdefault(m.group(1), {})
                continue
            if "=" in line and cur is not None:
                k, v = line.split("=", 1)
                cur[k.strip()] = [p.strip() for p in v.split(",")]
    return sections


_S = _parse()


def rgb(name):
    """a named colour's (r, g, b) from Palette.ini [Colours]"""
    v = _S["Colours"][name]
    return (float(v[0]), float(v[1]), float(v[2]))


def grade_value(name):
    v = _S["Grade"][name]
    return tuple(float(x) for x in v) if len(v) > 1 else float(v[0])


def _norm_lum(c):
    l = sum(a * b for a, b in zip(c, LUM))
    return tuple(x / l for x in c)


def _defaults():
    """every vector in the collection with its value at the default knobs (identity grade)"""
    out = [(n, rgb(n) + (1.0,)) for n in MPC_COLOURS]
    out.append(("Season", (0.0, 0.0, float(grade_value("EvergreenShare")), 0.0)))   # up, down, evergreen share
    out.append(("SeasonTint", _norm_lum(grade_value("AutumnTint")) + (1.0,)))
    for pre in ("World", "Plant", "PlantX"):
        for i, ax in enumerate("RGB"):
            row = [0.0, 0.0, 0.0]
            row[i] = 1.0
            out.append((pre + ax, tuple(row) + (0.0,)))
    return out


try:
    import unreal
    lib = unreal.MaterialEditingLibrary
    EAL = unreal.EditorAssetLibrary

    def ensure_mpc():
        """create /Game/MPC_JeebPalette or bring its default values up to Palette.ini, in place (UJeebPaletteTools: existing
        parameters keep their hidden Ids, which the materials' collection nodes find their names by; rewriting the list from Python
        gave them new Ids and every material built before lost its colours on the next load). Saved only if something changed."""
        want = _defaults()
        mpc = EAL.load_asset(MPC_PATH) if EAL.does_asset_exist(MPC_PATH) else None
        if mpc is None:
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            mpc = tools.create_asset("MPC_JeebPalette", "/Game", unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
        names = [unreal.Name(n) for n, _ in want]
        values = [unreal.LinearColor(v[0], v[1], v[2], v[3]) for _, v in want]
        if unreal.JeebPaletteTools.sync_vector_defaults(mpc, names, values):
            EAL.save_asset(MPC_PATH, only_if_is_dirty=False)
            unreal.log("PALETTEDIAG MPC_JeebPalette written (%d vectors)" % len(mpc.get_editor_property("vector_parameters")))
        else:
            unreal.log("PALETTEDIAG MPC_JeebPalette already up to date")
        return mpc

    def check(paths):
        """every palette node in these materials still names its parameter after a fresh load (run in a new process, after the
        builders): a node whose Id the collection lacks loads as None and the material falls back to the default grey"""
        bad = 0
        for path in paths:
            mat = EAL.load_asset(path)
            if mat is None:
                continue
            seen, found, todo = set(), [], []
            for prop in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_SUBSURFACE_COLOR, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
                n = lib.get_material_property_input_node(mat, prop)
                if n:
                    todo.append(n)
            while todo:
                e = todo.pop()
                if e is None or e.get_path_name() in seen:
                    continue
                seen.add(e.get_path_name())
                if isinstance(e, unreal.MaterialExpressionCollectionParameter):
                    found.append(str(e.get_editor_property("parameter_name")))
                todo.extend(x for x in lib.get_inputs_for_material_expression(mat, e) if x)
            none = found.count("None")
            bad += none
            unreal.log("PALETTEDIAG CHECK %s: %d collection nodes, %s" % (path, len(found), ("%d BROKEN" % none) if none else "ok"))
        return bad == 0

    _cache = {}

    def _node(mat, cls, **props):
        n = lib.create_material_expression(mat, cls, 0, 0)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def _link(a, b, pin="", out=""):
        if not lib.connect_material_expressions(a, out, b, pin):
            raise RuntimeError("PALETTEDIAG link failed: %s.%s -> %s.%s" % (a.get_class().get_name(), out, b.get_class().get_name(), pin))

    def _raw(mat, name):
        """the collection's vector (RGBA), one node per name per material"""
        key = (mat.get_path_name(), name, "raw")
        hit = _cache.get(key)
        if (hit is None or hit[0] != mat) and mat.get_path_name().split(".")[0] in PARAM_MODE:
            v = dict(_defaults())[name]
            n = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, 0, 0)
            n.set_editor_property("parameter_name", "Pal" + name)
            n.set_editor_property("group", "Palette")
            n.set_editor_property("default_value", unreal.LinearColor(v[0], v[1], v[2], v[3]))
            hit = _cache[key] = (mat, n)
        if hit is None or hit[0] != mat:   # (a material made again under the same name gets its own nodes)
            mpc = EAL.load_asset(MPC_PATH)
            if mpc is None:
                raise RuntimeError("PALETTEDIAG MPC_JeebPalette is missing: run palette.ensure_mpc() first")
            n = lib.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, 0, 0)
            n.set_editor_property("collection", mpc)
            n.set_editor_property("parameter_name", name)
            hit = _cache[key] = (mat, n)
        return hit[1]

    def _mask(mat, a, r, g, b, alpha=False, out=""):
        n = _node(mat, unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=alpha)
        _link(a, n, "", out)
        return n

    def colour(mat, name):
        """a named colour from the collection, RGB (for a Constant3Vector's place)"""
        key = (mat.get_path_name(), name, "rgb")
        hit = _cache.get(key)
        if hit is None or hit[0] != mat:
            hit = _cache[key] = (mat, _mask(mat, _raw(mat, name), True, True, True))
        return hit[1]

    def _bin(mat, cls, a, b, out_a="", out_b=""):
        n = _node(mat, cls)
        _link(a, n, "A", out_a)
        _link(b, n, "B", out_b)
        return n

    def _const(mat, v):
        return _node(mat, unreal.MaterialExpressionConstant, r=v)

    def _matrix(mat, c, pre, out=""):
        """the 3x3 colour matrix whose rows are the collection's <pre>R, <pre>G, <pre>B"""
        if out:
            c = _mask(mat, c, True, True, True, out=out)
        d = [_bin(mat, unreal.MaterialExpressionDotProduct, c, colour(mat, pre + ax)) for ax in "RGB"]
        res = _bin(mat, unreal.MaterialExpressionAppendVector, _bin(mat, unreal.MaterialExpressionAppendVector, d[0], d[1]), d[2])
        res.set_editor_property("desc", MARK + " " + pre)   # (how patch_existing tells a graded material)
        return res

    def _season(mat, c, share=None, out=""):
        """greens toward the autumn gold (Autumn > 0) or warm colours toward green (Autumn < 0), keeping each colour's brightness.
        How green (or warm) a colour is is measured against its own brightness, so the muted olive grass moves as far as the
        saturated leaves. share: None for plants, True for evergreens (EvergreenShare of it), or an expression 0..1 mixing to it."""
        if out:
            c = _mask(mat, c, True, True, True, out=out)
        lum = _bin(mat, unreal.MaterialExpressionDotProduct, c, _node(mat, unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(LUM[0], LUM[1], LUM[2], 0.0)))
        r = _mask(mat, c, True, False, False)
        g = _mask(mat, c, False, True, False)
        b = _mask(mat, c, False, False, True)
        soft = _node(mat, unreal.MaterialExpressionAdd, const_b=0.001)
        _link(lum, soft, "A")
        inv = _bin(mat, unreal.MaterialExpressionDivide, _const(mat, 2.0), soft)   # (green 0.5 of its brightness = fully green)
        half = lambda x, y: _bin(mat, unreal.MaterialExpressionMultiply, _bin(mat, unreal.MaterialExpressionAdd, x, y), _const(mat, 0.5))
        sat = lambda x: _one(mat, unreal.MaterialExpressionSaturate, x)
        green = sat(_bin(mat, unreal.MaterialExpressionMultiply, _bin(mat, unreal.MaterialExpressionSubtract, g, half(r, b)), inv))
        warm = sat(_bin(mat, unreal.MaterialExpressionMultiply, _bin(mat, unreal.MaterialExpressionSubtract, r, half(g, b)), inv))
        s = _raw(mat, "Season")
        up = _mask(mat, s, True, False, False)
        down = _mask(mat, s, False, True, False)
        alpha = _bin(mat, unreal.MaterialExpressionAdd, _bin(mat, unreal.MaterialExpressionMultiply, green, up), _bin(mat, unreal.MaterialExpressionMultiply, warm, down))
        if share is not None:
            ev = _mask(mat, s, False, False, True)
            if share is not True:   # an expression: 0 plant, 1 evergreen
                ev = _lerp(mat, _const(mat, 1.0), ev, share)
            alpha = _bin(mat, unreal.MaterialExpressionMultiply, alpha, ev)
        target = _bin(mat, unreal.MaterialExpressionMultiply, colour(mat, "SeasonTint"), lum)
        return _lerp(mat, c, target, alpha)

    def _one(mat, cls, a):
        n = _node(mat, cls)
        _link(a, n, "")
        return n

    def _lerp(mat, a, b, alpha):
        n = _node(mat, unreal.MaterialExpressionLinearInterpolate)
        _link(a, n, "A")
        _link(b, n, "B")
        _link(alpha, n, "Alpha")
        return n

    def grade_plant(mat, col, out="", share=None):
        return _matrix(mat, _season(mat, col, share, out), "Plant")

    def grade_plant_extra(mat, col, out="", share=None):
        return _matrix(mat, _season(mat, col, share, out), "PlantX")

    def grade_world(mat, col, out=""):
        return _matrix(mat, col, "World", out)

    def graded(mat, prop):
        """is this material output already the palette's grade (patch_existing)?"""
        n = lib.get_material_property_input_node(mat, prop)
        return n is not None and str(n.get_editor_property("desc")).startswith(MARK)

    def patch_existing(path, plant=True):
        """add the grade to an existing material's base colour (and subsurface colour) output in place, once (the realistic grass
        copies in /Game/GrassReal: their Fab packs are git-ignored, so a full rebuild needs Tools/BuildGrassPacks.ps1 on a machine
        with the packs, whose grass_packs.py adds the same grade; this keeps the committed copies in step without touching the
        packs' own material instances)"""
        mat = EAL.load_asset(path)
        if mat is None:
            unreal.log_warning("PALETTEDIAG %s is missing: skipped" % path)
            return False
        MP = unreal.MaterialProperty
        done = 0
        for prop in (MP.MP_BASE_COLOR, MP.MP_SUBSURFACE_COLOR):
            node = lib.get_material_property_input_node(mat, prop)
            if node is None or graded(mat, prop):
                continue
            out = lib.get_material_property_input_node_output_name(mat, prop)
            g = grade_plant(mat, node, out=out) if plant else grade_world(mat, node, out=out)
            if not lib.connect_material_property(g, "", prop):
                raise RuntimeError("PALETTEDIAG could not connect the grade in %s" % path)
            done += 1
        if done:
            lib.recompile_material(mat)
            EAL.save_asset(path)
        unreal.log("PALETTEDIAG %s: %s" % (path, ("graded (%d outputs)" % done) if done else "already graded"))
        return True

except ImportError:
    pass
