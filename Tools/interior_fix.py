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
print("INTFIX done")
