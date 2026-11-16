# One-off (knight_suit_import.py does the same): Used with Morph Targets on M_KnightSkin so the gauntlets (fist morphs) get their paint.
import unreal
m = unreal.EditorAssetLibrary.load_asset("/Game/Knight/M_KnightSkin")
m.set_editor_property("used_with_morph_targets", True)
unreal.MaterialEditingLibrary.recompile_material(m)
unreal.EditorAssetLibrary.save_asset("/Game/Knight/M_KnightSkin")
unreal.log("SUITDIAG morph flag set: %s" % m.get_editor_property("used_with_morph_targets"))
