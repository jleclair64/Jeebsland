# Interior fixes (2026-11-21 evening, owner's report): run by Tools\BuildInterior.ps1 after interior_import.py (or alone).
#  - the UpDraft sampler pieces (trestle desk, barstool, tankard, bottle) came in still placed where they stood in UpDraft's sample
#    scene, metres off their own origin, so the room drew them outside the house, floating: every vertex moved so the foot's middle
#    is the origin (the same rule as the Meshy props). Already there: left alone.
#  - the room read very orange: the tavern pack's plaster (MI_Plaster_Wall_001, used only by the room) a cooler, paler grey-cream
import unreal
lib = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
for p in ["/Game/Interior/Trestle/SM_Int_Trestle", "/Game/Interior/Barstool/SM_Int_Barstool", "/Game/Interior/Tankard/SM_Int_Tankard", "/Game/Interior/Bottle/SM_Int_Bottle"]:
    m = lib.load_asset(p)
    b = m.get_bounding_box()
    off = unreal.Vector(-(b.min.x + b.max.x) * 0.5, -(b.min.y + b.max.y) * 0.5, -b.min.z)
    if abs(off.x) < 1 and abs(off.y) < 1 and abs(off.z) < 1: print("INTFIX", p, "already at its origin"); continue
    d = m.get_static_mesh_description(0)
    for i in range(d.get_vertex_count()):
        v = unreal.VertexID(i)
        d.set_vertex_position(v, d.get_vertex_position(v) + off)
    m.build_from_static_mesh_descriptions([d])
    lib.save_asset(p)
    b = m.get_bounding_box()
    print("INTFIX %s moved by %.0f %.0f %.0f: now %.0f %.0f %.0f .. %.0f %.0f %.0f" % (p, off.x, off.y, off.z, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z))

pl = "/Game/Medieval_Tavern/Materials/Material_Instance/MI_Plaster_Wall_001"
src = lib.load_asset(pl)
MEL.set_material_instance_vector_parameter_value(src, "ZL - Tint", unreal.LinearColor(0.60, 0.60, 0.70, 1.0))   # (was 0.80 0.76 0.76: its picture is yellow, so blue is kept higher)
MEL.update_material_instance(src)
lib.save_asset(pl)
print("INTFIX plaster tint", MEL.get_material_instance_vector_parameter_value(src, "ZL - Tint"))


# --- 2026-11-21, all ten houses (owner: "warm but not orange"): the tavern pack's woods are a strong orange and its plaster a deep
# ochre, which is what made the first room read orange whatever the lights did. The pack is used only by the rooms, so its own
# instances are tinted: floor, wainscot and furniture woods to a richer, darker brown; and each house's plaster is its own pale
# instance (/Game/Interior/Mats/MI_Int_Plaster_<name>, set per house by Tools/interior_rooms.py through the list's Mat column).
MI = "/Game/Medieval_Tavern/Materials/Material_Instance/"
def tint(path, name, rgb):
    a = lib.load_asset(path)
    if not a: print("INTFIX missing", path); return
    MEL.set_material_instance_vector_parameter_value(a, name, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    MEL.update_material_instance(a)
    lib.save_asset(path)
PLASTER = {"": (0.85, 1.05, 2.6), "Cream": (0.90, 1.08, 2.4), "Straw": (0.94, 1.04, 2.15), "Rose": (1.00, 0.98, 2.4), "Grey": (0.72, 0.95, 2.6), "Sage": (0.70, 1.08, 2.1)}
tools = unreal.AssetToolsHelpers.get_asset_tools()
for nm, rgb in PLASTER.items():
    if not nm: tint(pl, "ZL - Tint", rgb); continue
    path = "/Game/Interior/Mats/MI_Int_Plaster_" + nm
    if not lib.does_asset_exist(path):
        a = tools.create_asset("MI_Int_Plaster_" + nm, "/Game/Interior/Mats", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(a, lib.load_asset(pl))
    tint(path, "ZL - Tint", rgb)
WOOD = (0.50, 0.68, 1.00)
for nm in ("MI_Wood_001_Wall_001", "MI_Wood_001_Wall_002", "MI_Wood_001_Wall_003", "MI_Wood_001_Wall_004", "MI_Wood_002_Beam_002"): tint(MI + nm, "ZL - Tint", WOOD)
tint(MI + "MI_Floor_002", "ZL - Tint", (0.52, 0.64, 0.80))   # (the planks: a little warmer than the wainscot, or they lean purple)
for nm in ("Chair_001", "Chair_002", "Table_001", "Table_002", "Beanch_001", "Closet_001", "Chest_001", "Shelf_002", "Stand_001", "Box_001", "Box_002", "Barrel_001", "Barrel_002", "Barrel_003", "Barrel_004", "Door_001"):
    tint(MI + "MI_" + nm, "PL - Tint", (0.60, 0.75, 1.05))
tint(MI + "MI_Carpet_002", "ZL - Tint", (0.16, 0.30, 0.33))   # (the round carpet: was a yellow olive)
tint(MI + "MI_Carpet_001", "ZL - Tint", (0.50, 0.36, 0.30))   # (the oval rug: was a loud yellow; now a soft straw)
print("INTFIX tints done")
print("INTFIX done")
