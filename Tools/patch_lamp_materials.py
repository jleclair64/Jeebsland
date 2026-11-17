# One-off material patch (run ONCE: it is not idempotent) (2026-11-16, owner: harsh white lamps, leaves glowing in shade). Run by Tools\PatchLampMaterials.ps1 in a windowless editor.
#  - /Game/TownDecor/M_TownDecor and M_TownDecorTwoSided: emissive = picture x GlowAmount x GlowTint (a new vector parameter, default a 2200 K flame).
#    Source/Jeebsland/LampLook.cpp sets GlowTint and GlowAmount live (jeeb.Lamp.Tint / jeeb.Lamp.Glow).
#  - /Game/M_LeafLitter and /Game/M_FlyLeaf: the faint "never pitch black" glow 0.14 -> 0.04, so a leaf in a tree's shade is dark.
import unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

for path in ("/Game/TownDecor/M_TownDecor", "/Game/TownDecor/M_TownDecorTwoSided"):
    m = EAL.load_asset(path)
    if m is None:
        unreal.log("LAMPDIAG missing " + path); continue
    node = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    if node is None:
        unreal.log("LAMPDIAG no emissive node in " + path); continue
    tint = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 1000)
    tint.set_editor_property("parameter_name", "GlowTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 0.5, 0.13, 1.0))
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 800)
    MEL.connect_material_expressions(node, "", mul, "A")
    MEL.connect_material_expressions(tint, "", mul, "B")
    MEL.connect_material_property(mul, "", MP.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_asset(path)
    unreal.log("LAMPDIAG patched " + path)

for path in ("/Game/M_LeafLitter", "/Game/M_FlyLeaf"):
    m = EAL.load_asset(path)
    if m is None:
        unreal.log("LAMPDIAG missing " + path); continue
    node = MEL.get_material_property_input_node(m, MP.MP_EMISSIVE_COLOR)
    if node is None or not isinstance(node, unreal.MaterialExpressionMultiply):
        unreal.log("LAMPDIAG " + path + " emissive node is " + str(node)); continue
    node.set_editor_property("const_b", 0.04)
    MEL.recompile_material(m)
    EAL.save_asset(path)
    unreal.log("LAMPDIAG leaf glow 0.04 in " + path)
unreal.log("LAMPDIAG done")
