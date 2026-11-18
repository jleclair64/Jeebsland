# Town buildings checklist

Started 2026-11-11 (owner). Every town needs the seven **required buildings** below. A town can also have **unique buildings** of its own, like Oak Hill's church. Tick a box when its model is chosen and reduced, and say on the line how far it has got.

How a building gets made:
- Look in Meshy's free library first (`Docs/MeshyCommunityLibrary.md`), or draw a picture and make it in Meshy (Oak Hill's bank, below).
- Shrink it to about 300k triangles with `Tools/building_reduce.py`.
- Bring it into the game with `Tools/BuildBuildings.ps1`. Its entry in `Tools/building_clean.py` sets its size.
- Check it in the game in sun and in shade.
- Delete the full-size `_Detailed` file afterwards. The link on its line is enough to download it again.

Files live in `Art/Buildings` as `<Town><Building>_Detailed.glb` (the full download, kept out of git) and `<Town><Building>_300k.glb` (what the game uses).

## For every building, later
- [ ] **Smoke from every chimney** (owner, 2026-11-11): a soft drifting plume, BotW-style, added to the building as its own piece. That fits the rule that decorations are separate.
- [ ] Decorations, each designed or found separately: lanterns and torches with real lights, flower boxes, signs.

## Required buildings, by town
The seven: **Home** (houses, one design reused and recoloured), **Store**, **Bank**, **Town Hall**, **Boxing Ring**, **Smith**, **Potion Shop**.

### Oak Hill (`Docs/OakHillPlan.md`)
- [x] Home: the 300k Nanite test house, `OakHillHouse_300k.glb`, picked by the owner 2026-11-07. Many copies measured 2026-11-12: all 23 Oak Hill buildings in view cost about 0.9 ms of GPU (editor); the owner decides whether houses drop to 150k.
- [x] Store: "Timbered Market House", https://www.meshy.ai/posts/019deaa3-2799-7726-a677-75fec8192529. Reduced from its FBX to `OakHillStore_300k.glb` (14 MB). Keeps its awning, crates and sign (owner).
- [x] Bank: made in Meshy from a picture styled on the house and the store.
  - Picture task `01a1cdfa-b3a0-7386-a51a-75f818a7b711`, model task `01a1cdfc-a27b-7007-862f-16330e6aff43` (details in `Docs/OakHillPlan.md`).
  - The stone was repainted a little warmer to match the picture. The owner approved the colour (2026-11-11).
  - Reduced: `OakHillBank_300k.glb` (24 MB).
- [x] Town Hall: drawn in Meshy (picture task `01a1ce15-89e6-77fe-b4fe-ee6565110573`), made 3D (task `01a1ce18-05e2-75bd-b36d-897b6d4db81b`). `OakHillTownHall_300k.glb`.
- [ ] Boxing Ring
- [x] Smith: drawn in Meshy (picture task `01a1ce18-08cc-723f-86dc-70d38b3a3593`), made 3D (task `01a1ce1b-f0fb-71ce-8db0-8d9ba668fff1`). `OakHillSmith_300k.glb`.
- [x] Potion Shop: drawn in Meshy (picture task `01a1ce11-cd48-75ba-b685-6232fd8ef6c5`, with lit windows), made 3D (task `01a1ce15-8705-7658-9066-5b6628876de6`). `OakHillPotionShop_300k.glb`.
- Second house design, the **corner house**: Meshy redrew the library's "Taverne 1" by janintibo without its sign, barrels and other extras (picture task `01a1ce1c-4c34-71f2-b4ad-106012f2ca79`), then made it 3D (task `01a1ce1e-6cde-741d-91ac-395dfba2e986`). `OakHillCornerHouse_300k.glb`.
- Unique, chosen: **Church** "Emberstone Chapel" by METATIGER (library, CC0) as `OakHillChurch_300k.glb`; **Well** "Stone-Walled Well" by METATIGER (library, CC0) as `OakHillWell_150k.glb` (small, so 150k).
- [x] War buildings (2026-11-18, made while the owner was away; the owner to judge): Meshy image-to-image pictures (nano-banana-pro, the smith and bank pictures as style references, 9 credits each), then image-to-3D (meshy-7.1 standard, PBR, 30 each), reduced to 300k (`Tools/building_reduce.py`) and built by `Tools/BuildWarBuildings.ps1` into `/Game/TownLab/War`. No library model fitted (tags fort, ruins: no long palisade fort or large castle ruin in the town's style).
  - **Fort** (both forts, one model): a log palisade on a rough stone footing, a fighting walk along its back wall, two little blockhouse towers with tiled roofs, a plank gate with iron straps. Picture task `01a1ef09-f020-7579-91e7-b00819aae974`, model task `01a1ef0a-fe1f-7565-a994-cda466ae91dd`. `OakHillFort_300k.glb`; game model `SM_TL_Fort` (60k, baked 2048). 14.5 x 5.8 m, gate 2.3 m, palisade 3.3 m, blockhouses 5.2 m.
  - **Watchtower**: a round stone tower with an overhanging timber lookout and a conical tiled roof. Picture `01a1ef09-f026-73de-b763-f5f907aec653`, model `01a1ef0a-fe56-77a7-9153-8adaa21ca10f`. `OakHillWatchtower_300k.glb`; `SM_TL_Watchtower` (40k, baked 2048). 5.3 m across, 13 m tall, door 2.3 m.
  - **Castle Ruins**: a roofless old hall of pale stone with tall empty Gothic windows, a great arched gateway, a broken round corner tower, moss and ivy. Picture `01a1ef09-f1c6-748e-9a69-2c209653197a`, model `01a1ef0a-feb5-76dd-a6a7-e7201e14ac3a`. `OakHillRuins_300k.glb`; `SM_TL_Ruins` (200k, Meshy's own 2048 pictures: the bake's UVs fell apart on its tracery, so `Tools/war_ruins_lowmodel.py` reduces it instead). 19 x 10.2 m, 11.9 m tall.
  - The `_Detailed.glb` downloads are in `Art/Buildings` (git-ignored, this PC only).
  - Placed (`OakHill.h` plots 27, 30-32; `TownLabSwaps.h`): the forts on the river bank north and south of the west road (not levelled: each sits a quarter of the way up the slope under its walls, its footing sunk into the rest), the watchtower on the bank northwest of the north fort (moved from the plan's spot, which the spacing pass pushed into the houses), the ruins on the castle knoll, levelled like a house. `-NoWarBuildings` (test runs) leaves their meshes out for frame timing; their walls stay.
- Unique, still to make: Garden, Graveyard, stone Bridge, the big oak.
- Every Oak Hill building is repainted to one colour scheme (`Tools/building_palette.py`, measured from the approved bank: warm grey stone, walnut timber, cream plaster, red-brown tiles) and its windows glow a soft yellow (`Tools/building_glow.py`, owner 2026-11-11: all current and future Oak Hill buildings). The bank and the first house keep their approved colours.
- **All of these are in the game** (2026-11-12): Oak Hill is laid out from the plan by `AOakHillTown` (see CLAUDE.md "Oak Hill is built"), with 16 houses (the plan's house north of the bank is left out: no room). The reviewers passed them; the `_Detailed` originals stay until the owner has seen the town. The church and well need their library links found before their originals go.

### Anchorage
- [ ] Home
- [ ] Store
- [x] Bank: "Bank" by brunobautistabellot, https://www.meshy.ai/posts/019fa562-64c9-7477-b0f2-cbb31cd5ebc2 (CC0, free). It's a white marble temple front with columns and gold doors.
  - 646k faces, reduced to 300k: `AnchorageBank_300k.glb` (17 MB). Not in the game yet.
  - Things to know: the back is a plain marble wall with no windows. From above, the flat roof has two dark slabs that look black, which can be repainted if it shows from hills.
- [ ] Town Hall
- [ ] Boxing Ring
- [ ] Smith
- [ ] Potion Shop
- Unique: (owner to say)

### Silver Point
- [ ] Home
- [ ] Store
- [ ] Bank
- [ ] Town Hall
- [ ] Boxing Ring
- [ ] Smith
- [ ] Potion Shop
- Unique: (owner to say)

### Birch Grove (the big walled troll town, in PvP 2)
- [ ] Home
- [ ] Store
- [ ] Bank
- [ ] Town Hall
- [ ] Boxing Ring
- [ ] Smith
- [ ] Potion Shop
- Unique: the town wall (owner to say more)

### Steele Creek
- [ ] Home
- [ ] Store
- [ ] Bank
- [ ] Town Hall
- [ ] Boxing Ring
- [ ] Smith
- [ ] Potion Shop
- Unique: (owner to say)

### Palmer Square
- [ ] Home
- [ ] Store
- [ ] Bank
- [ ] Town Hall
- [ ] Boxing Ring
- [ ] Smith
- [ ] Potion Shop
- Unique: (owner to say)

### Blackrock
The original walled town. Its buildings are built in code today (store, smithy, bank, cottages). Ask the owner whether it gets Meshy buildings like the others.

Towns come from `JeebWorld::Towns()` in `Source/Jeebsland/WorldMap.h`.
