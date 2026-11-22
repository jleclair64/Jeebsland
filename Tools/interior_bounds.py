import unreal, os
lib = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
out = []
for folder in ["/Game/Interior", "/Game/Medieval_Tavern/Meshes", "/Game/TownDecor", "/Game/Props", "/Game/Filler"]:
    for p in sorted(lib.list_assets(folder, recursive=True)):
        a = lib.load_asset(p)
        if not isinstance(a, unreal.StaticMesh): continue
        b = a.get_bounding_box()
        mats = [str(m.material_slot_name) + "=" + (m.material_interface.get_name() if m.material_interface else "None") for m in a.static_materials]
        out.append("B,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%s" % (p.split(".")[0], b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z, a.get_num_triangles(0), "|".join(mats)))
for p in sorted(lib.list_assets("/Game/Medieval_Tavern/Materials/Material_Instance", recursive=False)):
    a = lib.load_asset(p)
    if not isinstance(a, unreal.MaterialInstanceConstant): continue
    vs = ["%s=(%.2f %.2f %.2f)" % (n, MEL.get_material_instance_vector_parameter_value(a, n).r, MEL.get_material_instance_vector_parameter_value(a, n).g, MEL.get_material_instance_vector_parameter_value(a, n).b) for n in MEL.get_vector_parameter_names(a.get_base_material())]
    ss = ["%s=%.2f" % (n, MEL.get_material_instance_scalar_parameter_value(a, n)) for n in MEL.get_scalar_parameter_names(a.get_base_material())]
    out.append("M,%s,parent=%s,%s,%s" % (a.get_name(), a.parent.get_name() if a.parent else "-", ";".join(vs), ";".join(ss)))
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "dump.txt"), "w") as f: f.write("\n".join(out))
print("ROOMSDUMP done", len(out))
