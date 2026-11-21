# The thatched house's Meshy community props (2026-11-21, Art/Interior/Meshy, CC0): each one reduced for the game and written as a
# GLB that Tools/interior_import.py brings into /Game/Interior. Run by Tools\BuildInterior.ps1 in Blender.
# Per prop: (name, file under Art/Interior/Meshy, triangles to keep, size in cm, which size: 'h' height / 'l' longest side).
# Every prop ends with its origin at the middle of its foot, Z up, at real size (1 unit = 1 m; the importer makes it cm).
# Pictures: base colour 1024, the others 512 (they come at 2048-4096).
import bpy, sys, os
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)
ONLY = [s for s in os.environ.get("INTERIOR_ONLY", "").split(",") if s]
PROPS = [
    ("Bookshelf",    "bookshelves/OldBookshelf.glb",        16000, 195, 'h'),
    ("BraidedRug",   "rugs/BraidedRug.glb",                  3000, 190, 'l'),
    ("Tapestry",     "tapestries/ValeTapestry.glb",          6000, 150, 'h'),
    ("WallMap",      "maps-scrolls/TreasureMap.glb",         3000,  90, 'l'),
    ("Scroll",       "maps-scrolls/AncientScroll.glb",       3000,  45, 'l'),
    ("Bed",          "bed/SunlitBed.glb",                   11000, 205, 'l'),
    ("Chest",        "chest/TreasureChest.glb",              8000,  95, 'l'),
    ("PotionCrate",  "chest/PotionCollectionChest.glb",     10000,  60, 'l'),
    ("Cauldron",     "alchemy/MedievalCauldron.glb",         8000,  70, 'l'),
    ("Bottles",      "alchemy/EightAlchemyBottles.glb",     12000,  75, 'l'),
    ("FlowerPot",    "plants/BlossomsInPot.glb",             8000,  50, 'h'),
    ("Candlesticks", "candles/CandlestickPair.glb",          4000,  30, 'l'),
    ("Candelabra",   "candles/EternalIllumination.glb",      6000,  48, 'h'),
    ("BookStack",    "books/StackOfVintageBooks.glb",        4000,  32, 'l'),
    ("BooksRow",     "books/OldBooksRow.glb",                6000,  55, 'l'),
    # the UpDraft sampler (Fab, free): no reduction, its own size; only its foot moved to the origin (it comes placed in its sample scene)
    ("Trestle",  "../Fab/Updraft/full/UpDraft_Art_Stylized_Tavern_Inn_Room_SAMPLER/Export/GLB/UD_TIR_Trestle_Table.glb",     999999, 0, "l"),
    ("Barstool", "../Fab/Updraft/full/UpDraft_Art_Stylized_Tavern_Inn_Room_SAMPLER/Export/GLB/UD_TIR_Barstool.glb",          999999, 0, "l"),
    ("Tankard",  "../Fab/Updraft/full/UpDraft_Art_Stylized_Tavern_Inn_Room_SAMPLER/Export/GLB/UD_TIR_Tankard.glb",           999999, 0, "l"),
    ("Bottle",   "../Fab/Updraft/full/UpDraft_Art_Stylized_Tavern_Inn_Room_SAMPLER/Export/GLB/UD_TIR_Bottle_03_Medium.glb",  999999, 0, "l"),
]
for name, rel, keep, size, how in PROPS:
    if ONLY and name not in ONLY: continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(src, rel))
    obs = [o for o in bpy.data.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs: o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    if len(obs) > 1: bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    for o in list(bpy.data.objects):
        if o != ob: bpy.data.objects.remove(o)
    ob.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    tris0 = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    if tris0 > keep:
        m = ob.modifiers.new("dec", 'DECIMATE'); m.ratio = keep / tris0; m.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier="dec")
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    ext = hi - lo
    s = (size / 100.0) / (ext.z if how == "h" else max(ext)) if size > 0 else 1.0
    foot = Vector(((lo.x + hi.x) * 0.5, (lo.y + hi.y) * 0.5, lo.z))
    ob.data.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-foot))
    for img in bpy.data.images:
        if img.size[0] == 0: continue
        base = any(n.type == 'TEX_IMAGE' and n.image == img and any(l.to_socket.name == 'Base Color' for l in n.outputs[0].links)
                   for mt in bpy.data.materials if mt.use_nodes for n in mt.node_tree.nodes)
        t = 1024 if base else 512
        if img.size[0] > t: img.scale(t, t)
        img.pack()
    tris1 = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    ob.name = name
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, "SM_Int_" + name + ".glb"), export_format='GLB', use_selection=False)
    e = ext * s
    print("INTPROP %s %d -> %d tris, %.0f x %.0f x %.0f cm" % (name, tris0, tris1, e.x * 100, e.y * 100, e.z * 100))
