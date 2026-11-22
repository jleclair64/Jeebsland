# Adds a glow mask to the TownDecor parent materials (2026-11-21, agent shatter; safe to run again: it does nothing once the parameter is there).
#   emissive = (what it was: picture x GlowAmount x GlowTint) x GlowMaskTex.R, GlowMaskTex white by default, so a kind without a mask glows as before.
# The lamps' Glow material instances set GlowMaskTex to <Piece>_GlowMask (filler_clean.py writes it, town_decor_copy.py copies it), so the glass
# edge comes from the picture, not from which triangles are in the Glow slot. Run by Tools\PatchGlowMask.ps1 with the editor closed.
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
for path in ("/Game/TownDecor/M_TownDecor", "/Game/TownDecor/M_TownDecorTwoSided"):
    m = EAL.load_asset(path)
    if m is None:
        unreal.log("GLOWMASK missing " + path); continue
    if "GlowMaskTex" in [str(x) for x in MEL.get_texture_parameter_names(m)]:
        unreal.log("GLOWMASK already in " + path); continue
    node = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    if node is None:
        unreal.log("GLOWMASK no emissive node in " + path); continue
    t = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 1200)
    t.set_editor_property("parameter_name", "GlowMaskTex")
    t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    t.set_editor_property("texture", EAL.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -100, 1000)
    MEL.connect_material_expressions(node, "", mul, "A")
    MEL.connect_material_expressions(t, "R", mul, "B")
    MEL.connect_material_property(mul, "", MP.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_asset(path)
    unreal.log("GLOWMASK patched " + path)
unreal.log("GLOWMASK done")
