# Oak Hill's decor props, shipped (2026-11-14). The rebuilt town's decor (Source/Jeebsland/TownLabDecor.h) uses library props from
# /Game/Filler, the owner's filler lineup: 700 MB of options, git-ignored and never cooked. This copies just the ones the town uses,
# with their pictures, into /Game/TownDecor (tracked and cooked), on two fresh parent materials made like Filler's M_Filler /
# M_FillerTwoSided (Tools/filler_import.py) but flagged for instanced meshes, so the town draws each prop as one instanced set.
# The game loads /Game/TownDecor/<name> first and falls back to /Game/Filler/<name> (TownLabTown.cpp LoadDecor).
# Run by Tools/CopyTownDecor.ps1 in a windowless editor (this copy's editor closed). Run it again whenever TownLabDecor.h gains a
# /Game/Filler prop. Existing copies are replaced.
import os
import re
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DECOR_H = os.path.join(os.path.dirname(HERE), "Source", "Jeebsland", "TownLabDecor.h")
SRC, DST = "/Game/Filler", "/Game/TownDecor"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

names = sorted(set(re.findall(r"/Game/Filler/(SM_[A-Za-z0-9_]+)", open(DECOR_H, encoding="utf-8").read())))
unreal.log("DECORDIAG %d props: %s" % (len(names), ", ".join(names)))


def copy_texture(tex):
    if tex is None:
        return None
    path = tex.get_path_name().split(".")[0]
    if not path.startswith(SRC + "/"):
        return tex
    dst = DST + path[len(SRC):]
    if not EAL.does_asset_exist(dst):
        if not EAL.duplicate_asset(path, dst):
            unreal.log_error("DECORDIAG could not copy " + path)
            return tex
        EAL.save_asset(dst)
    return EAL.load_asset(dst)


def make_parent(name, two_sided):
    path = DST + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = tools.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", two_sided)
    m.set_editor_property("used_with_instanced_static_meshes", True)
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
        raise RuntimeError("DECORDIAG a connection failed in " + name)
    return m, (base, mr, nrm)


parents = {}
for pname, two in (("M_TownDecor", False), ("M_TownDecorTwoSided", True)):
    parents[pname] = make_parent(pname, two)
first_textures = {}

done = 0
for n in names:
    src_mesh = SRC + "/" + n
    if not EAL.does_asset_exist(src_mesh):
        unreal.log_error("DECORDIAG missing " + src_mesh)
        continue
    dst_mesh = DST + "/" + n
    if EAL.does_asset_exist(dst_mesh):
        EAL.delete_asset(dst_mesh)
    if not EAL.duplicate_asset(src_mesh, dst_mesh):
        unreal.log_error("DECORDIAG could not copy " + src_mesh)
        continue
    mesh = EAL.load_asset(dst_mesh)
    mats = mesh.get_editor_property("static_materials")
    for i, sm in enumerate(mats):
        mi = sm.get_editor_property("material_interface")
        if mi is None or not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        src_parent = mi.get_editor_property("parent")
        two = src_parent is not None and "TwoSided" in src_parent.get_name()
        pname = "M_TownDecorTwoSided" if two else "M_TownDecor"
        mi_name = mi.get_name()
        mi_path = DST + "/" + mi_name
        if EAL.does_asset_exist(mi_path):
            EAL.delete_asset(mi_path)
        new_mi = tools.create_asset(mi_name, DST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(new_mi, parents[pname][0])
        for p in ("BaseTex", "MRTex", "NormalTex"):
            t = MEL.get_material_instance_texture_parameter_value(mi, p)
            if t is not None:
                t2 = copy_texture(t)
                MEL.set_material_instance_texture_parameter_value(new_mi, p, t2)
                first_textures.setdefault((pname, p), t2)
        MEL.set_material_instance_scalar_parameter_value(new_mi, "GlowAmount", MEL.get_material_instance_scalar_parameter_value(mi, "GlowAmount"))
        EAL.save_asset(mi_path)
        sm.set_editor_property("material_interface", new_mi)
        mats[i] = sm
    mesh.set_editor_property("static_materials", mats)
    EAL.save_asset(dst_mesh)
    done += 1

# the parents' default pictures: copies (never a /Game/Filler picture, which would not be cooked)
for pname, (m, nodes) in parents.items():
    for node, p in zip(nodes, ("BaseTex", "MRTex", "NormalTex")):
        t = first_textures.get((pname, p)) or first_textures.get(("M_TownDecor", p))
        if t is not None:
            node.set_editor_property("texture", t)
    MEL.recompile_material(m)
    EAL.save_asset(DST + "/" + pname)

# anything left pointing into /Game/Filler?
leaks = []
for a in EAL.list_assets(DST, recursive=True, include_folder=False):
    for d in unreal.AssetRegistryHelpers.get_asset_registry().get_dependencies(a.split(".")[0], unreal.AssetRegistryDependencyOptions()) or []:
        if str(d).startswith(SRC + "/"):
            leaks.append("%s -> %s" % (a, d))
for l in leaks:
    unreal.log_error("DECORDIAG still refers to Filler: " + l)
unreal.log("DECORDIAG copied %d of %d props, %d references left in /Game/Filler" % (done, len(names), len(leaks)))
