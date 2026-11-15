# The world palette

Since 2026-11-15, one file holds the world's colours: `Content/Data/Palette.ini`. The grass, the ground, Oak Hill's lawn, the tree
leaves, needles and bark, the bushes, the water and the haze all read it. Five knobs move all of them together, so a change of season
or mood shifts the whole world at once. With every knob at 0 the world looks exactly as it did before the palette existed.

## Try it in Play (no build)

Open the console (`~`) and type, for example:

```
jeeb.Palette.Autumn 0.5
jeeb.Palette.Warmth 0.3
```

The knobs take -1 to 1. A value of 1 is the strongest sensible step, so try 0.2 to 0.5 first.

| Knob | + does | - does | Touches |
|---|---|---|---|
| `Autumn` | Greens turn gold-orange. Leaves that are already orange stay as they are. Pines and redwoods take a quarter of it. | Warm leaves and dry grass turn a fresh green (summer). | Plants only |
| `Warmth` | Warmer: red up, blue down, same brightness. | Cooler. | Every surface, the haze and the water |
| `Greenness` | Plants a lusher green. | Plants a drier olive. | Plants only |
| `Saturation` | Richer colours (up to 1.5x). | Greyer (down to 0.5x). | Every surface, the haze and the water |
| `Brightness` | Lighter surfaces (up to 1.25x). | Darker. | Every surface. The haze and water keep theirs, because the exposure looks after the light. |

To keep a setting, put it in the `[Knobs]` section of `Palette.ini`. The game reads that section at start, and
`jeeb.Palette.Reload` reads it again while playing. A knob set on the command line or in the console wins over the file.

The knobs change the colour of surfaces, not the light. The sun, the sky light and the post-process grade (`jeeb.Look.*`) are
separate. For example, `jeeb.Look.Warm` warms the whole picture, the knight and the HUD included, while `jeeb.Palette.Warmth`
warms only the world's materials.

## The colours

Each line in `[Colours]` reads `Name = R, G, B, class`. Values are linear colour, as in Unreal's colour pickers. The class says
which knobs touch the colour:

- `plant`: every knob, the season most.
- `evergreen`: a quarter of the season (the `EvergreenShare` value in `[Grade]`).
- `world`: Warmth, Saturation, Brightness.
- `sky`: Warmth and Saturation.
- `none`: never changed. These are physical numbers, such as the water's absorption.

| Group | Names | Read by |
|---|---|---|
| Field | `GrassBlade`, `GrassStem`, `Bush`, `FieldHollow`, `FieldSunny`, `FieldDry`, `GroundGrass` | M_Grass, M_GrassFlower, M_Bush, the field recipe (`Tools/grass_field.py`: M_Grass, M_Ground, M_TL_Ground, the realistic grass copies), M_Ground and M_TL_Ground |
| Realistic grass | `GrassPack1`, `GrassPack2` | Meadow.cpp, which sets the Fab grass's `JeebTint`. `jeeb.Grass.Tint1` and `Tint2` still override it. |
| Earth and stone | `Earth`, `Sand`, `Snow`, `RockFar`, `Cobble`, `Moss` | M_Ground, M_Dirt, M_TL_Ground, M_Stone |
| Trees | `LeafOrange`, `LeafRed`, `LeafAmber`, `LeafGold`, `LeafGreen`, `Needle`, `NeedleLight`, `NeedleRedwood`, `Bark`, `BarkWarm`, `BarkGrey`, `BarkRed`, `BarkRedwood` | Forest.cpp (near crowns, far stand-ins, trunks), AutumnDecor.cpp (fallen and flying leaves) |
| Water | `WaterScatter`, `WaterAbsorb`, `WaterBehind`, `WaterFoam`, `WaterGlow`, `WaterGlowDusk` | WaterLook.cpp. `jeeb.Water.*` still overrides each one. |
| Sky | `HazeNoon`, `HazeLow` | DayNight.cpp (the height fog's colour, noon to the low sun) |

Some entries are multipliers on a painted picture rather than plain colours: `GroundGrass`, `Earth`, `Cobble`, `FieldHollow`,
`FieldSunny`, `FieldDry`, `GrassPack1` and `GrassPack2`. Their meaning is "the picture times this".

### Changing a colour

1. Try it live: `jeeb.Palette.Set GrassBlade 0.5 0.47 0.33`. It changes at once in Play, and the file keeps the old value.
2. Keep it: edit the line in `Palette.ini`, then run `jeeb.Palette.Reload` in Play to check.
3. Before committing, rebuild what reads the colour with the editor closed. The builders bake the file's values in as the
   collection's defaults, so the editor viewport and any machine that has not run the game also see the new colour.
   - Field, Bush, Moss, `Earth`, `Sand`, `Snow`, `RockFar`: `Tools\BuildMaterials.ps1`, then `Tools\BuildWorldMaterial.ps1`.
     Either one alone also rewrites the collection.
   - `Cobble`: also `Tools\BuildTownLab.ps1`.
   - Tree, water and sky colours: no rebuild. Code sets them.
   - Palette-only changes, which only update the collection's defaults: `Tools\BuildPalette.ps1`.

`[Grade]` sets what the knobs mean, not today's look: how warm `Warmth 1` is, the gold that autumn turns toward, the green that
summer turns toward, and how much of the season the evergreens take.

## How it works

- `/Game/MPC_JeebPalette` is a Material Parameter Collection. It holds the named colours that the built materials use (the
  list in `Tools/palette.py`, `MPC_COLOURS`) and the knobs' grade.
- **The grade lives inside each material**, at the end of its base colour. It is applied there rather than to the named colours
  because many colours are multipliers on coloured pictures. For example, the realistic grass gets its green from its own
  textures, and its tint is nearly white, so only grading the finished colour can turn that grass gold.
  - Season (plants): a colour moves toward `SeasonTint` at its own brightness, by `Autumn` times how green it is. How green it is
    gets measured against its brightness, so the muted olive grass moves as far as the bright leaves.
  - Then a 3x3 colour matrix that combines Warmth, Saturation, Brightness and, for plants, Greenness.
  - The game (`Source/Jeebsland/Palette.cpp`) works out the matrices when a knob changes and writes them into the collection. It
    never does this per frame.
  - At the default knobs the season mix is 0 and the matrix rows are exactly (1,0,0), (0,1,0), (0,0,1), so the result equals the
    input bit for bit.
- **Mixed materials** (M_Ground, M_TL_Ground) apply the season and Greenness to their grass layer only, and the World matrix to the
  whole. Earth, rock and cobbles never go gold or green.
- Colours that code sets on material instances (trees, the realistic grass's tint) come from `JeebPalette::Raw`, and the material
  grades them. `JeebPalette::Bind` sets one now and again after a reload.
- Things that are not materials (the haze, the water's scattering and glow) take `JeebPalette::Graded`. This is the same maths done
  in C++, a copy of `_season` and `_matrix` in `palette.py`, so keep the two the same.
- The realistic grass copies in `/Game/GrassReal` get the grade from `Tools/grass_packs.py` (`BuildGrassPacks.ps1`, on a machine
  with the Fab packs). `Tools/BuildPalette.ps1` adds it in place once to the committed copies.
- **A material may read at most two collections.** The Fab grass's wind function already reads two in `M_GrassLibrary` and
  `M_MeadowFlowers`, so those two take the grade as plain vector parameters named `Pal<name>` (`PARAM_MODE` in `palette.py`), with
  the same identity defaults. Meadow.cpp hands its grass instances to `JeebPalette::BindGrade`, which sets them now and after every
  change. `M_OpenWorldFoliage` reads one other collection, so it reads the palette's.

### Pitfall: the collection's hidden Ids

A material's collection node stores the parameter's hidden Id, not its name, and works out the name from the Id when it loads.
If a parameter gets a new Id, every material built before loses that colour on its next load. It compiles to the default grey
material and logs `CollectionParameter has invalid parameter None`. A material that is still in the shader cache can look fine
until it next recompiles, so a quick look does not prove anything.

Writing the collection's parameter list from Python gives every parameter a new Id. `palette.ensure_mpc()` therefore goes through
`UJeebPaletteTools::SyncVectorDefaults` (C++, `JeebPaletteTools.cpp`). It changes the default values in place and adds new names
at the end, so existing parameters keep their Ids. Never set `vector_parameters` from Python.

`Tools\BuildPalette.ps1` ends with `PALETTEDIAG CHECK` lines. In a fresh process it loads each palette material and fails
(`PALETTEDIAG BROKEN`) if any node has lost its parameter, so run it after the other builders.

## Adding a material to the palette

In its Python builder:

```python
import palette
palette.ensure_mpc()                                   # once, before building
tint = palette.colour(mat, "Earth")                    # a named colour instead of a Constant3Vector
col = palette.grade_world(mat, col)                    # earth, stone, wood, plaster, roofs
col = palette.grade_plant(mat, col)                    # grass, leaves, bushes, flowers' stems
col = palette.grade_plant(mat, col, share=True)        # evergreens
col = palette.grade_plant_extra(mat, grass_layer)      # a plant layer in a mixed material (then grade_world on the whole)
```

Put the grade on the final base colour, before anything that is derived from it (subsurface, a glow that is a share of the base
colour). Never put it on flames, windows or other light sources. For a new named colour, add a line to `Palette.ini` and, if a
material reads it, its name to `MPC_COLOURS` in `palette.py`. Then run the builder, which adds it to the collection. For a colour
that code sets, use `JeebPalette::Raw` or `Bind` (or `Graded` when the thing is not a material). The cost is about 20
instructions per pixel for a plant grade and about 10 for a world grade.

## Not on the palette yet

- **Buildings (phase 2):** `M_TL_Building` and `M_Building`. The plan is one `palette.grade_world` call at their base colour,
  plus palette entries for plaster, timber, roof and stone if the owner wants to choose those.
- M_Color: the code-built props, pumpkins, hay and the castle's cubes.
- The plaza oak's and the town's weeds and ivy's imported materials.
- M_Water itself. It is not rebuilt; WaterLook pushes every water colour.
- The sun and the sky light. These are lighting, not surfaces.
