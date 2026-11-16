# Lists a material's graph (what feeds BaseColor, Metallic, Roughness, ...) and a material instance's parameters, into Saved/MaterialDump.txt.
# Run with the editor closed: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file, forward slashes> (-ExecutePythonScript hangs at startup in agent copies).
# Edit PATHS below for other materials. (2026-11-15, written to find why the knight's plate looked matte.)
import unreal
mel = unreal.MaterialEditingLibrary
out = []
def log(s): out.append(s)
def dump(path):
    m = unreal.EditorAssetLibrary.load_asset(path)
    log("=== %s %s" % (path, type(m).__name__))
    if isinstance(m, unreal.MaterialInstance):
        for p in ["parent"]:
            log("parent %s" % m.get_editor_property("parent").get_path_name())
        for n in mel.get_scalar_parameter_names(m): log("scalar %s = %s" % (n, mel.get_material_instance_scalar_parameter_value(m, n)))
        for n in mel.get_vector_parameter_names(m): log("vector %s = %s" % (n, mel.get_material_instance_vector_parameter_value(m, n)))
        for n in mel.get_texture_parameter_names(m):
            t = mel.get_material_instance_texture_parameter_value(m, n)
            log("tex %s = %s" % (n, t.get_path_name() if t else None))
        return
    for prop in ["blend_mode", "shading_model", "two_sided", "used_with_skeletal_mesh", "used_with_morph_targets"]:
        try: log("%s = %s" % (prop, m.get_editor_property(prop)))
        except Exception as e: log("%s ? %s" % (prop, e))
    log("num expressions %s" % mel.get_num_material_expressions(m))
    ids = {}
    def name(e):
        if e is None: return "None"
        k = e.get_name()
        return k
    seen = set()
    def walk(e, depth):
        if e is None: return
        k = e.get_name()
        if k in seen:
            log("  " * depth + "-> (see %s)" % k); return
        seen.add(k)
        desc = type(e).__name__
        extra = []
        for p in ["parameter_name", "default_value", "r", "g", "b", "a", "const_a", "const_b", "const_alpha", "constant", "texture", "sampler_type", "coordinate_index", "u_tiling", "v_tiling", "mask_r", "mask_g", "mask_b", "mask_a", "operation", "desc", "input_name"]:
            try:
                v = e.get_editor_property(p)
                if hasattr(v, "get_path_name"): v = v.get_path_name()
                extra.append("%s=%s" % (p, v))
            except Exception: pass
        try: inames = mel.get_material_expression_input_names(e)
        except Exception as ex: inames = ["?%s" % ex]
        log("  " * depth + "%s [%s] %s inputs=%s" % (k, desc, " ".join(extra), list(inames)))
        try: ins = mel.get_inputs_for_material_expression(m, e)
        except Exception as ex:
            log("  " * depth + "  (inputs? %s)" % ex); ins = []
        for i, sub in enumerate(ins):
            if sub is None: continue
            log("  " * depth + "  <%s>:" % (inames[i] if i < len(inames) else i))
            walk(sub, depth + 2)
    for prop in ["MP_BASE_COLOR", "MP_METALLIC", "MP_SPECULAR", "MP_ROUGHNESS", "MP_NORMAL", "MP_EMISSIVE_COLOR", "MP_AMBIENT_OCCLUSION", "MP_WORLD_POSITION_OFFSET", "MP_OPACITY_MASK"]:
        try:
            e = mel.get_material_property_input_node(m, getattr(unreal.MaterialProperty, prop))
        except Exception as ex:
            log("## %s ? %s" % (prop, ex)); continue
        try: outn = mel.get_material_property_input_node_output_name(m, getattr(unreal.MaterialProperty, prop))
        except Exception: outn = "?"
        log("## %s <- %s (%s)" % (prop, name(e), outn))
        walk(e, 1)
PATHS = ["/Game/Knight/M_KnightSkin", "/Game/NewSuit/MI_Suit_Chest"]
for p in PATHS:
    try: dump(p)
    except Exception as ex: log("FAILED %s %s" % (p, ex))
import os
open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Saved", "MaterialDump.txt"), "w").write("\n".join(out))
unreal.log("MATDUMP done %d lines" % len(out))
