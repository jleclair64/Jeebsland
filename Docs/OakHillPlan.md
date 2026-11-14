# Oak Hill town plan

**Reorganised 2026-11-13** (owner: houses "about 20%" smaller, shops "way too big, they make the knight look tiny", "the stores should sit around the square in the middle of the town", "another 20% closer together, and twice as hilly", the boxes round the stalls removed, and shop popups). What changed, in `OakHill.h`:
- **Scale:** 14 cm per sketch unit (`Unit`, was 17.5), hills twice their rise (`HillRise` 1.5, was 0.75). Roads stay 3.5 m wide. The ring road is radius 100 units (14 m) so the shops fit round it; the oak's lawn is radius 60 units (8.4 m). The west foot of the hills starts further from the river (`BaseM`) so the west road stays walkable.
- **Building sizes** (`Models()`, each the drawn size; doors measured on the models): houses 0.96 (11.5 x 8.6 m, door 2.4 m), corner house 1.0 (9 x 9.8 m, door 1.7 m), smith 0.65, bank 0.75 (arched door 2.1 m), store 0.75 (counter 0.9 m), potion shop 0.82 (door 1.9 m), town hall 0.85, church 0.85, well 0.85, market stall 0.7 (counter 1.0 m), bank stall 0.7, potion stall 0.8 wide but full height (`ScaleZ` 1: its counter would be knee high). Sizes were judged against the knight (1.9 m): doors and counters close to his height and chest.
- **Shops round the plaza** (`Ring` in `Plots()`), each 0.8 m outside the ring road, long side along it, door or open front toward the oak (`bFacePlaza`). Angles counter-clockwise from east: Bank Stall 24, Bank 66, Smith 115, Potion Stall 157, Potion Shop 205, Store 295, Market Stall 336. The four roads leave the ring at 0, 90, 180 and 270; 245 is left for the southwest road.
- **Other plots:** the town hall east of the plaza (497,410), the church (536.5,96.5), the well on the lawn (282,435), 16 houses re-settled on the offline map, the empty plots kept (boxing ring, castle ruins, garden, graveyard, forts, watchtower, bridge). Roads redrawn round the bigger ring (`Roads()`).
- **Fixed:** a plot's turn now comes from the plan's own long side (`FPlot::bAlongW`), not the model's measured sides, which had turned the nearly square bank, store and corner house 90 degrees off their walls.
- **Clutter removed:** the barrels, crates, sacks, cart and rope coil round the stalls (`BuildStoreClutter`) are gone (owner: "We'll add our own").
- **Shop counters** (`Counters()`, `NearCounter`): a spot 1.2 m in front of each shop's door or open front. E there opens the shop's window: the smith the smithy, the bank and bank stall the bank, the store, market stall, potion shop and potion stall the store (it sells potions). The server checks every trade against the same spots (`NearTrader`, 8 m, like Blackrock's traders); walking 6.5 m away closes it. Shopkeepers come later.
- **Measured** (JEEBOAK, 2026-11-13): closest buildings 3.1 m (the church and the house west of it); steepest road 12.3 degrees (the west road where it drops to the river), the north arc 11.2, every other road under 10; heights above the river: plaza 6.86 m, castle knoll 9.49, church rise 6.32, saddle 5.30, boxing ring 5.22; steepest open ground away from the rivers 29.6 degrees, on the castle knoll's flank (no road there). Each counter is 15.4 to 16.1 m from the oak.

**Built 2026-11-12** (Source/Jeebsland/OakHill.h, OakHillTown.cpp): the hills, roads, plots and buildings below are in the game. Differences from this plan: 16 houses (the one at sketch 315,280 is left out: the real bank and that house overlapped), buildings step back from roads and keep 3 m apart (so some sit a few metres off their sketch spot; the JEEBOAK log gives each final spot), pads are levelled and neighbouring floors pulled to similar heights, and the hills fade into the riverbanks over 10 m.

**Shrunk 2026-11-12** (owner: "too large ... the buildings are too spread out. Can we shrink it by about 50%"): the layout is now 17.5 cm per sketch unit (`JeebOakHill::Unit`, 0.25 before), so distances are 70% and the town covers half the area (about 100 x 140 m); the building models keep their real size. At a straight 50% (12.5 cm) the real models could not fit between the roads (they overlapped each other and the roads, and the spacing pass threw shops across town). Other changes with it: roads 3.5 m wide (were 5), buildings step back 0.5 m from roads (was 1 m), the plaza's ring road is radius 64 units (11.2 m), the hills keep 75% of their rise (`HillRise`) so roads stay under about 12 degrees, the west road, north arc and southwest road are drawn on to the river bank, and the bridge and forts keep their old spots on the bank. Every building was settled by hand on an offline map (`OakHill.h` has the final sketch spots; the in-game spacing pass now only nudges a few by a metre or so). Moved well away from their sketch spots: the bank (north of the plaza, square-on), the house north of it (to the northeast), a house from the castle ruins' corner to east of the garden, and a few houses at the south edge. The southeast road starts a little further down the south road to make room for the store. Every building is kept. The units in the sections below are still sketch units; multiply by 0.175 for metres.

**Market stall (2026-11-12):** the new general store (`Art/Buildings/GeneralStore_B.glb` -> `SM_GeneralStore_300k`, 290k triangles, its own colours kept, 11 m wide) is a building of its own beside the shops ("Market Stall" in `Plots()`), west of the plaza on the north side of the west road, its open front south to the road, with barrels, crates, sacks, a rope coil and a hand cart behind and beside it (`AOakHillTown::BuildStoreClutter`, simple shapes, solid boxes). Two more stalls the same evening: the **Potion Stall** (`PotionShop_A.glb` -> `SM_PotionStall_300k`, its own colours, stretched to about 11 x 7.5 x 6.2 m: `ys` 1.5, `zs` 0.75 in `building_clean.py`; its back is open, so crates, barrels, sacks and a cart stand behind it as a back wall) south of the west road opposite the market stall, where a corner house stood (that house moved to the east edge, sketch 598,261), and the **Bank Stall** (`Bank_A.glb` repainted by `building_palette.py` profile `bankstall` to `Bank_A_town.glb` -> `SM_BankStall_300k`: slate roof to the town's clay tile, stone warmed, brass, gold and vault kept; 11 x 9.5 m) on the southeast road by the store, with clutter only beside it. No stall-sized spot is left within 40 m of the plaza; a smithy stall would need a house moved.

Approved by the owner on 2026-11-11: "close enough for now, tweak later". It was traced from `Docs/OakHillSketch.webp`, keeping the sketch's curving roads and the angles of its buildings. The owner rejected a first version with straight roads and square-on buildings, so keep those when changing it. The contours were added the same day and approved ("looks great"). Nothing here is built yet.

How it was planned: as a top-down drawing at true scale, traced from the owner's sketch and fitted to where the town really stands in the game. The river and its east branch were placed from `WorldMap.h`. Do it the same way for the other towns.

## Chosen models (from Meshy's free CC0 community library, see `Docs/MeshyCommunityLibrary.md`)
- **Store:** "Timbered Market House", https://www.meshy.ai/posts/019deaa3-2799-7726-a677-75fec8192529 (owner, 2026-11-11; first picked as the bank, then moved to the store the same day). Stone ground floor, timber and plaster upper floor, red tiles, a money-sack sign, a market awning, crates and barrels. About 1.8M faces, to be reduced to about 300k with Nanite. Downloaded 2026-11-11 as `Meshy_AI_Timbered_Market_House_20260502_texture_fbx.zip` (162 MB, in the owner's Downloads). Reduced from the FBX to `Art/Buildings/OakHillStore_300k.glb` (14 MB) the same day. The owner chose to keep its baked-in awning, crates, barrels and sign for now.

- **Made the same night (2026-11-11), same recipe as the bank** (a nano-banana-pro picture from the house, store and bank pictures, 9 credits; then `meshy_image_to_3d`, meshy-7.1 + meshy-7 at 4K, PBR, 30 credits; reduced to 300k). Task ids are in `Docs/TownBuildings.md`. Potion shop, town hall, smith, and the corner house (a second house design: the library's "Taverne 1" by janintibo, redrawn without its sign and extras).
- **Storybook cottage** (a third house design, owner 2026-11-13: "This looks cool you should add it"): "Storybook cottage v2" by kaesar3d (library, CC0, 1.84M faces), https://www.meshy.ai/posts/019e5c0f-fb24-7688-b231-5ebf24a74829. Red-tiled roof, a round stone tower with a conical roof and a balcony, half-timbered gables, an arched door up stone steps, a low stone garden wall. Downloaded as GLB (64 MB) to `Art/Buildings/StorybookCottage_Detailed.glb` (git-ignored), reduced to `StorybookCottage_300k.glb`; its wall lanterns, barrel, bench and flower pots are cut off in `building_clean.py` (owner's rule: a building is the shell).
- **Church:** "Emberstone Chapel" by METATIGER (library, CC0), `OakHillChurch_300k.glb`. **Well:** "Stone-Walled Well" by METATIGER (library, CC0), `OakHillWell_150k.glb`.
- All of them are repainted to the bank's colours and get softly glowing yellow windows (owner); see `Docs/TownBuildings.md`.
- **Bank:** made 2026-11-11 with Meshy image-to-image (nano-banana-pro, 9 credits), with the test house's picture and the store as references. First draw: Meshy task `01a1cdf7-5448-7126-8b75-f06b4d0a14f8`. A second pass (9 credits) darkened the wood to rich brown and took out a timber post that ran down through the stone wall: task `01a1cdfa-b3a0-7386-a51a-75f818a7b711`, the current picture, saved as `Art/Buildings/OakHillBank_picture.png`. Made into 3D the same day (30 credits): `meshy_image_to_3d` from that picture with meshy-7.1 standard geometry, the meshy-7 texture model at 4K, PBR on, image enhancement off, GLB only. Task `01a1cdfc-a27b-7007-862f-16330e6aff43`, saved as `Art/Buildings/OakHillBank_Detailed.glb` (104 MB, 2.71M triangles, git-ignored like the house's Detailed file). Checked from every side in Blender: the back, sides, roof and chimney are all there. The owner found the model's stone colder than the picture, so its stone (the grey, unsaturated parts of the colour texture) was tinted warmer, keeping its brightness: red x1.04, blue x0.88. Then it was reduced to `Art/Buildings/OakHillBank_300k.glb` (300k triangles, 24 MB) with `Tools/building_reduce.py`. Still to do: import it, check it in the game, then delete the big file. The checklist of every town's buildings is `Docs/TownBuildings.md`. It has a stone body, a cream plaster and timber gable, red tiles, a columned porch and barred windows. Free library options from the same day are in the scratchpad notes (Stone Mansion `01964c59-8168-7178-b420-322fc608b711` was the pick there).

## Coordinates
- Units below are SKETCH UNITS, the pixels of `Docs/OakHillSketch.webp` (540 x 800, north up).
- Metres from the plaza centre: east e = (px - 255) * Unit, north n = (410 - py) * Unit; Unit is 0.14 since 2026-11-13 (0.175 on 2026-11-12, 0.25 when this plan was drawn). The building and road spots listed below are the ORIGINAL plan's; `OakHill.h` holds the current ones.
- Game cm: X = OakHill.Pos.X + n*100, Y = OakHill.Pos.Y + e*100. OakHill.Pos = `JeebWorld::Towns()` "Oak Hill" = FromSketch(543, 875), about (-3550, 18290). North is +X, east is +Y.
- The town is about 100 x 140 m since the shrink (140 x 200 m as drawn). Rotations are degrees, clockwise as seen on the north-up map.

## Water (already in the game, `Rivers()` in WorldMap.h)
- The main river's centre is at about px 7 (e = -62 m), 14 m wide, running north to south down the west edge.
- It forks at about py 750 (n = -85 m). The east branch runs east-southeast along the south of town: about n = -102 m at e = 0, and n = -117 m at e = +50. So the town sits between two rivers.

## Roads (5 m wide = 20 sketch units; SVG paths in sketch units)
- **Ring road round the plaza:** a circle centred on (255,410), centreline radius 58.
- **West road to the bridge and on to Blackrock:** `M-150 410 L197 410`. Blackrock's centre is at almost the same X, 185 m west, so one straight road joins the two towns.
- **Bridge:** stone, a rectangle x -45..55, y 398..422 (25 m long, 6 m wide).
- **East road to the Town Hall door:** `M313 410 L400 410`
- **North road and its northeast branch past the ruins:** `M252 352 C 250 300 262 240 295 190 C 330 135 365 80 395 0`
- **North arc road:** `M20 180 C 70 140 150 135 220 150 C 255 158 280 172 300 185 C 360 190 450 220 545 295 L590 322`
- **Road behind the Bank, between it and the Town Hall, then southeast:** `M385 206 C 385 260 375 300 372 340 L372 494 C 372 560 440 610 545 640 L590 650`
- **South road to the Boxing Ring:** `M255 478 L255 574`
- **Southwest road (ends at the river fork):** `M250 505 C 215 560 150 620 20 700`
- **Southeast road to the Graveyard:** `M262 528 C 280 565 330 600 425 698`

## Buildings (centre px,py; size w x h in sketch units; rotation)
- **Plaza oak lawn:** a circle at (255,410), r 44, with the big oak in the middle.
- **Shops round the plaza:**
  - Smith (195,330) 64x44, -35
  - Bank (318,345) 64x44, 40
  - Store (310,485) 64x44, -40
  - Potion Shop (195,490) 64x44, 25
- **Town Hall** (430,410) 60x168, 0. Its door faces west onto the east road.
- **Garden** (500,440) 36x152, 0
- **Boxing Ring** (255,610) 64x72, 0
- **Castle Ruins** (240,95) 128x64, 20
- **Church** (490,150) 72x120, 15
- **Graveyard:** a circle at (470,735), r 60
- **Forts:** north (80,295) 44x104, 10; south (60,525) 44x104, 0
- **Watchtower:** a circle at (140,195), r 20
- **17 houses**, each 58x36 (14.4 x 9 m, the chosen 300k Nanite house drawn at 1.2x):
  - north: (100,100) -15; (405,125) 10; (230,195) 10; (205,250) 5; (330,230) 15; (315,280) 5; (430,265) 25; (500,320) 25
  - by the plaza: (130,372) 0
  - southwest: (120,470) 0; (140,540) 5; (75,600) 0; (150,680) 0
  - southeast: (340,548) 5; (380,600) 20; (335,680) 10; (490,565) 30

## Ground (metres above the river's surface, `JeebWorld::RiverLevel`)
- **2 m contour** (open, west and south): `M48 -20 C 45 200 45 500 60 690 C 90 760 250 775 380 790 C 470 805 540 820 600 835`
- **4 m contour** (open): `M70 -20 C 120 80 130 200 128 300 C 126 360 150 400 140 460 C 130 520 120 580 170 640 C 230 700 330 715 420 690 C 480 672 540 690 600 700`
- **6 m, round the plaza:** an ellipse centred on (255,405), rx 110, ry 100. The plaza's top is just over 6 m.
- **6 m, round the castle knoll:** an ellipse centred on (250,95), rx 140, ry 85.
- **6 m, the church's own rise:** an ellipse centred on (495,140), rx 65, ry 85, rotated 15.
- **8 m, the castle knoll:** an ellipse centred on (240,95), rx 95, ry 55, rotated 20. The knoll's top is about 9 m.
- **Between the contours:**
  - The north houses sit in a saddle of about 5 m between the plaza and the castle.
  - The forts and bridge are low (2-3 m), on a bank above the water.
  - The ring and the south houses are at about 4-5 m. The graveyard slopes down toward the south river.
  - The steepest street is about 5 degrees, and every street stays under 8 degrees.
  - East of the drawing, the land blends back into the ordinary lowland (`JeebWorld::LandHeight`).

## Open-air market store candidates (2026-11-12)
The owner wants the four shops redone as open-air market stalls, general store first. The first pass (three free library stalls) was rejected as too small and bare and deleted. Second pass: generated on Meshy (text to 3D, latest model, 300k triangles target, PBR texture). Full size in `Art/Buildings` (not reduced or imported), front previews in `Docs/GeneralStore/`. The old store stays untouched.
- A `GeneralStore_A.glb`: cream canvas peaked tent on poles over a counter booth, pots, pans and tools hanging at the sides, barrels, sacks, baskets in front. Preview task 01a1d25b-4e9d-777e-a8f1-2f28246db2fb, textured task 01a1d25f-9568-77bc-85c7-f3fbc035d1c6.
- B `GeneralStore_B.glb`: timber-framed trading post with a red and cream roof, back shelves full of goods, front counter, barrels, crates and jars. Preview task 01a1d25b-575c-77bd-855e-3fa24abe5820, textured task 01a1d25f-9e40-732b-8d8b-028ff97f9972.
### Bank (market stall redesign, 2026-11-12)

Owner: focus on the structure, less like a tent. Text to 3D, latest model, triangle, 300k target, remesh; three untextured drafts, the best one textured with PBR (about 70 credits in all). Full size in `Art/Buildings` (not reduced or imported), previews in `Docs/Bank/`.
- A `Bank_A.glb`: open-fronted bank on a cut-stone base, four round pillars with brass bands, dark slate roof with a front gable and a blank sign board, a curved oak teller counter with a brass scrollwork grille, a round iron vault door with brass trim in the back stone wall, gold bars and coins on the counter, chests and chairs inside. Preview task 01a1d266-d175-7332-8196-193b9b502729, textured task 01a1d269-1e1f-7728-bff7-de5a59b0bac6.
- Rejected drafts (untextured, pictures `Docs/Bank/draft_B.png`, `draft_C.png`): B a closed two-part stone house with a tile roof (not open), task 01a1d266-dae6-744c-8d43-0f8b07544afc; C broken (a lone door and wall), task 01a1d266-e2c0-716a-8277-5b21e6ec070e.
- A2 `Bank_A2.glb` (owner chose A, 'Recolour it'): the same model retextured with a red and cream tiled roof like General Store B and a cream and grey stone base. The pillars stay brown with brass bands, the counter top and floor came out red, and the sign has no coin. Textured task 01a1d26b-73b3-77d5-9ff6-164693c15f09, picture `Docs/Bank/Bank_A2_front.png`.
- A3 `Bank_A3.glb` (owner: pillars stone, roof too red): the same model, third texture pass. Cream stone pillars and base, reddish oak counter, roof asked as the town tile (#6b3426) but came out a brighter orange terracotta; `building_palette.py`'s repaint can pull it to the town tile. Textured task 01a1d27b-2bd1-756d-85a2-f8fb1ca63d94, picture `Docs/Bank/Bank_A3_front.png`.

**Bank decision (owner, 2026-11-12):** use the first one, `Art/Buildings/Bank_A.glb` (task 01a1d269-1e1f-7728-bff7-de5a59b0bac6), and fix its colours in the game (the town palette repaint and material tweaks). A2 and A3 stay on disk but are not used.

### Potion shop (market stall redesign, 2026-11-12)

Text to 3D, latest model, triangle, 300k target, remesh; four untextured drafts (20 credits each), the only whole one textured with PBR (10): 90 credits. Full size in `Art/Buildings` (not reduced or imported), pictures in `Docs/PotionShop/` (Meshy's front preview, plus Blender three-quarter and straight-front renders of the textured model).
- A `PotionShop_A.glb`: an open timber stall under a dark shingle roof with a small front gable, a long counter of bottles, a flask, a mortar, a basket of herbs and red fruit, a black cauldron at the left end, red spotted mushrooms and green herb bundles hanging from the front beam, a small shelf unit in the middle at the back, a white skull hung under the gable. Preview task 01a1d27d-c659-75f0-b86d-27cdeb88e5d1, textured task 01a1d283-66f7-71ec-aa50-9269cb74aabf. Flaws: shallow (1.9 x 0.86 x 1.43, so about 5 m deep at 11 m wide and 8 m tall), the back is open to the sky except the middle shelf, the posts are knobbly, the goods are fewer than asked, and the roof asked as muted plum (#5b3a52) came out a dark grey-brown.
- Rejected drafts (untextured): B (`draft_B.png`) loose props and a floating chandelier, no building, task 01a1d27d-8a7d-708d-af1e-a57e4f86a4cd; C (`draft_C.png`) a closed cottage with a pillared porch and a front door, task 01a1d27e-274a-7123-ba73-38fe6b6a62a5; D (`draft_D.png`) only a cauldron and a hanging ball, task 01a1d281-6730-739d-a3e4-83869030b282.
- Lesson: a long list of small goods tips the latest model into making only the goods (B and D, which both put the alchemy list ahead of or level with the structure). A, which put "open front so you can see inside, sturdy timber frame with thick wooden pillars holding heavy beams, a proper sloped tiled roof" first, kept a building. C's "apothecary building ... front gable" gave a closed house.
