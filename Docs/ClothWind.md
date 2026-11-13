# Cloth wind (M_ClothWind)

For banners, awnings, market canopies, flags, washing lines: cloth that leans downwind and flutters with the same gust field as
the grass and trees (`add_wind` in `Tools/build_materials.py`, approved by the owner). Built by `Tools/BuildTownLifeMaterials.ps1`
(editor closed; makes only `M_ClothWind` and `M_ChimneySmoke`). Not used by any prop yet.

## Using it on a prop
1. Make a material instance of `/Game/M_ClothWind` (in `prop_import.py` / `building_import.py`: `set_material_instance_parent`, then
   `set_material_instance_scalar_parameter_value` / `..._vector_parameter_value`).
2. Parameters:
   - `Color` (vector): the cloth's colour.
   - `Sway` (default 1): how far it moves. 0 = still, 1 = about 30 cm at the free edge in a full gust (the same gusts as the grass),
     0.5 for a heavy canvas awning, up to 2 for light silk.
   - Which parts are fixed (the "attach mask": 0 = pinned, 1 = free):
     - Default: from the mesh's UV **V**. V = 0 at the top of the UV square stays still, V = 1 moves most, so a banner whose texture
       square runs top to bottom hangs correctly with no further setup.
     - `MaskFromU` = 1: from UV U instead (a flag fixed at its left side, U = 0).
     - `MaskFromVertexColor` = 1: from the vertex colour's red (paint 0 where it is nailed, 1 where it hangs free). Use this for
       awnings with several attached edges.
     - `FlipMask` = 1 turns the mask round. `MaskPower` (1.5) eases it so the fixed edge stays truly still.
3. The movement is in world space (it leans along the wind, not along the mesh), so it needs no per-prop direction.
4. The mesh must have enough vertices where it bends (a flat banner of 2 triangles can only slide: give it a 6x10 grid or more).
5. Nanite: leave Nanite off for cloth meshes unless Nanite's world position offset is switched on in the project; cloth is small and
   cheap as a normal static mesh.
6. Lit by the sun and two-sided; the colour lightens a little where a gust is passing, like the grass.
