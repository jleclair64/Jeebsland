# TownLab buildings (2026-11-14)

The house and the common store, rebuilt as game models for the TownLab test town (style: `StyleSheet.md`, Overwatch's Eichenwalde).
They have the same shape, footprint, door and scale as Oak Hill's `SM_OakHillHouse_300k` / `SM_OakHillStore_300k`, so they go into the same plots with the same `FModel` row (OakHill.h).

## What they are
| | Old (`/Game/Buildings`) | New (`/Game/TownLab/Buildings`) |
|---|---|---|
| House | `SM_OakHillHouse_300k`: 298k triangles, Nanite, 1 material, 2048 textures | `SM_TL_House`: 60k triangles (flower boxes spared in the reduction), Nanite, 1 material, 4096 colour + normal, 2048 MR/glow/mask |
| Store | `SM_OakHillStore_300k`: 299k, Nanite, 1 material, 4096 textures | `SM_TL_Store`: 50k, Nanite, 1 material, same texture sizes as the house |
| Collision | `SM_*_Col` (20k, complex as simple) | `SM_TL_House_Col`, `SM_TL_Store_Col` (copies of the same 20k meshes) |

- **Detail is baked, not modelled.** `Tools/townlab_bake.py` (Blender) collapse-reduces the finished 300k model (lanterns already taken off, plaster smoothed and colour flattened by building_clean.py and building_flatten.py). It lays out fresh UVs and bakes onto the low model:
  - the colour;
  - a tangent normal map of the high model's shape plus its own normal map;
  - roughness/metal;
  - the window glow;
  - the window marks;
  - ambient occlusion and edge convexity.
- **The painted look** is applied to the baked colour (numbers at the top of townlab_bake.py):
  - each colour group graded in Lab: plaster to a warm butter cream, wood richer and less orange, thatch golden and a little less saturated, stone warm grey;
  - the occlusion painted in warm, never black;
  - bevelled edges picked out about 10% lighter;
  - a soft gradient, lighter at the top and slightly deeper and greener at the foot.
- **Material:** `M_TL_Building` is M_Building's graph with the same parameters, so OakHillLife's glow code works unchanged:
  - parameters `Night`, `GlowStrength`, `HouseOffset` (custom primitive data 0), `RimStrength` and the rest;
  - the window marks come from `T_TL_<Name>_Mask` (R a random number per window, G window, B never glows) instead of vertex colour, so the low model needs no vertex colours;
  - MR's red channel feeds ambient occlusion.
- **To rebuild:** `Tools\BuildTownLabBuildings.ps1` (bake, then import, with the editor closed on the project). It needs `Tools\BuildBuildings.ps1 -CleanOnly OakHillHouse_300k OakHillStore_300k` once first.

## Decorations (separate pieces, owner rule)
Made by `Tools/townlab_decor.py`, which also writes the placement list `Source/Jeebsland/TownLabDecor.h`.
- `SM_TL_IvyClimb`, `SM_TL_IvyWide`, `SM_TL_IvyHang`, `SM_TL_MossFringe`: ivy and moss cards, two bent layers each (96 triangles), all on one painted atlas `T_TL_Foliage` (2048, alpha). Material `M_TL_Foliage`: masked, two-sided.
- `SM_TL_Weeds`: three crossed leaf cards (6 triangles) at the foot of the walls.
- `SM_TL_Stones`: small lumps, coloured by `MI_TL_Stones` (`M_TL_VertexPaint`). `SM_TL_MossClump` (`MI_TL_Moss`) is made but not placed (moss=True on a building in townlab_decor.py tries it again).
- Oak Hill's filler props from `/Game/Filler` (the Meshy library pieces, CC0, `Art/Filler/Sources.md`): firewood, watering can, pots, pail, crates, sacks, barrel, basket, and lanterns where building_clean.py took the old ones off (`bLight`).

The planner reads the real model with raycasts:
- the walls' faces, the windows (from the window marks) and the doors;
- how high each wall runs clear under the eaves;
- the roof surface just above each eave, where the moss sits.

It then places:
- ivy at corners and on clear stretches, never over a window or the door;
- a hanging tress under the eaves;
- (moss along the eaves: off);
- stones and weeds at the corners;
- a few props against the side and back walls.

### Using the placements (for the town)
- `JeebTownLabDecor::For(TEXT("TL_House"))` returns the pieces.
- `JeebTownLabDecor::Place(BuildingTransform, Piece)` returns the world transform. The building transform is the building component's own, with its FModel Scale.
- Ivy, moss and lanterns scale with the building. Props, stones and weeds keep their own size.
- Draw one instanced set per TownLab mesh, which is one draw per kind. The `/Game/Filler` materials are not flagged for instancing, so use plain components for those.
- No shadows from ivy, moss, weeds or stones.
- Lantern lights: in the MoveLab lineup each is a point light of 70 lumens, radius 3 m, warm orange, no shadows. Switch them on with the windows at dusk.

## Looking at them
- MoveLab with `-JeebShot -TownLabBuildings` puts old and new side by side, past the +Y test houses, at Y 5600. The order is old house, new house, old store, new store.
- `-TownLabSet=old|new` shows one pair only, for frame-cost runs. Log JEEBTLB gives each building's triangles, materials and decor draws.
- `-ShotEyes=X_Y_Z_Yaw_Pitch+...` takes several free-camera stills in one run (Z above the floor).

## Frame cost (MoveLab, 2026-11-14, editor -game at 1280x720, solo on the machine, JEEBGPU medians; two runs per view)
| View | Old pair | New pair (with decor) |
|---|---|---|
| Overview of both buildings | 4.13 / 3.75 ms GPU, 76 draws | 3.81 / 3.73 ms, 104 draws |
| House close (same angle) | 3.77 ms, 78 draws | 3.83 ms, 109 draws |
| Store close (same angle) | 4.06 ms, 79 draws | 3.96 ms, 116 draws |
The difference is within run-to-run noise. Nanite triangles are not in the RHI count; the extra ~30-40 draws and ~80-140k triangles are the decorations, mostly the library props (non-Nanite, 4-16k triangles each). An earlier run measured the store close-up +0.3 ms; if a town of them costs too much, make the props Nanite or swap in lighter ones first.

## Textures
No outside textures were downloaded and no Meshy credits were spent (0): everything is baked from the existing Meshy models, painted in `townlab_decor.py`, or Oak Hill's CC0 filler library.

## For the owner to judge
- Ivy sits beside the corner posts and is narrow where the window is close to the post; the store has ivy only at its corners (so its doors and shop front stay clear).
- The thatch's straw is a little softer close up than the 300k model's (60k triangles plus a 4096 picture); from street distance it reads the same.
- Kept from the original models (not changed here): a dark seam where the house chimney meets the gable, the store's darker back wall stone (same as the old store), the store's hanging sign going nearly black at dusk.
- Moss clumps on the roofs were tried and taken out (they read as stickers); the model's own ridge moss is graded into the straw.

## Town hall, church, well and potion shop (build-b, 2026-11-14)
| | Old (`/Game/Buildings`) | New (`/Game/TownLab/Buildings`) |
|---|---|---|
| Town hall | `SM_OakHillTownHall_300k`: 300k | `SM_TL_TownHall`: 60k, Nanite, 1 material (`MI_TL_TownHall`) |
| Church | `SM_OakHillChurch_300k`: 299k | `SM_TL_Church`: 60k, Nanite, 1 material |
| Potion shop | `SM_OakHillPotionShop_300k`: 300k | `SM_TL_PotionShop`: 50k, Nanite, 1 material |
| Well | `SM_OakHillWell_150k`: 150k | `SM_TL_Well`: 26k, Nanite, 1 material (no windows) |
- Same pivot, door (+Y) and FModel Scale as the old models; collision `SM_TL_<Name>_Col` from building_clean.py's `_Col` meshes.
- Their roofs are red clay tiles, not thatch: townlab_bake.py's "tile" group (above `tile_z`, the eave height) is shifted to one town colour, `TILE_TARGET` (the town hall's graded terracotta), so every roof matches. The church's stained glass, which building_clean.py read as forge fire, is turned into window marks (`fire_as_window`): dark by day, lit at dusk.
- Decor (townlab_decor.py): town hall 27 pieces, church 29 (its door lantern back as a lit lantern), potion shop 31, well 8. `ivy_min` keeps ivy to full-size patches on walls cut up by windows; `no_hang` drops the eave tress (the town hall's jetty, the church's chimney); `no_ivy` and `feet` keep the well to a pail, two stone piles and weeds.
- Import only these: `Tools\BuildTownLabBuildings.ps1 -Only TL_TownHall,TL_Church,TL_Well,TL_PotionShop` (reuses the shared M_TL_Building, T_NoGlow and decor). Needs `Tools\BuildBuildings.ps1 -CleanOnly OakHillTownHall_300k OakHillChurch_300k OakHillWell_150k OakHillPotionShop_300k` once first.
- Lineup: `-TownLabBuildings` shows them in a second yard behind the house and store (town halls and churches 36 m back, potion shops and wells 74 m back; the new one of each pair is on the left seen from the front).
- Frame cost (performance reviewer, MoveLab 1920x1080 noon, two runs each): overview old 4.17 ms / new 4.13 ms GPU, close view old 3.95 / new 4.12 (noise 0.2-0.27 ms); about +58 draws, all decor. No Meshy credits spent.
- For the owner: the church keeps a painted crack on one side's plaster and its painted stained glass from the original model; the backs of the buildings read greyer only in shade, as the old ones do.
