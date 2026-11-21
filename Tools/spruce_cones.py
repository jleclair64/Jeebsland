# The spruces' cones, repainted (owner, 2026-11-21: "less orange and more dark brown/green"). The cones are painted into the Spruce
# Forest pack's needle texture (hard-wired in the pack's master material, not a parameter) and into its far cards' colour textures
# (the "Albedo" parameter). The pack's own files are never changed. Run by Tools/BuildSpruceCones.ps1, twice:
#   -ConesStep=export   writes each of those textures the forest's spruces use to Saved/SpruceCones/<name>.png (logs CONES lines)
#   -ConesStep=import   brings Saved/SpruceCones/<name>_Cones.png (repainted by the .ps1) in as /Game/TreeGroves/SpruceCones/<name>_Cones
#                       with the pack texture's own settings, then copies the needle materials there: <master>_Cones (the master
#                       material, its needle texture swapped for the repainted one), <tree>_MA_Cones and <tree>_leaf_Cones (the
#                       pack's instances, re-parented onto it)
# At run time JeebTreeLook::ConeLeaf (TreeLook.cpp) gives each spruce's needle slot its <tree>_leaf_Cones, and DarkCones swaps the far
# cards' Albedo.
import os
import sys
import unreal

PACK = "/Game/PN_interactiveSpruceForest"
DEST = "/Game/TreeGroves/SpruceCones"
# the meshes OakForest.cpp and Config/DefaultGame.ini use
MESHES = ["full/high/spruce_full_01", "full/high/spruce_full_02", "full/high/spruce_full_03", "half/high/spruce_half_02", "small/spruce_small_01"]
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Saved", "SpruceCones").replace("\\", "/")

step = "export"
for a in sys.argv + unreal.SystemLibrary.get_command_line().split():
    if a.lower().startswith("-conesstep="):
        step = a.split("=", 1)[1].lower()

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def needle_samples(mat):
    """the texture sample nodes of a master material that read a needle colour texture (Leaf_A*), reached from its outputs"""
    found, seen = [], set()

    def walk(expr):
        if expr is None or expr in seen:
            return
        seen.add(expr)
        if isinstance(expr, unreal.MaterialExpressionTextureSample):
            t = expr.get_editor_property("texture")
            if t and t.get_name().startswith("Leaf_A") and not t.get_name().startswith("Leaf_AO"):
                found.append(expr)
        for inp in mel.get_inputs_for_material_expression(mat, expr):
            walk(inp)
    for prop in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_SUBSURFACE_COLOR, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        walk(mel.get_material_property_input_node(mat, prop))
    return found


def survey():
    """textures: name -> texture (far cards' Albedo and the masters' needle textures); leaves: [(leaf instance, [its parents up to the master], master)]"""
    textures, leaves = {}, []
    for m in MESHES:
        mesh = eal.load_asset(PACK + "/Meshes/" + m)
        if not mesh:
            unreal.log_warning("CONES no mesh " + m)
            continue
        for sm in mesh.get_editor_property("static_materials"):
            mi = sm.get_editor_property("material_interface")
            if not mi or not isinstance(mi, unreal.MaterialInstance):
                continue
            name = mi.get_name()
            if "imposter" in name:
                # (2026-11-21 evening: the opacity sheet "Masks" too, cut more see-through like the near model's fronds)
                for p in ("Albedo", "Masks"):
                    t = mel.get_material_instance_texture_parameter_value(mi, p)
                    if t and t.get_path_name().startswith(PACK):
                        textures[t.get_name()] = t
            elif name.endswith("_leaf") or name.endswith("_branch") or name.endswith("_trunk"):
                # (2026-11-21 evening: branch and trunk get copies too, so every near material sits on the copied master, which
                # crossfades with dithering into the far card instead of popping)
                chain, cur = [], mi.get_editor_property("parent")
                while cur is not None and isinstance(cur, unreal.MaterialInstance):
                    chain.append(cur)
                    cur = cur.get_editor_property("parent")
                if cur is None:
                    continue
                for s in needle_samples(cur):
                    t = s.get_editor_property("texture")
                    textures[t.get_name()] = t
                leaves.append((mi, chain, cur))
                unreal.log("CONES %s: %s -> %s -> %s" % (m, name, " -> ".join(c.get_name() for c in chain), cur.get_name()))
    return textures, leaves


def fresh_copy(src, name):
    path = DEST + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return eal.duplicate_asset(src.get_path_name().split(".")[0], path)


textures, leaves = survey()
if step == "export":
    os.makedirs(OUT, exist_ok=True)
    for name, t in sorted(textures.items()):
        task = unreal.AssetExportTask()
        task.set_editor_property("object", t)
        task.set_editor_property("filename", OUT + "/" + name + ".png")
        task.set_editor_property("automated", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("exporter", unreal.TextureExporterPNG())
        ok = unreal.Exporter.run_asset_export_task(task)
        unreal.log("CONES export %s srgb %s %s: %s" % (name, t.get_editor_property("srgb"), t.get_editor_property("compression_settings"), "ok" if ok else "FAILED"))
    unreal.log("CONES export done")
else:
    made = {}
    for name, t in sorted(textures.items()):
        png = OUT + "/" + name + "_Cones.png"
        if not os.path.exists(png):
            unreal.log_warning("CONES no " + png)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", png)
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("destination_name", name + "_Cones")
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        new = eal.load_asset(DEST + "/" + name + "_Cones")
        if not new:
            unreal.log_warning("CONES import FAILED " + name)
            continue
        for prop in ("srgb", "compression_settings", "lod_group", "max_texture_size", "mip_gen_settings", "address_x", "address_y", "filter", "lod_bias",
                     "compression_no_alpha", "never_stream"):
            try:
                new.set_editor_property(prop, t.get_editor_property(prop))
            except Exception as e:
                unreal.log_warning("CONES %s: %s not copied (%s)" % (name, prop, e))
        eal.save_asset(DEST + "/" + name + "_Cones", False)
        made[name] = new
        unreal.log("CONES imported %s_Cones %dx%d" % (name, new.blueprint_get_size_x(), new.blueprint_get_size_y()))
    # the needle materials: the master with the repainted texture, and each tree's instances on top of it
    masters, mids, done = {}, {}, 0
    for leaf, chain, master in leaves:
        mname = master.get_name()
        if mname not in masters:
            copy = fresh_copy(master, mname + "_Cones")
            swapped = 0
            for s in needle_samples(copy):
                t = s.get_editor_property("texture")
                if t.get_name() in made:
                    s.set_editor_property("texture", made[t.get_name()])
                    swapped += 1
            copy.set_editor_property("dithered_lod_transition", True)   # (the far card's MA_Imposter already has it)
            mel.recompile_material(copy)
            eal.save_asset(DEST + "/" + mname + "_Cones", False)
            masters[mname] = copy
            unreal.log("CONES material %s_Cones: %d needle texture samples swapped" % (mname, swapped))
        parent = masters[mname]
        for mid in reversed(chain):   # (from the master down)
            if mid.get_name() not in mids:
                c = fresh_copy(mid, mid.get_name() + "_Cones")
                mel.set_material_instance_parent(c, parent)
                mel.update_material_instance(c)
                eal.save_asset(DEST + "/" + mid.get_name() + "_Cones", False)
                mids[mid.get_name()] = c
            parent = mids[mid.get_name()]
        c = fresh_copy(leaf, leaf.get_name() + "_Cones")
        mel.set_material_instance_parent(c, parent)
        mel.update_material_instance(c)
        eal.save_asset(DEST + "/" + leaf.get_name() + "_Cones", False)
        done += 1
        unreal.log("CONES material %s_Cones on %s" % (leaf.get_name(), parent.get_name()))
    unreal.log("CONES import done: %d textures, %d needle materials" % (len(made), done))
